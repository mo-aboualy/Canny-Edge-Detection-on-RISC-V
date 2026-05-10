#include "gaussian_blur.h"
#include <cstdint>

/**
 * Implementation of 5x5 Gaussian Blur using a Scalar Baseline approach.
 */
void gaussian_blur_5x5(const Image& in, Image& out) {
    const int32_t width  = in.width;
    const int32_t height = in.height;

    // 5x5 Gaussian Kernel (Integer-based for performance and consistency)
    const int32_t kernel[5][5] = {
        {1,  4,  7,  4, 1},
        {4, 16, 26, 16, 4},
        {7, 26, 41, 26, 7},
        {4, 16, 26, 16, 4},
        {1,  4,  7,  4, 1}
    };
    
    // The sum of all weights in the kernel (used for normalization)
    const int32_t kernel_sum = 273;

    // Outer loops: Iterate over every pixel in the image (y = row, x = column)
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            
            int32_t accumulator = 0;

            // Inner loops: Iterate over the 5x5 neighborhood around (x, y)
            for (int32_t ky = -2; ky <= 2; ++ky) {
                for (int32_t kx = -2; kx <= 2; ++kx) {
                    
                    // Calculate coordinates of the neighbor pixel
                    int32_t current_y = y + ky;
                    int32_t current_x = x + kx;

                    // EDGE HANDLING (Zero Padding):
                    // Only add to the accumulator if the neighbor is inside image bounds.
                    // If it is outside, we effectively treat the neighbor as 0.
                    if (current_x >= 0 && current_x < width && 
                        current_y >= 0 && current_y < height) {
                        
                        uint8_t pixel_value = in.pixels[current_y * width + current_x];
                        int32_t weight = kernel[ky + 2][kx + 2];
                        
                        accumulator += (int32_t)pixel_value * weight;
                    }
                }
            }

            // Normalize the result by dividing by the kernel sum
            // and store it in the output image buffer.
            out.pixels[y * width + x] = (uint8_t)(accumulator / kernel_sum);
        }
    }
}