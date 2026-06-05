/**
 * @file test_blur.cpp
 * @brief Phase 2.2 Verification: Applies Gaussian Blur to a raw image.
 */

#include "image_io.h"
#include "gaussian_blur.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // 1. Image Parameters
    const char* input_file  = (argc > 1) ? argv[1] : "input_512.raw";
    const char* output_file = (argc > 2) ? argv[2] : "output_blurred_512.raw";
    const uint32_t width    = (argc > 3) ? (uint32_t)std::atoi(argv[3]) : 512;
    const uint32_t height   = (argc > 4) ? (uint32_t)std::atoi(argv[4]) : 512;

    Image input_img;
    Image output_img;

    // 2. LOAD INPUT
    if (!image_read(input_file, width, height, input_img)) {
        std::cerr << "Error: Could not read " << input_file << ". Run verify_io first!" << std::endl;
        return 1;
    }

    // 3. PRE-ALLOCATE OUTPUT
    output_img.width = width;
    output_img.height = height;

    size_t data_size = (size_t)width * height;
    size_t aligned_size = (data_size + 63) & ~63;
    output_img.pixels = (uint8_t*)aligned_alloc(64, aligned_size);

    if (!output_img.pixels) {
        std::cerr << "Error: Failed to allocate aligned memory for output." << std::endl;
        image_free(input_img);
        return 1;
    }

    // 4. EXECUTE BLUR
    std::cout << "Processing: Applying 5x5 Gaussian Blur..." << std::endl;
    gaussian_blur_5x5(input_img, output_img);

    // 5. SAVE RESULT
    if (image_write(output_file, output_img)) {
        std::cout << "Success: Blurred image saved as " << output_file << std::endl;
    } else {
        std::cerr << "Error: Failed to write output file." << std::endl;
    }

    // 6. CLEANUP
    image_free(input_img);
    image_free(output_img);

    return 0;
}