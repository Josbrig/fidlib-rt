# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Jörg Simbrig
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
    # fiview source-specific suppressions (legacy Jim Peters code, pre-C99)
    -Wno-overlength-strings
    -Wno-missing-prototypes
    -Wno-strict-prototypes
    -Wno-format-truncation
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
# Always runs (Boxcar-FIR end-to-end); SIMD primitive test only when FIDLIB_SIMD.

add_executable(test_fidlib_simd test_fidlib_simd.c)
target_link_libraries(test_fidlib_simd PRIVATE fidlib)
target_compile_definitions(test_fidlib_simd PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(test_fidlib_simd PRIVATE
  ${CMAKE_SOURCE_DIR}/fidlib       # fid_simd.h direkt einbindbar
  ${CMAKE_CURRENT_BINARY_DIR}      # fidlib/fidlib.h redirect
)
if(FIDLIB_SIMD)
  # FIDLIB_SIMD ist via PUBLIC auf fidlib gesetzt — wird transitiv vererbt.
  # Also: direct inclusion of fid_simd.h in the test requires the same flag.
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


# ── 10. test_fidgen_cpp ───────────────────────────────────────────────────

if(BUILD_FIDGEN)
  add_executable(test_fidgen_cpp test_fidgen_cpp.cpp)
  set_property(TARGET test_fidgen_cpp PROPERTY CXX_STANDARD 20)
  set_property(TARGET test_fidgen_cpp PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET test_fidgen_cpp PROPERTY CXX_EXTENSIONS OFF)
  target_link_libraries(test_fidgen_cpp PRIVATE fidgen_lib)
  target_include_directories(test_fidgen_cpp PRIVATE
    ${CMAKE_SOURCE_DIR}/fidgen/include   # <fidgen/*.hpp>
    ${CMAKE_CURRENT_BINARY_DIR}          # fidlib/fidlib.h redirect
  )
  if(TEST_SANITIZE_FLAGS)
    target_compile_options(test_fidgen_cpp PRIVATE ${TEST_SANITIZE_FLAGS})
    target_link_options(test_fidgen_cpp PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()
  set_target_properties(test_fidgen_cpp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )
  add_test(NAME fidgen_cpp COMMAND test_fidgen_cpp)
  set_tests_properties(fidgen_cpp PROPERTIES
    LABELS "unit;fidgen"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endif()


# ── 11. test_fidgen_integration ──────────────────────────────────────────
# Matrix: 8 Sprachen × 5 Filtertypen + SIMD-Matrix (c99) + C-API-Smoke.

if(BUILD_FIDGEN)
  add_executable(test_fidgen_integration test_fidgen_integration.cpp)
  set_property(TARGET test_fidgen_integration PROPERTY CXX_STANDARD 20)
  set_property(TARGET test_fidgen_integration PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET test_fidgen_integration PROPERTY CXX_EXTENSIONS OFF)
  target_link_libraries(test_fidgen_integration PRIVATE fidgen_lib)
  target_include_directories(test_fidgen_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/fidgen/include
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  if(TEST_SANITIZE_FLAGS)
    target_compile_options(test_fidgen_integration PRIVATE ${TEST_SANITIZE_FLAGS})
    target_link_options(test_fidgen_integration PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()
  set_target_properties(test_fidgen_integration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )
  add_test(NAME fidgen_integration COMMAND test_fidgen_integration)
  set_tests_properties(fidgen_integration PROPERTIES
    LABELS "integration;fidgen"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endif()


# ── 12. test_fidgen_numerical ────────────────────────────────────────────
# Vergleicht FilterDescriptor Direct-Form-II gegen fidlib-Runtime (fid_run).
# Tolerance: max_abs_diff <= 1e-12, max_rel_diff <= 1e-10 over 512 samples.

if(BUILD_FIDGEN)
  add_executable(test_fidgen_numerical test_fidgen_numerical.cpp)
  set_property(TARGET test_fidgen_numerical PROPERTY CXX_STANDARD 20)
  set_property(TARGET test_fidgen_numerical PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET test_fidgen_numerical PROPERTY CXX_EXTENSIONS OFF)
  target_link_libraries(test_fidgen_numerical PRIVATE fidgen_lib)
  target_include_directories(test_fidgen_numerical PRIVATE
    ${CMAKE_SOURCE_DIR}/fidgen/include
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  if(TEST_SANITIZE_FLAGS)
    target_compile_options(test_fidgen_numerical PRIVATE ${TEST_SANITIZE_FLAGS})
    target_link_options(test_fidgen_numerical PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()
  set_target_properties(test_fidgen_numerical PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )
  add_test(NAME fidgen_numerical COMMAND test_fidgen_numerical)
  set_tests_properties(fidgen_numerical PROPERTIES
    LABELS "unit;fidgen;numerical"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endif()


# ── 13. test_fidgen_c99_smoke ────────────────────────────────────────────
# cmake generiert echte C99-Header via fidgen (add_custom_command),
# kompiliert sie und vergleicht Impulsantworten gegen fidlib-Runtime.

if(BUILD_FIDGEN)
  set(_SMOKE_GENDIR ${CMAKE_CURRENT_BINARY_DIR}/fidgen_smoke_gen)
  file(MAKE_DIRECTORY ${_SMOKE_GENDIR})

  foreach(_spec_name IN ITEMS
      "LpBu4/1000:lpbu4_1000:44100"
      "LpBu6/1000:lpbu6_1000:44100"
      "LpBu8/1000:lpbu8_1000:44100"
      "HpBu2/8000:hpbu2_8000:44100"
      "BpBu4/500-2000:bpbu4_500_2000:44100"
      "BsBu4/500-2000:bsbu4_500_2000:44100"
      "LpBe4/1000:lpbe4_1000:44100"
      "LpCh4/-1/1000:lpch4_1000:44100"
      "LpHm/1000:lphm_1000:44100"
      "LpHn/1000:lphn_1000:44100"
      "LpBa/1000:lpba_1000:44100"
  )
    string(REPLACE ":" ";" _parts "${_spec_name}")
    list(GET _parts 0 _spec)
    list(GET _parts 1 _name)
    list(GET _parts 2 _rate)

    add_custom_command(
      OUTPUT  ${_SMOKE_GENDIR}/${_name}.h
      COMMAND fidgen -l c99 -r ${_rate} -n ${_name}
                     -o ${_SMOKE_GENDIR}/${_name}.h
                     ${_spec}
      DEPENDS fidgen
      COMMENT "fidgen: generating ${_name}.h (${_spec} @ ${_rate} Hz)"
      VERBATIM
    )
    list(APPEND _SMOKE_HEADERS ${_SMOKE_GENDIR}/${_name}.h)
  endforeach()

  add_executable(test_fidgen_c99_smoke
    test_fidgen_c99_smoke.c
    ${_SMOKE_HEADERS}
  )
  set_property(TARGET test_fidgen_c99_smoke PROPERTY C_STANDARD 99)
  set_property(TARGET test_fidgen_c99_smoke PROPERTY C_STANDARD_REQUIRED ON)
  set_property(TARGET test_fidgen_c99_smoke PROPERTY C_EXTENSIONS OFF)

  target_include_directories(test_fidgen_c99_smoke PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}    # fidlib/fidlib.h redirect
  )
  target_link_libraries(test_fidgen_c99_smoke PRIVATE fidlib)

  # suppress -Wundef / -Wpedantic for the generated C99 files (compiler options
  # come from the root CMakeLists and may include flags incompatible with C99)
  target_compile_options(test_fidgen_c99_smoke PRIVATE
    -Wno-missing-prototypes
    -Wno-strict-prototypes
  )

  if(TEST_SANITIZE_FLAGS)
    target_compile_options(test_fidgen_c99_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
    target_link_options(test_fidgen_c99_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()
  set_target_properties(test_fidgen_c99_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  add_test(NAME fidgen_c99_smoke COMMAND test_fidgen_c99_smoke)
  set_tests_properties(fidgen_c99_smoke PROPERTIES
    LABELS "integration;fidgen;numerical"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endif()


# ── 14. test_fidgen_cli ───────────────────────────────────────────────────
# Ersetzt scripts/test-fidgen.sh durch echte C++-Tests.
# Meta (version/list-langs/check), alle 8 Sprachen + Aliase, Filtertypen,
# Optionen, SIMD-Inhalte, Dateiausgabe, Fehlerbehandlung.

if(BUILD_FIDGEN)
  add_executable(test_fidgen_cli test_fidgen_cli.cpp)
  set_property(TARGET test_fidgen_cli PROPERTY CXX_STANDARD 20)
  set_property(TARGET test_fidgen_cli PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET test_fidgen_cli PROPERTY CXX_EXTENSIONS OFF)
  target_link_libraries(test_fidgen_cli PRIVATE fidgen_lib)
  target_include_directories(test_fidgen_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/fidgen/include
    ${CMAKE_SOURCE_DIR}/fidgen/src      # cli.hpp
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  if(TEST_SANITIZE_FLAGS)
    target_compile_options(test_fidgen_cli PRIVATE ${TEST_SANITIZE_FLAGS})
    target_link_options(test_fidgen_cli PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()
  set_target_properties(test_fidgen_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )
  add_test(NAME fidgen_cli COMMAND test_fidgen_cli)
  set_tests_properties(fidgen_cli PROPERTIES
    LABELS "unit;fidgen;cli"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endif()


# ── 15. test_fidgen_cpp20_smoke ──────────────────────────────────────────
# cmake generiert echte C++20-Header via fidgen (add_custom_command),
# kompiliert sie und vergleicht Impulsantworten gegen fidlib-Runtime.

if(BUILD_FIDGEN)
  set(_CPP20_SMOKE_GENDIR ${CMAKE_CURRENT_BINARY_DIR}/fidgen_cpp20_smoke_gen)
  file(MAKE_DIRECTORY ${_CPP20_SMOKE_GENDIR})
  unset(_CPP20_SMOKE_HEADERS)

  foreach(_spec_name IN ITEMS
      "LpBu4/1000:lpbu4_1000:44100"
      "LpBu6/1000:lpbu6_1000:44100"
      "LpBu8/1000:lpbu8_1000:44100"
      "HpBu2/8000:hpbu2_8000:44100"
      "BpBu4/500-2000:bpbu4_500_2000:44100"
      "BsBu4/500-2000:bsbu4_500_2000:44100"
      "LpBe4/1000:lpbe4_1000:44100"
      "LpCh4/-1/1000:lpch4_1000:44100"
      "LpHm/1000:lphm_1000:44100"
      "LpHn/1000:lphn_1000:44100"
      "LpBa/1000:lpba_1000:44100"
  )
    string(REPLACE ":" ";" _parts "${_spec_name}")
    list(GET _parts 0 _spec)
    list(GET _parts 1 _name)
    list(GET _parts 2 _rate)

    add_custom_command(
      OUTPUT  ${_CPP20_SMOKE_GENDIR}/${_name}.hpp
      COMMAND fidgen -l cpp20 -r ${_rate} -n ${_name}
                     -o ${_CPP20_SMOKE_GENDIR}/${_name}.hpp
                     ${_spec}
      DEPENDS fidgen
      COMMENT "fidgen: generating ${_name}.hpp (${_spec} @ ${_rate} Hz)"
      VERBATIM
    )
    list(APPEND _CPP20_SMOKE_HEADERS ${_CPP20_SMOKE_GENDIR}/${_name}.hpp)
  endforeach()

  add_executable(test_fidgen_cpp20_smoke
    test_fidgen_cpp20_smoke.cpp
    ${_CPP20_SMOKE_HEADERS}
  )
  set_property(TARGET test_fidgen_cpp20_smoke PROPERTY CXX_STANDARD 20)
  set_property(TARGET test_fidgen_cpp20_smoke PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET test_fidgen_cpp20_smoke PROPERTY CXX_EXTENSIONS OFF)

  target_include_directories(test_fidgen_cpp20_smoke PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}    # fidlib/fidlib.h redirect
  )
  target_link_libraries(test_fidgen_cpp20_smoke PRIVATE fidlib)

  if(TEST_SANITIZE_FLAGS)
    target_compile_options(test_fidgen_cpp20_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
    target_link_options(test_fidgen_cpp20_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()
  set_target_properties(test_fidgen_cpp20_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  add_test(NAME fidgen_cpp20_smoke COMMAND test_fidgen_cpp20_smoke)
  set_tests_properties(fidgen_cpp20_smoke PROPERTIES
    LABELS "integration;fidgen;numerical"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
  )
endif()


# ── 17. test_fidgen_rust_smoke ────────────────────────────────────────────
# cmake generiert Rust-Dateien via fidgen --with-test, kompiliert sie mit rustc
# und vergleicht Impulsantworten (popen) gegen fidlib-Runtime.

if(BUILD_FIDGEN)
  find_program(RUSTC rustc)
  if(RUSTC)
    set(_RUST_SMOKE_GENDIR ${CMAKE_CURRENT_BINARY_DIR}/fidgen_rust_smoke_gen)
    file(MAKE_DIRECTORY ${_RUST_SMOKE_GENDIR})
    unset(_RUST_SMOKE_BINS)

    foreach(_spec_name IN ITEMS
        "LpBu4/1000:lpbu4_1000:44100"
        "LpBu6/1000:lpbu6_1000:44100"
        "LpBu8/1000:lpbu8_1000:44100"
        "HpBu2/8000:hpbu2_8000:44100"
        "BpBu4/500-2000:bpbu4_500_2000:44100"
        "BsBu4/500-2000:bsbu4_500_2000:44100"
        "LpBe4/1000:lpbe4_1000:44100"
        "LpCh4/-1/1000:lpch4_1000:44100"
        "LpHm/1000:lphm_1000:44100"
        "LpHn/1000:lphn_1000:44100"
        "LpBa/1000:lpba_1000:44100"
    )
      string(REPLACE ":" ";" _parts "${_spec_name}")
      list(GET _parts 0 _spec)
      list(GET _parts 1 _name)
      list(GET _parts 2 _rate)

      add_custom_command(
        OUTPUT  ${_RUST_SMOKE_GENDIR}/${_name}.rs
        COMMAND fidgen -l rust --with-test -r ${_rate} -n ${_name}
                       -o ${_RUST_SMOKE_GENDIR}/${_name}.rs
                       ${_spec}
        DEPENDS fidgen
        COMMENT "fidgen: generating ${_name}.rs (${_spec} @ ${_rate} Hz)"
        VERBATIM
      )
      add_custom_command(
        OUTPUT  ${_RUST_SMOKE_GENDIR}/${_name}
        COMMAND ${RUSTC} -o ${_RUST_SMOKE_GENDIR}/${_name}
                         ${_RUST_SMOKE_GENDIR}/${_name}.rs
        DEPENDS ${_RUST_SMOKE_GENDIR}/${_name}.rs
        COMMENT "rustc: compiling ${_name}.rs"
        VERBATIM
      )
      list(APPEND _RUST_SMOKE_BINS ${_RUST_SMOKE_GENDIR}/${_name})
    endforeach()

    add_custom_target(fidgen_rust_smoke_bins ALL
      DEPENDS ${_RUST_SMOKE_BINS}
    )

    add_executable(test_fidgen_rust_smoke test_fidgen_rust_smoke.cpp)
    set_property(TARGET test_fidgen_rust_smoke PROPERTY CXX_STANDARD 20)
    set_property(TARGET test_fidgen_rust_smoke PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(TARGET test_fidgen_rust_smoke PROPERTY CXX_EXTENSIONS OFF)
    target_link_libraries(test_fidgen_rust_smoke PRIVATE fidlib)
    target_include_directories(test_fidgen_rust_smoke PRIVATE
      ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_compile_definitions(test_fidgen_rust_smoke PRIVATE
      RUST_SMOKE_BINDIR="${_RUST_SMOKE_GENDIR}"
    )
    add_dependencies(test_fidgen_rust_smoke fidgen_rust_smoke_bins)

    if(TEST_SANITIZE_FLAGS)
      target_compile_options(test_fidgen_rust_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
      target_link_options(test_fidgen_rust_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
    endif()
    set_target_properties(test_fidgen_rust_smoke PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )

    add_test(NAME fidgen_rust_smoke COMMAND test_fidgen_rust_smoke)
    set_tests_properties(fidgen_rust_smoke PROPERTIES
      LABELS "integration;fidgen;numerical"
      ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
  else()
    message(STATUS "rustc not found — fidgen_rust_smoke skipped")
  endif()
endif()


# ── 18. test_fidgen_python_smoke ──────────────────────────────────────────
# cmake generiert Python-Dateien via fidgen --with-test.
# Test startet sie via popen("python3 <path>") und vergleicht Impulsantworten.

if(BUILD_FIDGEN)
  find_program(PYTHON3 python3)
  if(PYTHON3)
    set(_PY_SMOKE_GENDIR ${CMAKE_CURRENT_BINARY_DIR}/fidgen_python_smoke_gen)
    file(MAKE_DIRECTORY ${_PY_SMOKE_GENDIR})
    unset(_PY_SMOKE_SCRIPTS)

    foreach(_spec_name IN ITEMS
        "LpBu4/1000:lpbu4_1000:44100"
        "LpBu6/1000:lpbu6_1000:44100"
        "LpBu8/1000:lpbu8_1000:44100"
        "HpBu2/8000:hpbu2_8000:44100"
        "BpBu4/500-2000:bpbu4_500_2000:44100"
        "BsBu4/500-2000:bsbu4_500_2000:44100"
        "LpBe4/1000:lpbe4_1000:44100"
        "LpCh4/-1/1000:lpch4_1000:44100"
        "LpHm/1000:lphm_1000:44100"
        "LpHn/1000:lphn_1000:44100"
        "LpBa/1000:lpba_1000:44100"
    )
      string(REPLACE ":" ";" _parts "${_spec_name}")
      list(GET _parts 0 _spec)
      list(GET _parts 1 _name)
      list(GET _parts 2 _rate)

      add_custom_command(
        OUTPUT  ${_PY_SMOKE_GENDIR}/${_name}.py
        COMMAND fidgen -l python --with-test -r ${_rate} -n ${_name}
                       -o ${_PY_SMOKE_GENDIR}/${_name}.py
                       ${_spec}
        DEPENDS fidgen
        COMMENT "fidgen: generating ${_name}.py (${_spec} @ ${_rate} Hz)"
        VERBATIM
      )
      list(APPEND _PY_SMOKE_SCRIPTS ${_PY_SMOKE_GENDIR}/${_name}.py)
    endforeach()

    add_custom_target(fidgen_python_smoke_scripts ALL
      DEPENDS ${_PY_SMOKE_SCRIPTS}
    )

    add_executable(test_fidgen_python_smoke test_fidgen_python_smoke.cpp)
    set_property(TARGET test_fidgen_python_smoke PROPERTY CXX_STANDARD 20)
    set_property(TARGET test_fidgen_python_smoke PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(TARGET test_fidgen_python_smoke PROPERTY CXX_EXTENSIONS OFF)
    target_link_libraries(test_fidgen_python_smoke PRIVATE fidlib)
    target_include_directories(test_fidgen_python_smoke PRIVATE
      ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_compile_definitions(test_fidgen_python_smoke PRIVATE
      PYTHON_SMOKE_GENDIR="${_PY_SMOKE_GENDIR}"
      PYTHON3_BIN="${PYTHON3}"
    )
    add_dependencies(test_fidgen_python_smoke fidgen_python_smoke_scripts)

    if(TEST_SANITIZE_FLAGS)
      target_compile_options(test_fidgen_python_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
      target_link_options(test_fidgen_python_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
    endif()
    set_target_properties(test_fidgen_python_smoke PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )

    add_test(NAME fidgen_python_smoke COMMAND test_fidgen_python_smoke)
    set_tests_properties(fidgen_python_smoke PROPERTIES
      LABELS "integration;fidgen;numerical"
      ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
  else()
    message(STATUS "python3 not found — fidgen_python_smoke skipped")
  endif()
endif()


# ── 19. test_fidgen_simd_smoke ────────────────────────────────────────────
# Generiert c99-Header mit --simd sse2, kompiliert mit -msse2.
# Testet skalaren Pfad + SSE2 2-Kanal-Pfad gegen fidlib-Runtime.
# x86_64 only (SSE2 always available).

if(BUILD_FIDGEN)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_SIMD_SMOKE_GENDIR ${CMAKE_CURRENT_BINARY_DIR}/fidgen_simd_smoke_gen)
    file(MAKE_DIRECTORY ${_SIMD_SMOKE_GENDIR})
    unset(_SIMD_SMOKE_HEADERS)

    foreach(_spec_name IN ITEMS
        "LpBu4/1000:lpbu4_1000_sse2:44100"
        "LpBu6/1000:lpbu6_1000_sse2:44100"
        "LpBu8/1000:lpbu8_1000_sse2:44100"
        "HpBu2/8000:hpbu2_8000_sse2:44100"
        "BpBu4/500-2000:bpbu4_500_2000_sse2:44100"
        "BsBu4/500-2000:bsbu4_500_2000_sse2:44100"
        "LpBe4/1000:lpbe4_1000_sse2:44100"
        "LpCh4/-1/1000:lpch4_1000_sse2:44100"
    )
      string(REPLACE ":" ";" _parts "${_spec_name}")
      list(GET _parts 0 _spec)
      list(GET _parts 1 _name)
      list(GET _parts 2 _rate)

      add_custom_command(
        OUTPUT  ${_SIMD_SMOKE_GENDIR}/${_name}.h
        COMMAND fidgen -l c99 --simd sse2 -r ${_rate} -n ${_name}
                       -o ${_SIMD_SMOKE_GENDIR}/${_name}.h
                       ${_spec}
        DEPENDS fidgen
        COMMENT "fidgen: generating ${_name}.h (${_spec} @ ${_rate} Hz, SSE2)"
        VERBATIM
      )
      list(APPEND _SIMD_SMOKE_HEADERS ${_SIMD_SMOKE_GENDIR}/${_name}.h)
    endforeach()

    add_executable(test_fidgen_simd_smoke
      test_fidgen_simd_smoke.c
      ${_SIMD_SMOKE_HEADERS}
    )
    set_property(TARGET test_fidgen_simd_smoke PROPERTY C_STANDARD 99)
    set_property(TARGET test_fidgen_simd_smoke PROPERTY C_STANDARD_REQUIRED ON)
    set_property(TARGET test_fidgen_simd_smoke PROPERTY C_EXTENSIONS OFF)

    target_include_directories(test_fidgen_simd_smoke PRIVATE
      ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_link_libraries(test_fidgen_simd_smoke PRIVATE fidlib)
    target_compile_options(test_fidgen_simd_smoke PRIVATE
      -msse2
      -Wno-missing-prototypes
      -Wno-strict-prototypes
      -Wno-unused-variable
    )

    if(TEST_SANITIZE_FLAGS)
      target_compile_options(test_fidgen_simd_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
      target_link_options(test_fidgen_simd_smoke PRIVATE ${TEST_SANITIZE_FLAGS})
    endif()
    set_target_properties(test_fidgen_simd_smoke PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )

    add_test(NAME fidgen_simd_smoke COMMAND test_fidgen_simd_smoke)
    set_tests_properties(fidgen_simd_smoke PROPERTIES
      LABELS "integration;fidgen;numerical;simd"
      ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    )
  endif()
endif()


# ── 16. test_firun_sil ───────────────────────────────────────────────────

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


# ── 17. test_fiview2_fuzz ─────────────────────────────────────────────────
# Randomised stress test for FilterState / recompute_slot.
# Hammers all FilterFamily × FilterPassband × order combinations with
# random + edge-case rate/fc values. ASan + UBSan catch any crash.

if(BUILD_FIVIEW2)
  add_executable(test_fiview2_fuzz
    test_fiview2_fuzz.cpp
    ${CMAKE_SOURCE_DIR}/fiview2/src/filter_state.cpp
  )

  set_property(TARGET test_fiview2_fuzz PROPERTY CXX_STANDARD 20)
  set_property(TARGET test_fiview2_fuzz PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET test_fiview2_fuzz PROPERTY CXX_EXTENSIONS OFF)

  target_include_directories(test_fiview2_fuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/fiview2/src   # filter_state.hpp etc.
  )

  target_link_libraries(test_fiview2_fuzz PRIVATE
    fidlib
    fidgen_lib
  )

  target_compile_options(test_fiview2_fuzz PRIVATE
    -Wall -Wextra -Wpedantic
    ${TEST_SANITIZE_FLAGS}
  )

  if(TEST_SANITIZE_FLAGS)
    target_link_options(test_fiview2_fuzz PRIVATE ${TEST_SANITIZE_FLAGS})
  endif()

  set_target_properties(test_fiview2_fuzz PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  add_test(NAME fiview2_fuzz COMMAND test_fiview2_fuzz)
  set_tests_properties(fiview2_fuzz PROPERTIES
    LABELS "fuzz;fiview2"
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
    TIMEOUT 120
  )
endif()
