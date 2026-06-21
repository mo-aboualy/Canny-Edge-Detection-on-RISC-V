#include "hysteresis.h"
#include <cstring>
#include <vector>

void hysteresis_threshold(const uint8_t* nms, uint8_t* out,
                          int width, int height,
                          uint8_t low_thresh, uint8_t high_thresh) {
    const int n = width * height;

    // Pass 1: classify every pixel
    //   255 = strong,  128 = weak,  0 = non-edge
    for (int i = 0; i < n; ++i) {
        if (nms[i] >= high_thresh)      out[i] = 255;
        else if (nms[i] >= low_thresh)  out[i] = 128;
        else                             out[i] = 0;
    }

    // Pass 2: promote weak pixels that are 8-connected to a strong pixel.
    // We use an iterative approach: seed the stack with all strong pixels,
    // then propagate to adjacent weak pixels (BFS/DFS flood fill).
    std::vector<int> stack;
    stack.reserve(4096);

    for (int i = 0; i < n; ++i) {
        if (out[i] == 255) stack.push_back(i);
    }

    // 8-connectivity offsets
    const int dx[8] = {-1,  0,  1, -1, 1, -1, 0, 1};
    const int dy[8] = {-1, -1, -1,  0, 0,  1, 1, 1};

    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();

        int cy = idx / width;
        int cx = idx % width;

        for (int k = 0; k < 8; ++k) {
            int nx = cx + dx[k];
            int ny = cy + dy[k];

            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;

            int nidx = ny * width + nx;
            if (out[nidx] == 128) {       // weak → promote
                out[nidx] = 255;
                stack.push_back(nidx);
            }
        }
    }

    // Pass 3: any remaining weak pixels are not connected → suppress
    for (int i = 0; i < n; ++i) {
        if (out[i] == 128) out[i] = 0;
    }
}
