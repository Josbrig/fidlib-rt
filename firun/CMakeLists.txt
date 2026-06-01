# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Jörg Simbrig (cmake build system)
# firun source: Copyright (C) 2004 Jim Peters <jim@uazu.net>, GPL-2.0-only
option(FIRUN_CXX20_COMPAT "Compile firun with C++20 compiler" OFF)

add_executable(firun firun.c)

target_link_libraries(firun PRIVATE fidlib)

target_compile_definitions(firun PRIVATE _POSIX_C_SOURCE=200809L)

if(FIRUN_CXX20_COMPAT)
  set_source_files_properties(firun.c PROPERTIES LANGUAGE CXX)
  target_compile_options(firun PRIVATE -std=c++20 -Wno-old-style-cast)
endif()
