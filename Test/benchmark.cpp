#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "image_io.h"
#include "gaussian_blur.h"
#include "vectorization_analysis.h"
#include "sobel.h"
#include "magnitude.h"
#include "direction.h"
#include "timer.h"

#define W     512
#define H     512
#define ITERS 200

int main() {
    size_t alloc_size = (W * H + 63) & ~63;

    // ── Step 1: Allocate all buffers ──────────────────────────
    uint8_t* raw_input = (uint8_t*)aligned_alloc(64, alloc_size);
    if (!raw_input) { printf("Error: raw_input alloc failed.\n"); return 1; }

    uint8_t* raw_output = (uint8_t*)aligned_alloc(64, alloc_size);
    if (!raw_output) { printf("Error: raw_output alloc failed.\n"); free(raw_input); return 1; }

    uint8_t* blur_out_pixels = (uint8_t*)aligned_alloc(64, alloc_size);
    if (!blur_out_pixels) { printf("Error: blur_out_pixels alloc failed.\n"); free(raw_output); free(raw_input); return 1; }

    uint8_t* mag_out = (uint8_t*)aligned_alloc(64, alloc_size);
    if (!mag_out) { printf("Error: mag_out alloc failed.\n"); free(blur_out_pixels); free(raw_output); free(raw_input); return 1; }

    uint8_t* dir_out = (uint8_t*)aligned_alloc(64, alloc_size);
    if (!dir_out) { printf("Error: dir_out alloc failed.\n"); free(mag_out); free(blur_out_pixels); free(raw_output); free(raw_input); return 1; }

    // ── Step 2: Fill input with fake pixel data ───────────────
    for (int i = 0; i < W * H; i++)
        raw_input[i] = (uint8_t)(i % 256);

    // ── Step 3: Wrap raw pointers in Image structs ────────────
    Image input_img;
    input_img.width  = W;
    input_img.height = H;
    input_img.pixels = raw_input;

    // blur_img is used as both output of gaussian and input of sobel
    Image blur_img;
    blur_img.width  = W;
    blur_img.height = H;
    blur_img.pixels = blur_out_pixels;

    // ── Step 4: Pre-compute valid data for downstream stages ──
    // Must run gaussian first so blur_img contains real data,
    // not uninitialized memory, before passing it into sobel.
    gaussian_blur_5x5(input_img, blur_img);

    GradientImage gradient;
    gradient.gx        = nullptr;
    gradient.gy        = nullptr;
    gradient.magnitude = nullptr;
    gradient.width     = W;
    gradient.height    = H;

    // Run sobel once to populate gradient.gx and gradient.gy
    // so magnitude and direction benchmarks have valid input data.
    sobel_3x3(blur_img, gradient);

    // ── Section 1: Vectorization Analysis ────────────────────
    printf("======================================================\n");
    printf("  Section 1: Vectorization Analysis (Gaussian only)\n");
    printf("  scalar = with boundary check  -> compiler CANNOT vectorize\n");
    printf("  padded = with pre-padding     -> compiler CAN vectorize\n");
    printf("======================================================\n");

    BENCHMARK("gaussian_scalar (check)  ", ITERS,
              gaussian_blur_scalar(raw_input, raw_output, W, H));

    BENCHMARK("gaussian_padded (no check)", ITERS,
              gaussian_blur_padded(raw_input, raw_output, W, H));

    printf("\n  NOTE: See vec_report.txt for compiler auto-vec analysis.\n");
    printf("  A speedup from padded over scalar indicates the compiler\n");
    printf("  successfully vectorized the boundary-check-free version.\n\n");

    // ── Section 2: Pipeline Stage Timing ─────────────────────
    printf("======================================================\n");
    printf("  Section 2: Full Pipeline Stage Timing\n");
    printf("  All 4 stages benchmarked using real pipeline functions\n");
    printf("  Results map directly to the Phase 4 report table\n");
    printf("======================================================\n");

    // Stage 1: Gaussian
    BENCHMARK("1. Gaussian blur 5x5     ", ITERS,
              gaussian_blur_5x5(input_img, blur_img));

    // Stage 2: Sobel — benchmarked manually because sobel_3x3 calls
    // aligned_alloc internally on every call. Using the BENCHMARK macro
    // would leak memory on all 200 iterations. Instead we time the
    // alloc+compute cycle honestly (ITERS iterations, divide by ITERS),
    // then restore the gradient once outside the timed window so
    // magnitude and direction still have valid data.
    {
        uint64_t t0 = get_ns();
        for (int i = 0; i < ITERS; i++) {
            sobel_3x3(blur_img, gradient);
            gradient_free(gradient);
        }
        uint64_t t1 = get_ns();
        printf("%-25s %llu ns/call\n", "2. Sobel 3x3",
               (unsigned long long)((t1 - t0) / ITERS));
    }

    // Restore gradient outside the timed window for stages 3 and 4.
    // Note: gradient.gx/gy are uint16_t* internally but mag_out and
    // dir_out are separate uint8_t* buffers — they are not the same.
    sobel_3x3(blur_img, gradient);

    // Stage 3: Magnitude — takes raw int16_t* pointers, no allocation
    BENCHMARK("3. Magnitude (L1)        ", ITERS,
              gradient_magnitude(gradient.gx, gradient.gy, mag_out, W, H));

    // Stage 4: Direction — takes raw int16_t* pointers, no allocation
    BENCHMARK("4. Direction             ", ITERS,
              gradient_direction(gradient.gx, gradient.gy, dir_out, W, H));

    // ── Report table shell ────────────────────────────────────
    printf("\n======================================================\n");
    printf("  Copy ns/call values into the report table:\n");
    printf("  Stage              | this run | -O0 | -O1 | -O2 | -O3 | vec\n");
    printf("  Gaussian 5x5       |          |     |     |     |     |\n");
    printf("  Sobel 3x3          |          |     |     |     |     |\n");
    printf("  Magnitude (L1)     |          |     |     |     |     |\n");
    printf("  Direction          |          |     |     |     |     |\n");
    printf("======================================================\n\n");

    // ── Cleanup ───────────────────────────────────────────────
    gradient_free(gradient);
    free(dir_out);
    free(mag_out);
    free(blur_out_pixels);
    free(raw_output);
    free(raw_input);

    return 0;
}