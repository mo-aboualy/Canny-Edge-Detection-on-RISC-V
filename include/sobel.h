#ifndef SOBEL_H
#define SOBEL_H

#include "image_io.h"
#include <cstdint>

/**
 * @file sobel.h
 * @brief Sobel 3x3 gradient computation for grayscale raw images.
 */

/**
 * @brief Holds the result of Sobel gradient computation.
 *
 * All buffers are 64-byte aligned for future RVV vectorization.
 * Border pixels (1px edge) are set to 0.
 */
struct GradientImage {
    int16_t*  gx;        /**< Horizontal gradient buffer (signed 16-bit). */
    int16_t*  gy;        /**< Vertical gradient buffer (signed 16-bit). */
    uint16_t* magnitude; /**< Gradient magnitude, clamped to [0, 255]. */
    uint32_t  width;     /**< Image width in pixels. */
    uint32_t  height;    /**< Image height in pixels. */
};

/**
 * @brief Frees all buffers allocated inside a GradientImage.
 *
 * @param g The GradientImage whose buffers will be freed.
 */
void gradient_free(GradientImage& g);

/**
 * @brief Applies Sobel 3x3 gradient operator to the input image.
 *
 * Computes gx, gy, and magnitude. Allocates all output buffers internally
 * using aligned_alloc(64, ...). Caller is responsible for calling
 * gradient_free() when done.
 *
 * @param input  Source grayscale image (typically the blurred image).
 * @param output GradientImage to be populated (buffers allocated internally).
 */
void sobel_3x3(const Image& input, GradientImage& output);

#endif // SOBEL_H