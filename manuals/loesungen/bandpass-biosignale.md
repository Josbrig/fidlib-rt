<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Solution: Bandpass Filtering for Bio- and Measurement Signals

## The Problem

An EEG device (or similar biosignal sensor) delivers data at a 250 Hz sample rate.
The frequency band of interest lies between 8 Hz and 30 Hz (alpha + beta waves).
Mains hum at 50 Hz and DC offset (drift) should be suppressed.

**Requirements:**
- Passband: 8–30 Hz
- Stopband attenuation below 5 Hz and above 45 Hz
- Linear phase response in the passband desired (Bessel) or steepest rolloff (Butterworth)
- Real-time capable at 250 Hz sample rate (not CPU-critical)
- Multi-channel capable: 8 EEG channels in parallel

---

## Which project tools help

- **`BpBu4/8-30`** — Butterworth bandpass 4th order, passband 8–30 Hz
- **`BpBe4/8-30`** — Bessel bandpass (linear phase response, better for biosignals)
- **fidlib C API** — one `buf` instance per channel
- **firun** — multi-channel processing with multiple filter specs

---

## Step by Step — Variant A: Command line with firun

### Step 1: Understand the filter specification

```
BpBu4/8-30
│  │ │  └── Upper corner frequency: 30 Hz
│  │ └──── Lower corner frequency: 8 Hz
│  └──── Bu = Butterworth
└──── Bp = Bandpass
```

A 4th-order Butterworth bandpass with two-sided corner frequencies has
8 poles internally (2 per order × 2 sides). The transfer function is the product of two
Butterworth lowpasses after bilinear transformation.

### Step 2: Single-channel test with synthetic input

```bash
# Step response: must decay to 0 in the passband (no DC component):
build/bin/firun -d 500 250 %S BpBu4/8-30 | tail -10

# Sine response at 15 Hz (in passband — must be nearly unity gain):
# (Not directly testable in firun; use C program or Python)
```

### Step 3: Filter raw data from one EEG channel

ASCII format (one value per line, voltage values as float):

```bash
cat kanal1.txt | build/bin/firun 250 a BpBu4/8-30 > kanal1_gefiltert.txt
```

### Step 4: 8 channels in parallel (interleaved format)

Assumption: the raw data is stored as 8× signed 16-bit per frame (interleaved):

```bash
cat eeg_8kanal.raw | \
    build/bin/firun 250 s8 \
        BpBu4/8-30 BpBu4/8-30 BpBu4/8-30 BpBu4/8-30 \
        BpBu4/8-30 BpBu4/8-30 BpBu4/8-30 BpBu4/8-30 \
    > eeg_8kanal_gefiltert.raw
```

The format `s8` means: 8 signed 16-bit values per frame.
Each filter spec filters exactly one channel.

### Step 5: Filter cascade (highpass + lowpass = bandpass)

Alternatively a bandpass can be realised as a cascade of HP + LP.
This is numerically more stable at extreme frequency ratios:

```bash
# Equivalent to BpBu2/8-30, but explicitly as a cascade:
cat signal.txt | \
    build/bin/firun 250 a HpBu2/8 LpBu2/30 > gefiltert.txt
```

---

## Step by Step — Variant B: C API for 8 channels

### Alloc phase (once at startup)

```c
#include <fidlib/fidlib.h>
#include <stdlib.h>

#define N_CHANNELS 8

FidFilter *filt = fid_design("BpBu4/8-30", 250.0, -1.0, -1.0, 0, NULL);
FidFunc   *step_fn;
void      *run = fid_run_new(filt, &step_fn);

// One state buffer per channel — state is completely separate
void *buf[N_CHANNELS];
for (int ch = 0; ch < N_CHANNELS; ch++)
    buf[ch] = fid_run_newbuf(run);

free(filt);
```

### Run phase (for each frame with 8 samples)

```c
void process_frame(double frame_in[8], double frame_out[8]) {
    for (int ch = 0; ch < N_CHANNELS; ch++)
        frame_out[ch] = step_fn(buf[ch], frame_in[ch]);
}
```

Each channel has its own state buffer — `run` (coefficients) is shared,
`buf[ch]` holds the filter state (delay lines) per channel. This is
memory-efficient and thread-safe when each thread gets its own `buf` instances.

### Free phase

```c
for (int ch = 0; ch < N_CHANNELS; ch++)
    fid_run_freebuf(buf[ch]);
fid_run_free(run);
```

---

## Filter variants for biosignals

| Requirement | Fispec | Order | Comment |
|---|---|---|---|
| Broad alpha+beta | `BpBu4/8-30` | 4 | Standard, steep rolloff |
| Linear phase response | `BpBe4/8-30` | 4 | Bessel — no phase distortion |
| Alpha only (8–13 Hz) | `BpBu4/8-13` | 4 | Narrower band |
| Beta only (13–30 Hz) | `BpBu4/13-30` | 4 | |
| Delta band (0.5–4 Hz) | `BpBu4/0.5-4` | 4 | Note: close to Nyquist lower bound |
| Suppress mains hum | `BsBu2/49-51` | 2 | Band-stop / notch 50 Hz |
| DC removal | `HpBu2/0.5` | 2 | Highpass removes DC drift |

### Typical cascade for EEG preprocessing

```bash
# DC removal → bandpass → notch:
cat kanal.txt | \
    build/bin/firun 250 a \
        HpBu2/0.5 \
        BpBu4/0.5-45 \
        BsBu2/49-51 \
    > kanal_vorverarbeitet.txt
```

In the C API: three separate `fid_run_new` objects, three `buf` instances,
output of the first is input to the second.

---

## Analyse frequency response (without external tool)

```bash
# Output impulse response and compute FFT with Python/gnuplot:
build/bin/firun -d 2048 250 %I BpBu4/8-30 > impulse_bp.dat

# With Python (numpy/scipy):
python3 - << 'EOF'
import numpy as np
import matplotlib.pyplot as plt
x = np.loadtxt('impulse_bp.dat')
f = np.fft.rfftfreq(len(x), 1/250)
H = np.abs(np.fft.rfft(x))
plt.plot(f, 20*np.log10(H + 1e-12))
plt.xlabel('Frequenz [Hz]')
plt.ylabel('Magnitude [dB]')
plt.title('BpBu4 8-30 Hz @ 250 Hz')
plt.grid(True)
plt.show()
EOF
```

---

## Verification

```bash
# DC suppression: step response must approach 0 over time:
build/bin/firun -d 1000 250 %S BpBu4/8-30 | tail -5
# Expected: values close to 0.0

# Impulse response: must decay after a finite time:
build/bin/firun -d 500 250 %I BpBu4/8-30 | awk '{sum+=($1<0?-$1:$1)} END{print "Energie:",sum}'
```
