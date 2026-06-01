<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Installationsanleitung — NVIDIA Jetson (JetPack)

**Plattform:** NVIDIA Jetson Nano / Xavier NX / Orin NX / AGX Orin (aarch64)
**OS:** Ubuntu 20.04 / 22.04 via JetPack 5.x / 6.x
**GPU:** NVIDIA GPU (Tegra integriert) + CUDA, Vulkan, OpenCL via JetPack-Stack

## Was ist nach der Installation aktivierbar?

| Feature | Status | Bemerkung |
|---------|--------|-----------|
| `FIDLIB_SIMD=ON` | sofort | NEON auf AArch64 |
| `FIDLIB_FFT=ON` | sofort | Overlap-Save + FFTW3 |
| `FIDLIB_VULKAN=ON` | sofort (JetPack 5+) | NVIDIA Vulkan via JetPack-Treiber |
| `FIDLIB_OPENCL=ON` | sofort (JetPack) | OpenCL via CUDA ICD, echte GPU-Devices |

> **Voraussetzung:** JetPack muss bereits installiert sein. JetPack bringt
> CUDA, cuDNN, libvulkan und libOpenCL automatisch mit.
> Prüfen: `dpkg -l | grep nvidia-jetpack`

---

## Option A — Mit dem Install-Script

```bash
chmod +x scripts/install-deps-jetson.sh

# Vollständig (Basis + GPU-Toolchain):
bash scripts/install-deps-jetson.sh

# Nur Basis ohne GPU-Tools:
bash scripts/install-deps-jetson.sh --no-gpu
```

> Das Script erkennt automatisch ob `aptitude` verfügbar ist; falls nicht,
> fällt es auf `apt-get` zurück.

---

## Option B — Manuelle Installation

### Schritt 0: JetPack-Status prüfen

```bash
dpkg -l nvidia-jetpack
# Erwartet: Status "ii" (installed) mit Version 5.x oder 6.x

# CUDA prüfen:
nvcc --version
# Erwartet: release 11.x oder 12.x

# Vulkan-Treiber prüfen:
dpkg -l libvulkan1
ls /usr/lib/aarch64-linux-gnu/libvulkan.so* 2>/dev/null || echo "Vulkan nicht gefunden"
```

Falls JetPack noch nicht installiert ist:
→ [NVIDIA JetPack SDK Manager](https://developer.nvidia.com/embedded/jetpack)

### Schritt 1: aptitude installieren (empfohlen)

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

### Schritt 5: Vulkan Shader-Compiler und Tools

JetPack liefert `libvulkan1` und `libvulkan-dev` bereits mit.
Nur der GLSL-Shader-Compiler muss aus dem Ubuntu-Repository installiert werden:

```bash
sudo aptitude install -y \
    glslang-tools \
    spirv-tools \
    vulkan-tools

# Falls libvulkan-dev fehlt (selten):
sudo aptitude install -y libvulkan-dev libvulkan1
```

GPU prüfen:
```bash
vulkaninfo --summary
# Erwartet: deviceName = NVIDIA Tegra ..., apiVersion = 1.x
```

### Schritt 6: OpenCL-Headers und ICD-Loader

JetPack liefert `libOpenCL.so` via CUDA — nur Headers und ICD-Dev-Paket nötig:

```bash
sudo aptitude install -y \
    opencl-headers \
    ocl-icd-opencl-dev \
    ocl-icd-libopencl1 \
    clinfo
```

GPU-Devices prüfen:
```bash
clinfo --list
# Erwartet: NVIDIA GPU als OpenCL-Device
```

### Schritt 7: Dokumentation (optional)

```bash
sudo aptitude install -y doxygen graphviz
```

---

## cmake-Build nach der Installation

### Vollständiger Jetson-Build (NEON + FFT + Vulkan + OpenCL)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -DFIDLIB_OPENCL=ON \
      -S . -B build_jetson

cmake --build build_jetson -j$(nproc)
ctest --test-dir build_jetson --output-on-failure
```

### Benchmark — NEON vs. OLA/FFT vs. Vulkan vs. OpenCL

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
      -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

### Ohne GPU (Fallback-Build — falls JetPack-GPU-Stack fehlt)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
      -S . -B build_cpu

cmake --build build_cpu -j$(nproc)
```

---

## Jetson-Modell-spezifische Hinweise

| Modell | RAM | Empfohlener FFT-Threshold | Besonderheit |
|--------|-----|--------------------------|--------------|
| Nano (4 GB) | 4 GB | 512 (Standard) | Ältere Maxwell-GPU, OpenCL 1.2 |
| Xavier NX (8/16 GB) | 8–16 GB | 512 | Volta-GPU, OpenCL 3.0 |
| Orin NX (8/16 GB) | 8–16 GB | 512 | Ampere-GPU, Vulkan 1.3 |
| AGX Orin (32/64 GB) | 32–64 GB | 256–512 | Stärkste Einzel-GPU im Embedded-Bereich |

---

## OpenCL vs. Vulkan auf Jetson

Beide Backends sind auf Jetson vollständig funktionsfähig. Für `fidlib`:

- **Vulkan Compute** nutzt `fir_dot.comp` (SPIR-V, FP32, 64 Threads/Workgroup)
- **OpenCL** nutzt `fir_dot.cl` (C99-Kernel, FP32, 64 Work-Items/Workgroup)

In der Dispatch-Priorität steht OpenCL vor Vulkan — wenn beide kompiliert sind,
wird OpenCL bevorzugt (erster erfolgreicher Init gewinnt). Zum Testen nur eines
Backends: das andere beim cmake-Configure deaktivieren.

---

## Problemlösungen

| Problem | Ursache | Lösung |
|---------|---------|--------|
| `nvcc` nicht gefunden | CUDA PATH fehlt | `export PATH=$PATH:/usr/local/cuda/bin` in `~/.bashrc` |
| `clinfo` zeigt 0 Devices | OpenCL-ICD nicht registriert | `ls /etc/OpenCL/vendors/` — NVIDIA-ICD prüfen |
| cmake findet Vulkan nicht | `libvulkan-dev` fehlt | `sudo aptitude install libvulkan-dev` |
| `glslangValidator` fehlt | `glslang-tools` fehlt | `sudo aptitude install glslang-tools` |
| cmake findet OpenCL nicht | Headers fehlen | `sudo aptitude install opencl-headers ocl-icd-opencl-dev` |
| Linker: `-lOpenCL` nicht gefunden | `libOpenCL.so` Symlink fehlt | `sudo aptitude install ocl-icd-libopencl1` |
| `aptitude` fehlt (Ubuntu-Minimal) | Nicht in Minimal-Image | `sudo apt-get install aptitude` oder `apt-get` verwenden |
