# Concept: fidlib — C++20 Compilability, Realtime Safety, Security

## Goal

fidlib is written in C99. The goal is **not** to port it to C++20, but to:

1. Make the existing C code compilable with a C++20 compiler (`-std=c++20`)
   — without restructuring the logic.
2. Use insights gained from C++20 analysis to selectively improve
   security, performance, and realtime safety.

---

## Phase 1 — Establishing C++20 Compilability

The C++20 compiler is stricter than C99 and enforces correctness that was
silently tolerated by the C compiler. The goal of this phase is a **clean
build without `-fpermissive`**.

### Known Obstacles in fidlib.c

| Problem | Location | C++20 Error |
|---|---|---|
| Implicit `void *` casts | `Alloc()`, `fid_run_newbuf()` | `error: invalid conversion from 'void*'` |
| `error()` — `exit()` without `[[noreturn]]` | `fidlib.c:253` | Compiler cannot prove no UB follows |
| Flexible array members (`double buf[0]`) | `fidrf_combined.h` | Not standard in C++ (extension) |
| VLA usage (if still present) | various | Forbidden in C++ |
| C-style casts | various | Warning → error with `-Werror` |
| `register` keyword | `fidrf_cmdlist.h` | Deprecated in C++17, removed in C++20 (UB as storage class) |
| Implicit `int` return types | older locations | Error in C++ |

### Measures for Phase 1

```
- Make all void* casts explicit (static_cast<T*> or C-cast in extern "C" block)
- Annotate error() with [[noreturn]] (C23/GCC attribute, portable wrapper)
- double buf[0] → double buf[1] or std::array (only when compiling as C++)
- Remove register keyword (has had no effect since GCC 7+)
- extern "C" guards in fidlib.h are already present (JamesHight fork)
```

**Build flag for Phase 1:**
```cmake
# In lib/CMakeLists.txt, option for C++ compilation:
target_compile_options(fidlib PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>)
```

Alternatively: the `.c` files as `.cpp` with `set_source_files_properties(... LANGUAGE CXX)`.

---

## Phase 2 — Security Hardening (derived from C++20 analysis)

### 2.1 `error()` — no abort in the RT path

**Problem:** `error()` in `fidlib.c:253` calls `exit()`. This is catastrophic
in a realtime context (audio thread, embedded).

```c
// Current:
static void error(char *fmt, ...) { ... exit(1); }
```

**Solution:** Make error handling separable via callback:

```c
typedef void (*FidErrorFunc)(const char *msg, void *userdata);
void fid_set_error_handler(FidErrorFunc fn, void *userdata);
```

Default handler remains `exit()` for existing users. RT users set
their own handler (e.g., sets an error flag and returns a null filter).

### 2.2 `const` Correctness

The JamesHight fork already has `const char *spec`. Beyond that:

- `fid_response()`, `fid_response_pha()`: `filt` parameter as `const FidFilter *`
- All read-only coefficient pointers in `RunBuf` as `const double *`

### 2.3 Integer Overflow / Size Checks

`FFCSIZE(n, m)` calculates allocation sizes. No overflow check.
C++20 `std::numeric_limits` / `__builtin_add_overflow` as portable wrapper.

---

## Phase 3 — Realtime Safety

### 3.1 No `malloc` in the Hot Path

**Currently:** `fid_run_newbuf()` and `fid_run_initbuf()` call `calloc()`.
This is acceptable for design time, but `fid_run_newbuf()` must **not**
be called in the audio thread.

**Strategy:** Documentation + API annotation (no code restructuring needed):
- `fid_design()`, `fid_run_new()`, `fid_run_newbuf()` → "alloc phase" (not RT-safe)
- `funcp(buf, sample)` → "run phase" (RT-safe, zero-alloc)

Long-term: `fid_run_newbuf_inplace(void *run, void *mem, size_t len)` —
initialize buffer in pre-allocated memory (arena/pool compatible).

### 3.2 Cache-Friendly Layout

`RunBuf` contains `double *coef` and `double *cmd` as pointers to separate
memory regions. The hot path in `filter_step()` jumps between three
memory regions:

```
RunBuf.coef  →  [somewhere in heap]
RunBuf.cmd   →  [somewhere in heap]
RunBuf.buf   →  [directly after RunBuf — already the case after our fix]
```

**Optimization:** Allocate `coef` and `cmd` together with `buf` in one
contiguous block:

```
[ RunBuf header | coef[] | cmd[] | buf[] ]
```

This places the entire filter state in one cache line group.
`fid_run_newbuf()` can allocate this directly with the known layout.

### 3.3 `filter_step()` — Branch Reduction

The command list in `fidrf_cmdlist.h` works through the coefficients with a
`switch/char` dispatch. This is fast, but the dispatch loop has branches.

For C++20: `constexpr` unrolling for known filter orders (biquad,
4th order etc.) as template specializations — without touching the C path.

### 3.4 Compiler Hints

```c
// Portable wrapper for __builtin_expect:
#if defined(__GNUC__) || defined(__clang__)
#  define FID_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define FID_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define FID_LIKELY(x)   (x)
#  define FID_UNLIKELY(x) (x)
#endif
```

Apply in `filter_step()` to the `END` command (most frequent exit).

---

## Phase 4 — Performance

### 4.1 Link-Time Optimization (LTO)

```cmake
set_property(TARGET fidlib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
```

Especially effective for `filter_step()`, since the function is always
called through a function pointer — LTO can devirtualize this.

### 4.2 `-O3 -ffast-math` for the Hot-Path Files

`ffast-math` is generally safe for audio processing (no NaN/Inf
in normal operation). Scope only to `fidrf_cmdlist.h`/`fidlib.c`, not globally.

### 4.3 SIMD (long-term)

For multi-channel processing (N channels in parallel): NEON (ARM) / SSE2 (x86)
via compiler auto-vectorization — requires struct-of-arrays instead of
array-of-structs for the buffers. Scope: separate feature ticket.

---

## Implementation Order

```
1. Phase 1: C++20 compilability (blocker for all further analyses)
2. Phase 2.1: Decouple error() handler (RT prerequisite)
3. Phase 2.2: Complete const correctness
4. Phase 3.1: Document alloc phase / run phase + inplace API
5. Phase 3.2: Consolidate cache layout
6. Phase 3.3+3.4: Compiler hints + filter_step optimization
7. Phase 4: LTO + ffast-math + SIMD (separate)
```

---

## cmake Integration (Proposal)

```cmake
# lib/CMakeLists.txt — extension:

option(FIDLIB_CXX20_COMPAT "Compile fidlib with C++20 compiler" OFF)

if(FIDLIB_CXX20_COMPAT)
  set_source_files_properties(fidlib.c PROPERTIES LANGUAGE CXX)
  target_compile_options(fidlib PRIVATE -std=c++20 -Wno-old-style-cast)
endif()

option(FIDLIB_LTO "Link-Time Optimization for fidlib" OFF)
if(FIDLIB_LTO)
  set_property(TARGET fidlib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
```

---

*Created: 2026-05-27 — Base: fidlib JamesHight fork v0.9.11*
