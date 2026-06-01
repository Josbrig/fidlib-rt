# Konzept: fidlib — C++20-Kompilierbarkeit, Realtime-Tauglichkeit, Sicherheit

## Ziel

fidlib ist in C99 geschrieben. Ziel ist **nicht** eine Portierung nach C++20,
sondern:

1. Den bestehenden C-Code mit einem C++20-Compiler (`-std=c++20`) übersetzbar
   machen — ohne Umbau der Logik.
2. Die durch C++20-Analyse gewonnenen Erkenntnisse nutzen, um gezielt
   Sicherheit, Geschwindigkeit und Realtime-Tauglichkeit zu verbessern.

---

## Phase 1 — C++20-Kompilierbarkeit herstellen

Der C++20-Compiler ist strenger als C99 und erzwingt Korrektheit, die im
C-Compiler still toleriert wurde. Das Ziel dieser Phase ist ein **sauberer
Build ohne `-fpermissive`**.

### Bekannte Hürden in fidlib.c

| Problem | Ort | C++20-Fehler |
|---|---|---|
| `void *`-Casts implizit | `Alloc()`, `fid_run_newbuf()` | `error: invalid conversion from 'void*'` |
| `error()` — `exit()` ohne `[[noreturn]]` | `fidlib.c:253` | Compiler kann nicht beweisen dass danach kein UB folgt |
| Flexible Array Members (`double buf[0]`) | `fidrf_combined.h` | In C++ nicht standard (extension) |
| VLA-Nutzung (falls noch vorhanden) | div. | In C++ verboten |
| C-Style Casts | div. | Warnung → Fehler bei `-Werror` |
| `register`-Keyword | `fidrf_cmdlist.h` | Deprecated in C++17, entfernt in C++20 (UB als Storage Class) |
| Implizite `int`-Rückgabetypen | alt. Stellen | Fehler in C++ |

### Maßnahmen Phase 1

```
- Alle void*-Casts explizit machen (static_cast<T*> oder C-Cast im extern "C"-Block)
- error() mit [[noreturn]] annotieren (C23/GCC-Attribut, portable Wrapper)
- double buf[0] → double buf[1] oder std::array (nur wenn in C++ kompiliert)
- register-Keyword entfernen (ohnehin ohne Wirkung seit GCC 7+)
- extern "C" Guards in fidlib.h sind bereits vorhanden (JamesHight-Fork)
```

**Build-Flag für Phase 1:**
```cmake
# In lib/CMakeLists.txt, Option für C++-Compile:
target_compile_options(fidlib PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>)
```

Alternativ: die `.c`-Dateien als `.cpp` mit `set_source_files_properties(... LANGUAGE CXX)`.

---

## Phase 2 — Sicherheits-Hardening (aus C++20-Analyse gewonnen)

### 2.1 `error()` — kein Abort im RT-Pfad

**Problem:** `error()` in `fidlib.c:253` ruft `exit()` auf. Das ist in einem
Realtime-Kontext (Audio-Thread, Embedded) katastrophal.

```c
// Aktuell:
static void error(char *fmt, ...) { ... exit(1); }
```

**Lösung:** Fehlerbehandlung über Callback trennbar machen:

```c
typedef void (*FidErrorFunc)(const char *msg, void *userdata);
void fid_set_error_handler(FidErrorFunc fn, void *userdata);
```

Default-Handler bleibt `exit()` für bestehende Nutzer. RT-Nutzer setzen
einen eigenen Handler (z.B. setzt ein Fehler-Flag und gibt einen
Null-Filter zurück).

### 2.2 `const`-Korrektheit

Der JamesHight-Fork hat `const char *spec` bereits. Darüber hinaus:

- `fid_response()`, `fid_response_pha()`: `filt`-Parameter als `const FidFilter *`
- Alle read-only Koeffizient-Pointer in `RunBuf` als `const double *`

### 2.3 Integer-Overflow / Größen-Checks

`FFCSIZE(n, m)` berechnet Allokationsgrößen. Kein Overflow-Check.
C++20-`std::numeric_limits` / `__builtin_add_overflow` als portabler Wrapper.

---

## Phase 3 — Realtime-Tauglichkeit

### 3.1 Kein `malloc` im Hot Path

**Aktuell:** `fid_run_newbuf()` und `fid_run_initbuf()` rufen `calloc()` auf.
Das ist für Design-Zeit akzeptabel, aber `fid_run_newbuf()` darf **nicht**
im Audio-Thread aufgerufen werden.

**Strategie:** Dokumentation + API-Annotation (kein Code-Umbau nötig):
- `fid_design()`, `fid_run_new()`, `fid_run_newbuf()` → "Alloc-Phase" (nicht RT-safe)
- `funcp(buf, sample)` → "Run-Phase" (RT-safe, zero-alloc)

Langfristig: `fid_run_newbuf_inplace(void *run, void *mem, size_t len)` —
Buffer in vorallokiertem Speicher initialisieren (Arena/Pool-kompatibel).

### 3.2 Cache-Freundliches Layout

`RunBuf` enthält `double *coef` und `double *cmd` als Pointer auf getrennte
Speicherbereiche. Der Hot-Path in `filter_step()` sprintet zwischen drei
Speicherregionen:

```
RunBuf.coef  →  [irgendwo im Heap]
RunBuf.cmd   →  [irgendwo im Heap]
RunBuf.buf   →  [direkt hinter RunBuf — bereits so, nach unserem Fix]
```

**Optimierung:** `coef` und `cmd` zusammen mit `buf` in einem zusammenhängenden
Block allozieren:

```
[ RunBuf-Header | coef[] | cmd[] | buf[] ]
```

Damit liegt der gesamte Filter-State in einer Cache-Line-Gruppe.
`fid_run_newbuf()` kann das bei bekanntem Layout direkt so allozieren.

### 3.3 `filter_step()` — Branch-Reduktion

Die Command-List in `fidrf_cmdlist.h` arbeitet sich mit einem `switch/char`
durch die Koeffizienten. Das ist schnell, aber der Dispatch-Loop hat
Branches.

Für C++20: `constexpr`-Unrolling für bekannte Filterordnungen (Biquad,
4. Ordnung etc.) als Template-Spezialisierungen möglich — ohne den
C-Pfad zu berühren.

### 3.4 Compiler-Hints

```c
// Portabler Wrapper für __builtin_expect:
#if defined(__GNUC__) || defined(__clang__)
#  define FID_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define FID_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define FID_LIKELY(x)   (x)
#  define FID_UNLIKELY(x) (x)
#endif
```

Anwenden in `filter_step()` auf den `END`-Befehl (häufigster Exit).

---

## Phase 4 — Geschwindigkeit

### 4.1 Link-Time Optimization (LTO)

```cmake
set_property(TARGET fidlib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
```

Wirkt besonders bei `filter_step()`, da die Funktion immer über einen
Funktionspointer aufgerufen wird — LTO kann das devirtualisieren.

### 4.2 `-O3 -ffast-math` für die Hot-Path-Dateien

`ffast-math` ist für Audioverarbeitung in der Regel sicher (keine NaN/Inf
im Normalfall). Nur für `fidrf_cmdlist.h`/`fidlib.c` scopen, nicht global.

### 4.3 SIMD (langfristig)

Für Multi-Channel-Verarbeitung (N Kanäle parallel): NEON (ARM) / SSE2 (x86)
via Compiler Auto-Vectorization — erfordert struct-of-arrays statt
array-of-structs für die Buffer. Scope: eigenes Feature-Ticket.

---

## Umsetzungsreihenfolge

```
1. Phase 1: C++20-Kompilierbarkeit (Blocker für alle weiteren Analysen)
2. Phase 2.1: error()-Handler entkoppeln (RT-Grundvoraussetzung)
3. Phase 2.2: const-Korrektheit vervollständigen
4. Phase 3.1: Alloc-Phase / Run-Phase dokumentieren + inplace-API
5. Phase 3.2: Cache-Layout konsolidieren
6. Phase 3.3+3.4: Compiler-Hints + filter_step-Optimierung
7. Phase 4: LTO + ffast-math + SIMD (separat)
```

---

## cmake-Integration (Vorschlag)

```cmake
# lib/CMakeLists.txt — Erweiterung:

option(FIDLIB_CXX20_COMPAT "fidlib mit C++20-Compiler übersetzen" OFF)

if(FIDLIB_CXX20_COMPAT)
  set_source_files_properties(fidlib.c PROPERTIES LANGUAGE CXX)
  target_compile_options(fidlib PRIVATE -std=c++20 -Wno-old-style-cast)
endif()

option(FIDLIB_LTO "Link-Time Optimization für fidlib" OFF)
if(FIDLIB_LTO)
  set_property(TARGET fidlib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
```

---

*Erstellt: 2026-05-27 — Basis: fidlib JamesHight-Fork v0.9.11*
