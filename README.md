# Canny Edge Detection on RISC-V with Vector Extension (RVV)

Embedded Systems Project — Cairo University
Language: C++ | Target: RISC-V (rv64gcv) | Emulator: QEMU user-mode

---

## Prerequisites: Install WSL (Windows Users)

If you are on Windows and do not have a Linux environment, you must use Windows Subsystem for Linux (WSL).

1. Open PowerShell as Administrator.
2. Run the following command:
```
wsl --install
```
3. Restart your computer if prompted.
4. Open the new "Ubuntu" app from your Start menu and set up your UNIX username and password.
5. Update your system:
```
sudo apt update && sudo apt upgrade -y
```

---

## 🚀 Environment Setup Guide

### Step 1: Install System Dependencies

Open the "WSL" app from your Start menu and run:

```bash
sudo apt update
sudo apt install -y autoconf automake build-essential bison flex texinfo gperf libtool \
    patchutils bc cmake libglib2.0-dev libpixman-1-dev libslirp-dev ninja-build \
    libmpc-dev libmpfr-dev libgmp-dev zlib1g-dev libexpat-dev
```

---

### Step 2: Build the Custom RISC-V Toolchain

> ⚠️ **WARNING:** This compiles GCC from scratch — 30 to 60 minutes depending on your CPU. Grab a coffee.

```bash
cd ~
git clone https://github.com/riscv-collab/riscv-gnu-toolchain --recursive --depth 1 --shallow-submodules
cd riscv-gnu-toolchain
./configure --prefix=$HOME/riscv-toolchain --with-arch=rv64gcv --with-abi=lp64d
make -j$(nproc)
```

**Troubleshooting:**

If you see a `Killed` error or lines saying `deleting intermediate file ...`, press Enter and continue with only 2 cores:
```bash
make -j2
```

If WSL shuts down suddenly, reopen it and run:
```bash
cd ~/riscv-gnu-toolchain
./configure --prefix=$HOME/riscv-toolchain --with-arch=rv64gcv --with-abi=lp64d
make -j2
```

---

### Step 3: Build QEMU Emulator

```bash
cd ~
git clone https://github.com/qemu/qemu --depth 1
cd qemu
./configure --target-list=riscv64-linux-user --enable-plugins --prefix=$HOME/qemu-install
make -j$(nproc)
make install
```

**Troubleshooting:**

If you see `python venv creation failed` after the configure step:
```bash
sudo apt install -y python3-venv
./configure --target-list=riscv64-linux-user --enable-plugins
make -j$(nproc)
sudo make install
```

---

### Step 4: Add Tools to Your PATH

```bash
echo 'export PATH="$HOME/riscv-toolchain/bin:$HOME/qemu-install/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

**Verify the installation:**

```bash
qemu-riscv64 --version             # should report QEMU 9.x or newer
riscv64-unknown-elf-g++ --version  # should report GCC 13.x, 14.x, or newer
```

---

## Getting Started

```bash
git clone https://github.com/mo-aboualy/Canny-Edge-Detection-on-RISC-V.git
cd Canny-Edge-Detection-on-RISC-V
```

---

## Build Targets

| Command | Description |
|---|---|
| `make test` | Compile and run host-side GoogleTest unit tests |
| `make canny_rv` | Cross-compile the pipeline for RISC-V |
| `make run` | Run on QEMU at VLEN=128 (default) |
| `make run VLEN=256` | Run on QEMU at VLEN=256 |
| `make run VLEN=512` | Run on QEMU at VLEN=512 |
| `make clean` | Remove all build artifacts |

> The pipeline must produce identical output at VLEN=128, 256, and 512 — this is a core requirement of RVV's vector-length-agnostic design.

---

## Repository Structure

```
Canny-Edge-Detection-on-RISC-V/
├── include/
│   └── image_io.h          # Image struct and I/O API
├── src/
│   ├── image_io.cpp        # Raw grayscale I/O implementation
│   └── validate_io.cpp     # Write → read → pixel verification test
├── .github/
│   └── workflows/          # CI: builds and runs host tests on every push
├── .gitignore
├── Makefile
└── test_rvv.cpp            # RVV sanity check (strip-mining loop with vle8/vadd/vse8)
```

---

## Image Format

All pipeline stages operate on raw grayscale images: a plain binary file of exactly `width × height` bytes with no header. Pixel layout is row-major, 1 byte per pixel.

To generate a test image:
```bash
python3 -c "
import struct
width, height = 256, 256
with open('test_input.raw', 'wb') as f:
    for y in range(height):
        for x in range(width):
            f.write(struct.pack('B', x))
print('Done:', width * height, 'bytes')
"
```

To validate I/O is working:
```bash
g++ -Wall -Iinclude src/validate_io.cpp src/image_io.cpp -o validate
./validate
# Expected: PASS: image I/O working correctly
```

---

## Team Roles

| Student | Role | Week 1 | Week 2 |
|---|---|---|---|
| A (Mohamed) | Infrastructure | Toolchain, QEMU, Makefile, image I/O, CI | Profiling harness, binary size measurement, compiler flag sweep, QEMU VLEN experiments |
| B (Ahmed & Marwan) | Pipeline | Scalar Gaussian, Sobel, magnitude, direction | Compiler optimization analysis, auto-vectorization investigation |
| C (Hesham & Mina)  | Testing & Vectorization | GoogleTest setup, unit tests, kernel template interface | RVV intrinsic implementations, LMUL experiments, VLEN sweep |

---

## Git Workflow

- Branch for every feature: `git checkout -b feature/gaussian-blur`
- Merge via pull requests — no direct pushes to `main`
- Commit messages must describe the change: `Add 5x5 Gaussian convolution with zero-padding boundary handling`
- Binary files, `.raw` images, and compiled outputs are excluded via `.gitignore`
