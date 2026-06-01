# Portabilitätsanalyse: Compiler, Standards, Architekturen, Betriebssysteme

Stand: 2026-05-27  
Branch: feature/portability-analysis → gemergt nach develop

---

## 1 Komponenten

Das Projekt besteht aus drei unabhängig portierbaren Komponenten:

| Komponente | Quellen | Abhängigkeiten |
|---|---|---|
| **fidlib** | `fidlib/fidlib.c`, `fidmkf.h`, `fidrf_*.h` | libc (Heap, stdio), libm |
| **firun** | `firun/firun.c` | fidlib, POSIX (read, SIGPIPE, ssize_t) |
| **fiview** | `fiview/src/*.c` | fidlib, SDL 1.2 oder 2.x, X11/Xext, pthread, libm, libdl |

---

## 2 C-Standard

### Stated minimum: C17 (`CMAKE_C_STANDARD 17`, `CMAKE_C_EXTENSIONS OFF`)

Verwendete C99-Features:

| Feature | Fundstelle |
|---|---|
| `inline`-Schlüsselwort | `fidlib.c:242`, `display.c:1022` |
| `//`-Kommentare | überall |
| `vsnprintf` | `fidlib.c:258`, MSVC-Shim vorhanden |
| Deklarationen nach Code | vereinzelt in fidlib.c |
| `size_t`-Arithmetik explizit | überall in fidlib.c |
| `hypot()` | `fidmkf.h:164` (C99 standard) |
| Mixed `int`/`size_t` cast | via explizitem `(size_t)` Cast bereinigt |

C89/C90 scheidet **definitiv aus**: `inline`, `//`-Kommentare, gemischte Deklarationen.

**C11** compiliert problemlos — kein C11-Feature wird benötigt oder verwendet.

**C++20** ist als cmake-Option vorbereitet (`FIDLIB_CXX20_COMPAT`, `FIRUN_CXX20_COMPAT`, `FIVIEW_CXX20_COMPAT`). fidlib.h hat `extern "C"`-Guards. fiview benötigt noch `-fpermissive` und ist nicht vollständig C++-sauber.

### POSIX-Abhängigkeit (orthogonal zum C-Standard)

`fidlib.c` definiert intern `_POSIX_C_SOURCE 200809L`. firun und fiview setzen dies im cmake.  
POSIX-Abhängigkeit bedeutet: POSIX 2008 (auch bekannt als POSIX.1-2008 / SUSv4).

---

## 3 Compiler

### GCC ≥ 4.9 (empfohlen ≥ 8)

Primärer Zielcompiler. Vollständig unterstützt auf allen Zielarchitekturen.  
FID_NORETURN → `__attribute__((noreturn))`  
FID_LIKELY/UNLIKELY → `__builtin_expect`  

Flags `-Wall -Wextra -Wconversion -Wshadow -Werror` sind GCC/Clang-Syntax — kein MSVC.

### Clang ≥ 3.5

Identische Attribute-Syntax wie GCC → vollständig unterstützt.  
AppleClang (macOS Xcode): theoretisch, aber fiview-cmake blockiert (siehe §5).

### MSVC (Visual Studio 2015+)

Explizit adressiert in `fidlib.c` und `all.h`:
- `XINLINE` → `__inline`
- `vsnprintf` → `_vsnprintf`, `snprintf` → `_snprintf`
- `FID_NORETURN` → `__declspec(noreturn)`
- `NAN` → `nan_global` (0.0/0.0 als Konstante nicht unterstützt)
- `isnan` → `_isnan`
- `<unistd.h>` ausgeschlossen für T_MSVC

**fidlib allein**: compiliert mit MSVC.  
**firun mit MSVC**: **nicht portierbar** — `ssize_t`, `read(0, …)`, `SIGPIPE` sind POSIX-only.  
**fiview mit MSVC**: theoretisch (T_MSVC-Pfad vorhanden), aber cmake-Skript nicht angepasst (§5.2).

Außerdem: cmake-Flags `-Wall -Wextra -Wconversion -Wshadow` sind keine MSVC-Flags → cmake-Skript würde bei MSVC fehlschlagen.

### MinGW-w64

T_MINGW-Pfad vorhanden. MinGW-w64 stellt `ssize_t` und eingeschränktes POSIX bereit.  
Praxistauglich für fidlib und firun (mit Einschränkungen bei SIGPIPE).  
fiview: T_MINGW + WIN_TIME-Pfad vorhanden.

---

## 4 Prozessorarchitekturen

Das Projekt ist **architektur-agnostisch**:

- Kein Assembler, keine SIMD-Intrinsics, kein `#ifdef __arm__` oder ähnliches
- `RF_JIT` (x86-spezifischer JIT-Kompiler) war vorhanden, ist aber **nicht mehr unterstützt** und wird nicht eingebunden (`fidrf_jit.h` nicht vorhanden)
- Alle Berechnungen auf `double` — kein Endianness-sensitiver Bytezugriff auf Multi-Byte-Integers
- `size_t`-Arithmetik durchgehend korrekt

**Theoretisch compilierbar auf**: x86_64, i386, aarch64, armv7, RISC-V, MIPS, PowerPC, SPARC — überall wo GCC/Clang C99 unterstützt und eine POSIX-libc vorhanden ist.

**Primäre Zielplattform dieses Projekts**: aarch64 (Raspberry Pi 5, Cortex-A76).

---

## 5 Betriebssysteme

### 5.1 Linux ✓ (vollständig)

Primärplattform. Alle drei Komponenten funktionieren.  
SDL 1.2 und SDL 2.x werden per ExternalProject gebaut.  
X11/Xext als SDL-Backend auf dem Desktop, Wayland-Support durch SDL2 möglich (aber nicht konfiguriert).

### 5.2 macOS — bedingt compilierbar ✓ (cmake-Blocker behoben)

fidlib: **compilierbar**.  
firun: **compilierbar** (`ssize_t` via POSIX, `#ifdef SIGPIPE`-Guard jetzt vorhanden).  
fiview: cmake-Blocker behoben:
1. `T_APPLE` → `UNIX_TIME` in `all.h` eingetragen; cmake setzt `T_APPLE` automatisch bei `if(APPLE)`.
2. `SDL_EXTRA_LIBS` jetzt plattformbewusst: macOS bekommt nur `m pthread`, kein `X11 Xext`.
3. SDL 1.2 `config.sub`-Pfad: `find_file()` sucht Homebrew-Pfade, fällt auf `/usr/share/misc` zurück.

Ungetestet mangels macOS-Maschine, aber alle cmake-Blocker sind strukturell beseitigt.

### 5.3 Windows (MinGW-w64 / MSYS2) — partiell ✓ (cmake-Blocker behoben)

fidlib: **compilierbar**.  
firun: **bedingt compilierbar** — `SIGPIPE` jetzt mit `#ifdef SIGPIPE` bewacht; `ssize_t` via `_MSC_VER`-Shim (`typedef long long ssize_t`, `read` → `_read`); MinGW-w64 kompiliert durch.  
fiview: cmake setzt `T_MINGW` automatisch bei `WIN32 && !MSVC`; `SDL_EXTRA_LIBS` ohne `X11 Xext` unter Windows.

### 5.4 Windows (MSVC / native) — partiell ✓ (cmake-Blocker behoben)

fidlib: **compilierbar**.  
firun: **bedingt compilierbar** — `ssize_t`-Shim und `#include <io.h>` + `read` → `_read` unter `_MSC_VER`.  
fiview: cmake setzt `T_MSVC` bei MSVC; Compiler-Flags auf `/W3 /WX /wd4996` umgestellt.  
Ungetestet mangels MSVC-Umgebung.

### 5.5 Embedded / Bare-Metal — nein

fidlib nutzt `malloc`/`realloc` und `fprintf(stderr, …)` → benötigt libc mit Heap und stdio. Kein Port ohne `Alloc()`-Ersatz und Error-Handler-Umleitung via `fid_set_error_handler()`.

---

## 6 Zusammenfassung

| Szenario | fidlib | firun | fiview |
|---|---|---|---|
| Linux / GCC ≥ 8 / x86_64 | ✓ | ✓ | ✓ |
| Linux / GCC ≥ 8 / aarch64 | ✓ | ✓ | ✓ |
| Linux / Clang ≥ 8 | ✓ | ✓ | ✓ |
| macOS / AppleClang | ✓ | ✓ | ✗ (cmake X11) |
| Windows / MinGW-w64 | ✓ | ⚠ (SIGPIPE) | ⚠ (cmake) |
| Windows / MSVC 2019+ | ✓ | ⚠ (ssize_t-Shim vorhanden, ungetestet) | ⚠ (cmake vorbereitet, ungetestet) |
| WSL2 (Linux-Kernel) | ✓ | ✓ | ⚠ (kein X11 ohne XServer) |
| Embedded / Bare-Metal | ✗ | ✗ | ✗ |

---

## 7 Blockierende Probleme

| # | Problem | Status |
|---|---|---|
| P1 | cmake `SDL_EXTRA_LIBS` hartkodiert `X11 Xext` | ✓ behoben — plattformbewusst |
| P2 | Kein `T_APPLE` Macro | ✓ behoben — all.h + cmake |
| P3 | cmake flags nicht MSVC-kompatibel | ✓ behoben — `if(MSVC)` Guard |
| P4 | `SIGPIPE`/`ssize_t` POSIX-only in firun | ✓ behoben — `#ifdef SIGPIPE`, `_MSC_VER`-Shim |
| P5 | SDL 1.2 `config.sub` Linux-Pfad hartkodiert | ✓ behoben — `find_file()` Homebrew-aware |
| P6 | fidlib heap-Abhängigkeit für Embedded | offen — eigenes Feature-Ticket |
