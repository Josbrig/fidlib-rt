<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Installationsanleitung — Raspberry Pi 4

**Plattform:** Raspberry Pi 4 Model B (BCM2711, aarch64)
**OS:** Raspberry Pi OS Bookworm (Debian 12)
**GPU:** VideoCore VI (V3D 4.2) — kein Vulkan Compute, kein OpenCL

## Was ist nach der Installation aktivierbar?

| Feature | Status | Bemerkung |
|---------|--------|-----------|
| `FIDLIB_SIMD=ON` | sofort | NEON ist auf AArch64 immer verfügbar |
| `FIDLIB_FFT=ON` | sofort | Overlap-Save + FFTW3-Backend |
| `FIDLIB_VULKAN=ON` | nicht sinnvoll | V3D 4.2 hat keinen Vulkan-Compute-Support |
| `FIDLIB_OPENCL=ON` | nicht verfügbar | Kein funktionierender OpenCL-Treiber für V3D 4.2 |

> **GPU-Compute-Einschränkung:** Der VideoCore VI (V3D 4.2) im BCM2711 unterstützt
> zwar Vulkan für Grafik (Mesa V3DV ab Mesa 21), aber **Vulkan Compute Pipelines**
> (`VkComputePipeline`) sind nicht verfügbar. Das cmake-System setzt `FIDLIB_VULKAN`
> automatisch auf `OFF`, wenn der Treiber keine Compute-Fähigkeiten meldet.
> Für GPU-Compute: RPi 5 (VideoCore VII) oder Desktop-PC verwenden.

---

## Option A — Mit dem Install-Script

```bash
chmod +x scripts/install-deps-raspi4.sh
bash scripts/install-deps-raspi4.sh
```

Das Script prüft BCM2711-SoC, zeigt Pakete, simuliert und fragt nach Bestätigung.

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
pkg-config --modversion fftw3   # Versions-Check
```

### Schritt 5: Dokumentation (optional)

```bash
sudo aptitude install -y doxygen graphviz
```

> **Kein Schritt für Vulkan/OpenCL** — diese Features sind auf RPi 4 nicht verfügbar.

---

## cmake-Build nach der Installation

### Standard-Build (NEON + FFT — empfohlene Konfiguration)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -S . -B build_raspi4

cmake --build build_raspi4 -j$(nproc)
ctest --test-dir build_raspi4 --output-on-failure
```

Erwartete cmake-Ausgabe:
```
-- fidlib SIMD: NEON (aarch64)
-- fidlib FFT: Overlap-Save + FFTW3 3.x.x
```

### Debug-Build

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -S . -B build_debug

cmake --build build_debug -j$(nproc)
ctest --test-dir build_debug --output-on-failure
```

### Benchmark (NEON vs. OLA/FFT)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

### Was passiert wenn man `FIDLIB_VULKAN=ON` trotzdem setzt?

cmake erkennt automatisch, dass kein geeigneter Vulkan-Compute-Treiber vorhanden ist,
und deaktiviert das Feature mit einer Warnung:

```
-- WARNING: fidlib VULKAN: Vulkan nicht gefunden — FIDLIB_VULKAN deaktiviert
```

Es gibt keinen Build-Fehler — das System fällt sicher auf NEON/FFT zurück.

---

## Leistungserwartung RPi 4

Mit NEON + FFTW3-FFT:
- Direkte Faltung (< 512 Taps): ~20–30 Millionen Samples/s (NEON FP64)
- Overlap-Save FFT (≥ 512 Taps): deutlich schneller als O(N²)-Direktfaltung
- FP32-Modus (`-DFIDLIB_PRECISION=float`): ~2× schneller als FP64 bei FIR

---

## Problemlösungen

| Problem | Ursache | Lösung |
|---------|---------|--------|
| `cmake` findet `fftw3` nicht | `libfftw3-dev` fehlt | `sudo aptitude install libfftw3-dev` |
| SDL2-Configure-Fehler | X11/Audio-Header fehlen | Schritt 3 wiederholen |
| `aptitude` fehlt | Nicht vorinstalliert | `sudo apt-get install aptitude` |
| cmake meldet `FIDLIB_VULKAN` deaktiviert | V3D 4.2 ohne Compute | Erwartet — kein Handlungsbedarf |
