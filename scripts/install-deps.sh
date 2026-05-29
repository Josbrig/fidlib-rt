#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps.sh — Installs all build dependencies for digitalfilterdesign
#
# Prerequisite: Debian/Ubuntu/Raspberry Pi OS with aptitude
# Usage:        bash scripts/install-deps.sh [--gpu]
#
#   --gpu   also installs Vulkan and OpenCL dev packages plus
#           the GLSL shader compiler (for FIDLIB_VULKAN and FIDLIB_OPENCL)

set -euo pipefail

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
head_()  { echo -e "${CYAN}── $* ──${NC}"; }

# ── Arguments ────────────────────────────────────────────────────────────────
WITH_GPU=0
for arg in "$@"; do
    case "$arg" in
        --gpu) WITH_GPU=1 ;;
        *) error "Unknown argument: $arg"; echo "Usage: $0 [--gpu]"; exit 1 ;;
    esac
done

# ── Checks ───────────────────────────────────────────────────────────────────
if ! command -v aptitude &>/dev/null; then
    error "aptitude not found. Please install first: sudo apt-get install aptitude"
    exit 1
fi

if [[ $EUID -ne 0 ]]; then
    SUDO=sudo
else
    SUDO=
fi

# ── Package groups ────────────────────────────────────────────────────────────

# Group 1: core build tools
BUILD_TOOLS=(
    build-essential     # gcc, g++, make
    cmake
    git
    pkg-config
)

# Group 2: SDL2 source build (ExternalProject — no system SDL needed,
#           but X11/audio headers for the SDL2 cmake configure step)
SDL2_DEPS=(
    libx11-dev
    libxext-dev
    libxrandr-dev
    libxcursor-dev
    libxi-dev
    libxinerama-dev
    libxxf86vm-dev
    libgl1-mesa-dev
    libasound2-dev
    libpulse-dev
)

# Group 3: SDL 1.2 (only when FIVIEW_USE_SDL2=OFF — needs Autotools)
SDL12_DEPS=(
    autoconf
    automake
    libtool
)

# Group 4: Optional FFT backend (FIDLIB_FFT=ON + FFTW3 backend)
FFT_DEPS=(
    libfftw3-dev        # Overlap-Save FFTW3 backend (faster than built-in Radix-2)
)

# Group 5: Vulkan Compute (FIDLIB_VULKAN=ON)
#   libvulkan-dev   — Vulkan headers + loader
#   glslang-tools   — glslangValidator (GLSL -> SPIR-V shader compiler)
#   spirv-tools     — SPIR-V utilities (optional: spirv-dis for debugging)
#   vulkan-tools    — vulkaninfo (check GPU capabilities)
VULKAN_DEPS=(
    libvulkan-dev
    glslang-tools
    spirv-tools
    vulkan-tools
)

# Group 6: OpenCL Compute (FIDLIB_OPENCL=ON)
#   opencl-headers          — OpenCL C headers (platform-independent)
#   ocl-icd-opencl-dev      — ICD loader + libOpenCL.so
#   ocl-icd-libopencl1      — ICD loader runtime
#   mesa-opencl-icd         — Clover software renderer (RPi5: 0 GPU devices,
#                             but useful for compiling/testing with CPU fallback)
#   clinfo                  — OpenCL device info (diagnostics)
OPENCL_DEPS=(
    opencl-headers
    ocl-icd-opencl-dev
    ocl-icd-libopencl1
    mesa-opencl-icd
    clinfo
)

# Group 7: Documentation
DOC_DEPS=(
    doxygen
    graphviz
)

# ── Assemble package list ────────────────────────────────────────────────────
BASE_PACKAGES=( "${BUILD_TOOLS[@]}" "${SDL2_DEPS[@]}" "${FFT_DEPS[@]}" "${DOC_DEPS[@]}" )
GPU_PACKAGES=( "${VULKAN_DEPS[@]}" "${OPENCL_DEPS[@]}" )

if [[ $WITH_GPU -eq 1 ]]; then
    ALL_PACKAGES=( "${BASE_PACKAGES[@]}" "${GPU_PACKAGES[@]}" )
else
    ALL_PACKAGES=( "${BASE_PACKAGES[@]}" )
fi

# ── Overview ─────────────────────────────────────────────────────────────────
echo
head_ "Base packages"
printf '  %s\n' "${BASE_PACKAGES[@]}"

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    head_ "GPU packages (Vulkan + OpenCL)"
    printf '  %s\n' "${GPU_PACKAGES[@]}"
    echo
    warn "RPi5 note: OpenCL GPU support requires Rusticl-Mesa (custom Mesa build)."
    warn "  mesa-opencl-icd (Clover) returns 0 GPU devices on RPi5."
    warn "  Vulkan (V3D 7.1) works out of the box after 'aptitude install libvulkan-dev glslang-tools'."
else
    echo
    info "GPU packages not selected. For Vulkan/OpenCL: $0 --gpu"
fi
echo

# ── Simulate ─────────────────────────────────────────────────────────────────
info "Simulating installation (--simulate) ..."
echo
$SUDO aptitude install --simulate -y "${ALL_PACKAGES[@]}"
echo

# ── Confirmation ─────────────────────────────────────────────────────────────
read -rp "Proceed with installation? [y/N] " CONFIRM
if [[ "${CONFIRM,,}" != "y" ]]; then
    warn "Aborted."
    exit 0
fi

# ── Install ──────────────────────────────────────────────────────────────────
info "Installing packages ..."
$SUDO aptitude install -y "${ALL_PACKAGES[@]}"

# ── Print versions ───────────────────────────────────────────────────────────
echo
info "Installed versions:"
cmake --version        | head -1
gcc   --version        | head -1
g++   --version        | head -1
doxygen --version      | head -1
dot -V 2>&1            | head -1
pkg-config --modversion fftw3 2>/dev/null && echo "fftw3: $(pkg-config --modversion fftw3)" || true

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    info "GPU toolchain:"
    glslangValidator --version 2>/dev/null | head -1 || warn "glslangValidator not found"
    vulkaninfo --summary 2>/dev/null | grep -E "deviceName|apiVersion" | head -4 || true
    clinfo --list 2>/dev/null | head -6 || true
fi

# ── cmake snippets ────────────────────────────────────────────────────────────
echo
info "Done. Next steps:"
echo
echo "  # Set up hooks (once after clone):"
echo "  bash scripts/install-hooks.sh"
echo
echo "  # Standard build (NEON + Overlap-Save FFT):"
echo "  cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug \\"
echo "        -DFIDLIB_FFT=ON \\"
echo "        -S . -B build"
echo "  cmake --build build -j\$(nproc)"
echo "  ctest --test-dir build --output-on-failure"
echo

if [[ $WITH_GPU -eq 1 ]]; then
    echo "  # Vulkan build (RPi5 — libvulkan-dev + glslang-tools required):"
    echo "  cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release \\"
    echo "        -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON \\"
    echo "        -S . -B build_vk"
    echo "  cmake --build build_vk -j\$(nproc)"
    echo
    echo "  # OpenCL build (desktop/Jetson — CPU fallback only on RPi5):"
    echo "  cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release \\"
    echo "        -DFIDLIB_FFT=ON -DFIDLIB_OPENCL=ON \\"
    echo "        -S . -B build_ocl"
    echo "  cmake --build build_ocl -j\$(nproc)"
    echo
fi

echo "  # Benchmark (BUILD_BENCHMARKS=ON):"
echo "  cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release \\"
echo "        -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \\"
echo "        -S . -B build_bench"
echo "  cmake --build build_bench --target bench_fir_backends -j\$(nproc)"
echo "  ./build_bench/bin/bench_fir_backends"
