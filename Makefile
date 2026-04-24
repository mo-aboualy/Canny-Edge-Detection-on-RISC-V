VLEN ?= 128
HOST_CXX = g++
RV_CXX = riscv64-unknown-elf-g++

HOST_FLAGS = -Wall -Wextra -O2 -Iinclude
RV_FLAGS = -Wall -Wextra -O2 -Iinclude -march=rv64gcv 

BUILD_DIR_HOST = build/host
BUILD_DIR_RV = build/rv

all: test canny_rv

test: 
	mkdir -p $(BUILD_DIR_HOST)
	$(HOST_CXX) $(HOST_FLAGS) src/host_tests.cpp -o $(BUILD_DIR_HOST)/test_runner -lgtest -lgtest_main -pthread
	./$(BUILD_DIR_HOST)/test_runner

canny_rv:
	mkdir -p $(BUILD_DIR_RV)
	$(RV_CXX) $(RV_FLAGS) src/main.cpp -o $(BUILD_DIR_RV)/canny_rv

run: canny_rv
	qemu-riscv64 -cpu rv64,v=true,vlen=$(VLEN) ./$(BUILD_DIR_RV)/canny_rv

clean:
	rm -rf build/*

