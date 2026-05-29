#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-raspi3.sh — Dependencies for Raspberry Pi 3 / Zero 2 W
#
# Target platform:  RPi 3B/3B+/Zero 2 W, Raspberry Pi OS Bookworm (aarch64)
# GPU capabilities: VideoCore IV — no Vulkan, no OpenCL
#
# Enabled features after installation:
#   FIDLIB_SIMD=ON      NEON (AArch64, always available)
#   FIDLIB_FFT=ON       Overlap-Save (built-in Radix-2 or FFTW3)
#   FIDLIB_VULKAN=OFF   VideoCore IV has no Vulkan support
#   FIDLIB_OPENCL=OFF   VideoCore IV has no OpenCL support
#
# Note RPi Zero 2 W: 512 MB RAM — FFTW3 backend recommended, avoid large FFT blocks
#   (FIDLIB_FFT_THRESHOLD > 512).
#
# Usage: bash scripts/install-deps-raspi3.sh

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
sect()  { echo -e "\n${CYAN}${BOLD}── $* ──${NC}"; }

# ── Platform check ───────────────────────────────────────────────────────────
ARCH=$(uname -m)
if [[ "$ARCH" != "aarch64" ]]; then
    warn "This script is for aarch64 (RPi 3/Zero2) — current architecture: $ARCH"
    warn "Continue anyway?"
    read -rp "[y/N] " C; [[ "${C,,}" == "y" ]] || exit 1
fi

if ! grep -qi "raspberry" /proc/cpuinfo 2>/dev/null &&
   ! grep -qi "BCM2837\|BCM2710" /proc/cpuinfo 2>/dev/null; then
    warn "No BCM2837/BCM2710 SoC detected — not a Raspberry Pi 3 / Zero 2 W?"
fi

# ── Check RAM (Zero 2 W: 512 MB) ─────────────────────────────────────────────
TOTAL_MEM_KB=$(grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2}' || echo 0)
if [[ "$TOTAL_MEM_KB" -lt 700000 && "$TOTAL_MEM_KB" -gt 0 ]]; then
    warn "Low RAM detected (~512 MB — RPi Zero 2 W?)."
    warn "  FFTW3 (libfftw3-dev) will be installed — keep FFT_THRESHOLD low (≤ 512)."
fi

# ── Check aptitude ───────────────────────────────────────────────────────────
if ! command -v aptitude &>/dev/null; then
    error "aptitude not found: sudo apt-get install aptitude"
    exit 1
fi
[[ $EUID -ne 0 ]] && SUDO=sudo || SUDO=

# ── Packages ─────────────────────────────────────────────────────────────────
sect "Package list"

BUILD=(
    build-essential     # gcc, g++, make
    cmake               # >= 3.16 required
    git
    pkg-config
)

SDL2=(                  # SDL2 source build (ExternalProject_Add)
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev
    libxi-dev libxinerama-dev libxxf86vm-dev
    libgl1-mesa-dev libasound2-dev libpulse-dev
)

FFT=(
    libfftw3-dev        # Overlap-Save FFTW3 backend (faster than built-in Radix-2)
)

DOC=(
    doxygen
    graphviz
)

ALL=( "${BUILD[@]}" "${SDL2[@]}" "${FFT[@]}" "${DOC[@]}" )

printf '  %s\n' "${ALL[@]}"

info "GPU note: VideoCore IV (RPi 3 / Zero 2 W) does not support Vulkan/OpenCL."
info "  Optimal path: NEON-SIMD + Overlap-Save FFT."

# ── Simulate + confirmation ───────────────────────────────────────────────────
sect "Simulation"
$SUDO aptitude install --simulate -y "${ALL[@]}"
echo
read -rp "Proceed with installation? [y/N] " CONFIRM
[[ "${CONFIRM,,}" == "y" ]] || { warn "Aborted."; exit 0; }

# ── Install ───────────────────────────────────────────────────────────────────
sect "Installation"
$SUDO aptitude install -y "${ALL[@]}"

# ── Versions ──────────────────────────────────────────────────────────────────
sect "Installed versions"
cmake --version               | head -1
gcc   --version               | head -1
pkg-config --modversion fftw3 2>/dev/null | sed 's/^/fftw3: /' || true

# ── cmake configuration ───────────────────────────────────────────────────────
sect "Recommended cmake configuration"
cat <<'EOF'
  # Standard RPi 3 / Zero 2 W build (NEON + FFT):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON \
        -DFIDLIB_FFT=ON \
        -S . -B build_raspi3
  cmake --build build_raspi3 -j$(nproc)
  ctest --test-dir build_raspi3 --output-on-failure

  # Zero 2 W: keep FFT threshold low (limited RAM):
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
