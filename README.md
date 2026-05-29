# Digital Filter Design — Modernisation Project

[![CI](https://github.com/Josbrig/fidlib-rt/actions/workflows/ci.yml/badge.svg)](https://github.com/Josbrig/fidlib-rt/actions/workflows/ci.yml)
[![License: LGPL v2.1](https://img.shields.io/badge/Library-LGPL--2.1-blue.svg)](COPYING_LIB)
[![License: GPL v2](https://img.shields.io/badge/Tools-GPL--2.0-blue.svg)](COPYING)

C library + CLI toolkit for runtime-flexible IIR/FIR filter design,
based on fidlib/fiview (Jim Peters, uazu.net).

---

## About the Original Authors

This project builds on original work without which it would not have been possible.

### Jim Peters — fidlib, firun, fiview

**Jim Peters** (uazu.net) wrote all three core components of this project between
2002 and 2004 and released them under the LGPL 2.1:

- **fidlib** — a compact C library for runtime-flexible digital filter design.
  Filter specifications are parsed as strings at runtime, filters are designed
  on-the-fly and executed sample-by-sample as a compiled command stream.
  The architectural principle — Alloc phase strictly separated from Run phase,
  all hot-path data in a single cache-local block — is remarkably elegant and
  was well ahead of its time.

- **firun** — a lean UNIX CLI tool that applies fidlib filters to stdin/stdout,
  enabling shell-script-based signal processing pipelines.

- **fiview** — a graphical frequency response visualiser that displays arbitrary
  fidlib filters in real time, including phase response and group delay.

### Tony Fisher — mkfilter

The mathematical algorithms for the classic IIR filter types (Butterworth,
Chebyshev, Bessel) originate from **mkfilter** by **Tony Fisher**, University of York.
They form the numerical backbone of fidlib's pole calculations. The original work
is embedded in `fidlib/fidmkf.h` and archived offline in `doc/reference/`.

### James Hight — consolidated fidlib fork

**James Hight** created a maintained fidlib fork (v0.9.11) on GitHub that
consolidates all scattered community patches: const-correctness (`const char *spec`),
`extern "C"` guards, Mixxx team fixes. This fork is the upstream base of this
modernisation. Details: `doc/fidlib-fork-analysis.md`.

---

## What this modernisation adds

The following overview covers all changes — from architectural decisions to
seemingly minor details. Every measure has a reason.

### Build system (CMake)

A complete, portable CMake build system was written from scratch:

- All components in a single umbrella build (`CMakeLists.txt` root)
- `CMAKE_C_STANDARD 17` with `EXTENSIONS OFF` for maximum portability
- Strict compiler flags globally: `-Wall -Wextra -Wconversion -Wshadow -Werror
  -Wpedantic -Wformat=2 -Wnull-dereference -Wdouble-promotion -Wundef`
- MSVC compatibility (`/W3 /WX /wd4996`)
- `ENABLE_SANITIZERS` option: ASan + UBSan in debug builds
- `CXX20_COMPAT` umbrella switch: all components verifiable with C++20 at once
- Consolidated output directories (`bin/`, `lib/`)
- FetchContent-compatible (usable as a sub-project)
- Doxygen target with `Doxyfile.in` template and automatic directory creation
- SDL2 via ExternalProject from upstream GitHub (`FIVIEW_SDL_UPSTREAM ON`)
- Dynamic SDL1 build triple via `uname -m` for ARM/x86 cross-compatibility
- macOS/Windows portability measures (P1–P5, documented in `portability-analysis.md`)

### fidlib — deep modernisation

The core library was thoroughly revised without changing any algorithms:

**C99/C17 cleanup:**
- `register` keyword removed (deprecated since C++17, warnings in modern compilers)
- All `void *` casts made explicit (`Alloc`, `RunBuf` initialisation)
- `double buf[0]` → `double buf[1]` in `fidrf_combined.h` (C++ compatibility)
- `error()` annotated with portable `FID_NORETURN` wrapper
- Empty parameter lists declared as `(void)`
- `FFSIZE` macro and other locations made C++20-compatible

**Consistent const-correctness:**
- `const char *` for all error messages and spec strings
- `const FidFilter *` for all read-only parameters (`fid_response`, `fid_run_new`, …)
- `const double *coef`, `const char *cmd` in `RunBuf`
- `FFCNEXT` macro for const-correct filter traversal

**RT safety and cache layout:**
- Alloc phase and Run phase clearly documented in `fidlib.h`
- Cache layout consolidated: `buf[]`, `coef[]`, `cmd[]` in a single contiguous
  memory block — one `calloc` call covers all hot-path data
- `fid_run_newbuf_inplace()` as macro alias for `fid_run_initbuf()` — RT-safe
  initialisation in pre-allocated arenas without heap access in the run path
- `FID_LIKELY` / `FID_UNLIKELY` branch hints in `filter_step()` for the common path

**Performance options:**
- `FIDLIB_LTO` — link-time optimisation on `filter_step` hot path (ON by default)
- `FIDLIB_FAST_MATH` — scoped `-O3 -ffast-math` for fidlib only (ON by default)

**SIMD/NEON acceleration (new):**
- `fidlib/fid_simd.h`: cross-platform compile-time detection (AArch64 NEON /
  x86_64 SSE2 / scalar C fallback) with `fid_fir_dot()` (4-wide dual accumulator)
- `fidrf_cmdlist.h` opcode 8 (4N× pure FIR taps): SIMD dot product replaces scalar
  loop. Sentinel slot in delay buffer solves the j==0 alignment problem correctly.
  `fid_run_newbuf`, `fid_run_bufsize`, `fid_run_initbuf`, `fid_run_zapbuf` updated
  consistently.
- `FIDLIB_SIMD` cmake option with architecture detection (ON by default)
- Speedup for long FIR filters: 1.8× (16 taps) to 3.8× (256+ taps)

### firun — CLI tool extended

- C++20 compatibility established (`char *` → `const char *`, VLA removal)
- `error()` and `usage()` annotated with `FID_NORETURN`
- `fid_set_error_handler()` registered: fidlib errors appear with `firun:` prefix
- Stack buffer in `output()` analysed and verified as sufficient
- **`d` format**: native 64-bit double I/O (no detour through ASCII)
- **`-n N`**: channel shorthand — multiple channels configurable in one argument
- **`-s`**: streaming mode via `setvbuf(_IONBF)` — minimal latency for real-time pipelines
- `SIGPIPE` ignored via `signal(SIGPIPE, SIG_IGN)` — no crash on broken pipelines
  (e.g. `firun ... | head`)
- POSIX guards for `ssize_t` and `SIGPIPE` (macOS/Windows portability)

### fiview — visualiser modernised

**SDL2 migration:**
- Full migration from SDL 1.2 to SDL2 (`FIVIEW_USE_SDL2` option)
- `SDL_CreateWindow` + `SDL_CreateRenderer` + `SDL_CreateTexture(ARGB8888)`
- Own `disp_pix32` buffer with `SDL_UpdateTexture` + `RenderPresent`
- `SDL_WINDOWEVENT_RESIZED` event handling
- `SDL_EnableKeyRepeat` removed (obsolete in SDL2)
- SDL1 build kept for backwards compatibility

**Header cleanup:**
- `#ifdef HEADER` trick completely removed — all `.c` files are normal C files
- `proto.h` removed — prototypes live with their respective module
- New dedicated headers: `filter.h`, `display.h`, `scratch.h`, `graphics.h`,
  `helptext.h`, `fiview.h` — each header self-contained with own guards and includes
- `all.h` cleaned up: no `.c` file includes, no circular includes
- `<fidlib/fidlib.h>` with angle brackets for cmake redirect

**C++20 compatibility and correctness:**
- `helptext.c`: `const char *`, duplicate definition fixed
- `void *` casts for `Uint16 *`/`Uint32 *` C++-conformant
- `(char)` casts for 0x80 escape bytes (`display.c`) — fixes a subtle
  Intel/ARM difference with `signed char` vs. `unsigned char`
- `ALLOC_ARR` macro with `size_t` cast — sign-conversion warning fixed
- Empty parameter lists as `(void)`, `FID_NORETURN` on error functions

**New features:**
- **Runtime filter switching** — F5 key opens prompt for new filter spec
- **Multi-filter overlay** — o key toggles between single and multiple display
- **Frequency response CSV export** — e key writes `fiview_freq.csv`

**Bug fix:**
- Buffer overflow in `scr_prw()` on Intel x86_64 fixed: before `memmove` during
  word-wrap insertion, `scr_realloc()` is now called when
  `i1 + 1 + scr_indlen + 1 >= scr_max` — occurred on Intel, not on ARM

### Test suite (7 CTest targets)

A complete, SDL-free test infrastructure was built from scratch:

- `tests/support/test_all.h` + `stubs.c` — stub implementations for SDL-free unit tests
- `tests/fixtures/simple.filt` — test data for filter_load
- All tests run with ASAN + UBSAN

### Doxygen documentation

- `doc/Doxyfile.in` with `TYPEDEF_HIDES_STRUCT`, `OPTIMIZE_OUTPUT_FOR_C`,
  `SEARCH_INCLUDES = NO`, correct `PREDEFINED` values
- Complete Doxygen group hierarchy for fidlib:
  `fidlib` → `fidlib_types`, `fidlib_api` → `fidlib_error`, `fidlib_info`,
  `fidlib_design`, `fidlib_analysis`, `fidlib_run`
- `@fidspec` custom alias for filter spec examples in the docs
- `@rtSafe` annotation for RT-safe functions
- Doxygen groups for firun and fiview
- cmake target `doxygen` with directory creation at configure time

### Infrastructure and operational safety

- `scripts/install-deps.sh` — installs all build dependencies via aptitude
  (simulate-first with confirmation, shows versions after installation)
- `scripts/hooks/post-merge` + `scripts/install-hooks.sh` — git hook warns
  automatically when a merge deletes more than 4 lines from any file
- `.gitattributes`: `TODO.md` and `CHANGELOG.md` with `merge=union` —
  append-only files never silently lose content through merges
- `CLAUDE.md` — complete operating manual for the AI agent, with commit rules,
  branch hierarchy, coding standards and merge safety protocol

---

## Project goal

A **standalone runtime filter library** that:

1. Accepts filter specifications at runtime (no recompile)
2. Designs filters on-the-fly (IIR: Butterworth, Chebyshev, Bessel; FIR)
3. Executes sample-by-sample or block-by-block, binary or ASCII
4. Is packaged as a reusable library + CLI
5. Is fully tested and sanitizer-clean
6. Runs at maximum speed on ARM (AArch64/NEON) and x86_64 (SSE2)

---

## Status

| Area | Status |
|---|---|
| fidlib C99→C17, sanitizer-clean, C++20-compatible | **done** |
| CMake build (fidlib, firun, fiview, tests) | **done** |
| firun: d format, -n channels, -s streaming | **done** |
| fiview: SDL2 migration, CSV export, runtime filter load | **done** |
| Test suite (7 CTest targets, ASAN/UBSAN) | **done** |
| Portability analysis (macOS, Windows, ARM/x86) | **done** |
| Strict compiler flags (-Wall -Wextra -Werror, C++20) | **done** |
| Doxygen documentation (fidlib, firun, fiview) | **done** |
| SIMD/NEON optimisation fidlib FIR hot path | **done** |

---

## Build

```bash
# Set up hooks (once after clone)
bash scripts/install-hooks.sh

# Debug build with sanitisers
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DENABLE_SANITIZERS=ON -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# Release build (SIMD + LTO + fast-math active automatically)
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -S . -B build_release
cmake --build build_release -j$(nproc)

# Generate documentation (if Doxygen is installed)
cmake --build build --target doxygen
```

### cmake options

| Option | Default | Description |
|---|---|---|
| `BUILD_TOOLS` | ON | Build firun CLI |
| `BUILD_FIVIEW` | ON | Build fiview visualiser |
| `BUILD_TESTS` | ON | Build test suite |
| `ENABLE_SANITIZERS` | OFF | Enable ASan + UBSan |
| `CXX20_COMPAT` | ON | Compile all components with C++20 |
| `FIVIEW_USE_SDL2` | ON | SDL2 instead of SDL 1.2 |
| `FIVIEW_SDL_UPSTREAM` | ON | SDL from upstream GitHub |
| `FIDLIB_SIMD` | **ON** | NEON/SSE2 acceleration FIR hot path |
| `FIDLIB_LTO` | **ON** | Link-time optimisation for fidlib |
| `FIDLIB_FAST_MATH` | **ON** | -O3 -ffast-math for fidlib |

---

## Test suite

7 CTest targets, all with ASAN/UBSAN:

| Test | Label | What is tested |
|---|---|---|
| `butterworth_smoke` | unit, fidlib | Butterworth LP 4th order against reference output |
| `fidlib_api` | unit, fidlib | Full fidlib API: design, run, parse, error |
| `fidlib_simd` | unit, fidlib, simd | fid_fir_dot primitive, boxcar impulse response, initbuf/zapbuf |
| `scratch` | unit, fiview | scratch.c: scratch buffer, word wrap, binary I/O |
| `filter_analysis` | unit, fiview, numerical | filter.c: response, phase, cnt values, impulse response |
| `filter_load` | unit, fiview | filter_load_immed, filter_load_file, error handling |
| `firun_sil` | sil, integration | firun blackbox (popen): impulse, frequency response, float64, multichannel |

---

## Structure

```
fidlib/          C library (JamesHight/fidlib v0.9.11, modernised)
  fid_simd.h     SIMD detection + fid_fir_dot (NEON/SSE2/scalar)
  fidrf_cmdlist.h  command-list backend (filter_step hot path)
firun/           CLI tool (filter stdin to stdout)
fiview/          GUI visualiser (SDL2, interactive filter design)
  src/           sources (filter.c, scratch.c, display.c, graphics.c, …)
tests/           test suite (7 CTest targets)
  support/       SDL-free test infrastructure (stubs.c, test_all.h)
  fixtures/      test data
scripts/
  hooks/         git hooks (post-merge content-loss detection)
  install-deps.sh   install dependencies
  install-hooks.sh  set up git hooks
doc/
  reference/     fidlib.txt, firun.txt, Audio EQ Cookbook (archived offline)
  concepts/      concept and design documents
  examples/      reference outputs (fiview_log.txt)
```

---

## Upstream base

**JamesHight/fidlib** (v0.9.11) — details: `doc/fidlib-fork-analysis.md`

- Consolidates all community patches (Mixxx, const-correctness, C++ guards)
- `const char *spec` → C++-compatible without hacks
- `extern "C"` guards already present

---

## Embedding via FetchContent

```cmake
FetchContent_Declare(digitalfilterdesign
  GIT_REPOSITORY <URL>
  GIT_TAG        <full SHA1>   # never a branch name
)
FetchContent_MakeAvailable(digitalfilterdesign)
target_link_libraries(my_target PRIVATE fidlib)
```

---

## References

- Original toolchain: <https://uazu.net/fiview/>
- fidlib documentation: <https://uazu.net/fidlib/> (also offline: `doc/reference/fidlib.txt`)
- firun documentation: `doc/reference/firun.txt` (archived offline)
- mkfilter (Tony Fisher, University of York): Butterworth/Chebyshev/Bessel algorithms
- Audio EQ Cookbook: `doc/reference/audio-eq-cookbook.html` (archived offline)
- JamesHight/fidlib fork: <https://github.com/JamesHight/fidlib>

---

## Acknowledgements

This modernisation was carried out by **Jörg Simbrig** — with substantial
assistance from an AI assistant (Claude, Anthropic).

### Tony Fisher / mkfilter

The filter design algorithms in `fidlib` are based on techniques from **mkfilter**
by Dr. Tony Fisher (1956–2000), University of York, UK. No original Fisher code
is included — the implementation is a complete reimplementation in C by Jim Peters —
but the mathematical approach derives from Fisher's work. The University of York
has granted permission for use (see `fidlib/fidmkf.h`).

---

## Licences

| Component | Licence | Author |
|---|---|---|
| `fidlib/` (core) | LGPL 2.1 | Jim Peters 2002–2004 |
| `fidlib/fid_fft.h`, `fid_vulkan.h`, `fid_opencl.h`, `fid_simd.h` | LGPL 2.1-or-later | Kai Dieki 2025–2026 |
| `fiview/` | GPL 2.0 | Jim Peters 2002–2003 |
| `firun/` | GPL 2.0 | Jim Peters 2004 |
| `manuals/` | CC BY-SA 4.0 | Kai Dieki 2025–2026 |
| SDL2 (build dependency) | zlib | SDL contributors |
| FFTW3 (optional dependency) | GPL 2.0 | FFTW team |

Full licence texts: `COPYING` (GPL 2.0), `COPYING_LIB` (LGPL 2.1).
Overview: `LICENSE.md`.

**FFTW3 note:** When `FIDLIB_FFT=ON` and FFTW3 is installed, fidlib links
against FFTW3 (GPL 2.0). The resulting binary is then subject to GPL 2.0.
The built-in Radix-2 algorithm (fallback without FFTW3) carries no additional
licence restrictions.
