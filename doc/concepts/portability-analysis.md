# Portability Analysis: Compilers, Standards, Architectures, Operating Systems

Date: 2026-05-27  
Branch: feature/portability-analysis → merged into develop

---

## 1 Components

The project consists of three independently portable components:

| Component | Sources | Dependencies |
|---|---|---|
| **fidlib** | `fidlib/fidlib.c`, `fidmkf.h`, `fidrf_*.h` | libc (heap, stdio), libm |
| **firun** | `firun/firun.c` | fidlib, POSIX (read, SIGPIPE, ssize_t) |
| **fiview** | `fiview/src/*.c` | fidlib, SDL 1.2 or 2.x, X11/Xext, pthread, libm, libdl |

---

## 2 C Standard

### Stated minimum: C17 (`CMAKE_C_STANDARD 17`, `CMAKE_C_EXTENSIONS OFF`)

C99 features used:

| Feature | Location |
|---|---|
| `inline` keyword | `fidlib.c:242`, `display.c:1022` |
| `//` comments | everywhere |
| `vsnprintf` | `fidlib.c:258`, MSVC shim available |
| Declarations after code | isolated in fidlib.c |
| `size_t` arithmetic explicit | everywhere in fidlib.c |
| `hypot()` | `fidmkf.h:164` (C99 standard) |
| Mixed `int`/`size_t` cast | cleaned up via explicit `(size_t)` cast |

C89/C90 is **definitely ruled out**: `inline`, `//` comments, mixed declarations.

**C11** compiles without issues — no C11 feature is needed or used.

**C++20** is prepared as a cmake option (`FIDLIB_CXX20_COMPAT`, `FIRUN_CXX20_COMPAT`, `FIVIEW_CXX20_COMPAT`). fidlib.h has `extern "C"` guards. fiview still requires `-fpermissive` and is not fully C++ clean.

### POSIX Dependency (orthogonal to C standard)

`fidlib.c` defines `_POSIX_C_SOURCE 200809L` internally. firun and fiview set this in cmake.  
POSIX dependency means: POSIX 2008 (also known as POSIX.1-2008 / SUSv4).

---

## 3 Compilers

### GCC ≥ 4.9 (recommended ≥ 8)

Primary target compiler. Fully supported on all target architectures.  
FID_NORETURN → `__attribute__((noreturn))`  
FID_LIKELY/UNLIKELY → `__builtin_expect`  

Flags `-Wall -Wextra -Wconversion -Wshadow -Werror` are GCC/Clang syntax — not MSVC.

### Clang ≥ 3.5

Identical attribute syntax as GCC → fully supported.  
AppleClang (macOS Xcode): theoretical, but fiview cmake blocks (see §5).

### MSVC (Visual Studio 2015+)

Explicitly addressed in `fidlib.c` and `all.h`:
- `XINLINE` → `__inline`
- `vsnprintf` → `_vsnprintf`, `snprintf` → `_snprintf`
- `FID_NORETURN` → `__declspec(noreturn)`
- `NAN` → `nan_global` (0.0/0.0 as constant not supported)
- `isnan` → `_isnan`
- `<unistd.h>` excluded for T_MSVC

**fidlib alone**: compiles with MSVC.  
**firun with MSVC**: **not portable** — `ssize_t`, `read(0, …)`, `SIGPIPE` are POSIX-only.  
**fiview with MSVC**: theoretical (T_MSVC path available), but cmake script not adapted (§5.2).

Also: cmake flags `-Wall -Wextra -Wconversion -Wshadow` are not MSVC flags → cmake script would fail with MSVC.

### MinGW-w64

T_MINGW path available. MinGW-w64 provides `ssize_t` and limited POSIX.  
Practically usable for fidlib and firun (with SIGPIPE limitations).  
fiview: T_MINGW + WIN_TIME path available.

---

## 4 Processor Architectures

The project is **architecture-agnostic**:

- No assembler, no SIMD intrinsics, no `#ifdef __arm__` or similar
- `RF_JIT` (x86-specific JIT compiler) was present but is **no longer supported** and not included (`fidrf_jit.h` not available)
- All calculations on `double` — no endianness-sensitive byte access on multi-byte integers
- `size_t` arithmetic consistently correct

**Theoretically compilable on**: x86_64, i386, aarch64, armv7, RISC-V, MIPS, PowerPC, SPARC — everywhere GCC/Clang supports C99 and a POSIX libc is available.

**Primary target platform of this project**: aarch64 (Raspberry Pi 5, Cortex-A76).

---

## 5 Operating Systems

### 5.1 Linux ✓ (complete)

Primary platform. All three components work.  
SDL 1.2 and SDL 2.x are built via ExternalProject.  
X11/Xext as SDL backend on the desktop, Wayland support possible via SDL2 (but not configured).

### 5.2 macOS — conditionally compilable ✓ (cmake blocker fixed)

fidlib: **compilable**.  
firun: **compilable** (`ssize_t` via POSIX, `#ifdef SIGPIPE` guard now present).  
fiview: cmake blocker fixed:
1. `T_APPLE` → `UNIX_TIME` entered in `all.h`; cmake sets `T_APPLE` automatically with `if(APPLE)`.
2. `SDL_EXTRA_LIBS` now platform-aware: macOS gets only `m pthread`, no `X11 Xext`.
3. SDL 1.2 `config.sub` path: `find_file()` searches Homebrew paths, falls back to `/usr/share/misc`.

Untested due to lack of macOS machine, but all cmake blockers are structurally resolved.

### 5.3 Windows (MinGW-w64 / MSYS2) — partial ✓ (cmake blocker fixed)

fidlib: **compilable**.  
firun: **conditionally compilable** — `SIGPIPE` now guarded with `#ifdef SIGPIPE`; `ssize_t` via `_MSC_VER` shim (`typedef long long ssize_t`, `read` → `_read`); MinGW-w64 compiles through.  
fiview: cmake sets `T_MINGW` automatically with `WIN32 && !MSVC`; `SDL_EXTRA_LIBS` without `X11 Xext` on Windows.

### 5.4 Windows (MSVC / native) — partial ✓ (cmake blocker fixed)

fidlib: **compilable**.  
firun: **conditionally compilable** — `ssize_t` shim and `#include <io.h>` + `read` → `_read` under `_MSC_VER`.  
fiview: cmake sets `T_MSVC` with MSVC; compiler flags switched to `/W3 /WX /wd4996`.  
Untested due to lack of MSVC environment.

### 5.5 Embedded / Bare-Metal — no

fidlib uses `malloc`/`realloc` and `fprintf(stderr, …)` → requires libc with heap and stdio. No port without `Alloc()` replacement and error handler redirection via `fid_set_error_handler()`.

---

## 6 Summary

| Scenario | fidlib | firun | fiview |
|---|---|---|---|
| Linux / GCC ≥ 8 / x86_64 | ✓ | ✓ | ✓ |
| Linux / GCC ≥ 8 / aarch64 | ✓ | ✓ | ✓ |
| Linux / Clang ≥ 8 | ✓ | ✓ | ✓ |
| macOS / AppleClang | ✓ | ✓ | ✗ (cmake X11) |
| Windows / MinGW-w64 | ✓ | ⚠ (SIGPIPE) | ⚠ (cmake) |
| Windows / MSVC 2019+ | ✓ | ⚠ (ssize_t shim available, untested) | ⚠ (cmake prepared, untested) |
| WSL2 (Linux kernel) | ✓ | ✓ | ⚠ (no X11 without XServer) |
| Embedded / Bare-Metal | ✗ | ✗ | ✗ |

---

## 7 Blocking Issues

| # | Problem | Status |
|---|---|---|
| P1 | cmake `SDL_EXTRA_LIBS` hardcoded `X11 Xext` | ✓ fixed — platform-aware |
| P2 | No `T_APPLE` macro | ✓ fixed — all.h + cmake |
| P3 | cmake flags not MSVC compatible | ✓ fixed — `if(MSVC)` guard |
| P4 | `SIGPIPE`/`ssize_t` POSIX-only in firun | ✓ fixed — `#ifdef SIGPIPE`, `_MSC_VER` shim |
| P5 | SDL 1.2 `config.sub` Linux path hardcoded | ✓ fixed — `find_file()` Homebrew-aware |
| P6 | fidlib heap dependency for embedded | open — separate feature ticket |
