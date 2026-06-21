/**
 * @file Test_sobel.cpp
 * @brief Phase 2.3 Verification: Applies Sobel Gradient to a blurred raw image.
 *
 * Usage: ./test_sobel <input.raw> <output_magnitude.raw> <width> <height>
 *
 * Example:
 *   ./test_sobel Tool/test_blurred.raw Tool/test_magnitude.raw 512 512
 *
 * Like Test_blur.cpp, this is a MANUAL/VISUAL check. It takes the output
 * of the Gaussian blur stage (a smoothed image) and runs it through the
 * real Sobel implementation, saving the gradient magnitude as a viewable
 * image. A correctly working Sobel stage should produce an image that
 * looks like a black background with bright white outlines tracing the
 * edges of objects in the original photo.
 */

#include "image_io.h"
#include "sobel.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {

    // ─────────────────────────────────────────────
    // 1. PARSE ARGUMENTS
    // ─────────────────────────────────────────────
    // All arguments optional with defaults pointing at the standard
    // Tool/ folder paths, so the typical workflow is:
    //   1) Tool/prepare_image.py  -> creates input_512.raw
    //   2) Test_blur              -> creates the blurred image
    //   3) Test_sobel              -> creates the final edge-magnitude image
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
    // Sobel is meant to run AFTER Gaussian blur in the pipeline -- blurring
    // first reduces noise so Sobel doesn't pick up false edges from
    // sensor/compression noise. This is why the default input file name
    // assumes it's reading the blur stage's output, not the raw photo.
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

    // Note: sobel_3x3 allocates gx, gy, and magnitude internally.
    // Unlike gaussian_blur_5x5() (where the caller pre-allocates the
    // output buffer), sobel_3x3() owns its own output allocation -- the
    // caller only needs to set width/height and initialize the pointers
    // to nullptr before the call (good practice, even though sobel_3x3
    // overwrites them).
    gradient.gx        = nullptr;
    gradient.gy        = nullptr;
    gradient.magnitude = nullptr;
    gradient.width     = width;
    gradient.height    = height;

    // ─────────────────────────────────────────────
    // 4. EXECUTE SOBEL
    // ─────────────────────────────────────────────
    // This is the real scalar baseline implementation -- the same function
    // tested for correctness in host_tests.cpp (vertical/horizontal edge
    // detection, zero gradient on uniform images, zeroed borders).
    std::cout << "Step 2: Applying Sobel 3x3 gradient..." << std::endl;

    sobel_3x3(input_img, gradient);

    std::cout << "Step 3: Sobel gradient computed successfully." << std::endl;

    // ─────────────────────────────────────────────
    // 5. SAVE MAGNITUDE AS RAW (uint16_t → uint8_t)
    // ─────────────────────────────────────────────
    // magnitude is already clamped to [0, 255] so safe to cast.
    // GradientImage.magnitude is stored as uint16_t internally (gradient
    // values before normalization can briefly exceed 255), but by the time
    // sobel_3x3() returns, it has already been normalized/clamped into the
    // [0,255] range -- so truncating to uint8_t here loses no information.
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
    // gradient_free() is required (not just image_free()) because
    // GradientImage owns three separate internally-allocated buffers
    // (gx, gy, magnitude), all of which need releasing -- a plain
    // image_free() wouldn't know about them since it only operates
    // on the simpler Image struct.
    gradient_free(gradient);
    image_free(input_img);
    image_free(magnitude_img);

    std::cout << "----------------------------------------" << std::endl;
    std::cout << " Done." << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    return 0;
}
