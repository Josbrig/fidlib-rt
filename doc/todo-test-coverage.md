# TODO: Test-Coverage (feature/test-coverage) ✓

Branch: `feature/test-coverage` → gemergt nach develop  
Konzept: `doc/test-concept.md`

---

## Phase 0 — Infrastruktur ✓

- [x] `tests/fixtures/` Verzeichnis anlegen
- [x] Fixture `tests/fixtures/simple.filt` schreiben (Inhalt: `LpBu4/400`)
- [x] `tests/support/test_all.h` — SDL-freier all.h-Ersatz via `-include`
- [x] `tests/support/stubs.c` — longjmp-Error-Mock, Alloc, Globals
- [x] `tests/CMakeLists.txt` überarbeitet: Sanitizer-Defaults, Label-Infrastruktur, `add_fiview_module_test`-Makro

---

## Phase 1 — fidlib API ✓

- [x] `fid_design`: LpBu4, HpBu4, BpBu2, BsBu4, ApRe, LpBe6, LpCh4
- [x] `fid_flatten` + `fid_cat`: Reihenfilter, Äquivalenz
- [x] `fid_run_*` Lifecycle: newbuf, freebuf, zapbuf, inplace, zwei unabhängige Bufs
- [x] `fid_cv_array` + `fid_design_coef`
- [x] `fid_parse` / `fid_rewrite_spec`
- [x] `fid_set_error_handler` (longjmp-Mock): fc>Nyquist, f0>f1, unbekannte Spec, Ordnung 32
- [x] `fid_list_filters_buf`, `fid_version`, `fid_calc_delay`
- [x] In CMakeLists.txt eingebunden, Label `unit;fidlib`, `ASAN_OPTIONS=detect_leaks=0`

---

## Phase 2 — filter.c Numerik ✓

- [x] `filter_response`: LpBu4, HpBu4, BpBu2 DC/Nyquist/fc
- [x] `filter_response` mit Phase: Butterworth N·45°-Invariante
- [x] `filter_resp_range`: Array-Länge, Werte in [0,1.05]
- [x] `filter_setup_gain`: typ, gain100, m3db, m6db
- [x] `filter_setup_cnt`: Ordnung cnt50<cnt90<cnt99<cnt999, Referenzwerte LpBu6/400
- [x] `filter_run` Impulsantwort + Sprungantwort + zapbuf-Äquivalenz
- [x] `filter_dump`: Strings, kein Crash bei Mehrfachfilter
- [x] Label `unit;fiview;numerical`, `ASAN_OPTIONS=detect_leaks=0`

---

## Phase 3 — scratch ✓

- [x] `scr_zap`, `scr_pr`, `scr_lf`, `SCR_PUTC`, `scr_zap_pr`
- [x] `scr_prw`: Wrap, Einrückung, langes Wort, leerer String
- [x] `scr_inc`, `scr_wrD`, `scr_wrI`: Binary-Round-Trip
- [x] `scr_realloc`: 40000 Bytes, scr_max wächst, ASan-clean
- [x] `scr_dup`: unabhängige Kopie
- [x] Label `unit;fiview`

---

## Phase 4 — filter_load ✓

- [x] `filter_load_immed("LpBu4/400")`: n_filt==1
- [x] `filter_load_immed("LpBu4/400,HpBu4/5000")`: n_filt==2
- [x] `filter_load_file` aus Fixture
- [x] `filter_load_file("/nonexistent")`: longjmp-Mock
- [x] Label `unit;fiview`, `ASAN_OPTIONS=detect_leaks=0`

---

## Phase 5 — firun SIL ✓

- [x] Impulsantwort: Peak in [0.001,2.0], Abklingen, n_lines==N_IMP
- [x] Frequenzgang `-r`: DC≈1.0, fc≈0.707, 10·fc<0.01
- [x] Float64 Round-Trip: `%I/d` → `d/a 'x 1'` identisch zu `%I/a` (Δ<1e-9)
- [x] Mehrkanaligkeit: `-n 2 %I/aa` → 2 gleiche Werte pro Zeile
- [x] Label `sil;integration`

---

## Phase 6 — Sanitizer-Baseline ✓

- [x] Alle Test-Targets mit `-fsanitize=address,undefined -fno-omit-frame-pointer`
- [x] 6/6 Tests ASan/UBSan-clean (ARM Raspberry Pi 5)
- [x] `fix(display)`: (char)-Casts für 0x80 (Intel signed-char vs ARM unsigned-char)
- [x] `fix(graphics)`: void*→Uint16*/Uint32* Casts für C++
- [x] `ASAN_OPTIONS=detect_leaks=0` per CTest ENVIRONMENT für Filter-Tests (Alloc-Heap by design)

---

## Phase 7 — display_logic (offen)

- [ ] Voraussetzung: `#ifdef TEST_NO_SDL`-Guard in display.c um SDL-Renderaufrufe
- [ ] `inRect`, `setup_pager`, `progress_init` State-Tests
- [ ] Label `unit;fiview`

---

## Querbezüge

| Was | Wo |
|---|---|
| Testkonzept | `doc/test-concept.md` |
| Referenz-Log | `doc/examples/fiview_log.txt` |
| fidlib Public API | `fidlib/fidlib.h` |
| filter.c / filter.h | `fiview/src/filter.c`, `fiview/src/filter.h` |
| scratch.c / scratch.h | `fiview/src/scratch.c`, `fiview/src/scratch.h` |
| firun.c | `firun/firun.c` |
