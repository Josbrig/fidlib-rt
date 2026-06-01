#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-raspi4.sh — Abhängigkeiten für Raspberry Pi 4
#
# Zielplattform:  RPi 4, Raspberry Pi OS Bookworm (aarch64)
# GPU-Fähigkeiten: VideoCore VI (V3D 4.2)
#   Vulkan:  experimentell via Mesa V3DV (Vulkan 1.0/1.1, kein Compute in Mesa < 23.1)
#   OpenCL:  nicht unterstützt (Clover/Rusticl: kein V3D-6-Treiber)
#
# Aktivierte Features nach Installation:
#   FIDLIB_SIMD=ON      NEON (AArch64, immer verfügbar)
#   FIDLIB_FFT=ON       Overlap-Save (built-in Radix-2 oder FFTW3)
#   FIDLIB_VULKAN=OFF   V3D 4.2 hat keinen Vulkan-Compute-Shader-Support
#                       (Compute benötigt Vulkan 1.1 + computeShader = VK_TRUE)
#   FIDLIB_OPENCL=OFF   Kein funktionierender OpenCL-Treiber für V3D 4.2
#
# Aufruf: bash scripts/install-deps-raspi4.sh

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
    warn "Dieses Skript ist für aarch64 (RPi 4) — aktuelle Architektur: $ARCH"
    warn "Trotzdem fortfahren?"
    read -rp "[j/N] " C; [[ "${C,,}" == "j" ]] || exit 1
fi

if ! grep -qi "raspberry" /proc/cpuinfo 2>/dev/null &&
   ! grep -qi "BCM2711"   /proc/cpuinfo 2>/dev/null; then
    warn "Kein BCM2711-SoC erkannt — kein Raspberry Pi 4?"
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

DOC=(
    doxygen
    graphviz
)

ALL=( "${BUILD[@]}" "${SDL2[@]}" "${FFT[@]}" "${DOC[@]}" )

printf '  %s\n' "${ALL[@]}"

warn "GPU-Hinweis: RPi 4 (VideoCore VI / V3D 4.2) hat keinen Vulkan-Compute-Support."
warn "  Vulkan-Rendering (Graphics) funktioniert ab Mesa 21 teilweise,"
warn "  aber Vulkan Compute Shader (VkComputePipeline) ist nicht verfügbar."
warn "  Empfehlung: NEON-SIMD + Overlap-Save FFT ist der optimale Pfad auf RPi 4."

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
pkg-config --modversion fftw3 2>/dev/null | sed 's/^/fftw3: /' || true

# ── cmake-Konfiguration ──────────────────────────────────────────────────────
sect "Empfohlene cmake-Konfiguration"
cat <<'EOF'
  # Standard-Build RPi 4 (NEON + FFT — kein GPU-Compute):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON \
        -DFIDLIB_FFT=ON \
        -S . -B build_raspi4
  cmake --build build_raspi4 -j$(nproc)
  ctest --test-dir build_raspi4 --output-on-failure

  # Benchmark (NEON vs. OLA/FFT):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \
        -S . -B build_bench
  cmake --build build_bench --target bench_fir_backends -j$(nproc)
  ./build_bench/bin/bench_fir_backends

  # FIDLIB_VULKAN=ON und FIDLIB_OPENCL=ON sind auf RPi 4 nicht sinnvoll.
  # Für GPU-Compute: RPi 5 (VideoCore VII, Vulkan 1.2) oder Desktop verwenden.
EOF
