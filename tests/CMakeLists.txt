# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
cmake_minimum_required(VERSION 3.16)

# ── Sanitizer flags (applied to all test targets) ─────────────────────────

if(NOT MSVC)
  set(TEST_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
else()
  set(TEST_SANITIZE_FLAGS "")
endif()

# ── Fixture paths ─────────────────────────────────────────────────────────

set(TEST_FIXTURES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
set(TEST_SUPPORT_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/support)

# ── fiview source paths ───────────────────────────────────────────────────

set(FIVIEW_SRC ${CMAKE_SOURCE_DIR}/fiview/src)

# ── Helper: <fidlib/fidlib.h> redirect (same trick as fiview/CMakeLists.txt)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fidlib)
configure_file(
  ${CMAKE_SOURCE_DIR}/fidlib/fidlib.h
  ${CMAKE_CURRENT_BINARY_DIR}/fidlib/fidlib.h
  COPYONLY
)

# ══════════════════════════════════════════════════════════════════════════
# fiview_test_support — stub implementations (error/warn/Alloc/globals)
# Compiled with the test all.h pre-injected.
# ══════════════════════════════════════════════════════════════════════════

add_library(fiview_test_support STATIC ${TEST_SUPPORT_DIR}/stubs.c)

target_compile_options(fiview_test_support PRIVATE
  -include ${TEST_SUPPORT_DIR}/test_all.h
  -DT_LINUX
  -D_POSIX_C_SOURCE=200809L
  ${TEST_SANITIZE_FLAGS}
  # suppress warnings that come from generated stub code
  -Wno-missing-prototypes
  -Wno-strict-prototypes
)

target_include_directories(fiview_test_support PUBLIC
  ${TEST_SUPPORT_DIR}              # test_all.h (for transitive includes)
  ${FIVIEW_SRC}                    # filter.h, scratch.h, etc.
  ${CMAKE_CURRENT_BINARY_DIR}      # fidlib/fidlib.h redirect
)

target_link_libraries(fiview_test_support PUBLIC fidlib)

if(TEST_SANITIZE_FLAGS)
  target_link_options(fiview_test_support PUBLIC ${TEST_SANITIZE_FLAGS})
endif()


# ══════════════════════════════════════════════════════════════════════════
# Macro: add_fiview_module_test
# Compiles the test + listed fiview sources with test_all.h injection.
# ══════════════════════════════════════════════════════════════════════════

macro(add_fiview_module_test target src)
  set(_extra_srcs ${ARGN})

  add_executable(${target} ${src} ${_extra_srcs})

  target_compile_options(${target} PRIVATE
    -include ${TEST_SUPPORT_DIR}/test_all.h
    -DT_LINUX
    -D_POSIX_C_SOURCE=200809L
    ${TEST_SANITIZE_FLAGS}
    # fiview source-specific suppressions
    -Wno-overlength-strings
    -Wno-missing-prototypes
    -Wno-strict-prototypes
  )

  target_include_directories(${target} PRIVATE
    ${TEST_SUPPORT_DIR}
    ${FIVIEW_SRC}
    ${CMAKE_CURRENT_BINARY_DIR}    # fidlib/fidlib.h
  )

  target_link_libraries(${target} PRIVATE fiview_test_support)

  if(TEST_SANITIZE_FLAGS)
    target_link_options(${target} PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()

  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )
endmacro()


# ══════════════════════════════════════════════════════════════════════════
# Test targets
# ══════════════════════════════════════════════════════════════════════════

# ── 1. butterworth_smoke (existing, preserved) ────────────────────────────

add_executable(test_butterworth test_butterworth.c)
target_link_libraries(test_butterworth PRIVATE fidlib)
target_compile_definitions(test_butterworth PRIVATE _POSIX_C_SOURCE=200809L)
if(TEST_SANITIZE_FLAGS)
  target_compile_options(test_butterworth PRIVATE ${TEST_SANITIZE_FLAGS})
  target_link_options(test_butterworth PRIVATE ${TEST_SANITIZE_FLAGS})
endif()
set_target_properties(test_butterworth PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME butterworth_smoke COMMAND test_butterworth)
set_tests_properties(butterworth_smoke PROPERTIES LABELS "unit;fidlib")


# ── 2. test_fidlib_api ────────────────────────────────────────────────────

add_executable(test_fidlib_api test_fidlib_api.c)
target_link_libraries(test_fidlib_api PRIVATE fidlib)
target_compile_definitions(test_fidlib_api PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(test_fidlib_api PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}   # fidlib/fidlib.h
)
if(TEST_SANITIZE_FLAGS)
  target_compile_options(test_fidlib_api PRIVATE ${TEST_SANITIZE_FLAGS})
  target_link_options(test_fidlib_api PRIVATE ${TEST_SANITIZE_FLAGS})
endif()
set_target_properties(test_fidlib_api PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME fidlib_api COMMAND test_fidlib_api)
set_tests_properties(fidlib_api PROPERTIES
  LABELS "unit;fidlib"
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)


# ── 3. test_scratch ──────────────────────────────────────────────────────

add_fiview_module_test(test_scratch
  test_scratch.c
  ${FIVIEW_SRC}/scratch.c
)
add_test(NAME scratch COMMAND test_scratch)
set_tests_properties(scratch PROPERTIES LABELS "unit;fiview")


# ── 4. test_filter_analysis ───────────────────────────────────────────────

add_fiview_module_test(test_filter_analysis
  test_filter_analysis.c
  ${FIVIEW_SRC}/filter.c
  ${FIVIEW_SRC}/scratch.c
)
add_test(NAME filter_analysis COMMAND test_filter_analysis)
set_tests_properties(filter_analysis PROPERTIES
  LABELS "unit;fiview;numerical"
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)


# ── 5. test_filter_load ───────────────────────────────────────────────────

add_fiview_module_test(test_filter_load
  test_filter_load.c
  ${FIVIEW_SRC}/filter.c
  ${FIVIEW_SRC}/scratch.c
)
# Pass fixture path as a compile-time definition
target_compile_definitions(test_filter_load PRIVATE
  FIXTURES_DIR="${TEST_FIXTURES_DIR}"
)
add_test(NAME filter_load
  COMMAND test_filter_load
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(filter_load PROPERTIES
  LABELS "unit;fiview"
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)


# ── 6+. test_fidlib_precision ────────────────────────────────────────────
# FIR precision (FP32/FP64), IIR stability, DC/Nyquist.

add_executable(test_fidlib_precision test_fidlib_precision.c)
target_link_libraries(test_fidlib_precision PRIVATE fidlib)
target_compile_definitions(test_fidlib_precision PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(test_fidlib_precision PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
)
if(FIDLIB_PRECISION_F32 OR FIDLIB_PRECISION STREQUAL "float")
  target_compile_definitions(test_fidlib_precision PRIVATE FIDLIB_PRECISION_F32)
endif()
if(TEST_SANITIZE_FLAGS)
  target_compile_options(test_fidlib_precision PRIVATE ${TEST_SANITIZE_FLAGS})
  target_link_options(test_fidlib_precision PRIVATE ${TEST_SANITIZE_FLAGS})
endif()
set_target_properties(test_fidlib_precision PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME fidlib_precision COMMAND test_fidlib_precision)
set_tests_properties(fidlib_precision PROPERTIES
  LABELS "unit;fidlib;precision"
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)


# ── 6+. test_fidlib_simd ─────────────────────────────────────────────────
# Always runs (boxcar FIR end-to-end); SIMD primitive test only when FIDLIB_SIMD.

add_executable(test_fidlib_simd test_fidlib_simd.c)
target_link_libraries(test_fidlib_simd PRIVATE fidlib)
target_compile_definitions(test_fidlib_simd PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(test_fidlib_simd PRIVATE
  ${CMAKE_SOURCE_DIR}/fidlib       # fid_simd.h direkt einbindbar
  ${CMAKE_CURRENT_BINARY_DIR}      # fidlib/fidlib.h redirect
)
if(FIDLIB_SIMD)
  # FIDLIB_SIMD ist via PUBLIC auf fidlib gesetzt — wird transitiv vererbt.
  # Also: direct inclusion of fid_simd.h in the test needs the same flag.
  target_compile_definitions(test_fidlib_simd PRIVATE FIDLIB_SIMD)
endif()
if(TEST_SANITIZE_FLAGS)
  target_compile_options(test_fidlib_simd PRIVATE ${TEST_SANITIZE_FLAGS})
  target_link_options(test_fidlib_simd PRIVATE ${TEST_SANITIZE_FLAGS})
endif()
set_target_properties(test_fidlib_simd PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME fidlib_simd COMMAND test_fidlib_simd)
set_tests_properties(fidlib_simd PROPERTIES
  LABELS "unit;fidlib;simd"
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)


# ── 7. test_fidlib_fft ───────────────────────────────────────────────────
# Overlap-Save engine correctness. Only meaningful when FIDLIB_FFT is ON;
# the test itself skips gracefully when compiled without FIDLIB_FFT.

add_executable(test_fidlib_fft test_fidlib_fft.c)
target_link_libraries(test_fidlib_fft PRIVATE fidlib)
target_compile_definitions(test_fidlib_fft PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(test_fidlib_fft PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}   # fidlib/fidlib.h redirect
)
if(TEST_SANITIZE_FLAGS)
  target_compile_options(test_fidlib_fft PRIVATE ${TEST_SANITIZE_FLAGS})
  target_link_options(test_fidlib_fft PRIVATE ${TEST_SANITIZE_FLAGS})
endif()
set_target_properties(test_fidlib_fft PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME fidlib_fft COMMAND test_fidlib_fft)
set_tests_properties(fidlib_fft PROPERTIES
  LABELS "unit;fidlib;fft"
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)


# ── 8. test_fidlib_vulkan ─────────────────────────────────────────────────
# Vulkan Compute FIR engine. Skips gracefully when FIDLIB_VULKAN not compiled
# in or when no Vulkan GPU is found at runtime.

add_executable(test_fidlib_vulkan test_fidlib_vulkan.c)
target_link_libraries(test_fidlib_vulkan PRIVATE fidlib)
target_compile_definitions(test_fidlib_vulkan PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(test_fidlib_vulkan PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
)
if(TEST_SANITIZE_FLAGS)
  target_compile_options(test_fidlib_vulkan PRIVATE ${TEST_SANITIZE_FLAGS})
  target_link_options(test_fidlib_vulkan PRIVATE ${TEST_SANITIZE_FLAGS})
endif()
set_target_properties(test_fidlib_vulkan PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME fidlib_vulkan COMMAND test_fidlib_vulkan)
set_tests_properties(fidlib_vulkan PROPERTIES
  LABELS "unit;fidlib;vulkan"
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)


# ── 9. test_fidlib_opencl ─────────────────────────────────────────────────
# OpenCL FIR engine. Skips gracefully when FIDLIB_OPENCL not compiled in.
# When no GPU device is found at runtime, falls back to OLA/scalar (still passes).

add_executable(test_fidlib_opencl test_fidlib_opencl.c)
target_link_libraries(test_fidlib_opencl PRIVATE fidlib)
target_compile_definitions(test_fidlib_opencl PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(test_fidlib_opencl PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
)
if(TEST_SANITIZE_FLAGS)
  target_compile_options(test_fidlib_opencl PRIVATE ${TEST_SANITIZE_FLAGS})
  target_link_options(test_fidlib_opencl PRIVATE ${TEST_SANITIZE_FLAGS})
endif()
set_target_properties(test_fidlib_opencl PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
add_test(NAME fidlib_opencl COMMAND test_fidlib_opencl)
set_tests_properties(fidlib_opencl PROPERTIES
  LABELS "unit;fidlib;opencl"
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)


# ══════════════════════════════════════════════════════════════════════════
# Benchmarks (BUILD_BENCHMARKS=ON; NOT part of ctest)
# ══════════════════════════════════════════════════════════════════════════

option(BUILD_BENCHMARKS "FIR-Backend-Benchmarks bauen (nicht Teil von ctest)" OFF)
if(BUILD_BENCHMARKS)
  add_executable(bench_fir_backends bench_fir_backends.c)
  target_link_libraries(bench_fir_backends PRIVATE fidlib)
  target_compile_definitions(bench_fir_backends PRIVATE
    _POSIX_C_SOURCE=200809L
  )
  target_include_directories(bench_fir_backends PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  # No sanitizers: benchmarks need realistic performance numbers
  set_target_properties(bench_fir_backends PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )
  message(STATUS "Benchmarks: bench_fir_backends → ${CMAKE_BINARY_DIR}/bin/")
endif()


# ── 11. test_firun_sil ───────────────────────────────────────────────────

if(BUILD_TOOLS)
  add_executable(test_firun_sil test_firun_sil.c)
  target_compile_definitions(test_firun_sil PRIVATE
    _POSIX_C_SOURCE=200809L
    FIRUN_BIN="${CMAKE_BINARY_DIR}/bin/firun"
  )
  target_compile_options(test_firun_sil PRIVATE -Wno-missing-prototypes)
  if(TEST_SANITIZE_FLAGS)
    target_compile_options(test_firun_sil PRIVATE ${TEST_SANITIZE_FLAGS})
    target_link_options(test_firun_sil PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()
  set_target_properties(test_firun_sil PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )
  # firun must be built before this test runs
  add_dependencies(test_firun_sil firun)

  add_test(NAME firun_sil
    COMMAND test_firun_sil
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  set_tests_properties(firun_sil PROPERTIES LABELS "sil;integration")
endif()
