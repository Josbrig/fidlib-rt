# CLAUDE.md — digitalfilterdesign

Developer quick-reference for AI-assisted work in this repository.

---

## Project overview

Modernised C library + CLI toolkit based on fidlib/fiview (Jim Peters, uazu.net).
Accepts filter specifications at runtime, designs and runs IIR/FIR filters without
recompilation. See `README.md` for full documentation.

---

## Build

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Generator is always `-G "Unix Makefiles"`. Debug build always explicit.

---

## Code standards

| Component | Standard |
|---|---|
| fidlib core | C99, `-Wall -Wextra -Wconversion -Wshadow -Werror` |
| fidgen, fiview2 | C++20, same warnings + `-Wpedantic` |
| Public headers | `extern "C"` guards, no VLAs |

Sanitizers in debug: `-fsanitize=address,undefined`

---

## Branch hierarchy

```
main       ← released
  ↑ --no-ff
develop    ← stable integration
  ↑ --no-ff
feature/*  ← active development
```

All merges use `--no-ff`. Never skip levels.

---

## Upstream basis

**fidlib:** JamesHight/fidlib (v0.9.11) — consolidates all community patches,
`const char *spec`, `extern "C"` guards. See `doc/fidlib-fork-analysis.md`.

**Submodules:**
```
vendor/fidlib   → https://github.com/JamesHight/fidlib.git
vendor/mkfilter → https://github.com/billthefarmer/mkfilter.git
```

---

## Key reference files

| What | Where |
|---|---|
| fidlib API reference | `doc/reference/fidlib.txt` |
| firun reference | `doc/reference/firun.txt` |
| Audio EQ Cookbook | `doc/reference/audio-eq-cookbook.html` |
| Fork analysis | `doc/fidlib-fork-analysis.md` |
