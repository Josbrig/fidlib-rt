# TODO: Test Coverage (feature/test-coverage) ✓

Branch: `feature/test-coverage` → merged into develop
Concept: `doc/test-concept.md`

---

## Phase 0 — Infrastructure ✓

- [x] Create `tests/fixtures/` directory
- [x] Write fixture `tests/fixtures/simple.filt` (content: `LpBu4/400`)
- [x] `tests/support/test_all.h` — SDL-free all.h replacement via `-include`
- [x] `tests/support/stubs.c` — longjmp error mock, Alloc, globals
- [x] `tests/CMakeLists.txt` reworked: sanitizer defaults, label infrastructure, `add_fiview_module_test` macro

---

## Phase 1 — fidlib API ✓

- [x] `fid_design`: LpBu4, HpBu4, BpBu2, BsBu4, ApRe, LpBe6, LpCh4
- [x] `fid_flatten` + `fid_cat`: series filter, equivalence
- [x] `fid_run_*` lifecycle: newbuf, freebuf, zapbuf, inplace, two independent bufs
- [x] `fid_cv_array` + `fid_design_coef`
- [x] `fid_parse` / `fid_rewrite_spec`
- [x] `fid_set_error_handler` (longjmp mock): fc>Nyquist, f0>f1, unknown spec, order 32
- [x] `fid_list_filters_buf`, `fid_version`, `fid_calc_delay`
- [x] Registered in CMakeLists.txt, label `unit;fidlib`, `ASAN_OPTIONS=detect_leaks=0`

---

## Phase 2 — filter.c Numerics ✓

- [x] `filter_response`: LpBu4, HpBu4, BpBu2 DC/Nyquist/fc
- [x] `filter_response` with phase: Butterworth N·45° invariant
- [x] `filter_resp_range`: array length, values in [0,1.05]
- [x] `filter_setup_gain`: type, gain100, m3db, m6db
- [x] `filter_setup_cnt`: order cnt50<cnt90<cnt99<cnt999, reference values LpBu6/400
- [x] `filter_run` impulse response + step response + zapbuf equivalence
- [x] `filter_dump`: strings, no crash on multi-stage filter
- [x] Label `unit;fiview;numerical`, `ASAN_OPTIONS=detect_leaks=0`

---

## Phase 3 — scratch ✓

- [x] `scr_zap`, `scr_pr`, `scr_lf`, `SCR_PUTC`, `scr_zap_pr`
- [x] `scr_prw`: wrap, indentation, long word, empty string
- [x] `scr_inc`, `scr_wrD`, `scr_wrI`: binary round-trip
- [x] `scr_realloc`: 40000 bytes, scr_max grows, ASan-clean
- [x] `scr_dup`: independent copy
- [x] Label `unit;fiview`

---

## Phase 4 — filter_load ✓

- [x] `filter_load_immed("LpBu4/400")`: n_filt==1
- [x] `filter_load_immed("LpBu4/400,HpBu4/5000")`: n_filt==2
- [x] `filter_load_file` from fixture
- [x] `filter_load_file("/nonexistent")`: longjmp mock
- [x] Label `unit;fiview`, `ASAN_OPTIONS=detect_leaks=0`

---

## Phase 5 — firun SIL ✓

- [x] Impulse response: peak in [0.001,2.0], decay, n_lines==N_IMP
- [x] Frequency response `-r`: DC≈1.0, fc≈0.707, 10·fc<0.01
- [x] Float64 round-trip: `%I/d` → `d/a 'x 1'` identical to `%I/a` (Δ<1e-9)
- [x] Multi-channel: `-n 2 %I/aa` → 2 equal values per line
- [x] Label `sil;integration`

---

## Phase 6 — Sanitizer Baseline ✓

- [x] All test targets with `-fsanitize=address,undefined -fno-omit-frame-pointer`
- [x] 6/6 tests ASan/UBSan-clean (ARM Raspberry Pi 5)
- [x] `fix(display)`: (char) casts for 0x80 (Intel signed-char vs ARM unsigned-char)
- [x] `fix(graphics)`: void*→Uint16*/Uint32* casts for C++
- [x] `ASAN_OPTIONS=detect_leaks=0` per CTest ENVIRONMENT for filter tests (Alloc heap by design)

---

## Phase 7 — display_logic (open)

- [ ] Prerequisite: `#ifdef TEST_NO_SDL` guard in display.c around SDL render calls
- [ ] `inRect`, `setup_pager`, `progress_init` state tests
- [ ] Label `unit;fiview`

---

## Cross-references

| What | Where |
|---|---|
| Test concept | `doc/test-concept.md` |
| Reference log | `doc/examples/fiview_log.txt` |
| fidlib public API | `fidlib/fidlib.h` |
| filter.c / filter.h | `fiview/src/filter.c`, `fiview/src/filter.h` |
| scratch.c / scratch.h | `fiview/src/scratch.c`, `fiview/src/scratch.h` |
| firun.c | `firun/firun.c` |
