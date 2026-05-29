#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-desktop-x86.sh — Dependencies for Desktop Linux x86_64
#
# Target platform:  Ubuntu 22.04+ / Debian Bookworm, x86_64
# GPU capabilities: full Vulkan and OpenCL stack available
#   NVIDIA: proprietary driver + CUDA-OpenCL / Vulkan
#   AMD:    Mesa RADV (Vulkan) + Mesa ROCm/Clover (OpenCL) or ROCm OpenCL
#   Intel:  Mesa ANV (Vulkan) + Intel NEO (OpenCL)
#
# Enabled features after installation:
#   FIDLIB_SIMD=ON      SSE2 (x86_64, always available; AVX2 via compiler flags)
#   FIDLIB_FFT=ON       Overlap-Save + FFTW3
#   FIDLIB_VULKAN=ON    Vulkan 1.x Compute (NVIDIA/AMD/Intel)
#   FIDLIB_OPENCL=ON    OpenCL (GPU/CPU depending on installed driver)
#
# Usage: bash scripts/install-deps-desktop-x86.sh [--no-gpu]
#
#   --no-gpu   install build base + FFTW3 only (no Vulkan/OpenCL)

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
if [[ "$ARCH" != "x86_64" ]]; then
    warn "This script is for x86_64 — current architecture: $ARCH"
    warn "Continue anyway?"
    read -rp "[y/N] " C; [[ "${C,,}" == "y" ]] || exit 1
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
    libfftw3-dev        # Overlap-Save FFTW3 backend
)

VULKAN=(
    libvulkan-dev       # Vulkan headers + ICD loader
    glslang-tools       # glslangValidator: GLSL → SPIR-V for fir_dot.comp
    spirv-tools         # spirv-dis / spirv-val (diagnostics)
    vulkan-tools        # vulkaninfo — check GPU capabilities
    mesa-vulkan-drivers # Mesa RADV (AMD) + ANV (Intel) Vulkan drivers
)

OPENCL=(
    opencl-headers      # for #include <CL/opencl.h> (compile-time)
    ocl-icd-opencl-dev  # ICD loader + libOpenCL.so
    ocl-icd-libopencl1  # ICD loader runtime
    clinfo              # OpenCL diagnostics
)

DOC=(
    doxygen
    graphviz
)

BASE_PACKAGES=( "${BUILD[@]}" "${SDL2[@]}" "${FFT[@]}" "${DOC[@]}" )
GPU_PACKAGES=( "${VULKAN[@]}" "${OPENCL[@]}" )

if [[ $WITH_GPU -eq 1 ]]; then
    ALL=( "${BASE_PACKAGES[@]}" "${GPU_PACKAGES[@]}" )
else
    ALL=( "${BASE_PACKAGES[@]}" )
fi

sect "Base packages"
printf '  %s\n' "${BASE_PACKAGES[@]}"

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    sect "GPU packages (Vulkan + OpenCL)"
    printf '  %s\n' "${GPU_PACKAGES[@]}"
    echo
    warn "Driver notes:"
    warn "  NVIDIA: Install proprietary driver + nvidia-opencl-icd or cuda-opencl separately."
    warn "    → sudo aptitude install nvidia-driver nvidia-opencl-icd"
    warn "  AMD:    mesa-opencl-icd (Clover/ROCm) or amdgpu-pro-opencl-icd (proprietary)."
    warn "    → sudo aptitude install mesa-opencl-icd"
    warn "  Intel:  intel-opencl-icd (Intel NEO, recommended for OpenCL 3.0)."
    warn "    → sudo aptitude install intel-opencl-icd"
    warn "  The OpenCL ICD packages are GPU-vendor-specific and are NOT installed automatically"
    warn "  here — please install manually as needed."
fi
echo

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
g++   --version               | head -1
pkg-config --modversion fftw3 2>/dev/null | sed 's/^/fftw3: /' || true

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    info "GPU toolchain:"
    glslangValidator --version 2>/dev/null | head -1 || warn "glslangValidator not found"
    vulkaninfo --summary 2>/dev/null \
        | grep -E "deviceName|apiVersion" | head -8 || true
    clinfo --list 2>/dev/null | head -10 || true
fi

# ── cmake configuration ───────────────────────────────────────────────────────
sect "Recommended cmake configuration"
cat <<'EOF'
  # Full desktop build (SSE2 + FFT + Vulkan + OpenCL):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON \
        -DFIDLIB_FFT=ON \
        -DFIDLIB_VULKAN=ON \
        -DFIDLIB_OPENCL=ON \
        -S . -B build_desktop
  cmake --build build_desktop -j$(nproc)
  ctest --test-dir build_desktop --output-on-failure

  # Vulkan only (without OpenCL):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON \
        -S . -B build_vk
  cmake --build build_vk -j$(nproc)

  # OpenCL only (without Vulkan):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DFIDLIB_OPENCL=ON \
        -S . -B build_ocl
  cmake --build build_ocl -j$(nproc)

  # Benchmark (all backends compared):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
        -DBUILD_BENCHMARKS=ON \
        -S . -B build_bench
  cmake --build build_bench --target bench_fir_backends -j$(nproc)
  ./build_bench/bin/bench_fir_backends
EOF
