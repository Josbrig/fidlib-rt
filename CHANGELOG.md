# Changelog

All notable changes to this project will be documented in this file.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) — [SemVer](https://semver.org/).

---

## [0.1.2] — 2026-06-01

### Added

**fiview2 — WebAssembly browser port** (`web/`)
- Single-file HTML application (`fiview2.html`) — no server, no installation, works offline
- All 10 filter families, all 4 passbands, all fidlib spec types
- Canvas 2D plots: Frequency Response (log X, dB Y, phase overlay), Impulse/Step Response
  (time axis in ms/s), Poles/Zeros (unit circle, hover tooltip)
- Scroll-wheel zoom + drag-pan + double-click reset on Frequency Response and Impulse plots
- A/B/C/D comparison: freeze up to 4 filter designs; overlaid in all plots
- Stability panel: STABLE/UNSTABLE indicator, pole magnitude bar, FP32 warning at order ≥ 8,
  Order −2 button for unstable filters
- Guided Mode wizard (2-step dialog: use case + problem → sensible filter preset)
- Filter Cascade tab: SOS coefficient table (b0/b1/b2/a1/a2 per stage)
- Export tab: code generation in all 8 languages; Copy + Download buttons;
  textarea fills available panel height
- Persistent state: `localStorage` auto-save/load, JSON file export/import, URL hash sharing
- `web/bundle.py`: packages HTML + WASM as a single self-contained offline file (~600 KB)
- `dist-deploy/`: pre-built WASM artifacts for local development without Emscripten

### Fixed

**fiview2 (desktop + web)**
- `PkBq` (Peaking EQ) spec parameter order was wrong: was `<freq>/<Q>/<gain>`,
  must be `<Q>/<gain>/<freq>` — filter produced incorrect results in both versions
- FIR Hann and Hamming filter names were swapped (key `Hm` = Hamming, `Hn` = Hann)

**fiview2 web**
- Parameter panel vanished when selecting any FIR or special filter type
  (`passband.parentElement` was `panel-body`; fixed with explicit `#passband-row` wrapper)
- FIR spec included a tap count that fidlib does not accept (`LpHm100/fc` → `LpHm/fc`);
  fidlib windowed FIR filters determine tap count from the cutoff frequency automatically
- Order slider and Passband selector are now correctly hidden for FIR filters
  (fidlib only supports LP variants for windowed FIR; no user-settable tap count)

---

## [0.1.1] — 2026-06-01

### Added

**fiview2**
- Frequency Response panel: scroll-wheel zoom and drag-pan on the frequency axis;
  double-click or "Reset Zoom" button to restore full view
- Impulse / Step Response panel: scroll-wheel zoom and drag-pan on the time axis
- Impulse / Step Response panel: time axis (ms / s) and amplitude Y-axis with
  adaptive tick spacing
- Fuzz test `test_fiview2_fuzz`: 29 000+ probes covering all filter family ×
  passband × order combinations plus randomised edge cases; registered in ctest
  (`ctest -L fuzz`)

### Fixed

**fiview2**
- Crash when switching sample rate to a value lower than the current cutoff
  frequency (`fc1 > nyq` after rate change — fidlib received a normalised
  frequency above 0.5)
- Crash when dragging the lower-cutoff slider above the upper cutoff in BP/BS
  mode (inverted band passed to fidlib)
- Crash / silent `exit(1)` for Bessel filters with order > 10 (fidlib hard limit);
  order slider is now capped at 10 for Bessel
- Crash for FIR filters with `fc1 = 0` (division by zero in fidlib → `max =
  INT_MAX` → heap overflow)
- Silent `exit(1)` when `fc2 ≥ Nyquist` in BP/BS mode
- Hang / corrupted output for numerically unstable high-order filters (impulse
  response loop now aborts on non-finite values)
- NULL dereference in `fid_run_newbuf` when `fid_run_new` returned NULL
- Guided Mode wizard did not open when clicking "Open Wizard" (`g_gs.shown` was
  never set to `true`)

---

## [0.1.0] — 2026-05-31

### Added

**fidlib — modernised runtime filter library**
- C99/C++20 clean build (`-Wall -Wextra -Wconversion -Wshadow -Werror`)
- ASan + UBSan clean in Debug mode
- SIMD acceleration for FIR hotpath: NEON (AArch64), SSE2, AVX2
- Overlap-save FFT convolution engine for long FIR filters (optional, `FIDLIB_FFT=ON`)
- Vulkan compute backend for GPU-accelerated FIR (FP32, `FIDLIB_VULKAN=ON`)
- OpenCL backend for GPU-accelerated FIR on desktop / NVIDIA Jetson (`FIDLIB_OPENCL=ON`)
- `extern "C"` guards and `const char *spec` throughout — C++ compatible without hacks
- FetchContent-compatible cmake build; LGPL-2.1-or-later

**fidgen — universal filter code generator**
- Generates runtime-ready filter code from a fidlib spec string
- Output languages: C99, C++20, Python, Rust, MATLAB/Octave, Julia, Verilog, SystemVerilog
- SIMD variants: NEON, SSE2, AVX2, Auto (all three in one file)
- FIR support: windowed FIR (Hann, Hamming, Blackman, Bartlett) fully unrolled
- C API (`fidgen_generate`, `fidgen_free`, `fidgen_version`) for embedding in other tools
- 9 test suites: unit, integration, numerical smoke (C99/C++20/Rust/Python/SIMD), CLI
- Standalone build option (`FIDGEN_STANDALONE=ON`) for use without the full project

**fiview2 — filter design workbench (GUI)**
- Built with Dear ImGui 1.92.6 (docking branch), GLFW, OpenGL 3.3+
- Cockpit-style layout via [imtile](https://github.com/Josbrig/imtile): all 8 panels
  visible simultaneously, pixel-fixed sizes, persistent layout (`fiview2.ini`)
- Parameters panel: slider + numerical input for every parameter, order up to 20,
  context-sensitive labels, direct fidlib spec input (raw mode)
- Frequency Response: auto-scaling Y axis, adaptive dB grid, phase overlay
- Poles / Zeros: unit circle, interactive hover tooltip, A/B comparison overlay
- Impulse / Step Response panel
- Stability panel: Jury criterion, FP32 precision warning
- Filter Cascade: SOS coefficient display
- A/B Comparison: freeze up to 4 filter designs; poles/zeros shown per slot
- fidgen Export: generate code in all 8 languages directly from the GUI
- Guided Mode wizard (2-step filter design)
- JSON serialisation for filter state save/load
- Audio backend interface (NullBackend default; PortAudio optional via `FIVIEW2_AUDIO=ON`)
- FFT spectrum analysis via KissFFT

**Build system**
- CMake 3.16+, `Unix Makefiles` generator
- All dependencies via `FetchContent` — no system packages required beyond OpenGL
- `imtile` cockpit layout library as FetchContent dependency

### Fixed
- `fidlib`: `const char *spec` parameter throughout (was `char *spec`) — C++ compatibility
- Build clean with GCC 12/13 and Clang in both Debug and Release

### Notes
- `fiview` (legacy SDL visualizer, Jim Peters): build disabled by default (`BUILD_FIVIEW=OFF`)
  due to an open C/C++ linker issue; use `fiview2` instead
- FIDLIB_VULKAN=ON requires Vulkan SDK headers (Apache-2.0); incompatible with GPL-2.0-only
  targets (`fiview`, `firun`) — see `doc/github-strict-ruleset.md §1.3`

[0.1.2]: https://github.com/Josbrig/fidlib-rt/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/Josbrig/fidlib-rt/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Josbrig/fidlib-rt/releases/tag/v0.1.0
