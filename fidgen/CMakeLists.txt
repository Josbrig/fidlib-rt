# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-2026 Jörg Simbrig
cmake_minimum_required(VERSION 3.16)

# ── C++20 required for this sub-project ──────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── fidlib/fidlib.h redirect ──────────────────────────────────────────────────
# filter_descriptor.cpp includes <fidlib/fidlib.h>.
#
# FIDGEN_STANDALONE=ON: fidlib is provided externally (the 'fidlib' cmake target
# must already exist). Set FIDLIB_INCLUDE_DIR to the directory containing fidlib.h.
# FIDGEN_STANDALONE=OFF (default): fidlib is built by the parent project; copy
# fidlib.h from the well-known path in the source tree.

option(FIDGEN_STANDALONE
    "Build fidgen against an externally provided fidlib target" OFF)

if(FIDGEN_STANDALONE)
    if(NOT TARGET fidlib)
        message(FATAL_ERROR
            "FIDGEN_STANDALONE=ON requires a 'fidlib' cmake target. "
            "Build fidlib first, then add_subdirectory(fidgen) or "
            "provide -DFIDLIB_INCLUDE_DIR and call add_subdirectory before fidgen.")
    endif()
    if(NOT DEFINED FIDLIB_INCLUDE_DIR)
        message(FATAL_ERROR
            "FIDGEN_STANDALONE=ON requires -DFIDLIB_INCLUDE_DIR=<dir-containing-fidlib.h>")
    endif()
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fidlib)
    configure_file(
        ${FIDLIB_INCLUDE_DIR}/fidlib.h
        ${CMAKE_CURRENT_BINARY_DIR}/fidlib/fidlib.h
        COPYONLY
    )
else()
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fidlib)
    configure_file(
        ${CMAKE_SOURCE_DIR}/fidlib/fidlib.h
        ${CMAKE_CURRENT_BINARY_DIR}/fidlib/fidlib.h
        COPYONLY
    )
endif()

# ── FIDGEN_SIMD_FLAGS — compiler flags for projects compiling generated SIMD code
# Auto-detected from host architecture; can be overridden by the user.
# Pass these flags when compiling targets that #include fidgen-generated headers
# with --simd sse2 / --simd avx2 / --simd neon / --simd auto.

if(NOT DEFINED FIDGEN_SIMD_FLAGS)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(FIDGEN_SIMD_FLAGS "-msse2" CACHE STRING
            "Compiler flags to activate SIMD in fidgen-generated code (x86_64: -msse2)")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|armv8")
        # NEON is mandatory on AArch64 — no extra flag needed
        set(FIDGEN_SIMD_FLAGS "" CACHE STRING
            "Compiler flags to activate SIMD in fidgen-generated code (AArch64: NEON builtin)")
    else()
        set(FIDGEN_SIMD_FLAGS "" CACHE STRING
            "Compiler flags to activate SIMD in fidgen-generated code")
    endif()
endif()
message(STATUS "FIDGEN_SIMD_FLAGS = '${FIDGEN_SIMD_FLAGS}' (${CMAKE_SYSTEM_PROCESSOR})")

# ── Sources ───────────────────────────────────────────────────────────────────

set(FIDGEN_LIB_SRCS
    src/filter_descriptor.cpp
    src/c99_generator.cpp
    src/cpp20_generator.cpp
    src/python_generator.cpp
    src/rust_generator.cpp
    src/matlab_generator.cpp
    src/julia_generator.cpp
    src/verilog_generator.cpp
    src/generator_factory.cpp
    src/cli.cpp
    src/fidgen_c_api.cpp
)

# ── fidgen_lib — reusable core (for tests + API integration) ─────────────────

add_library(fidgen_lib STATIC ${FIDGEN_LIB_SRCS})

target_include_directories(fidgen_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # <fidgen/*.hpp>
    ${CMAKE_CURRENT_BINARY_DIR}           # provides fidlib/fidlib.h
)

target_link_libraries(fidgen_lib PUBLIC fidlib)

# GCC 12 false-positive: operator+(const char*, string&&) with large inlined offsets.
# Fixed in GCC 13; suppress here rather than rewrite idiomatic code.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13")
    target_compile_options(fidgen_lib PRIVATE -Wno-restrict)
endif()


# ── fidgen — CLI executable ───────────────────────────────────────────────────

add_executable(fidgen src/main.cpp)
target_link_libraries(fidgen PRIVATE fidgen_lib)

set_target_properties(fidgen PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS fidgen RUNTIME DESTINATION bin)
install(TARGETS fidgen_lib ARCHIVE DESTINATION lib)
install(DIRECTORY include/fidgen DESTINATION include)
