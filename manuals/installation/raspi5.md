<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Installationsanleitung — Raspberry Pi 5

**Plattform:** Raspberry Pi 5 (BCM2712, aarch64)
**OS:** Raspberry Pi OS Bookworm (Debian 12)
**GPU:** VideoCore VII (V3D 7.1) — Vulkan 1.2 nativ via Mesa V3DV

## Was ist nach der Installation aktivierbar?

| Feature | Status | Bemerkung |
|---------|--------|-----------|
| `FIDLIB_SIMD=ON` | sofort | NEON ist auf AArch64 immer verfügbar |
| `FIDLIB_FFT=ON` | sofort | Overlap-Save + FFTW3-Backend |
| `FIDLIB_VULKAN=ON` | sofort | Vulkan 1.2 Compute via V3D 7.1 (FP32) |
| `FIDLIB_OPENCL=ON` | nur nach Mesa-Rusticl-Eigenbau | Clover liefert 0 GPU-Devices auf RPi5 |

---

## Option A — Mit dem Install-Script

Das Script [`scripts/install-deps-raspi5.sh`](../../scripts/install-deps-raspi5.sh)
erledigt alle Schritte automatisch mit Bestätigungsabfrage.

```bash
# Einmalig ausführbar machen (nur beim ersten Mal nötig):
chmod +x scripts/install-deps-raspi5.sh

# Starten:
bash scripts/install-deps-raspi5.sh
```

Das Script:
1. Prüft ob `aarch64` und BCM2712-SoC vorhanden sind
2. Prüft ob `aptitude` installiert ist
3. Zeigt alle Pakete die installiert werden
4. Führt `aptitude install --simulate` durch (Trocken-Lauf ohne Änderungen)
5. Fragt nach Bestätigung (`j` = ja, alles andere = Abbruch)
6. Installiert alle Pakete
7. Zeigt installierte Versionen und cmake-Beispiele

---

## Option B — Manuelle Installation

### Schritt 1: aptitude installieren (falls nicht vorhanden)

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
```

Versionen prüfen:
```bash
cmake --version   # muss >= 3.16 sein
gcc --version
```

### Schritt 3: SDL2-Build-Abhängigkeiten

SDL2 wird via `ExternalProject_Add` aus dem Quellcode gebaut —
System-SDL2 ist nicht nötig. Aber die X11- und Audio-Header müssen vorhanden sein,
damit der SDL2-cmake-Configure-Schritt durchläuft:

```bash
sudo aptitude install -y \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxinerama-dev libxxf86vm-dev \
    libgl1-mesa-dev libasound2-dev libpulse-dev
```

### Schritt 4: FFTW3 (für Overlap-Save FFT-Backend)

```bash
sudo aptitude install -y libfftw3-dev

# Prüfen:
pkg-config --modversion fftw3
```

Ohne FFTW3 fällt das FFT-Backend automatisch auf den eingebauten Radix-2-Algorithmus
zurück — das funktioniert, FFTW3 ist aber ~2-3× schneller bei großen FFT-Blöcken.

### Schritt 5: Vulkan-Toolchain

```bash
sudo aptitude install -y \
    libvulkan-dev \
    glslang-tools \
    spirv-tools \
    vulkan-tools
```

- **`libvulkan-dev`** — Vulkan-Header + ICD-Loader (wird von Mesa V3DV bedient)
- **`glslang-tools`** — `glslangValidator`: kompiliert den GLSL Compute Shader
  `fidlib/fir_dot.comp` nach SPIR-V (passiert automatisch beim cmake-Build)
- **`spirv-tools`** — `spirv-dis` / `spirv-val` für Diagnose (optional)
- **`vulkan-tools`** — `vulkaninfo` zum Prüfen der GPU-Fähigkeiten

GPU prüfen:
```bash
vulkaninfo --summary
# Erwartet: deviceName = V3D 7.1, apiVersion = 1.2.x
```

### Schritt 6: OpenCL (optional — nur für Diagnose nutzbar)

```bash
sudo aptitude install -y \
    opencl-headers \
    ocl-icd-opencl-dev \
    mesa-opencl-icd \
    clinfo
```

> **Hinweis:** `mesa-opencl-icd` (Clover) meldet auf RPi5 **0 GPU-Devices**.
> OpenCL-GPU-Support erfordert einen selbst gebauten Mesa mit Rusticl-Backend —
> das ist ein mehrstündiger Build-Prozess. Vulkan ist der empfohlene GPU-Pfad.

OpenCL-Devices anzeigen:
```bash
clinfo --list
# Auf Standard-RPi5: zeigt nur CPU-Devices oder leer
```

### Schritt 7: Dokumentations-Tools (optional)

```bash
sudo aptitude install -y doxygen graphviz
```

---

## cmake-Build nach der Installation

### Standard-Build (NEON + FFT + Vulkan)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -S . -B build_raspi5

cmake --build build_raspi5 -j$(nproc)
ctest --test-dir build_raspi5 --output-on-failure
```

Erwartete cmake-Ausgabe (relevante Zeilen):
```
-- fidlib SIMD: NEON (aarch64)
-- fidlib FFT: Overlap-Save + FFTW3 3.x.x
-- fidlib VULKAN: Vulkan 1.2.xxx, Batch=256, Threshold=256
```

### Debug-Build (mit Sanitizern für Entwicklung)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -S . -B build_debug

cmake --build build_debug -j$(nproc)
ctest --test-dir build_debug --output-on-failure
```

### Benchmark (alle Backends vergleichen)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

### Mit OpenCL (nach Rusticl-Mesa-Eigenbau)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
      -S . -B build_full

cmake --build build_full -j$(nproc)
```

---

## Problemlösungen

| Problem | Ursache | Lösung |
|---------|---------|--------|
| `cmake` findet Vulkan nicht | `libvulkan-dev` fehlt | `sudo aptitude install libvulkan-dev` |
| `glslangValidator` fehlt | `glslang-tools` nicht installiert | `sudo aptitude install glslang-tools` |
| `FIDLIB_VULKAN` wird auf `OFF` gesetzt | Vulkan oder Shader-Compiler fehlt | cmake-Ausgabe lesen, Pakete nachinstallieren |
| `fftw3` nicht gefunden | `libfftw3-dev` fehlt | `sudo aptitude install libfftw3-dev` |
| `clinfo` zeigt 0 Devices | Clover ohne Rusticl | GPU-OpenCL nur nach Mesa-Rusticl-Build |
| `aptitude` fehlt | Nicht vorinstalliert | `sudo apt-get install aptitude` |
