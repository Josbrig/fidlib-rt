# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Jörg Simbrig (cmake build system)
# fiview sources: Copyright (C) 2002-2003 Jim Peters <jim@uazu.net>, GPL-2.0-only
include(ExternalProject)

option(FIVIEW_USE_SDL2      "Build fiview against SDL2 instead of SDL 1.2"    ON)
option(FIVIEW_CXX20_COMPAT  "Compile fiview with C++20 compiler"     ON)
set(_SDL2_REPO  "https://github.com/libsdl-org/SDL.git")
set(_SDL12_REPO "https://github.com/libsdl-org/SDL-1.2.git")

# P1: Platform-aware SDL extra link libraries
if(APPLE)
  # SDL handles Cocoa/Metal internally; no X11 on modern macOS
  set(_SDL_EXTRA_BASE m pthread)
elseif(WIN32)
  # SDL Windows build links its own system dependencies
  set(_SDL_EXTRA_BASE "")
else()
  # Linux / generic UNIX: X11 backend
  set(_SDL_EXTRA_BASE m pthread dl X11 Xext)
endif()

if(FIVIEW_USE_SDL2)
  ExternalProject_Add(SDL2_ext
    GIT_REPOSITORY ${_SDL2_REPO}
    GIT_TAG        c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc  # release-2.30.9
    GIT_SHALLOW    TRUE

    UPDATE_COMMAND ""

    LOG_CONFIGURE ON
    LOG_BUILD     ON
    LOG_INSTALL   ON

    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DSDL_STATIC=ON
      -DSDL_SHARED=OFF
      -DSDL_TESTS=OFF
  )
  ExternalProject_Get_Property(SDL2_ext install_dir)
  set(SDL_INCLUDE_DIR ${install_dir}/include)
  set(SDL_LIB        ${install_dir}/lib/libSDL2.a)
  set(SDL_DEP        SDL2_ext)
  set(SDL_CDEF       T_SDL2)
  set(SDL_EXTRA_LIBS ${_SDL_EXTRA_BASE})
else()
  # Detect host architecture for autotools --build flag
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env uname -m
    OUTPUT_VARIABLE HOST_ARCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT HOST_ARCH)
    set(HOST_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()

  # P5: platform-aware --build triple and config.sub path
  if(APPLE)
    set(SDL_BUILD_TRIPLE "${HOST_ARCH}-apple-darwin")
    # Homebrew automake (arm64 or x86_64), fallback to system path
    find_file(_CONFIG_SUB config.sub
      PATHS
        /opt/homebrew/share/automake-1.17
        /opt/homebrew/share/automake-1.16
        /usr/local/share/automake-1.17
        /usr/local/share/automake-1.16
        /opt/local/share/automake-1.17
        /usr/share/misc
      NO_DEFAULT_PATH)
    find_file(_CONFIG_GUESS config.guess
      PATHS
        /opt/homebrew/share/automake-1.17
        /opt/homebrew/share/automake-1.16
        /usr/local/share/automake-1.17
        /usr/local/share/automake-1.16
        /opt/local/share/automake-1.17
        /usr/share/misc
      NO_DEFAULT_PATH)
    if(NOT _CONFIG_SUB)
      message(WARNING "config.sub not found; SDL 1.2 configure may fail. Install automake via Homebrew.")
      set(_CONFIG_SUB /usr/share/misc/config.sub)
      set(_CONFIG_GUESS /usr/share/misc/config.guess)
    endif()
  else()
    set(SDL_BUILD_TRIPLE "${HOST_ARCH}-linux-gnu")
    set(_CONFIG_SUB   /usr/share/misc/config.sub)
    set(_CONFIG_GUESS /usr/share/misc/config.guess)
  endif()

  ExternalProject_Add(SDL12_ext
    GIT_REPOSITORY ${_SDL12_REPO}
    GIT_TAG        457d4e55ffe1b6ad4c4fa4559dbda8360bf8253d  # release-1.2.15
    GIT_SHALLOW    TRUE

    UPDATE_COMMAND ""

    LOG_CONFIGURE ON   # SDL autotools warnings → build_dir/SDL12_ext-prefix/.../LastConfigure.log
    LOG_BUILD     ON   # SDL compiler warnings  → .../LastBuild.log
    LOG_INSTALL   ON

    PATCH_COMMAND
      ${CMAKE_COMMAND} -E copy ${_CONFIG_SUB}   <SOURCE_DIR>/build-scripts/config.sub
      COMMAND
      ${CMAKE_COMMAND} -E copy ${_CONFIG_GUESS} <SOURCE_DIR>/build-scripts/config.guess
      COMMAND
      sed -i "s/register long \\*data,unsigned len/const long *data,unsigned len/g"
        <SOURCE_DIR>/src/video/x11/SDL_x11sym.h

    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> sh autogen.sh
      COMMAND
        <SOURCE_DIR>/configure
          --prefix=<INSTALL_DIR>
          --build=${SDL_BUILD_TRIPLE}
          --disable-shared
          --enable-static
          "CFLAGS=-Wno-deprecated-declarations"

    BUILD_COMMAND   make -j
    INSTALL_COMMAND make install
  )
  ExternalProject_Get_Property(SDL12_ext install_dir)
  set(SDL_INCLUDE_DIR ${install_dir}/include)
  set(SDL_LIB        ${install_dir}/lib/libSDL.a)
  set(SDL_DEP        SDL12_ext)
  set(SDL_CDEF       "")
  set(SDL_EXTRA_LIBS ${_SDL_EXTRA_BASE})
  message(STATUS
    "LICENSE: SDL 1.2 ist LGPL-2.1-or-later. Statisches Linken verpflichtet zur "
    "Weitergabe von Objektdateien oder Build-Anleitung (LGPL §6), damit Nutzer SDL "
    "austauschen koennen. Quellcode + CMake-Build in diesem Repo erfuellen diese Pflicht. "
    "Weiterverteilung von Binaries: Objektdateien beilegen oder auf dieses Repo verweisen.")
endif()

# fiview includes <fidlib/fidlib.h> — redirect to our lib target
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fidlib)
configure_file(
  ${CMAKE_SOURCE_DIR}/fidlib/fidlib.h
  ${CMAKE_CURRENT_BINARY_DIR}/fidlib/fidlib.h
  COPYONLY
)

set(FIVIEW_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(FIVIEW_SOURCES
  ${FIVIEW_SRC}/display.c
  ${FIVIEW_SRC}/filter.c
  ${FIVIEW_SRC}/graphics.c
  ${FIVIEW_SRC}/helptext.c
  ${FIVIEW_SRC}/scratch.c
  ${FIVIEW_SRC}/fiview.c
)

add_executable(fiview ${FIVIEW_SOURCES})
add_dependencies(fiview ${SDL_DEP})

# P2: platform macro — each target platform gets exactly one T_* define
if(APPLE)
  target_compile_definitions(fiview PRIVATE T_APPLE _POSIX_C_SOURCE=200809L)
elseif(WIN32)
  if(MSVC)
    target_compile_definitions(fiview PRIVATE T_MSVC)
  else()
    target_compile_definitions(fiview PRIVATE T_MINGW)
  endif()
else()
  target_compile_definitions(fiview PRIVATE T_LINUX _POSIX_C_SOURCE=200809L)
endif()

if(SDL_CDEF)
  target_compile_definitions(fiview PRIVATE ${SDL_CDEF})
endif()

target_compile_options(fiview PRIVATE
  -Wall -Wextra -Wconversion -Wshadow -Werror
  -Wpedantic
  $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
  $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
  -Wformat=2
  -Wnull-dereference
  -Wdouble-promotion
  -Wundef
)

# graphics.c: SDL 1.2 API uses Uint32/Uint16/Uint8 — int sign conversions are inherent;
# local 'curr' variable shadows global intentionally (Jim Peters code pattern)
set_source_files_properties(${FIVIEW_SRC}/graphics.c PROPERTIES
  COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion;-Wno-float-conversion;-Wno-shadow"
)
# helptext.c: long help string exceeds ISO C99 minimum string literal length
set_source_files_properties(${FIVIEW_SRC}/helptext.c PROPERTIES
  COMPILE_OPTIONS "-Wno-overlength-strings"
)

if(FIVIEW_CXX20_COMPAT)
  set_source_files_properties(${FIVIEW_SOURCES} PROPERTIES LANGUAGE CXX)
  target_compile_options(fiview PRIVATE -std=c++20 -fpermissive)
endif()

target_include_directories(fiview PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}    # fidlib/fidlib.h redirect
  ${FIVIEW_SRC}                  # all.h, proto.h
  ${SDL_INCLUDE_DIR}             # SDL/SDL.h or SDL2/SDL.h
)

target_link_libraries(fiview PRIVATE
  fidlib
  ${SDL_LIB}
  ${SDL_EXTRA_LIBS}
)
