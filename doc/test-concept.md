# Test Concept: digitalfilterdesign

**Project path:** `digitalfilterdesign`  
**Branch:** `feature/test-coverage`  
**Date:** 2026-05-27

---

## 1. Test Strategy — Overview

The project is divided into three independently testable components:

| Component | Testable parts | Exclusions |
|---|---|---|
| **fidlib** | API complete | — |
| **firun** | Parsing, format decoding, pipeline | Binary I/O on real FDs only via SIL |
| **fiview** | scratch, filter, display logic | SDL render code |

Test levels:
1. **Unit** — isolated function, no external deps
2. **Component** — module-internal, possibly mock for error handler / file I/O
3. **SIL (Software-in-the-Loop)** — firun as black box, reference data compared
4. **Numerical-mathematical** — filter against analytical target values
5. **Regression** — golden reference from fiview_log.txt / CSV exports

All tests run under CTest via `ctest --test-dir build --output-on-failure`.  
Debug builds use `-fsanitize=address,undefined` — tests must be sanitizer-clean.

---

## 2. Numerical Filter Testability

IIR and FIR filters are fully determined by their transfer function H(z).
All of their properties are analytically predictable and suitable as pass/fail criteria.

### 2.1 Measurable Characteristics

| Characteristic | Method | Expected value |
|---|---|---|
| DC gain (f=0) | `fid_response(ff, 0.0)` | 1.0 (lowpass), 0.0 (highpass) |
| Nyquist gain | `fid_response(ff, 0.5)` | 0.0 (lowpass), 1.0 (highpass) |
| -3 dB cutoff frequency | Binary search | fc ± 1 % |
| -6 dB limit | Binary search | f6 ± 1 % |
| Stopband attenuation | max `fid_response` in stopband | < -40 dB (Butterworth order 4) |
| Passband ripple | max/min `fid_response` in passband | < 0.1 dB |
| Group delay | impulse response: centroid | samples ≈ N/2 (FIR) |
| Impulse response decay | last significant amplitude | < 1e-6 after cnt_max samples |
| Phase at fc | `fid_response_pha(ff, fc)` | -N·45° (Butterworth order N) |

### 2.2 Tolerance Band Philosophy

Floating-point DSP has finite precision. Tolerances:
- Frequency values: ± 0.5 % of target frequency
- Amplitudes in passband: ± 0.01 (linear), corresponds to ~ 0.09 dB
- Amplitudes in stopband: absolute value < 0.003 (≈ -50 dB) for BuN≥4
- Phase angle: ± 1°

### 2.3 Reference Test Case (already implemented)

`tests/test_butterworth.c`: LpBu6 at 44100 Hz / fc=400 Hz

```
DC gain:        ~1.000  (tolerance ±0.001)
Gain at fc:     ~0.707  (tolerance ±0.005)
Gain at 10·fc: <0.005
```

This test serves as the template for all further filter type tests.

---

## 3. fidlib — Unit Tests

### 3.1 API Coverage

All public functions from `fidlib/fidlib.h`:

#### fid_design / fid_design_coef

```
Test cases:
  LpBu2/1000    @ sr=44100  → type 0 (low-pass), 2 biquad sections
  HpBu4/5000    @ sr=44100  → type 1 (high-pass)
  BpBu2/1000-2000 @ sr=44100 → type 2 (band-pass), n_poles=4
  BsRa4/1000-2000 → type 3 (band-stop / notch via resonance)
  ApBu2/1000    → type 4 (all-pass), phase response
  LpBe6/100     → Bessel lowpass (flat group delay)
  LpCh2/1000/3dB → Chebyshev, 3dB ripple
  LpCh4/1000/0.5dB → Chebyshev, 0.5dB ripple

  For each: check
  - fid_response(ff, 0.0)   → passband expectation
  - fid_response(ff, fc/sr) → ~0.707 for Butterworth
  - fid_response(ff, 10·fc/sr) → stopband value
  - fid_response_pha(ff, fc/sr) → phase expectation
  - Correct FidFilter structure: info[0].type, info[0].len
```

#### fid_flatten / fid_cat

```
Test cases:
  flatten(ff1) ∘ flatten(ff2) == flatten(cat(ff1, ff2))
  Empty filter: cat(NULL, ff) == ff
  Series filter: cat(LpBu2, HpBu2) → bandpass-like response
```

#### fid_run_* Lifecycle

```
Test cases:
  newbuf  → bufsize matches fid_run_bufsize
  initbuf → buffer is zeroed
  zapbuf  → reset behavior: impulse response identical from start
  freebuf → no leak (verifiable with ASan)
  free    → no leak

  Special case fid_run_newbuf_inplace (RT-safe):
    Pre-allocated buffer, minimum size = fid_run_bufsize
    Result identical to fid_run_newbuf
```

#### fid_cv_array

```
  fid_design_coef(...) → double* array
  Values match FidFilter coefficients (float64 round-trip)
```

#### fid_parse / fid_rewrite_spec

```
  "BpBu2/1000-2000" → correct tokenization
  "LpBu4/400" @ sr=44100 → same response as fid_design
  fid_rewrite_spec: check normalization (e.g. whitespace, capitalization)
  Invalid spec: fid_set_error_handler catches the error (mock test)
```

#### fid_set_error_handler

```
  Normal error: custom handler called, not abort()
  NULL handler: default behavior (abort) — ONLY check if callable, no
                actual crash in test
  test_butterworth.c already shows the pattern: longjmp from error handler
```

#### fid_list_filters / fid_version

```
  fid_list_filters: return buffer non-NULL, at least "Butterworth" in it
  fid_version: non-empty string
```

#### fid_calc_delay

```
  LpBu6/400: delay <= cnt_max from filter.c analysis
  Ratio delay(BuN) ~ N·sr/fc
```

### 3.2 Edge Cases (Boundary / Error)

```
  Order 0:   fid_design fails → error_handler
  fc = 0:    fid_design fails
  fc > sr/2: fid_design fails (Nyquist)
  sr = 0:    fid_design fails
  Bandpass with f0 > f1: error
  Extremely high order (32): no UB, no overflow (ASan)
```

---

## 4. filter.c — Component Tests (fiview-internal)

fiview/src/filter.c contains analysis logic that does not require SDL.

### 4.1 filter_load_immed / filter_load_file

```
  filter_load_immed("LpBu4/400"):
    - n_filt == 1
    - curr->typ == 0
    - curr->filt->filt != NULL

  filter_load_immed("LpBu4/400,HpBu4/5000"):
    - n_filt == 2 (two SubFilt)

  filter_load_file("tests/fixtures/simple.filt"):
    - file with "LpBu2/1000" → same as immed

  Error case: filter_load_immed("") → error_handler (mock)
  Error case: filter_load_file("/nonexistent") → return 0
```

### 4.2 filter_response / filter_resp_range

```
  LpBu4/400:
    filter_response(ff, 0.0, NULL)       == 1.0 ± 0.001
    filter_response(ff, 400.0/44100, NULL) ~= 0.707 ± 0.01
    filter_response(ff, 4000.0/44100, NULL) < 0.005

  With phase:
    filter_response(ff, 400.0/44100, &phase)
    phase ~= -N*45° ± 2° (Butterworth)

  filter_resp_range(ff, 0.0, 0.5, 512, 4):
    - return array length 512
    - maximum at 0.0 is ~1.0
    - values fall monotonically to Nyquist
```

### 4.3 filter_setup_gain / filter_setup_cnt

```
  filter_setup_gain:
    gain100 > 0
    type correctly identified (0=LPF, 1=HPF, 2=BPF, 3=BSF, 4=APF)
    m3db[0] ~= fc ± 1%
    m6db present for all standard types

  filter_setup_cnt:
    cnt50 < cnt90 < cnt95 < cnt99 < cnt999 < cnt_max
    cnt_max < 50*sr/fc (filter decays in finite time)
```

### 4.4 filter_dump

```
  LpBu4/400: dump != NULL
  Contains "Butterworth", "low-pass", "400"
  no crash on multi-stage filter
```

### 4.5 filter_run / runfilter_step

```
  Impulse response LpBu4/400:
    Step 1: runfilter_step(rr, 1.0)
    Steps 2..N: runfilter_step(rr, 0.0)
    Sum of all samples ~= 1.0 (gain 1.0 * sr/fc ... proportional)
    Value after cnt_max steps < 1e-5

  Step response:
    All steps: runfilter_step(rr, 1.0)
    Value converges to 1.0 ± 0.001 after cnt99 steps

  zapbuf equivalence:
    runfilter_free → new run → same response
```

---

## 5. scratch.c — Unit Tests

scratch is stateful but fully isolatable (global buffer).

### 5.1 Basic Operations

```
  scr_zap():         scr_len == 0, scratch[0] == '\0'
  scr_pr("hello"):   scr_len == 5, strcmp(scratch, "hello") == 0
  scr_lf():          scratch[scr_len-1] == '\n'
  SCR_PUTC('x'):     correct termination
```

### 5.2 scr_vpr / scr_pr Format

```
  Numeric: scr_pr("%d", 42)      → "42"
  Float:   scr_pr("%.3f", 3.14)  → "3.140"
  Concat:  scr_pr("a"); scr_pr("b") → "ab"
```

### 5.3 scr_prw — Word Wrap

```
  scr_wrap(20, "  "):
  scr_prw("this is a long text that needs to be wrapped")
  → Each line <= 20 characters
  → Continuation lines start with "  "
  → No words cut off

  Edge case: word longer than line width → no loop, no UB
  Edge case: empty string → no crash
```

### 5.4 scr_inc / scr_wrD / scr_wrI

```
  scr_inc(16): pointer != NULL, 16 bytes zeroed
  scr_wrD(3.14): memcpy round-trip correct
  scr_wrI(0xDEAD): memcpy round-trip correct
```

### 5.5 scr_realloc — Capacity Growth

```
  Write 32768 bytes → no crash, no overflow
  Write 64001 bytes → scr_max >= 65536
  ASan: no heap overflow at boundary
```

### 5.6 scr_dup

```
  scr_zap_pr("test"):
  char *s = scr_dup()
  strcmp(s, "test") == 0
  s != scratch  (independent copy)
  free(s) → no leak
```

---

## 6. firun — Tests

### 6.1 Unit: decode_spec / spec_count

```
  decode_spec("a")  → FORMAT_ASCII
  decode_spec("d")  → FORMAT_DOUBLE
  decode_spec("f")  → FORMAT_FLOAT
  decode_spec("w")  → FORMAT_INT16_LE
  decode_spec("W")  → FORMAT_INT16_BE
  decode_spec("s")  → FORMAT_INT16_LE (synonym)
  decode_spec("b")  → FORMAT_INT8
  decode_spec("")   → error / default
  decode_spec("z")  → error (unknown)

  spec_count("a,b,c") → 3
  spec_count("LpBu4/400") → 1
  spec_count("LpBu2/1000,HpBu2/5000") → 2
```

### 6.2 Unit: parse_filters

```
  "LpBu4/400" @ sr=44100:
    filt_count == 1
    filt[0] != NULL

  "LpBu2/1000,HpBu2/5000":
    filt_count == 2

  Error case: "" → error
```

### 6.3 SIL: firun as Black Box (Process Tests)

SIL tests call `firun` via `popen`/`subprocess` and check stdout against reference data.

#### Impulse response (format ASCII)

```sh
echo "" | ./firun -r LpBu4/400 -s 44100
```

Expected: frequency response lines, `0.000000 1.0` at f=0.

```sh
./firun -I 44100 LpBu4/400 -o a
```

Expected: impulse response as ASCII, first line ~ 1.0, decays after cnt_max samples.

#### Frequency response (format ASCII)

```sh
./firun -r LpBu4/400 -s 44100 -n 100
```

Lines: `freq response [phase]`.  
Check: line at f≈0 → response ≈ 1.0; line at f≈fc → response ≈ 0.707; line at f≈10fc → response < 0.01.

#### Format round-trip (binary)

```sh
./firun -I 44100 LpBu4/400 -o d | ./firun -I 44100 LpBu4/400 -i d -o a
```

ASCII output must match direct ASCII output (float64 round-trip is lossless).

#### Multi-channel operation

```sh
./firun -n 2 -I 44100 LpBu4/400 -o a
```

Output has twice as many columns as single-channel version.

#### Streaming mode

```sh
dd if=/dev/zero bs=4 count=1 | ./firun -s 44100 LpBu4/400 -i f -o f | wc -c
```

Output: 4 bytes (exactly one float32 back).

### 6.4 Regression: Reference Data from fiview_log.txt

`doc/examples/fiview_log.txt` contains a complete Butterworth LP analysis.
Extracts from it as golden reference:

```
tests/fixtures/LpBu6_400_44100_response.csv   ← frequency response
tests/fixtures/LpBu6_400_44100_impulse.csv    ← impulse response (cnt99 samples)
```

Test: firun output compared against these CSVs (maximum deviation < 1e-5).

---

## 7. Mock Strategies

### 7.1 Error Handler Mock (fidlib)

fidlib calls `abort()` by default on error. For tests:

```c
static int error_triggered;
static void mock_error(const char *msg) {
    error_triggered = 1;
    longjmp(error_jmp, 1);
}
// In test:
fid_set_error_handler(mock_error);
if (setjmp(error_jmp) == 0) {
    fid_design(0, 44100.0, 0, 0.0, 0.0, "INVALID");
    assert(0); // should not reach
}
assert(error_triggered);
```

This pattern is already established in `test_butterworth.c`.

### 7.2 File I/O Mock (filter_load_file)

`filter_load_file` opens a file via `fopen`. Tests use temporary files:

```c
// Write fixture:
FILE *f = fopen("/tmp/test_filter.filt", "w");
fputs("LpBu4/400\n", f);
fclose(f);
// Test:
assert(filter_load_file("/tmp/test_filter.filt") == 1);
```

No interposition needed — real I/O with tmp files is more stable than dlsym mocks.

### 7.3 Scratch Buffer Isolation

Since `scratch` is global, every test must call `scr_zap()` beforehand. CTest tests
are separate processes anyway — no cross-test state possible.

### 7.4 firun stdin/stdout Mock

firun reads `stdin` and writes `stdout`. SIL tests via pipes without a real mock:

```c
// popen-based test (POSIX):
FILE *p = popen("./firun -I 44100 LpBu4/400 -o a", "r");
// parse output
pclose(p);
```

Alternatively: firun in subprocess with `pipe()/fork()/exec()` for timing control.

---

## 8. display.c / Non-SDL Logic

fiview/src/display.c contains pure data logic in addition to SDL render code:

### 8.1 setup_pager / progress_init (state-only)

```
  setup_pager("text", "info", 0):
    s_pager_txt == "text"
    s_pager_inf == "info"
    s_pager_typ == 0
    s_pager_cnt == 0

  progress_init(&pr, 100, "loading", 40):
    pr.max == 100
    pr.txt == "loading"
    pr.wid == 40
    pr.cur == 0
```

These functions do not need SDL if the render path is abstracted via a flag
(e.g. `#ifdef TEST_NO_SDL`). Prerequisite: refactoring of the display.c initialization path.

### 8.2 inRect

```
  Rect r = {10, 10, 100, 100};
  inRect(&r, 50, 50) == 1
  inRect(&r, 5, 5)   == 0
  inRect(&r, 10, 10) == 1  (boundary point)
  inRect(&r, 110, 110) == 0
```

This function is pure (no side effects), directly testable.

---

## 9. CTest Infrastructure

### 9.1 Directory Structure (Target)

```
tests/
  CMakeLists.txt
  test_butterworth.c          ← existing
  test_fidlib_api.c           ← new: complete API coverage
  test_scratch.c              ← new
  test_filter_analysis.c      ← new: filter_response, setup_gain, setup_cnt, dump
  test_filter_load.c          ← new: load_immed, load_file
  test_firun_sil.c            ← new: popen-based SIL tests
  test_display_logic.c        ← new: inRect, pager/progress state (when #ifdef)
  fixtures/
    simple.filt
    LpBu6_400_44100_response.csv
    LpBu6_400_44100_impulse.csv
```

### 9.2 CMakeLists.txt — Extension

```cmake
add_executable(test_fidlib_api     test_fidlib_api.c)
target_link_libraries(test_fidlib_api PRIVATE fidlib)
target_compile_options(test_fidlib_api PRIVATE -fsanitize=address,undefined)
target_link_options(test_fidlib_api PRIVATE -fsanitize=address,undefined)
add_test(NAME fidlib_api COMMAND test_fidlib_api)

add_executable(test_scratch        test_scratch.c
    ${CMAKE_SOURCE_DIR}/fiview/src/scratch.c)
target_include_directories(test_scratch PRIVATE
    ${CMAKE_SOURCE_DIR}/fiview/src
    ${CMAKE_SOURCE_DIR}/vendor/fidlib)
add_test(NAME scratch COMMAND test_scratch)

# ... analogous for further targets
```

### 9.3 Sanitizer Baseline

All tests run in debug build with:
```
-fsanitize=address,undefined
-fno-omit-frame-pointer
```

No test may produce an ASan/UBSan report — this is a mandatory gate for CI.

### 9.4 CTest Labels

```cmake
set_tests_properties(fidlib_api    PROPERTIES LABELS "unit;fidlib")
set_tests_properties(scratch       PROPERTIES LABELS "unit;fiview")
set_tests_properties(filter_analysis PROPERTIES LABELS "unit;fiview;numerical")
set_tests_properties(firun_sil     PROPERTIES LABELS "sil;integration")
```

Execution by label:
```sh
ctest --test-dir build -L unit
ctest --test-dir build -L sil
```

---

## 10. Test Pyramid and Prioritization

```
        [ Regression / SIL ]   ← firun_sil, fiview_log golden ref
       [  Component / Integration ]  ← filter_analysis, filter_load
      [   Unit / Numerical  ]   ← fidlib_api, scratch, display_logic
```

Implementation order by risk:

| Priority | Test | Value |
|---|---|---|
| 1 (now) | `test_fidlib_api.c` | Foundation — all others build on this |
| 2 | `test_filter_analysis.c` | Numerical correctness of analysis functions |
| 3 | `test_scratch.c` | Simple, high safety net |
| 4 | `test_filter_load.c` | I/O paths, error handling |
| 5 | `test_firun_sil.c` | Full pipeline test, requires firun build |
| 6 | Fixture CSVs | Regression basis from fiview_log.txt |
| 7 | `test_display_logic.c` | Only sensible after #ifdef refactoring |

---

## 11. What is Explicitly NOT Tested

- SDL render code (window, pixels, events) — no headless SDL in CI environment
- fiview main loop (event loop) — too much SDL coupling
- Platform specifics (MSVC/Windows paths in fidlib) — out of scope for RasPi target

---

## Appendix A: Known Reference Values

From `doc/examples/fiview_log.txt`, LpBu6 @ fc=400 Hz, sr=44100 Hz:

```
DC gain:             1.000000
Gain @ fc:          ~0.707107   (-3.01 dB)
Gain @ 10·fc (4000 Hz): ~3.2e-7  (-130 dB)
cnt50:               ~9
cnt90:               ~32
cnt99:               ~59
cnt_max:             ~130
```

These values are mandatory reference for `test_filter_analysis.c` and SIL tests.

---

## Appendix B: Checklist for New Filter Types

For each new filter type to be tested:

- [ ] DC gain correct (0.0 or 1.0)
- [ ] Nyquist gain correct
- [ ] Cutoff frequency within ±1 % of fc
- [ ] Stopband value below specification
- [ ] Impulse response decays (< 1e-5 after cnt_max)
- [ ] ASan/UBSan clean
- [ ] fid_run_zapbuf produces identical response on second run
