# TODO — digitalfilterdesign

---

## feature/cmake-setup ✓ (merged → develop)

- [x] Create root `CMakeLists.txt` (C99, compiler flags, sanitizer option)
- [x] Create `fidlib/`: fidlib source files + `fidlib/CMakeLists.txt`
- [x] Clean up `fidlib.c` for C99: warning-clean, UB/ASan-clean
- [x] `fidrf_cmdlist.h`: fixed null-pointer bug in `RunBuf.buf`
- [x] Create `firun/`: `firun.c` + `firun/CMakeLists.txt`
- [x] Create `tests/`: Butterworth smoke test against `fiview_log.txt`
- [x] `fiview/CMakeLists.txt`: fiview via ExternalProject + SDL 1.2
- [x] Build + tests green
- [x] Commit + push + merge → develop

---

## feature/fidlib-rt-optimierung ✓ (merged → develop)

Concept: `doc/concepts/fidlib-cpp20-rt-optimierung.md`

### Phase 1 — C++20 Compilability ✓
- [x] Remove `register` keyword
- [x] All `void*` casts explicit (`Alloc`, `RunBuf` initialisation)
- [x] `error()` annotated with `FID_NORETURN` (portable wrapper)
- [x] `double buf[0]` → `double buf[1]` in `fidrf_combined.h`
- [x] cmake option `FIDLIB_CXX20_COMPAT` — build with `-std=c++20` error-free

### Phase 2 — Safety ✓
- [x] `error()`/`fid_set_error_handler()`: `const char *` throughout
- [x] `const FidFilter *` for `fid_response()`, `fid_response_pha()`, `fid_calc_delay()`, `fid_run_new()`
- [x] `const double *coef`, `const char *cmd` in `RunBuf`
- [x] `FFCNEXT` macro for const-correct filter traversal

### Phase 3 — Realtime ✓
- [x] Alloc/Run phase documented in `fidlib.h`
- [x] `fid_run_newbuf_inplace()` as macro alias for `fid_run_initbuf()`
- [x] Cache layout consolidated: `buf[]`, `coef[]`, `cmd[]` in one block
- [x] `FID_LIKELY` / `FID_UNLIKELY` + application in `filter_step()`

### Phase 4 — Speed ✓
- [x] `FIDLIB_LTO` cmake option
- [x] `FIDLIB_FAST_MATH` cmake option (`-O3 -ffast-math` scoped)
- [ ] SIMD/NEON: separate feature ticket (open)

### Completion ✓
- [x] All tests green (C99 + C++20)
- [x] New tests: `fid_run_newbuf_inplace`, `fid_set_error_handler`
- [x] Commit + push

---

## feature/firun-optimierung ✓ (merged → develop)

Concept: `doc/concepts/firun-optimierung.md`

### Phase 1 — C++20 Compilability ✓
- [x] `char *` → `const char *` for literals and read-only parameters
- [x] `error()` + `usage()` with `FID_NORETURN`, `const char *fmt`
- [x] `refill_input()` as `static void`
- [x] `FIRUN_CXX20_COMPAT` cmake option — build with `-std=c++20` error-free

### Phase 2 — Robustness ✓
- [x] `fid_set_error_handler()` registered (fid_err_handler with "firun: fidlib: " prefix)
- [x] Stack buffer buf[32] in output() verified — sufficient
- [x] SIGPIPE ignored via `signal(SIGPIPE, SIG_IGN)`

### Phase 3 — Extensions ✓
- [x] `d` format (64-bit double I/O)
- [x] `-n N` channel shorthand
- [x] `-s` streaming mode (setvbuf _IONBF)

---

## feature/fiview-sdl2 ✓ (merged → develop)

Concept: `doc/concepts/fiview-optimierung.md`

### Phase 1 — SDL2 Migration ✓
- [x] `FIVIEW_USE_SDL2` cmake option, SDL2 via ExternalProject
- [x] `graphics.c`: SDL_CreateWindow + SDL_CreateRenderer + SDL_CreateTexture(ARGB8888)
- [x] Own disp_pix32 buffer, update() via SDL_UpdateTexture + RenderPresent
- [x] `fiview.c`: SDL_WINDOWEVENT_RESIZED, SDL_EnableKeyRepeat removed
- [x] SDL1 build backwards compatible

### Phase 2 — C++20 Compilability ✓
- [x] `helptext.c`: const char *, duplicate definition fixed
- [x] `all.h`: `<fidlib/fidlib.h>` angle brackets for cmake redirect
- [x] `FIVIEW_CXX20_COMPAT` cmake option — build with `-std=c++20 -fpermissive` error-free

### Phase 3 — Architecture ✓
- [x] Runtime filter switching (F5 key → prompt)
- [x] Multi-filter overlay (o key toggle)
- [x] Frequency response export as CSV (e key → fiview_freq.csv)

---

## feature/vendor-cleanup ✓ (merged → develop)

- [x] `vendor/` completely removed (fidlib, mkfilter, fiview, gmeteor)
- [x] fiview sources moved to `fiview/src/`
- [x] `fiview/CMakeLists.txt`: SDL1 `--build` triple dynamic via `uname -m`
- [x] `doc/fiview-display-setup.md`: X11 forwarding and screenshot capture
- [x] `.gitmodules` cleared, submodules deregistered

---

## refactor(fiview): Header Cleanup ✓ (directly on develop)

- [x] `#ifdef HEADER` trick removed: all `.c` files are regular C files
- [x] `proto.h` removed — prototypes live in their respective modules
- [x] New headers: `filter.h`, `display.h`, `scratch.h`, `graphics.h`, `helptext.h`, `fiview.h`
- [x] Each header self-contained (own guards, own includes)
- [x] `all.h` cleaned: no `.c` file include, no circular include
- [x] Empty parameter lists as `(void)`, `FID_NORETURN` on error functions
- [x] `ALLOC_ARR`: `size_t` cast — sign-conversion warning fixed

---

## feature/portability-analysis ✓ (merged → develop)

Analysis: `doc/concepts/portability-analysis.md`

- [x] Portability analysis: compiler, standards, architectures, OS
- [x] cmake: macOS/Windows P1–P5 portability measures
- [x] `FIVIEW_SDL_UPSTREAM` option introduced
- [x] firun: POSIX guards for ssize_t and SIGPIPE

---

## feature/strict-compiler-flags ✓ (merged → develop)

- [x] Strict compiler flags: `-Wall -Wextra -Wconversion -Wshadow -Werror`
- [x] Global `CXX20_COMPAT` umbrella switch
- [x] `fix(fidlib)`: FFSIZE macro and empty parameter lists C++20 compatible
- [x] `fix(fiview)`: const correctness and void* casts

---

## feature/test-coverage ✓ (merged → develop)

Concept: `doc/test-concept.md`

- [x] SDL-free test infrastructure: `tests/support/test_all.h`, `stubs.c`
- [x] `tests/fixtures/simple.filt`
- [x] `test_butterworth` (smoke, existing, extended)
- [x] `test_fidlib_api` — full fidlib API coverage
- [x] `test_scratch` — scratch.c unit tests
- [x] `test_filter_analysis` — numerical tests (response, phase, cnt, run)
- [x] `test_filter_load` — filter_load_immed, filter_load_file
- [x] `test_firun_sil` — firun black-box SIL tests (popen)
- [x] `fix(display)`: (char) casts for 0x80 escape bytes (Intel/ARM difference)
- [x] `fix(graphics)`: void*→Uint16*/Uint32* casts for C++ compatibility
- [x] C standard: `CMAKE_C_STANDARD 99` → `17`
- [x] SDL2 mirror activated as default (`FIVIEW_SDL_UPSTREAM OFF`)
- [x] 6/6 tests green (ASAN/UBSAN, ARM + x86)

---

## feature/documentation ✓ (merged → develop)

- [x] README.md updated (status, build, tests, structure)
- [x] TODO.md updated (all features documented)
- [x] doc/sources-to-secure.md: SDL mirror status updated
- [x] doc/todo-test-coverage.md: marked as completed
- [x] doc/concepts/portability-analysis.md: C standard corrected to 17

---

## feature/merge-sicherheit ✓ (merged → develop)

- [x] `.gitattributes`: `TODO.md`, `CHANGELOG.md` with `merge=union`
- [x] `scripts/hooks/post-merge`: warns on line loss > 4
- [x] `scripts/install-hooks.sh`: symlink installer
- [x] CLAUDE.md section G: mandatory merge verification after every merge
- [x] DokuWiki: `claude:git-merge-sicherheit` + `develop:digitalfilterdesign:merge-sicherheit`

---

## feature/simd-neon-optimierung ← current

- [x] `fidlib/fid_simd.h`: SIMD detection + `fid_fir_dot()` (NEON / SSE2 / scalar fallback)
- [x] `fidrf_cmdlist.h`: opcode-8 (4N× FIR) SIMD path + sentinel slot in delay buffer
- [x] `FIDLIB_SIMD` cmake option (OFF default) with architecture detection (AArch64/x86_64)
- [x] `tests/test_fidlib_simd.c`: primitive test `fid_fir_dot` + boxcar impulse + `initbuf/zapbuf`
- [x] All tests green (NEON active, ASAN/UBSAN)

---

## Open

- [x] **Buffer overflow `./fiview -D` on Intel (x86_64)**: Fixed in `scr_prw()` —
  before memmove at word-wrap insertion, `scr_realloc()` is now called when
  `i1 + 1 + scr_indlen + 1 >= scr_max`. SDL1+SDL2 both run without crash.
