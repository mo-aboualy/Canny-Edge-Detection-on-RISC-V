/**
 * @file test_blur.cpp
 * @brief Phase 2.2 Verification: Applies Gaussian Blur to a raw image.
 *
 * Like Verify_io.cpp, this is a MANUAL/VISUAL check -- it runs the real
 * production gaussian_blur_5x5() function on a real image file (not a
 * synthetic in-memory pattern), so a human can open the before/after
 * with Tool/view_result.py and visually confirm the blur looks correct
 * (no smearing artifacts, no shifted image, no garbage at the borders).
 *
 * This complements the automated GoogleTest invariant tests in
 * host_tests.cpp, which check mathematical properties (uniform image
 * stays uniform, impulse spreads to neighbors) but never actually look
 * at a real photograph.
 */

#include "image_io.h"
#include "gaussian_blur.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // 1. Image Parameters
    // Command-line arguments are all optional, with sensible defaults,
    // so this can be run with zero setup (./test_blur) as long as
    // input_512.raw already exists from Tool/prepare_image.py, or
    // with explicit files/sizes (./test_blur in.raw out.raw 256 256)
    // for testing other image sizes.
    const char* input_file  = (argc > 1) ? argv[1] : "input_512.raw";
    const char* output_file = (argc > 2) ? argv[2] : "output_blurred_512.raw";
    const uint32_t width    = (argc > 3) ? (uint32_t)std::atoi(argv[3]) : 512;
    const uint32_t height   = (argc > 4) ? (uint32_t)std::atoi(argv[4]) : 512;

    Image input_img;
    Image output_img;

    // 2. LOAD INPUT
    // image_read() requires the caller to already know width/height
    // (the raw format has no header to read them from), which is why
    // they must be passed in explicitly here.
    if (!image_read(input_file, width, height, input_img)) {
        std::cerr << "Error: Could not read " << input_file << ". Run verify_io first!" << std::endl;
        return 1;
    }

    // 3. PRE-ALLOCATE OUTPUT
    // Unlike sobel_3x3() (which allocates its own output buffers
    // internally), gaussian_blur_5x5() expects the caller to pre-allocate
    // the output Image with the same dimensions as the input. This matches
    // the function's documented contract in gaussian_blur.h.
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
    // This calls the actual scalar baseline implementation -- the same
    // function that gets unit-tested in host_tests.cpp and later compared
    // against the RVV vectorized version for equivalence in Phase 6.
    std::cout << "Processing: Applying 5x5 Gaussian Blur..." << std::endl;
    gaussian_blur_5x5(input_img, output_img);

    // 5. SAVE RESULT
    // Written in the same raw format as the input, so it can be fed
    // directly into Test_sobel.cpp as the next pipeline stage, or
    // opened with Tool/view_result.py for a visual check.
    if (image_write(output_file, output_img)) {
        std::cout << "Success: Blurred image saved as " << output_file << std::endl;
    } else {
        std::cerr << "Error: Failed to write output file." << std::endl;
    }

    // 6. CLEANUP
    // Both buffers were allocated with aligned_alloc() (one inside
    // image_read(), one manually above), so both must go through
    // image_free() to avoid a mismatched allocator/deallocator pair.
    image_free(input_img);
    image_free(output_img);

    return 0;
}
