# SIMD/NEON Optimization for fidlib — Concept and Analysis

## What is this?

**SIMD** (Single Instruction, Multiple Data) is a CPU extension that processes multiple
numbers simultaneously with a single machine instruction. On the Raspberry Pi 5
(ARM Cortex-A76, AArch64), the instruction set is called **NEON** and operates on
128-bit registers. For `double` (64-bit), exactly two values fit in one NEON register.

The central instruction is `vfmaq_f64` — a vectorized fused multiply-add:

```
acc[0] += coef[0] * data[0]
acc[1] += coef[1] * data[1]   ← both in ONE clock cycle
```

The implementation also uses **dual accumulation** (two accumulators `acc0`
and `acc1` alternating), so that with 4 elements per loop iteration, 4 MACs are
completed in ~2 cycles instead of ~4.

---

## Where is this useful?

**Exclusively for long FIR filters** (Finite Impulse Response). An FIR filter of
length N computes a dot product per output sample:

```
y[t] = Σ h[k] · x[t−k]   for k = 0..N−1
```

These are N independent multiplications + additions — the ideal case for SIMD, because
there is no serial dependency.

**Not improved** are recursive IIR filters (Butterworth, Chebyshev, Bessel), since
their feedback structure enforces a serial dependency:

```
y[t] = b0·x[t] + b1·x[t−1] − a1·y[t−1] − a2·y[t−2]
```

`y[t]` depends on `y[t−1]` → no parallelization across taps is possible.

**Typical FIR applications** where the optimization takes effect:

- Windowed-sinc lowpass filters (audio resampling, antialiasing)
- Parks-McClellan equalizers with many taps
- Bandpass filters with sharp rolloff (> 50 taps)
- FIR differentiators and Hilbert transformers

---

## Where in the code is this implemented?

### `fidlib/fid_simd.h` — Primitive level

Platform detection at compile time and `fid_fir_dot()`:

```c
// Detection
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  define FID_SIMD_NEON 1       // AArch64 (Pi 4, Pi 5, Apple M1/M2, ...)
#elif defined(__SSE2__)
#  define FID_SIMD_SSE2 1       // x86_64
// otherwise: scalar C fallback

// Core function (NEON variant)
static inline double fid_fir_dot(const double *coef, const double *data, int n) {
    float64x2_t acc0 = vdupq_n_f64(0.0);
    float64x2_t acc1 = vdupq_n_f64(0.0);
    for (int i = 0; i <= n-4; i += 4) {
        acc0 = vfmaq_f64(acc0, vld1q_f64(coef+i),   vld1q_f64(data+i));
        acc1 = vfmaq_f64(acc1, vld1q_f64(coef+i+2), vld1q_f64(data+i+2));
    }
    // + remaining taps (1–3) scalar
```

### `fidlib/fidrf_cmdlist.h` — Hot path level

`filter_step()` processes a stream of opcodes. **Opcode 8** covers the case
of `4N× pure FIR taps` — exactly the long FIR block:

```c
// Scalar path (FIDLIB_SIMD off):
case 8:
    cnt = *cmd++;
    do { FIR; FIR; FIR; FIR; } while (--cnt > 0);  // 4 scalar MACs/round

// SIMD path (FIDLIB_SIMD on):
case 8: {
    int n = (int)(unsigned char)*cmd++ * 4;
    buf[-1] = tmp;                         // write sentinel
    fir += fid_fir_dot(coef, buf-1, n);    // SIMD dot product
    coef += n;  buf += n;  tmp = buf[-1];  // advance state machine
}
```

#### Technical Detail: Sentinel Slot

The delay buffer invariant states: at the time of opcode 8 entry, `buf[-1] == tmp`
always holds. For the case `j == 0` (opcode 8 is the very first command in the stream,
which is common for pure FIR filters), `buf[-1]` must be writable.
For this, `fid_run_newbuf()` reserves an additional `double` before `buf[0]`:

```
[ RunBuf header | sentinel_double | buf[0..siz-1] | coef[] | cmd[] ]
                                    ^--- rb->buf
```

All buffer management functions were consistently updated:

| Function | Change |
|---|---|
| `fid_run_newbuf()` | allocates `+1 double`, `rb->buf = alloc+1` |
| `fid_run_bufsize()` | returns `+sizeof(double)` |
| `fid_run_initbuf()` | identical layout with `memset(base, 0, buf_bytes)` |
| `fid_run_zapbuf()` | additionally zeros `buf[-1]` |

### `fidlib/CMakeLists.txt` — Build level

```cmake
option(FIDLIB_SIMD "SIMD acceleration for the FIR hot path" OFF)
if(FIDLIB_SIMD)
    target_compile_definitions(fidlib PUBLIC FIDLIB_SIMD)
    if(aarch64)  → NEON is always available, no extra flag needed
    if(x86_64)   → -msse2
```

`PUBLIC` ensures that test code and all consumer targets automatically
receive `FIDLIB_SIMD`.

---

## Performance Improvement

### Theoretical Analysis (Cortex-A76, Raspberry Pi 5)

| Metric | Scalar | NEON (dual-acc) | Factor |
|---|---|---|---|
| MACs per cycle (compute) | 1 | 4 (2× `vfmaq_f64` parallel) | **4×** |
| Practical (with memory, loop overhead) | — | — | **1.8×–3×** |

The Cortex-A76 has two floating-point pipelines that execute `vfmaq_f64` with
throughput 1/cycle. Dual accumulation (`acc0`/`acc1` alternating) hides the
4-cycle latency and keeps both pipes busy → ~4 MACs per cycle.

### Scaled by Filter Length

| FIR taps N | Scalar cycles | NEON cycles | Speed-up |
|---|---|---|---|
| 16 | ~16 | ~8 | **1.8×** |
| 64 | ~64 | ~22 | **2.9×** |
| 256 | ~256 | ~70 | **3.7×** |
| 1024 | ~1024 | ~268 | **3.8×** |

Values are estimates for cache-resident access. Memory-bound filters (delay line
does not fit in L1/L2) converge to ~2×, as memory bandwidth becomes the bottleneck.

### What is NOT faster

| Filter type | Opcode | SIMD effect |
|---|---|---|
| IIR biquad (Butterworth / Chebyshev / Bessel) | 18, 21 | **0 %** |
| IIR only (pole-only) | 16, 19 | **0 %** |
| Short FIR < 8 taps | 5, 6, 7 | **0 %** (ENDFIR path) |
| Long FIR ≥ 12 taps | **8** | **1.8×–3.8×** |

By far the most common filter type in this project (Butterworth LP/HP via `LpBu`/`HpBu`)
uses IIR and benefits **not at all**. The optimization only pays off when FIR filters
are specifically used with `fid_cv_array` or a Parks-McClellan designer.

---

## Activation

All three performance options have been **ON by default since 2026-05-28**:

| cmake option | Default | Effect |
|---|---|---|
| `FIDLIB_SIMD` | **ON** | NEON / SSE2 FIR dot product |
| `FIDLIB_FAST_MATH` | **ON** | `-O3 -ffast-math` scoped to fidlib |
| `FIDLIB_LTO` | **ON** | Link-time optimization on filter_step |

A normal build therefore automatically activates all optimizations:

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -S . -B build_release
cmake --build build_release -j$(nproc)
```

To disable individual stages:

```bash
cmake ... -DFIDLIB_SIMD=OFF -DFIDLIB_FAST_MATH=OFF -DFIDLIB_LTO=OFF
```

---

## Related Concepts

- `doc/concepts/fidlib-cpp20-rt-optimierung.md` — RT safety, cache layout
- `doc/concepts/fidlib-rt-todo.md` — original optimization roadmap
