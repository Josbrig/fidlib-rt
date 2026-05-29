# TODO: fidlib RT optimization / C++20 ✓

Branch: `feature/fidlib-rt-optimierung` → merged into develop
Concept: `doc/concepts/fidlib-cpp20-rt-optimierung.md`

---

## Phase 1 — C++20 Compilability ✓

- [x] Remove `register` keyword (`fidrf_cmdlist.h`)
- [x] Make all `void*` casts explicit (`Alloc()`, `fid_run_newbuf()`, `fid_run_initbuf()`)
- [x] Annotate `error()` with `[[noreturn]]` attribute (portable wrapper via `FID_NORETURN`)
- [x] Change `double buf[0]` (flexible array) in `fidrf_combined.h` to `double buf[1]`
- [x] Create cmake option `FIDLIB_CXX20_COMPAT` in `lib/CMakeLists.txt`
- [x] Build with `-std=c++20` completes without errors

## Phase 2 — Security ✓

- [x] Introduce `error()` callback API (`fid_set_error_handler()`) — no `exit()` in RT path
- [x] Default handler remains `exit()`, RT users can set their own
- [x] `const FidFilter *` for `fid_response()` and `fid_response_pha()`
- [x] `const double *coef` in `RunBuf` (read-only in hot path)
- [x] Overflow check in `FFCSIZE` macro (`__builtin_add_overflow`)

## Phase 3 — Realtime Safety ✓

- [x] API comment: clearly document alloc phase vs. run phase
- [x] `fid_run_newbuf_inplace()` as macro alias for `fid_run_initbuf()`
- [x] Consolidate cache layout: allocate `buf[]`, `coef[]`, `cmd[]` in one block
- [x] Introduce `FID_LIKELY` / `FID_UNLIKELY` macros
- [x] Apply `FID_LIKELY` to `END` command in `filter_step()`

## Phase 4 — Performance ✓

- [x] cmake option `FIDLIB_LTO` for link-time optimization
- [x] `-O3 -ffast-math` scoped to fidlib (only `fidrf_cmdlist.h` compile unit)
- [ ] SIMD (NEON/ARM): separate feature ticket, placeholder only here

## Completion ✓

- [x] All existing tests still pass
- [x] New test: `fid_run_newbuf_inplace`, `fid_set_error_handler()` — no `exit()` on invalid filter
- [x] Commit + merge → develop
