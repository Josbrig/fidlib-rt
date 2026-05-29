<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Installation Guide — NVIDIA Jetson (JetPack)

**Platform:** NVIDIA Jetson Nano / Xavier NX / Orin NX / AGX Orin (aarch64)
**OS:** Ubuntu 20.04 / 22.04 via JetPack 5.x / 6.x
**GPU:** NVIDIA GPU (integrated Tegra) + CUDA, Vulkan, OpenCL via JetPack stack

## What can be enabled after installation?

| Feature | Status | Note |
|---------|--------|------|
| `FIDLIB_SIMD=ON` | immediately | NEON on AArch64 |
| `FIDLIB_FFT=ON` | immediately | Overlap-Save + FFTW3 |
| `FIDLIB_VULKAN=ON` | immediately (JetPack 5+) | NVIDIA Vulkan via JetPack driver |
| `FIDLIB_OPENCL=ON` | immediately (JetPack) | OpenCL via CUDA ICD, real GPU devices |

> **Prerequisite:** JetPack must already be installed. JetPack brings
> CUDA, cuDNN, libvulkan, and libOpenCL automatically.
> Check: `dpkg -l | grep nvidia-jetpack`

---

## Option A — Using the install script

```bash
chmod +x scripts/install-deps-jetson.sh

# Full install (base + GPU toolchain):
bash scripts/install-deps-jetson.sh

# Base only, without GPU tools:
bash scripts/install-deps-jetson.sh --no-gpu
```

> The script automatically detects whether `aptitude` is available; if not,
> it falls back to `apt-get`.

---

## Option B — Manual installation

### Step 0: Check JetPack status

```bash
dpkg -l nvidia-jetpack
# Expected: status "ii" (installed) with version 5.x or 6.x

# Check CUDA:
nvcc --version
# Expected: release 11.x or 12.x

# Check Vulkan driver:
dpkg -l libvulkan1
ls /usr/lib/aarch64-linux-gnu/libvulkan.so* 2>/dev/null || echo "Vulkan not found"
```

If JetPack is not yet installed:
→ [NVIDIA JetPack SDK Manager](https://developer.nvidia.com/embedded/jetpack)

### Step 1: Install aptitude (recommended)

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

### Step 5: Vulkan shader compiler and tools

JetPack already ships `libvulkan1` and `libvulkan-dev`.
Only the GLSL shader compiler needs to be installed from the Ubuntu repository:

```bash
sudo aptitude install -y \
    glslang-tools \
    spirv-tools \
    vulkan-tools

# If libvulkan-dev is missing (rare):
sudo aptitude install -y libvulkan-dev libvulkan1
```

Check GPU:
```bash
vulkaninfo --summary
# Expected: deviceName = NVIDIA Tegra ..., apiVersion = 1.x
```

### Step 6: OpenCL headers and ICD loader

JetPack provides `libOpenCL.so` via CUDA — only headers and the ICD dev package are needed:

```bash
sudo aptitude install -y \
    opencl-headers \
    ocl-icd-opencl-dev \
    ocl-icd-libopencl1 \
    clinfo
```

Check GPU devices:
```bash
clinfo --list
# Expected: NVIDIA GPU as OpenCL device
```

### Step 7: Documentation (optional)

```bash
sudo aptitude install -y doxygen graphviz
```

---

## cmake build after installation

### Full Jetson build (NEON + FFT + Vulkan + OpenCL)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -DFIDLIB_OPENCL=ON \
      -S . -B build_jetson

cmake --build build_jetson -j$(nproc)
ctest --test-dir build_jetson --output-on-failure
```

### Benchmark — NEON vs. OLA/FFT vs. Vulkan vs. OpenCL

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
      -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

### Without GPU (fallback build — if JetPack GPU stack is missing)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
      -S . -B build_cpu

cmake --build build_cpu -j$(nproc)
```

---

## Jetson model-specific notes

| Model | RAM | Recommended FFT threshold | Note |
|--------|-----|--------------------------|------|
| Nano (4 GB) | 4 GB | 512 (default) | Older Maxwell GPU, OpenCL 1.2 |
| Xavier NX (8/16 GB) | 8–16 GB | 512 | Volta GPU, OpenCL 3.0 |
| Orin NX (8/16 GB) | 8–16 GB | 512 | Ampere GPU, Vulkan 1.3 |
| AGX Orin (32/64 GB) | 32–64 GB | 256–512 | Most powerful single GPU in embedded |

---

## OpenCL vs. Vulkan on Jetson

Both backends are fully functional on Jetson. For `fidlib`:

- **Vulkan Compute** uses `fir_dot.comp` (SPIR-V, FP32, 64 threads/workgroup)
- **OpenCL** uses `fir_dot.cl` (C99 kernel, FP32, 64 work-items/workgroup)

In dispatch priority OpenCL comes before Vulkan — when both are compiled,
OpenCL is preferred (first successful init wins). To test only one
backend: disable the other in the cmake configure step.

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|---------|
| `nvcc` not found | CUDA PATH missing | `export PATH=$PATH:/usr/local/cuda/bin` in `~/.bashrc` |
| `clinfo` shows 0 devices | OpenCL ICD not registered | `ls /etc/OpenCL/vendors/` — check NVIDIA ICD |
| cmake cannot find Vulkan | `libvulkan-dev` missing | `sudo aptitude install libvulkan-dev` |
| `glslangValidator` missing | `glslang-tools` missing | `sudo aptitude install glslang-tools` |
| cmake cannot find OpenCL | Headers missing | `sudo aptitude install opencl-headers ocl-icd-opencl-dev` |
| Linker: `-lOpenCL` not found | `libOpenCL.so` symlink missing | `sudo aptitude install ocl-icd-libopencl1` |
| `aptitude` missing (Ubuntu minimal) | Not in minimal image | `sudo apt-get install aptitude` or use `apt-get` |
