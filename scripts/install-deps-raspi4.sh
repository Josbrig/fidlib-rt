#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-raspi4.sh — Dependencies for Raspberry Pi 4
#
# Target platform:  RPi 4, Raspberry Pi OS Bookworm (aarch64)
# GPU capabilities: VideoCore VI (V3D 4.2)
#   Vulkan:  experimental via Mesa V3DV (Vulkan 1.0/1.1, no Compute in Mesa < 23.1)
#   OpenCL:  not supported (Clover/Rusticl: no V3D-6 driver)
#
# Enabled features after installation:
#   FIDLIB_SIMD=ON      NEON (AArch64, always available)
#   FIDLIB_FFT=ON       Overlap-Save (built-in Radix-2 or FFTW3)
#   FIDLIB_VULKAN=OFF   V3D 4.2 has no Vulkan compute shader support
#                       (Compute requires Vulkan 1.1 + computeShader = VK_TRUE)
#   FIDLIB_OPENCL=OFF   No working OpenCL driver for V3D 4.2
#
# Usage: bash scripts/install-deps-raspi4.sh

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
    warn "This script is for aarch64 (RPi 4) — current architecture: $ARCH"
    warn "Continue anyway?"
    read -rp "[y/N] " C; [[ "${C,,}" == "y" ]] || exit 1
fi

if ! grep -qi "raspberry" /proc/cpuinfo 2>/dev/null &&
   ! grep -qi "BCM2711"   /proc/cpuinfo 2>/dev/null; then
    warn "No BCM2711 SoC detected — not a Raspberry Pi 4?"
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
    libfftw3-dev        # FIDLIB_FFT FFTW3 backend (optional, faster than Radix-2)
)

DOC=(
    doxygen
    graphviz
)

ALL=( "${BUILD[@]}" "${SDL2[@]}" "${FFT[@]}" "${DOC[@]}" )

printf '  %s\n' "${ALL[@]}"

warn "GPU note: RPi 4 (VideoCore VI / V3D 4.2) has no Vulkan compute support."
warn "  Vulkan rendering (Graphics) works partially from Mesa 21 onwards,"
warn "  but Vulkan Compute Shaders (VkComputePipeline) are not available."
warn "  Recommendation: NEON-SIMD + Overlap-Save FFT is the optimal path on RPi 4."

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
  # Standard RPi 4 build (NEON + FFT — no GPU compute):
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

  # FIDLIB_VULKAN=ON and FIDLIB_OPENCL=ON are not useful on RPi 4.
  # For GPU compute: use RPi 5 (VideoCore VII, Vulkan 1.2) or desktop.
EOF
