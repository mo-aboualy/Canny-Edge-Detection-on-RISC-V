#include "image_io.h"
#include "gaussian_blur.h"
#include "sobel.h"
#include "magnitude.h"
#include "direction.h"
#include "profiler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

// Allocate a zeroed image buffer.
// We use aligned_alloc(64, ...) everywhere — not just for RVV in Phase 6,
// but because the compiler is more willing to auto-vectorize aligned loads.
static Image alloc_image(uint32_t w, uint32_t h) {
    Image img;
    img.width  = w;
    img.height = h;
    size_t sz  = (size_t)w * h;
    size_t aligned_sz = (sz + 63) & ~63; // round up to 64-byte boundary
    img.pixels = (uint8_t*)aligned_alloc(64, aligned_sz);
    if (!img.pixels) {
        fprintf(stderr, "ERROR: aligned_alloc failed for %ux%u image\n", w, h);
        exit(1);
    }
    memset(img.pixels, 0, sz);
    return img;
}

// Build a fake test image we can run the pipeline on.
// It's a left-to-right gradient with a bright stripe down the middle.
// The stripe gives Sobel something real to react to, so the timing
// reflects actual edge-detection work rather than a trivial all-zero case.
static void generate_test_image(Image& img) {
    const uint32_t W  = img.width;
    const uint32_t H  = img.height;
    const uint32_t cx = W / 2;

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint8_t val = (uint8_t)(x % 256);
            if (x >= cx - 32 && x < cx + 32)
                val = 255; // hard vertical edge on both sides of the stripe
            img.pixels[y * W + x] = val;
        }
    }
}

// --- printing helpers 

static void print_separator() {
    printf("  %-28s  %-16s  %-6s\n",
           "────────────────────────────",
           "────────────────",
           "──────");
}

static void print_header() {
    printf("\n");
    printf("  %-28s  %-16s  %-6s\n", "Stage", "avg ms / call", "share");
    print_separator();
}

static void print_row(const char* name, uint64_t total_ns, int iters, double pct) {
    double avg_ms = profiler_ns_to_ms(total_ns) / (double)iters;
    printf("  %-28s  %8.3f ms         %5.1f%%\n", name, avg_ms, pct);
}

static void print_total(uint64_t total_ns, int iters) {
    print_separator();
    double avg_ms = profiler_ns_to_ms(total_ns) / (double)iters;
    printf("  %-28s  %8.3f ms         %5.1f%%\n", "TOTAL PIPELINE", avg_ms, 100.0);
}



int main(int argc, char* argv[]) {
    uint32_t W     = 512;
    uint32_t H     = 512;
    int      ITERS = 100;

    if (argc >= 3) {
        W = (uint32_t)atoi(argv[1]);
        H = (uint32_t)atoi(argv[2]);
    }
    if (argc >= 4) {
        ITERS = atoi(argv[3]);
    }
    if (W < 5 || H < 5 || ITERS < 1) {
        fprintf(stderr, "Usage: %s [width height iters]\n", argv[0]);
        fprintf(stderr, "  width/height >= 5, iters >= 1\n");
        return 1;
    }

    printf("\n=== Phase 5: Canny Pipeline Profiling ===\n");
    printf("Image size : %u x %u  (%u pixels)\n", W, H, W * H);
    printf("Iterations : %d per stage\n", ITERS);
    printf("(QEMU wall-clock — percentages are what matter, not absolute ms)\n");

    // --- allocate all buffers up front --------------------------------------
    //
    // We allocate everything before the timed loops so malloc overhead
    // doesn't show up in the measurements. All buffers are 64-byte aligned.

    Image src     = alloc_image(W, H);
    Image blurred = alloc_image(W, H);

    // Gradient buffers (gx, gy, magnitude). Normally sobel_3x3() allocates
    // these itself, but we pre-allocate here so the timing loop isn't
    // measuring malloc instead of the actual Sobel math.
    GradientImage grad;
    {
        size_t count      = (size_t)W * H;
        size_t aligned_sz = (count + 63) & ~63;
        grad.width     = W;
        grad.height    = H;
        grad.gx        = (int16_t* )aligned_alloc(64, aligned_sz * sizeof(int16_t));
        grad.gy        = (int16_t* )aligned_alloc(64, aligned_sz * sizeof(int16_t));
        grad.magnitude = (uint16_t*)aligned_alloc(64, aligned_sz * sizeof(uint16_t));
        if (!grad.gx || !grad.gy || !grad.magnitude) {
            fprintf(stderr, "ERROR: gradient buffer allocation failed\n");
            return 1;
        }
    }

    size_t   px       = (size_t)W * H;
    size_t   px_align = (px + 63) & ~63;
    uint8_t* mag_u8   = (uint8_t*)aligned_alloc(64, px_align); // normalized magnitude
    uint8_t* dir      = (uint8_t*)aligned_alloc(64, px_align); // direction bins
    if (!mag_u8 || !dir) {
        fprintf(stderr, "ERROR: output buffer allocation failed\n");
        return 1;
    }

    generate_test_image(src);

    uint64_t t0, t1;
    uint64_t ns_gaussian, ns_sobel, ns_magnitude, ns_direction;

    

    t0 = profiler_now();
    for (int i = 0; i < ITERS; i++) {
        gaussian_blur_5x5(src, blurred);
    }
    t1 = profiler_now();
    ns_gaussian = t1 - t0;

    

    t0 = profiler_now();
    for (int i = 0; i < ITERS; i++) {
        const uint32_t Ww = blurred.width;
        const uint32_t Hh = blurred.height;
        for (uint32_t y = 0; y < Hh; y++) {
            for (uint32_t x = 0; x < Ww; x++) {
                // border pixels: no 3x3 neighbourhood, just zero them out
                if (x == 0 || x == Ww-1 || y == 0 || y == Hh-1) {
                    grad.gx[y*Ww+x] = 0;
                    grad.gy[y*Ww+x] = 0;
                    grad.magnitude[y*Ww+x] = 0;
                    continue;
                }
                // Gx kernel: detects vertical edges (left-right intensity change)
                int16_t gx =
                    -1*(int16_t)blurred.pixels[(y-1)*Ww+(x-1)] +
                    +1*(int16_t)blurred.pixels[(y-1)*Ww+(x+1)] +
                    -2*(int16_t)blurred.pixels[(y  )*Ww+(x-1)] +
                    +2*(int16_t)blurred.pixels[(y  )*Ww+(x+1)] +
                    -1*(int16_t)blurred.pixels[(y+1)*Ww+(x-1)] +
                    +1*(int16_t)blurred.pixels[(y+1)*Ww+(x+1)];
                // Gy kernel: detects horizontal edges (top-bottom intensity change)
                int16_t gy =
                    -1*(int16_t)blurred.pixels[(y-1)*Ww+(x-1)] +
                    -2*(int16_t)blurred.pixels[(y-1)*Ww+(x  )] +
                    -1*(int16_t)blurred.pixels[(y-1)*Ww+(x+1)] +
                    +1*(int16_t)blurred.pixels[(y+1)*Ww+(x-1)] +
                    +2*(int16_t)blurred.pixels[(y+1)*Ww+(x  )] +
                    +1*(int16_t)blurred.pixels[(y+1)*Ww+(x+1)];
                grad.gx[y*Ww+x] = gx;
                grad.gy[y*Ww+x] = gy;
            }
        }
    }
    t1 = profiler_now();
    ns_sobel = t1 - t0;

    
    t0 = profiler_now();
    for (int i = 0; i < ITERS; i++) {
        gradient_magnitude(grad.gx, grad.gy, mag_u8, (int)W, (int)H);
    }
    t1 = profiler_now();
    ns_magnitude = t1 - t0;

   

    t0 = profiler_now();
    for (int i = 0; i < ITERS; i++) {
        gradient_direction(grad.gx, grad.gy, dir, (int)W, (int)H);
    }
    t1 = profiler_now();
    ns_direction = t1 - t0;

    // --- print results -------------------------------------------------------

    uint64_t ns_total = ns_gaussian + ns_sobel + ns_magnitude + ns_direction;
    double   total_d  = (double)ns_total;

    print_header();
    print_row("Gaussian 5x5",             ns_gaussian,  ITERS, 100.0*(double)ns_gaussian /total_d);
    print_row("Sobel Gx/Gy",              ns_sobel,     ITERS, 100.0*(double)ns_sobel    /total_d);
    print_row("Magnitude L1 (|Gx|+|Gy|)", ns_magnitude, ITERS, 100.0*(double)ns_magnitude/total_d);
    print_row("Direction (4-bin)",         ns_direction, ITERS, 100.0*(double)ns_direction/total_d);
    print_total(ns_total, ITERS);


    // --- cleanup 
    image_free(src);
    image_free(blurred);
    gradient_free(grad);
    free(mag_u8);
    free(dir);

    return 0;
}