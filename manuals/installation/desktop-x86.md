<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Installationsanleitung — Desktop Linux x86_64

**Plattform:** Ubuntu 22.04+ / Debian Bookworm, x86_64
**GPU:** NVIDIA, AMD oder Intel — vollständiger Vulkan- und OpenCL-Stack verfügbar

## Was ist nach der Installation aktivierbar?

| Feature | Status | Bemerkung |
|---------|--------|-----------|
| `FIDLIB_SIMD=ON` | sofort | SSE2 ist auf x86_64 immer verfügbar |
| `FIDLIB_FFT=ON` | sofort | Overlap-Save + FFTW3 |
| `FIDLIB_VULKAN=ON` | sofort (mit GPU-Treiber) | NVIDIA/AMD/Intel je nach Treiber |
| `FIDLIB_OPENCL=ON` | sofort (mit GPU-Treiber) | ICD-System — Treiber separat installieren |

> **GPU-Treiber-Hinweis:** Das Install-Script installiert Vulkan-Header, ICD-Loader
> und Mesa-Treiber (für AMD/Intel). NVIDIA benötigt den proprietären Treiber separat.
> Ohne GPU-Treiber läuft alles trotzdem — Fallback auf SSE2 + FFT.

---

## Option A — Mit dem Install-Script

```bash
# Vollständig (Basis + GPU):
bash scripts/install-deps-desktop-x86.sh

# Nur Basis ohne GPU-Pakete:
bash scripts/install-deps-desktop-x86.sh --no-gpu
```

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
gcc --version
g++ --version
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
pkg-config --modversion fftw3
```

### Schritt 5: Vulkan-Toolchain

```bash
sudo aptitude install -y \
    libvulkan-dev \
    glslang-tools \
    spirv-tools \
    vulkan-tools \
    mesa-vulkan-drivers     # Mesa RADV (AMD) + ANV (Intel)
```

GPU-Capabilities prüfen:
```bash
vulkaninfo --summary
```

Erwartete Ausgabe (Beispiel AMD):
```
deviceName  = AMD Radeon RX 6600
apiVersion  = 1.3.xxx
```

### Schritt 6: OpenCL-Basis (ICD-Loader + Headers)

```bash
sudo aptitude install -y \
    opencl-headers \
    ocl-icd-opencl-dev \
    ocl-icd-libopencl1 \
    clinfo
```

OpenCL-Devices anzeigen:
```bash
clinfo --list
```

### Schritt 7: GPU-Vendor-spezifische OpenCL-Treiber

Das OpenCL-ICD-System lädt Treiber dynamisch — welche Treiber verfügbar sind,
hängt vom GPU-Hersteller ab:

#### NVIDIA

```bash
# Proprietärer Treiber (empfohlen):
sudo aptitude install -y nvidia-driver nvidia-opencl-icd

# CUDA-OpenCL (falls CUDA bereits installiert):
# libOpenCL.so kommt automatisch mit CUDA — kein extra Paket nötig
```

#### AMD

```bash
# Mesa Clover (Open Source, ältere GPUs):
sudo aptitude install -y mesa-opencl-icd

# ROCm (für neuere RDNA-GPUs, separate Installation):
# https://rocm.docs.amd.com/en/latest/deploy/linux/
```

#### Intel

```bash
# Intel NEO (empfohlen, OpenCL 3.0, alle Gen9+ GPUs):
sudo aptitude install -y intel-opencl-icd
```

---

## cmake-Build nach der Installation

### Vollständiger Build (SSE2 + FFT + Vulkan + OpenCL)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -DFIDLIB_OPENCL=ON \
      -S . -B build_desktop

cmake --build build_desktop -j$(nproc)
ctest --test-dir build_desktop --output-on-failure
```

Erwartete cmake-Ausgabe:
```
-- fidlib SIMD: SSE2 (x86_64)
-- fidlib FFT: Overlap-Save + FFTW3 3.x.x
-- fidlib VULKAN: Vulkan 1.x.xxx, Batch=256, Threshold=256
-- fidlib OPENCL: OpenCL 3.0, Batch=256, Threshold=256
```

### Nur Vulkan (ohne OpenCL)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON \
      -S . -B build_vk

cmake --build build_vk -j$(nproc)
```

### Nur OpenCL (ohne Vulkan)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_OPENCL=ON \
      -S . -B build_ocl

cmake --build build_ocl -j$(nproc)
```

### Benchmark — alle Backends vergleichen

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
      -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

CSV-Ausgabe enthält: Backend, Tap-Anzahl, Samples/Sekunde.

### FP32-Modus (schnellerer SIMD-Pfad)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
      -DFIDLIB_PRECISION=float \
      -S . -B build_fp32

cmake --build build_fp32 -j$(nproc)
```

> **Achtung:** FP32 kann bei IIR-Filtern hoher Ordnung numerisch instabil werden.
> Für FIR-Filter ist FP32 sicher und ~2× schneller als FP64.

---

## Dispatch-Priorität im Betrieb

Wenn mehrere Backends aktiviert sind, wählt `fid_run_new` automatisch:

```
OpenCL  →  Vulkan  →  OLA/FFT  →  NEON/SSE2-Scalar
```

Jede Stufe wird nur genommen wenn:
- Das Backend kompiliert ist (`#ifdef FIDLIB_OPENCL` etc.)
- Die Tap-Anzahl den Threshold überschreitet
- Die Initialisierung erfolgreich war (GPU gefunden)

---

## Problemlösungen

| Problem | Ursache | Lösung |
|---------|---------|--------|
| `vulkaninfo` zeigt keine Devices | GPU-Treiber fehlt | Vendor-Treiber installieren (s.o.) |
| `clinfo` zeigt keine GPU-Devices | OpenCL-ICD fehlt | Vendor-OpenCL-Paket nachinstallieren |
| NVIDIA: `vulkaninfo` findet keine GPU | nvidia-driver nicht aktiv | `sudo nvidia-smi` — Treiber-Status prüfen |
| cmake findet Vulkan nicht | `libvulkan-dev` fehlt | `sudo aptitude install libvulkan-dev` |
| cmake findet OpenCL nicht | `ocl-icd-opencl-dev` fehlt | `sudo aptitude install ocl-icd-opencl-dev opencl-headers` |
| Linker-Fehler `-lOpenCL` | `libOpenCL.so` fehlt | `sudo aptitude install ocl-icd-libopencl1` |
