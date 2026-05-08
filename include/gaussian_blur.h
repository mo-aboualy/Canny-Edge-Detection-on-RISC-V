#ifndef GAUSSIAN_BLUR_H
#define GAUSSIAN_BLUR_H

#include "image_io.h"

/**
 * @file gaussian_blur.h
 * @brief 5x5 Gaussian Blur for grayscale raw images.
 */

/**
 * @brief Applies a 5x5 Gaussian Blur to the input image.
 *
 * Reduces noise before gradient computation.
 * Output buffer must be pre-allocated with the same dimensions as input.
 * Memory must be 64-byte aligned for future RVV vectorization.
 *
 * @param in  Source grayscale image.
 * @param out Destination image (pre-allocated, same dimensions as in).
 */
void gaussian_blur_5x5(const Image& in, Image& out);

#endif // GAUSSIAN_BLUR_H