#pragma once
#include <cstdint>

void gradient_direction(const int16_t* Gx, const int16_t* Gy,
                        uint8_t* dir, int width, int height);
