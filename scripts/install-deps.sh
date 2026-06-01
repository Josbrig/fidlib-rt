#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-deps.sh — Installiert alle Build-Abhängigkeiten für digitalfilterdesign
#
# Voraussetzung: Debian/Ubuntu/Raspberry Pi OS mit aptitude
# Aufruf:        bash scripts/install-deps.sh [--gpu]
#
#   --gpu   installiert zusätzlich Vulkan- und OpenCL-Dev-Pakete sowie
#           den GLSL-Shader-Compiler (für FIDLIB_VULKAN und FIDLIB_OPENCL)

set -euo pipefail

# ── Farben ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
head_()  { echo -e "${CYAN}── $* ──${NC}"; }

# ── Argumente ────────────────────────────────────────────────────────────────
WITH_GPU=0
for arg in "$@"; do
    case "$arg" in
        --gpu) WITH_GPU=1 ;;
        *) error "Unbekanntes Argument: $arg"; echo "Aufruf: $0 [--gpu]"; exit 1 ;;
    esac
done

# ── Prüfungen ────────────────────────────────────────────────────────────────
if ! command -v aptitude &>/dev/null; then
    error "aptitude nicht gefunden. Bitte zuerst installieren: sudo apt-get install aptitude"
    exit 1
fi

if [[ $EUID -ne 0 ]]; then
    SUDO=sudo
else
    SUDO=
fi

# ── Paket-Gruppen ─────────────────────────────────────────────────────────────

# Gruppe 1: Basis-Build-Werkzeuge
BUILD_TOOLS=(
    build-essential     # gcc, g++, make
    cmake
    git
    pkg-config
)

# Gruppe 2: SDL2-Quell-Build (ExternalProject — kein System-SDL nötig,
#           aber X11/audio-Headers für den SDL2-cmake-Configure-Schritt)
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

# Gruppe 3: SDL 1.2 (nur wenn FIVIEW_USE_SDL2=OFF — braucht Autotools)
SDL12_DEPS=(
    autoconf
    automake
    libtool
)

# Gruppe 4: Optionales FFT-Backend (FIDLIB_FFT=ON + FFTW3-Backend)
FFT_DEPS=(
    libfftw3-dev        # Overlap-Save FFTW3-Backend (schneller als built-in Radix-2)
)

# Gruppe 5: Vulkan Compute (FIDLIB_VULKAN=ON)
#   libvulkan-dev   — Vulkan-Header + Loader
#   glslang-tools   — glslangValidator (GLSL → SPIR-V Shader-Compiler)
#   spirv-tools     — SPIR-V Utilities (optional: spirv-dis zum Debuggen)
#   vulkan-tools    — vulkaninfo (GPU-Fähigkeiten prüfen)
VULKAN_DEPS=(
    libvulkan-dev
    glslang-tools
    spirv-tools
    vulkan-tools
)

# Gruppe 6: OpenCL Compute (FIDLIB_OPENCL=ON)
#   opencl-headers          — OpenCL C-Headers (Platform-unabhängig)
#   ocl-icd-opencl-dev      — ICD-Loader + libOpenCL.so
#   ocl-icd-libopencl1      — ICD-Loader Runtime
#   mesa-opencl-icd         — Clover-Software-Renderer (RPi5: 0 GPU-Devices,
#                             aber zum Kompilieren/Testen auf CPU-Fallback nützlich)
#   clinfo                  — OpenCL-Device-Info (Diagnose)
OPENCL_DEPS=(
    opencl-headers
    ocl-icd-opencl-dev
    ocl-icd-libopencl1
    mesa-opencl-icd
    clinfo
)

# Gruppe 7: Dokumentation
DOC_DEPS=(
    doxygen
    graphviz
)

# ── Pakete zusammenstellen ────────────────────────────────────────────────────
BASE_PACKAGES=( "${BUILD_TOOLS[@]}" "${SDL2_DEPS[@]}" "${FFT_DEPS[@]}" "${DOC_DEPS[@]}" )
GPU_PACKAGES=( "${VULKAN_DEPS[@]}" "${OPENCL_DEPS[@]}" )

if [[ $WITH_GPU -eq 1 ]]; then
    ALL_PACKAGES=( "${BASE_PACKAGES[@]}" "${GPU_PACKAGES[@]}" )
else
    ALL_PACKAGES=( "${BASE_PACKAGES[@]}" )
fi

# ── Übersicht ─────────────────────────────────────────────────────────────────
echo
head_ "Basis-Pakete"
printf '  %s\n' "${BASE_PACKAGES[@]}"

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    head_ "GPU-Pakete (Vulkan + OpenCL)"
    printf '  %s\n' "${GPU_PACKAGES[@]}"
    echo
    warn "RPi5-Hinweis: OpenCL GPU-Support erfordert Rusticl-Mesa (Mesa-Eigenbau)."
    warn "  mesa-opencl-icd (Clover) liefert 0 GPU-Devices auf RPi5."
    warn "  Vulkan (V3D 7.1) funktioniert sofort nach 'aptitude install libvulkan-dev glslang-tools'."
else
    echo
    info "GPU-Pakete nicht ausgewählt. Für Vulkan/OpenCL: $0 --gpu"
fi
echo

# ── Simulate ─────────────────────────────────────────────────────────────────
info "Simuliere Installation (--simulate) ..."
echo
$SUDO aptitude install --simulate -y "${ALL_PACKAGES[@]}"
echo

# ── Bestätigung ──────────────────────────────────────────────────────────────
read -rp "Installation durchführen? [j/N] " CONFIRM
if [[ "${CONFIRM,,}" != "j" ]]; then
    warn "Abgebrochen."
    exit 0
fi

# ── Installieren ─────────────────────────────────────────────────────────────
info "Installiere Pakete ..."
$SUDO aptitude install -y "${ALL_PACKAGES[@]}"

# ── Versionen ausgeben ───────────────────────────────────────────────────────
echo
info "Installierte Versionen:"
cmake --version        | head -1
gcc   --version        | head -1
g++   --version        | head -1
doxygen --version      | head -1
dot -V 2>&1            | head -1
pkg-config --modversion fftw3 2>/dev/null && echo "fftw3: $(pkg-config --modversion fftw3)" || true

if [[ $WITH_GPU -eq 1 ]]; then
    echo
    info "GPU-Toolchain:"
    glslangValidator --version 2>/dev/null | head -1 || warn "glslangValidator nicht gefunden"
    vulkaninfo --summary 2>/dev/null | grep -E "deviceName|apiVersion" | head -4 || true
    clinfo --list 2>/dev/null | head -6 || true
fi

# ── cmake-Snippets ────────────────────────────────────────────────────────────
echo
info "Fertig. Nächste Schritte:"
echo
echo "  # Hooks einrichten (einmalig nach Clone):"
echo "  bash scripts/install-hooks.sh"
echo
echo "  # Standard-Build (NEON + Overlap-Save FFT):"
echo "  cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug \\"
echo "        -DFIDLIB_FFT=ON \\"
echo "        -S . -B build"
echo "  cmake --build build -j\$(nproc)"
echo "  ctest --test-dir build --output-on-failure"
echo

if [[ $WITH_GPU -eq 1 ]]; then
    echo "  # Vulkan-Build (RPi5 — libvulkan-dev + glslang-tools erforderlich):"
    echo "  cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release \\"
    echo "        -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON \\"
    echo "        -S . -B build_vk"
    echo "  cmake --build build_vk -j\$(nproc)"
    echo
    echo "  # OpenCL-Build (Desktop/Jetson — auf RPi5 nur CPU-Fallback):"
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
