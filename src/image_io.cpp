#include "image_io.h"
#include <cstdio>
#include <cstdlib> // Required for aligned_alloc

/**
 * Reads a raw grayscale image from disk.
 * Per Phase 2.1: The file must be exactly width * height bytes[cite: 56].
 */
bool image_read(const char* path, uint32_t width, uint32_t height, Image& out) {
    // Open file in "Read Binary" mode to avoid line-ending translations 
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    size_t expected = (size_t)width * height;
    
    // Ensure the size for aligned_alloc is a multiple of the alignment (64)
    // This is a safety measure for the memory allocator
    size_t alloc_size = (expected + 63) & ~63; 

    out.width  = width;
    out.height = height;
    
    // CRITICAL: Use aligned_alloc(64, size) instead of malloc/std::vector[cite: 59].
    // This alignment is required for RISC-V vector load intrinsics later.
    out.pixels = (uint8_t*)aligned_alloc(64, alloc_size);

    if (!out.pixels) {
        fclose(f);
        return false;
    }

    // Read raw pixel data directly into the aligned buffer [cite: 56]
    size_t n = fread(out.pixels, 1, expected, f);
    fclose(f);

    // Verify that the file contained the expected amount of data [cite: 56]
    if (n != expected) {
        free(out.pixels);
        out.pixels = nullptr;
        return false;
    }

    return true;
}

/**
 * Writes raw pixel data to a file.
 * Per Phase 2.1: No headers, no compression.
 */
bool image_write(const char* path, const Image& img) {
    // Open file in "Write Binary" mode 
    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }

    size_t total_bytes = (size_t)img.width * img.height;
    
    // Write exactly width * height bytes to the file [cite: 56]
    size_t n = fwrite(img.pixels, 1, total_bytes, f);
    fclose(f);

    // Confirm the write was successful
    return n == total_bytes;
}

/**
 * Helper to clean up the memory allocated by aligned_alloc.
 */
void image_free(Image& img) {
    if (img.pixels) {
        free(img.pixels);
        img.pixels = nullptr;
    }
}