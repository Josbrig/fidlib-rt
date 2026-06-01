# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-2026 Jörg Simbrig
#
# fiview2 — Reimagined Filter Design Workbench
# All dependencies are fetched via FetchContent — no system packages required
# beyond OpenGL (always system-provided).

cmake_minimum_required(VERSION 3.20)

# ── Options ───────────────────────────────────────────────────────────────────

option(FIVIEW2_DOCKING
    "Dear ImGui Docking-Branch verwenden (empfohlen)" ON)
option(FIVIEW2_AUDIO
    "Audio-Backend aktivieren (PortAudio via FetchContent)" OFF)

if(NOT BUILD_FIVIEW2)
    return()
endif()

# ── FetchContent setup ────────────────────────────────────────────────────────

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ── Dear ImGui ────────────────────────────────────────────────────────────────

set(_IMGUI_REPO "https://github.com/ocornut/imgui.git")

# v1.92.6-docking commit SHA  (FIVIEW2_DOCKING=ON)
# v1.92.6        commit SHA  (FIVIEW2_DOCKING=OFF)
if(FIVIEW2_DOCKING)
    set(_IMGUI_SHA "2a1b69f05748ad909f03acf4533447cac1331611")
else()
    set(_IMGUI_SHA "6ded5230d043aa32c755e65c910c2af5002fb9f9")
endif()

FetchContent_Declare(imgui
    GIT_REPOSITORY ${_IMGUI_REPO}
    GIT_TAG        ${_IMGUI_SHA}
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(imgui)

# ── GLFW ── find_package first; FetchContent fallback ────────────────────────

find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "glfw3 not found via find_package — fetching via FetchContent")
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        URL      https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
        URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
    )
    FetchContent_MakeAvailable(glfw)
endif()

# ── OpenGL ────────────────────────────────────────────────────────────────────

find_package(OpenGL REQUIRED)

# ── Dear ImGui static library ─────────────────────────────────────────────────

add_library(imgui_core STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
if(FIVIEW2_DOCKING)
    target_sources(imgui_core PRIVATE ${imgui_SOURCE_DIR}/imgui_internal.h)
endif()
target_include_directories(imgui_core SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_core PUBLIC OpenGL::GL glfw)
target_compile_options(imgui_core PRIVATE -w)  # suppress warnings in vendor code
if(FIVIEW2_DOCKING)
    target_compile_definitions(imgui_core PUBLIC IMGUI_ENABLE_DOCKING)
endif()

# ── nlohmann/json ─────────────────────────────────────────────────────────────

FetchContent_Declare(json
    URL      https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# ── Eigen3 ── find_package first; FetchContent fallback ──────────────────────

find_package(Eigen3 3.3 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
    message(STATUS "Eigen3 not found — fetching via FetchContent")
    FetchContent_Declare(eigen3
        URL      https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
        URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
    )
    set(EIGEN_BUILD_DOC       OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING         OFF CACHE BOOL "" FORCE)
    set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(eigen3)
    if(NOT TARGET Eigen3::Eigen)
        add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
        set_target_properties(Eigen3::Eigen PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${eigen3_SOURCE_DIR}")
    endif()
endif()

# ── KissFFT (spectrum analysis) ───────────────────────────────────────────────

FetchContent_Declare(kissfft
    URL      https://github.com/mborgerding/kissfft/archive/refs/tags/v131.tar.gz
    URL_HASH SHA256=a4f28cac6da18a5af7726a442416de87a6a3108fa12a59e101fa2f23defe4cf7
)
FetchContent_MakeAvailable(kissfft)

# KissFFT v131 has no CMakeLists — build it ourselves
add_library(kissfft_float STATIC
    ${kissfft_SOURCE_DIR}/kiss_fft.c
    ${kissfft_SOURCE_DIR}/tools/kiss_fftr.c
)
target_include_directories(kissfft_float PUBLIC
    ${kissfft_SOURCE_DIR}
    ${kissfft_SOURCE_DIR}/tools
)
target_compile_options(kissfft_float PRIVATE -w)
add_library(kissfft::kissfft-float ALIAS kissfft_float)

# ── PortAudio (optional audio backend) ───────────────────────────────────────

if(FIVIEW2_AUDIO)
    FetchContent_Declare(portaudio
        GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
        GIT_TAG        147dd722548358763a8b649b3e4b41dfffbcfbb6  # v19.7.0
        GIT_SHALLOW    FALSE
    )
    set(PA_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(portaudio)
endif()

# ── imtile (Cockpit-Layout-Library) ──────────────────────────────────────────

FetchContent_Declare(imtile
    GIT_REPOSITORY https://github.com/Josbrig/imtile.git
    GIT_TAG        9b787e8f96e72cefc3762889b48d9f1a55f0a037
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(imtile)

# ── fiview2 sources ───────────────────────────────────────────────────────────

set(FIVIEW2_SRCS
    src/main.cpp
    src/app.cpp
    src/filter_state.cpp
    src/serialization.cpp
    src/panels/freq_plot.cpp
    src/panels/pz_plot.cpp
    src/panels/param_panel.cpp
    src/panels/cascade_panel.cpp
    src/panels/export_panel.cpp
    src/panels/impulse_panel.cpp
    src/panels/compare_view.cpp
    src/panels/stability_panel.cpp
    src/util/math_utils.cpp
    src/audio/audio_backend.cpp   # provides NullBackend even when FIVIEW2_AUDIO=OFF
    src/audio/spectrum.cpp
)

add_executable(fiview2 ${FIVIEW2_SRCS})

set_property(TARGET fiview2 PROPERTY CXX_STANDARD 20)
set_property(TARGET fiview2 PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET fiview2 PROPERTY CXX_EXTENSIONS OFF)

target_include_directories(fiview2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(fiview2 PRIVATE
    imgui_core
    imtile
    fidlib
    fidgen_lib
    nlohmann_json::nlohmann_json
    Eigen3::Eigen
    kissfft::kissfft-float
    OpenGL::GL
    glfw
)

if(FIVIEW2_AUDIO)
    target_link_libraries(fiview2 PRIVATE PortAudio::PortAudio)
endif()

target_compile_definitions(fiview2 PRIVATE
    FIVIEW2_VERSION="0.1.0"
    $<$<BOOL:${FIVIEW2_DOCKING}>:IMGUI_ENABLE_DOCKING>
    $<$<BOOL:${FIVIEW2_AUDIO}>:FIVIEW2_AUDIO_ENABLED>
)

target_compile_options(fiview2 PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Wno-error
)

set_target_properties(fiview2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "fiview2: imgui=${_IMGUI_SHA} docking=${FIVIEW2_DOCKING} audio=${FIVIEW2_AUDIO}")
