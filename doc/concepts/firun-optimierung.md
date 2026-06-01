# Konzept: firun — C++20, RT, Robustheit

## Ziel

firun ist das CLI-Frontend für fidlib. Aktuell: direkte Portierung aus
`vendor/fidlib/firun.c`. Ziel ist kein Umbau des Algorithmus, sondern:

1. Mit C++20-Compiler übersetzbar (analog fidlib)
2. Robustheit: Fehlerbehandlung ohne `exit()` tief im Aufrufstack
3. Kleine Strukturverbesserungen für Lesbarkeit und Wartbarkeit

---

## Analyse firun.c

firun.c ist ~600 Zeilen C. Es:
- Parst CLI-Argumente (`argc/argv`)
- Ruft `fid_design()` und `fid_run_new()` auf
- Liest Samples von stdin, filtert, schreibt auf stdout
- Nutzt `fprintf(stderr, ...)` + `exit(1)` für Fehler

### Bekannte C++20-Hürden

| Problem | Zeile (ca.) | Fehler in C++20 |
|---|---|---|
| String-Literal → `char *` | div. | `-Werror=write-strings` |
| `void*`-Casts implizit | div. | invalid conversion |
| `exit()` direkt nach Fehlermeldung | div. | kein Stackunwind, kein Cleanup |

### Maßnahmen

```
- char * → const char * für alle String-Literale und fmt-Parameter
- Fehlerausgabe: zentralen err_exit()-Wrapper einführen der
  [[noreturn]] annotiert ist und stderr + exit(1) kapselt
- Alle void*-Casts explizit
- cmake: target_compile_options(firun ... -Wno-old-style-cast) für
  FIDLIB_CXX20_COMPAT-Modus (CLI-Code ist weniger streng als Library)
```

---

## Phase 1 — C++20-Kompilierbarkeit

- `char *` String-Literale → `const char *`
- `err_exit(const char *fmt, ...)` mit `FID_NORETURN` einführen
- `void*`-Casts explizit
- cmake-Option: firun baut mit `-std=c++20` wenn `FIDLIB_CXX20_COMPAT=ON`

## Phase 2 — Robustheit

- `fid_set_error_handler()` registrieren: bei Fehler in fidlib setzt
  Handler ein globales Flag, firun gibt saubere Fehlermeldung aus
- Puffer-Handling: feste Stack-Puffer durch size-checked Varianten ersetzen
- Signal-Handling für SIGPIPE (stdout geschlossen)

## Phase 3 — Erweiterungen (optional, eigene Tickets)

- `--format float32|int16|double` für flexible Sample-Formate
- `--channels N` für Multi-Kanal (nutzt mehrere `fid_run_newbuf` Instanzen)
- Streaming-Modus ohne Latenz-Overhead (direkte Read/Write ohne stdio-Puffer)

---

## cmake-Integration

```cmake
# cli/CMakeLists.txt — Ergänzung:
if(FIDLIB_CXX20_COMPAT)
  set_source_files_properties(firun.c PROPERTIES LANGUAGE CXX)
  target_compile_options(firun PRIVATE -std=c++20 -Wno-old-style-cast)
endif()
```

---

*Erstellt: 2026-05-27*
