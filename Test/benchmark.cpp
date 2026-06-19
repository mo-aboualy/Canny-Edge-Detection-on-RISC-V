#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "image_io.h"
#include "gaussian_blur.h"
#include "sobel.h"
#include "magnitude.h"
#include "direction.h"
#include "timer.h"

static constexpr int W = 512;
static constexpr int H = 512;
static constexpr int ITERS = 200;
static constexpr size_t ALIGNMENT = 64;

static size_t round_up_to_alignment(size_t bytes) {
    return (bytes + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}

static uint8_t* alloc_u8_buffer(size_t count) {
    const size_t bytes = round_up_to_alignment(count * sizeof(uint8_t));
    uint8_t* ptr = static_cast<uint8_t*>(aligned_alloc(ALIGNMENT, bytes));

    if (!ptr) {
        std::printf("ERROR: aligned_alloc failed for %zu bytes\n", bytes);
        std::exit(1);
    }

    std::memset(ptr, 0, bytes);
    return ptr;
}

static void fill_test_pattern(uint8_t* data, int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int gradient = (x + y) & 0xFF;
            const int rectangle =
                (x > width / 4 && x < 3 * width / 4 &&
                 y > height / 4 && y < 3 * height / 4)
                    ? 96
                    : 0;

            data[y * width + x] =
                static_cast<uint8_t>((gradient + rectangle) & 0xFF);
        }
    }
}

static int max_abs_diff_u8(const uint8_t* a, const uint8_t* b, size_t count) {
    int max_diff = 0;
    for (size_t i = 0; i < count; ++i) {
        const int diff = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        const int abs_diff = diff < 0 ? -diff : diff;
        if (abs_diff > max_diff) {
            max_diff = abs_diff;
        }
    }
    return max_diff;
}

static void gaussian_blur_padded_2d(const uint8_t* in, uint8_t* out, int width, int height) {
    int PW = width + 4, PH = height + 4;
    uint8_t* pad = static_cast<uint8_t*>(std::calloc(PW * PH, 1));
    if (!pad) return;

    for (int r = 0; r < height; r++) {
        std::memcpy(pad + (r + 2) * PW + 2, in + r * width, width);
    }

    static const int16_t GAUSS[5][5] = {
        { 2,  4,  5,  4,  2},
        { 4,  9, 12,  9,  4},
        { 5, 12, 15, 12,  5},
        { 4,  9, 12,  9,  4},
        { 2,  4,  5,  4,  2}
    };

    for (int r = 0; r < height; r++) {
        for (int c = 0; c < width; c++) {
            int32_t acc = 0;
            for (int kr = 0; kr < 5; kr++) {
                for (int kc = 0; kc < 5; kc++) {
                    acc += static_cast<int32_t>(pad[(r + kr) * PW + (c + kc)]) * GAUSS[kr][kc];
                }
            }
            int32_t v = acc / 273;
            out[r * width + c] = v < 0 ? 0 : (v > 255 ? 255 : v);
        }
    }
    std::free(pad);
}

int main() {
    const size_t pixel_count = static_cast<size_t>(W) * static_cast<size_t>(H);

    uint8_t* input_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* spatial_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* separable_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* mag_l1_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* mag_l2_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* dir_pixels = alloc_u8_buffer(pixel_count);

    fill_test_pattern(input_pixels, W, H);

    Image input_img{static_cast<uint32_t>(W), static_cast<uint32_t>(H), input_pixels};
    Image spatial_img{static_cast<uint32_t>(W), static_cast<uint32_t>(H), spatial_pixels};
    Image separable_img{static_cast<uint32_t>(W), static_cast<uint32_t>(H), separable_pixels};

    gaussian_blur_5x5_spatial_2d(input_img, spatial_img);
    gaussian_blur_5x5_separable_1d(input_img, separable_img);
    std::printf("Gaussian spatial vs separable max|diff|: %d\n",
    max_abs_diff_u8(spatial_pixels, separable_pixels, pixel_count));

    GradientImage gradient{};
    sobel_3x3(spatial_img, gradient);

    std::printf("============================================================\n");
    std::printf(" Phase 4 Benchmark Configuration\n");
    std::printf(" Image size: %dx%d | Iterations per stage: %d\n", W, H, ITERS);
    std::printf("============================================================\n\n");

    uint64_t t0, t1;
    
    // 1. Spatial 2D Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gaussian_blur_5x5_spatial_2d(input_img, spatial_img);
    }
    t1 = get_ns();
    unsigned long long t_spatial = (t1 - t0) / ITERS;

    // 1b. Padded 2D Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gaussian_blur_padded_2d(input_pixels, spatial_pixels, W, H);
    }
    t1 = get_ns();
    unsigned long long t_padded = (t1 - t0) / ITERS;

    // 2. Separable 1D Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gaussian_blur_5x5_separable_1d(input_img, separable_img);
    }
    t1 = get_ns();
    unsigned long long t_separable = (t1 - t0) / ITERS;

    // 3. Sobel 3x3 Timing
    GradientImage bench_target_gradient{};
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        sobel_3x3(spatial_img, bench_target_gradient);
    }
    t1 = get_ns();
    unsigned long long t_sobel = (t1 - t0) / ITERS;
    gradient_free(bench_target_gradient);

    // 4. Magnitude L1 Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gradient_magnitude_l1(gradient.gx, gradient.gy, mag_l1_pixels, W, H);
    }
    t1 = get_ns();
    unsigned long long t_mag_l1 = (t1 - t0) / ITERS;

    // 5. Magnitude L2 Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gradient_magnitude_l2(gradient.gx, gradient.gy, mag_l2_pixels, W, H);
    }
    t1 = get_ns();
    unsigned long long t_mag_l2 = (t1 - t0) / ITERS;

    // 6. Direction Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gradient_direction(gradient.gx, gradient.gy, dir_pixels, W, H);
    }
    t1 = get_ns();
    unsigned long long t_direction = (t1 - t0) / ITERS;

    // Print individual details to match prior formats
    std::printf("1. Gaussian spatial 2D (Un-padded) : %llu ns/call\n", t_spatial);
    std::printf("1b. Gaussian Pre-padded 2D         : %llu ns/call\n", t_padded);
    std::printf("2. Gaussian separable 1D           : %llu ns/call\n", t_separable);
    std::printf("3. Sobel 3x3                       : %llu ns/call\n", t_sobel);
    std::printf("4. Magnitude L1                    : %llu ns/call\n", t_mag_l1);
    std::printf("5. Magnitude L2                    : %llu ns/call\n", t_mag_l2);
    std::printf("6. Direction                       : %llu ns/call\n", t_direction);

    // Dynamic Matrix Generation
    std::printf("\n========================================================================\n");
    std::printf(" Live Profile Report (Current Target Optimization Profile)\n");
    std::printf("========================================================================\n");
    std::printf("%-30s | Runtime (ns/call)\n", "Stage");
    std::printf("------------------------------------------------------------------------\n");
    std::printf("%-30s | %llu ns\n", "Gaussian spatial 2D (Un-padded)", t_spatial);
    std::printf("%-30s | %llu ns\n", "Gaussian Pre-padded 2D", t_padded);
    std::printf("%-30s | %llu ns\n", "Gaussian separable 1D", t_separable);
    std::printf("%-30s | %llu ns\n", "Sobel 3x3", t_sobel);
    std::printf("%-30s | %llu ns\n", "Magnitude L1", t_mag_l1);
    std::printf("%-30s | %llu ns\n", "Magnitude L2", t_mag_l2);
    std::printf("%-30s | %llu ns\n", "Direction", t_direction);
    std::printf("========================================================================\n");

    gradient_free(gradient);
    std::free(dir_pixels);
    std::free(mag_l2_pixels);
    std::free(mag_l1_pixels);
    std::free(separable_pixels);
    std::free(spatial_pixels);
    std::free(input_pixels);

    return 0;
}
