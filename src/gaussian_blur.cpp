#include "gaussian_blur.h"
#include <cstdint>
#include <cstdlib>
#include <string.h>

/**
 * Version 1: WITH boundary check.
 * vec_report: "not vectorized: control flow in loop" <- EXPECTED FAIL
 */
void gaussian_blur_5x5(const Image& in, Image& out) {
    const int32_t W = in.width;
    const int32_t H = in.height;
    static const int32_t K[5][5] = {
        {1,  4,  7,  4, 1},
        {4, 16, 26, 16, 4},
        {7, 26, 41, 26, 7},
        {4, 16, 26, 16, 4},
        {1,  4,  7,  4, 1}
    };
    for (int32_t y = 0; y < H; ++y) {
        for (int32_t x = 0; x < W; ++x) {
            int32_t acc = 0;
            for (int32_t ky = -2; ky <= 2; ++ky) {
                for (int32_t kx = -2; kx <= 2; ++kx) {
                    int32_t cy = y + ky;
                    int32_t cx = x + kx;
                    if (cx >= 0 && cx < W && cy >= 0 && cy < H) {
                        acc += (int32_t)in.pixels[cy * W + cx]
                               * K[ky+2][kx+2];
                    }
                }
            }
            out.pixels[y * W + x] = (uint8_t)(acc / 273);
        }
    }
}

/**
 * Version 2: fully unrolled kernel, vectorizable x-loop.
 *
 * WHY THIS WORKS:
 * Every kernel coefficient is a COMPILE-TIME CONSTANT literal.
 * The compiler can emit: vadd(src[x], 7) for all x at once.
 * No variable k to load, no loop-carried dependency on k.
 *
 * Structure:
 *   for y:                          <- row loop
 *     for x = 0..W: acc[x] = ...   <- VECTORIZED: 512 independent MACs
 *     for x = 0..W: out[x] = ...   <- VECTORIZED: normalize
 *
 * vec_report: "vectorized 512 iterations" <- EXPECTED SUCCESS
 */
static void convolve_padded(const uint8_t* __restrict__ pad,
                             uint8_t*       __restrict__ out,
                             int32_t W, int32_t H, int32_t PW) {
    for (int32_t y = 0; y < H; ++y) {
        // Each row pointer — computed once per kernel row
        const uint8_t* r0 = pad + (y + 0) * PW;
        const uint8_t* r1 = pad + (y + 1) * PW;
        const uint8_t* r2 = pad + (y + 2) * PW;
        const uint8_t* r3 = pad + (y + 3) * PW;
        const uint8_t* r4 = pad + (y + 4) * PW;
        uint8_t* dst = out + y * W;

        // Fully unrolled 5x5 kernel — all coefficients are literals
        // Compiler sees: acc[x] = r0[x+0]*1 + r0[x+1]*4 + ... (pure constants)
        // No variable loads, no aliasing → vectorizes the x loop
        for (int32_t x = 0; x < W; ++x) {
            int32_t acc =
                // Row 0: {1, 4, 7, 4, 1}
                (int32_t)r0[x+0] *  1 + (int32_t)r0[x+1] *  4 +
                (int32_t)r0[x+2] *  7 + (int32_t)r0[x+3] *  4 +
                (int32_t)r0[x+4] *  1 +
                // Row 1: {4, 16, 26, 16, 4}
                (int32_t)r1[x+0] *  4 + (int32_t)r1[x+1] * 16 +
                (int32_t)r1[x+2] * 26 + (int32_t)r1[x+3] * 16 +
                (int32_t)r1[x+4] *  4 +
                // Row 2: {7, 26, 41, 26, 7}
                (int32_t)r2[x+0] *  7 + (int32_t)r2[x+1] * 26 +
                (int32_t)r2[x+2] * 41 + (int32_t)r2[x+3] * 26 +
                (int32_t)r2[x+4] *  7 +
                // Row 3: {4, 16, 26, 16, 4}
                (int32_t)r3[x+0] *  4 + (int32_t)r3[x+1] * 16 +
                (int32_t)r3[x+2] * 26 + (int32_t)r3[x+3] * 16 +
                (int32_t)r3[x+4] *  4 +
                // Row 4: {1, 4, 7, 4, 1}
                (int32_t)r4[x+0] *  1 + (int32_t)r4[x+1] *  4 +
                (int32_t)r4[x+2] *  7 + (int32_t)r4[x+3] *  4 +
                (int32_t)r4[x+4] *  1;

            dst[x] = (uint8_t)(acc / 273);
        }
    }
}

void gaussian_blur_padded(const Image& in, Image& out) {
    const int32_t W  = in.width;
    const int32_t H  = in.height;
    const int32_t PW = W + 4;
    const int32_t PH = H + 4;

    uint8_t* pad = (uint8_t*)calloc(PW * PH, 1);
    for (int32_t r = 0; r < H; r++)
        memcpy(pad + (r + 2) * PW + 2, in.pixels + r * W, W);

    convolve_padded(pad, out.pixels, W, H, PW);
    free(pad);
}
