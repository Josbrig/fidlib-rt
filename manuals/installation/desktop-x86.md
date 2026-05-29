<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Installation Guide — Desktop Linux x86_64

**Platform:** Ubuntu 22.04+ / Debian Bookworm, x86_64
**GPU:** NVIDIA, AMD, or Intel — full Vulkan and OpenCL stack available

## What can be enabled after installation?

| Feature | Status | Note |
|---------|--------|------|
| `FIDLIB_SIMD=ON` | immediately | SSE2 is always available on x86_64 |
| `FIDLIB_FFT=ON` | immediately | Overlap-Save + FFTW3 |
| `FIDLIB_VULKAN=ON` | immediately (with GPU driver) | NVIDIA/AMD/Intel depending on driver |
| `FIDLIB_OPENCL=ON` | immediately (with GPU driver) | ICD system — install driver separately |

> **GPU driver note:** The install script installs Vulkan headers, ICD loader,
> and Mesa drivers (for AMD/Intel). NVIDIA requires the proprietary driver separately.
> Without a GPU driver everything still works — fallback to SSE2 + FFT.

---

## Option A — Using the install script

```bash
# Full install (base + GPU):
bash scripts/install-deps-desktop-x86.sh

# Base only, without GPU packages:
bash scripts/install-deps-desktop-x86.sh --no-gpu
```

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
gcc --version
g++ --version
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
pkg-config --modversion fftw3
```

### Step 5: Vulkan toolchain

```bash
sudo aptitude install -y \
    libvulkan-dev \
    glslang-tools \
    spirv-tools \
    vulkan-tools \
    mesa-vulkan-drivers     # Mesa RADV (AMD) + ANV (Intel)
```

Check GPU capabilities:
```bash
vulkaninfo --summary
```

Expected output (AMD example):
```
deviceName  = AMD Radeon RX 6600
apiVersion  = 1.3.xxx
```

### Step 6: OpenCL base (ICD loader + headers)

```bash
sudo aptitude install -y \
    opencl-headers \
    ocl-icd-opencl-dev \
    ocl-icd-libopencl1 \
    clinfo
```

Show OpenCL devices:
```bash
clinfo --list
```

### Step 7: GPU vendor-specific OpenCL drivers

The OpenCL ICD system loads drivers dynamically — which drivers are available
depends on the GPU vendor:

#### NVIDIA

```bash
# Proprietary driver (recommended):
sudo aptitude install -y nvidia-driver nvidia-opencl-icd

# CUDA OpenCL (if CUDA is already installed):
# libOpenCL.so comes automatically with CUDA — no extra package needed
```

#### AMD

```bash
# Mesa Clover (open source, older GPUs):
sudo aptitude install -y mesa-opencl-icd

# ROCm (for newer RDNA GPUs, separate installation):
# https://rocm.docs.amd.com/en/latest/deploy/linux/
```

#### Intel

```bash
# Intel NEO (recommended, OpenCL 3.0, all Gen9+ GPUs):
sudo aptitude install -y intel-opencl-icd
```

---

## cmake build after installation

### Full build (SSE2 + FFT + Vulkan + OpenCL)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -DFIDLIB_OPENCL=ON \
      -S . -B build_desktop

cmake --build build_desktop -j$(nproc)
ctest --test-dir build_desktop --output-on-failure
```

Expected cmake output:
```
-- fidlib SIMD: SSE2 (x86_64)
-- fidlib FFT: Overlap-Save + FFTW3 3.x.x
-- fidlib VULKAN: Vulkan 1.x.xxx, Batch=256, Threshold=256
-- fidlib OPENCL: OpenCL 3.0, Batch=256, Threshold=256
```

### Vulkan only (without OpenCL)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON \
      -S . -B build_vk

cmake --build build_vk -j$(nproc)
```

### OpenCL only (without Vulkan)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_OPENCL=ON \
      -S . -B build_ocl

cmake --build build_ocl -j$(nproc)
```

### Benchmark — compare all backends

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
      -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

CSV output contains: backend, tap count, samples/second.

### FP32 mode (faster SIMD path)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
      -DFIDLIB_PRECISION=float \
      -S . -B build_fp32

cmake --build build_fp32 -j$(nproc)
```

> **Warning:** FP32 can become numerically unstable for high-order IIR filters.
> For FIR filters FP32 is safe and ~2× faster than FP64.

---

## Dispatch priority at runtime

When multiple backends are enabled, `fid_run_new` automatically selects:

```
OpenCL  →  Vulkan  →  OLA/FFT  →  NEON/SSE2-Scalar
```

Each stage is only used when:
- The backend is compiled in (`#ifdef FIDLIB_OPENCL` etc.)
- The tap count exceeds the threshold
- Initialization succeeded (GPU found)

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|---------|
| `vulkaninfo` shows no devices | GPU driver missing | Install vendor driver (see above) |
| `clinfo` shows no GPU devices | OpenCL ICD missing | Install vendor OpenCL package |
| NVIDIA: `vulkaninfo` finds no GPU | nvidia-driver not active | `sudo nvidia-smi` — check driver status |
| cmake cannot find Vulkan | `libvulkan-dev` missing | `sudo aptitude install libvulkan-dev` |
| cmake cannot find OpenCL | `ocl-icd-opencl-dev` missing | `sudo aptitude install ocl-icd-opencl-dev opencl-headers` |
| Linker error `-lOpenCL` | `libOpenCL.so` missing | `sudo aptitude install ocl-icd-libopencl1` |
