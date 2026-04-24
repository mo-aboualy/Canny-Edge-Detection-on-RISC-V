#pragma once

#include <cstdint>
#include <vector>

struct Image {
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> pixels; // row-major, 1 byte per pixel
};

// Read a raw grayscale image: a plain binary file of exactly width*height bytes.
// Returns true on success, false if the file cannot be opened or is the wrong size.
bool image_read(const char* path, uint32_t width, uint32_t height, Image& out);

// Write a raw grayscale image: width*height bytes, no header.
// Returns true on success, false if the file cannot be written.
bool image_write(const char* path, const Image& img);
