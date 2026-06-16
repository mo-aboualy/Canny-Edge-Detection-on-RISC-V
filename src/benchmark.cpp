#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstdint>
#include "image_io.h"
#include "gaussian_blur.h"
#include "timer.h"

#define W     512
#define H     512
#define ITERS 200

int main() {
    Image input, output;
    input.width   = W;
    input.height  = H;
    input.pixels  = (uint8_t*)aligned_alloc(64, W * H);
    output.width  = W;
    output.height = H;
    output.pixels = (uint8_t*)aligned_alloc(64, W * H);

    for (int i = 0; i < W * H; i++)
        input.pixels[i] = (uint8_t)(i % 256);

    printf("=== Phase 4: Gaussian Blur Benchmark (%dx%d, %d iters) ===\n\n", W, H, ITERS);
    printf("%-30s %s\n",   "Function", "Time (ns/call)");
    printf("%-30s %s\n\n", "--------", "--------------");

    BENCHMARK("v1: gaussian_blur_5x5",    ITERS, gaussian_blur_5x5(input, output));
    BENCHMARK("v2: gaussian_blur_padded", ITERS, gaussian_blur_padded(input, output));

    printf("\nSee build/phase4/vec_report.txt for vectorization analysis.\n");

    free(input.pixels);
    free(output.pixels);
    return 0;
}
