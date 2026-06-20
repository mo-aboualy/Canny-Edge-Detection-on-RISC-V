#pragma once
#include <cstdint>

/**
 * @file nms.h
 * @brief Non-Maximum Suppression for Canny Edge Detection.
 *
 * Thins edges to 1-pixel width by suppressing gradient pixels that are
 * not local maxima along the gradient direction.
 */

/**
 * @brief Applies non-maximum suppression to gradient magnitude.
 *
 * For each pixel, compares its magnitude against the two neighbours in the
 * gradient direction. If the pixel is not the local maximum, it is zeroed out.
 *
 * Direction encoding (matches direction.h):
 *   0 = 0°   (horizontal)   → compare left/right  (x-1, x+1)
 *   1 = 45°  (diagonal ↗)  → compare (x+1,y-1) / (x-1,y+1)
 *   2 = 90°  (vertical)    → compare above/below (y-1, y+1)
 *   3 = 135° (diagonal ↘)  → compare (x-1,y-1) / (x+1,y+1)
 *
 * Border pixels are always set to 0.
 *
 * @param mag    Input gradient magnitude (uint8_t, same dims as image).
 * @param dir    Gradient direction map produced by gradient_direction().
 * @param out    Output thinned edge map (pre-allocated, same dims).
 * @param width  Image width in pixels.
 * @param height Image height in pixels.
 */
void non_maximum_suppression(const uint8_t* mag, const uint8_t* dir,
                             uint8_t* out, int width, int height);
