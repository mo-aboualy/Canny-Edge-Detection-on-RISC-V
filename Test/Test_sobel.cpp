/**
 * @file Test_sobel.cpp
 * @brief Phase 2.3 Verification: Applies Sobel Gradient to a blurred raw image.
 * 
 * Usage: ./test_sobel <input.raw> <output_magnitude.raw> <width> <height>
 * 
 * Example:
 *   ./test_sobel Tool/test_blurred.raw Tool/test_magnitude.raw 512 512
 */

#include "image_io.h"
#include "sobel.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {

    // ─────────────────────────────────────────────
    // 1. PARSE ARGUMENTS
    // ─────────────────────────────────────────────
    const char* input_file  = (argc > 1) ? argv[1] : "Tool/test_blurred.raw";
    const char* output_file = (argc > 2) ? argv[2] : "Tool/test_magnitude.raw";
    const uint32_t width    = (argc > 3) ? (uint32_t)std::atoi(argv[3]) : 512;
    const uint32_t height   = (argc > 4) ? (uint32_t)std::atoi(argv[4]) : 512;

    std::cout << "----------------------------------------" << std::endl;
    std::cout << " Sobel Gradient Test" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << " Input  : " << input_file  << std::endl;
    std::cout << " Output : " << output_file << std::endl;
    std::cout << " Size   : " << width << "x" << height << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    // ─────────────────────────────────────────────
    // 2. LOAD INPUT (blurred image)
    // ─────────────────────────────────────────────
    Image input_img;

    if (!image_read(input_file, width, height, input_img)) {
        std::cerr << "Error: Could not read '" << input_file << "'." << std::endl;
        std::cerr << "       Make sure you ran the blur step first." << std::endl;
        return 1;
    }

    std::cout << "Step 1: Input loaded successfully." << std::endl;

    // ─────────────────────────────────────────────
    // 3. PRE-ALLOCATE GRADIENT OUTPUT
    // ─────────────────────────────────────────────
    GradientImage gradient;

    // Note: sobel_3x3 allocates gx, gy, and magnitude internally
    gradient.gx        = nullptr;
    gradient.gy        = nullptr;
    gradient.magnitude = nullptr;
    gradient.width     = width;
    gradient.height    = height;

    // ─────────────────────────────────────────────
    // 4. EXECUTE SOBEL
    // ─────────────────────────────────────────────
    std::cout << "Step 2: Applying Sobel 3x3 gradient..." << std::endl;

    sobel_3x3(input_img, gradient);

    std::cout << "Step 3: Sobel gradient computed successfully." << std::endl;

    // ─────────────────────────────────────────────
    // 5. SAVE MAGNITUDE AS RAW (uint16_t → uint8_t)
    // ─────────────────────────────────────────────
    // magnitude is already clamped to [0, 255] so safe to cast
    Image magnitude_img;
    magnitude_img.width  = width;
    magnitude_img.height = height;

    size_t count        = (size_t)width * height;
    size_t aligned_size = (count + 63) & ~63;
    magnitude_img.pixels = (uint8_t*)aligned_alloc(64, aligned_size);

    if (!magnitude_img.pixels) {
        std::cerr << "Error: Failed to allocate memory for magnitude image." << std::endl;
        gradient_free(gradient);
        image_free(input_img);
        return 1;
    }

    for (size_t i = 0; i < count; i++) {
        magnitude_img.pixels[i] = (uint8_t)gradient.magnitude[i];
    }

    if (image_write(output_file, magnitude_img)) {
        std::cout << "Step 4: Magnitude saved as '" << output_file << "'." << std::endl;
    } else {
        std::cerr << "Error: Failed to write output file." << std::endl;
    }

    // ─────────────────────────────────────────────
    // 6. CLEANUP
    // ─────────────────────────────────────────────
    gradient_free(gradient);
    image_free(input_img);
    image_free(magnitude_img);

    std::cout << "----------------------------------------" << std::endl;
    std::cout << " Done." << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    return 0;
}