<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Solution: Signal Processing in the Shell with firun

## The Problem

Measurement data is to be filtered on the command line — without writing a custom
C program. The data is present as ASCII text, binary PCM, or WAV.
firun reads from `stdin` and writes to `stdout` and can be embedded in any
shell pipeline.

**Use cases:**
- Filter a WAV file and save as a new WAV
- Smooth CSV measurement data (moving average / IIR lowpass)
- Visualise the impulse and step response of a filter
- Filter multi-channel PCM streams in parallel
- Filter cascades (multiple specs in sequence)

---

## Which project tools help

- **`firun`** — CLI tool: `firun [options] <rate> <format> <FilterSpec...>`
- **fidlib Fispec DSL** — filter type, order, frequency as a compact string
- **Format codes** — ASCII, 16-bit PCM, 32-bit float, multi-channel
- **Synthetic signals** — `%I` (impulse), `%S` (step) as input without a file

---

## Step 1: Build firun

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
      -DBUILD_TOOLS=ON \
      -S . -B build

cmake --build build -j$(nproc)
# Result: build/bin/firun
```

Quick test:
```bash
build/bin/firun -d 5 44100 %I LpBu4/1000
# Outputs 5 ASCII values of the impulse response
```

---

## Format codes overview

| Code | Meaning |
|------|---------|
| `a` | ASCII floating-point (one value per line) |
| `s` | Signed 16-bit little-endian (PCM audio) |
| `S` | Signed 16-bit big-endian |
| `w` | Unsigned 16-bit little-endian |
| `f` | 32-bit float (machine order) |
| `d` | 64-bit double (machine order) |
| `b` | Unsigned 8-bit |
| `c` | Signed 8-bit |
| `_` | Dummy byte (skip) |
| `%I` | Synthetic impulse (stdin ignored) |
| `%S` | Synthetic step (stdin ignored) |

Multi-channel: format code + number = samples per frame, e.g. `s2` = stereo S16LE.

---

## Use case 1: Analyse filter properties

### Output impulse response

```bash
# 500 samples of the impulse response of a 4-pole Butterworth lowpass:
build/bin/firun -d 500 44100 %I LpBu4/4000
```

### Step response (check DC gain)

```bash
# Step response: must converge to 1.0 for a lowpass:
build/bin/firun -d 1000 44100 %S LpBu4/4000 | tail -3
```

### Visualise frequency response (with gnuplot)

```bash
build/bin/firun -d 8192 44100 %I LpBu4/4000 > impulse.dat

gnuplot << 'EOF'
set terminal qt
N = 8192
set xrange [0:22050]
set xlabel "Frequenz [Hz]"
set ylabel "Magnitude [dB]"
set title "Frequenzantwort LpBu4/4000 @ 44100 Hz"
fft_mag(filename) = system(sprintf("python3 -c \"\
import numpy as np; \
x=np.loadtxt('%s'); \
f=np.fft.rfftfreq(len(x),1/44100.0); \
H=20*np.log10(abs(np.fft.rfft(x))+1e-12); \
[print(f[i],H[i]) for i in range(len(f))]\"", filename))
# simpler: precompute data with Python, then plot
EOF
```

---

## Use case 2: Filter a WAV file

Using `sox` as a conversion wrapper:

```bash
# Mono WAV → lowpass 4000 Hz → new WAV:
sox eingabe.wav -t raw -e signed -b 16 -r 44100 - \
    | build/bin/firun 44100 s LpBu4/4000 \
    | sox -t raw -e signed -b 16 -r 44100 -c 1 - ausgabe.wav
```

```bash
# Stereo WAV (interleaved L+R) → bandpass 100-3000 Hz on both channels:
sox eingabe_stereo.wav -t raw -e signed -b 16 -r 44100 - \
    | build/bin/firun 44100 s2 BpBu4/100-3000 BpBu4/100-3000 \
    | sox -t raw -e signed -b 16 -r 44100 -c 2 - ausgabe_stereo.wav
```

---

## Use case 3: Smooth CSV measurement data

Measurement data as ASCII (one value per line, e.g. from a sensor):

```bash
# Smooth measurement data: IIR lowpass at 10 Hz, sensor rate 100 Hz:
cat sensor_data.csv | build/bin/firun 100 a LpBu2/10 > smoothed.csv
```

For a moving average over N values: boxcar FIR of length N.
Since fidlib has no direct boxcar CLI syntax, use the IIR approximation:

```bash
# Very gentle IIR LP as smoothing filter (low cutoff):
cat sensor.csv | build/bin/firun 100 a LpBu1/1 > smoothed.csv
```

---

## Use case 4: Filter cascade

Multiple filter specs in sequence = serial cascade:

```bash
# DC removal → bandpass → notch 50 Hz (EEG preprocessing):
cat eeg.txt | \
    build/bin/firun 250 a \
        HpBu2/0.5 \
        BpBu4/0.5-45 \
        BsBu2/49-51 \
    > eeg_filtered.txt
```

```bash
# Two notch filters: 50 Hz and 100 Hz harmonic:
cat signal.txt | \
    build/bin/firun 44100 a \
        BsBu2/49-51 \
        BsBu2/99-101 \
    > denoised.txt
```

---

## Use case 5: Limit output duration

```bash
# Filter only the first 2 seconds (at 44100 Hz = 88200 samples):
cat long_audio.raw | \
    build/bin/firun -d 2s 44100 s LpBu4/4000 > short_filtered.raw

# Or exactly N samples:
build/bin/firun -d 44100 44100 %I LpBu4/4000 > one_second_impulse.dat
```

The `-d` option accepts: `Ns` (seconds), `Nm` (minutes), `N` (samples).

---

## Use case 6: Live monitoring with aplay (Linux)

```bash
# Microphone → highpass (DC removal) → speaker in real time:
arecord -f S16_LE -r 44100 -c 1 | \
    build/bin/firun 44100 s HpBu2/20 | \
    aplay -f S16_LE -r 44100 -c 1
```

```bash
# Bandpass radio demodulation (SDR):
# rtl_sdr → FM demodulator → lowpass → aplay (simplified):
rtl_sdr -f 100e6 -s 250000 -g 30 - 2>/dev/null | \
    # ... FM demodulation here ... | \
    build/bin/firun 48000 s LpBu4/15000 | \
    aplay -f S16_LE -r 48000 -c 1
```

---

## Fispec quick reference

| Type | Code | Example | Description |
|------|------|---------|-------------|
| Butterworth LP | `LpBu` | `LpBu4/1000` | Lowpass 4th order, -3 dB at 1 kHz |
| Butterworth HP | `HpBu` | `HpBu4/200` | Highpass |
| Butterworth BP | `BpBu` | `BpBu4/300-3000` | Bandpass |
| Butterworth BS | `BsBu` | `BsBu2/49-51` | Band-stop (notch) |
| Bessel LP | `LpBe` | `LpBe4/1000` | Lowpass, linear phase response |
| Chebyshev LP | `LpCh` | `LpCh4/-1/1000` | Lowpass, 1 dB ripple, steeper rolloff |
| Biquad Peaking | `PkBq` | `PkBq/1000/1/6` | Parametric EQ: +6 dB at 1 kHz, Q=1 |
| Biquad Low-Shelf | `LsBq` | `LsBq/200/6` | Bass boost +6 dB below 200 Hz |
| Allpass | `ApBu` | `ApBu2/1000` | Phase rotation only |
| Resonator | `BpRe` | `BpRe/1000/10` | Narrow bandpass, Q=10 |

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|---------|
| Output stays empty | stdin delivers nothing | Set `-d N` or use `%I`/`%S` |
| Wrong amplitude | Wrong format (s vs a) | Match format to input data |
| Noise/artefacts | Wrong channel count | Check format digit (`s2` for stereo) |
| `firun` not found | `BUILD_TOOLS` not enabled | Rerun cmake with `-DBUILD_TOOLS=ON` |
| Very slow output | ASCII format (a) | Use binary format (`s`, `f`) for performance |
