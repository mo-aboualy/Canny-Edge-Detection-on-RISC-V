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
    /*
     * Deterministic synthetic image.
     * This avoids file I/O during benchmarking and makes every optimization
     * level run the same workload.
     */
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

static int max_abs_diff_u8(
    const uint8_t* a,
    const uint8_t* b,
    size_t count
) {
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

static void benchmark_sobel_stage(const Image& blurred) {
    const uint64_t t0 = get_ns();

    for (int i = 0; i < ITERS; ++i) {
        GradientImage temp{};
        sobel_3x3(blurred, temp);
        gradient_free(temp);
    }

    const uint64_t t1 = get_ns();
    const unsigned long long avg =
        static_cast<unsigned long long>(
            (t1 - t0) / static_cast<uint64_t>(ITERS)
        );

    std::printf("%-34s %llu ns/call\n", "3. Sobel 3x3", avg);
}

int main() {
    const size_t pixel_count =
        static_cast<size_t>(W) * static_cast<size_t>(H);

    uint8_t* input_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* spatial_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* separable_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* mag_l1_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* mag_l2_pixels = alloc_u8_buffer(pixel_count);
    uint8_t* dir_pixels = alloc_u8_buffer(pixel_count);

    fill_test_pattern(input_pixels, W, H);

    Image input_img{};
    input_img.width = W;
    input_img.height = H;
    input_img.pixels = input_pixels;

    Image spatial_img{};
    spatial_img.width = W;
    spatial_img.height = H;
    spatial_img.pixels = spatial_pixels;

    Image separable_img{};
    separable_img.width = W;
    separable_img.height = H;
    separable_img.pixels = separable_pixels;

    /*
     * Warm-up and correctness comparison.
     */
    gaussian_blur_5x5_spatial_2d(input_img, spatial_img);
    gaussian_blur_5x5_separable_1d(input_img, separable_img);

    const int gaussian_max_diff =
        max_abs_diff_u8(spatial_pixels, separable_pixels, pixel_count);

    GradientImage gradient{};
    sobel_3x3(spatial_img, gradient);

    std::printf("============================================================\n");
    std::printf(" Phase 4 Benchmark Configuration\n");
    std::printf(" Image size: %dx%d\n", W, H);
    std::printf(" Iterations per stage: %d\n", ITERS);
    std::printf(" Timer: CLOCK_MONOTONIC via get_ns()\n");
    std::printf("============================================================\n\n");

    std::printf("============================================================\n");
    std::printf(" Gaussian Implementation Comparison\n");
    std::printf(" spatial 2D : direct 5x5 matrix convolution\n");
    std::printf(" separable  : 1D horizontal pass + 1D vertical pass\n");
    std::printf(" max absolute difference: %d\n", gaussian_max_diff);
    std::printf("============================================================\n");

    BENCHMARK("1. Gaussian spatial 2D", ITERS,
              gaussian_blur_5x5_spatial_2d(input_img, spatial_img));

    BENCHMARK("2. Gaussian separable 1D", ITERS,
              gaussian_blur_5x5_separable_1d(input_img, separable_img));

    std::printf("\n");
    std::printf("============================================================\n");
    std::printf(" Scalar Pipeline Timing\n");
    std::printf(" Pipeline uses spatial 2D Gaussian as the reference path.\n");
    std::printf("============================================================\n");

    benchmark_sobel_stage(spatial_img);

    BENCHMARK("4. Magnitude L1", ITERS,
              gradient_magnitude_l1(gradient.gx, gradient.gy, mag_l1_pixels, W, H));

    BENCHMARK("5. Magnitude L2", ITERS,
              gradient_magnitude_l2(gradient.gx, gradient.gy, mag_l2_pixels, W, H));

    BENCHMARK("6. Direction", ITERS,
              gradient_direction(gradient.gx, gradient.gy, dir_pixels, W, H));

    std::printf("\n");
    std::printf("Report table columns:\n");
    std::printf("Stage | -O0 | -O2 | -O3 | -Os | -Ofast | Auto-vec\n");
    std::printf("Gaussian spatial 2D   | ___ | ___ | ___ | ___ | ___ | ___\n");
    std::printf("Gaussian separable 1D | ___ | ___ | ___ | ___ | ___ | ___\n");
    std::printf("Sobel 3x3             | ___ | ___ | ___ | ___ | ___ | ___\n");
    std::printf("Magnitude L1          | ___ | ___ | ___ | ___ | ___ | ___\n");
    std::printf("Magnitude L2          | ___ | ___ | ___ | ___ | ___ | ___\n");
    std::printf("Direction             | ___ | ___ | ___ | ___ | ___ | ___\n");

    gradient_free(gradient);

    std::free(dir_pixels);
    std::free(mag_l2_pixels);
    std::free(mag_l1_pixels);
    std::free(separable_pixels);
    std::free(spatial_pixels);
    std::free(input_pixels);

    return 0;
}
