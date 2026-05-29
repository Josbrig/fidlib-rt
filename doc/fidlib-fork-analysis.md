# fidlib Fork Analysis — Which Base Do We Use?

Date: 2026-05-26  
Context: Selecting the upstream base for the modernization project

---

## Candidates at a Glance

| Attribute | [uazu/fidlib] | [JamesHight/fidlib] |
|---|---|---|
| Author / origin | Jim Peters (original author) | JamesHight as consolidator of multiple patches |
| Commits | **2** | **18** |
| Last activity | Aug 26, 2014 | Aug 29, 2014 |
| Build system | Shell scripts (`mk-firun`, `mk-firun-mingw`) | **Autotools** (`configure.ac`, `Makefile.am`, `bootstrap.sh`) |
| Version | 0.9.10 | **0.9.11** |
| C++ compatibility | no | **yes** (`extern "C"` guards) |
| `const` correctness | no (`char *spec`) | **yes** (`const char *spec`) |
| Compiler warnings | unclean | **cleaned up** |
| pkg-config | no | **yes** (`fidlib.pc.in`) |
| Stars (GitHub) | 5 | **11** |
| Own forks | 6 | 2 |
| Status per README | **unmaintained** | unmaintained (implicit) |

[uazu/fidlib]: https://github.com/uazu/fidlib
[JamesHight/fidlib]: https://github.com/JamesHight/fidlib

---

## What exactly is JamesHight/fidlib?

It is **not a classic fork** of `uazu/fidlib` via the GitHub fork mechanism — the repo graph
shows no direct lineage. JamesHight imported a copy of the original code in 2013 and then
systematically merged patches from the community:

### Included Patches (commit reconstruction)

```
2013-07  JamesHight    Lib import (original copy from uazu.net)
2013-07  JamesHight    README corrections

2014-08  daschuer      "Johns initial Mixxx commit" — Mixxx integration
2014-08  daschuer      API: char *spec → const char *spec
2014-08  daschuer      Replace #ifdef MIXXX with #ifdef __cplusplus
2014-08  daschuer      Clean up discards-'const'-qualifier warnings
2014-08  daschuer/     Windows/MinGW cross-compile fixes
         ulatekh
2014-08  daschuer      Fixed -Wsign-compare warnings
2014-08  daschuer      Fixed unused-parameter warnings
2014-08  daschuer      Removed zero-size array
2014-08  daschuer      Debug mode with extra warnings (mk_firun -d)

2014-08  kwhat         Autotools build system (configure.ac, Makefile.am)
2014-08  kwhat         Bugfixes for packaging
2014-08  kwhat         Fixed uninitialized-before-use warnings
```

**Core statement:** JamesHight/fidlib is the unofficial "community edition" of fidlib — it
consolidates all practically tested patches that never flowed back into the original.

---

## The Other Forks

All other forks (`daschuer/fidlib`, `gdkar/fidlib`, `EEGKit/fidlib`, `wjcroft/fidlib-1`) have
exactly **2 commits** and are identical to the uazu original. None contain their own further
development. `EEGKit/fidlib` even contains an explicit note:

> *CURRENTLY UNMAINTAINED — Consider daschuer/mixxxdj fork if this doesn't work for you.*

The `daschuer/mixxxdj` branch recommended in uazu/fidlib is exactly the set of patches that
is already consolidated via JamesHight/fidlib.

---

## API Difference: The Decisive Break

The most important substantive difference between the two candidates is const correctness:

```c
// uazu/fidlib — original
FidFilter *fid_design(char *spec, ...);
void fid_rewrite_spec(char *spec, ...);

// JamesHight/fidlib — patched version
FidFilter *fid_design(const char *spec, ...);
void fid_rewrite_spec(const char *spec, ...);
```

Anyone passing filter specifications from string literals (the most common use case)
gets a compiler error in C++ and a warning in C with the original. The JamesHight version
is API-compatible but const-correct — no breaking change for existing users.

---

## Recommendation

**We build on [JamesHight/fidlib].**

Rationale:

1. **Consolidates the best available patches** from multiple independent sources
   (Mixxx team, kwhat, ulatekh) — everything that was missing from the original is already collected.

2. **Autotools base** makes the step to CMake or Meson trivial — there is already a
   clean `configure.ac` with versioning, `pkg-config` support and options.

3. **const correctness** is a prerequisite for clean C++17/C99 and would otherwise
   need to be added first.

4. **C++ guards** are present — the library can already be embedded in C++ projects today
   without name mangling problems.

5. Version **0.9.11** vs 0.9.10 shows that the patches were regarded as a patch release,
   not a fork.

The uazu/fidlib repo serves as a **reference for the mathematical core** — in particular
`fidmkf.h` (mkfilter algorithms) — but not as the working base.

---

## Recommended Next Steps

1. Take `JamesHight/fidlib` as a Git subtree or direct import into `vendor/fidlib/`.
2. Migrate the build system from Autotools to **CMake** (or Meson) — `configure.ac` as
   template for all dependencies and feature flags.
3. Clean up `fidlib.h` / `fidlib.c` for **C99** (VLAs, `//` comments already OK,
   wrap `__attribute__` portably).
4. Write first smoke test: Butterworth LP 4th order, impulse response against reference values
   from fiview.
