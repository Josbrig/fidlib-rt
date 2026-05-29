# Concept: firun — C++20, RT, Robustness

## Goal

firun is the CLI frontend for fidlib. Currently: a direct port from
`vendor/fidlib/firun.c`. The goal is not to restructure the algorithm, but to:

1. Make it compilable with a C++20 compiler (analogous to fidlib)
2. Robustness: error handling without `exit()` deep in the call stack
3. Small structural improvements for readability and maintainability

---

## Analysis of firun.c

firun.c is ~600 lines of C. It:
- Parses CLI arguments (`argc/argv`)
- Calls `fid_design()` and `fid_run_new()`
- Reads samples from stdin, filters them, writes to stdout
- Uses `fprintf(stderr, ...)` + `exit(1)` for errors

### Known C++20 Obstacles

| Problem | Line (approx.) | Error in C++20 |
|---|---|---|
| String literal → `char *` | various | `-Werror=write-strings` |
| Implicit `void*` casts | various | invalid conversion |
| `exit()` directly after error message | various | no stack unwind, no cleanup |

### Measures

```
- char * → const char * for all string literals and fmt parameters
- Error output: introduce central err_exit() wrapper that is
  annotated [[noreturn]] and wraps stderr + exit(1)
- Make all void* casts explicit
- cmake: target_compile_options(firun ... -Wno-old-style-cast) for
  FIDLIB_CXX20_COMPAT mode (CLI code is less strict than library)
```

---

## Phase 1 — C++20 Compilability

- `char *` string literals → `const char *`
- Introduce `err_exit(const char *fmt, ...)` with `FID_NORETURN`
- Make `void*` casts explicit
- cmake option: firun builds with `-std=c++20` when `FIDLIB_CXX20_COMPAT=ON`

## Phase 2 — Robustness

- Register `fid_set_error_handler()`: on error in fidlib, handler sets
  a global flag, firun outputs a clean error message
- Buffer handling: replace fixed stack buffers with size-checked variants
- Signal handling for SIGPIPE (stdout closed)

## Phase 3 — Extensions (optional, separate tickets)

- `--format float32|int16|double` for flexible sample formats
- `--channels N` for multi-channel (uses multiple `fid_run_newbuf` instances)
- Streaming mode without latency overhead (direct read/write without stdio buffer)

---

## cmake Integration

```cmake
# cli/CMakeLists.txt — addition:
if(FIDLIB_CXX20_COMPAT)
  set_source_files_properties(firun.c PROPERTIES LANGUAGE CXX)
  target_compile_options(firun PRIVATE -std=c++20 -Wno-old-style-cast)
endif()
```

---

*Created: 2026-05-27*
