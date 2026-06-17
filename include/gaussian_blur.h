#ifndef GAUSSIAN_BLUR_H
#define GAUSSIAN_BLUR_H

#include "image_io.h"

/**
 * @file gaussian_blur.h
 * @brief Gaussian blur implementations for grayscale raw images.
 *
 * Phase 4 compares two scalar Gaussian implementations:
 * 1. Spatial 2D convolution using a 5x5 matrix.
 * 2. Separable convolution using a 1D vector in two passes.
 */

/**
 * @brief Applies a 5x5 Gaussian blur using a spatial 2D matrix.
 *
 * This performs one direct 5x5 convolution per output pixel.
 * Zero-padding is used at image boundaries.
 */
void gaussian_blur_5x5_spatial_2d(const Image& in, Image& out);

/**
 * @brief Applies a 5x5 Gaussian blur using a separable 1D vector.
 *
 * This performs:
 *   horizontal 1D convolution
 *   then vertical 1D convolution
 *
 * The goal is to compare 25 operations per pixel against roughly 10
 * operations per pixel.
 * Zero-padding is used at image boundaries.
 */
void gaussian_blur_5x5_separable_1d(const Image& in, Image& out);

/**
 * @brief Default Gaussian blur entry point used by the existing pipeline.
 *
 * Kept for compatibility with the old code.
 * Currently calls the spatial 2D version.
 */
void gaussian_blur_5x5(const Image& in, Image& out);

#endif // GAUSSIAN_BLUR_H
