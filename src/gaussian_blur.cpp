#include "gaussian_blur.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int32_t ALIGNMENT = 64;

/*
 * Separable 5x5 Gaussian kernel:
 *
 * 1D vector:
 *   [1 4 6 4 1], sum = 16
 *
 * Spatial 2D matrix is the outer product of the 1D vector:
 *
 *   1   4   6   4   1
 *   4  16  24  16   4
 *   6  24  36  24   6
 *   4  16  24  16   4
 *   1   4   6   4   1
 *
 * Matrix sum = 256.
 *
 * This makes the spatial 2D implementation and separable 1D
 * implementation mathematically comparable.
 */
constexpr int32_t GAUSS_1D[5] = {1, 4, 6, 4, 1};

constexpr int32_t GAUSS_2D[5][5] = {
    {1,  4,  6,  4, 1},
    {4, 16, 24, 16, 4},
    {6, 24, 36, 24, 6},
    {4, 16, 24, 16, 4},
    {1,  4,  6,  4, 1}
};

static inline uint8_t clamp_u8(int32_t value) {
    if (value < 0) {
        return 0;
    }

    if (value > 255) {
        return 255;
    }

    return static_cast<uint8_t>(value);
}

static inline size_t round_up_to_alignment(size_t bytes) {
    return (bytes + (ALIGNMENT - 1)) & ~(static_cast<size_t>(ALIGNMENT) - 1);
}

} // namespace

void gaussian_blur_5x5_spatial_2d(const Image& in, Image& out) {
    const int32_t width = static_cast<int32_t>(in.width);
    const int32_t height = static_cast<int32_t>(in.height);

    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            int32_t accumulator = 0;

            for (int32_t ky = -2; ky <= 2; ++ky) {
                for (int32_t kx = -2; kx <= 2; ++kx) {
                    const int32_t yy = y + ky;
                    const int32_t xx = x + kx;

                    /*
                     * Zero-padding:
                     * If the neighbor is outside the image, it contributes 0.
                     */
                    if (yy >= 0 && yy < height && xx >= 0 && xx < width) {
                        const uint8_t pixel =
                            in.pixels[static_cast<size_t>(yy) * width + xx];

                        const int32_t weight = GAUSS_2D[ky + 2][kx + 2];
                        accumulator += static_cast<int32_t>(pixel) * weight;
                    }
                }
            }

            /*
             * Kernel sum = 256.
             * Division by 256 is implemented as a right shift by 8.
             */
            out.pixels[static_cast<size_t>(y) * width + x] =
                clamp_u8(accumulator >> 8);
        }
    }
}

void gaussian_blur_5x5_separable_1d(const Image& in, Image& out) {
    const int32_t width = static_cast<int32_t>(in.width);
    const int32_t height = static_cast<int32_t>(in.height);
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);

    /*
     * Temporary buffer stores the horizontal pass before the vertical pass.
     * int32_t is used because horizontal sums can exceed uint8_t.
     */
    const size_t temp_bytes =
        round_up_to_alignment(count * sizeof(int32_t));

    int32_t* temp = static_cast<int32_t*>(
        aligned_alloc(ALIGNMENT, temp_bytes)
    );

    if (!temp) {
        /*
         * Fail safely: produce a black image if allocation fails.
         * This should not happen for the benchmark sizes.
         */
        std::memset(out.pixels, 0, count);
        return;
    }

    /*
     * Pass 1: horizontal 1D convolution.
     * Each pixel uses five horizontal neighbors.
     */
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            int32_t accumulator = 0;

            for (int32_t kx = -2; kx <= 2; ++kx) {
                const int32_t xx = x + kx;

                if (xx >= 0 && xx < width) {
                    const uint8_t pixel =
                        in.pixels[static_cast<size_t>(y) * width + xx];

                    accumulator += static_cast<int32_t>(pixel) *
                                   GAUSS_1D[kx + 2];
                }
            }

            temp[static_cast<size_t>(y) * width + x] = accumulator;
        }
    }

    /*
     * Pass 2: vertical 1D convolution over the temporary buffer.
     *
     * Total normalization:
     *   horizontal sum = 16
     *   vertical sum   = 16
     *   total          = 256
     */
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            int32_t accumulator = 0;

            for (int32_t ky = -2; ky <= 2; ++ky) {
                const int32_t yy = y + ky;

                if (yy >= 0 && yy < height) {
                    accumulator += temp[static_cast<size_t>(yy) * width + x] *
                                   GAUSS_1D[ky + 2];
                }
            }

            out.pixels[static_cast<size_t>(y) * width + x] =
                clamp_u8(accumulator >> 8);
        }
    }

    std::free(temp);
}

void gaussian_blur_5x5(const Image& in, Image& out) {
    /*
     * Compatibility wrapper for the existing pipeline.
     * The default path remains the direct spatial implementation.
     */
    gaussian_blur_5x5_spatial_2d(in, out);
}
