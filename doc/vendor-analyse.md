# Vendor Analysis: Inventory and cmake Rebuild

Date: 2026-05-26

---

## 1. Overview of Vendor Components

| Component | Path | Type | Size | Status |
|---|---|---|---|---|
| fidlib | `vendor/fidlib/` | Git submodule (JamesHight) | ~5,400 LOC | initialised, v0.9.11 |
| fiview | `vendor/fiview/` | Archive copy (uazu.net) | ~6,000 LOC | complete, v0.9.10 |
| mkfilter | `vendor/mkfilter/` | Git submodule (billthefarmer) | ~2,100 LOC | initialised |
| gmeteor | `vendor/gmeteor/` | Archive (SourceForge) | 769 kB | complete, v0.95 (2013-01-06) |

---

## 2. fidlib (vendor/fidlib)

### What it is

C library for runtime filter design and execution. The core of the entire stack.
Origin: Jim Peters (uazu.net, 2002–2004). This fork: JamesHight/fidlib, v0.9.11
— consolidates all community patches (const-correctness, `extern "C"` guards, C++ compatibility).

### Files

| File | Purpose |
|---|---|
| `fidlib.h` | Public API (77 lines) |
| `fidlib.c` | Complete implementation (2,408 lines) |
| `fidmkf.h` | mkfilter-derived filter types — included internally |
| `fidrf_cmdlist.h` | Recommended execution engine (command-list) |
| `fidrf_combined.h` | Alternative engine (flattened, less precise) |
| `fidrf_jit.h` | Deprecated JIT engine (x86-only, warned) |
| `firun.c` | CLI test tool (GPL, optional) |

### Public API (fidlib.h)

```c
// Housekeeping
void     fid_set_error_handler(void (*rout)(char *));
char    *fid_version(void);
void     fid_list_filters(FILE *out);
int      fid_list_filters_buf(char *buf, char *bufend);

// Filter design
FidFilter *fid_design(const char *spec, double rate,
                      double freq0, double freq1,
                      int f_adj, char **descp);
double     fid_design_coef(double *coef, int n_coef, const char *spec,
                           double rate, double freq0, double freq1, int adj);
FidFilter *fid_parse(double rate, char **pp, FidFilter **ffp);
FidFilter *fid_cv_array(double *arr);
FidFilter *fid_cat(int freeme, ...);

// Filter analysis
double     fid_response(FidFilter *filt, double freq);
double     fid_response_pha(FidFilter *filt, double freq, double *phase);
int        fid_calc_delay(FidFilter *filt);
FidFilter *fid_flatten(FidFilter *filt);
void       fid_rewrite_spec(const char *spec, double freq0, double freq1,
                            int adj, char **spec1p, char **spec2p,
                            double *freq0p, double *freq1p, int *adjp);

// Filter execution (real-time signal processing)
void *fid_run_new(FidFilter *filt, double (**funcpp)(void *, double));
void *fid_run_newbuf(void *run);
int   fid_run_bufsize(void *run);
void  fid_run_initbuf(void *run, void *buf);
void  fid_run_zapbuf(void *buf);
void  fid_run_freebuf(void *runbuf);
void  fid_run_free(void *run);
```

### Filter specification DSL (fispec)

```
LpBu4/100        Butterworth lowpass, order 4, cutoff 100 Hz
HpBe6/0.1        Bessel highpass, order 6, relative cutoff 0.1
BpCh2/0.5/50-60  Chebyshev bandpass, ripple 0.5 dB, 50–60 Hz
BsRe/100/50      Resonator bandstop, Q=100, 50 Hz
LsBq/0.7/-6/100  Low-shelving biquad (Audio EQ Cookbook), -6 dB, 100 Hz
x                Series connection (multiple filters chained)
```

Over 47 predefined filter types in three classes:
- **mkfilter-based:** Bessel, Butterworth, Chebyshev (arbitrary order) + resonators
- **Audio EQ Cookbook:** biquad variants (LpBq, HpBq, BpBq, PkBq, LsBq, HsBq, …)
- **FIR windows:** Blackman, Hamming, Hann, Bartlett lowpass

### Current build system

Autotools (Autoconf + Automake + Libtool).
Produces: `libfidlib.so` / `libfidlib.a`, optionally `firun`.
Dependency: only `-lm`.

### Licence

`fidlib.h` / `fidlib.c` / `fidmkf.h`: **GNU LGPL v2.1**
`firun.c`: **GNU GPL v2**

---

## 3. fiview (vendor/fiview)

### What it is

Interactive SDL GUI tool for filter development and visualisation.
Purpose: view frequency response + impulse response graphically, interactively
adjust filter parameters, export C code for the resulting filter.

### Files

| File | Purpose |
|---|---|
| `src/fiview.c` | Main program, SDL loop, file output (881 lines) |
| `src/filter.c` | Filter loading, analysis (frequency/impulse response), time constants (2,207 lines) |
| `src/display.c` | Layout + rendering of display areas (1,132 lines) |
| `src/graphics.c` | SDL graphics abstractions, pixel handling 16/32 bpp (1,024 lines) |
| `src/helptext.c` | Embedded help text + dynamic filter list (593 lines) |
| `src/scratch.c` | Scratch buffer management with auto-realloc (256 lines) |
| `src/fidlib/` | Embedded older fidlib copy (2,304 lines) |
| `src/mk` | Shell script build system (no Makefile, no cmake) |

### Dependencies

- **SDL 1.2** (graphics + events)
- **libm**
- No FFTW, no GTK, no X11 directly

### Embedded fidlib vs. vendor/fidlib

The `src/fidlib/` copy embedded in fiview is **older** than `vendor/fidlib`:
- Without `extern "C"` guards
- Without const-correctness
- Without Autotools build

fiview was originally the reference frontend for fidlib — both have since become separate projects.

### Outputs

`fiview.log`: fully annotated C code for the displayed filter (3 versions: readable,
compiler-optimised, with `fid_design_coef`). Also frequency response analysis, time constants.
`fiview.coef`: raw IIR/FIR coefficients.

Example log is at `doc/examples/fiview_log.txt`.

### Licence

**GNU GPL v2**

---

## 4. mkfilter (vendor/mkfilter)

### What it is

Academic filter design tool by Dr A.J. Fisher (Univ. York, 1992).
Computes poles/zeros of classical IIR filters (Butterworth, Bessel, Chebyshev)
using S-plane theory and transforms via bilinear transform (BLT) or
matched Z-transform (MZT) into the z-domain.

### Files

| File | Purpose |
|---|---|
| `mkfilter.C` | CLI: pole computation, BLT/MZT, difference equation, output (699 lines) |
| `mkfilter.h` | Typedefs, constants, inline utilities |
| `complex.C/.h` | Complex arithmetic (operators, sqrt, exp(jθ), polynomial evaluation) |
| `gencode.C` | Filter → C code generator (reads mkfilter -l output) |
| `genplot.C` | Filter → PNG graph via libgd |
| `mkshape.C` | FIR designer: raised cosine, root-raised cosine, Hilbert |
| `mkaverage.C` | Moving average FIR |
| `readdata.C` | Helper: parser for mkfilter `-l` output |

### CLI interface (short form)

```bash
mkfilter -Bu -Lp -o 4 -a 0.2          # Butterworth lowpass, order 4, α=0.2
mkfilter -Ch 0.5 -Bp -o 2 -a 0.1 0.2  # Chebyshev bandpass, ripple 0.5 dB
mkfilter -Re 10 -Bp -a 0.05            # Resonator, Q=10, α=0.05

mkfilter -Bu -Lp -o 4 -a 0.2 -l | gencode -ansic   # → C code
mkfilter -Bu -Lp -o 4 -a 0.2 -l | genplot freq.png  # → PNG frequency response
```

### Relationship to fidlib

**Overlap:** `fidmkf.h` in fidlib implements the same pole computations —
Butterworth, Bessel, Chebyshev for arbitrary order. The mathematics is identical.

**Role in the project:** Reference and validation. When fidlib computes a filter,
mkfilter can be used to cross-check (poles, difference equation).

### Current build system

GNU Make + gcc/g++ (`-std=gnu++98`, `-fpermissive`).
`genplot` requires **libgd** (PNG output).

### Licence

No explicit licence statement. Academically released, distributed on GitHub for decades.

---

## 5. gmeteor (vendor/gmeteor)

### What it is

FIR filter designer for equiripple filters against arbitrary frequency response masks,
based on Parks-McClellan / Remez exchange algorithm.
Scheme/Guile-based. Developed ca. 2005–2013 on SourceForge.

### Status

Complete source code in `vendor/gmeteor/gmeteor-0.95.tar.gz` (769 kB).
Retrieved directly from SourceForge — the originally saved file was a
404 HTML page (wrong download URL). The project has been inactive since 2013,
but the download still works via `https://sourceforge.net/projects/gmeteor/files/`.

### Algorithm

Not Parks-McClellan/Remez, but **METEOR** (Steiglitz, Parks, Kaiser — IEEE Trans.
Signal Processing, 1992): reduction of FIR design to a linear program,
solved via simplex. Result is also equiripple but more general than classic
Remez: arbitrary frequency response masks, also specifiable analytically via Scheme.

### Build system

Autotools (configure.ac, Makefile.am).

### Source files (from tarball)

| File | Purpose |
|---|---|
| `gmeteor.c` | Main C code |
| `simplex.c/.h` | Simplex algorithm (LP core) |
| `lpp*.c` (12 files) | LP solver, Fortran-to-C conversion |
| `f2c.h` | Fortran-to-C compatibility header |
| `gmeteor-core.scm` | Guile/Scheme core |
| `gmeteor-lib.scm` | Helper library |
| `gmeteor-simple.scm` | Simplified interface |
| `gmeteor-getopt.scm` | CLI option processing |
| `gmeteor.in` | Script template |
| `examples/*.scm` | 7 example specifications |
| `doc/gmeteor.pdf` | Complete documentation |

### Dependencies

- **libguile** (GNU Scheme interpreter)
- **libm**

### Licence

**GNU GPL**

### Role in the project

Closes the gap that fidlib has for equiripple FIR design: fidlib has no
METEOR/Remez optimisation. gmeteor allows arbitrary frequency response masks.

---

## 6. Architecture overview: how the pieces fit together

```
mkfilter                    gmeteor
  │                              │
  │  Pole/zero                   │  METEOR algorithm
  │  computation (reference)     │  FIR equiripple (arbitrary mask)
  │                              │
  └──────────────┬───────────────┘
                 │
              fidlib
         (core library)
         ┌─────────────────────────────────────────┐
         │  fid_design("LpBu4/100", 44100, ...)    │
         │  → FidFilter* (internal representation)│
         │  fid_run_new(filt, &funcp)              │
         │  → sample = funcp(buf, input)           │
         └─────────────────────────────────────────┘
                 │
              fiview
         (reference frontend)
         ┌─────────────────────────────────────────┐
         │  GUI: frequency response + impulse resp │
         │  Export: fiview.log (C code)            │
         │  Export: fiview.coef (coefficients)     │
         └─────────────────────────────────────────┘
```

---

## 7. What the new cmake project can become

### Guiding principle

fidlib and fiview are two sides of the same coin: fidlib is the engine,
fiview was the observation tool. The new project replaces both
with a cleanly structured cmake project — without GUI dependency,
without an embedded fidlib copy, with modern C99 and a testable library.

### Target architecture

```
digitalfilterdesign/
├── CMakeLists.txt              ← root cmake
├── lib/
│   ├── CMakeLists.txt
│   ├── fidlib.c                ← C99-cleaned, taken from vendor/fidlib
│   ├── fidlib.h                ← public API, with extern "C" guards
│   ├── fidmkf.h                ← internal (mkfilter core)
│   ├── fidrf_cmdlist.h         ← internal (execution engine)
│   └── fidrf_combined.h        ← internal (alternative engine)
├── cli/
│   ├── CMakeLists.txt
│   └── firun.c                 ← CLI tool (taken from vendor/fidlib)
└── tests/
    ├── CMakeLists.txt
    └── test_butterworth.c      ← impulse response validation against fiview_log.txt
```

### What is taken from vendor/

| Vendor source | Taken as | Reason |
|---|---|---|
| `vendor/fidlib/fidlib.c` | `lib/fidlib.c` | Core of the library |
| `vendor/fidlib/fidlib.h` | `lib/fidlib.h` | Public API |
| `vendor/fidlib/fidmkf.h` | `lib/fidmkf.h` | Filter type implementation |
| `vendor/fidlib/fidrf_cmdlist.h` | `lib/fidrf_cmdlist.h` | Execution engine |
| `vendor/fidlib/fidrf_combined.h` | `lib/fidrf_combined.h` | Alternative engine |
| `vendor/fidlib/firun.c` | `cli/firun.c` | Reference CLI tool |
| `vendor/mkfilter/` | — | Reference/validation only, no code transfer |
| `vendor/fiview/` | — | Reference only (algorithms already in fidlib) |
| `vendor/gmeteor/` | — | Tarball available, not yet integrated — for a later step |

### What is not taken

- `vendor/fiview/src/fidlib/` — outdated embedded copy, replaced by `lib/fidlib.c`
- `vendor/fiview/src/*.c` — SDL GUI, no value for the library project
- `fidrf_jit.h` — deprecated, x86-only, with warning note in code
- `vendor/mkfilter/genplot.C` — libgd dependency, no value

### cmake targets

| Target | Type | Description |
|---|---|---|
| `fidlib` | STATIC/SHARED library | Core library, public header fidlib.h |
| `firun` | Executable | CLI tool, optional (`-DBUILD_TOOLS=ON`) |
| `test_butterworth` | Test | Butterworth LP 4th order impulse response against reference |

### Compiler flags (from CLAUDE.md)

```cmake
target_compile_options(fidlib PRIVATE
  -Wall -Wextra -Wconversion -Wshadow -Werror
)
# For debug + tests:
target_compile_options(fidlib PRIVATE
  -fsanitize=address,undefined
)
```

### When vendor/ can be dropped

vendor/ can be removed once:

1. `lib/fidlib.c` is taken from `vendor/fidlib/fidlib.c` and C99-cleaned
   (sanitizer-clean, no VLAs in API, no UB)
2. `cli/firun.c` is taken from `vendor/fidlib/firun.c`
3. A smoke test (Butterworth LP 4th order, impulse response) against the
   reference in `doc/examples/fiview_log.txt` passes
4. cmake is correctly configured (FetchContent-compatible)

The git submodules `vendor/fidlib` and `vendor/mkfilter` then become
archive/reference only — no longer a build dependency.

---

## 8. Open items

| Topic | Status | Action required |
|---|---|---|
| gmeteor source code | present (`vendor/gmeteor/gmeteor-0.95.tar.gz`) | cmake integration for a later step |
| fiview SDL GUI | reference, not integrated | no action needed as long as `fiview_log.txt` serves as test oracle |
| fidrf_jit.h | deprecated, disabled | omit — do not include in cmake |
| firun licence | GPL v2 (not LGPL) | keep firun as a clearly separate optional target |
| mkfilter libgd | only needed for genplot | do not include genplot if no PNG output is needed |
| Parks-McClellan FIR | no replacement for gmeteor | leave open for a later step |
