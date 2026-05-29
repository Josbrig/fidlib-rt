#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-jetson.sh — Dependencies for NVIDIA Jetson (JetPack)
#
# Target platform:  NVIDIA Jetson Nano / Xavier NX / Orin NX / AGX Orin
#                   Ubuntu 20.04 / 22.04 (aarch64), JetPack 5.x / 6.x
#
# GPU capabilities: NVIDIA GPU + CUDA
#   Vulkan:  fully supported via NVIDIA proprietary driver (JetPack 5+)
#   OpenCL:  via CUDA OpenCL ICD (libcuda.so + nvidia-opencl-icd)
#   CUDA:    not directly used by this project, but installed alongside
#
# Enabled features after installation:
#   FIDLIB_SIMD=ON      NEON (AArch64, always available)
#   FIDLIB_FFT=ON       Overlap-Save + FFTW3
#   FIDLIB_VULKAN=ON    Vulkan 1.x Compute via NVIDIA driver
#   FIDLIB_OPENCL=ON    OpenCL via CUDA ICD (NVIDIA GPU as compute device)
#
# Prerequisite: JetPack 5.x or 6.x must already be installed.
#   Check: dpkg -l | grep -i jetpack
#   JetPack ships CUDA, cuDNN, libvulkan, libOpenCL already.
#
# Usage: bash scripts/install-deps-jetson.sh [--no-gpu]

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
sect()  { echo -e "\n${CYAN}${BOLD}── $* ──${NC}"; }

# ── Arguments ─────────────────────────────────────────────────────────────────
WITH_GPU=1
for arg in "$@"; do
    case "$arg" in
        --no-gpu) WITH_GPU=0 ;;
        *) error "Unknown argument: $arg"; echo "Usage: $0 [--no-gpu]"; exit 1 ;;
    esac
done

# ── Platform check ───────────────────────────────────────────────────────────
ARCH=$(uname -m)
if [[ "$ARCH" != "aarch64" ]]; then
    warn "This script is for aarch64 (Jetson) — current architecture: $ARCH"
    warn "Continue anyway?"
    read -rp "[y/N] " C; [[ "${C,,}" == "y" ]] || exit 1
fi

# Check for Tegra SoC
if ! grep -qi "tegra\|jetson\|nvidia" /proc/cpuinfo 2>/dev/null &&
   ! grep -qi "tegra\|jetson"         /proc/device-tree/model 2>/dev/null; then
    warn "No Tegra/Jetson SoC detected."
fi

# Display JetPack version (if present)
if dpkg -l nvidia-jetpack 2>/dev/null | grep -q "^ii"; then
    JP_VER=$(dpkg -l nvidia-jetpack 2>/dev/null | awk '/nvidia-jetpack/{print $3}' | head -1)
    info "JetPack detected: $JP_VER"
else
    warn "nvidia-jetpack package not found — is JetPack correctly installed?"
    warn "  Check: dpkg -l | grep -i jetpack"
fi

# ── Check aptitude ───────────────────────────────────────────────────────────
# Jetson/Ubuntu: apt or aptitude
if command -v aptitude &>/dev/null; then
    PKG_MGR_CMD="aptitude"
    SIM_FLAG="--simulate"
else
    warn "aptitude not found — falling back to apt-get."
    warn "  For more consistent dependency handling: sudo apt-get install aptitude"
    PKG_MGR_CMD="apt-get"
    SIM_FLAG="--dry-run"
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
    libfftw3-dev        # Overlap-Save FFTW3 backend
)

# Vulkan headers and shader compiler
# libvulkan1 and libvulkan-dev are provided by JetPack.
# If JetPack is not installed: fall back to libvulkan-dev from Ubuntu repo.
VULKAN_TOOLS=(
    glslang-tools       # glslangValidator: GLSL → SPIR-V for fir_dot.comp
    spirv-tools         # spirv-dis / spirv-val (diagnostics)
    vulkan-tools        # vulkaninfo
)

# OpenCL headers + ICD loader
# The CUDA package ships libOpenCL.so — only headers + ICD loader dev needed here.
OPENCL_HEADERS=(
    opencl-headers      # for #include <CL/opencl.h>
    ocl-icd-opencl-dev  # ICD loader + libOpenCL.so (if not provided by CUDA)
    ocl-icd-libopencl1  # ICD loader runtime
    clinfo              # OpenCL diagnostics
)

DOC=(
    doxygen
    graphviz
)

BASE_PACKAGES=( "${BUILD[@]}" "${SDL2[@]}" "${FFT[@]}" "${DOC[@]}" )
GPU_PACKAGES=( "${VULKAN_TOOLS[@]}" "${OPENCL_HEADERS[@]}" )

if [[ $WITH_GPU -eq 1 ]]; then
    ALL=( "${BASE_PACKAGES[@]}" "${GPU_PACKAGES[@]}" )
else
    ALL=( "${BASE_PACKAGES[@]}" )
fi

sect "Base packages"
printf '  %s\n' "${BASE_PACKAGES[@]}"

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    sect "GPU packages (Vulkan tools + OpenCL headers)"
    printf '  %s\n' "${GPU_PACKAGES[@]}"
    echo
    info "Note: JetPack installs Vulkan driver + CUDA-OpenCL automatically."
    info "  libvulkan1, libvulkan-dev and libOpenCL.so come from the CUDA/JetPack stack."
    info "  Only shader compiler tools and OpenCL dev headers are added here."
fi
echo

# ── Check Vulkan library separately ──────────────────────────────────────────
if [[ $WITH_GPU -eq 1 ]]; then
    if ! dpkg -l libvulkan1 2>/dev/null | grep -q "^ii"; then
        warn "libvulkan1 not found — installing separately:"
        $SUDO $PKG_MGR_CMD install -y libvulkan-dev libvulkan1 || true
    fi
fi

# ── Simulate + confirmation ───────────────────────────────────────────────────
sect "Simulation"
$SUDO $PKG_MGR_CMD install $SIM_FLAG -y "${ALL[@]}"
echo
read -rp "Proceed with installation? [y/N] " CONFIRM
[[ "${CONFIRM,,}" == "y" ]] || { warn "Aborted."; exit 0; }

# ── Install ───────────────────────────────────────────────────────────────────
sect "Installation"
$SUDO $PKG_MGR_CMD install -y "${ALL[@]}"

# ── Versions ──────────────────────────────────────────────────────────────────
sect "Installed versions"
cmake --version               | head -1
gcc   --version               | head -1
pkg-config --modversion fftw3 2>/dev/null | sed 's/^/fftw3: /' || true

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    info "GPU toolchain:"
    nvcc --version 2>/dev/null | grep "release" || warn "CUDA nvcc not found"
    glslangValidator --version 2>/dev/null | head -1 || warn "glslangValidator not found"
    vulkaninfo --summary 2>/dev/null \
        | grep -E "deviceName|apiVersion" | head -4 || true
    clinfo --list 2>/dev/null | head -8 || true
fi

# ── cmake configuration ───────────────────────────────────────────────────────
sect "Recommended cmake configuration"
cat <<'EOF'
  # Full Jetson build (NEON + FFT + Vulkan + OpenCL):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON \
        -DFIDLIB_FFT=ON \
        -DFIDLIB_VULKAN=ON \
        -DFIDLIB_OPENCL=ON \
        -S . -B build_jetson
  cmake --build build_jetson -j$(nproc)
  ctest --test-dir build_jetson --output-on-failure

  # Benchmark (NEON vs. OLA/FFT vs. Vulkan vs. OpenCL):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
        -DBUILD_BENCHMARKS=ON \
        -S . -B build_bench
  cmake --build build_bench --target bench_fir_backends -j$(nproc)
  ./build_bench/bin/bench_fir_backends

  # NEON + FFT only (without GPU, if JetPack GPU stack is missing):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
        -S . -B build_jetson_cpu
  cmake --build build_jetson_cpu -j$(nproc)
EOF
