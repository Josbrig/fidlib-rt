#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-raspi3.sh — Abhängigkeiten für Raspberry Pi 3 / Zero 2 W
#
# Zielplattform:  RPi 3B/3B+/Zero 2 W, Raspberry Pi OS Bookworm (aarch64)
# GPU-Fähigkeiten: VideoCore IV — kein Vulkan, kein OpenCL
#
# Aktivierte Features nach Installation:
#   FIDLIB_SIMD=ON      NEON (AArch64, immer verfügbar)
#   FIDLIB_FFT=ON       Overlap-Save (built-in Radix-2 oder FFTW3)
#   FIDLIB_VULKAN=OFF   VideoCore IV hat keine Vulkan-Unterstützung
#   FIDLIB_OPENCL=OFF   VideoCore IV hat keine OpenCL-Unterstützung
#
# Hinweis RPi Zero 2 W: 512 MB RAM — FFTW3-Backend empfohlen, große FFT-Blöcke
#   (FIDLIB_FFT_THRESHOLD > 512) vermeiden.
#
# Aufruf: bash scripts/install-deps-raspi3.sh

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
    warn "Dieses Skript ist für aarch64 (RPi 3/Zero2) — aktuelle Architektur: $ARCH"
    warn "Trotzdem fortfahren?"
    read -rp "[j/N] " C; [[ "${C,,}" == "j" ]] || exit 1
fi

if ! grep -qi "raspberry" /proc/cpuinfo 2>/dev/null &&
   ! grep -qi "BCM2837\|BCM2710" /proc/cpuinfo 2>/dev/null; then
    warn "Kein BCM2837/BCM2710-SoC erkannt — kein Raspberry Pi 3 / Zero 2 W?"
fi

# ── RAM prüfen (Zero 2 W: 512 MB) ────────────────────────────────────────────
TOTAL_MEM_KB=$(grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2}' || echo 0)
if [[ "$TOTAL_MEM_KB" -lt 700000 && "$TOTAL_MEM_KB" -gt 0 ]]; then
    warn "Wenig RAM erkannt (~512 MB — RPi Zero 2 W?)."
    warn "  FFTW3 (libfftw3-dev) wird installiert — FFT_THRESHOLD niedrig halten (≤ 512)."
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
    libfftw3-dev        # Overlap-Save FFTW3-Backend (schneller als built-in Radix-2)
)

DOC=(
    doxygen
    graphviz
)

ALL=( "${BUILD[@]}" "${SDL2[@]}" "${FFT[@]}" "${DOC[@]}" )

printf '  %s\n' "${ALL[@]}"

info "GPU-Hinweis: VideoCore IV (RPi 3 / Zero 2 W) unterstützt kein Vulkan/OpenCL."
info "  Optimaler Pfad: NEON-SIMD + Overlap-Save FFT."

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
  # Standard-Build RPi 3 / Zero 2 W (NEON + FFT):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON \
        -DFIDLIB_FFT=ON \
        -S . -B build_raspi3
  cmake --build build_raspi3 -j$(nproc)
  ctest --test-dir build_raspi3 --output-on-failure

  # Zero 2 W: FFT-Threshold niedrig halten (wenig RAM):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON \
        -DFIDLIB_FFT=ON \
        -DFIDLIB_FFT_THRESHOLD=256 \
        -S . -B build_raspi3_lowmem
  cmake --build build_raspi3_lowmem -j2

  # Benchmark:
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \
        -S . -B build_bench
  cmake --build build_bench --target bench_fir_backends -j$(nproc)
  ./build_bench/bin/bench_fir_backends
EOF
