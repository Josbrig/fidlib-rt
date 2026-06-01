<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Installationsanleitung — Raspberry Pi 3 / Zero 2 W

**Plattform:** Raspberry Pi 3B / 3B+ / Zero 2 W (BCM2837 / BCM2710, aarch64)
**OS:** Raspberry Pi OS Bookworm (Debian 12)
**GPU:** VideoCore IV — kein Vulkan, kein OpenCL

## Was ist nach der Installation aktivierbar?

| Feature | Status | Bemerkung |
|---------|--------|-----------|
| `FIDLIB_SIMD=ON` | sofort | NEON auf AArch64 |
| `FIDLIB_FFT=ON` | sofort | Overlap-Save + FFTW3 |
| `FIDLIB_VULKAN=ON` | nicht verfügbar | VideoCore IV ohne Vulkan |
| `FIDLIB_OPENCL=ON` | nicht verfügbar | VideoCore IV ohne OpenCL |

> **Zero 2 W-Besonderheit:** Nur 512 MB RAM. Große FFT-Blöcke
> (`FIDLIB_FFT_THRESHOLD` > 512) können unter Speicherdruck führen.
> Empfehlung: Threshold auf 256 belassen oder reduzieren.

---

## Option A — Mit dem Install-Script

```bash
chmod +x scripts/install-deps-raspi3.sh
bash scripts/install-deps-raspi3.sh
```

Das Script erkennt automatisch wenig RAM (< 700 MB → Zero 2 W) und gibt einen
entsprechenden Hinweis aus.

---

## Option B — Manuelle Installation

### Schritt 1: aptitude installieren

```bash
sudo apt-get update
sudo apt-get install aptitude
```

### Schritt 2: Build-Basis

```bash
sudo aptitude install -y \
    build-essential \
    cmake \
    git \
    pkg-config

cmake --version   # muss >= 3.16 sein
```

### Schritt 3: SDL2-Build-Abhängigkeiten

```bash
sudo aptitude install -y \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxinerama-dev libxxf86vm-dev \
    libgl1-mesa-dev libasound2-dev libpulse-dev
```

### Schritt 4: FFTW3

```bash
sudo aptitude install -y libfftw3-dev
```

### Schritt 5: Dokumentation (optional)

```bash
sudo aptitude install -y doxygen graphviz
```

---

## cmake-Build nach der Installation

### Standard-Build RPi 3 (NEON + FFT)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -S . -B build_raspi3

cmake --build build_raspi3 -j$(nproc)
ctest --test-dir build_raspi3 --output-on-failure
```

### Zero 2 W — Low-Memory-Build

Der FFT-Threshold bestimmt, ab wie vielen Taps die Overlap-Save-Engine aktiviert
wird. Ein niedrigerer Wert reduziert den Speicherbedarf pro Filter-Instanz:

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_FFT_THRESHOLD=256 \
      -S . -B build_zero2

cmake --build build_zero2 -j2   # -j2 wegen 512 MB RAM
```

### Benchmark

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

> **Hinweis für Zero 2 W:** Der Benchmark läuft auch auf 512 MB RAM durch —
> er misst nur kurze Bursts und allokiert keine riesigen Buffer.

---

## Speicher-Richtwerte

| Konfiguration | Speicher pro Filter-Instanz |
|---|---|
| Direktfaltung 64 Taps (FP64) | ~1 KB |
| Direktfaltung 512 Taps (FP64) | ~4 KB |
| OLA/FFT 1024 Taps, N=2048 | ~128 KB (FFT-Buffer) |
| OLA/FFT 4096 Taps, N=8192 | ~512 KB |

Für Zero 2 W: Threshold ≤ 256 und Tap-Anzahl ≤ 1024 empfohlen.

---

## Problemlösungen

| Problem | Ursache | Lösung |
|---------|---------|--------|
| Build bricht mit OOM ab | Zu wenig RAM beim Linken | `cmake --build ... -j2` statt `-j$(nproc)` |
| `cmake --version` < 3.16 | Alte cmake-Version | `sudo aptitude install cmake` (Bookworm hat 3.25+) |
| `FIDLIB_VULKAN` wird ignoriert | V3D IV ohne Compute | Erwartet — kein Handlungsbedarf |
| fftw3 nicht gefunden | `libfftw3-dev` fehlt | `sudo aptitude install libfftw3-dev` |
