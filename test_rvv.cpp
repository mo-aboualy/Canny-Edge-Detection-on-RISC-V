#include <stdio.h>
#include <riscv_vector.h>

int main() {
    // Simple test: add 1 to each element of an array using RVV
    unsigned char input[16]  = {10, 20, 30, 40, 50, 60, 70, 80,
                                 90,100,110,120,130,140,150,160};
    unsigned char output[16] = {0};

    size_t n = 16;
    size_t i = 0;

    while (i < n) {
        size_t vl = __riscv_vsetvl_e8m1(n - i);
        vuint8m1_t v = __riscv_vle8_v_u8m1(input + i, vl);
        v = __riscv_vadd_vx_u8m1(v, 1, vl);
        __riscv_vse8_v_u8m1(output + i, v, vl);
        i += vl;
    }

    printf("RVV test result:\n");
    for (int j = 0; j < 16; j++) {
        printf("  input[%2d]=%3d  output[%2d]=%3d\n", j, input[j], j, output[j]);
    }
    printf("PASS if every output = input + 1\n");
    return 0;
}

