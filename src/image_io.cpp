#include "image_io.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>

uint8_t* load_raw_image(const char* filename, size_t width, size_t height) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        std::cerr << "Error: Could not open file " << filename << " for reading.\n";
        return nullptr;
    }

    size_t size = width * height;
    
    // C++ aligned_alloc requires the size to be a multiple of the alignment (64).
    // This bitwise math rounds the size up to the nearest multiple of 64.
    size_t alloc_size = (size + 63) & ~63; 
    
    // Allocate 64-byte aligned memory as required by the hints guide
    uint8_t* buffer = static_cast<uint8_t*>(aligned_alloc(64, alloc_size));

    if (!buffer) {
        std::cerr << "Error: Memory allocation failed.\n";
        fclose(file);
        return nullptr;
    }

    size_t bytes_read = fread(buffer, 1, size, file);
    if (bytes_read != size) {
        std::cerr << "Warning: Read " << bytes_read << " bytes, expected " << size << ".\n";
    }

    fclose(file);
    return buffer;
}

void save_raw_image(const char* filename, const uint8_t* data, size_t width, size_t height) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        std::cerr << "Error: Could not open file " << filename << " for writing.\n";
        return;
    }

    size_t size = width * height;
    size_t bytes_written = fwrite(data, 1, size, file);
    if (bytes_written != size) {
        std::cerr << "Warning: Wrote " << bytes_written << " bytes, expected " << size << ".\n";
    }

    fclose(file);
}
