# TODO: firun — C++20, Robustheit ✓

Branch: `feature/firun-optimierung` → gemergt nach develop
Konzept: `doc/concepts/firun-optimierung.md`

## Phase 1 — C++20-Kompilierbarkeit
- [x] `char *` String-Literale → `const char *` in `firun.c`
- [x] `error(const char *fmt, ...)` mit `FID_NORETURN`, `usage()` ebenso
- [x] `decode_spec`/`spec_count`: `const char *` Parameter
- [x] `refill_input()`: `static void` + `void`-Parameter
- [x] `cli/CMakeLists.txt`: `FIRUN_CXX20_COMPAT` Option hinzugefügt
- [x] Build mit `-std=c++20` fehlerfrei

## Phase 2 — Robustheit
- [x] `fid_set_error_handler()` in firun registrieren (fid_err_handler)
- [x] Stack-Puffer geprüft: buf[32] in output() ausreichend für alle Formate
- [x] SIGPIPE-Handler: `signal(SIGPIPE, SIG_IGN)` in main()

## Phase 3 — Erweiterungen (optional)
- [x] `d`-Format (64-bit double I/O) als neues Format-Zeichen
- [x] `-n N` für Multi-Kanal (Shorthand: `-n2 f` expandiert zu `ff`)
- [x] `-s` Streaming-Modus (setvbuf _IONBF, unbuffered stdout)

## Abschluss
- [x] Alle Tests grün
- [x] Commit + Merge → develop
