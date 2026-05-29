# GPU and Coprocessor Acceleration — Analysis

Date: 2026-05-28  
Branch: feature/gpu-beschleunigung  
Primary target platform: Raspberry Pi 5 (BCM2712, Cortex-A76, VideoCore VII)

---

## 1  Background

The CPU-side SIMD/NEON acceleration (`fid_simd.h`) already accelerates the FIR hot path
considerably (up to 3.8× for long filters). This report analyzes whether and where
GPU-based or coprocessor-based extensions are useful beyond that.

---

## 2  Critical Caveat: FP64 on VideoCore GPUs

fidlib computes exclusively with `double` (IEEE 754 FP64, 64-bit). All VideoCore GPUs
(RPi 1–5) do **not** support FP64 natively on a GPU:

| GPU generation | FP32 | FP64 |
|---|---|---|
| VideoCore IV (RPi 1–3) | QPU, native | not available |
| VideoCore VI (RPi 4) | native | not available |
| VideoCore VII (RPi 5, V3D 7.1) | native | not available |

A GPU path for fidlib would necessarily force FP32 or execute FP64 software-emulated
(~8–16× slower than FP32) — both are not a valid option in most scenarios for
numerically precise filter calculations.

**Consequence:** GPU use is only sensible if either  
(a) FP32 precision is consciously accepted (audio applications, visualization), or  
(b) the GPU is used for tasks outside the filter hot path (coefficient calculation,
FFT-based convolution with overlap-add).

---

## 3  Raspberry Pi — Hardware Overview by Model

### 3.1  Table: What is in which Pi?

| Model | SoC | CPU core | NEON | GPU | GPU compute |
|---|---|---|---|---|---|
| RPi 1 | BCM2835 | ARM1176JZF-S (ARMv6) | No | VideoCore IV | No (no API) |
| RPi 2 v1.1 | BCM2836 | Cortex-A7 (ARMv7) | 32-bit | VideoCore IV | No |
| RPi 2 v1.2 | BCM2837 | Cortex-A53 (ARMv8) | 64-bit | VideoCore IV | No |
| RPi 3 B/B+ | BCM2837 | Cortex-A53 (ARMv8) | 64-bit | VideoCore IV | No |
| RPi 4 B | BCM2711 | Cortex-A72 (ARMv8.2) | 64-bit | VideoCore VI | Yes (Vulkan 1.1, OpenCL 1.2†) |
| RPi 5 | BCM2712 | Cortex-A76 (ARMv8.2) | 64-bit | VideoCore VII | Yes (Vulkan 1.2, OpenCL†) |
| RPi 400 | BCM2711 | like RPi 4 | 64-bit | VideoCore VI | like RPi 4 |
| RPi CM4 | BCM2711 | like RPi 4 | 64-bit | VideoCore VI | like RPi 4 |
| RPi Zero 2 W | BCM2837B0 | Cortex-A53 | 64-bit | VideoCore IV | No |

† = requires optional package installation; native FP64 not available

### 3.2  CPU features on this system (RPi 5)

```
fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp
cpuid asimdrdm lrcpc dcpop asimddp
```

Important: **no `sve`** — Scalable Vector Extension is not implemented on the Cortex-A76
(ARMv8.2 makes SVE optional; the A76 omits it).

---

## 4  Available Compute APIs on Raspberry Pi

### 4.1  Vulkan Compute

On this system **immediately usable**:

```
GPU0: V3D 7.1.10.2 — Vulkan 1.2.289 — PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
Driver: mesa-vulkan-drivers 24.2.8 (installed)
```

Vulkan Compute means: arbitrary computations on the GPU via compute shaders (GLSL/SPIR-V),
without a graphics context. The API is low-level, verbose, but portable and productively
usable on RPi 4/5.

| Property | RPi 4 | RPi 5 |
|---|---|---|
| Vulkan version | 1.1 | 1.2 |
| Driver | Mesa V3DV | Mesa V3DV |
| `VK_KHR_storage_buffer_storage_class` | Yes | Yes |
| Compute shader | Yes | Yes |
| FP64 extension | No | No |
| Max workgroup size | 256 | 256 |
| Installation effort | none (Mesa) | none (Mesa) |

### 4.2  OpenCL

`mesa-opencl-icd` 24.2.8 is installed on this system. However, actual GPU usage
is **not** available:

**Implementation: Clover** — Mesa's older Gallium-based OpenCL.
Clover requires a hardware-specific pipe driver (`pipe_v3d.so` for VideoCore VII),
which is **not included** in the Debian package. The available pipe drivers cover
AMD, NVIDIA, Qualcomm Adreno and software rasterizer — none of them match V3D.

```
/usr/lib/aarch64-linux-gnu/gallium-pipe/
  pipe_kmsro.so   pipe_msm.so     pipe_nouveau.so
  pipe_r300.so    pipe_r600.so    pipe_radeonsi.so
  pipe_swrast.so  pipe_vmwgfx.so
  — no pipe_v3d.so —
```

Consequence: OpenCL calls fall back to `pipe_swrast` (llvmpipe, CPU software).
For GPU compute this is no more useful than direct CPU computation, with higher overhead.

**Rusticl** (the new Mesa OpenCL stack with Vulkan backend, which could address V3D
via V3DV) is not included in this Debian package. It would require a custom Mesa build
with `-Dgallium-opencl=disabled -Dglx=disabled -Dopencl-spirv=true`.

For RPi 4 there is the community project **VC4CL** (OpenCL 1.2 for VideoCore VI),
which uses 32-bit precision and is no longer actively developed.

| | RPi 1–3 | RPi 4 | RPi 5 |
|---|---|---|---|
| OpenCL installed | No | VC4CL (community) | mesa-opencl-icd (Clover, installed) |
| Actually GPU? | — | Yes (FP32, community) | No (CPU fallback via pipe_swrast) |
| Recommendation | — | Prefer Vulkan | Vulkan — only GPU path |

### 4.3  OpenGL ES Compute Shaders

Available from OpenGL ES 3.1, supported on RPi 3+ via Mesa. No separate
installation needed. Practically identical limitations as Vulkan (FP64 missing),
but older and less flexible API for compute tasks. Not recommended for
new development — Vulkan is the more modern path.

### 4.4  CUDA

Only on NVIDIA hardware (Jetson Nano, Jetson AGX Orin, desktop GPUs). Not
available on Raspberry Pi and not portable.

### 4.5  Metal

Only on Apple hardware (M1/M2/M3/M4, A-Series). Not available on Raspberry Pi.
Relevant for macOS/iOS ports.

---

## 5  ARM-specific Extensions Beyond NEON

### 5.1  SVE — Scalable Vector Extension

| Architecture | SVE | On RPi? |
|---|---|---|
| ARMv8.0–8.1 (Cortex-A53, A72) | No | RPi 1–4: No |
| ARMv8.2 (Cortex-A76) | Optional | RPi 5: **No** (not implemented) |
| ARMv8.2 (AWS Graviton2) | Yes | Not RPi |
| ARMv9 (Cortex-X2, A710) | SVE2 | Not RPi |
| Apple M1/M2 | AMX (proprietary) | Not RPi |

The Cortex-A76 in the RPi 5 does **not** implement SVE, even though the architecture
would allow it. SVE is thus not a viable path on this chip.

### 5.2  ASIMD-DP (dot product) — already available

`asimddp` is present in the feature string of the RPi 5. This is the
`vdot` / `udot` instruction (INT8 dot product). Not usable for FP64 filter calculations —
only relevant for quantized neural networks.

### 5.3  Half-Precision NEON (asimdhp / fphp)

`asimdhp` and `fphp` are present: 16-bit floating-point NEON.
Unsuitable for audio filters (too little precision, 3–4 decimal places).
Could be useful for visualization in fiview.

### 5.4  NEON with -march=armv8.2-a+dotprod

Allows the compiler to use `vdot` instructions for integer code.
Not relevant for the FP64 core of fidlib.

---

## 6  Analysis: Where Would GPU Compute Help?

### 6.1  Streaming vs. Batch — the fundamental problem

firun and the fidlib hot path process audio **sample-by-sample** (one `double`
per call to `filter_step`). The GPU is optimized for batch processing:
many data in parallel, high throughput, but high startup latency.

Transfer overhead for a typical audio block (1024 samples × 8 bytes = 8 KB):

- CPU→GPU transfer (PCIe / shared memory): ~10–50 µs
- GPU kernel launch: ~5–20 µs  
- Total latency: ~15–70 µs

At 44100 Hz and 1024 samples the available time window is **23.2 ms**.
The transfer latency is therefore manageable — but only when operating in blocks.

### 6.2  FIR filters (opcode 8) — candidate

The NEON-accelerated FIR hot path (`fid_fir_dot`) could benefit further from
the GPU for very long filters:

| FIR length | NEON gain | GPU potential | Conclusion |
|---|---|---|---|
| < 64 taps | 2–3× | Overhead dominates | No |
| 64–256 taps | 3–4× | Marginal gain (~1.2×) | Questionable |
| 256–1024 taps | 3.5–4× | Relevant in block mode | Conditionally yes |
| > 1024 taps | 3.8× | FFT convolution (overlap-add) preferable | Yes (FFT) |

**Most important finding:** For very long FIR filters, **FFT-based convolution
(overlap-add / overlap-save)** is always superior to GPU compute shaders, because
FFT scales O(N log N) instead of O(N²) and is also highly optimizable on CPU.

### 6.3  IIR filters (opcodes 16, 18, 19, 21) — not a candidate

Butterworth, Chebyshev, Bessel: serial feedback structure. The `y[t−1]`
from the previous sample step must be known before `y[t]` can be computed.
GPU cannot resolve this dependency. No gain possible.

### 6.4  Coefficient calculation (fidmkf.h) — not a candidate

The coefficient calculation (Butterworth poles, bilinear transformation) runs
once and takes < 1 ms on CPU. GPU overhead would dominate.

### 6.5  FFT-based convolution — real candidate

For very long FIR filters (> 512 taps, resampling, room acoustics simulation):

```
Overlap-add on GPU:
  1. Input block FFT  → GPU
  2. Element-wise multiplication with H(f) (filter frequency response)
  3. IFFT              → GPU
  4. Overlap-add      → CPU or GPU
```

On VideoCore VII (RPi 5): Mesa Vulkan Compute supports this in FP32.
For FP64 software emulation would be required — practically not sensible.

---

## 7  Is it Worth It — by Raspberry Pi Model?

| Model | CPU NEON | GPU compute | Recommendation |
|---|---|---|---|
| **RPi 1** | No (ARMv6) | No | Scalar C only |
| **RPi 2 v1.1** (A7) | 32-bit NEON | No | NEON 32-bit (float) |
| **RPi 2 v1.2 / RPi 3** | 64-bit NEON | No | NEON (FP64, worthwhile) |
| **RPi 4** | 64-bit NEON | Vulkan 1.1 | Prefer NEON; GPU only for FIR > 512 taps + FP32 |
| **RPi 5** | 64-bit NEON | Vulkan 1.2 | Prefer NEON; GPU for FFT convolution + FP32 interesting |
| **RPi Zero 2 W** | 64-bit NEON | No | NEON, no GPU compute |
| **RPi CM4** | like RPi 4 | like RPi 4 | like RPi 4 |

### Summary: When is GPU worthwhile on the RPi?

**Worthwhile:**
- RPi 4/5, very long FIR filters (> 512 taps), block mode (not realtime per sample)
- FP32 precision acceptable (audio visualization, realtime spectrum in fiview)
- Vulkan Compute available, no additional package needed

**Not worthwhile:**
- RPi 1/2/3 (no compute API)
- IIR filters (serial dependency, GPU does not help)
- Realtime sample streaming (latency > gain)
- FP64 precision required (GPU only emulates this)

---

## 8  Other ARM Systems Beyond Raspberry Pi

| System | CPU | GPU | Compute API | Note |
|---|---|---|---|---|
| **Jetson Nano** | Cortex-A57 | Maxwell (128 CUDA cores) | CUDA 10, cuDNN | FP64 available, real GPU |
| **Jetson Orin Nano** | Cortex-A78AE | Ampere (1024 CUDA cores) | CUDA 11, FP64 | Best FP64 performance in ARM embedded |
| **Apple M1/M2/M3** | Firestorm+Icestorm (ARMv8.6) | Apple GPU (7–40 cores) | Metal Compute | AMX matrix extension; excellent for FP64 compute |
| **AWS Graviton3** | Neoverse V1 (ARMv9, SVE2) | No GPU | — | SVE2 for CPU; no integrated GPU |
| **BeagleBone Black** | Cortex-A8 | PowerVR (no compute) | — | PRU coprocessor for realtime I/O |
| **Qualcomm RB5/RB3** | Cortex-A77 + Hexagon DSP | Adreno 650 | OpenCL 2.0, Hexagon SDK | Hexagon DSP: real FP32/INT8 DSP processor |
| **NXP i.MX 8** | Cortex-A53 + Cortex-M4 | Vivante GC7000Lite | OpenCL 1.2 | M4 usable as DSP coprocessor |

### Particularly interesting: Qualcomm Hexagon DSP

Qualcomm chips (RB5, RB3 Gen2, Snapdragon-based boards) have a dedicated
Hexagon DSP designed for exactly these filter processing tasks:
- FP32 + INT8 native
- Very low latency (no bus transfer like GPU)
- Hexagon SDK (C API, no shader language needed)
- Disadvantage: Qualcomm hardware only

---

## 9  Possible Implementation Paths

### Path A: Vulkan Compute (FP32, RPi 4/5)

```
fidlib/fid_vulkan.h        — platform detection + context init
fidlib/fid_vulkan.c        — VkDevice, VkQueue, VkBuffer, compute pipeline
fidlib/shaders/fir_dot.comp — GLSL compute shader (FP32)
fidlib/CMakeLists.txt      — FIDLIB_VULKAN option
```

cmake option: `-DFIDLIB_VULKAN=ON`  
Requirements: `libvulkan-dev`, SPIR-V compiler (`glslc` from `glslang-tools`)  
FP32 precision; only sensible for very long FIR blocks

### Path B: OpenCL (FP32, desktop / Jetson / Apple)

```
fidlib/fid_opencl.h
fidlib/fid_opencl.c
fidlib/kernels/fir_dot.cl
```

cmake option: `-DFIDLIB_OPENCL=ON`  
More portable than Vulkan on non-RPi hardware (AMD, NVIDIA, Jetson, Apple).  
**Not sensible on RPi 5:** `mesa-opencl-icd` is Clover without `pipe_v3d.so` —
OpenCL runs on the CPU software rasterizer, not on VideoCore VII.
Real GPU OpenCL on RPi 5 would require a custom Mesa build with Rusticl.

### Path C: Overlap-Add FFT (FP64 compatible, CPU or GPU)

Longest-term most interesting path for very long FIR filters:
- FFT-based convolution on CPU with FFTW3 or KissFFT
- Automatic GPU fallback via cuFFT (Jetson) or vkFFT (Vulkan)
- FP64 fully preserved on CPU FFT path
- Transparent interface: same `fidlib` API, different execution path

---

## 10  Overall Recommendation

| Priority | Measure | Effort | Target |
|---|---|---|---|
| 1 | NEON remains the primary path | — | All RPi 2v1.2+, FP64, no overhead |
| 2 | Overlap-add FFT (CPU, FFTW3) | medium | FIR > 512 taps, FP64, RPi 3+ |
| 3 | Vulkan Compute (FP32) | high | FIR > 512 taps, FP32 acceptable, RPi 4/5 |
| 4 | OpenCL (FP32) | medium | Desktop + Jetson (not RPi — no GPU benefit) |
| 5 | CUDA | high | Jetson only; not in primary scope |

**Recommended for the next implementation phase:** Path C (CPU FFT with FFTW3).
This solves the actual problem of long FIR filters without FP32 precision loss,
runs on all target systems, and creates the foundation for a later Vulkan path.

**OpenCL on RPi explicitly excluded:** `mesa-opencl-icd` (Clover) has no
V3D pipe driver — calls run on the CPU. Vulkan Compute is on RPi 4/5 the
only usable GPU path and is preferred for GPU work.

---

## 11  Assessment Change Through a Template-like Precision Approach

### 11.1  Initial Question

Does the assessment change if all relevant code sites are designed as type-generic
C functions — analogous to C++ templates, but using C means — so that
the execution path can be instantiated with `float` instead of `double` when needed?

**Short answer:** Yes, the assessment changes substantially at two points.
At one point it remains unchanged.

---

### 11.2  What Changes: FP32 Execution Path

The central obstacle to GPU use was that fidlib knows only `double` and
VideoCore has no native FP64. A template approach solves exactly this problem:
**design phase remains FP64, execution phase becomes precision-parametric.**

#### Two Independent Concerns

| Layer | File | double locations | Must remain FP64? |
|---|---|---|---|
| Coefficient calculation | `fidlib.c`, `fidmkf.h` | ~250 | **Yes.** Pole calculation, bilinear transformation, complex arithmetic require FP64 |
| Execution hot path | `fidrf_cmdlist.h` | ~44 | **No** — for FIR; conditional for IIR |
| Buffer layout | `fidrf_cmdlist.h` | ~8 | No — follows the execution type |
| Public API (`FidFunc`) | `fidlib.h` | ~15 | No — can be parametrized in parallel |

The coefficient calculation designs in FP64 and converts once when building
the `FidRun` instance. Per-sample overhead: zero.

#### Possible C Implementation: X-Include Pattern

```c
/* fidrf_execute_impl.h — write once, include twice */
#ifndef FID_REAL
#  define FID_REAL double
#endif

typedef FID_REAL (FidFuncT)(void *buf, FID_REAL input);

static FID_REAL
filter_step_impl(void *buf, FID_REAL input)
{
    /* identical opcode dispatch, just FID_REAL instead of double */
    ...
}
```

```c
/* fid_execute_f64.c */
#define FID_REAL double
#include "fidrf_execute_impl.h"

/* fid_execute_f32.c */
#define FID_REAL float
#include "fidrf_execute_impl.h"
```

44 `double` locations in `fidrf_cmdlist.h` need to be switched to `FID_REAL` —
a manageable effort for the greatest possible effect.

---

### 11.3  What Changes Concretely: NEON + GPU

#### FP32 NEON: a real gain even today

With an FP32 path, `fid_simd.h` can also use FP32 NEON:

```c
/* FP32 NEON: 4 floats per register instead of 2 doubles */
float32x4_t acc0 = vdupq_n_f32(0.f);
float32x4_t acc1 = vdupq_n_f32(0.f);
for (int i = 0; i <= n-8; i += 8) {
    acc0 = vfmaq_f32(acc0, vld1q_f32(coef+i),   vld1q_f32(data+i));
    acc1 = vfmaq_f32(acc1, vld1q_f32(coef+i+4), vld1q_f32(data+i+4));
}
```

Instead of 2 doubles per `vfmaq_f64` instruction, `vfmaq_f32` processes 4 floats.
On the Cortex-A76 (RPi 5): ~2× more throughput than the FP64 NEON path.
The current FP64 NEON gain is up to 3.8×. With FP32 NEON,
**~7–8× compared to scalar FP64** would be achievable — without GPU, without drivers.

#### GPU Vulkan Compute: now a valid path

| Scenario | Without templates | With templates + FP32 path |
|---|---|---|
| FIR < 64 taps | NEON: 2–3× | NEON-FP32: 4–6× |
| FIR 64–512 taps | NEON: 3–4× | NEON-FP32: 6–8×; GPU marginal |
| FIR > 512 taps | NEON: 3.8×; GPU no FP64 | NEON-FP32: 7–8×; GPU FP32 viable |
| IIR (all orders) | CPU FP64 | CPU FP64 (remains) |

The GPU path for FIR > 512 taps with templates is **no longer a workaround**, but
a clean parallel execution path. RPi 4 and RPi 5 with Vulkan Compute become
real GPU candidates for long FIR filters (overlap-add, FP32).

---

### 11.4  What Does Not Change: IIR Stability in FP32

**IIR on GPU remains excluded — and FP32 IIR on CPU is risky.**

For IIR filters a mathematical constraint applies that no template can remove:
high-order poles at low frequencies lie very close to the unit circle boundary.

Example: 8th order Butterworth, 100 Hz at 44100 Hz sample rate:

```
z-pole ≈ 0.999985724 + 0.000031416i
```

Representable in FP64. In FP32 (7 decimal places):

```
z-pole ≈ 0.9999857 + 0.0000314i
```

This sounds harmless — but it is not. The feedback loop accumulates the
error over many samples. For hard lowpass filters or high Q values, FP32 IIR
can become unstable or noisier than the filter attenuates.

**Consequence for the template approach:** coefficient design in FP64, execution in FP32
only enabled for FIR. For IIR the path must continue to be FP64 or explicitly
marked as "low-precision mode" to the user.

---

### 11.5  Revised Raspberry Pi Recommendation with Templates

| Model | Previously | With FP32 templates |
|---|---|---|
| **RPi 1** | Scalar C only | Scalar C only (no NEON, no GPU API) |
| **RPi 2 v1.1** (A7) | 32-bit NEON | FP32 NEON (previously FP64 suboptimal) |
| **RPi 2 v1.2 / RPi 3** | FP64 NEON, no GPU | FP32 NEON for FIR (~2× gain) |
| **RPi 4** | FP64 NEON, GPU no FP64 | FP32 NEON + Vulkan FP32 FIR (**Yes**) |
| **RPi 5** | FP64 NEON 3.8×, GPU blocked | FP32 NEON ~7–8× + Vulkan FP32 FIR (**Yes**) |
| **RPi Zero 2 W** | FP64 NEON | FP32 NEON for FIR |

**In brief:** RPi 3 benefits from better NEON. RPi 4 and 5 become GPU-capable for FIR.

---

### 11.6  Implementation Effort vs. Gain

| Measure | Effort | Gain |
|---|---|---|
| `fidrf_cmdlist.h`: 44 × `double` → `FID_REAL` | Small | FP32 execution path unlocked |
| `fid_simd.h`: FP32 NEON variant | Small | +2× NEON throughput for FIR |
| Public API: `FidFuncF32` parallel to `FidFunc` | Medium | Caller-side type choice |
| Vulkan compute shader (FIR FP32) | Large | GPU for FIR > 512 taps on RPi 4/5 |
| IIR FP32 path | Medium + risk | Questionable — numerically unstable at high orders |

**Recommended order:**
1. Parametrize `fidrf_cmdlist.h` with `FID_REAL` (foundation)
2. Add FP32 NEON to `fid_simd.h` (immediate gain, no GPU effort)
3. Vulkan path only for FIR, after verification of FP32 coefficient conversion

---

## Appendix: Package Status on This System

```
Vulkan:   mesa-vulkan-drivers 24.2.8   — immediately usable (V3D 7.1, GPU)
OpenCL:   mesa-opencl-icd 24.2.8 installed (ICD: /etc/OpenCL/vendors/mesa.icd)
          Implementation: Clover, OpenCL 1.1
          No pipe_v3d.so → Number of devices: 0
/dev/dri: card0, card1, renderD128 present
```

clinfo output (current state):
```
Platform Name     Clover
Platform Version  OpenCL 1.1 Mesa 24.2.8-1~bpo12+rpt4
Number of devices 0
  → No devices found in platform
  → clCreateContextFromType(..., CL_DEVICE_TYPE_GPU) No devices found
```

**Conclusion:** OpenCL is formally registered but completely unusable — not a single device,
neither GPU nor CPU fallback. Clover finds no matching Gallium pipe for VideoCore VII.
Vulkan Compute is and remains the only functioning GPU compute path on this system.

**Consequence:** Despite installed `mesa-opencl-icd`, no GPU-accelerated OpenCL is
available on this RPi 5. Clover has no V3D pipe driver.
Rusticl (the newer Mesa OpenCL stack with Vulkan backend) could address V3D via V3DV
but is not registered as an ICD in this Mesa version.

For real GPU OpenCL on the RPi 5 one would need either:
- Rusticl manually activated (Mesa build option `rusticl`, not in Debian package), or
- wait for a future Mesa backport with Rusticl ICD.

**Vulkan Compute remains the only immediately usable GPU compute path on this system.**
