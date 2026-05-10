#pragma once
#include <cstdint>

void gradient_magnitude(const int16_t* Gx, const int16_t* Gy,
                        uint8_t* mag, int width, int height);
