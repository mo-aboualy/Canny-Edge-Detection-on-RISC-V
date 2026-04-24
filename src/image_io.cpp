#include "image_io.h"

#include <cstdio>

bool image_read(const char* path, uint32_t width, uint32_t height, Image& out) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    size_t expected = (size_t)width * height;

    out.width  = width;
    out.height = height;
    out.pixels.resize(expected);

    size_t n = fread(out.pixels.data(), 1, expected, f);
    fclose(f);

    if (n != expected) {
        out.pixels.clear();
        return false;
    }

    return true;
}

bool image_write(const char* path, const Image& img) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }

    size_t expected = (size_t)img.width * img.height;
    size_t n = fwrite(img.pixels.data(), 1, expected, f);
    fclose(f);

    return n == expected;
}
