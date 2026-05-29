# Licenses

This repository contains components under different licenses.

## fidlib — LGPL 2.1

The core filter library (`fidlib/`) is released under the
**GNU Lesser General Public License version 2.1 or later**.
Copyright (C) 2002–2004 Jim Peters <http://uazu.net/>.
See `COPYING_LIB` for the full license text.

New extensions to fidlib (`fid_fft.h`, `fid_vulkan.h`, `fid_opencl.h`,
`fid_simd.h`, `fir_dot.comp`, `fir_dot.cl`, `spv_to_header.cmake`) are
released under the same license:
Copyright (C) 2025–2026 Kai Dieki.

## fiview and firun — GPL 2.0

The filter visualizer (`fiview/`) and the CLI filter tool (`firun/`) are
released under the **GNU General Public License version 2**.
Copyright (C) 2002–2004 Jim Peters <http://uazu.net/>.
See `COPYING` for the full license text.

Tests, scripts, and build infrastructure follow the same GPL 2.0 terms.

## Documentation — CC BY-SA 4.0

The manuals under `manuals/` and documents under `doc/` are released under
the **Creative Commons Attribution-ShareAlike 4.0 International License**.
See: https://creativecommons.org/licenses/by-sa/4.0/

## SDL2 (build dependency)

SDL2 is used as an ExternalProject build dependency for fiview.
SDL2 is released under the **zlib license**.
See: https://www.libsdl.org/license.php

## FFTW3 (optional build dependency)

When `FIDLIB_FFT=ON` and `libfftw3-dev` is installed, fidlib links against
FFTW3, which is released under the **GNU General Public License version 2
or later**. This means any binary that links fidlib+FFTW3 is subject to
GPL 2.0 terms. The built-in Radix-2 FFT backend (used when FFTW3 is absent)
carries no additional license restrictions.

## Acknowledgements

The filter design algorithms in `fidlib` are based on techniques from
**mkfilter** by Dr. Tony Fisher (1956–2000), University of York, UK.
No original mkfilter source code remains — the implementation is a complete
rewrite in C by Jim Peters — but the mathematical approach derives from
Fisher's work. The University of York has granted permission for this use.
See `fidlib/fidmkf.h` for the full correspondence.
