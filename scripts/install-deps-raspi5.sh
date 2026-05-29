#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-raspi5.sh — Dependencies for Raspberry Pi 5
#
# Target platform:  RPi 5, Raspberry Pi OS Bookworm (aarch64)
# GPU capabilities: VideoCore VII (V3D 7.1) — Vulkan 1.2 natively via Mesa V3DV
#                   OpenCL GPU: only with self-built Mesa Rusticl
#
# Enabled features after installation:
#   FIDLIB_SIMD=ON      NEON (AArch64, always available)
#   FIDLIB_FFT=ON       Overlap-Save (built-in Radix-2 or FFTW3)
#   FIDLIB_VULKAN=ON    Vulkan 1.2 Compute via V3D 7.1 (FP32)
#   FIDLIB_OPENCL=OFF   RPi5 default: Clover has 0 GPU devices
#                       (ON only after self-built Mesa Rusticl)
#
# Usage: bash scripts/install-deps-raspi5.sh

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
    warn "This script is for aarch64 (RPi 5) — current architecture: $ARCH"
    warn "Continue anyway?"
    read -rp "[y/N] " C; [[ "${C,,}" == "y" ]] || exit 1
fi

if ! grep -qi "raspberry" /proc/cpuinfo 2>/dev/null &&
   ! grep -qi "BCM2712"   /proc/cpuinfo 2>/dev/null; then
    warn "No BCM2712 SoC detected — not a Raspberry Pi 5?"
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

VULKAN=(
    libvulkan-dev       # Vulkan headers + ICD loader (V3D 7.1 → Vulkan 1.2)
    glslang-tools       # glslangValidator: GLSL → SPIR-V for fir_dot.comp
    spirv-tools         # spirv-dis / spirv-val (optional, diagnostics)
    vulkan-tools        # vulkaninfo — check GPU capabilities
)

OPENCL=(
    opencl-headers      # for #include <CL/opencl.h> (compile-time)
    ocl-icd-opencl-dev  # ICD loader + libOpenCL.so
    mesa-opencl-icd     # Clover: 0 GPU devices on RPi5, but CPU path testable
    clinfo              # OpenCL diagnostics
)

DOC=(
    doxygen
    graphviz
)

ALL=( "${BUILD[@]}" "${SDL2[@]}" "${FFT[@]}" "${VULKAN[@]}" "${OPENCL[@]}" "${DOC[@]}" )

printf '  %s\n' "${ALL[@]}"

warn "OpenCL GPU note: Clover (mesa-opencl-icd) provides no GPU devices on RPi5."
warn "  For real OpenCL GPU support: build Mesa Rusticl from source."
warn "  Vulkan (V3D 7.1) works out of the box — recommended GPU path."

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
glslangValidator --version 2>/dev/null | head -1 || true
pkg-config --modversion fftw3 2>/dev/null | sed 's/^/fftw3: /' || true
vulkaninfo --summary 2>/dev/null \
    | grep -E "deviceName|apiVersion" | head -4 || true

# ── cmake configuration ───────────────────────────────────────────────────────
sect "Recommended cmake configuration"
cat <<'EOF'
  # Full RPi5 build (NEON + FFT + Vulkan):
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

  # OpenCL GPU (only after self-built Mesa Rusticl):
  # cmake ... -DFIDLIB_OPENCL=ON ...
EOF
