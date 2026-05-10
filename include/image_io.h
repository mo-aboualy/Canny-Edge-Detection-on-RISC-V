#ifndef image_io_h
#define image_io_h

#include <cstdint>

/**
 * Image structure using raw pixels for manual memory management.
 * Essential for Phase 2.1 and future Phase 6 vectorization. [cite: 54, 59, 148]
 */
struct Image {
    uint32_t width;
    uint32_t height;
    uint8_t* pixels; // row-major, 1 byte per pixel [cite: 56]
};

/**
 * Read a raw grayscale image: exactly width*height bytes. [cite: 56]
 * Uses aligned_alloc(64, ...) for future RVV performance. [cite: 59, 60]
 */
bool image_read(const char* path, uint32_t width, uint32_t height, Image& out);

/**
 * Write a raw grayscale image: width*height bytes, no header. [cite: 56, 57]
 */
bool image_write(const char* path, const Image& img);

/**
 * Frees the memory allocated by aligned_alloc.
 */
void image_free(Image& img);

#endif // IMAGE_IO_HPP