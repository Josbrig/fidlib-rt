<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Solution: Low-Pass Noise Suppression in Audio Signals

## The Problem

An audio signal (sample rate 44100 Hz) contains high-frequency noise above
4000 Hz — typical with switching noise, HF interference, or cheap A/D converters.
The noise should be removed without affecting the useful signal (< 3500 Hz).

**Requirements:**
- Cutoff: 4000 Hz
- Sufficient stopband attenuation in the noise band (> 3 dB at 4 kHz, > 20 dB at 8 kHz)
- Causal filter (real-time capable, no look-ahead)
- Minimal effort: no recompilation when the cutoff changes

---

## Which project tools help

- **fidlib** — designs and runs the filter from a runtime specification
- **firun** — applies the filter to raw data on the command line
- **`LpBu4/4000`** — Butterworth lowpass 4th order, cutoff 4000 Hz
- **`FIDLIB_SIMD=ON`** — NEON/SSE2 acceleration for real-time operation

---

## Step by Step — Variant A: Command line with firun

### Step 1: Build the project (if not already done)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -S . -B build

cmake --build build -j$(nproc)
```

This produces `build/bin/firun`.

### Step 2: Understand the filter specification

```
LpBu4/4000
│  │ │  └── Cutoff frequency: 4000 Hz
│  │ └──── Order: 4 (two chained biquads = 4 poles)
│  └──── Bu = Butterworth (maximally flat passband)
└──── Lp = Lowpass
```

Butterworth 4th order at 44100 Hz: -3 dB at 4000 Hz, -80 dB at ~16 kHz.

### Step 3: Test the impulse response (without real audio data)

```bash
build/bin/firun -d 100 44100 %I LpBu4/4000
```

- `%I` — synthetic impulse as input (1, 0, 0, 0, ...)
- `-d 100` — output 100 samples
- Output is ASCII, one value per line

Expected: first value ≈ 0.0003 (filter gain), then ringing impulse response,
decaying to 0.

### Step 4: Filter raw 16-bit audio data

```bash
# Signed 16-bit PCM (little-endian), 1 channel, 44100 Hz:
cat eingabe.raw | build/bin/firun 44100 s LpBu4/4000 > ausgabe.raw
```

### Step 5: Using sox as pre/post processor

```bash
# WAV → raw PCM → filter → raw PCM → WAV:
sox eingabe.wav -t raw -e signed -b 16 -r 44100 - | \
    build/bin/firun 44100 s LpBu4/4000 | \
    sox -t raw -e signed -b 16 -r 44100 -c 1 - ausgabe.wav
```

### Step 6: Stereo signal (2 channels, interleaved)

With stereo each frame contains alternating L+R. firun filters each channel
with its own filter instance:

```bash
# Two identical filter specs → L and R are each filtered with LpBu4/4000:
cat stereo.raw | build/bin/firun 44100 s2 LpBu4/4000 LpBu4/4000 > gefiltert.raw
```

The format `s2` means: 2 signed 16-bit values per frame (L, R).

---

## Step by Step — Variant B: C API in your own program

### Step 1: Alloc phase (once, before the RT loop)

```c
#include <fidlib/fidlib.h>
#include <stdlib.h>

// Design filter: Butterworth LP 4th order, cutoff 4000 Hz, rate 44100 Hz
FidFilter *filt = fid_design("LpBu4/4000", 44100.0, -1.0, -1.0, 0, NULL);

// Create run object (selects best backend: NEON/FFT/Vulkan)
FidFunc *step_fn;
void    *run = fid_run_new(filt, &step_fn);

// Filter state buffer (one per channel/instance)
void *buf = fid_run_newbuf(run);

free(filt);   // FidFilter no longer needed — coefficients are in run
```

### Step 2: Run phase (RT-safe, no malloc)

```c
// For each incoming sample:
double output = step_fn(buf, input_sample);
```

`step_fn` is a direct function pointer — no virtual dispatch,
no heap. Safe in audio callbacks (JACK, ALSA, PortAudio).

### Step 3: Free phase (on shutdown)

```c
fid_run_freebuf(buf);
fid_run_free(run);
```

### Complete minimal example

```c
#include <fidlib/fidlib.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FidFilter *filt = fid_design("LpBu4/4000", 44100.0, -1.0, -1.0, 0, NULL);
    FidFunc   *fn;
    void      *run = fid_run_new(filt, &fn);
    void      *buf = fid_run_newbuf(run);
    free(filt);

    // Process impulse and output first 20 samples
    printf("%.8f\n", fn(buf, 1.0));
    for (int i = 0; i < 19; i++)
        printf("%.8f\n", fn(buf, 0.0));

    fid_run_freebuf(buf);
    fid_run_free(run);
    return 0;
}
```

Compile:
```bash
gcc -o lp_test lp_test.c \
    -I build/fidlib -L build/fidlib -lfidlib -lm \
    -O2
```

---

## Filter variants for similar requirements

| Requirement | Fispec | Comment |
|---|---|---|
| Softer transition | `LpBu6/4000` | Order 6, steeper rolloff |
| Phase error not important | `LpBe4/4000` | Bessel: linear phase response |
| Maximum stopband attenuation | `LpCh4/-1/4000` | Chebyshev: 1 dB ripple, steep rolloff |
| Higher cutoff | `LpBu4/8000` | Cutoff 8 kHz at 44100 Hz |
| Low sample rate (e.g. 8000 Hz) | `LpBu4/800` | Cutoff = 10% of Nyquist frequency |

---

## Verification

```bash
# Output step response and check last value (→ must approach 1.0):
build/bin/firun -d 200 44100 %S LpBu4/4000 | tail -5

# Plot impulse response (with gnuplot):
build/bin/firun -d 300 44100 %I LpBu4/4000 > impulse.dat
gnuplot -e "plot 'impulse.dat' with lines title 'LpBu4 @ 44100 Hz'; pause -1"
```
