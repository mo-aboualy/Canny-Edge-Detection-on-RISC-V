# ============================================================================
# Canny Edge Detection on RISC-V — Build System
# Dual-target Makefile: builds the same pipeline source files with TWO
# different compilers — HOST_CXX (native, for fast iteration + GoogleTest)
# and RV_CXX (RISC-V cross-compiler, for the real deliverable + RVV testing).
# ============================================================================

# Use '>' instead of a Tab character to mark recipe (command) lines below.
# Functionally identical to Tab — purely a readability/style choice.
.RECIPEPREFIX := >

# Default vector register width (in bits) used when running QEMU.
# '?=' means "only set this if not already set" — this is what lets you
# override it from the command line, e.g. `make run VLEN=256`.
VLEN ?= 128

# --- Toolchain / tool nicknames ---
# Using variables here means the actual compiler name only has to be
# changed in ONE place if it ever needs to change.
HOST_CXX := g++                          # native compiler — builds binaries that run directly on this machine
RV_CXX := riscv64-linux-gnu-g++          # RISC-V cross-compiler — builds binaries that only run on RISC-V (via QEMU here)
QEMU := qemu-riscv64                     # RISC-V user-mode emulator — runs RV_CXX-built binaries on this machine
OBJDUMP := riscv64-linux-gnu-objdump     # disassembler — used in Phase 4 to inspect generated RISC-V assembly
TIME := /usr/bin/time                    # external timing utility (not GNU Make's built-in time)

# --- Compiler flags ---
# -Wall -Wextra        : enable extra compiler warnings (catch bugs early)
# -O2                   : optimization level for host builds (fixed; RV optimization level is set per-target below)
# -Iinclude             : look in include/ for header files
# -march=rv64gcv        : target a 64-bit RISC-V CPU with the Vector (v) extension — REQUIRED for any RVV intrinsic to compile
# -static               : produce a fully self-contained binary (no external shared libs needed) — required for QEMU user-mode to run it cleanly
HOST_FLAGS = -Wall -Wextra -O2 -Iinclude
RV_FLAGS   = -Wall -Wextra -Iinclude -march=rv64gcv -static

# --- Linker libraries ---
HOST_LIBS := -lgtest -lgtest_main -pthread -lm   # GoogleTest + pthreads (host-only test runner) + math library
RV_LIBS := -lm                                    # RISC-V binaries only need the math library (no GoogleTest on-target)

# --- Output directories, kept separate so host/RV/phase4/visualization builds never collide ---
BUILD_DIR_HOST := build/host
BUILD_DIR_RV := build/rv
BUILD_DIR_P4 := build/phase4
BUILD_DIR_VIZ := build/viz

# Full list of pipeline source files (every Canny stage, including the
# bonus NMS + hysteresis stages). Defined once here and reused throughout
# the file via $(PIPELINE_SRCS) so every recipe stays in sync automatically.
PIPELINE_SRCS := \
    src/image_io.cpp \
    src/gaussian_blur.cpp \
    src/gaussian_rvv.cpp \
    src/sobel.cpp \
    src/sobel_rvv.cpp \
    src/magnitude.cpp \
    src/magnitude_rvv.cpp \
    src/direction.cpp \
    src/nms.cpp \
    src/hysteresis.cpp

# .PHONY declares these as "command names", not real files to be produced.
# Without this, if a file literally named "pipeline_stages" ever existed,
# Make would mistakenly think this target is already up to date.
.PHONY: pipeline_stages

# Builds and immediately runs the full 8-stage pipeline visualizer (the
# program that produced docs/pipeline_stages.jpeg). The '|' before
# $(BUILD_DIR_HOST) marks it as an "order-only" prerequisite — Make just
# ensures the folder exists first, without treating folder timestamps as
# a reason to rebuild.
pipeline_stages: | $(BUILD_DIR_HOST)
> $(HOST_CXX) $(HOST_FLAGS) \
> 	Test/Test_pipeline_stages.cpp \
> 	$(PIPELINE_SRCS) \
> 	-o $(BUILD_DIR_HOST)/test_pipeline_stages \
> 	-lm
> ./$(BUILD_DIR_HOST)/test_pipeline_stages input_512.raw 512 512 Tool/stages 50 100
# Arguments above: input image, width, height, output folder/prefix,
# and the two hysteresis thresholds (low=50, high=100).

# --- Phase 4 setup ---
PHASE4_SRC := Test/benchmark.cpp
VEC_REPORT := $(BUILD_DIR_P4)/vec_report.txt

# Master .PHONY list for every command-style target defined below.
.PHONY: all test canny_rv run clean \
        phase4 phase4_build phase4_sweep phase4_sizes phase4_vec_count \
        phase5 phase6_lmul visualize visualize_rv phase6_magnitude

# Default target — running plain `make` with no arguments builds and runs
# the host test suite, then cross-compiles the production RISC-V binary.
all: test canny_rv

# Generic rule: create any of the four build directories on demand.
# '@' before a command suppresses Make from echoing the command itself
# to the terminal (so only the *output* of the command shows, not "mkdir -p ...").
$(BUILD_DIR_HOST) $(BUILD_DIR_RV) $(BUILD_DIR_P4) $(BUILD_DIR_VIZ):
> @mkdir -p $@

# ============================================================================
# --- Host-side Testing (Phase 3) ---
# Compiled natively with g++, NOT the RISC-V cross-compiler, so iteration
# is fast and GoogleTest can be used directly.
# ============================================================================
# Test sources = the GoogleTest driver + every pipeline file EXCEPT the
# three _rvv.cpp variants (those contain RVV intrinsics that only compile
# under -march=rv64gcv, which the host build doesn't use) + nms/hysteresis explicitly re-added.
TEST_SRCS := src/host_tests.cpp $(filter-out src/gaussian_rvv.cpp src/sobel_rvv.cpp src/magnitude_rvv.cpp, $(PIPELINE_SRCS)) src/nms.cpp src/hysteresis.cpp

$(BUILD_DIR_HOST)/test_runner: $(TEST_SRCS) | $(BUILD_DIR_HOST)
> @echo "[BUILD] Host Test Runner"
> @$(HOST_CXX) $(HOST_FLAGS) $^ -o $@ $(HOST_LIBS)
# $^ = all prerequisite files listed above (the full TEST_SRCS list)
# $@ = the target itself (build/host/test_runner)

test: $(BUILD_DIR_HOST)/test_runner
> @./$
# $< = the FIRST prerequisite only (here, just the test_runner binary) — i.e. "run what was just built"

# ============================================================================
# --- Phase 4: Compiler Optimization Sweep ---
# Same scalar pipeline, cross-compiled for RISC-V at six different
# optimization levels, to measure what the compiler gives "for free".
# ============================================================================

$(BUILD_DIR_P4)/bench_O0: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark O0"
> @$(RV_CXX) -O0 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)
# -O0 = no optimization at all (baseline)

$(BUILD_DIR_P4)/bench_O2: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark O2"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)
# -O2 = standard "release" optimization level

$(BUILD_DIR_P4)/bench_O3: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark O3"
> @$(RV_CXX) -O3 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)
# -O3 = aggressive optimization, including auto-vectorization attempts

$(BUILD_DIR_P4)/bench_Os: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark Os"
> @$(RV_CXX) -Os $(RV_FLAGS) $^ -o $@ $(RV_LIBS)
# -Os = optimize for smallest binary size, not speed

$(BUILD_DIR_P4)/bench_Ofast: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark Ofast"
> @$(RV_CXX) -Ofast $(RV_FLAGS) $^ -o $@ $(RV_LIBS)
# -Ofast = -O3 plus relaxed floating-point rules (-ffast-math) — faster sqrt() etc., at the cost of strict IEEE correctness

$(BUILD_DIR_P4)/bench_O3_vec: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark O3_vec"
> @$(RV_CXX) -O3 -ftree-vectorize -fopt-info-vec-all=$(VEC_REPORT) $(RV_FLAGS) $^ -o $@ $(RV_LIBS)
# Same as -O3, but additionally asks GCC to write a report (to VEC_REPORT)
# explaining which loops it successfully auto-vectorized and which it
# couldn't, and why. Used to justify which kernels need hand-written RVV in Phase 6.

# Convenience target: building this alone triggers all six binaries above.
phase4_build: $(BUILD_DIR_P4)/bench_O0 $(BUILD_DIR_P4)/bench_O2 \
               $(BUILD_DIR_P4)/bench_O3 $(BUILD_DIR_P4)/bench_Os \
               $(BUILD_DIR_P4)/bench_Ofast $(BUILD_DIR_P4)/bench_O3_vec

# Runs each of the six binaries under QEMU in turn, printing a labeled
# header before each — this is what produces the Phase 4 timing table.
phase4_sweep: phase4_build
> @for opt in O0 O2 O3 Os Ofast O3_vec; do \
>   echo "\n--- [ -$$opt ] ---"; \
>   $(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_$$opt; \
> done
# Note: '$$opt' (double dollar sign) is needed because this loop runs
# inside the shell, not inside Make itself — a single '$' would be
# interpreted by Make instead of bash.

# Reports the on-disk size (in bytes) of each of the six binaries —
# this is what produces the Phase 4 binary-size table.
phase4_sizes: phase4_build
> @echo "\n--- Binary Sizes ---"
> @for opt in O0 O2 O3 Os Ofast O3_vec; do \
>   stat -c "%s bytes : $$opt" $(BUILD_DIR_P4)/bench_$$opt; \
> done

# Disassembles the -O3_vec binary and counts how many "vsetvli"/"vsetivli"
# instructions appear — each one marks a point where the compiler
# successfully configured a vector operation. This is the
# "Total vector configuration instructions: 170" figure in the README.
phase4_vec_count: $(BUILD_DIR_P4)/bench_O3_vec
> @echo "\n--- Vector Configs (vsetvli) ---"
> @$(OBJDUMP) -d $< | grep -E "vset[i]?vli" | wc -l
# $< = first prerequisite (the bench_O3_vec binary)

# Running `make phase4` alone triggers all three Phase 4 sub-targets in order.
phase4: phase4_sweep phase4_sizes phase4_vec_count

# ============================================================================
# --- Phase 5: Profiling and Hotspot Identification ---
# ============================================================================
phase5: $(BUILD_DIR_P4)/bench_Ofast
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $< | tee $(BUILD_DIR_P4)/phase5_hotspots.txt
# Runs the -Ofast benchmark binary and pipes its output through `tee`,
# which prints the result to the screen AND saves a copy to
# phase5_hotspots.txt at the same time — this is the per-stage
# percentage breakdown table (Gaussian 56%, Sobel 20%, ...).

# ============================================================================
# --- Phase 6: Manual RVV Optimization & Tuning ---
# ============================================================================
PHASE6_LMUL_SRC := Test/benchmark_lmul.cpp
PHASE6_LMUL_DEPS := src/image_io.cpp src/gaussian_blur.cpp src/gaussian_rvv.cpp

$(BUILD_DIR_P4)/bench_lmul: $(PHASE6_LMUL_SRC) $(PHASE6_LMUL_DEPS) | $(BUILD_DIR_P4)
> @echo "[BUILD] LMUL Sweep"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

phase6_lmul: $(BUILD_DIR_P4)/bench_lmul
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_lmul
# Builds then runs the LMUL=1/2/4 sweep binary — produces the LMUL
# comparison table (94.47 ms / 58.39 ms / 59.36 ms at VLEN=128, etc.)

PHASE6_MAG_DEPS := src/image_io.cpp src/magnitude.cpp src/magnitude_rvv.cpp

$(BUILD_DIR_P4)/test_magnitude_rvv: Test/test_magnitude_rvv.cpp $(PHASE6_MAG_DEPS) | $(BUILD_DIR_P4)
> @echo "[BUILD] Magnitude RVV equivalence test"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

phase6_magnitude: $(BUILD_DIR_P4)/test_magnitude_rvv
> @echo ""
> @echo "============================================================"
> @echo " Phase 6.5: Magnitude RVV Equivalence — VLEN sweep"
> @echo "============================================================"
> @for vlen in 128 256 512; do \
> 	echo "--- VLEN=$$vlen ---"; \
> 	$(QEMU) -cpu rv64,v=true,vlen=$$vlen $(BUILD_DIR_P4)/test_magnitude_rvv; \
> done
# Runs the Magnitude L1 scalar-vs-RVV equivalence test at all three VLEN
# values in a row — this is what produced the "max|diff| = 0 → PASS"
# correctness table for Magnitude in the README. Proves the RVV kernel
# isn't accidentally tuned to one specific vector width.

# ============================================================================
# --- Production Deployment Execution Targets ---
# The actual deliverable: the full pipeline, cross-compiled and runnable
# on RISC-V/QEMU end to end.
# ============================================================================
canny_rv: $(PIPELINE_SRCS) src/main.cpp | $(BUILD_DIR_RV)
> @echo "[BUILD] canny_rv"
> @$(RV_CXX) -O2 $(RV_FLAGS) src/main.cpp $(PIPELINE_SRCS) -o $(BUILD_DIR_RV)/canny_rv $(RV_LIBS)

run: canny_rv
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_RV)/canny_rv
# `run` depends on `canny_rv`, so Make automatically (re)builds the binary
# first if needed, then executes it under QEMU at whatever VLEN was
# requested (defaults to 128, or override with `make run VLEN=256`).

# ============================================================================
# --- Visualization ---
# Builds small standalone driver programs (blur-only, blur+Sobel) so a
# real photo can be run through the pipeline and the result inspected as
# an actual image, rather than just raw timing numbers.
# ============================================================================

# Host-side blur-only visualizer.
$(BUILD_DIR_VIZ)/test_blur: Test/Test_blur.cpp src/image_io.cpp src/gaussian_blur.cpp | $(BUILD_DIR_VIZ)
> @echo "[BUILD] Viz test_blur (Host)"
> @$(HOST_CXX) $(HOST_FLAGS) $^ -o $@ -lm

# Host-side blur+Sobel visualizer.
$(BUILD_DIR_VIZ)/test_sobel: Test/Test_sobel.cpp src/image_io.cpp src/gaussian_blur.cpp src/sobel.cpp | $(BUILD_DIR_VIZ)
> @echo "[BUILD] Viz test_sobel (Host)"
> @$(HOST_CXX) $(HOST_FLAGS) $^ -o $@ -lm

# RISC-V cross-compiled blur-only visualizer (uses the RVV Gaussian kernel).
$(BUILD_DIR_VIZ)/test_blur_rv: Test/Test_blur.cpp src/image_io.cpp src/gaussian_blur.cpp src/gaussian_rvv.cpp | $(BUILD_DIR_VIZ)
> @echo "[BUILD] Viz test_blur_rv (RV)"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

# RISC-V cross-compiled blur+Sobel visualizer (uses both RVV kernels).
$(BUILD_DIR_VIZ)/test_sobel_rv: Test/Test_sobel.cpp src/image_io.cpp src/gaussian_blur.cpp src/gaussian_rvv.cpp src/sobel.cpp src/sobel_rvv.cpp | $(BUILD_DIR_VIZ)
> @echo "[BUILD] Viz test_sobel_rv (RV)"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

# Runs the RV visualizers under QEMU at all three VLEN values, then
# diffs the resulting images against each other to confirm VLEN-agnostic
# correctness VISUALLY (not just via max|diff| numbers), and finally
# builds a side-by-side comparison PNG.
visualize_rv: $(BUILD_DIR_VIZ)/test_blur_rv $(BUILD_DIR_VIZ)/test_sobel_rv
> @python3 Tool/prepare_image.py Tool/test_image.jpg > /dev/null 2>&1
> @for vlen in 128 256 512; do \
>   $(QEMU) -cpu rv64,v=true,vlen=$$vlen $(BUILD_DIR_VIZ)/test_blur_rv input_512.raw $(BUILD_DIR_VIZ)/blurred_vlen$$vlen.raw 512 512; \
>   $(QEMU) -cpu rv64,v=true,vlen=$$vlen $(BUILD_DIR_VIZ)/test_sobel_rv $(BUILD_DIR_VIZ)/blurred_vlen$$vlen.raw $(BUILD_DIR_VIZ)/magnitude_vlen$$vlen.raw 512 512; \
> done
> @cmp -s $(BUILD_DIR_VIZ)/magnitude_vlen128.raw $(BUILD_DIR_VIZ)/magnitude_vlen256.raw || echo "[WARN] VLEN128 vs VLEN256 differs"
> @cmp -s $(BUILD_DIR_VIZ)/magnitude_vlen128.raw $(BUILD_DIR_VIZ)/magnitude_vlen512.raw || echo "[WARN] VLEN128 vs VLEN512 differs"
> @python3 Tool/save_pipeline_png.py $(BUILD_DIR_VIZ)/magnitude_vlen128.raw $(BUILD_DIR_VIZ)/magnitude_vlen256.raw $(BUILD_DIR_VIZ)/magnitude_vlen512.raw $(BUILD_DIR_VIZ)/vlen_comparison.png "VLEN=128" "VLEN=256" "VLEN=512" > /dev/null 2>&1
> @mkdir -p /mnt/c/Users/Mohamed/Desktop/ 2>/dev/null || true
> @cp $(BUILD_DIR_VIZ)/vlen_comparison.png /mnt/c/Users/Mohamed/Desktop/vlen_comparison.png
> @echo "[SAVED] Desktop/vlen_comparison.png"
# NOTE: the Desktop path above is hardcoded to one team member's Windows
# username. The "|| true" after mkdir means this fails silently on any
# other machine instead of crashing the build — harmless, but worth
# changing to a portable path (e.g. inside build/viz/) eventually.

# Same idea as visualize_rv, but runs entirely on the host machine (fast,
# no QEMU/cross-compile needed) — good for quick iteration while developing.
visualize: $(BUILD_DIR_VIZ)/test_blur $(BUILD_DIR_VIZ)/test_sobel
> @python3 Tool/prepare_image.py Tool/test_image.jpg > /dev/null 2>&1
> @$(BUILD_DIR_VIZ)/test_blur input_512.raw $(BUILD_DIR_VIZ)/blurred.raw 512 512
> @$(BUILD_DIR_VIZ)/test_sobel $(BUILD_DIR_VIZ)/blurred.raw $(BUILD_DIR_VIZ)/magnitude.raw 512 512
> @python3 Tool/save_pipeline_png.py input_512.raw $(BUILD_DIR_VIZ)/blurred.raw $(BUILD_DIR_VIZ)/magnitude.raw $(BUILD_DIR_VIZ)/pipeline_output.png > /dev/null 2>&1
> @mkdir -p /mnt/c/Users/Mohamed/Desktop/ 2>/dev/null || true
> @cp $(BUILD_DIR_VIZ)/pipeline_output.png /mnt/c/Users/Mohamed/Desktop/pipeline_output.png
> @echo "[SAVED] Desktop/pipeline_output.png"

# Deletes every build artifact by removing the entire build/ folder —
# gives you a clean slate to rebuild everything from scratch.
clean:
> @rm -rf build
> @echo "[CLEAN] Build artifacts removed."