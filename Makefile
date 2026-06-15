RV_CXX      = riscv64-unknown-elf-g++
RV_CXXFLAGS = -O3 -march=rv64gcv -std=c++17

QEMU        = qemu-riscv64
VLEN_128    = 128
IMG_WIDTH   = 100
IMG_HEIGHT  = 75

all: profile_rv

profile_rv: profile_pipeline.cpp
	$(RV_CXX) $(RV_CXXFLAGS) profile_pipeline.cpp -o profile_rv

run_profile: profile_rv
	$(QEMU) -cpu rv64,v=true,vlen=$(VLEN_128) ./profile_rv $(IMG_WIDTH) $(IMG_HEIGHT)

clean:
	rm -f profile_rv

.PHONY: all run_profile clean