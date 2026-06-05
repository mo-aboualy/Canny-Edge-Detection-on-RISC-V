#include "sobel.h"
#include <cstdlib>
#include <cmath>

void gradient_free(GradientImage& g) {
    free(g.gx);
    free(g.gy);
    free(g.magnitude);
    g.gx = nullptr;
    g.gy = nullptr;
    g.magnitude = nullptr;
}

void sobel_3x3(const Image& input, GradientImage& output) {
    const uint32_t W = input.width;
    const uint32_t H = input.height;

    size_t count       = (size_t)W * H;
    size_t aligned_cnt = (count + 63) & ~63;

    output.width     = W;
    output.height    = H;
    output.gx        = (int16_t*)aligned_alloc(64, aligned_cnt * sizeof(int16_t));
    output.gy        = (int16_t*)aligned_alloc(64, aligned_cnt * sizeof(int16_t));
    output.magnitude = (uint16_t*)aligned_alloc(64, aligned_cnt * sizeof(uint16_t));

    // Sobel kernels
    // Gx: [-1, 0, +1 / -2, 0, +2 / -1, 0, +1]
    // Gy: [-1,-2, -1 /  0, 0,  0 / +1,+2, +1]

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            if (x == 0 || x == W-1 || y == 0 || y == H-1) {
                output.gx[y*W+x]        = 0;
                output.gy[y*W+x]        = 0;
                output.magnitude[y*W+x] = 0;
                continue;
            }

            int16_t gx =
                -1 * input.pixels[(y-1)*W+(x-1)] +
                +1 * input.pixels[(y-1)*W+(x+1)] +
                -2 * input.pixels[(y  )*W+(x-1)] +
                +2 * input.pixels[(y  )*W+(x+1)] +
                -1 * input.pixels[(y+1)*W+(x-1)] +
                +1 * input.pixels[(y+1)*W+(x+1)];

            int16_t gy =
                -1 * input.pixels[(y-1)*W+(x-1)] +
                -2 * input.pixels[(y-1)*W+(x  )] +
                -1 * input.pixels[(y-1)*W+(x+1)] +
                +1 * input.pixels[(y+1)*W+(x-1)] +
                +2 * input.pixels[(y+1)*W+(x  )] +
                +1 * input.pixels[(y+1)*W+(x+1)];

            output.gx[y*W+x] = gx;
            output.gy[y*W+x] = gy;

            float mag = sqrtf((float)gx*gx + (float)gy*gy);
            output.magnitude[y*W+x] = (uint16_t)(mag > 255.0f ? 255.0f : mag);
        }
    }
}