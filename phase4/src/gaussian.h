#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const int16_t GAUSS[5][5] = {
    { 2,  4,  5,  4,  2},
    { 4,  9, 12,  9,  4},
    { 5, 12, 15, 12,  5},
    { 4,  9, 12,  9,  4},
    { 2,  4,  5,  4,  2}
};

// Version 1: WITH boundary check (compiler cannot vectorize)
void gaussian_blur_scalar(const uint8_t* in, uint8_t* out,
                          int W, int H) {
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            int32_t acc = 0;
            for (int kr = -2; kr <= 2; kr++) {
                for (int kc = -2; kc <= 2; kc++) {
                    int sr = r + kr, sc = c + kc;
                    if (sr < 0 || sr >= H || sc < 0 || sc >= W)
                        continue;
                    acc += (int32_t)in[sr * W + sc]
                           * GAUSS[kr+2][kc+2];
                }
            }
            int32_t v = acc / 273;
            out[r * W + c] = v < 0 ? 0 : (v > 255 ? 255 : v);
        }
    }
}

// Version 2: WITH pre-padding (compiler CAN vectorize)
void gaussian_blur_padded(const uint8_t* in, uint8_t* out,
                          int W, int H) {
    int PW = W + 4, PH = H + 4;
    uint8_t* pad = (uint8_t*)calloc(PW * PH, 1);

    // Copy image into center of padded buffer
    for (int r = 0; r < H; r++)
        memcpy(pad + (r+2)*PW + 2, in + r*W, W);

    // NO boundary check inside loop → compiler can vectorize
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            int32_t acc = 0;
            for (int kr = 0; kr < 5; kr++)
                for (int kc = 0; kc < 5; kc++)
                    acc += (int32_t)pad[(r+kr)*PW + (c+kc)]
                           * GAUSS[kr][kc];
            int32_t v = acc / 273;
            out[r * W + c] = v < 0 ? 0 : (v > 255 ? 255 : v);
        }
    }
    free(pad);
}

