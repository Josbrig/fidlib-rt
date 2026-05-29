# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2002-2004 Jim Peters <jim@uazu.net> (fidlib core, LGPL 2.1)
# Copyright (C) 2025-2026 Kai Dieki (cmake build system, GPU extensions)
add_library(fidlib STATIC fidlib.c)

target_compile_definitions(fidlib PRIVATE
  RF_CMDLIST
  VERSION="0.9.11"
)

target_include_directories(fidlib
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
)

find_library(MATH_LIB m)
target_link_libraries(fidlib PUBLIC ${MATH_LIB})

# C++20 compatibility mode: compile fidlib.c as C++20 to catch type errors
option(FIDLIB_CXX20_COMPAT "Compile fidlib with C++20 compiler (analysis + safety)" OFF)
if(FIDLIB_CXX20_COMPAT)
  set_source_files_properties(fidlib.c PROPERTIES LANGUAGE CXX)
  target_compile_options(fidlib PRIVATE
    -std=c++20
    -Wno-old-style-cast
    -Wno-zero-as-null-pointer-constant
  )
endif()

# Link-Time Optimization for filter_step hot path
option(FIDLIB_LTO "Enable link-time optimisation for fidlib" ON)
if(FIDLIB_LTO)
  set_property(TARGET fidlib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Scoped fast-math: only for fidlib's arithmetic-heavy filter execution
option(FIDLIB_FAST_MATH "Scoped -O3 -ffast-math for fidlib (RT/speed)" ON)
if(FIDLIB_FAST_MATH)
  target_compile_options(fidlib PRIVATE -O3 -ffast-math)
endif()

# SIMD acceleration: vectorised FIR dot product (NEON / SSE2 / scalar fallback)
option(FIDLIB_SIMD "SIMD acceleration for the FIR hot path (NEON/SSE2)" ON)
if(FIDLIB_SIMD)
  target_compile_definitions(fidlib PUBLIC FIDLIB_SIMD)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64|aarch64_be")
    # NEON is always available on AArch64 — no extra flag needed
    message(STATUS "fidlib SIMD: NEON (${CMAKE_SYSTEM_PROCESSOR})")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386")
    target_compile_options(fidlib PRIVATE -msse2)
    message(STATUS "fidlib SIMD: SSE2 (${CMAKE_SYSTEM_PROCESSOR})")
  else()
    message(STATUS "fidlib SIMD: scalar fallback (${CMAKE_SYSTEM_PROCESSOR} unknown)")
  endif()
endif()

# Execution precision: double (FP64, default) or float (FP32, GPU/SIMD preparation)
# FP32: FIR filters ~2x faster NEON vectorisation; IIR can become numerically unstable.
set(FIDLIB_PRECISION "double" CACHE STRING
    "Filter execution precision: double (FP64, default) or float (FP32)")
set_property(CACHE FIDLIB_PRECISION PROPERTY STRINGS "double" "float")

if(FIDLIB_PRECISION STREQUAL "float")
  target_compile_definitions(fidlib PUBLIC FID_REAL=float FIDLIB_PRECISION_F32)
  message(STATUS "fidlib PRECISION: float (FP32) — warning: IIR filters may become unstable!")
else()
  message(STATUS "fidlib PRECISION: double (FP64, default)")
endif()

# Overlap-Save FFT convolution for long FIR filters (avoids O(N²) hot path)
option(FIDLIB_FFT "Overlap-Save FFT engine for FIR filters above tap threshold" OFF)
set(FIDLIB_FFT_THRESHOLD 512 CACHE STRING
    "FIR tap threshold for automatic FFT switching")
if(FIDLIB_FFT)
  target_compile_definitions(fidlib PUBLIC
      FIDLIB_FFT
      FIDLIB_FFT_THRESHOLD=${FIDLIB_FFT_THRESHOLD})
  # Optional FFTW3 backend (higher performance than built-in Radix-2)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(FFTW3 QUIET fftw3)
  endif()
  if(FFTW3_FOUND)
    target_compile_definitions(fidlib PRIVATE FIDLIB_FFT_FFTW3)
    target_link_libraries(fidlib PRIVATE ${FFTW3_LIBRARIES})
    target_include_directories(fidlib PRIVATE ${FFTW3_INCLUDE_DIRS})
    message(STATUS "fidlib FFT: Overlap-Save + FFTW3 ${FFTW3_VERSION}")
    message(STATUS "LICENSE NOTE: FFTW3 is GPL-2.0-or-later — any binary linking fidlib+FFTW3 is subject to GPL 2.0 terms.")
  else()
    message(STATUS "fidlib FFT: Overlap-Save + built-in Radix-2 (FFTW3 not found)")
  endif()
endif()

# ── Vulkan Compute: GPU FIR for long FIR filters (FP32, VideoCore VII / desktop)
option(FIDLIB_VULKAN "Vulkan Compute backend for GPU FIR acceleration (FP32)" OFF)
set(FIDLIB_VULKAN_THRESHOLD 256 CACHE STRING
    "FIR tap threshold for automatic Vulkan switching")
set(FIDLIB_VULKAN_BATCH 256 CACHE STRING
    "Vulkan batch size in samples (latency vs. dispatch overhead)")
if(FIDLIB_VULKAN)
  find_package(Vulkan QUIET)
  if(NOT Vulkan_FOUND)
    message(WARNING "fidlib VULKAN: Vulkan not found — FIDLIB_VULKAN disabled "
                    "(apt install libvulkan-dev)")
    set(FIDLIB_VULKAN OFF CACHE BOOL "" FORCE)
  else()
    # Find shader compiler: glslc (preferred) or glslangValidator
    find_program(GLSLC_EXECUTABLE NAMES glslc)
    find_program(GLSLANG_EXECUTABLE NAMES glslangValidator)
    if(GLSLC_EXECUTABLE)
      set(_SHADER_COMPILER "${GLSLC_EXECUTABLE}")
      set(_SHADER_FLAGS    "-fshader-stage=comp" "--target-env=vulkan1.0")
    elseif(GLSLANG_EXECUTABLE)
      set(_SHADER_COMPILER "${GLSLANG_EXECUTABLE}")
      set(_SHADER_FLAGS    "-V")
    else()
      message(WARNING "fidlib VULKAN: no GLSL compiler (glslc/glslangValidator) found — "
                      "shader cannot be compiled; FIDLIB_VULKAN disabled")
      set(FIDLIB_VULKAN OFF CACHE BOOL "" FORCE)
    endif()
  endif()
endif()
if(FIDLIB_VULKAN)
  message(WARNING
    "LICENSE: FIDLIB_VULKAN=ON links Vulkan headers (Khronos, Apache-2.0). "
    "Apache-2.0 is FSF-incompatible with GPL-2.0-only. "
    "Binaries linking fidlib+Vulkan together with fiview or firun (GPL-2.0-only) "
    "CANNOT be legally redistributed. "
    "Permitted: fidlib alone as LGPL-2.1 library with FIDLIB_VULKAN=ON (without fiview/firun). "
    "Details: doc/legal/lizenz-analyse.md section 2.3.")
  # Compile GLSL compute shader to SPIR-V and embed as C array
  set(_SHADER_SRC  "${CMAKE_CURRENT_SOURCE_DIR}/fir_dot.comp")
  set(_SHADER_SPV  "${CMAKE_CURRENT_BINARY_DIR}/fir_dot.spv")
  set(_SHADER_HDR  "${CMAKE_CURRENT_BINARY_DIR}/fir_dot_spv.h")
  add_custom_command(
    OUTPUT  "${_SHADER_SPV}"
    COMMAND "${_SHADER_COMPILER}" ${_SHADER_FLAGS} -o "${_SHADER_SPV}" "${_SHADER_SRC}"
    DEPENDS "${_SHADER_SRC}"
    COMMENT "Compiling fir_dot.comp -> SPIR-V"
  )
  add_custom_command(
    OUTPUT  "${_SHADER_HDR}"
    COMMAND ${CMAKE_COMMAND}
            -D INPUT="${_SHADER_SPV}"
            -D OUTPUT="${_SHADER_HDR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/spv_to_header.cmake"
    DEPENDS "${_SHADER_SPV}"
    COMMENT "Embedding SPIR-V as C array -> fir_dot_spv.h"
  )
  # Create INTERFACE target for the generated header
  add_custom_target(fir_dot_spv_header DEPENDS "${_SHADER_HDR}")
  add_dependencies(fidlib fir_dot_spv_header)

  target_compile_definitions(fidlib PUBLIC
      FIDLIB_VULKAN
      FIDLIB_VULKAN_THRESHOLD=${FIDLIB_VULKAN_THRESHOLD}
      FIDLIB_VULKAN_BATCH=${FIDLIB_VULKAN_BATCH})
  target_include_directories(fidlib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(fidlib PRIVATE Vulkan::Vulkan)
  message(STATUS "fidlib VULKAN: Vulkan ${Vulkan_VERSION}, "
                 "Batch=${FIDLIB_VULKAN_BATCH}, Threshold=${FIDLIB_VULKAN_THRESHOLD}")
endif()

# ── OpenCL: GPU FIR for desktop / Jetson (FP32; RPi5 requires Rusticl-Mesa)
# Standard RPi5: Clover returns 0 devices -> FIDLIB_OPENCL gives no GPU benefit there.
# Set explicitly: -DFIDLIB_OPENCL=ON (e.g. desktop PC, NVIDIA Jetson).
option(FIDLIB_OPENCL "OpenCL backend for GPU FIR (desktop/Jetson; RPi5 needs Rusticl)" OFF)
set(FIDLIB_OPENCL_THRESHOLD 256 CACHE STRING
    "FIR tap threshold for automatic OpenCL switching")
set(FIDLIB_OPENCL_BATCH 256 CACHE STRING
    "OpenCL batch size in samples")
if(FIDLIB_OPENCL)
  find_package(OpenCL QUIET)
  if(NOT OpenCL_FOUND)
    message(WARNING "fidlib OPENCL: no OpenCL development files found — "
                    "FIDLIB_OPENCL disabled (apt install opencl-headers ocl-icd-opencl-dev)")
    set(FIDLIB_OPENCL OFF CACHE BOOL "" FORCE)
  else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
      message(WARNING "fidlib OPENCL: aarch64 without Rusticl has no GPU OpenCL devices. "
                      "FIDLIB_OPENCL is active, but on a standard RPi5 the runtime init "
                      "finds no compute device and falls back to NEON/scalar.")
    endif()
    target_compile_definitions(fidlib PUBLIC
        FIDLIB_OPENCL
        FIDLIB_OPENCL_THRESHOLD=${FIDLIB_OPENCL_THRESHOLD}
        FIDLIB_OPENCL_BATCH=${FIDLIB_OPENCL_BATCH})
    target_link_libraries(fidlib PRIVATE OpenCL::OpenCL)
    message(STATUS "fidlib OPENCL: OpenCL ${OpenCL_VERSION_STRING}, "
                   "Batch=${FIDLIB_OPENCL_BATCH}, Threshold=${FIDLIB_OPENCL_THRESHOLD}")
  endif()
endif()
