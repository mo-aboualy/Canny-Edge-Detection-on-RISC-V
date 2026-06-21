#include <cstdint>  // fixed-width integer types: uint8_t, int32_t, uint64_t, etc.
#include <cstdio>   // printf, used throughout for all the result/status output
#include <cstdlib>  // aligned_alloc, calloc, free, exit — used for buffer management
#include <cstring>  // memcpy, memset — used for copying and zero-filling buffers

#include "image_io.h"      // defines the Image struct used to wrap raw pixel buffers
#include "gaussian_blur.h" // declares gaussian_blur_5x5_spatial_2d and gaussian_blur_5x5_separable_1d
#include "sobel.h"         // declares sobel_3x3 and the GradientImage struct
#include "magnitude.h"     // declares gradient_magnitude_l1 and gradient_magnitude_l2
#include "direction.h"     // declares gradient_direction
#include "timer.h"         // provides get_ns() for nanosecond-precision timing

static constexpr int W = 512;
// W = test image width in pixels — controls the size of EVERY buffer and EVERY stage's workload
static constexpr int H = 512;
// H = test image height in pixels — same role as W, for vertical dimension
static constexpr int ITERS = 200;
// ITERS = how many times each stage is repeated before averaging —
//         higher = more stable timing result, but slower to run the whole benchmark
static constexpr size_t ALIGNMENT = 64;
// ALIGNMENT = required memory alignment in bytes, matching cache-line size
//             and what some RVV load/store instructions expect

static size_t round_up_to_alignment(size_t bytes) {
    // takes a byte count and rounds it UP to the next multiple of ALIGNMENT (64)
    return (bytes + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
    // adds 63, then masks off the low 6 bits — classic bitwise "round up to power of 2" trick
    // example: bytes=100 → (100+63)=163 → 163 & ~63 = 128 (next multiple of 64)
}

static uint8_t* alloc_u8_buffer(size_t count) {
    // allocates a zero-initialized, 64-byte-aligned buffer of 'count' uint8_t elements
    const size_t bytes = round_up_to_alignment(count * sizeof(uint8_t));
    // total bytes needed, rounded up to the alignment boundary

    uint8_t* ptr = static_cast<uint8_t*>(aligned_alloc(ALIGNMENT, bytes));
    // aligned_alloc(64, bytes): guarantees the returned address is a multiple of 64,
    // unlike plain malloc which gives no alignment guarantee

    if (!ptr) {
        // if allocation failed (out of memory), ptr will be nullptr
        std::printf("ERROR: aligned_alloc failed for %zu bytes\n", bytes);
        // print a clear error message instead of silently continuing with a bad pointer
        std::exit(1);
        // terminate immediately with a nonzero exit code (1 = failure)
    }

    std::memset(ptr, 0, bytes);
    // zero-fills the entire buffer, guaranteeing no leftover garbage memory
    // affects correctness comparisons or benchmark results

    return ptr;
}

static void fill_test_pattern(uint8_t* data, int width, int height) {
    // fills 'data' with a deterministic synthetic test image (gradient + rectangle)
    for (int y = 0; y < height; ++y) {
        // y = current row
        for (int x = 0; x < width; ++x) {
            // x = current column

            const int gradient = (x + y) & 0xFF;
            // creates a diagonal brightness gradient; & 0xFF wraps the value into 0-255,
            // so the gradient repeats in diagonal bands every 256 pixels

            const int rectangle =
                (x > width / 4 && x < 3 * width / 4 &&
                 y > height / 4 && y < 3 * height / 4)
                    ? 96
                    : 0;
            // adds +96 brightness inside the middle 50% of the image (both dimensions),
            // creating a sharp rectangular edge useful for testing Sobel/gradient detection
            // outside that region, adds 0 (no change)

            data[y * width + x] =
                static_cast<uint8_t>((gradient + rectangle) & 0xFF);
            // combines gradient + rectangle, wraps into 0-255 again, and writes the final pixel
            // y * width + x converts the 2D position into the correct 1D array index
        }
    }
}

static int max_abs_diff_u8(const uint8_t* a, const uint8_t* b, size_t count) {
    // compares two buffers element-by-element, returns the LARGEST absolute difference found
    int max_diff = 0;
    // tracks the biggest difference seen so far, starts at 0 (meaning "no difference yet")

    for (size_t i = 0; i < count; ++i) {
        // i = current index, loops through every element in both buffers

        const int diff = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        // signed subtraction; cast to int first so negative results aren't wrapped/corrupted
        // (uint8_t subtraction alone would wrap around incorrectly for negative results)

        const int abs_diff = diff < 0 ? -diff : diff;
        // manual absolute value: if diff is negative, flip its sign; otherwise keep as-is

        if (abs_diff > max_diff) {
            max_diff = abs_diff;
            // update the running maximum if this difference is the biggest seen so far
        }
    }
    return max_diff;
    // 0 means the two buffers are IDENTICAL; anything above 0 means they differ somewhere
}

static void gaussian_blur_padded_2d(const uint8_t* in, uint8_t* out, int width, int height) {
    // local raw-pointer version of the padded Gaussian blur, used to time the raw-pointer
    // code path separately from the team's Image-struct-based functions

    int PW = width + 4, PH = height + 4;
    // padded width/height: +4 because the 5x5 kernel reaches 2 pixels outward on each side

    uint8_t* pad = static_cast<uint8_t*>(std::calloc(PW * PH, 1));
    // allocates and zero-fills the padded buffer in one call — the zeros become the border

    if (!pad) return;
    // if allocation failed, exit early rather than crash on a null pointer

    for (int r = 0; r < height; r++) {
        // copies the real image into the padded buffer, one row at a time
        std::memcpy(pad + (r + 2) * PW + 2, in + r * width, width);
        // destination shifted down 2 rows and right 2 columns to skip the padding border
    }

    static const int16_t GAUSS[5][5] = {
        // local copy of the same Gaussian kernel weights used elsewhere
        { 2,  4,  5,  4,  2},
        { 4,  9, 12,  9,  4},
        { 5, 12, 15, 12,  5},
        { 4,  9, 12,  9,  4},
        { 2,  4,  5,  4,  2}
    };

    for (int r = 0; r < height; r++) {
        // r = current row in ORIGINAL (unpadded) image coordinates
        for (int c = 0; c < width; c++) {
            // c = current column in ORIGINAL image coordinates

            int32_t acc = 0;
            // accumulator for the weighted sum of this pixel's neighborhood

            for (int kr = 0; kr < 5; kr++) {
                // kr ranges 0-4 because we're reading from the PADDED buffer directly
                for (int kc = 0; kc < 5; kc++) {
                    // kc follows the same 0-4 logic horizontally

                    acc += static_cast<int32_t>(pad[(r + kr) * PW + (c + kc)]) * GAUSS[kr][kc];
                    // reads from the padded buffer with NO bounds checking needed —
                    // every position is guaranteed valid (real pixel or zero-padding)
                }
            }
            int32_t v = acc / 273;
            // normalize by the kernel's coefficient sum

            out[r * width + c] = v < 0 ? 0 : (v > 255 ? 255 : v);
            // clamp into valid uint8_t range before writing the output
        }
    }
    std::free(pad);
    // release the padded buffer's memory; critical since this function runs 200 times in the benchmark
}

int main() {
    const size_t pixel_count = static_cast<size_t>(W) * static_cast<size_t>(H);
    // total number of pixels in the test image, used to size every buffer below

    uint8_t* input_pixels = alloc_u8_buffer(pixel_count);
    // buffer holding the synthetic test image (source for every stage)
    uint8_t* spatial_pixels = alloc_u8_buffer(pixel_count);
    // buffer holding the result of the spatial 2D Gaussian blur
    uint8_t* separable_pixels = alloc_u8_buffer(pixel_count);
    // buffer holding the result of the separable 1D Gaussian blur
    uint8_t* mag_l1_pixels = alloc_u8_buffer(pixel_count);
    // buffer holding the L1 gradient magnitude result
    uint8_t* mag_l2_pixels = alloc_u8_buffer(pixel_count);
    // buffer holding the L2 gradient magnitude result
    uint8_t* dir_pixels = alloc_u8_buffer(pixel_count);
    // buffer holding the gradient direction classification result

    fill_test_pattern(input_pixels, W, H);
    // generates the deterministic gradient+rectangle test image into input_pixels

    Image input_img{static_cast<uint32_t>(W), static_cast<uint32_t>(H), input_pixels};
    // wraps the raw input buffer in the Image struct expected by the pipeline functions
    Image spatial_img{static_cast<uint32_t>(W), static_cast<uint32_t>(H), spatial_pixels};
    // wraps the spatial-blur output buffer the same way
    Image separable_img{static_cast<uint32_t>(W), static_cast<uint32_t>(H), separable_pixels};
    // wraps the separable-blur output buffer the same way

    gaussian_blur_5x5_spatial_2d(input_img, spatial_img);
    // runs the slow, direct 5x5 convolution once (for correctness checking, not yet timing)
    gaussian_blur_5x5_separable_1d(input_img, separable_img);
    // runs the fast, separable 1D-pass version once on the SAME input

    std::printf("Gaussian spatial vs separable max|diff|: %d\n",
    max_abs_diff_u8(spatial_pixels, separable_pixels, pixel_count));
    // compares both outputs pixel-by-pixel; prints 0 if they match exactly,
    // confirming the separable optimization is mathematically correct, not approximate

    GradientImage gradient{};
    // holds the REAL gradient data (gx, gy) that Magnitude/Direction will read from later
    sobel_3x3(spatial_img, gradient);
    // computes Sobel gradients ONCE on the blurred image, filling 'gradient'

    std::printf("============================================================\n");
    std::printf(" Phase 4 Benchmark Configuration\n");
    std::printf(" Image size: %dx%d | Iterations per stage: %d\n", W, H, ITERS);
    std::printf("============================================================\n\n");
    // prints a header summarizing the test configuration before showing results

    uint64_t t0, t1;
    // reusable timestamp variables: t0 = before, t1 = after, for each timing block below
    
    // 1. Spatial 2D Timing
    t0 = get_ns();
    // timestamp before starting the repeated calls
    for (int i = 0; i < ITERS; ++i) {
        gaussian_blur_5x5_spatial_2d(input_img, spatial_img);
        // runs the spatial Gaussian blur, discarding the timing-irrelevant fact that
        // it overwrites spatial_pixels each time (we already checked correctness above)
    }
    t1 = get_ns();
    // timestamp after all 200 repetitions finish
    unsigned long long t_spatial = (t1 - t0) / ITERS;
    // average time for ONE call, in nanoseconds

    // 1b. Padded 2D Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gaussian_blur_padded_2d(input_pixels, spatial_pixels, W, H);
        // times the RAW POINTER padded version defined earlier in this same file
    }
    t1 = get_ns();
    unsigned long long t_padded = (t1 - t0) / ITERS;

    // 2. Separable 1D Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gaussian_blur_5x5_separable_1d(input_img, separable_img);
        // times the team's official separable Gaussian implementation
    }
    t1 = get_ns();
    unsigned long long t_separable = (t1 - t0) / ITERS;

    // 3. Sobel 3x3 Timing
    GradientImage bench_target_gradient{};
    // a SEPARATE, throwaway gradient object used only for timing — keeps the real
    // 'gradient' (used later by Magnitude/Direction) untouched during this loop
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        sobel_3x3(spatial_img, bench_target_gradient);
        // times the Sobel 3x3 gradient computation
    }
    t1 = get_ns();
    unsigned long long t_sobel = (t1 - t0) / ITERS;
    gradient_free(bench_target_gradient);
    // releases the throwaway gradient's internal buffers (gx, gy arrays) immediately after timing

    // 4. Magnitude L1 Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gradient_magnitude_l1(gradient.gx, gradient.gy, mag_l1_pixels, W, H);
        // times the L1 (|Gx| + |Gy|) magnitude computation, using the REAL gradient data
    }
    t1 = get_ns();
    unsigned long long t_mag_l1 = (t1 - t0) / ITERS;

    // 5. Magnitude L2 Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gradient_magnitude_l2(gradient.gx, gradient.gy, mag_l2_pixels, W, H);
        // times the L2 (sqrt(Gx²+Gy²)) magnitude computation — expected to be slower due to sqrt
    }
    t1 = get_ns();
    unsigned long long t_mag_l2 = (t1 - t0) / ITERS;

    // 6. Direction Timing
    t0 = get_ns();
    for (int i = 0; i < ITERS; ++i) {
        gradient_direction(gradient.gx, gradient.gy, dir_pixels, W, H);
        // times the gradient direction quantization (0°/45°/90°/135°) step
    }
    t1 = get_ns();
    unsigned long long t_direction = (t1 - t0) / ITERS;

// Print individual details to match prior formats
    std::printf("1. Gaussian spatial 2D (Un-padded) : %.3f ms/call\n", static_cast<double>(t_spatial) / 1e6);
    // /1e6 converts nanoseconds to milliseconds (1ms = 1,000,000ns); %.3f shows 3 decimal places
    std::printf("1b. Gaussian Pre-padded 2D         : %.3f ms/call\n", static_cast<double>(t_padded) / 1e6);
    std::printf("2. Gaussian separable 1D           : %.3f ms/call\n", static_cast<double>(t_separable) / 1e6);
    std::printf("3. Sobel 3x3                       : %.3f ms/call\n", static_cast<double>(t_sobel) / 1e6);
    std::printf("4. Magnitude L1                    : %.3f ms/call\n", static_cast<double>(t_mag_l1) / 1e6);
    std::printf("5. Magnitude L2                    : %.3f ms/call\n", static_cast<double>(t_mag_l2) / 1e6);
    std::printf("6. Direction                       : %.3f ms/call\n", static_cast<double>(t_direction) / 1e6);

    // Dynamic Matrix Generation
    std::printf("\n========================================================================\n");
    std::printf(" Live Profile Report (Current Target Optimization Profile)\n");
    std::printf("========================================================================\n");
    std::printf("%-30s | Runtime (ms/call)\n", "Stage");
    std::printf("------------------------------------------------------------------------\n");
    std::printf("%-30s | %8.3f ms\n", "Gaussian spatial 2D (Un-padded)", static_cast<double>(t_spatial) / 1e6);
    // %-30s left-aligns the stage name in a 30-char field; %8.3f right-aligns the ms value in 8 chars
    std::printf("%-30s | %8.3f ms\n", "Gaussian Pre-padded 2D", static_cast<double>(t_padded) / 1e6);
    std::printf("%-30s | %8.3f ms\n", "Gaussian separable 1D", static_cast<double>(t_separable) / 1e6);
    std::printf("%-30s | %8.3f ms\n", "Sobel 3x3", static_cast<double>(t_sobel) / 1e6);
    std::printf("%-30s | %8.3f ms\n", "Magnitude L1", static_cast<double>(t_mag_l1) / 1e6);
    std::printf("%-30s | %8.3f ms\n", "Magnitude L2", static_cast<double>(t_mag_l2) / 1e6);
    std::printf("%-30s | %8.3f ms\n", "Direction", static_cast<double>(t_direction) / 1e6);
    std::printf("========================================================================\n");
    // this whole block re-prints the same 7 numbers in a neatly aligned table format,
    // making it easy to screenshot/compare against other optimization-level runs

    gradient_free(gradient);
    // frees the REAL gradient's internal gx/gy arrays (different from the throwaway one above)
    std::free(dir_pixels);
    // releases each allocated buffer in reverse order of typical allocation, avoiding any leaks
    std::free(mag_l2_pixels);
    std::free(mag_l1_pixels);
    std::free(separable_pixels);
    std::free(spatial_pixels);
    std::free(input_pixels);

    return 0;
    // 0 = program exited successfully
}