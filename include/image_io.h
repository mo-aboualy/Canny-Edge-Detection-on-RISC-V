#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include <cstdint>
#include <cstddef>

// Loads a raw grayscale image.
// Returns a pointer to the aligned memory buffer, or nullptr if it fails.
uint8_t* load_raw_image(const char* filename, size_t width, size_t height);

// Saves a raw grayscale image buffer to a file.
void save_raw_image(const char* filename, const uint8_t* data, size_t width, size_t height);

#endif

