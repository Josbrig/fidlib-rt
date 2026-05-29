cmake_minimum_required(VERSION 3.16)
project(digitalfilterdesign C CXX)

# ── Licence overview ─────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=================================================================")
message(STATUS "  Licence overview — digitalfilterdesign")
message(STATUS "  fidlib (core library)     LGPL-2.1-or-later")
message(STATUS "  fiview, firun (tools)     GPL-2.0-only  ('version 2' only!)")
message(STATUS "  CMake build, tests        GPL-2.0-only")
message(STATUS "  Documentation/Manuals     CC-BY-SA-4.0")
message(STATUS "  Licence texts             COPYING (GPL-2)  COPYING_LIB (LGPL-2.1)")
message(STATUS "-----------------------------------------------------------------")
message(STATUS "  WARNING: FIDLIB_FFT=ON + FFTW3    -> binary becomes GPL-2.0-or-later")
message(STATUS "  WARNING: FIDLIB_VULKAN=ON          -> Apache-2.0 + GPL-2.0 incompatible")
message(STATUS "  Details: doc/legal/lizenz-analyse.md")
message(STATUS "=================================================================")
message(STATUS "")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_TOOLS       "Build firun CLI tool"                ON)
option(BUILD_FIVIEW      "Build fiview visualiser"             ON)
option(BUILD_TESTS       "Build test suite"                    ON)
option(ENABLE_SANITIZERS "Enable ASan + UBSan"                 OFF)
option(CXX20_COMPAT      "Compile all components with C++20"   ON)

if(CXX20_COMPAT)
  set(FIDLIB_CXX20_COMPAT ON CACHE BOOL "" FORCE)
  set(FIRUN_CXX20_COMPAT  ON CACHE BOOL "" FORCE)
  set(FIVIEW_CXX20_COMPAT ON CACHE BOOL "" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # executables
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # shared libs (*.so)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # static libs (*.a)

if(MSVC)
  add_compile_options(/W3 /WX /wd4996)   # 4996: deprecated POSIX names
else()
  add_compile_options(
    -Wall -Wextra -Wconversion -Wshadow -Werror
    -Wpedantic
    $<$<AND:$<COMPILE_LANGUAGE:C>,$<NOT:$<BOOL:${CXX20_COMPAT}>>>:-Wstrict-prototypes>
    $<$<AND:$<COMPILE_LANGUAGE:C>,$<NOT:$<BOOL:${CXX20_COMPAT}>>>:-Wmissing-prototypes>
    -Wformat=2
    -Wnull-dereference
    -Wdouble-promotion
    -Wundef
  )
endif()

if(ENABLE_SANITIZERS)
  if(MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

add_subdirectory(fidlib)

# ── Doxygen ───────────────────────────────────────────────────────────────

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
  set(DOXYGEN_IN  ${CMAKE_SOURCE_DIR}/doc/Doxyfile.in)
  set(DOXYGEN_OUT ${CMAKE_BINARY_DIR}/Doxyfile)
  configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/doc/html)

  add_custom_target(doxygen
    COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
  message(STATUS "Doxygen ${DOXYGEN_VERSION} found — target 'doxygen' available")
  if(DOXYGEN_DOT_FOUND)
    message(STATUS "  dot (Graphviz) found — full graph support enabled")
  else()
    message(WARNING "  dot (Graphviz) not found — call/include graphs disabled")
  endif()
else()
  message(STATUS "Doxygen not found — target 'doxygen' not available")
endif()

if(BUILD_TOOLS)
  add_subdirectory(firun)
endif()

if(BUILD_FIVIEW)
  add_subdirectory(fiview)
endif()

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
