#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-raspi5.sh — Abhängigkeiten für Raspberry Pi 5
#
# Zielplattform:  RPi 5, Raspberry Pi OS Bookworm (aarch64)
# GPU-Fähigkeiten: VideoCore VII (V3D 7.1) — Vulkan 1.2 nativ via Mesa V3DV
#                  OpenCL GPU: nur mit selbst gebautem Mesa Rusticl
#
# Aktivierte Features nach Installation:
#   FIDLIB_SIMD=ON      NEON (AArch64, immer verfügbar)
#   FIDLIB_FFT=ON       Overlap-Save (built-in Radix-2 oder FFTW3)
#   FIDLIB_VULKAN=ON    Vulkan 1.2 Compute via V3D 7.1 (FP32)
#   FIDLIB_OPENCL=OFF   RPi5-Standard: Clover hat 0 GPU-Devices
#                       (ON nur nach Mesa-Rusticl-Eigenbau)
#
# Aufruf: bash scripts/install-deps-raspi5.sh

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
sect()  { echo -e "\n${CYAN}${BOLD}── $* ──${NC}"; }

# ── Plattform-Prüfung ────────────────────────────────────────────────────────
ARCH=$(uname -m)
if [[ "$ARCH" != "aarch64" ]]; then
    warn "Dieses Skript ist für aarch64 (RPi 5) — aktuelle Architektur: $ARCH"
    warn "Trotzdem fortfahren?"
    read -rp "[j/N] " C; [[ "${C,,}" == "j" ]] || exit 1
fi

if ! grep -qi "raspberry" /proc/cpuinfo 2>/dev/null &&
   ! grep -qi "BCM2712"   /proc/cpuinfo 2>/dev/null; then
    warn "Kein BCM2712-SoC erkannt — kein Raspberry Pi 5?"
fi

# ── aptitude prüfen ──────────────────────────────────────────────────────────
if ! command -v aptitude &>/dev/null; then
    error "aptitude nicht gefunden: sudo apt-get install aptitude"
    exit 1
fi
[[ $EUID -ne 0 ]] && SUDO=sudo || SUDO=

# ── Pakete ───────────────────────────────────────────────────────────────────
sect "Paketliste"

BUILD=(
    build-essential     # gcc, g++, make
    cmake               # >= 3.16 erforderlich
    git
    pkg-config
)

SDL2=(                  # SDL2-Quell-Build (ExternalProject_Add)
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev
    libxi-dev libxinerama-dev libxxf86vm-dev
    libgl1-mesa-dev libasound2-dev libpulse-dev
)

FFT=(
    libfftw3-dev        # FIDLIB_FFT FFTW3-Backend (optional, schneller als Radix-2)
)

VULKAN=(
    libvulkan-dev       # Vulkan-Header + ICD-Loader (V3D 7.1 → Vulkan 1.2)
    glslang-tools       # glslangValidator: GLSL → SPIR-V für fir_dot.comp
    spirv-tools         # spirv-dis / spirv-val (optional, Diagnose)
    vulkan-tools        # vulkaninfo — GPU-Capabilities prüfen
)

OPENCL=(
    opencl-headers      # für #include <CL/opencl.h> (compile-time)
    ocl-icd-opencl-dev  # ICD-Loader + libOpenCL.so
    mesa-opencl-icd     # Clover: 0 GPU-Devices auf RPi5, aber CPU-Pfad testbar
    clinfo              # OpenCL-Diagnose
)

DOC=(
    doxygen
    graphviz
)

ALL=( "${BUILD[@]}" "${SDL2[@]}" "${FFT[@]}" "${VULKAN[@]}" "${OPENCL[@]}" "${DOC[@]}" )

printf '  %s\n' "${ALL[@]}"

warn "OpenCL GPU-Hinweis: Clover (mesa-opencl-icd) liefert auf RPi5 keine GPU-Devices."
warn "  Für echten OpenCL-GPU-Support: Mesa Rusticl aus Quellcode bauen."
warn "  Vulkan (V3D 7.1) funktioniert sofort — empfohlener GPU-Pfad."

# ── Simulate + Bestätigung ───────────────────────────────────────────────────
sect "Simulation"
$SUDO aptitude install --simulate -y "${ALL[@]}"
echo
read -rp "Installation durchführen? [j/N] " CONFIRM
[[ "${CONFIRM,,}" == "j" ]] || { warn "Abgebrochen."; exit 0; }

# ── Installieren ─────────────────────────────────────────────────────────────
sect "Installation"
$SUDO aptitude install -y "${ALL[@]}"

# ── Versionen ────────────────────────────────────────────────────────────────
sect "Installierte Versionen"
cmake --version               | head -1
gcc   --version               | head -1
glslangValidator --version 2>/dev/null | head -1 || true
pkg-config --modversion fftw3 2>/dev/null | sed 's/^/fftw3: /' || true
vulkaninfo --summary 2>/dev/null \
    | grep -E "deviceName|apiVersion" | head -4 || true

# ── cmake-Konfiguration ──────────────────────────────────────────────────────
sect "Empfohlene cmake-Konfiguration"
cat <<'EOF'
  # Vollständiger RPi5-Build (NEON + FFT + Vulkan):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON \
        -DFIDLIB_FFT=ON \
        -DFIDLIB_VULKAN=ON \
        -S . -B build_raspi5
  cmake --build build_raspi5 -j$(nproc)
  ctest --test-dir build_raspi5 --output-on-failure

  # Benchmark:
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DBUILD_BENCHMARKS=ON \
        -S . -B build_bench
  cmake --build build_bench --target bench_fir_backends -j$(nproc)
  ./build_bench/bin/bench_fir_backends

  # OpenCL GPU (nur nach Mesa-Rusticl-Eigenbau):
  # cmake ... -DFIDLIB_OPENCL=ON ...
EOF
