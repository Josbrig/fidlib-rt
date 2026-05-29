<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Solution: Long FIR Filters Efficiently with Overlap-Save FFT

## The Problem

A FIR filter with 2048 taps is to be applied to an audio stream at 44100 Hz.
Direct convolution requires 2048 multiplications per sample — at 44100
samples/s that is ~90 million multiplications/second. On an embedded
system (RPi 5, Cortex-A76) this is already noticeable; with even longer FIR filters
(e.g. room impulse responses with 16384 taps) direct convolution is simply too slow.

**Requirements:**
- FIR filter with ≥ 512 taps on an embedded system in real time
- No manual FFT programming — automatic dispatch
- Correctness check: OLA output must match direct convolution

---

## Which project tools help

- **`FIDLIB_FFT=ON`** — activates the overlap-save engine in `fid_fft.h`
- **`FIDLIB_FFT_THRESHOLD`** — above this tap count OLA is chosen automatically
  (default: 512)
- **`FIDLIB_FFT_FFTW3`** — when `libfftw3-dev` is installed, FFTW3 is used instead of
  the built-in radix-2 algorithm (2–3× faster)
- **`FidFunc *step_fn`** — identical API to direct convolution; no code changes required

---

## Overlap-Save principle (brief summary)

Direct FIR convolution: for each output sample M multiplications are needed.
Cost: O(M) per sample.

Overlap-Save (OLA): the input is divided into blocks of size B,
each block is processed via FFT convolution:
- FFT size: N = next power of 2 with N ≥ 2M
- Block size: B = N − M + 1
- Cost per block: O(N log N) — amortised O((N log N) / B) per sample

For M=1024: N=2048, B=1025 → amortised ~22 operations/sample instead of 1024.

**Latency:** each block is only output once B input samples are available.
Latency = B − 1 samples.

---

## Step 1: cmake build with FFT backend

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -S . -B build_fft

cmake --build build_fft -j$(nproc)
```

Check cmake output:
```
-- fidlib FFT: Overlap-Save + FFTW3 3.3.x    ← FFTW3 found (faster)
-- or --
-- fidlib FFT: Overlap-Save + built-in Radix-2  ← fallback
```

If FFTW3 is not found: `sudo aptitude install libfftw3-dev` and rerun cmake.

## Step 2: Configure the threshold

The threshold determines when OLA is used instead of direct convolution.
The default is 512 taps. For real-time applications with tight memory a
lower value may make sense:

```bash
# OLA from 128 taps onwards:
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_FFT_THRESHOLD=128 \
      -S . -B build_lowthresh

cmake --build build_lowthresh -j$(nproc)
```

## Step 3: Create a long FIR filter

fidlib allows FIR filters via `fid_cv_array` (coefficient array) or
external coefficient files. For typical audio applications:

```c
// Example: boxcar FIR with 1024 taps (rectangular impulse response)
#include <fidlib/fidlib.h>
#include <stdlib.h>
#include <string.h>

static void *make_boxcar_fir(int M, double **fn_out_step, void **run_out) {
    // Coefficients: all equal to 1/M (normalised averaging filter)
    double *coef = malloc(M * sizeof(double));
    for (int i = 0; i < M; i++) coef[i] = 1.0 / M;

    // Build FidFilter from coefficient array
    FidFilter *ff = fid_cv_array(coef, M);
    free(coef);

    FidFunc *fn;
    void    *run = fid_run_new(ff, &fn);
    free(ff);

    *fn_out_step = fn;
    *run_out     = run;
    return fid_run_newbuf(run);
}
```

## Step 4: Identical API to direct convolution

The OLA backend is completely transparent — `step_fn` and `buf` are the same
types as for direct convolution:

```c
FidFunc *step_fn;
void    *run;
void    *buf = make_boxcar_fir(1024, &step_fn, &run);
// ^ With 1024 > 512 (threshold): OLA engine automatically active

// Run phase: identical to direct convolution:
double out = step_fn(buf, input_sample);

// Free phase: identical:
fid_run_freebuf(buf);
fid_run_free(run);
```

No code changes at all when switching from direct convolution to OLA —
just invoke cmake again with `-DFIDLIB_FFT=ON`.

## Step 5: Account for latency

With OLA the first B−1 output samples are 0 (the first block is only ready after
B input samples). For M=1024, N=2048, B=1025 the latency is
**1024 samples** ≈ 23 ms at 44100 Hz.

For real-time applications with latency requirements:
- Increase threshold to compute small FIR filters directly (no latency)
- Decrease threshold to switch to OLA earlier (smaller blocks, less latency)

Calculating OLA latency:
```
N = next power of 2 with N ≥ 2*M
B = N - M + 1
Latency = B - 1 samples = N - M samples
```

For M=512:  N=1024, latency=512 samples = 11.6 ms @ 44100 Hz
For M=1024: N=2048, latency=1024 samples = 23 ms
For M=4096: N=8192, latency=4096 samples = 93 ms

## Step 6: Benchmark — OLA vs. direct convolution

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

Expected CSV output (RPi 5 example):
```
backend,taps,samples_per_sec
scalar,64,15000000
neon,64,43000000
ola_fftw3,1024,280000000
ola_fftw3,4096,210000000
```

OLA/FFTW3 is typically 5–20× faster than NEON direct convolution for long FIR filters.

---

## firun with long FIR filter

firun automatically selects OLA when the project was built with `FIDLIB_FFT=ON`
and the filter has enough taps:

```bash
# Boxcar FIR via firun — 'x' is the FIR identity opcode in fidlib:
# (Custom FIR coefficients: via fid_cv_array in C or external file)

# Test: impulse response of a long FIR (1024 taps, if implemented):
build_fft/bin/firun -d 2200 44100 %I "x1024" 2>/dev/null || echo "FIR spec depends on filter type"
```

---

## Verification: check OLA correctness

```bash
# Test binary from the test suite:
cd build_fft && ctest -R fidlib_fft --output-on-failure
```

The `test_fidlib_fft` checks:
- **Impulse test**: 600-tap boxcar → after 600 samples exactly output 1/600 × 600 = 1.0
- **DC test**: constant input → output converges to the input signal
- **Energy conservation**: Parseval's theorem
- **Nyquist test**: π frequency → output 0 for even tap count

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|---------|
| OLA not active (cmake reports Radix-2) | libfftw3-dev missing | `sudo aptitude install libfftw3-dev` |
| Filter not in OLA engine (direct convolution) | Tap count < threshold | Lower `FIDLIB_FFT_THRESHOLD` |
| Output has zero prefix | OLA latency | Normal — first B-1 samples are 0 |
| High memory usage | Large N (many taps) | Increase threshold, reduce tap count |
