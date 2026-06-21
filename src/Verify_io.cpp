/**
 * @file verify_io.cpp
 * @brief Test utility to verify Phase 2.1 Raw Image I/O implementation.
 *
 * This is a MANUAL/VISUAL sanity check, separate from the automated
 * GoogleTest suite in host_tests.cpp. It exists so a human can generate
 * a known test pattern, write it to disk, and later open it with
 * Tool/view_result.py to visually confirm the bytes on disk are correct.
 */

#include "image_io.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

/**
 * Creates a 100x100 test pattern:
 * A 50x50 white square (255) centered on a black background (0).
 *
 * Why a square instead of random noise? A simple geometric shape with
 * sharp, known edges is easy to verify by eye after round-tripping
 * through the raw file format -- if the square is misaligned, smeared,
 * or in the wrong position, the I/O logic (or the row-major indexing)
 * has a bug.
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
    // RVV loads at VLEN=512 read 64 bytes per vector register, so aligning
    // every image buffer to a 64-byte boundary now means the RVV kernels
    // written in Phase 6 can use aligned (faster) load instructions later,
    // without needing to re-allocate or copy memory.
    size_t data_size = (size_t)width * height;
    // Round data_size UP to the next multiple of 64.
    // aligned_alloc() requires the allocation size itself to be a multiple
    // of the alignment, not just the starting address.
    // (data_size + 63) & ~63  =>  add 63, then clear the low 6 bits.
    // This is the standard "round up to power-of-two multiple" bit trick.
    size_t aligned_size = (data_size + 63) & ~63; // Round up to nearest 64
    test_img.pixels = (uint8_t*)aligned_alloc(64, aligned_size);

    if (!test_img.pixels) {
        std::cerr << "Error: Failed to allocate aligned memory." << std::endl;
        return 1;
    }

    // 2. PATTERN GENERATION
    // Initialize background to black (0).
    // memset is safe here because pixel value 0 == byte value 0 for
    // every byte in the buffer -- this would NOT work for a non-zero fill.
    std::memset(test_img.pixels, 0, data_size);

    // Draw a white square (255) from (25,25) to (74,74) -- a 50x50 region
    // centered in the 100x100 canvas (25 pixels of black margin on each side).
    for (uint32_t y = 25; y < 75; ++y) {
        for (uint32_t x = 25; x < 75; ++x) {
            // Row-major indexing: index = y * width + x
            // This matches the layout documented in image_io.h: pixels are
            // stored row by row, left to right, top to bottom -- identical
            // to how numpy.fromfile(...).reshape(H, W) expects the data
            // in Tool/view_result.py.
            test_img.pixels[y * width + x] = 255;
        }
    }

    // 3. FILE I/O TEST
    // This is the actual function under test: image_write() from image_io.cpp.
    // Writing exactly width*height bytes with no header is the "raw grayscale"
    // format required by the project spec.
    std::cout << "Attempting to write: " << filename << "..." << std::endl;
    if (image_write(filename, test_img)) {
        std::cout << "Success: File written to disk." << std::endl;
    } else {
        std::cerr << "Error: Failed to write file." << std::endl;
        image_free(test_img);
        return 1;
    }

    // 4. CLEANUP
    // Must call image_free() because the pixel buffer was allocated with
    // aligned_alloc(), not new[] -- mismatching allocator/deallocator pairs
    // is undefined behavior, so image_free() exists specifically to pair
    // correctly with aligned_alloc().
    image_free(test_img);
    std::cout << "Verification utility finished successfully." << std::endl;

    return 0;
}
