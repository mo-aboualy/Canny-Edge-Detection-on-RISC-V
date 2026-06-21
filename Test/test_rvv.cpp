#include <stdio.h>
#include <riscv_vector.h>   // RVV 1.0 intrinsics header — provides all __riscv_v* functions and vector types (e.g. vuint8m1_t)

int main() {
    // --- Critical First Test (per project guide, Phase 1.5) ---
    // This program is the minimal end-to-end sanity check for the whole toolchain:
    // if this compiles with -march=rv64gcv and produces correct output under QEMU
    // at VLEN=128/256/512, then the cross-compiler, RVV codegen, and emulator are
    // all working together correctly — before any real pipeline code is trusted.

    // Simple test: add 1 to each element of an array using RVV
    unsigned char input[16]  = {10, 20, 30, 40, 50, 60, 70, 80,
                                 90,100,110,120,130,140,150,160};
    unsigned char output[16] = {0};

    size_t n = 16;   // total number of elements to process
    size_t i = 0;    // index of the next unprocessed element

    // --- Strip-mining loop ---
    // This is the canonical RVV loop shape: instead of a hardcoded "process N
    // elements per iteration" like fixed-width SIMD (e.g. AVX/NEON), we ask the
    // hardware/emulator at runtime how many elements it can handle this round.
    // The same compiled binary works correctly whether VLEN is 128, 256, or 512 —
    // a real RISC-V chip with a wider vector unit just does more work per loop pass.
    while (i < n) {
        // __riscv_vsetvl_e8m1(n - i):
        //   - e8  = 8-bit element width (matches unsigned char)
        //   - m1  = LMUL=1 (no register grouping — 1 physical reg per logical vector)
        //   - argument (n - i) = "how many elements are left to do"
        //   - returns vl = the actual number of elements the hardware will process
        //     this iteration (capped by VLEN/8 at LMUL=1; may be less than requested
        //     on the final, partial "tail" iteration)
        size_t vl = __riscv_vsetvl_e8m1(n - i);

        // Vector load: read vl consecutive bytes starting at input+i into a vector
        // register. vuint8m1_t = vector of unsigned 8-bit elements, LMUL=1.
        vuint8m1_t v = __riscv_vle8_v_u8m1(input + i, vl);

        // Vector-scalar add: adds the scalar value 1 to every one of the vl lanes
        // in v, in a single instruction (this is the "vector" part — one instruction,
        // many elements, as opposed to a scalar loop doing this one byte at a time).
        v = __riscv_vadd_vx_u8m1(v, 1, vl);

        // Vector store: write the vl resulting elements back out to output+i.
        __riscv_vse8_v_u8m1(output + i, v, vl);

        // Advance by however many elements were actually processed this round —
        // NOT by a hardcoded constant. This is what makes the loop VLEN-agnostic:
        // on a wider VLEN, vl is larger and we advance faster; on a narrower VLEN,
        // vl is smaller and the loop just takes more iterations to finish, but the
        // final result is identical either way.
        i += vl;
    }

    // --- Verification output ---
    // Printed manually (no assert()) so a human running this under QEMU can
    // visually confirm every output[j] == input[j] + 1 at each tested VLEN.
    printf("RVV test result:\n");
    for (int j = 0; j < 16; j++) {
        printf("  input[%2d]=%3d  output[%2d]=%3d\n", j, input[j], j, output[j]);
    }
    printf("PASS if every output = input + 1\n");
    return 0;
}