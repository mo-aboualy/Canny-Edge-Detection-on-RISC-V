/**
 * @file sobel_rvv.cpp
 * @brief RVV-accelerated Sobel 3x3 gradient computation — Phase 6.
 *
 * ── What is vectorised ────────────────────────────────────────────────
 * The inner loop across output columns is strip-mined with RVV intrinsics.
 * For every chunk of vl output pixels we load 8 shifted neighbourhoods
 * (tl, t_, tr, ml, mr, bl, b_, br) and accumulate Gx and Gy via six
 * widening signed×unsigned multiply-accumulates each.
 *
 * Magnitude is computed scalar (sqrtf), matching sobel.cpp bit-for-bit.
 *
 * ── Boundary handling ─────────────────────────────────────────────────
 * Border rows (y=0, y=H-1) and border columns (x=0, x=W-1) have
 * Gx=Gy=magnitude=0, written before the RVV loop. The RVV strip-mine
 * covers x in [1, W-2] only.
 *
 * ── LMUL choice ───────────────────────────────────────────────────────
 * Base LMUL=1 (e8m1 loads). After one widening MAC the accumulator is
 * i16m2. We hold 8 u8m1 neighbourhood loads and 2 i16m2 accumulators
 * live simultaneously — 8x1 + 2x2 = 12 logical register groups — well
 * within the 32 available at LMUL=1.
 *
 * ── Vector-length agnosticism ─────────────────────────────────────────
 * Every loop iteration calls vsetvl_e8m1 at the top. The same binary
 * produces identical output at VLEN=128, 256, and 512.
 */

#include "sobel_rvv.h"
#include "sobel.h"       /* gradient_free, GradientImage, sobel_3x3 fallback */
#include "image_io.h"

#include <cstdlib>
#include <cmath>

#ifdef __riscv_vector
#include <riscv_vector.h>
#endif

/* Round n up to the next 64-byte boundary for aligned_alloc. */
static inline size_t align64(size_t n) {
    return (n + 63u) & ~static_cast<size_t>(63u);
}

void sobel_3x3_rvv(const Image& input, GradientImage& output) {

#ifndef __riscv_vector
    /*
     * Host / GoogleTest build: no RVV available.
     * Delegate to the verified scalar reference so every host test that
     * calls sobel_3x3_rvv() compiles and passes without change.
     */
    sobel_3x3(input, output);
    return;

#else  /* __riscv_vector */

    const int32_t W = static_cast<int32_t>(input.width);
    const int32_t H = static_cast<int32_t>(input.height);
    const size_t  N = static_cast<size_t>(W) * static_cast<size_t>(H);

    output.width     = input.width;
    output.height    = input.height;
    output.gx        = (int16_t* )aligned_alloc(64, align64(N * sizeof(int16_t)));
    output.gy        = (int16_t* )aligned_alloc(64, align64(N * sizeof(int16_t)));
    output.magnitude = (uint16_t*)aligned_alloc(64, align64(N * sizeof(uint16_t)));

    if (!output.gx || !output.gy || !output.magnitude) {
        /* Allocation failure: zero everything and return. */
        free(output.gx);
        free(output.gy);
        free(output.magnitude);
        output.gx = output.gy = nullptr;
        output.magnitude = nullptr;
        return;
    }

    const uint8_t* px = input.pixels;

    /* ── Zero the top border row ──────────────────────────────────── */
    for (int32_t x = 0; x < W; ++x) {
        output.gx[x]        = 0;
        output.gy[x]        = 0;
        output.magnitude[x] = 0;
    }

    /* ── Zero the bottom border row ───────────────────────────────── */
    for (int32_t x = 0; x < W; ++x) {
        const int32_t base      = (H - 1) * W + x;
        output.gx[base]        = 0;
        output.gy[base]        = 0;
        output.magnitude[base] = 0;
    }

    /* ── Process interior rows ────────────────────────────────────── */
    for (int32_t y = 1; y < H - 1; ++y) {

        const uint8_t* row_t = px + (y - 1) * W;  /* row above */
        const uint8_t* row_m = px + (y    ) * W;  /* current row */
        const uint8_t* row_b = px + (y + 1) * W;  /* row below */

        /* Zero left border column for this row. */
        output.gx       [y * W + 0]     = 0;
        output.gy       [y * W + 0]     = 0;
        output.magnitude[y * W + 0]     = 0;

        /* Zero right border column for this row. */
        output.gx       [y * W + W - 1] = 0;
        output.gy       [y * W + W - 1] = 0;
        output.magnitude[y * W + W - 1] = 0;

        /*
         * ── RVV strip-mine: compute Gx and Gy for x in [1, W-2] ───
         *
         * Sobel-X kernel:            Sobel-Y kernel:
         *   -1  0  +1                  -1  -2  -1
         *   -2  0  +2                   0   0   0
         *   -1  0  +1                  +1  +2  +1
         *
         * Centre column contributes 0 to Gx; centre row contributes 0
         * to Gy. We load all 8 non-centre neighbours and share t_/b_
         * between Gx (unused) and Gy.
         */
        const int32_t x_end = W - 1;   /* exclusive upper bound */
        int32_t x = 1;

        while (x < x_end) {

            /*
             * vsetvl_e8m1:
             *   Sets the hardware VL CSR and returns vl — how many
             *   uint8 elements fit in one LMUL=1 vector register group.
             *   At VLEN=128 vl <= 16; at VLEN=512 vl <= 64.
             *   Casting to size_t before the call avoids signed/unsigned
             *   comparison warnings from the remaining count.
             */
            const size_t vl =
                __riscv_vsetvl_e8m1(static_cast<size_t>(x_end - x));

            /*
             * Load 8 u8m1 neighbourhood vectors.
             *   vle8_v_u8m1: unit-stride load of vl bytes into u8m1.
             *   LMUL=1 matches vsetvl above; 8 u8m1 registers used.
             *   The pointer offsets -1/+1 shift the neighbourhood window
             *   one column left or right relative to output column x.
             */
            vuint8m1_t tl = __riscv_vle8_v_u8m1(row_t + x - 1, vl); /* top-left   */
            vuint8m1_t t_ = __riscv_vle8_v_u8m1(row_t + x,     vl); /* top-centre */
            vuint8m1_t tr = __riscv_vle8_v_u8m1(row_t + x + 1, vl); /* top-right  */
            vuint8m1_t ml = __riscv_vle8_v_u8m1(row_m + x - 1, vl); /* mid-left   */
            vuint8m1_t mr = __riscv_vle8_v_u8m1(row_m + x + 1, vl); /* mid-right  */
            vuint8m1_t bl = __riscv_vle8_v_u8m1(row_b + x - 1, vl); /* bot-left   */
            vuint8m1_t b_ = __riscv_vle8_v_u8m1(row_b + x,     vl); /* bot-centre */
            vuint8m1_t br = __riscv_vle8_v_u8m1(row_b + x + 1, vl); /* bot-right  */

            /*
             * ── Accumulate Gx ─────────────────────────────────────
             * Gx = -1*tl + 1*tr + -2*ml + 2*mr + -1*bl + 1*br
             *
             * vmv_v_x_i16m2: broadcast scalar 0 to vl i16 lanes.
             *   LMUL=2 because vwmaccsu doubles the element width from
             *   u8 (1 byte) to i16 (2 bytes), so the result needs
             *   LMUL=2. Same vl element count as the u8m1 loads.
             *
             * __riscv_vwmaccsu_vx_i16m2(acc, scalar_i8, vector_u8, vl):
             *   acc[i] += sign_extend(scalar_i8) * zero_extend(vector_u8[i])
             *   "su" = Scalar signed, vector Unsigned.
             *   The result is widened to i16 before accumulation.
             *   Max |Gx| = 4*255 = 1020, fits in int16 (max 32767). OK.
             *   All scalar weights are cast to int8_t explicitly to
             *   satisfy the intrinsic signature and prevent sign/width
             *   promotion surprises with plain integer literals.
             */
            vint16m2_t gx = __riscv_vmv_v_x_i16m2(0, vl);
            gx = __riscv_vwmaccsu_vx_i16m2(gx, (int8_t)(-1), tl, vl);
            gx = __riscv_vwmaccsu_vx_i16m2(gx, (int8_t)( 1), tr, vl);
            gx = __riscv_vwmaccsu_vx_i16m2(gx, (int8_t)(-2), ml, vl);
            gx = __riscv_vwmaccsu_vx_i16m2(gx, (int8_t)( 2), mr, vl);
            gx = __riscv_vwmaccsu_vx_i16m2(gx, (int8_t)(-1), bl, vl);
            gx = __riscv_vwmaccsu_vx_i16m2(gx, (int8_t)( 1), br, vl);

            /*
             * ── Accumulate Gy ─────────────────────────────────────
             * Gy = -1*tl + -2*t_ + -1*tr + 1*bl + 2*b_ + 1*br
             *
             * Same reasoning as Gx: vmv_v_x_i16m2 to zero-init, then
             * six vwmaccsu_vx calls with explicit int8_t scalar casts.
             * t_ and b_ are re-used from the Gx loads above; no
             * additional memory traffic.
             */
            vint16m2_t gy = __riscv_vmv_v_x_i16m2(0, vl);
            gy = __riscv_vwmaccsu_vx_i16m2(gy, (int8_t)(-1), tl, vl);
            gy = __riscv_vwmaccsu_vx_i16m2(gy, (int8_t)(-2), t_, vl);
            gy = __riscv_vwmaccsu_vx_i16m2(gy, (int8_t)(-1), tr, vl);
            gy = __riscv_vwmaccsu_vx_i16m2(gy, (int8_t)( 1), bl, vl);
            gy = __riscv_vwmaccsu_vx_i16m2(gy, (int8_t)( 2), b_, vl);
            gy = __riscv_vwmaccsu_vx_i16m2(gy, (int8_t)( 1), br, vl);

            /*
             * ── Store Gx and Gy ───────────────────────────────────
             * vse16_v_i16m2: store vl int16 elements to memory.
             *   vl here is the element count (set for e8m1 width), but
             *   element count is the same regardless of element width —
             *   we computed vl i16 results from vl u8 inputs, so storing
             *   vl i16 elements is correct.
             *   LMUL=2 matches the i16m2 accumulator type.
             *   Pointer advances by vl i16 values = vl*2 bytes.
             */
            __riscv_vse16_v_i16m2(output.gx + y * W + x, gx, vl);
            __riscv_vse16_v_i16m2(output.gy + y * W + x, gy, vl);

            x += static_cast<int32_t>(vl);
        }

        /*
         * ── Scalar magnitude for this row ─────────────────────────
         * Gx and Gy are fully written for row y. Compute magnitude
         * using the same sqrtf formula as scalar sobel.cpp to produce
         * bit-identical output.magnitude values for the equivalence test.
         *
         * Note: the downstream pipeline computes its working magnitude
         * via gradient_magnitude_l1() over the Gx/Gy buffers. This
         * sqrtf magnitude fills output.magnitude only to satisfy the
         * GradientImage contract from sobel.h.
         */
        for (int32_t xi = 1; xi < W - 1; ++xi) {
            const int16_t gxv = output.gx[y * W + xi];
            const int16_t gyv = output.gy[y * W + xi];
            const float   mag = sqrtf((float)gxv * gxv + (float)gyv * gyv);
            output.magnitude[y * W + xi] =
                (uint16_t)(mag > 255.0f ? 255.0f : mag);
        }
    }

#endif /* __riscv_vector */
}