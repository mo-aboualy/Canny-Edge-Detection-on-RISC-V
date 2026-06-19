/**
 * @file main.cpp
 * @brief Phase 4/5/6 — Canny pipeline profiling on RISC-V / QEMU.
 *
 * Consolidates the old benchmark.cpp and the old main.cpp into one file.
 *
 * Timer: get_ns() via Linux syscall 113 (clock_gettime / CLOCK_MONOTONIC).
 *        This is the correct timer for QEMU user-mode because it measures
 *        wall-clock time, not emulated cycle counts.  rdcycle is NOT used.
 *
 * Build:
 *   make run            (VLEN=128, default)
 *   make run VLEN=256
 *   make run VLEN=512
 *
 * Usage (optional args):
 *   ./canny_rv [width height iters]
 */

#include "image_io.h"
#include "gaussian_blur.h"
#include "sobel.h"
#include "magnitude.h"
#include "magnitude_rvv.h"
#include "sobel_rvv.h"
#include "magnitude_rvv.h"
#include "sobel_rvv.h"
#include "direction.h"
#include "timer.h"         // get_ns() — CLOCK_MONOTONIC via ecall
#include "gaussian_rvv.h"
#include "sobel_rvv.h"


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int32_t ALIGNMENT = 64;

/** Round n up to the next multiple of 64. */
static inline size_t align64(size_t n) { return (n + 63) & ~static_cast<size_t>(63); }

/** Allocate a 64-byte-aligned, zero-filled pixel buffer. */
static uint8_t* alloc_pixels(size_t count) {
    uint8_t* p = static_cast<uint8_t*>(aligned_alloc(ALIGNMENT, align64(count)));
    if (!p) { std::fprintf(stderr, "ERROR: aligned_alloc failed\n"); std::exit(1); }
    std::memset(p, 0, count);
    return p;
}

/** Build a synthetic test image: gradient + bright vertical stripe. */
static void fill_test_image(uint8_t* pixels, int32_t W, int32_t H) {
    const int32_t cx = W / 2;
    for (int32_t y = 0; y < H; ++y) {
        for (int32_t x = 0; x < W; ++x) {
            uint8_t v = static_cast<uint8_t>((x + y) & 0xFF);
            if (x >= cx - 32 && x < cx + 32) v = 255; // hard vertical edge
            pixels[y * W + x] = v;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pretty-print helpers
// ─────────────────────────────────────────────────────────────────────────────

static void print_separator() {
    std::printf("  %-32s  %-16s  %s\n",
        "────────────────────────────────",
        "────────────────",
        "──────");
}

static void print_header() {
    std::printf("\n");
    std::printf("  %-32s  %-16s  %s\n", "Stage", "avg ms / call", "share");
    print_separator();
}

static void print_row(const char* name, uint64_t total_ns,
                      int iters, double share_pct) {
    double avg_ms = static_cast<double>(total_ns)
                    / (static_cast<double>(iters) * 1.0e6);
    std::printf("  %-32s  %8.3f ms       %5.1f%%\n", name, avg_ms, share_pct);
}

static void print_total(uint64_t ns, int iters) {
    print_separator();
    double avg_ms = static_cast<double>(ns)
                    / (static_cast<double>(iters) * 1.0e6);
    std::printf("  %-32s  %8.3f ms       %5.1f%%\n",
                "TOTAL PIPELINE", avg_ms, 100.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    // ── Configuration ────────────────────────────────────────────────────────
    uint32_t W     = 512;
    uint32_t H     = 512;
    int      ITERS = 100;

    if (argc >= 3) {
        W = static_cast<uint32_t>(std::atoi(argv[1]));
        H = static_cast<uint32_t>(std::atoi(argv[2]));
    }
    if (argc >= 4) ITERS = std::atoi(argv[3]);

    if (W < 5 || H < 5 || ITERS < 1) {
        std::fprintf(stderr,
            "Usage: %s [width height iters]   (width/height >= 5, iters >= 1)\n",
            argv[0]);
        return 1;
    }

    const size_t N = static_cast<size_t>(W) * H;

    std::printf("\n=== Canny Pipeline Profiling (Phase 4 / 5 / 6) ===\n");
    std::printf("Image      : %u x %u  (%zu pixels)\n", W, H, N);
    std::printf("Iterations : %d per stage\n", ITERS);
    std::printf("Timer      : CLOCK_MONOTONIC via get_ns() (wall-clock, QEMU-safe)\n");
    std::printf("NOTE: percentages matter more than absolute ms values under QEMU.\n");

    // ── Pre-allocate all buffers ─────────────────────────────────────────────
    // Allocating outside the timed loops avoids malloc noise in measurements.

    // Source image
    Image src;
    src.width  = W;
    src.height = H;
    src.pixels = alloc_pixels(N);
    fill_test_image(src.pixels, static_cast<int32_t>(W), static_cast<int32_t>(H));

    // Blurred output (shared by all Gaussian variants; overwritten each iter)
    Image blurred;
    blurred.width  = W;
    blurred.height = H;
    blurred.pixels = alloc_pixels(N);

    // Gradient buffers (sobel_3x3 normally allocates internally; we pre-allocate
    // so the Sobel timing measures convolution, not malloc).
    GradientImage grad;
    grad.width     = W;
    grad.height    = H;
    grad.gx        = static_cast<int16_t* >(
                         aligned_alloc(ALIGNMENT, align64(N * sizeof(int16_t))));
    grad.gy        = static_cast<int16_t* >(
                         aligned_alloc(ALIGNMENT, align64(N * sizeof(int16_t))));
    grad.magnitude = static_cast<uint16_t*>(
                         aligned_alloc(ALIGNMENT, align64(N * sizeof(uint16_t))));
    if (!grad.gx || !grad.gy || !grad.magnitude) {
        std::fprintf(stderr, "ERROR: gradient buffer allocation failed\n");
        return 1;
    }
    std::memset(grad.gx,        0, N * sizeof(int16_t));
    std::memset(grad.gy,        0, N * sizeof(int16_t));
    std::memset(grad.magnitude, 0, N * sizeof(uint16_t));

    // Magnitude (uint8_t) and direction outputs
    uint8_t* mag_u8 = alloc_pixels(N);
    uint8_t* dir    = alloc_pixels(N);

    // ── Correctness check: spatial_2d vs separable_1d vs rvv ─────────────────
    {
        uint8_t* out_spatial   = alloc_pixels(N);
        uint8_t* out_separable = alloc_pixels(N);
        uint8_t* out_rvv       = alloc_pixels(N);

        Image tmp_sp  = { W, H, out_spatial   };
        Image tmp_sep = { W, H, out_separable };
        Image tmp_rvv = { W, H, out_rvv       };

        gaussian_blur_5x5_spatial_2d   (src, tmp_sp);
        gaussian_blur_5x5_separable_1d (src, tmp_sep);
        gaussian_blur_5x5_rvv          (src, tmp_rvv);

        int max_diff = 0;
        for (size_t i = 0; i < N; ++i) {
            int d = static_cast<int>(out_spatial[i]) - static_cast<int>(out_separable[i]);
            if (d < 0) d = -d;
            if (d > max_diff) max_diff = d;
        }
        std::printf("\nGaussian correctness: spatial_2d vs separable_1d "
                    "max |diff| = %d pixel(s)  %s\n",
                    max_diff, max_diff <= 1 ? "(PASS)" : "(WARN — check kernel)");

        // Gaussian correctness: separable_1d vs rvv
        int max_diff_rvv = 0;
        for (size_t i = 0; i < N; ++i) {
            int d = static_cast<int>(out_separable[i]) - static_cast<int>(out_rvv[i]);
            if (d < 0) d = -d;
            if (d > max_diff_rvv) max_diff_rvv = d;
        }
        std::printf("Gaussian correctness: separable_1d vs rvv "
                    "max |diff| = %d pixel(s)  %s\n",
                    max_diff_rvv, max_diff_rvv <= 1 ? "(PASS)" : "(FAIL)");

        std::free(out_spatial);
        std::free(out_separable);
        std::free(out_rvv);
    }

    // ── Benchmarking ─────────────────────────────────────────────────────────

    uint64_t t0, t1;
    uint64_t ns_spatial, ns_separable, ns_rvv, ns_sobel, ns_sobel_rvv, ns_mag_l1, ns_mag_l1_rvv, ns_mag_l2, ns_dir;

    // 1. Gaussian — spatial 2-D  (reference / slowest path)
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i)
        gaussian_blur_5x5_spatial_2d(src, blurred);
    t1 = get_ns();
    ns_spatial = t1 - t0;

    // 2. Gaussian — separable 1-D  (scalar optimised)
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i)
        gaussian_blur_5x5_separable_1d(src, blurred);
    t1 = get_ns();
    ns_separable = t1 - t0;

    // 3. Gaussian — RVV  (intrinsic; falls back to separable on host)
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i)
        gaussian_blur_5x5_rvv(src, blurred);
    t1 = get_ns();
    ns_rvv = t1 - t0;

    // Run separable once more to leave a realistic blurred image in `blurred`
    // before the downstream stages are measured.
    gaussian_blur_5x5_separable_1d(src, blurred);

    // 4. Sobel Gx / Gy
    // We call sobel_3x3 which allocates internally; free and reuse each iter
    // to avoid measuring accumulated heap growth.
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        // Inline the kernel directly onto the pre-allocated grad buffers so
        // the measurement is pure computation, not malloc/free.
        const uint32_t Ww = blurred.width;
        const uint32_t Hh = blurred.height;
        for (uint32_t y = 0; y < Hh; ++y) {
            for (uint32_t x = 0; x < Ww; ++x) {
                if (x == 0 || x == Ww-1 || y == 0 || y == Hh-1) {
                    grad.gx[y*Ww+x] = 0;
                    grad.gy[y*Ww+x] = 0;
                    grad.magnitude[y*Ww+x] = 0;
                    continue;
                }
                int16_t gx =
                    -1*(int16_t)blurred.pixels[(y-1)*Ww+(x-1)] +
                    +1*(int16_t)blurred.pixels[(y-1)*Ww+(x+1)] +
                    -2*(int16_t)blurred.pixels[(y  )*Ww+(x-1)] +
                    +2*(int16_t)blurred.pixels[(y  )*Ww+(x+1)] +
                    -1*(int16_t)blurred.pixels[(y+1)*Ww+(x-1)] +
                    +1*(int16_t)blurred.pixels[(y+1)*Ww+(x+1)];
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
    t1 = get_ns();
    ns_sobel = t1 - t0;

    // 4b. Sobel — RVV
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        GradientImage tmp_g{};
        sobel_3x3_rvv(blurred, tmp_g);
        gradient_free(tmp_g);
    }
    t1 = get_ns();
    ns_sobel_rvv = t1 - t0;

    // 5. Magnitude — L1  (|Gx| + |Gy|, integer, fast)
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i)
        gradient_magnitude_l1(grad.gx, grad.gy, mag_u8,
                              static_cast<int>(W), static_cast<int>(H));
    t1 = get_ns();
    ns_mag_l1 = t1 - t0;

    // 5b. Magnitude — L1 RVV
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i)
        gradient_magnitude_l1_rvv(grad.gx, grad.gy, mag_u8,
                              static_cast<int>(W), static_cast<int>(H));
    t1 = get_ns();
    ns_mag_l1_rvv = t1 - t0;

    // 6. Magnitude — L2  (sqrt(Gx²+Gy²), float, accurate)
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i)
        gradient_magnitude_l2(grad.gx, grad.gy, mag_u8,
                              static_cast<int>(W), static_cast<int>(H));
    t1 = get_ns();
    ns_mag_l2 = t1 - t0;

    // 7. Direction — 4-bin quantisation
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i)
        gradient_direction(grad.gx, grad.gy, dir,
                           static_cast<int>(W), static_cast<int>(H));
    t1 = get_ns();
    ns_dir = t1 - t0;

   // ── Results ──────────────────────────────────────────────────────────────

    // Changed to use ns_spatial instead of ns_separable for the main pipeline
    const uint64_t ns_pipeline = ns_spatial + ns_sobel + ns_mag_l1 + ns_dir;
    const double   D = static_cast<double>(ns_pipeline);

    std::printf("\n============================================================\n");
    std::printf(" Standard pipeline  (spatial 2-D Gaussian + L1 magnitude)\n");
    std::printf("============================================================\n");
    print_header();
    print_row("Gaussian spatial 2-D",        ns_spatial,   ITERS, 100.0*ns_spatial  /D);
    print_row("Sobel Gx/Gy",                 ns_sobel,     ITERS, 100.0*ns_sobel    /D);
    print_row("Magnitude L1 (|Gx|+|Gy|)",    ns_mag_l1,    ITERS, 100.0*ns_mag_l1   /D);
    print_row("Direction (4-bin)",           ns_dir,       ITERS, 100.0*ns_dir      /D);
    print_total(ns_pipeline, ITERS);

    std::printf("\n============================================================\n");
    std::printf(" Alternative / comparison stages\n");
    std::printf("============================================================\n");
    print_header();
    print_row("Gaussian separable 1-D",       ns_separable, ITERS,
              100.0 * static_cast<double>(ns_separable) / static_cast<double>(ns_spatial));
    print_row("Gaussian RVV          (vec)",  ns_rvv,       ITERS,
              100.0 * static_cast<double>(ns_rvv)       / static_cast<double>(ns_spatial));
    print_row("Sobel RVV             (vec)",  ns_sobel_rvv,   ITERS,
              100.0 * static_cast<double>(ns_sobel_rvv)   / static_cast<double>(ns_sobel));
    print_row("Magnitude L1 RVV      (vec)",  ns_mag_l1_rvv, ITERS,
              100.0 * static_cast<double>(ns_mag_l1_rvv)  / static_cast<double>(ns_mag_l1));
    print_row("Magnitude L2 (baseline ref)",  ns_mag_l2,    ITERS,
              100.0 * static_cast<double>(ns_mag_l2)    / static_cast<double>(ns_mag_l1));
    std::printf("  (share %% column = ratio vs the baseline for that stage)\n");

    std::printf("\n============================================================\n");
    std::printf(" Gaussian speedup summary\n");
    std::printf("============================================================\n");
    std::printf("  spatial_2d   / separable_1d  ratio: %.2fx  "
                "(>1 means separable is faster)\n",
                static_cast<double>(ns_spatial)   / static_cast<double>(ns_separable));
    std::printf("  separable_1d / rvv           ratio: %.2fx  "
                "(>1 means RVV is faster)\n",
                static_cast<double>(ns_separable) / static_cast<double>(ns_rvv));
    std::printf("  spatial_2d   / rvv           ratio: %.2fx\n",
                static_cast<double>(ns_spatial)   / static_cast<double>(ns_rvv));

    // ── Cleanup ──────────────────────────────────────────────────────────────
    image_free(src);
    image_free(blurred);
    gradient_free(grad);
    std::free(mag_u8);
    std::free(dir);

    return 0;
}