#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-jetson.sh — Abhängigkeiten für NVIDIA Jetson (JetPack)
#
# Zielplattform:  NVIDIA Jetson Nano / Xavier NX / Orin NX / AGX Orin
#                 Ubuntu 20.04 / 22.04 (aarch64), JetPack 5.x / 6.x
#
# GPU-Fähigkeiten: NVIDIA GPU + CUDA
#   Vulkan:  vollständig via NVIDIA proprietary driver (JetPack 5+)
#   OpenCL:  via CUDA OpenCL ICD (libcuda.so + nvidia-opencl-icd)
#   CUDA:    nicht direkt von diesem Projekt genutzt, aber mitinstalliert
#
# Aktivierte Features nach Installation:
#   FIDLIB_SIMD=ON      NEON (AArch64, immer verfügbar)
#   FIDLIB_FFT=ON       Overlap-Save + FFTW3
#   FIDLIB_VULKAN=ON    Vulkan 1.x Compute via NVIDIA driver
#   FIDLIB_OPENCL=ON    OpenCL via CUDA ICD (NVIDIA GPU als Compute-Device)
#
# Voraussetzung: JetPack 5.x oder 6.x muss bereits installiert sein.
#   Prüfen: dpkg -l | grep -i jetpack
#   JetPack bringt CUDA, cuDNN, libvulkan, libOpenCL bereits mit.
#
# Aufruf: bash scripts/install-deps-jetson.sh [--no-gpu]

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
sect()  { echo -e "\n${CYAN}${BOLD}── $* ──${NC}"; }

# ── Argumente ────────────────────────────────────────────────────────────────
WITH_GPU=1
for arg in "$@"; do
    case "$arg" in
        --no-gpu) WITH_GPU=0 ;;
        *) error "Unbekanntes Argument: $arg"; echo "Aufruf: $0 [--no-gpu]"; exit 1 ;;
    esac
done

# ── Plattform-Prüfung ────────────────────────────────────────────────────────
ARCH=$(uname -m)
if [[ "$ARCH" != "aarch64" ]]; then
    warn "Dieses Skript ist für aarch64 (Jetson) — aktuelle Architektur: $ARCH"
    warn "Trotzdem fortfahren?"
    read -rp "[j/N] " C; [[ "${C,,}" == "j" ]] || exit 1
fi

# Tegra-SoC prüfen
if ! grep -qi "tegra\|jetson\|nvidia" /proc/cpuinfo 2>/dev/null &&
   ! grep -qi "tegra\|jetson"         /proc/device-tree/model 2>/dev/null; then
    warn "Kein Tegra/Jetson-SoC erkannt."
fi

# JetPack-Version anzeigen (falls vorhanden)
if dpkg -l nvidia-jetpack 2>/dev/null | grep -q "^ii"; then
    JP_VER=$(dpkg -l nvidia-jetpack 2>/dev/null | awk '/nvidia-jetpack/{print $3}' | head -1)
    info "JetPack erkannt: $JP_VER"
else
    warn "nvidia-jetpack-Paket nicht gefunden — ist JetPack korrekt installiert?"
    warn "  Prüfen: dpkg -l | grep -i jetpack"
fi

# ── aptitude prüfen ──────────────────────────────────────────────────────────
# Jetson/Ubuntu: apt oder aptitude
if command -v aptitude &>/dev/null; then
    PKG_MGR_CMD="aptitude"
    SIM_FLAG="--simulate"
else
    warn "aptitude nicht gefunden — verwende apt-get als Fallback."
    warn "  Für konsistenteres Dependency-Handling: sudo apt-get install aptitude"
    PKG_MGR_CMD="apt-get"
    SIM_FLAG="--dry-run"
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
    libfftw3-dev        # Overlap-Save FFTW3-Backend
)

# Vulkan-Header und Shader-Compiler
# libvulkan1 und libvulkan-dev werden von JetPack bereitgestellt.
# Wenn JetPack nicht installiert: Fallback auf libvulkan-dev aus Ubuntu-Repo.
VULKAN_TOOLS=(
    glslang-tools       # glslangValidator: GLSL → SPIR-V für fir_dot.comp
    spirv-tools         # spirv-dis / spirv-val (Diagnose)
    vulkan-tools        # vulkaninfo
)

# OpenCL-Headers + ICD-Loader
# Das CUDA-Paket bringt libOpenCL.so mit — hier nur Headers + ICD-Loader-Dev.
OPENCL_HEADERS=(
    opencl-headers      # für #include <CL/opencl.h>
    ocl-icd-opencl-dev  # ICD-Loader + libOpenCL.so (falls nicht durch CUDA bereitgestellt)
    ocl-icd-libopencl1  # ICD-Loader Runtime
    clinfo              # OpenCL-Diagnose
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

sect "Basis-Pakete"
printf '  %s\n' "${BASE_PACKAGES[@]}"

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    sect "GPU-Pakete (Vulkan-Tools + OpenCL-Headers)"
    printf '  %s\n' "${GPU_PACKAGES[@]}"
    echo
    info "Hinweis: JetPack installiert Vulkan-Treiber + CUDA-OpenCL automatisch."
    info "  libvulkan1, libvulkan-dev und libOpenCL.so kommen aus dem CUDA/JetPack-Stack."
    info "  Hier werden nur Shader-Compiler-Tools und OpenCL-Dev-Headers ergänzt."
fi
echo

# ── Vulkan-Bibliothek separat prüfen ─────────────────────────────────────────
if [[ $WITH_GPU -eq 1 ]]; then
    if ! dpkg -l libvulkan1 2>/dev/null | grep -q "^ii"; then
        warn "libvulkan1 nicht gefunden — wird separat nachinstalliert:"
        $SUDO $PKG_MGR_CMD install -y libvulkan-dev libvulkan1 || true
    fi
fi

# ── Simulate + Bestätigung ───────────────────────────────────────────────────
sect "Simulation"
$SUDO $PKG_MGR_CMD install $SIM_FLAG -y "${ALL[@]}"
echo
read -rp "Installation durchführen? [j/N] " CONFIRM
[[ "${CONFIRM,,}" == "j" ]] || { warn "Abgebrochen."; exit 0; }

# ── Installieren ─────────────────────────────────────────────────────────────
sect "Installation"
$SUDO $PKG_MGR_CMD install -y "${ALL[@]}"

# ── Versionen ────────────────────────────────────────────────────────────────
sect "Installierte Versionen"
cmake --version               | head -1
gcc   --version               | head -1
pkg-config --modversion fftw3 2>/dev/null | sed 's/^/fftw3: /' || true

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    info "GPU-Toolchain:"
    nvcc --version 2>/dev/null | grep "release" || warn "CUDA nvcc nicht gefunden"
    glslangValidator --version 2>/dev/null | head -1 || warn "glslangValidator nicht gefunden"
    vulkaninfo --summary 2>/dev/null \
        | grep -E "deviceName|apiVersion" | head -4 || true
    clinfo --list 2>/dev/null | head -8 || true
fi

# ── cmake-Konfiguration ──────────────────────────────────────────────────────
sect "Empfohlene cmake-Konfiguration"
cat <<'EOF'
  # Vollständiger Jetson-Build (NEON + FFT + Vulkan + OpenCL):
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

  # Nur NEON + FFT (ohne GPU, falls JetPack-GPU-Stack fehlt):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
        -S . -B build_jetson_cpu
  cmake --build build_jetson_cpu -j$(nproc)
EOF
