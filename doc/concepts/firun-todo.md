# TODO: firun — C++20, Robustness ✓

Branch: `feature/firun-optimierung` → merged into develop
Concept: `doc/concepts/firun-optimierung.md`

## Phase 1 — C++20 Compilability
- [x] `char *` string literals → `const char *` in `firun.c`
- [x] `error(const char *fmt, ...)` with `FID_NORETURN`, `usage()` likewise
- [x] `decode_spec`/`spec_count`: `const char *` parameters
- [x] `refill_input()`: `static void` + `void` parameter
- [x] `cli/CMakeLists.txt`: `FIRUN_CXX20_COMPAT` option added
- [x] Build with `-std=c++20` completes without errors

## Phase 2 — Robustness
- [x] Register `fid_set_error_handler()` in firun (fid_err_handler)
- [x] Stack buffer checked: buf[32] in output() sufficient for all formats
- [x] SIGPIPE handler: `signal(SIGPIPE, SIG_IGN)` in main()

## Phase 3 — Extensions (optional)
- [x] `d` format (64-bit double I/O) as new format character
- [x] `-n N` for multi-channel (shorthand: `-n2 f` expands to `ff`)
- [x] `-s` streaming mode (setvbuf _IONBF, unbuffered stdout)

## Completion
- [x] All tests pass
- [x] Commit + merge → develop
