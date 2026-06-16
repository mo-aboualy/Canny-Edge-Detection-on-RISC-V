# ─── Compilers ────────────────────────────────────────────────────────────────
HOST_CXX = g++
RV_CXX   = riscv64-unknown-elf-g++

# ─── Flags ───────────────────────────────────────────────────────────────────
HOST_FLAGS = -Wall -Wextra -O2 -Iinclude
RV_FLAGS   = -Wall -Wextra -O2 -Iinclude -march=rv64gcv -static

# Phase 4 sweep targets share the same sources but different -O flags
RV_FLAGS_O0 = -Wall -Wextra -O0 -Iinclude -march=rv64gcv -static
RV_FLAGS_O3 = -Wall -Wextra -O3 -Iinclude -march=rv64gcv -static
RV_FLAGS_O3V = -Wall -Wextra -O3 -ftree-vectorize -Iinclude -march=rv64gcv -static

# ─── Directories ──────────────────────────────────────────────────────────────
BUILD_DIR_HOST = build/host
BUILD_DIR_RV   = build/rv

# ─── QEMU settings ────────────────────────────────────────────────────────────
VLEN ?= 128
QEMU = qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN)

# ─── Common sources (all pipeline stages) ────────────────────────────────────
PIPELINE_SRCS = src/image_io.cpp \
                src/gaussian_blur.cpp \
                src/sobel.cpp \
                src/magnitude.cpp \
                src/direction.cpp

# ─── Default target ───────────────────────────────────────────────────────────
.PHONY: all test canny_rv run profile clean

all: test canny_rv

# ─── Host unit tests (GoogleTest) ────────────────────────────────────────────
test:
	mkdir -p $(BUILD_DIR_HOST)
	$(HOST_CXX) $(HOST_FLAGS) \
	    src/host_tests.cpp $(PIPELINE_SRCS) \
	    -o $(BUILD_DIR_HOST)/test_runner \
	    -lgtest -lgtest_main -pthread
	./$(BUILD_DIR_HOST)/test_runner

# ─── Cross-compile full pipeline for RISC-V (Phase 5 profiling binary) ───────
canny_rv:
	mkdir -p $(BUILD_DIR_RV)
	$(RV_CXX) $(RV_FLAGS) \
	    src/main.cpp $(PIPELINE_SRCS) \
	    -o $(BUILD_DIR_RV)/canny_rv

# ─── Run profiling binary on QEMU ────────────────────────────────────────────
run: canny_rv
	$(QEMU) ./$(BUILD_DIR_RV)/canny_rv

# ─── Profile with a non-power-of-two image (good for strip-mining coverage) ──
profile: canny_rv
	@echo "--- VLEN=$(VLEN)  512x512 (200 iters) ---"
	$(QEMU) ./$(BUILD_DIR_RV)/canny_rv 512 512 200
	@echo ""
	@echo "--- VLEN=$(VLEN)  480x270 non-pow2 (200 iters) ---"
	$(QEMU) ./$(BUILD_DIR_RV)/canny_rv 480 270 200

# ─── Phase 4 compiler sweep (O0 / O2 / O3 / O3+vec) ─────────────────────────
$(BUILD_DIR_RV)/canny_O0:
	mkdir -p $(BUILD_DIR_RV)
	$(RV_CXX) $(RV_FLAGS_O0) src/main.cpp $(PIPELINE_SRCS) -o $@

$(BUILD_DIR_RV)/canny_O3:
	mkdir -p $(BUILD_DIR_RV)
	$(RV_CXX) $(RV_FLAGS_O3) src/main.cpp $(PIPELINE_SRCS) -o $@

$(BUILD_DIR_RV)/canny_O3_vec:
	mkdir -p $(BUILD_DIR_RV)
	$(RV_CXX) $(RV_FLAGS_O3V) src/main.cpp $(PIPELINE_SRCS) \
	    -o $@ 2>build/vec_report.txt

sweep: $(BUILD_DIR_RV)/canny_O0 canny_rv $(BUILD_DIR_RV)/canny_O3 $(BUILD_DIR_RV)/canny_O3_vec
	@echo "====== Phase 4 Compiler Sweep — timing (VLEN=$(VLEN)) ======"
	@echo "--- -O0 ---"; $(QEMU) $(BUILD_DIR_RV)/canny_O0    512 512 50
	@echo "--- -O2 ---"; $(QEMU) $(BUILD_DIR_RV)/canny_rv    512 512 50
	@echo "--- -O3 ---"; $(QEMU) $(BUILD_DIR_RV)/canny_O3    512 512 50
	@echo "--- -O3+vec ---"; $(QEMU) $(BUILD_DIR_RV)/canny_O3_vec 512 512 50

sizes: $(BUILD_DIR_RV)/canny_O0 canny_rv $(BUILD_DIR_RV)/canny_O3 $(BUILD_DIR_RV)/canny_O3_vec
	@echo "====== Binary Sizes ======"
	@for f in canny_O0 canny_rv canny_O3 canny_O3_vec; do \
	    printf '%-18s %s bytes\n' $$f $$(stat -c%s $(BUILD_DIR_RV)/$$f 2>/dev/null || echo N/A); \
	done

# ─── Clean ────────────────────────────────────────────────────────────────────
clean:
	rm -rf build/