#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <cstdint>

// ============================================================================
// PHASE 2: SCALAR BASELINE PIPELINE IMPLEMENTATIONS
// ============================================================================

// 2.2 Gaussian Blur: 5x5 kernel parameterized as a C++ template
template<typename PixelType, typename AccType, typename KernelType>
void gaussian_blur_scalar(const PixelType* input, PixelType* output, int width, int height) {
    const KernelType kernel[5][5] = {
        {1,  4,  7,  4, 1},
        {4, 16, 26, 16, 4},
        {7, 26, 41, 26, 7},
        {4, 16, 26, 16, 4},
        {1,  4,  7,  4, 1}
    };
    
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            AccType sum = 0;
            for (int ky = -2; ky <= 2; ++ky) {
                int py = r + ky;
                for (int kx = -2; kx <= 2; ++kx) {
                    int px = c + kx;
                    if (py >= 0 && py < height && px >= 0 && px < width) {
                        sum += static_cast<AccType>(input[py * width + px]) * kernel[ky + 2][kx + 2];
                    }
                }
            }
            AccType val = sum / 273;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            output[r * width + c] = static_cast<PixelType>(val);
        }
    }
}

// 2.3 Sobel Gradient Computation: 3x3 horizontal and vertical kernels
template<typename PixelType, typename GradeType, typename KernelType>
void sobel_gradients_scalar(const PixelType* input, GradeType* Gx, GradeType* Gy, int width, int height) {
    const KernelType kx[3][3] = {
        {-1, 0, 1},
        {-2, 0, 2},
        {-1, 0, 1}
    };
    const KernelType ky[3][3] = {
        {-1, -2, -1},
        { 0,  0,  0},
        { 1,  2,  1}
    };

    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            int32_t sum_x = 0;
            int32_t sum_y = 0;
            for (int k_y = -1; k_y <= 1; ++k_y) {
                int py = r + k_y;
                for (int k_x = -1; k_x <= 1; ++k_x) {
                    int px = c + k_x;
                    if (py >= 0 && py < height && px >= 0 && px < width) {
                        PixelType pixel = input[py * width + px];
                        sum_x += static_cast<int32_t>(pixel) * kx[k_y + 1][k_x + 1];
                        sum_y += static_cast<int32_t>(pixel) * ky[k_y + 1][k_x + 1];
                    }
                }
            }
            Gx[r * width + c] = static_cast<GradeType>(sum_x);
            Gy[r * width + c] = static_cast<GradeType>(sum_y);
        }
    }
}

// 2.4 Gradient Magnitude: Two-pass normalized L1 Norm layout
template<typename GradeType, typename MagType>
void compute_magnitude_scalar(const GradeType* Gx, const GradeType* Gy, MagType* magnitude, int width, int height) {
    int size = width * height;
    MagType max_mag = 0;
    
    for (int i = 0; i < size; ++i) {
        MagType ax = (Gx[i] < 0) ? -Gx[i] : Gx[i];
        MagType ay = (Gy[i] < 0) ? -Gy[i] : Gy[i];
        MagType mag = ax + ay;
        magnitude[i] = mag;
        if (mag > max_mag) {
            max_mag = mag;
        }
    }
    
    if (max_mag > 0) {
        for (int i = 0; i < size; ++i) {
            magnitude[i] = static_cast<MagType>((static_cast<uint32_t>(magnitude[i]) * 255) / max_mag);
        }
    }
}

// 2.5 Gradient Direction: Quantized to 4 values via integer cross-multiplication
template<typename GradeType, typename DirType>
void compute_direction_scalar(const GradeType* Gx, const GradeType* Gy, DirType* direction, int width, int height) {
    int size = width * height;
    for (int i = 0; i < size; ++i) {
        GradeType x = Gx[i];
        GradeType y = Gy[i];
        GradeType ax = (x < 0) ? -x : x;
        GradeType ay = (y < 0) ? -y : y;
        
        if (static_cast<int32_t>(ay) * 5 < static_cast<int32_t>(ax) * 2) {
            direction[i] = 0;
        } else if (static_cast<int32_t>(ay) * 2 > static_cast<int32_t>(ax) * 5) {
            direction[i] = 2;
        } else {
            if ((x > 0 && y > 0) || (x < 0 && y < 0)) {
                direction[i] = 1;
            } else {
                direction[i] = 3;
            }
        }
    }
}

// ============================================================================
// MAIN PERFORMANCE BENCHMARKING HARNESS
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <width> <height>\n";
        return 1;
    }
    
    int width = std::atoi(argv[1]);
    int height = std::atoi(argv[2]);
    size_t num_pixels = static_cast<size_t>(width) * height;
    
    if (width <= 0 || height <= 0) {
        std::cerr << "Invalid image dimensions.\n";
        return 1;
    }

    size_t alloc_size_u8 = ((num_pixels + 63) / 64) * 64;
    size_t alloc_size_i16 = (((num_pixels * sizeof(int16_t)) + 63) / 64) * 64;
    size_t alloc_size_u32 = (((num_pixels * sizeof(uint32_t)) + 63) / 64) * 64;

    uint8_t* input_img = static_cast<uint8_t*>(aligned_alloc(64, alloc_size_u8));
    uint8_t* blur_out  = static_cast<uint8_t*>(aligned_alloc(64, alloc_size_u8));
    int16_t* grad_x    = static_cast<int16_t*>(aligned_alloc(64, alloc_size_i16));
    int16_t* grad_y    = static_cast<int16_t*>(aligned_alloc(64, alloc_size_i16));
    uint32_t* mag_out  = static_cast<uint32_t*>(aligned_alloc(64, alloc_size_u32));
    uint8_t* dir_out   = static_cast<uint8_t*>(aligned_alloc(64, alloc_size_u8));

    if (!input_img || !blur_out || !grad_x || !grad_y || !mag_out || !dir_out) {
        std::cerr << "Memory allocation failed.\n";
        return 1;
    }

    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            if (r > height / 4 && r < 3 * height / 4 && c > width / 4 && c < 3 * width / 4) {
                input_img[r * width + c] = 255;
            } else {
                input_img[r * width + c] = 0;
            }
        }
    }

    const int ITERATIONS = 100; 

    // 1. Profile Gaussian Blur Loop
    auto start_gaussian = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        gaussian_blur_scalar<uint8_t, int32_t, int16_t>(input_img, blur_out, width, height);
    }
    auto end_gaussian = std::chrono::high_resolution_clock::now();
    double gaussian_total_time = std::chrono::duration<double>(end_gaussian - start_gaussian).count();

    // 2. Profile Sobel Gradients Loop
    auto start_sobel = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        sobel_gradients_scalar<uint8_t, int16_t, int16_t>(blur_out, grad_x, grad_y, width, height);
    }
    auto end_sobel = std::chrono::high_resolution_clock::now();
    double sobel_total_time = std::chrono::duration<double>(end_sobel - start_sobel).count();

    // 3. Profile Magnitude Loop
    auto start_mag = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        compute_magnitude_scalar<int16_t, uint32_t>(grad_x, grad_y, mag_out, width, height);
    }
    auto end_mag = std::chrono::high_resolution_clock::now();
    double magnitude_total_time = std::chrono::duration<double>(end_mag - start_mag).count();

    // 4. Profile Direction Quantization Loop
    auto start_dir = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        compute_direction_scalar<int16_t, uint8_t>(grad_x, grad_y, dir_out, width, height);
    }
    auto end_dir = std::chrono::high_resolution_clock::now();
    double direction_total_time = std::chrono::duration<double>(end_dir - start_dir).count();

    double aggregate_pipeline_time = gaussian_total_time + sobel_total_time + 
                                      magnitude_total_time + direction_total_time;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "=========================================================\n";
    std::cout << "          Canny Pipeline Hotspot Analysis Breakdown       \n";
    std::cout << "=========================================================\n";
    std::cout << "Gaussian Blur : " 
              << (gaussian_total_time / ITERATIONS) * 1000.0 << " ms | " 
              << (gaussian_total_time / aggregate_pipeline_time) * 100.0 << "%\n";
              
    std::cout << "Sobel Gx/Gy   : " 
              << (sobel_total_time / ITERATIONS) * 1000.0 << " ms | " 
              << (sobel_total_time / aggregate_pipeline_time) * 100.0 << "%\n";
              
    std::cout << "Magnitude     : " 
              << (magnitude_total_time / ITERATIONS) * 1000.0 << " ms | " 
              << (magnitude_total_time / aggregate_pipeline_time) * 100.0 << "%\n";
              
    std::cout << "Direction     : " 
              << (direction_total_time / ITERATIONS) * 1000.0 << " ms | " 
              << (direction_total_time / aggregate_pipeline_time) * 100.0 << "%\n";
    std::cout << "=========================================================\n";
    std::cout << "Total Average Pipeline Iteration Time: " 
              << (aggregate_pipeline_time / ITERATIONS) * 1000.0 << " ms\n";

    free(input_img);
    free(blur_out);
    free(grad_x);
    free(grad_y);
    free(mag_out);
    free(dir_out);

    return 0;
}