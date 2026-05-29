<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Solution: GPU-Accelerated FIR Filtering via Vulkan (Raspberry Pi 5)

## The Problem

A very long FIR filter (4096 taps, e.g. a room impulse response / convolution reverb)
is to be processed in real time on the Raspberry Pi 5. Even the
overlap-save FFT engine hits CPU limits with very long filters.
The VideoCore VII (V3D 7.1) of the RPi 5 supports Vulkan 1.2 with compute shaders —
the GPU core should take over the parallel convolution.

**Requirements:**
- FIR filter with ≥ 256 taps offloaded to GPU
- Vulkan 1.2 Compute via Mesa V3DV (no proprietary driver)
- Transparent fallback: if GPU unavailable → OLA/FFT or NEON
- No API difference compared to CPU filtering

---

## Which project tools help

- **`FIDLIB_VULKAN=ON`** — activates the Vulkan compute backend in `fid_vulkan.h`
- **`fir_dot.comp`** — GLSL compute shader (64 threads, FP32)
- **`FIDLIB_VULKAN_THRESHOLD`** — above this tap count the GPU is preferred (default: 256)
- **`FIDLIB_VULKAN_BATCH`** — number of samples per dispatch (default: 256)
- **`spv_to_header.cmake`** — shader is compiled to SPIR-V at build time and
  embedded as a C array; no external shader file needed at runtime

---

## Step 1: Install dependencies

```bash
sudo aptitude install libvulkan-dev glslang-tools spirv-tools vulkan-tools

# Check whether V3D 7.1 is found:
vulkaninfo --summary
# Expected: deviceName = V3D 7.1, apiVersion = 1.2.xxx, deviceType = INTEGRATED_GPU
```

If `vulkaninfo` shows no GPU:
```bash
# Activate Mesa V3DV (should be active automatically in Bookworm):
ls /usr/share/vulkan/icd.d/
# Expected: broadcom_icd.aarch64.json or similar
```

## Step 2: cmake build with Vulkan

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -S . -B build_vk

cmake --build build_vk -j$(nproc)
```

cmake output (relevant lines):
```
-- Compiling fir_dot.comp → SPIR-V
-- Embedding SPIR-V as C array → fir_dot_spv.h
-- fidlib VULKAN: Vulkan 1.2.xxx, Batch=256, Threshold=256
```

If cmake disables `FIDLIB_VULKAN`:
```
-- WARNING: fidlib VULKAN: no GLSL compiler (glslc/glslangValidator) found
```
→ `sudo aptitude install glslang-tools` and rerun cmake.

## Step 3: Understand Vulkan initialisation

The Vulkan engine initialises **lazily** on the first `fid_run_new` call
with a FIR filter ≥ threshold. If no GPU is found or the Vulkan init fails,
`fid_run_new` automatically falls back to OLA/FFT:

```
fid_run_new(filt, &fn) is called
    → tap count ≥ FIDLIB_VULKAN_THRESHOLD?
    → vk_init() (once): VkInstance → PhysicalDevice → Device → Queue
    → no device found → returns NULL → next stage (OLA/FFT)
```

## Step 4: Program unchanged — same API

```c
#include <fidlib/fidlib.h>
#include <stdlib.h>

// Exactly the same API as without Vulkan:
FidFilter *filt = fid_design("...", 44100.0, -1.0, -1.0, 0, NULL);
// filt must be a FIR filter with ≥ 256 taps — e.g. via fid_cv_array

FidFunc *step_fn;
void    *run = fid_run_new(filt, &step_fn);
// ^ Automatically selects: OpenCL → Vulkan → OLA → Scalar

void *buf = fid_run_newbuf(run);
free(filt);

// RT phase:
double out = step_fn(buf, input_sample);
// ^ Internally: samples are buffered; at B samples → GPU dispatch → output

// Cleanup:
fid_run_freebuf(buf);
fid_run_free(run);
```

## Step 5: Understand the batch mechanism

The Vulkan backend collects samples in a host buffer until `FIDLIB_VULKAN_BATCH`
samples are available, then:

1. Coefficients + input buffer → GPU VRAM (host-visible buffer, unified memory)
2. Compute dispatch: `ceil(B/64)` workgroups, 64 threads each → parallel FIR convolution
3. `vkQueueWaitIdle` — wait until GPU is done
4. Read output buffer from GPU → deliver sample by sample

Output only arrives after `FIDLIB_VULKAN_BATCH` input samples (= batch latency).
With Batch=256 and 44100 Hz: ~5.8 ms latency.

**Adjust batch size:**
```bash
# Larger batches → less dispatch overhead, more latency:
cmake ... -DFIDLIB_VULKAN_BATCH=1024 ...

# Smaller batches → less latency, more overhead:
cmake ... -DFIDLIB_VULKAN_BATCH=64 ...
```

## Step 6: Control dispatch priority

When both `FIDLIB_VULKAN=ON` and `FIDLIB_OPENCL=ON` and `FIDLIB_FFT=ON`:

```
OpenCL (highest priority) → Vulkan → OLA/FFT → NEON/Scalar
```

Vulkan without OpenCL:
```bash
cmake ... -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=OFF ...
```

Then Vulkan is the first GPU option.

## Step 7: Benchmark — CPU vs. GPU

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

CSV output contains `backend` column: `scalar`, `neon`, `ola_fftw3`, `vulkan`.

## Step 8: Run tests

```bash
ctest --test-dir build_vk -R fidlib_vulkan --output-on-failure
```

The test checks the correctness of the Vulkan backend against direct convolution.
If no GPU is found it skips itself with `SKIP`.

---

## How the shader works

`fidlib/fir_dot.comp` (GLSL compute shader):
```glsl
layout(local_size_x = 64) in;   // 64 threads per workgroup
layout(push_constant) uniform Params { int M; int B; } params;
// CoefBuf: FIR coefficients (M values, FP32)
// InputBuf: input ring with M-1 history + B new samples
// OutBuf: B output samples

void main() {
    int i = int(gl_GlobalInvocationID.x);  // sample index
    if (i >= params.B) return;
    float sum = 0.0;
    for (int k = 0; k < params.M; k++)
        sum += coef[k] * x[i + params.M - 1 - k];
    y[i] = sum;
}
```

Each thread computes one output sample independently → massively parallel.
With B=256 and 64 threads: 4 workgroups, all parallel on the GPU.

---

## Unified memory on RPi 5

The VideoCore VII has unified memory (DRAM is shared by CPU and GPU).
The Vulkan backend detects this via `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` and avoids unnecessary copies —
the buffer is directly accessible from both sides.

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|---------|
| cmake disables VULKAN | libvulkan-dev or glslangValidator missing | Install packages, rerun cmake |
| `vk_init()` finds no device | No Vulkan ICD loaded | Check `ls /usr/share/vulkan/icd.d/` |
| Test skips itself (SKIP) | No Vulkan device at runtime | Expected when no GPU — not an error |
| Wrong output values | FP32 precision for long filters | Vulkan uses FP32; use OLA/NEON for FP64 |
| High latency | Batch size too large | Reduce `FIDLIB_VULKAN_BATCH` |
