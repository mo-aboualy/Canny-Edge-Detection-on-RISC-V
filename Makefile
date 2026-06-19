.RECIPEPREFIX := >

VLEN ?= 128

HOST_CXX := g++
RV_CXX := riscv64-linux-gnu-g++
QEMU := qemu-riscv64
OBJDUMP := riscv64-linux-gnu-objdump
TIME := /usr/bin/time

HOST_FLAGS = -Wall -Wextra -O2 -Iinclude

# FIX: Added -flto here. This handles cross-file optimizations at link-time,
# allowing the auto-vectorizer to see inside functions without clashing names.
RV_FLAGS   = -Wall -Wextra -Iinclude -march=rv64gcv -static

HOST_LIBS := -lgtest -lgtest_main -pthread -lm
RV_LIBS := -lm

BUILD_DIR_HOST := build/host
BUILD_DIR_RV := build/rv
BUILD_DIR_P4 := build/phase4

PIPELINE_SRCS := \
    src/image_io.cpp \
    src/gaussian_blur.cpp \
    src/gaussian_rvv.cpp \
    src/sobel.cpp \
    src/magnitude.cpp \
    src/direction.cpp

# Restored back to the original standard benchmark file
PHASE4_SRC := Test/benchmark.cpp
VEC_REPORT := $(BUILD_DIR_P4)/vec_report.txt

.PHONY: all test canny_rv run clean \
        phase4 phase4_build phase4_sweep phase4_sizes phase4_vec_count \
        phase5 phase6_lmul

all: test canny_rv

$(BUILD_DIR_HOST) $(BUILD_DIR_RV) $(BUILD_DIR_P4):
> @mkdir -p $@

# --- Host-side Testing ---
TEST_SRCS := src/host_tests.cpp $(filter-out src/gaussian_rvv.cpp, $(PIPELINE_SRCS))
$(BUILD_DIR_HOST)/test_runner: $(TEST_SRCS) | $(BUILD_DIR_HOST)
> @echo "  [HOST_CXX] Building test runner..."
> @$(HOST_CXX) $(HOST_FLAGS) $^ -o $@ $(HOST_LIBS)

test: $(BUILD_DIR_HOST)/test_runner
> @echo "  [RUN]      Running host-side tests..."
> @./$<

# --- Phase 4: Compiler Optimization Sweep ---
$(BUILD_DIR_P4)/bench_O0: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "  [RV_CXX]   Compiling -O0 benchmark..."
> @$(TIME) -f "              -> Compile Time: %e sec" \
  $(RV_CXX) -O0 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O2: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "  [RV_CXX]   Compiling -O2 benchmark..."
> @$(TIME) -f "              -> Compile Time: %e sec" \
  $(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O3: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "  [RV_CXX]   Compiling -O3 benchmark..."
> @$(TIME) -f "              -> Compile Time: %e sec" \
  $(RV_CXX) -O3 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_Os: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "  [RV_CXX]   Compiling -Os benchmark..."
> @$(TIME) -f "              -> Compile Time: %e sec" \
  $(RV_CXX) -Os $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_Ofast: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "  [RV_CXX]   Compiling -Ofast benchmark..."
> @$(TIME) -f "              -> Compile Time: %e sec" \
  $(RV_CXX) -Ofast $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O3_vec: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "  [RV_CXX]   Compiling -O3_vec benchmark (with report)..."
> @$(TIME) -f "              -> Compile Time: %e sec" \
  $(RV_CXX) -O3 -ftree-vectorize -fopt-info-vec-all=$(VEC_REPORT) \
  $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

phase4_build: $(BUILD_DIR_P4)/bench_O0 $(BUILD_DIR_P4)/bench_O2 \
              $(BUILD_DIR_P4)/bench_O3 $(BUILD_DIR_P4)/bench_Os \
              $(BUILD_DIR_P4)/bench_Ofast $(BUILD_DIR_P4)/bench_O3_vec

phase4_sweep: phase4_build
> @echo "============================================================"
> @echo " Running Performance Optimization Sweep (VLEN=$(VLEN))"
> @echo "============================================================"
> @for opt in O0 O2 O3 Os Ofast O3_vec; do \
  echo ""; \
  echo "--- Optimization Level -$$opt ---"; \
  $(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_$$opt; \
 done

phase4_sizes: phase4_build
> @echo ""
> @echo "============================================================"
> @echo " Binary Code Size Comparison"
> @echo "============================================================"
> @for opt in O0 O2 O3 Os Ofast O3_vec; do \
  stat -c "$$opt binary size: %s bytes" $(BUILD_DIR_P4)/bench_$$opt; \
 done

phase4_vec_count: $(BUILD_DIR_P4)/bench_O3_vec
> @echo ""
> @echo "============================================================"
> @echo " Auto-Vectorization Structural Analysis (-O3_vec)"
> @echo "============================================================"
> @echo -n "Total vector configuration (vset) instructions: "
> @$(OBJDUMP) -d $< | grep -E "vset[i]?vli" | wc -l
> @echo ""
> @echo "--- First successful vectorization messages ---"
> @grep -i "optimized:" $(VEC_REPORT) | head -20 || true
> @echo ""
> @echo "--- First missed vectorization messages ---"
> @grep -i "missed:" $(VEC_REPORT) | grep -vi "missed" | head -20 || true

phase4: phase4_sweep phase4_sizes phase4_vec_count

# --- Phase 5: Profiling and Hotspot Identification ---
phase5: $(BUILD_DIR_P4)/bench_Ofast
> @mkdir -p $(BUILD_DIR_P4)
> @echo "============================================================"
> @echo " Running Phase 5 Hotspot Identification (-Ofast)"
> @echo "============================================================"
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $< | tee $(BUILD_DIR_P4)/phase5_hotspots.txt

# --- Phase 6: Manual RVV Optimization & Tuning ---
PHASE6_LMUL_SRC := Test/benchmark_lmul.cpp
PHASE6_LMUL_DEPS := src/image_io.cpp src/gaussian_blur.cpp src/gaussian_rvv.cpp

$(BUILD_DIR_P4)/bench_lmul: $(PHASE6_LMUL_SRC) $(PHASE6_LMUL_DEPS) | $(BUILD_DIR_P4)
> @echo "  [RV_CXX]   Compiling LMUL Sweep Benchmark..."
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

phase6_lmul: $(BUILD_DIR_P4)/bench_lmul
> @echo "============================================================"
> @echo " Running LMUL Sweep Tests (VLEN=$(VLEN))"
> @echo "============================================================"
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_lmul

# --- Production Deployment Execution Targets ---
canny_rv: | $(BUILD_DIR_RV)
> @echo "  [RV_CXX]   Building main optimized canny_rv application..."
> @$(RV_CXX) -O2 $(RV_FLAGS) \
  src/main.cpp \
  $(PIPELINE_SRCS) \
  -o $(BUILD_DIR_RV)/canny_rv \
  $(RV_LIBS)

run: canny_rv
> @echo "  [EMU]      Executing canny_rv via QEMU (VLEN=$(VLEN))..."
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_RV)/canny_rv

clean:
> @echo "Cleaning up generated binaries and build artifacts..."
> @rm -rf build
