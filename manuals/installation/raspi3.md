<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Installation Guide — Raspberry Pi 3 / Zero 2 W

**Platform:** Raspberry Pi 3B / 3B+ / Zero 2 W (BCM2837 / BCM2710, aarch64)
**OS:** Raspberry Pi OS Bookworm (Debian 12)
**GPU:** VideoCore IV — no Vulkan, no OpenCL

## What can be enabled after installation?

| Feature | Status | Note |
|---------|--------|------|
| `FIDLIB_SIMD=ON` | immediately | NEON on AArch64 |
| `FIDLIB_FFT=ON` | immediately | Overlap-Save + FFTW3 |
| `FIDLIB_VULKAN=ON` | not available | VideoCore IV without Vulkan |
| `FIDLIB_OPENCL=ON` | not available | VideoCore IV without OpenCL |

> **Zero 2 W note:** Only 512 MB RAM. Large FFT blocks
> (`FIDLIB_FFT_THRESHOLD` > 512) can cause memory pressure.
> Recommendation: keep threshold at 256 or lower.

---

## Option A — Using the install script

```bash
chmod +x scripts/install-deps-raspi3.sh
bash scripts/install-deps-raspi3.sh
```

The script automatically detects low RAM (< 700 MB → Zero 2 W) and prints
a corresponding notice.

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
```

### Step 5: Documentation (optional)

```bash
sudo aptitude install -y doxygen graphviz
```

---

## cmake build after installation

### Standard RPi 3 build (NEON + FFT)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -S . -B build_raspi3

cmake --build build_raspi3 -j$(nproc)
ctest --test-dir build_raspi3 --output-on-failure
```

### Zero 2 W — low-memory build

The FFT threshold determines how many taps are required before the overlap-save
engine is activated. A lower value reduces memory usage per filter instance:

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_FFT_THRESHOLD=256 \
      -S . -B build_zero2

cmake --build build_zero2 -j2   # -j2 due to 512 MB RAM
```

### Benchmark

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

> **Note for Zero 2 W:** The benchmark also runs on 512 MB RAM —
> it only measures short bursts and does not allocate large buffers.

---

## Memory reference values

| Configuration | Memory per filter instance |
|---|---|
| Direct convolution 64 taps (FP64) | ~1 KB |
| Direct convolution 512 taps (FP64) | ~4 KB |
| OLA/FFT 1024 taps, N=2048 | ~128 KB (FFT buffer) |
| OLA/FFT 4096 taps, N=8192 | ~512 KB |

For Zero 2 W: threshold ≤ 256 and tap count ≤ 1024 recommended.

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|---------|
| Build aborts with OOM | Not enough RAM during linking | `cmake --build ... -j2` instead of `-j$(nproc)` |
| `cmake --version` < 3.16 | Old cmake version | `sudo aptitude install cmake` (Bookworm has 3.25+) |
| `FIDLIB_VULKAN` is ignored | V3D IV without compute | Expected — no action needed |
| fftw3 not found | `libfftw3-dev` missing | `sudo aptitude install libfftw3-dev` |
