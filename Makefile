VLEN ?= 128
HOST_CXX = g++
RV_CXX   = riscv64-unknown-elf-g++

HOST_FLAGS = -Wall -Wextra -O2 -Iinclude
RV_FLAGS   = -Wall -Wextra -Iinclude -march=rv64gcv -static

BUILD_DIR_HOST = build/host
BUILD_DIR_RV   = build/rv
BUILD_DIR_P4   = build/phase4

# Source files shared between host tests and phase4 benchmark
PIPELINE_SRCS = src/image_io.cpp \
                src/gaussian_blur.cpp \
                src/sobel.cpp \
                src/magnitude.cpp \
                src/direction.cpp

# ─────────────────────────────────────────────────────────────
# Default
# ─────────────────────────────────────────────────────────────
all: test canny_rv

# ─────────────────────────────────────────────────────────────
# Phase 3: Host-side GoogleTest
# ─────────────────────────────────────────────────────────────
test:
	mkdir -p $(BUILD_DIR_HOST)
	$(HOST_CXX) $(HOST_FLAGS) src/host_tests.cpp src/image_io.cpp src/gaussian_blur.cpp src/sobel.cpp src/magnitude.cpp src/direction.cpp -o $(BUILD_DIR_HOST)/test_runner -lgtest -lgtest_main -pthread
	./$(BUILD_DIR_HOST)/test_runner

# ─────────────────────────────────────────────────────────────
# Phase 4: Compiler Optimization Sweep (cross-compiled, run on QEMU)
#
# Each target compiles the SAME benchmark source at a different
# optimization level so results are directly comparable.
# The benchmark times all four pipeline stages:
#   Gaussian | Sobel | Magnitude | Direction
# ─────────────────────────────────────────────────────────────

# Benchmark binary at each optimization level
$(BUILD_DIR_P4)/bench_O0: Test/benchmark.cpp $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
	$(RV_CXX) -O0 $(RV_FLAGS) $^ -o $@

$(BUILD_DIR_P4)/bench_O1: Test/benchmark.cpp $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
	$(RV_CXX) -O1 $(RV_FLAGS) $^ -o $@

$(BUILD_DIR_P4)/bench_O2: Test/benchmark.cpp $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
	$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@

$(BUILD_DIR_P4)/bench_O3: Test/benchmark.cpp $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
	$(RV_CXX) -O3 $(RV_FLAGS) $^ -o $@

# -O3 + auto-vectorization report written to vec_report.txt
# -fopt-info-vec-all  → shows which loops vectorized and which were rejected
# -ftree-vectorize    → explicitly enables auto-vectorization (implied by -O3 but stated clearly)
$(BUILD_DIR_P4)/bench_O3_vec: Test/benchmark.cpp $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
	$(RV_CXX) -O3 -ftree-vectorize $(RV_FLAGS) $^ -o $@ \
		-fopt-info-vec-all 2> vec_report.txt

$(BUILD_DIR_P4):
	mkdir -p $(BUILD_DIR_P4)

# Build all phase4 binaries
phase4_build: $(BUILD_DIR_P4)/bench_O0 \
              $(BUILD_DIR_P4)/bench_O1 \
              $(BUILD_DIR_P4)/bench_O2 \
              $(BUILD_DIR_P4)/bench_O3 \
              $(BUILD_DIR_P4)/bench_O3_vec

# Run the full optimization sweep on QEMU and print a results table
# Output maps directly to the report table:
#   Stage | -O0 | -O1 | -O2 | -O3 | Auto-vec
phase4_sweep: phase4_build
	@echo ""
	@echo "============================================================"
	@echo "  Phase 4: Compiler Optimization Sweep"
	@echo "  (wall-clock timing via CLOCK_MONOTONIC, 200 iterations)"
	@echo "============================================================"
	@echo ""
	@echo "--- -O0 ---"
	@qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_O0
	@echo ""
	@echo "--- -O1 ---"
	@qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_O1
	@echo ""
	@echo "--- -O2 ---"
	@qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_O2
	@echo ""
	@echo "--- -O3 ---"
	@qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_O3
	@echo ""
	@echo "--- -O3 + auto-vectorize ---"
	@qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_O3_vec

# Print binary sizes for the report table
phase4_sizes: phase4_build
	@echo ""
	@echo "====== Binary Sizes ======"
	@for level in O0 O1 O2 O3 O3_vec; do \
		printf "  %-12s %s bytes\n" $$level \
		$$(stat -c%s $(BUILD_DIR_P4)/bench_$$level); \
	done

# Count RVV vector instructions in the auto-vec binary
# A non-zero count means the compiler did emit vector instructions
phase4_vec_count: $(BUILD_DIR_P4)/bench_O3_vec
	@echo ""
	@echo "====== Vector Instruction Count (vset* instructions) ======"
	@riscv64-unknown-elf-objdump -d $(BUILD_DIR_P4)/bench_O3_vec \
		| grep -c vset || true
	@echo ""
	@echo "====== Auto-vectorization Report ======"
	@echo "(see vec_report.txt for full details)"
	@echo "--- Loops that FAILED to vectorize ---"
	@grep "missed" vec_report.txt | head -20 || true
	@echo "--- Loops that SUCCEEDED ---"
	@grep "vectorized" vec_report.txt | grep -v "missed" | head -20 || true

# Run everything for Phase 4 in one shot
phase4: phase4_sweep phase4_sizes phase4_vec_count

# ─────────────────────────────────────────────────────────────
# RISC-V pipeline binary (Phase 5+)
# ─────────────────────────────────────────────────────────────
canny_rv:
	mkdir -p $(BUILD_DIR_RV)
	$(RV_CXX) $(RV_FLAGS) src/main.cpp $(PIPELINE_SRCS) -o $(BUILD_DIR_RV)/canny_rv

run: canny_rv
	qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_RV)/canny_rv

# ─────────────────────────────────────────────────────────────
# Utilities
# ─────────────────────────────────────────────────────────────
clean:
	rm -rf build/ vec_report.txt