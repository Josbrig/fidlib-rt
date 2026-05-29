# TODO: GPU Acceleration — Implementation Plan

Date: 2026-05-28  
Branch: feature/gpu-beschleunigung  
Basis: `doc/concepts/gpu-beschleunigung-analyse.md`

---

## Design Principles

- IIR filters **always** remain FP64 on CPU (numerical stability, serial dependency)
- FIR filters get an optional FP32 path (NEON and GPU)
- Systems without GPU remain fully compatible — no mandatory dependency
- Compile-time selection via cmake options; runtime dispatch only within a chosen path
- Coefficient design (`fidmkf.h`, `fidlib.c`) always remains FP64

---

## Phase 1 — Precision Template Foundation

Prerequisite for all subsequent phases. No GPU code, but the foundation for FP32 paths.

- [ ] **1.1** `fidrf_cmdlist.h`: convert `double` in execution hot path to `FID_REAL`  
      44 locations; `FID_REAL` settable to `float` or `double` via cmake option.  
      IIR opcodes (16, 18, 19, 21): warning or compile error when `FID_REAL=float`.

- [ ] **1.2** Create cmake option `FIDLIB_PRECISION`  
      Values: `double` (default), `float`  
      Detection: if `aarch64` → default remains `double`; FP32 only explicit opt-in.  
      Defines `FID_REAL` as compile definition for fidlib and all dependent targets.

- [ ] **1.3** `fid_simd.h`: add FP32 NEON variant  
      `fid_fir_dot_f32()`: `float32x4_t` instead of `float64x2_t`, 4 elements/iteration instead of 2.  
      SSE2 analogue: `_mm_mul_ps` / `_mm_add_ps` (4×float instead of 2×double).  
      Both variants coexist; selection via `FID_REAL`.

- [ ] **1.4** Extend `FidFunc` typedef  
      `typedef double (FidFunc)(void *buf, double input);` remains for FP64 path.  
      New `typedef float (FidFuncF32)(void *buf, float input);` for FP32 path.  
      `fid_run_new()` returns the appropriate function pointer depending on `FID_REAL`.

- [ ] **1.5** Coefficient conversion in `fid_run_new()`  
      If `FID_REAL=float`: convert FP64 coefficients from `FidFilter.val[]` to FP32  
      and store in the `FidRun` object as `float` array.  
      Once only, not per sample — no RT overhead.

- [ ] **1.6** Test: FP32 vs. FP64 precision comparison  
      New test `test_fidlib_precision.c`:  
      Calculate FIR filter with known impulse response in FP64 and FP32.  
      Check that FP32 result is within acceptable tolerance (< 1e-6 relative).  
      IIR filter in FP32: trigger instability warning or test fails.

---

## Phase 2 — CPU FFT Overlap-Add (FP64, no GPU)

Highest priority for long FIR filters. No GPU overhead, FP64-safe, all target systems.

- [ ] **2.1** cmake: create `FIDLIB_FFT` option  
      Searches for FFTW3 (`find_package(FFTW3)`).  
      Fallback to KissFFT (header-only, embeddable in repo) if FFTW3 not found.  
      Compatible with RPi 3+, desktop, Jetson — everywhere libfftw3 is available.

- [ ] **2.2** `fidlib/fid_fft.h` / `fidlib/fid_fft.c`: overlap-add engine  
      API: `FidFftPlan *fid_fft_plan(const FidFilter *fir, int block_size);`  
      `void fid_fft_execute(FidFftPlan *plan, const double *in, double *out, int n);`  
      Internally: pre-calculate H(f) (FFT of coefficients), overlap-save per block.  
      Automatically active when FIR length > `FIDLIB_FFT_THRESHOLD` (default: 512 taps).

- [ ] **2.3** Integration in `fidrf_cmdlist.h` opcode 8  
      If `FIDLIB_FFT` active and FIR length ≥ threshold:  
      block mode instead of sample mode. `fid_run_new()` selects path automatically.  
      Fallback to NEON path for shorter filters or when `FIDLIB_FFT` not active.

- [ ] **2.4** Test: overlap-add correctness  
      Same impulse response as direct FIR path (result must be bit-identical to rounding error).  
      Edge cases: block boundaries, non-multiple block lengths, filter tail after impulse.

---

## Phase 3 — Vulkan Compute (FP32, RPi 4/5)

Prerequisite: Phase 1 complete. FIR only, FP32 only.

- [ ] **3.1** cmake: create `FIDLIB_VULKAN` option  
      `find_package(Vulkan)` — if not found, option automatically OFF.  
      Requires: `libvulkan-dev`, `glslang-tools` (for `glslc`).  
      On RPi 4/5 with Mesa V3DV immediately usable (no additional packages).  
      GPU-free systems: option remains OFF, no effect on behavior.

- [ ] **3.2** `fidlib/fid_vulkan.h` / `fidlib/fid_vulkan.c`: Vulkan context  
      One-time initialization: `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`.  
      Find compute queue (not graphics queue).  
      `fid_vulkan_init()` / `fid_vulkan_shutdown()` — lazy init, on first GPU call.  
      Error case (no Vulkan device): transparent fallback to NEON/scalar.

- [ ] **3.3** `fidlib/shaders/fir_dot.comp`: GLSL compute shader  
      Input: SSBO with `float` coefficients, SSBO with `float` samples.  
      Output: SSBO with `float` results (one value per workgroup).  
      Workgroup size: 64 or 256 threads (configurable via specialization constant).  
      cmake builds `.comp` → `.spv` (SPIR-V) at build time via `glslc`.

- [ ] **3.4** `fidlib/fid_vulkan.c`: buffer management  
      Host-visible `VkBuffer` for coefficients (once at `fid_run_new()`).  
      Host-visible `VkBuffer` for input/output data (per block).  
      On RPi 5 (unified memory): `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` without transfer overhead.

- [ ] **3.5** Auto-dispatch in `fidrf_cmdlist.h` opcode 8  
      Decision logic:  
      - FIR length < 64 taps → NEON (overhead dominates)  
      - FIR length 64–512 taps → NEON-FP32 (GPU marginal)  
      - FIR length > 512 taps + `FIDLIB_VULKAN` active + block mode → GPU  
      - Otherwise → NEON / scalar  
      Thresholds overridable as cmake defines.

- [ ] **3.6** Test: Vulkan FIR correctness  
      `test_fidlib_vulkan.c`: skip if no Vulkan device.  
      Same impulse response as CPU path, tolerance 1e-6 (FP32 precision).  
      Test with 64, 256, 1024 taps.

---

## Phase 4 — OpenCL (optional, desktop / Jetson)

Low priority. Not for RPi (Clover without V3D pipe, no GPU benefit).

- [ ] **4.1** cmake: create `FIDLIB_OPENCL` option  
      `find_package(OpenCL)` — if not found, option automatically OFF.  
      Explicit exclusion condition: if `CMAKE_SYSTEM_PROCESSOR` matches `aarch64` and  
      no Rusticl ICD detectable → issue warning, force option to OFF.  
      Target platforms: x86_64 (AMD/NVIDIA), Jetson (CUDA OpenCL ICD).

- [ ] **4.2** `fidlib/fid_opencl.h` / `fidlib/fid_opencl.c`  
      Analogous to Vulkan backend: context, queue, buffer, kernel launch.  
      `fidlib/kernels/fir_dot.cl`: OpenCL C kernel (FP32).  
      cmake compiles kernel at runtime via `clBuildProgram` (no SPIR-V needed).

- [ ] **4.3** Test: OpenCL FIR correctness  
      Skip if no OpenCL platform with GPU device (not CPU fallback).

---

## Phase 5 — Benchmarks

- [ ] **5.1** `tests/bench_fir_backends.c`  
      Measures throughput (samples/s) for all available paths:  
      scalar-FP64, NEON-FP64, NEON-FP32, FFT-overlap-add, Vulkan (if active), OpenCL (if active).  
      FIR lengths: 16, 64, 256, 512, 1024, 4096 taps.  
      Output as CSV for analysis.

- [ ] **5.2** cmake: `BUILD_BENCHMARKS` option  
      Separate cmake target, not part of `ctest` (benchmarks are not deterministic).

---

## Compatibility Invariants (must hold at every step)

| Condition | Invariant |
|---|---|
| `FIDLIB_VULKAN=OFF` (default) | No Vulkan header included, no linker dependency |
| `FIDLIB_OPENCL=OFF` (default) | No OpenCL header included, no linker dependency |
| `FIDLIB_FFT=OFF` (default) | No FFTW3/KissFFT dependency |
| `FIDLIB_PRECISION=double` (default) | No behavioral difference from current state |
| IIR filters always | FP64, CPU, no GPU dispatch, no exception |
| No GPU device at runtime | Transparent fallback to NEON/scalar, no error |
| RPi 1/2/3 | All options compilable, GPU options without effect |

---

## Order Dependencies

```
Phase 1 (precision template)
  └── Phase 2 (FFT, FP64) — independent of Phase 1, parallel start possible
  └── Phase 3 (Vulkan)    — requires Phase 1 (FP32 path)
       └── Phase 4 (OpenCL) — independent of Phase 3, after Phase 1
            └── Phase 5 (benchmarks) — after phases 1–4
```
