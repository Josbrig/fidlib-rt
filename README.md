# fidlib-rt — Runtime Digital Filter Library

C library + CLI toolkit for runtime-flexible IIR/FIR filter design,
based on fidlib/fiview by Jim Peters (uazu.net).

[![Licence: LGPL-2.1](https://img.shields.io/badge/licence-LGPL--2.1--or--later-blue)](COPYING_LIB)
[![Licence: GPL-2.0](https://img.shields.io/badge/licence-GPL--2.0--or--later-blue)](COPYING)

---

## What this is

A modernised C99/C++20 filter design toolkit built on top of Jim Peters' classic
[fidlib](https://uazu.net/fidlib/) runtime filter library. Filter specifications are
parsed from strings at runtime — no recompilation needed to change a filter.

**Components:**

| Component | Description |
|---|---|
| **fidlib** | Core runtime filter library (IIR/FIR, SIMD-accelerated) |
| **firun** | CLI tool: apply filters to stdin/stdout streams |
| **fidgen** | Code generator: emit filter code for 8 target languages |
| **fiview2** | GUI workbench: interactive filter design (Dear ImGui) |

---

## Quick start

```bash
# Clone
git clone https://github.com/Josbrig/fidlib-rt.git
cd fidlib-rt

# Debug build with sanitisers
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DENABLE_SANITIZERS=ON -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# Release build (SIMD + LTO + fast-math enabled automatically)
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -S . -B build_release
cmake --build build_release -j$(nproc)
```

---

## fidlib — Runtime filter library

Accepts a filter specification string at runtime, designs the filter on the fly,
and executes it sample-by-sample or in blocks:

```c
// Design a 4th-order Butterworth low-pass at 1 kHz / 44100 Hz sample rate
FidFilter *ff = fid_design("LpBu4/1000", 44100, -1, -1, 0, NULL);
FidRun    *run = fid_run_new(ff, &filter_step);
void      *buf = fid_run_newbuf(run);

double out = filter_step(buf, in_sample);   // RT-safe, no allocation

fid_run_freebuf(buf);
fid_run_free(run);
free(ff);
```

**Supported filter types:**

| Spec prefix | Type |
|---|---|
| `LpBuN`, `HpBuN`, `BpBuN`, `BsBuN` | Butterworth LP/HP/BP/BS order N |
| `LpBeN`, `HpBeN` | Bessel LP/HP |
| `LpChN/-dB`, `HpChN/-dB` | Chebyshev with ripple in dB |
| `LpHm`, `LpHn`, `LpBl`, `LpBa` | Windowed FIR (Hann/Hamming/Blackman/Bartlett) |
| `PkBq2/Q/dB/fc` | Peaking EQ biquad |
| `ApBq2/Q/fc` | Allpass biquad |
| `BpRe/Q/fc` | Bandpass resonator |

**SIMD acceleration** for the FIR hotpath: NEON (AArch64), SSE2, AVX2 — detected
at compile time, falls back to scalar C on unsupported platforms.

Optional backends: overlap-save FFT (`FIDLIB_FFT=ON`), Vulkan compute
(`FIDLIB_VULKAN=ON`), OpenCL (`FIDLIB_OPENCL=ON`).

---

## firun — Filter streams from the command line

```bash
# Apply a 4th-order Butterworth LP at 1 kHz to a 44100 Hz double-precision stream
firun -r 44100 LpBu4/1000 < input.f64 > output.f64

# Two-channel stereo with streaming (minimal latency)
firun -r 44100 -n 2 -s LpBu4/1000 < stereo.f64 > filtered.f64
```

---

## fidgen — Generate filter code for any language

Takes a fidlib spec string and emits a self-contained filter implementation
with coefficients baked in:

```bash
fidgen --lang c99   --rate 44100 -f LpBu4/1000   # C99 header
fidgen --lang cpp20 --rate 44100 -f LpBu4/1000   # C++20 header
fidgen --lang rust  --rate 44100 -f LpBu4/1000   # Rust module
fidgen --lang py    --rate 44100 -f LpBu4/1000   # Python class
fidgen --lang sv    --rate 44100 -f LpBu4/1000   # SystemVerilog RTL
```

**Supported output languages:** C99, C++20, Python, Rust, MATLAB/Octave, Julia,
Verilog, SystemVerilog. SIMD variants (NEON/SSE2/AVX2) available for C99/C++20.

---

## fiview2 — Interactive filter design workbench

```bash
./build/bin/fiview2
```

Cockpit-style GUI with all panels visible simultaneously:

- **Parameters**: filter type, order, cutoff frequencies — numerical input + slider
- **Frequency Response**: auto-scaling, phase overlay, A/B comparison
- **Poles / Zeros**: interactive diagram with A/B comparison overlay
- **Impulse / Step Response**
- **Stability**: Jury criterion, FP32 precision warning
- **Filter Cascade**: SOS coefficient display
- **fidgen Export**: generate code in all 8 languages directly from the GUI

Requires OpenGL 3.3+. Layout persists across sessions in `fiview2.ini`.

---

## fiview2 — Browser version (WebAssembly)

A fully self-contained single-file HTML port of fiview2 — no server, no installation,
works offline via `file://`.

Built with Emscripten; the fidlib + fidgen WASM module is Base64-embedded in the HTML file.
All panels, all 10 filter families, all 8 export languages — identical feature set to the
desktop version.

To build from source:

```bash
# Install Emscripten (once)
git clone https://github.com/emscripten-core/emsdk.git ~/tools/emsdk
cd ~/tools/emsdk && ./emsdk install latest && ./emsdk activate latest
source ~/tools/emsdk/emsdk_env.sh

# Configure and build
cmake -G "Unix Makefiles" -DBUILD_WEB=ON \
      -DCMAKE_TOOLCHAIN_FILE=~/tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
      -S . -B build-web
cmake --build build-web -j$(nproc)

# Bundle into a single offline HTML file
python3 web/bundle.py --wasm build-web/fiview2_wasm.wasm \
                      --js   build-web/fiview2_wasm.js \
                      --out  dist/fiview2.html
```

---

## CMake options

| Option | Default | Description |
|---|---|---|
| `BUILD_TOOLS` | ON | Build firun CLI tool |
| `BUILD_FIDGEN` | ON | Build fidgen code generator |
| `BUILD_FIVIEW2` | ON | Build fiview2 GUI workbench (requires OpenGL 3.3+) |
| `BUILD_WEB` | OFF | Build fiview2 WebAssembly port (requires Emscripten) |
| `BUILD_TESTS` | ON | Build test suite |
| `ENABLE_SANITIZERS` | OFF | Enable ASan + UBSan (Debug only) |
| `FIDLIB_SIMD` | ON | NEON/SSE2 FIR hotpath acceleration |
| `FIDLIB_LTO` | ON | Link-Time Optimisation for fidlib |
| `FIDLIB_FAST_MATH` | ON | Scoped -O3 -ffast-math for fidlib |
| `FIDLIB_FFT` | OFF | Overlap-save FFT engine for long FIR |
| `FIDLIB_VULKAN` | OFF | Vulkan compute backend (FP32 GPU-FIR) |
| `FIDLIB_OPENCL` | OFF | OpenCL backend (desktop/Jetson) |

---

## Use as a library (FetchContent)

```cmake
FetchContent_Declare(fidlib_rt
  GIT_REPOSITORY https://github.com/Josbrig/fidlib-rt.git
  GIT_TAG        <full SHA1>   # never a branch name
)
FetchContent_MakeAvailable(fidlib_rt)
target_link_libraries(my_target PRIVATE fidlib)
```

---

## Project layout

```
fidlib/          Core filter library (C99, LGPL-2.1-or-later)
firun/           CLI filter tool (GPL-2.0-or-later)
fidgen/          Filter code generator (GPL-2.0-or-later)
fiview2/         GUI workbench — Dear ImGui (GPL-2.0-or-later)
web/             Browser/WASM port — fiview2.html + wasm_api.cpp + bundle.py
tests/           Test suite (CTest)
doc/             Reference documentation
scripts/         Build helpers, git hooks
```

---

## Origins and credits

**Jim Peters** (uazu.net) wrote fidlib, firun, and fiview between 2002 and 2004
and released them under LGPL 2.1 / GPL 2.0. The architecture — alloc phase
strictly separated from the RT run phase, all hot-path data in a single
cache-local block — is remarkably elegant and was well ahead of its time.

**Tony Fisher** (University of York, 1956–2000) developed the IIR filter design
algorithms (Butterworth, Chebyshev, Bessel) used in fidlib. The mathematical
foundation is embedded in `fidlib/fidmkf.h`. The University of York approved use
(see file header).

**James Hight** created a consolidated fidlib fork (v0.9.11) on GitHub that
incorporates all scattered community patches: `const char *spec`, `extern "C"`
guards, Mixxx team fixes. This fork is the upstream basis for this modernisation.

This modernisation was carried out by **Jörg Simbrig**, with AI assistance
(Claude, Anthropic).

---

## Licences

| Component | Licence | Author |
|---|---|---|
| `fidlib/` core | LGPL-2.1-or-later | Jim Peters 2002–2004 |
| `fidlib/fid_fft.h`, `fid_simd.h`, `fid_vulkan.h`, `fid_opencl.h` | LGPL-2.1-or-later | Jörg Simbrig 2025–2026 |
| `fiview/` | GPL-2.0-only | Jim Peters 2002–2003 |
| `firun/` | GPL-2.0-only | Jim Peters 2004 |
| `fidgen/`, `fiview2/` | GPL-2.0-or-later | Jörg Simbrig 2025–2026 |
| SDL2 (build dependency) | zlib | SDL contributors |
| FFTW3 (optional) | GPL-2.0-or-later | FFTW team |

Full licence texts: `COPYING` (GPL-2.0), `COPYING_LIB` (LGPL-2.1).
See also `LICENSE.md` and `DISCLAIMER.md`.

> **Note:** `FIDLIB_VULKAN=ON` introduces Apache-2.0 headers from the Vulkan SDK.
> This is incompatible with the GPL-2.0-only `fiview`/`firun` components if linked
> into the same binary. See `doc/github-strict-ruleset.md §1.3`.
