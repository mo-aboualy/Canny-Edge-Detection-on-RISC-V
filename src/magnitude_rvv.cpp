#include "magnitude_rvv.h"
#include "magnitude.h"   // Scalar baseline fallback reference

#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef __riscv_vector
#include <riscv_vector.h>
#endif

static constexpr size_t ALIGNMENT = 64;

static inline size_t align64(size_t n) {
    return (n + 63) & ~(ALIGNMENT - 1);
}

void gradient_magnitude_l1_rvv(const int16_t* gx, const int16_t* gy,
                                uint8_t* magnitude, int width, int height) {
#ifndef __riscv_vector
    // Phase 2.1 / Phase 6.1 baseline preservation: 
    // If __riscv_vector is not defined, we fall back to the host scalar code seamlessly.
    // This allows the native GoogleTest framework to build without modification.
    gradient_magnitude_l1(gx, gy, magnitude, width, height);
    return;
#else
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Cache buffer allocation (Phase 6.5 optimization)
    // Allocates an aligned buffer to hold the un-normalized int32_t values.
    // Aligned memory prevents faults and optimizes spatial data transfers.
    int32_t* mag_cache = static_cast<int32_t*>(
        aligned_alloc(ALIGNMENT, align64(count * sizeof(int32_t))));
    if (!mag_cache) {
        std::memset(magnitude, 0, count);
        return;
    }

    // --- Phase 6.5 Pass 1: Compute Abs, Widening Add, & Global Max Reduction ---
    
    // (1) Operation: Broadcasts a scalar 0 to all lanes of an i32m1 vector to seed the reduction loop.
    // (2) LMUL Choice: LMUL=1 is selected because it only stores a single running scalar-destination variable.
    // (3) VLEN Agnosticism: The seed uses a fixed count of 1 element, remaining safe across 128, 256, or 512-bit setups.
    vint32m1_t running_max_seed = __riscv_vmv_v_x_i32m1(0, 1);

    size_t i = 0;
    while (i < count) {
        // (1) Operation: Standard dynamic vector length calculation for strip-mining tail cases.
        // (2) LMUL Choice: LMUL=1 is used as the base configuration for our 16-bit input loads.
        // (3) VLEN Agnosticism: 'vl' dynamically captures the hardware capacity; crucial for non-power-of-two tail-end handling.
        size_t vl = __riscv_vsetvl_e16m1(count - i);

        // (1) Operation: Loads consecutive signed 16-bit gradient components from Gx and Gy buffers.
        // (2) LMUL Choice: LMUL=1 is selected to maintain maximum register availability for operations.
        // (3) VLEN Agnosticism: Vector loading expands dynamically to read exactly 'vl' elements per loop step.
        vint16m1_t vgx = __riscv_vle16_v_i16m1(gx + i, vl);
        vint16m1_t vgy = __riscv_vle16_v_i16m1(gy + i, vl);

        // (1) Operation: Negates Gx components using vector reverse subtraction: vd = 0 - vs2.
        // (2) LMUL Choice: LMUL=1 matches the input vector configuration.
        // (3) VLEN Agnosticism: Agnostic element handling scaled seamlessly to the active 'vl'.
        vint16m1_t neg_gx = __riscv_vrsub_vx_i16m1(vgx, 0, vl);

        // (1) Operation: Computes the absolute value of Gx by taking the element-wise maximum of x and -x.
        // (2) LMUL Choice: LMUL=1 avoids logical register grouping and minimizes register pressure.
        // (3) VLEN Agnosticism: Works identically regardless of whether the physical hardware register is 128 or 512 bits wide.
        vint16m1_t abs_gx = __riscv_vmax_vv_i16m1(vgx, neg_gx, vl);

        // (1) Operation: Negates Gy components using vector reverse subtraction: vd = 0 - vs2.
        // (2) LMUL Choice: LMUL=1 matches the input vector configuration.
        // (3) VLEN Agnosticism: Agnostic element handling scaled seamlessly to the active 'vl'.
        vint16m1_t neg_gy = __riscv_vrsub_vx_i16m1(vgy, 0, vl);

        // (1) Operation: Computes the absolute value of Gy by taking the element-wise maximum of y and -y.
        // (2) LMUL Choice: LMUL=1 avoids logical register grouping and minimizes register pressure.
        // (3) VLEN Agnosticism: Works identically regardless of whether the physical hardware register is 128 or 512 bits wide.
        vint16m1_t abs_gy = __riscv_vmax_vv_i16m1(vgy, neg_gy, vl);

        // (1) Operation: Widening addition of absolute values (i16m1 + i16m1 -> i32m2) to avoid intermediate overflow.
        // (2) LMUL Choice: Automatically forces an LMUL expansion from m1 to m2 to maintain mathematical precision bounds.
        // (3) VLEN Agnosticism: Tracking register pairing rules correctly safeguards dynamic type width conversions under all VLEN variants.
        vint32m2_t vmag = __riscv_vwadd_vv_i32m2(abs_gx, abs_gy, vl);

        // (1) Operation: Caches the calculated 32-bit gradient sums into our temporary staging buffer.
        // (2) LMUL Choice: LMUL=2 matches the widened state output from our arithmetic evaluation.
        // (3) VLEN Agnosticism: Elements are written continuously according to the runtime computed 'vl'.
        __riscv_vse32_v_i32m2(mag_cache + i, vmag, vl);

        // (1) Operation: Vector reduction folding. Extracts the local maxima across all active lanes and aggregates them with the seed.
        // (2) LMUL Choice: Consumes the i32m2 collection and narrows the scalar result back into an isolated single-lane i32m1.
        // (3) VLEN Agnosticism: Correctly isolates hardware-independent global values across non-power-of-two boundaries.
        running_max_seed = __riscv_vredmax_vs_i32m2_i32m1(vmag, running_max_seed, vl);

        i += vl;
    }

    // (1) Operation: Moves lane 0 of the vector register to a standard scalar CPU register.
    // (2) LMUL Choice: Pulls explicitly from the target i32m1 structural result layout.
    // (3) VLEN Agnosticism: Uniformly extracts the first lane, ignoring hardware width parameters.
    const int32_t max_mag = __riscv_vmv_x_s_i32m1_i32(running_max_seed);

    // --- Phase 6.5 Pass 2: Vectorized Normalization (Fixed-Point Reciprocal Multiplications) ---
    if (max_mag == 0) {
        std::memset(magnitude, 0, count);
    } else {
        // High-performance replacement for hardware division loops (Amdahl's Law optimization).
        // Precalculates a 24-bit fixed point multiplier reciprocal on the scalar unit once.
        uint64_t numer = static_cast<uint64_t>(255) << 24;
        uint32_t recip_multiplier = static_cast<uint32_t>(numer / static_cast<uint32_t>(max_mag));

        size_t idx = 0;
        while (idx < count) {
            // (1) Operation: Drive the loop with SEW=8 and LMUL=1 to match the output destination format.
            // (2) LMUL Choice: LMUL=1 establishes the baseline for the entire structural narrowing chain.
            // (3) VLEN Agnosticism: Automatically handles non-power-of-two tail elements seamlessly.
            size_t vl = __riscv_vsetvl_e8m1(count - idx);

            // (1) Operation: Loads un-normalized 32-bit magnitude totals from our staging buffer as unsigned integers.
            // (2) LMUL Choice: Driven by 'vl' at e8m1, a 32-bit load scales up to an LMUL of m4.
            // (3) VLEN Agnosticism: Loads exactly 'vl' elements regardless of the underlying hardware width.
            vuint32m4_t vmag_cached = __riscv_vle32_v_u32m4(reinterpret_cast<const uint32_t*>(mag_cache + idx), vl);

            // (1) Operation: Unsigned widening vector multiplication (32-bit vector * 32-bit scalar -> 64-bit vector).
            // (2) LMUL Choice: Widens from m4 to m8 to completely eliminate risks of mathematical overflow.
            // (3) VLEN Agnosticism: Adheres to strict register pairing constraints across any hardware VLEN profile.
            vuint64m8_t vscaled = __riscv_vwmulu_vx_u64m8(vmag_cached, recip_multiplier, vl);

            // (1) Operation: Applies a logical vector shift right by 24 positions to restore normal scaling range.
            // (2) LMUL Choice: LMUL=8 matches the output width of the multiplication stage.
            vuint64m8_t vnorm64 = __riscv_vsrl_vx_u64m8(vscaled, 24, vl);

            // (1) Operation: Narrows the data downwards from 64-bit back into standard 32-bit unsigned integer ranges.
            // (2) LMUL Choice: Halves the configuration footprint from LMUL=8 back to LMUL=4.
            vuint32m4_t vnorm32 = __riscv_vncvt_x_x_w_u32m4(vnorm64, vl);

            // (1) Operation: Narrows the data downwards a second time from 32-bit into 16-bit unsigned spaces.
            // (2) LMUL Choice: Halves the configuration footprint from LMUL=4 back to LMUL=2.
            vuint16m2_t vnorm16 = __riscv_vncvt_x_x_w_u16m2(vnorm32, vl);

            // (1) Operation: Truncates, saturates, and clamps 16-bit values down into final 8-bit unsigned pixels.
            // (2) LMUL Choice: Narrows from LMUL=2 to an optimized single logical output register (LMUL=1).
            vuint8m1_t vout_pixels = __riscv_vnclipu_wx_u8m1(vnorm16, 0, vl);

            // (1) Operation: Stores the normalized 8-bit image pixels straight into the final destination image matrix buffer.
            // (2) LMUL Choice: LMUL=1 is selected to complete the output phase.
            // (3) VLEN Agnosticism: Safely writes structural outputs to non-power-of-two boundaries without memory corruptions.
            __riscv_vse8_v_u8m1(magnitude + idx, vout_pixels, vl);

            idx += vl;
        }
    }

    std::free(mag_cache);
#endif // __riscv_vector
}