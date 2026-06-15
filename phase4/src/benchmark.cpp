#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gaussian.h"
#include "timer.h"

#define W 512
#define H 512
#define ITERS 200

int main() {
    uint8_t* input  = (uint8_t*)aligned_alloc(64, W * H);
    uint8_t* output = (uint8_t*)aligned_alloc(64, W * H);

    // Fill with fake pixel data
    for (int i = 0; i < W * H; i++) input[i] = (uint8_t)(i % 256);

    printf("=== Gaussian Blur Benchmark (%dx%d, %d iters) ===\n\n",
           W, H, ITERS);

    BENCHMARK("scalar (with boundary check)", ITERS,
        gaussian_blur_scalar(input, output, W, H));

    BENCHMARK("padded (no boundary check)",   ITERS,
        gaussian_blur_padded(input, output, W, H));

    free(input);
    free(output);
    return 0;
}

