#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps-desktop-x86.sh — Abhängigkeiten für Desktop Linux x86_64
#
# Zielplattform:  Ubuntu 22.04+ / Debian Bookworm, x86_64
# GPU-Fähigkeiten: vollständiger Vulkan- und OpenCL-Stack verfügbar
#   NVIDIA: proprietary driver + CUDA-OpenCL / Vulkan
#   AMD:    Mesa RADV (Vulkan) + Mesa ROCm/Clover (OpenCL) oder ROCm OpenCL
#   Intel:  Mesa ANV (Vulkan) + Intel NEO (OpenCL)
#
# Aktivierte Features nach Installation:
#   FIDLIB_SIMD=ON      SSE2 (x86_64, immer verfügbar; AVX2 über Compiler-Flags)
#   FIDLIB_FFT=ON       Overlap-Save + FFTW3
#   FIDLIB_VULKAN=ON    Vulkan 1.x Compute (NVIDIA/AMD/Intel)
#   FIDLIB_OPENCL=ON    OpenCL (GPU/CPU je nach installiertem Treiber)
#
# Aufruf: bash scripts/install-deps-desktop-x86.sh [--no-gpu]
#
#   --no-gpu   nur Build-Basis + FFTW3 installieren (kein Vulkan/OpenCL)

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
if [[ "$ARCH" != "x86_64" ]]; then
    warn "Dieses Skript ist für x86_64 — aktuelle Architektur: $ARCH"
    warn "Trotzdem fortfahren?"
    read -rp "[j/N] " C; [[ "${C,,}" == "j" ]] || exit 1
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
    libfftw3-dev        # Overlap-Save FFTW3-Backend
)

VULKAN=(
    libvulkan-dev       # Vulkan-Header + ICD-Loader
    glslang-tools       # glslangValidator: GLSL → SPIR-V für fir_dot.comp
    spirv-tools         # spirv-dis / spirv-val (Diagnose)
    vulkan-tools        # vulkaninfo — GPU-Capabilities prüfen
    mesa-vulkan-drivers # Mesa RADV (AMD) + ANV (Intel) Vulkan-Treiber
)

OPENCL=(
    opencl-headers      # für #include <CL/opencl.h> (compile-time)
    ocl-icd-opencl-dev  # ICD-Loader + libOpenCL.so
    ocl-icd-libopencl1  # ICD-Loader Runtime
    clinfo              # OpenCL-Diagnose
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

sect "Basis-Pakete"
printf '  %s\n' "${BASE_PACKAGES[@]}"

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    sect "GPU-Pakete (Vulkan + OpenCL)"
    printf '  %s\n' "${GPU_PACKAGES[@]}"
    echo
    warn "Treiber-Hinweise:"
    warn "  NVIDIA: Proprietären Treiber + nvidia-opencl-icd oder cuda-opencl separat installieren."
    warn "    → sudo aptitude install nvidia-driver nvidia-opencl-icd"
    warn "  AMD:    mesa-opencl-icd (Clover/ROCm) oder amdgpu-pro-opencl-icd (proprietär)."
    warn "    → sudo aptitude install mesa-opencl-icd"
    warn "  Intel:  intel-opencl-icd (Intel NEO, empfohlen für OpenCL 3.0)."
    warn "    → sudo aptitude install intel-opencl-icd"
    warn "  Die OpenCL-ICD-Pakete sind GPU-Vendor-spezifisch und werden hier NICHT automatisch"
    warn "  installiert — bitte manuell nach Bedarf nachinstallieren."
fi
echo

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
g++   --version               | head -1
pkg-config --modversion fftw3 2>/dev/null | sed 's/^/fftw3: /' || true

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    info "GPU-Toolchain:"
    glslangValidator --version 2>/dev/null | head -1 || warn "glslangValidator nicht gefunden"
    vulkaninfo --summary 2>/dev/null \
        | grep -E "deviceName|apiVersion" | head -8 || true
    clinfo --list 2>/dev/null | head -10 || true
fi

# ── cmake-Konfiguration ──────────────────────────────────────────────────────
sect "Empfohlene cmake-Konfiguration"
cat <<'EOF'
  # Vollständiger Desktop-Build (SSE2 + FFT + Vulkan + OpenCL):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_SIMD=ON \
        -DFIDLIB_FFT=ON \
        -DFIDLIB_VULKAN=ON \
        -DFIDLIB_OPENCL=ON \
        -S . -B build_desktop
  cmake --build build_desktop -j$(nproc)
  ctest --test-dir build_desktop --output-on-failure

  # Nur Vulkan (ohne OpenCL):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON \
        -S . -B build_vk
  cmake --build build_vk -j$(nproc)

  # Nur OpenCL (ohne Vulkan):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DFIDLIB_OPENCL=ON \
        -S . -B build_ocl
  cmake --build build_ocl -j$(nproc)

  # Benchmark (alle Backends im Vergleich):
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=ON \
        -DBUILD_BENCHMARKS=ON \
        -S . -B build_bench
  cmake --build build_bench --target bench_fir_backends -j$(nproc)
  ./build_bench/bin/bench_fir_backends
EOF
