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
Copyright (C) 2025–2026 Jörg Simbrig.

## fiview and firun — GPL 2.0

The filter visualizer (`fiview/`) and the CLI filter tool (`firun/`) are
released under the **GNU General Public License version 2**.
Copyright (C) 2002–2004 Jim Peters <http://uazu.net/>.
See `COPYING` for the full license text.

## fidgen and fiview2 — GPL 2.0 or later

The filter code generator (`fidgen/`), the GUI workbench (`fiview2/`), the
WebAssembly browser port (`web/`), and the test suite (`tests/`) are released
under the **GNU General Public License version 2.0 or later**.
Copyright (C) 2025–2026 Jörg Simbrig.
See `COPYING` for the full license text.

Build infrastructure and scripts follow the same GPL 2.0-or-later terms.

## Documentation — CC BY-SA 4.0

The manuals under `manuals/` and documents under `doc/` are released under
the **Creative Commons Attribution-ShareAlike 4.0 International License**.
See: https://creativecommons.org/licenses/by-sa/4.0/

## Third-party build dependencies

| Component | License | Used by |
|---|---|---|
| Dear ImGui | MIT | fiview2 (desktop GUI) |
| GLFW | zlib | fiview2 (OpenGL window) |
| SDL2 | zlib | fiview (legacy), fiview2 optional audio |
| nlohmann/json | MIT | fiview2 state serialisation |
| KissFFT | BSD-3-Clause | fiview2 spectrum analysis |
| Eigen3 | MPL-2.0 | optional inverse filter design |
| imtile | GPL-2.0-or-later | fiview2 cockpit layout |
| FFTW3 (optional) | GPL-2.0-or-later | fidlib overlap-save FFT (`FIDLIB_FFT=ON`) |
| Vulkan SDK headers (optional) | Apache-2.0 | fidlib GPU backend (`FIDLIB_VULKAN=ON`) |

**FFTW3 note:** When `FIDLIB_FFT=ON`, any binary linking fidlib+FFTW3 is subject to
GPL 2.0 terms. The built-in Radix-2 FFT backend carries no additional restrictions.

**Vulkan note:** Apache-2.0 is incompatible with GPL-2.0-only. Avoid enabling
`FIDLIB_VULKAN=ON` in builds that also link `fiview` or `firun` (GPL-2.0-only).
`FIDLIB_VULKAN=ON` is safe with `fiview2` and `fidgen` (GPL-2.0-or-later).

## Acknowledgements

The filter design algorithms in `fidlib` are based on techniques from
**mkfilter** by Dr. Tony Fisher (1956–2000), University of York, UK.
No original mkfilter source code remains — the implementation is a complete
rewrite in C by Jim Peters — but the mathematical approach derives from
Fisher's work. The University of York has granted permission for this use.
See `fidlib/fidmkf.h` for the full correspondence.
