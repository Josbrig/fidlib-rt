# TODO: fidlib RT-Optimierung / C++20 ✓

Branch: `feature/fidlib-rt-optimierung` → gemergt nach develop
Konzept: `doc/concepts/fidlib-cpp20-rt-optimierung.md`

---

## Phase 1 — C++20-Kompilierbarkeit ✓

- [x] `register`-Keyword entfernen (`fidrf_cmdlist.h`)
- [x] Alle `void*`-Casts explizit machen (`Alloc()`, `fid_run_newbuf()`, `fid_run_initbuf()`)
- [x] `error()` mit `[[noreturn]]`-Attribut annotieren (portabler Wrapper via `FID_NORETURN`)
- [x] `double buf[0]` (flexible array) in `fidrf_combined.h` auf `double buf[1]` ändern
- [x] cmake-Option `FIDLIB_CXX20_COMPAT` in `lib/CMakeLists.txt` anlegen
- [x] Build mit `-std=c++20` fehlerfrei durchlaufen lassen

## Phase 2 — Sicherheit ✓

- [x] `error()`-Callback-API einführen (`fid_set_error_handler()`) — kein `exit()` im RT-Pfad
- [x] Default-Handler bleibt `exit()`, RT-Nutzer können eigenen setzen
- [x] `const FidFilter *` für `fid_response()` und `fid_response_pha()`
- [x] `const double *coef` in `RunBuf` (read-only im Hot-Path)
- [x] Overflow-Check in `FFCSIZE`-Makro (`__builtin_add_overflow`)

## Phase 3 — Realtime-Tauglichkeit ✓

- [x] API-Kommentar: Alloc-Phase vs. Run-Phase klar dokumentieren
- [x] `fid_run_newbuf_inplace()` als Makro-Alias für `fid_run_initbuf()`
- [x] Cache-Layout konsolidieren: `buf[]`, `coef[]`, `cmd[]` in einem Block allozieren
- [x] `FID_LIKELY` / `FID_UNLIKELY` Makros einführen
- [x] `FID_LIKELY` auf `END`-Befehl in `filter_step()` anwenden

## Phase 4 — Geschwindigkeit ✓

- [x] cmake-Option `FIDLIB_LTO` für Link-Time Optimization
- [x] `-O3 -ffast-math` scoped auf fidlib (nur `fidrf_cmdlist.h`-Compile-Unit)
- [ ] SIMD (NEON/ARM): eigenes Feature-Ticket, hier nur als Placeholder

## Abschluss ✓

- [x] Alle bestehenden Tests weiterhin grün
- [x] Neuen Test: `fid_run_newbuf_inplace`, `fid_set_error_handler()` — kein `exit()` bei ungültigem Filter
- [x] Commit + Merge → develop
