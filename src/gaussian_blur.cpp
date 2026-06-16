#include "gaussian_blur.h"
#include <cstdint>
#include <cstdlib>
#include <string.h>

static const int32_t KERNEL[5][5] = {
    {1,  4,  7,  4, 1},
    {4, 16, 26, 16, 4},
    {7, 26, 41, 26, 7},
    {4, 16, 26, 16, 4},
    {1,  4,  7,  4, 1}
};
static const int32_t KERNEL_SUM = 273;

/**
 * Version 1: WITH boundary check.
 * if-statement inside inner loop = control flow = blocks vectorization.
 * vec_report will say: "not vectorized: control flow in loop"
 */
void gaussian_blur_5x5(const Image& in, Image& out) {
    const int32_t W = in.width;
    const int32_t H = in.height;
    for (int32_t y = 0; y < H; ++y) {
        for (int32_t x = 0; x < W; ++x) {
            int32_t acc = 0;
            for (int32_t ky = -2; ky <= 2; ++ky) {
                for (int32_t kx = -2; kx <= 2; ++kx) {
                    int32_t cy = y + ky;
                    int32_t cx = x + kx;
                    if (cx >= 0 && cx < W && cy >= 0 && cy < H) {
                        acc += (int32_t)in.pixels[cy * W + cx]
                               * KERNEL[ky + 2][kx + 2];
                    }
                }
            }
            out.pixels[y * W + x] = (uint8_t)(acc / KERNEL_SUM);
        }
    }
}

/**
 * Version 2: WITH pre-padding, NO boundary check.
 * Branch-free inner loop. Compiler attempts vectorization but
 * fails due to 2D strided access pattern.
 * vec_report will say: "not vectorized: complicated access pattern"
 */
void gaussian_blur_padded(const Image& in, Image& out) {
    const int32_t W  = in.width;
    const int32_t H  = in.height;
    const int32_t PW = W + 4;
    const int32_t PH = H + 4;

    uint8_t* pad = (uint8_t*)calloc(PW * PH, 1);
    for (int32_t r = 0; r < H; r++)
        memcpy(pad + (r + 2) * PW + 2, in.pixels + r * W, W);

    for (int32_t y = 0; y < H; ++y) {
        for (int32_t x = 0; x < W; ++x) {
            int32_t acc = 0;
            for (int32_t ky = 0; ky < 5; ++ky)
                for (int32_t kx = 0; kx < 5; ++kx)
                    acc += (int32_t)pad[(y + ky) * PW + (x + kx)]
                           * KERNEL[ky][kx];
            out.pixels[y * W + x] = (uint8_t)(acc / KERNEL_SUM);
        }
    }
    free(pad);
}
