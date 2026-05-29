<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Solution: Real-Time Filtering in Your Own C Program

## The Problem

An application receives audio samples one by one from a callback
(e.g. JACK, ALSA, PortAudio) and must filter each sample immediately — without
buffer latency, without malloc in the RT thread, without recompilation on parameter change.

**Requirements:**
- RT-safe: no malloc/free, no lock, no system call in the hot path
- Filter type and frequency from runtime configuration (e.g. command-line argument)
- Support for IIR and FIR in the same codebase
- Multiple independent instances of the same filter (e.g. L + R channel)

---

## Which project tools help

- **fidlib three-phase model**: Alloc → Run → Free
  - `fid_design()` — filter design (not RT-safe, one-time only)
  - `fid_run_new()` — run object with coefficients (not RT-safe, one-time only)
  - `fid_run_newbuf()` — state buffer (not RT-safe, one-time only)
  - `FidFunc *step_fn(buf, sample)` — single-sample processing (**RT-safe**)
- **`FIDLIB_SIMD=ON`** — NEON/SSE2 vectorisation, automatically active

---

## The three-phase model

```
┌─────────────────────────────────────────────────────────────────┐
│  ALLOC PHASE (before the RT thread)                             │
│  fid_design()   → FidFilter* (poles/zeros, coefficients)       │
│  fid_run_new()  → Run* (optimised execution plan)              │
│  fid_run_newbuf() → Buf* (delay lines, state)                  │
│  free(filt)     → free FidFilter (no longer needed)            │
├─────────────────────────────────────────────────────────────────┤
│  RUN PHASE (in RT thread, callback)                             │
│  step_fn(buf, sample) → filtered sample                        │
│  ← zero-alloc, branch-free, ~ns latency                       │
├─────────────────────────────────────────────────────────────────┤
│  FREE PHASE (on shutdown)                                       │
│  fid_run_freebuf(buf)                                          │
│  fid_run_free(run)                                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## Step by Step

### Step 1: Build the project

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
      -S . -B build

cmake --build build -j$(nproc)
```

### Step 2: Minimal program — one filter, one channel

```c
// filter_example.c
#include <fidlib/fidlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Aufruf: %s <fispec>  z.B.  LpBu4/1000\n", argv[0]);
        return 1;
    }

    // ── Alloc phase ──────────────────────────────────────────────────
    FidFilter *filt = fid_design(argv[1], 44100.0, -1.0, -1.0, 0, NULL);
    if (!filt) {
        fprintf(stderr, "Ungueltige Filter-Spec: %s\n", argv[1]);
        return 1;
    }

    FidFunc *step_fn;
    void    *run = fid_run_new(filt, &step_fn);
    void    *buf = fid_run_newbuf(run);
    free(filt);

    // ── Run phase (RT-safe from here) ─────────────────────────────────
    double sample;
    while (fread(&sample, sizeof(double), 1, stdin) == 1) {
        double out = step_fn(buf, sample);
        fwrite(&out, sizeof(double), 1, stdout);
    }

    // ── Free phase ───────────────────────────────────────────────────
    fid_run_freebuf(buf);
    fid_run_free(run);
    return 0;
}
```

Compile and link:
```bash
gcc -o filter_example filter_example.c \
    -I/pfad/zum/projekt/fidlib \
    -L/pfad/zum/projekt/build/fidlib \
    -lfidlib -lm -O2

# Or via cmake target when building the project:
# target_link_libraries(mein_programm PRIVATE fidlib)
```

### Step 3: Stereo — one run object, two buffers

The key point: `run` (coefficients) is shared, `buf` is per instance.

```c
// One filter design for L and R:
FidFilter *filt  = fid_design("LpBu4/4000", 44100.0, -1.0, -1.0, 0, NULL);
FidFunc   *fn;
void      *run   = fid_run_new(filt, &fn);
void      *buf_l = fid_run_newbuf(run);   // state for L channel
void      *buf_r = fid_run_newbuf(run);   // state for R channel (separate!)
free(filt);

// In the callback:
double out_l = fn(buf_l, in_l);
double out_r = fn(buf_r, in_r);
```

### Step 4: Change filter at runtime (hot-swap)

Replacing an active filter without RT interruption requires an
atomic pointer swap. This pattern is possible without a mutex when the new filter
is fully allocated before the swap:

```c
// (Simplified — production code needs memory barrier / _Atomic)
struct ActiveFilter {
    FidFunc *fn;
    void    *run;
    void    *buf;
};

struct ActiveFilter *active = create_filter("LpBu4/4000", 44100.0);

// In non-RT thread: allocate new filter:
struct ActiveFilter *next = create_filter("LpBu4/2000", 44100.0);

// Atomic swap (simplified here with volatile — production: C11 _Atomic):
struct ActiveFilter *old = active;
active = next;            // pointer swap

// From this point the RT thread uses next.
// Free old only when certain no RT thread accesses it anymore.
destroy_filter(old);
```

### Step 5: Filter reset (set state to zero)

When a filter state should be reset (e.g. after silence):

```c
fid_run_zapbuf(buf);   // All delay lines set to 0
```

---

## Multiple filters in series (cascade)

firun supports filter cascades directly as multiple spec arguments.
In the C API you cascade manually by passing the output along:

```c
// HP + LP = bandpass (manually cascaded):
FidFilter *hp_filt = fid_design("HpBu2/100", 44100.0, -1.0, -1.0, 0, NULL);
FidFilter *lp_filt = fid_design("LpBu2/3000", 44100.0, -1.0, -1.0, 0, NULL);

FidFunc *hp_fn, *lp_fn;
void *hp_run = fid_run_new(hp_filt, &hp_fn);
void *lp_run = fid_run_new(lp_filt, &lp_fn);

void *hp_buf = fid_run_newbuf(hp_run);
void *lp_buf = fid_run_newbuf(lp_run);

free(hp_filt); free(lp_filt);

// In the callback:
double after_hp  = hp_fn(hp_buf, input);
double after_lp  = lp_fn(lp_buf, after_hp);   // cascade
```

---

## Typical latencies and throughput (RPi 5, AArch64, NEON)

| Filter | Order | Type | Latency/sample |
|--------|-------|------|----------------|
| `LpBu2/1000` | 2 | IIR | ~10 ns |
| `LpBu4/1000` | 4 | IIR | ~20 ns |
| `LpBu8/1000` | 8 | IIR | ~40 ns |
| Boxcar FIR 64 taps | — | FIR | ~15 ns (NEON) |
| Boxcar FIR 512 taps | — | FIR | ~60 ns (NEON) |
| Boxcar FIR 1024 taps (OLA) | — | FIR+FFT | ~8 ns/sample (amortised) |

IIR: O(order) per sample — scales linearly with filter order.
FIR > threshold: O(1) amortised thanks to overlap-save.

---

## Verification: unit test pattern

```c
// Test that DC gain is correct (lowpass → gain ≈ 1.0 at f=0):
void *buf = fid_run_newbuf(run);
fid_run_zapbuf(buf);
for (int i = 0; i < 10000; i++)
    fn(buf, 1.0);   // let it settle
double dc_gain = fn(buf, 1.0);
assert(fabs(dc_gain - 1.0) < 0.001);   // tolerance 0.1%
fid_run_freebuf(buf);
```
