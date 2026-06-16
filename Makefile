VLEN ?= 128
HOST_CXX = g++
RV_CXX   = riscv64-unknown-elf-g++
RV_CXX_LINUX = riscv64-linux-gnu-g++

HOST_FLAGS = -Wall -Wextra -O2 -Iinclude
RV_FLAGS   = -Wall -Wextra -O2 -Iinclude -march=rv64gcv

BUILD_DIR_HOST  = build/host
BUILD_DIR_RV    = build/rv
BUILD_DIR_BENCH = build/phase4

BENCH_SRC = src/benchmark.cpp src/gaussian_blur.cpp src/image_io.cpp
QEMU      = qemu-riscv64 -cpu rv64

# ── Default ──────────────────────────────────────────────────────────────────
all: test canny_rv

# ── Host Tests ───────────────────────────────────────────────────────────────
test:
	mkdir -p $(BUILD_DIR_HOST)
	$(HOST_CXX) $(HOST_FLAGS) src/host_tests.cpp src/image_io.cpp \
	    src/gaussian_blur.cpp src/sobel.cpp src/magnitude.cpp \
	    src/direction.cpp -o $(BUILD_DIR_HOST)/test_runner \
	    -lgtest -lgtest_main -pthread
	./$(BUILD_DIR_HOST)/test_runner

# ── RISC-V Binary ────────────────────────────────────────────────────────────
canny_rv:
	mkdir -p $(BUILD_DIR_RV)
	$(RV_CXX) $(RV_FLAGS) src/main.cpp -o $(BUILD_DIR_RV)/canny_rv

run: canny_rv
	qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN) ./$(BUILD_DIR_RV)/canny_rv

# ── Phase 4: Compiler Optimization Sweep ─────────────────────────────────────
RV_BENCH_FLAGS = -march=rv64gcv -static -Iinclude

$(BUILD_DIR_BENCH):
	mkdir -p $(BUILD_DIR_BENCH)

$(BUILD_DIR_BENCH)/bench_O0: $(BENCH_SRC) | $(BUILD_DIR_BENCH)
	$(RV_CXX_LINUX) -O0 $(RV_BENCH_FLAGS) $^ -o $@

$(BUILD_DIR_BENCH)/bench_O1: $(BENCH_SRC) | $(BUILD_DIR_BENCH)
	$(RV_CXX_LINUX) -O1 $(RV_BENCH_FLAGS) $^ -o $@

$(BUILD_DIR_BENCH)/bench_O2: $(BENCH_SRC) | $(BUILD_DIR_BENCH)
	$(RV_CXX_LINUX) -O2 $(RV_BENCH_FLAGS) $^ -o $@

$(BUILD_DIR_BENCH)/bench_O3: $(BENCH_SRC) | $(BUILD_DIR_BENCH)
	$(RV_CXX_LINUX) -O3 $(RV_BENCH_FLAGS) $^ -o $@

$(BUILD_DIR_BENCH)/bench_O3_vec: $(BENCH_SRC) | $(BUILD_DIR_BENCH)
	$(RV_CXX_LINUX) -O3 -ftree-vectorize -fopt-info-vec-all \
	    $(RV_BENCH_FLAGS) $^ -o $@ 2> $(BUILD_DIR_BENCH)/vec_report.txt

bench_sweep: $(BUILD_DIR_BENCH)/bench_O0 $(BUILD_DIR_BENCH)/bench_O1 \
             $(BUILD_DIR_BENCH)/bench_O2 $(BUILD_DIR_BENCH)/bench_O3 \
             $(BUILD_DIR_BENCH)/bench_O3_vec
	@echo "====== Phase 4: Timing Results (512x512, 200 iters) ======"
	@echo "--- O0 ---" && $(QEMU) $(BUILD_DIR_BENCH)/bench_O0
	@echo "--- O1 ---" && $(QEMU) $(BUILD_DIR_BENCH)/bench_O1
	@echo "--- O2 ---" && $(QEMU) $(BUILD_DIR_BENCH)/bench_O2
	@echo "--- O3 ---" && $(QEMU) $(BUILD_DIR_BENCH)/bench_O3
	@echo "--- O3+vec ---" && $(QEMU) $(BUILD_DIR_BENCH)/bench_O3_vec

bench_sizes: $(BUILD_DIR_BENCH)/bench_O0 $(BUILD_DIR_BENCH)/bench_O1 \
             $(BUILD_DIR_BENCH)/bench_O2 $(BUILD_DIR_BENCH)/bench_O3 \
             $(BUILD_DIR_BENCH)/bench_O3_vec
	@echo "====== Phase 4: Binary Sizes ======"
	@for f in O0 O1 O2 O3 O3_vec; do \
	    printf "%-15s %s bytes\n" $$f $$(stat -c%s $(BUILD_DIR_BENCH)/bench_$$f); \
	done

vec_count: $(BUILD_DIR_BENCH)/bench_O3_vec
	@echo "====== Phase 4: Vector Instructions in O3+vec binary ======"
	@riscv64-linux-gnu-objdump -d $(BUILD_DIR_BENCH)/bench_O3_vec | grep -c vset || true

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf build/*
