<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Installation Guide — Raspberry Pi 5

**Platform:** Raspberry Pi 5 (BCM2712, aarch64)
**OS:** Raspberry Pi OS Bookworm (Debian 12)
**GPU:** VideoCore VII (V3D 7.1) — Vulkan 1.2 natively via Mesa V3DV

## What can be enabled after installation?

| Feature | Status | Note |
|---------|--------|------|
| `FIDLIB_SIMD=ON` | immediately | NEON is always available on AArch64 |
| `FIDLIB_FFT=ON` | immediately | Overlap-Save + FFTW3 backend |
| `FIDLIB_VULKAN=ON` | immediately | Vulkan 1.2 Compute via V3D 7.1 (FP32) |
| `FIDLIB_OPENCL=ON` | only after self-built Mesa Rusticl | Clover reports 0 GPU devices on RPi5 |

---

## Option A — Using the install script

The script [`scripts/install-deps-raspi5.sh`](../../scripts/install-deps-raspi5.sh)
performs all steps automatically with a confirmation prompt.

```bash
# Make executable once (only needed the first time):
chmod +x scripts/install-deps-raspi5.sh

# Run:
bash scripts/install-deps-raspi5.sh
```

The script:
1. Checks for `aarch64` and BCM2712 SoC
2. Checks whether `aptitude` is installed
3. Shows all packages to be installed
4. Runs `aptitude install --simulate` (dry run without changes)
5. Asks for confirmation (`j` = yes, anything else = abort)
6. Installs all packages
7. Shows installed versions and cmake examples

---

## Option B — Manual installation

### Step 1: Install aptitude (if not present)

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
```

Check versions:
```bash
cmake --version   # must be >= 3.16
gcc --version
```

### Step 3: SDL2 build dependencies

SDL2 is built from source via `ExternalProject_Add` —
a system SDL2 is not required. But the X11 and audio headers must be present
so that the SDL2 cmake configure step succeeds:

```bash
sudo aptitude install -y \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxinerama-dev libxxf86vm-dev \
    libgl1-mesa-dev libasound2-dev libpulse-dev
```

### Step 4: FFTW3 (for Overlap-Save FFT backend)

```bash
sudo aptitude install -y libfftw3-dev

# Check:
pkg-config --modversion fftw3
```

Without FFTW3 the FFT backend automatically falls back to the built-in radix-2
algorithm — this works, but FFTW3 is ~2–3× faster for large FFT blocks.

### Step 5: Vulkan toolchain

```bash
sudo aptitude install -y \
    libvulkan-dev \
    glslang-tools \
    spirv-tools \
    vulkan-tools
```

- **`libvulkan-dev`** — Vulkan headers + ICD loader (served by Mesa V3DV)
- **`glslang-tools`** — `glslangValidator`: compiles the GLSL compute shader
  `fidlib/fir_dot.comp` to SPIR-V (happens automatically during the cmake build)
- **`spirv-tools`** — `spirv-dis` / `spirv-val` for diagnostics (optional)
- **`vulkan-tools`** — `vulkaninfo` to check GPU capabilities

Check GPU:
```bash
vulkaninfo --summary
# Expected: deviceName = V3D 7.1, apiVersion = 1.2.x
```

### Step 6: OpenCL (optional — usable for diagnostics only)

```bash
sudo aptitude install -y \
    opencl-headers \
    ocl-icd-opencl-dev \
    mesa-opencl-icd \
    clinfo
```

> **Note:** `mesa-opencl-icd` (Clover) reports **0 GPU devices** on RPi5.
> OpenCL GPU support requires a self-built Mesa with the Rusticl backend —
> this is a multi-hour build process. Vulkan is the recommended GPU path.

Show OpenCL devices:
```bash
clinfo --list
# On standard RPi5: shows only CPU devices or empty
```

### Step 7: Documentation tools (optional)

```bash
sudo aptitude install -y doxygen graphviz
```

---

## cmake build after installation

### Standard build (NEON + FFT + Vulkan)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -S . -B build_raspi5

cmake --build build_raspi5 -j$(nproc)
ctest --test-dir build_raspi5 --output-on-failure
```

Expected cmake output (relevant lines):
```
-- fidlib SIMD: NEON (aarch64)
-- fidlib FFT: Overlap-Save + FFTW3 3.x.x
-- fidlib VULKAN: Vulkan 1.2.xxx, Batch=256, Threshold=256
```

### Debug build (with sanitizers for development)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -S . -B build_debug

cmake --build build_debug -j$(nproc)
ctest --test-dir build_debug --output-on-failure
```

### Benchmark (compare all backends)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

### With OpenCL (after self-built Rusticl Mesa)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
      -S . -B build_full

cmake --build build_full -j$(nproc)
```

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|---------|
| `cmake` cannot find Vulkan | `libvulkan-dev` missing | `sudo aptitude install libvulkan-dev` |
| `glslangValidator` missing | `glslang-tools` not installed | `sudo aptitude install glslang-tools` |
| `FIDLIB_VULKAN` is set to `OFF` | Vulkan or shader compiler missing | Read cmake output, install missing packages |
| `fftw3` not found | `libfftw3-dev` missing | `sudo aptitude install libfftw3-dev` |
| `clinfo` shows 0 devices | Clover without Rusticl | GPU OpenCL only after Mesa Rusticl build |
| `aptitude` missing | Not pre-installed | `sudo apt-get install aptitude` |
