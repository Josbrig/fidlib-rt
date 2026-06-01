# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-2026 Jörg Simbrig
#
# Web build — Emscripten WASM module for fiview2
# Requires: emcmake cmake -DBUILD_WEB=ON ...

cmake_minimum_required(VERSION 3.16)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "web/CMakeLists.txt must be built with Emscripten toolchain")
endif()

# ── Sources ───────────────────────────────────────────────────────────────────

set(WASM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_api.cpp
    ${CMAKE_SOURCE_DIR}/fiview2/src/filter_state.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/filter_descriptor.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/generator_factory.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/c99_generator.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/cpp20_generator.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/python_generator.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/rust_generator.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/matlab_generator.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/julia_generator.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/verilog_generator.cpp
    ${CMAKE_SOURCE_DIR}/fidgen/src/fidgen_c_api.cpp
)

# ── fidlib as pure-C static library ──────────────────────────────────────────

add_library(fidlib_web STATIC ${CMAKE_SOURCE_DIR}/fidlib/fidlib.c)
set_property(TARGET fidlib_web PROPERTY LANGUAGE C)
set_property(TARGET fidlib_web PROPERTY C_STANDARD 99)
target_include_directories(fidlib_web PUBLIC ${CMAKE_SOURCE_DIR}/fidlib)
target_compile_definitions(fidlib_web PRIVATE RF_CMDLIST VERSION="0.9.11")
target_compile_options(fidlib_web PRIVATE
    -Wno-unused-function -Wno-format-nonliteral
    -Wno-missing-prototypes -Wno-float-conversion
    -Wno-deprecated -Wno-unused-but-set-variable
    -Wno-implicit-int-float-conversion -Wno-sign-conversion
)

# ── Target ────────────────────────────────────────────────────────────────────

add_executable(fiview2_wasm ${WASM_SOURCES})

target_compile_definitions(fiview2_wasm PRIVATE
    RF_CMDLIST
    VERSION="0.9.11"
)

set_property(TARGET fiview2_wasm PROPERTY CXX_STANDARD 20)
set_property(TARGET fiview2_wasm PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET fiview2_wasm PROPERTY CXX_EXTENSIONS OFF)

target_include_directories(fiview2_wasm PRIVATE
    ${CMAKE_SOURCE_DIR}/fidgen/include
    ${CMAKE_SOURCE_DIR}/fiview2/src
    ${CMAKE_SOURCE_DIR}           # so fidgen can #include <fidlib/fidlib.h>
)
target_link_libraries(fiview2_wasm PRIVATE fidlib_web)

# ── Emscripten link flags ─────────────────────────────────────────────────────

set(EM_EXPORTS
    "_wasm_freq_response_json"
    "_wasm_impulse_json"
    "_wasm_step_json"
    "_wasm_poles_zeros_json"
    "_wasm_generate_code"
    "_wasm_build_spec"
    "_wasm_free_string"
    "_wasm_last_error"
    "_wasm_clear_error"
    "_wasm_list_filters"
    "_malloc"
    "_free"
)

string(JOIN "," EM_EXPORTS_STR ${EM_EXPORTS})

target_link_options(fiview2_wasm PRIVATE
    -O2
    "SHELL:-s WASM=1"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=FidlibModule"
    "SHELL:-s EXPORTED_FUNCTIONS=[${EM_EXPORTS_STR}]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"UTF8ToString\",\"stringToUTF8\",\"lengthBytesUTF8\"]"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s ENVIRONMENT=web,node"
    "SHELL:-s SINGLE_FILE=0"
    --no-entry
    -fexceptions
)

target_compile_options(fiview2_wasm PRIVATE
    -O2
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-sign-conversion
    -fexceptions
)

# fidlib.c is C99 — suppress Clang-specific warnings that don't apply

set_target_properties(fiview2_wasm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "fiview2_wasm"
    SUFFIX ".js"
)
