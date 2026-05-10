/**
 * @file verify_io.cpp
 * @brief Test utility to verify Phase 2.1 Raw Image I/O implementation.
 */

#include "image_io.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

/**
 * Creates a 100x100 test pattern: 
 * A 50x50 white square (255) centered on a black background (0).
 */
int main() {
    const uint32_t width = 100;
    const uint32_t height = 100;
    const char* filename = "test_pattern.raw";
    
    Image test_img;
    test_img.width = width;
    test_img.height = height;

    // 1. ALLOCATION
    // Per guide: Use 64-byte alignment for future RVV (Vector) compatibility.
    size_t data_size = (size_t)width * height;
    size_t aligned_size = (data_size + 63) & ~63; // Round up to nearest 64
    test_img.pixels = (uint8_t*)aligned_alloc(64, aligned_size);

    if (!test_img.pixels) {
        std::cerr << "Error: Failed to allocate aligned memory." << std::endl;
        return 1;
    }

    // 2. PATTERN GENERATION
    // Initialize background to black (0)
    std::memset(test_img.pixels, 0, data_size);

    // Draw a white square (255) from (25,25) to (74,74)
    for (uint32_t y = 25; y < 75; ++y) {
        for (uint32_t x = 25; x < 75; ++x) {
            // Row-major indexing: index = y * width + x
            test_img.pixels[y * width + x] = 255;
        }
    }

    // 3. FILE I/O TEST
    std::cout << "Attempting to write: " << filename << "..." << std::endl;
    if (image_write(filename, test_img)) {
        std::cout << "Success: File written to disk." << std::endl;
    } else {
        std::cerr << "Error: Failed to write file." << std::endl;
        image_free(test_img);
        return 1;
    }

    // 4. CLEANUP
    image_free(test_img);
    std::cout << "Verification utility finished successfully." << std::endl;

    return 0;
}