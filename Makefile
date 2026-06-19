.RECIPEPREFIX := >

VLEN ?= 128

HOST_CXX := g++
RV_CXX := riscv64-linux-gnu-g++
QEMU := qemu-riscv64
OBJDUMP := riscv64-linux-gnu-objdump
TIME := /usr/bin/time

HOST_FLAGS = -Wall -Wextra -O2 -Iinclude
RV_FLAGS   = -Wall -Wextra -Iinclude -march=rv64gcv -static

HOST_LIBS := -lgtest -lgtest_main -pthread -lm
RV_LIBS := -lm

BUILD_DIR_HOST := build/host
BUILD_DIR_RV := build/rv
BUILD_DIR_P4 := build/phase4
BUILD_DIR_VIZ := build/viz

PIPELINE_SRCS := \
    src/image_io.cpp \
    src/gaussian_blur.cpp \
    src/gaussian_rvv.cpp \
    src/sobel.cpp \
    src/sobel_rvv.cpp \
    src/magnitude.cpp \
    src/magnitude_rvv.cpp \
    src/direction.cpp

PHASE4_SRC := Test/benchmark.cpp
VEC_REPORT := $(BUILD_DIR_P4)/vec_report.txt

.PHONY: all test canny_rv run clean \
        phase4 phase4_build phase4_sweep phase4_sizes phase4_vec_count \
        phase5 phase6_lmul visualize visualize_rv phase6_magnitude

all: test canny_rv

$(BUILD_DIR_HOST) $(BUILD_DIR_RV) $(BUILD_DIR_P4) $(BUILD_DIR_VIZ):
> @mkdir -p $@

# --- Host-side Testing ---
TEST_SRCS := src/host_tests.cpp $(filter-out src/gaussian_rvv.cpp src/sobel_rvv.cpp, $(PIPELINE_SRCS))
$(BUILD_DIR_HOST)/test_runner: $(TEST_SRCS) | $(BUILD_DIR_HOST)
> @echo "[BUILD] Host Test Runner"
> @$(HOST_CXX) $(HOST_FLAGS) $^ -o $@ $(HOST_LIBS)

test: $(BUILD_DIR_HOST)/test_runner
> @./$<

# --- Phase 4: Compiler Optimization Sweep ---
$(BUILD_DIR_P4)/bench_O0: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark O0"
> @$(RV_CXX) -O0 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O2: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark O2"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O3: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark O3"
> @$(RV_CXX) -O3 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_Os: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark Os"
> @$(RV_CXX) -Os $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_Ofast: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark Ofast"
> @$(RV_CXX) -Ofast $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_P4)/bench_O3_vec: $(PHASE4_SRC) $(PIPELINE_SRCS) | $(BUILD_DIR_P4)
> @echo "[BUILD] RV Benchmark O3_vec"
> @$(RV_CXX) -O3 -ftree-vectorize -fopt-info-vec-all=$(VEC_REPORT) $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

phase4_build: $(BUILD_DIR_P4)/bench_O0 $(BUILD_DIR_P4)/bench_O2 \
               $(BUILD_DIR_P4)/bench_O3 $(BUILD_DIR_P4)/bench_Os \
               $(BUILD_DIR_P4)/bench_Ofast $(BUILD_DIR_P4)/bench_O3_vec

phase4_sweep: phase4_build
> @for opt in O0 O2 O3 Os Ofast O3_vec; do \
>   echo "\n--- [ -$$opt ] ---"; \
>   $(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_$$opt; \
> done

phase4_sizes: phase4_build
> @echo "\n--- Binary Sizes ---"
> @for opt in O0 O2 O3 Os Ofast O3_vec; do \
>   stat -c "%s bytes : $$opt" $(BUILD_DIR_P4)/bench_$$opt; \
> done

phase4_vec_count: $(BUILD_DIR_P4)/bench_O3_vec
> @echo "\n--- Vector Configs (vsetvli) ---"
> @$(OBJDUMP) -d $< | grep -E "vset[i]?vli" | wc -l

phase4: phase4_sweep phase4_sizes phase4_vec_count

# --- Phase 5: Profiling and Hotspot Identification ---
phase5: $(BUILD_DIR_P4)/bench_Ofast
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $< | tee $(BUILD_DIR_P4)/phase5_hotspots.txt

# --- Phase 6: Manual RVV Optimization & Tuning ---
PHASE6_LMUL_SRC := Test/benchmark_lmul.cpp
PHASE6_LMUL_DEPS := src/image_io.cpp src/gaussian_blur.cpp src/gaussian_rvv.cpp

$(BUILD_DIR_P4)/bench_lmul: $(PHASE6_LMUL_SRC) $(PHASE6_LMUL_DEPS) | $(BUILD_DIR_P4)
> @echo "[BUILD] LMUL Sweep"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

phase6_lmul: $(BUILD_DIR_P4)/bench_lmul
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_P4)/bench_lmul

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

# --- Production Deployment Execution Targets ---
canny_rv: $(PIPELINE_SRCS) src/main.cpp | $(BUILD_DIR_RV)
> @echo "[BUILD] canny_rv"
> @$(RV_CXX) -O2 $(RV_FLAGS) src/main.cpp $(PIPELINE_SRCS) -o $(BUILD_DIR_RV)/canny_rv $(RV_LIBS)

run: canny_rv
> @$(QEMU) -cpu rv64,v=true,vlen=$(VLEN) $(BUILD_DIR_RV)/canny_rv

# --- Visualization ---
$(BUILD_DIR_VIZ)/test_blur: Test/Test_blur.cpp src/image_io.cpp src/gaussian_blur.cpp | $(BUILD_DIR_VIZ)
> @echo "[BUILD] Viz test_blur (Host)"
> @$(HOST_CXX) $(HOST_FLAGS) $^ -o $@ -lm

$(BUILD_DIR_VIZ)/test_sobel: Test/Test_sobel.cpp src/image_io.cpp src/gaussian_blur.cpp src/sobel.cpp | $(BUILD_DIR_VIZ)
> @echo "[BUILD] Viz test_sobel (Host)"
> @$(HOST_CXX) $(HOST_FLAGS) $^ -o $@ -lm

$(BUILD_DIR_VIZ)/test_blur_rv: Test/Test_blur.cpp src/image_io.cpp src/gaussian_blur.cpp src/gaussian_rvv.cpp | $(BUILD_DIR_VIZ)
> @echo "[BUILD] Viz test_blur_rv (RV)"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

$(BUILD_DIR_VIZ)/test_sobel_rv: Test/Test_sobel.cpp src/image_io.cpp src/gaussian_blur.cpp src/gaussian_rvv.cpp src/sobel.cpp src/sobel_rvv.cpp | $(BUILD_DIR_VIZ)
> @echo "[BUILD] Viz test_sobel_rv (RV)"
> @$(RV_CXX) -O2 $(RV_FLAGS) $^ -o $@ $(RV_LIBS)

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

visualize: $(BUILD_DIR_VIZ)/test_blur $(BUILD_DIR_VIZ)/test_sobel
> @python3 Tool/prepare_image.py Tool/test_image.jpg > /dev/null 2>&1
> @$(BUILD_DIR_VIZ)/test_blur input_512.raw $(BUILD_DIR_VIZ)/blurred.raw 512 512
> @$(BUILD_DIR_VIZ)/test_sobel $(BUILD_DIR_VIZ)/blurred.raw $(BUILD_DIR_VIZ)/magnitude.raw 512 512
> @python3 Tool/save_pipeline_png.py input_512.raw $(BUILD_DIR_VIZ)/blurred.raw $(BUILD_DIR_VIZ)/magnitude.raw $(BUILD_DIR_VIZ)/pipeline_output.png > /dev/null 2>&1
> @mkdir -p /mnt/c/Users/Mohamed/Desktop/ 2>/dev/null || true
> @cp $(BUILD_DIR_VIZ)/pipeline_output.png /mnt/c/Users/Mohamed/Desktop/pipeline_output.png
> @echo "[SAVED] Desktop/pipeline_output.png"

clean:
> @rm -rf build
> @echo "[CLEAN] Build artifacts removed."