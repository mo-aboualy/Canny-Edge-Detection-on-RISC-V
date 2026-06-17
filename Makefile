.RECIPEPREFIX := >

VLEN ?= 128

HOST_CXX := g++
RV_CXX := riscv64-linux-gnu-g++
QEMU := qemu-riscv64
OBJDUMP := riscv64-linux-gnu-objdump
TIME := /usr/bin/time

HOST_FLAGS = -Wall -Wextra -O2 -Iinclude
RV_FLAGS   = -Wall -Wextra -O2 -Iinclude -march=rv64gcv -static
HOST_LIBS := -lgtest -lgtest_main -pthread -lm
RV_LIBS := -lm

BUILD_DIR_HOST := build/host
BUILD_DIR_RV := build/rv
BUILD_DIR_P4 := build/phase4

PIPELINE_SRCS := \
	src/image_io.cpp \
	src/gaussian_blur.cpp \
	src/sobel.cpp \
	src/magnitude.cpp \
	src/direction.cpp

PHASE4_SRC := Test/benchmark.cpp
VEC_REPORT := $(BUILD_DIR_P4)/vec_report.txt

.PHONY: all test canny_rv run clean \
	phase4 phase4_build phase4_sweep phase4_sizes phase4_vec_count

all: test canny_rv

$(BUILD_DIR_HOST):
> mkdir -p $(BUILD_DIR_HOST)

$(BUILD_DIR_RV):
> mkdir -p $(BUILD_DIR_RV)

$(BUILD_DIR_P4):
> mkdir -p $(BUILD_DIR_P4)

test: | $(BUILD_DIR_HOST)
> $(HOST_CXX) $(HOST_FLAGS) \
> 	src/host_tests.cpp \
> 	$(PIPELINE_SRCS) \
> 	-o $(BUILD_DIR_HOST)/test_runner \
> 	$(HOST_LIBS)
> ./$(BUILD_DIR_HOST)/test_runner

# ─────────────────────────────────────────────────────────────
# Phase 4: Compiler Optimization Sweep
#
# The terminal shows:
#   1. Compile time for every optimization binary.
#   2. Runtime for spatial 2D Gaussian.
#   3. Runtime for separable 1D Gaussian.
#   4. Runtime for the rest of the scalar pipeline.
#   5. Binary size.
#   6. Auto-vectorization evidence.
# ─────────────────────────────────────────────────────────────

$(BUILD_DIR_P4)/bench_O0: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "Compiling O0..."
> $(TIME) -f "compile_time O0: %e sec" \
> 	$(RV_CXX) -O0 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O2: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "Compiling O2..."
> $(TIME) -f "compile_time O2: %e sec" \
> 	$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O3: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "Compiling O3..."
> $(TIME) -f "compile_time O3: %e sec" \
> 	$(RV_CXX) -O3 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_Os: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "Compiling Os..."
> $(TIME) -f "compile_time Os: %e sec" \
> 	$(RV_CXX) -Os $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_Ofast: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "Compiling Ofast..."
> $(TIME) -f "compile_time Ofast: %e sec" \
> 	$(RV_CXX) -Ofast $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O3_vec: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "Compiling O3_vec..."
> $(TIME) -f "compile_time O3_vec: %e sec" \
> 	$(RV_CXX) -O3 -ftree-vectorize -fopt-info-vec-all=$(VEC_REPORT) \
> 	$(RV_FLAGS) $^ -o $@ $(RV_LIBS)

phase4_build: \
	$(BUILD_DIR_P4)/bench_O0 \
	$(BUILD_DIR_P4)/bench_O2 \
	$(BUILD_DIR_P4)/bench_O3 \
	$(BUILD_DIR_P4)/bench_Os \
	$(BUILD_DIR_P4)/bench_Ofast \
	$(BUILD_DIR_P4)/bench_O3_vec

phase4_sweep: phase4_build
> @echo ""
> @echo "============================================================"
> @echo " Phase 4: Compiler Optimization Sweep"
> @echo " VLEN=$(VLEN)"
> @echo " Runtime timing: CLOCK_MONOTONIC, 200 iterations per stage"
> @echo "============================================================"
> @echo ""
> @for level in O0 O2 O3 Os Ofast O3_vec; do \
> 	echo "------------------------------------------------------------"; \
> 	echo "Running $$level"; \
> 	echo "------------------------------------------------------------"; \
> 	$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_$$level; \
> 	echo ""; \
> done

phase4_sizes: phase4_build
> @echo ""
> @echo "============================================================"
> @echo " Binary Sizes"
> @echo "============================================================"
> @for level in O0 O2 O3 Os Ofast O3_vec; do \
> 	printf "%-10s %s bytes\n" "$$level" "$$(stat -c%s $(BUILD_DIR_P4)/bench_$$level)"; \
> done

phase4_vec_count: $(BUILD_DIR_P4)/bench_O3_vec
> @echo ""
> @echo "============================================================"
> @echo " Auto-vectorization Evidence"
> @echo "============================================================"
> @echo "Vector setup instruction count in O3_vec binary:"
> @$(OBJDUMP) -d $(BUILD_DIR_P4)/bench_O3_vec | grep -c "vset" || true
> @echo ""
> @echo "Auto-vectorization report: $(VEC_REPORT)"
> @echo ""
> @echo "--- First missed vectorization messages ---"
> @grep -i "missed" $(VEC_REPORT) | head -20 || true
> @echo ""
> @echo "--- First successful vectorization messages ---"
> @grep -i "vectorized" $(VEC_REPORT) | grep -vi "missed" | head -20 || true

phase4: phase4_sweep phase4_sizes phase4_vec_count

canny_rv: | $(BUILD_DIR_RV)
> $(RV_CXX) -O2 $(RV_FLAGS) \
> 	src/main.cpp \
> 	$(PIPELINE_SRCS) \
> 	-o $(BUILD_DIR_RV)/canny_rv \
> 	$(RV_LIBS)

run: canny_rv
> $(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_RV)/canny_rv

clean:
> rm -rf build vec_report.txt

.PHONY: phase5

phase5: $(BUILD_DIR_P4)/bench_Ofast
> @echo ""
> @echo "============================================================"
> @echo " Phase 5: Profiling and Hotspot Identification"
> @echo " Build used: Ofast"
> @echo " VLEN=$(VLEN)"
> @echo " Output saved to: $(BUILD_DIR_P4)/phase5_hotspots.txt"
> @echo "============================================================"
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_Ofast | tee $(BUILD_DIR_P4)/phase5_hotspots.txt
> @echo ""
> @echo "Phase 5 interpretation:"
> @echo "Use the per-stage timings above to compute percentages."
> @echo "The hottest stages should be optimized first in Phase 6."
