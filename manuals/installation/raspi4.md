<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Installation Guide — Raspberry Pi 4

**Platform:** Raspberry Pi 4 Model B (BCM2711, aarch64)
**OS:** Raspberry Pi OS Bookworm (Debian 12)
**GPU:** VideoCore VI (V3D 4.2) — no Vulkan Compute, no OpenCL

## What can be enabled after installation?

| Feature | Status | Note |
|---------|--------|------|
| `FIDLIB_SIMD=ON` | immediately | NEON is always available on AArch64 |
| `FIDLIB_FFT=ON` | immediately | Overlap-Save + FFTW3 backend |
| `FIDLIB_VULKAN=ON` | not useful | V3D 4.2 has no Vulkan Compute support |
| `FIDLIB_OPENCL=ON` | not available | No working OpenCL driver for V3D 4.2 |

> **GPU compute limitation:** The VideoCore VI (V3D 4.2) in the BCM2711 does
> support Vulkan for graphics (Mesa V3DV since Mesa 21), but **Vulkan Compute Pipelines**
> (`VkComputePipeline`) are not available. The cmake system automatically sets `FIDLIB_VULKAN`
> to `OFF` when the driver reports no compute capability.
> For GPU compute: use RPi 5 (VideoCore VII) or a desktop PC.

---

## Option A — Using the install script

```bash
chmod +x scripts/install-deps-raspi4.sh
bash scripts/install-deps-raspi4.sh
```

The script checks the BCM2711 SoC, shows packages, simulates, and asks for confirmation.

---

## Option B — Manual installation

### Step 1: Install aptitude

```bash
sudo apt-get update
sudo apt-get install aptitude
```

### Step 2: Build base

```bash
sudo aptitude install -y \
    build-essential \
    cmake \
    git \
    pkg-config

cmake --version   # must be >= 3.16
```

### Step 3: SDL2 build dependencies

```bash
sudo aptitude install -y \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxinerama-dev libxxf86vm-dev \
    libgl1-mesa-dev libasound2-dev libpulse-dev
```

### Step 4: FFTW3

```bash
sudo aptitude install -y libfftw3-dev
pkg-config --modversion fftw3   # version check
```

### Step 5: Documentation (optional)

```bash
sudo aptitude install -y doxygen graphviz
```

> **No step for Vulkan/OpenCL** — these features are not available on RPi 4.

---

## cmake build after installation

### Standard build (NEON + FFT — recommended configuration)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -S . -B build_raspi4

cmake --build build_raspi4 -j$(nproc)
ctest --test-dir build_raspi4 --output-on-failure
```

Expected cmake output:
```
-- fidlib SIMD: NEON (aarch64)
-- fidlib FFT: Overlap-Save + FFTW3 3.x.x
```

### Debug build

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -S . -B build_debug

cmake --build build_debug -j$(nproc)
ctest --test-dir build_debug --output-on-failure
```

### Benchmark (NEON vs. OLA/FFT)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

### What happens if you set `FIDLIB_VULKAN=ON` anyway?

cmake automatically detects that no suitable Vulkan Compute driver is present
and disables the feature with a warning:

```
-- WARNING: fidlib VULKAN: Vulkan not found — FIDLIB_VULKAN disabled
```

There is no build error — the system falls back safely to NEON/FFT.

---

## Expected performance RPi 4

With NEON + FFTW3-FFT:
- Direct convolution (< 512 taps): ~20–30 million samples/s (NEON FP64)
- Overlap-Save FFT (≥ 512 taps): significantly faster than O(N²) direct convolution
- FP32 mode (`-DFIDLIB_PRECISION=float`): ~2× faster than FP64 for FIR

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|---------|
| `cmake` cannot find `fftw3` | `libfftw3-dev` missing | `sudo aptitude install libfftw3-dev` |
| SDL2 configure error | X11/audio headers missing | Repeat step 3 |
| `aptitude` missing | Not pre-installed | `sudo apt-get install aptitude` |
| cmake reports `FIDLIB_VULKAN` disabled | V3D 4.2 without compute | Expected — no action needed |
