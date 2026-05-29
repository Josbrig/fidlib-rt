# CLAUDE.md — digitalfilterdesign

This document is the binding operating manual for all Claude Code instances
working in this repository. Read before every action.

---

## Project overview

**Goal:** Modernised C library + CLI toolkit based on fidlib/fiview (Jim Peters,
uazu.net). Accepts filter specifications at runtime, designs and executes IIR/FIR
filters without recompile. Details: `README.md`.

---

## A — Working rules (MANDATORY)

| Rule | Why |
|---|---|
| Only do what the task scope requires | User has planned further steps themselves |
| Analysis → deliver only analysis, then STOP | No autonomous transition to next phase |
| Concept → deliver only concept, then STOP | |
| TODO/Plan → deliver only plan, then STOP | |
| Implementation starts ONLY on explicit signal ("do it", "start", "go") | |
| Short responses, no trailing summary, no emoji | |
| On conflict/ambiguity: stop + ask precisely, never deviate silently | |
| Save reports/docs as persistent files, not only as chat text | |
| Always write TODO lists in parallel as `.md` file into the repo (in addition to session UI) | Lost at session end otherwise |

---

## B — Commit timing (MOST CRITICAL RULE)

**NEVER commit or push autonomously.**

The user decides when to commit and push — not the agent.
Commit only when the user explicitly says: "commit", "commit this", "make a commit".
Push only when the user explicitly says: "push".

A "push" instruction is valid **exactly once** for the explicitly named batch —
not as a standing licence for subsequent commits in the same session.

"Task completed" is NOT a reason to commit.
"File created" is NOT a reason to commit.
There are no exceptions.

---

## C — Git rules

### Branch hierarchy

```
main          ← released
  ↑ --no-ff
develop       ← stability anchor
  ↑ --no-ff
feature/<X>   ← active development
```

- `git merge --no-ff` ALWAYS — never `--ff`, never `--ff-only`
- No skipping: `feature/*` → `develop` → `main`
- Sub-repos merge first, umbrella last

### Commit author

```bash
GIT_AUTHOR_NAME="Kai Dieki" GIT_AUTHOR_EMAIL="kai.dieki@users.noreply.github.com"
```

Co-author always:
```
Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

No "AI NOTICE CONFIRMED" in commit messages.

### Commit format

```
type(scope): short description
```

Types: `feat`, `fix`, `chore`, `docs`, `refactor`, `test`

### Submodule pointer invariants (once included in umbrella)

| Branch | Points to |
|--------|-----------|
| `main` | `main` tip |
| `develop` | `develop` tip |
| `feature/<X>` | `feature/<X>` tip |

### FetchContent GIT_TAG

Full SHA1 commit hash — **never** a branch name, **never** `HEAD`.

### Safety

- No `--no-verify`
- No force-push to `main` or `develop`
- Never work in `_deps/` or `vendor/` except on an explicit vendor-update task

---

## D — cmake conventions

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

- Generator always `-G "Unix Makefiles"` — no Ninja (breaks with cache conflicts)
- Always specify `-DCMAKE_BUILD_TYPE=Debug`
- `CMAKE_CURRENT_SOURCE_DIR` instead of `CMAKE_SOURCE_DIR` in sub-projects
- Build FetchContent-compatible (so umbrella can include via FetchContent)
- No Autotools, no Meson — cmake is the standard in this project

---

## E — C coding rules (fidlib modernisation)

fidlib is C code (not C++). Modernisation goal: **C99**, clean under all warnings.

```
-Wall -Wextra -Wconversion -Wshadow -Werror
-fsanitize=address,undefined   (for tests)
```

- `const char *spec` everywhere (JamesHight patch already applied)
- No VLAs in the public API
- `extern "C"` guards in all public headers (C++ compatibility)
- No undefined behaviour (UB sanitiser in debug build)
- No `__attribute__` directives without a portable wrapper

When writing a C++ wrapper layer:

```
-fno-exceptions
-fno-rtti
-std=c++20
-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
```

---

## F — Project context and analysis results

### Upstream base: decided

**JamesHight/fidlib** (v0.9.11) — not `uazu/fidlib` (v0.9.10).

Rationale (full analysis in `doc/fidlib-fork-analysis.md`):
- Consolidates all community patches (Mixxx team, const-correctness, C++ guards)
- Autotools present → CMake migration trivial
- `const char *spec` instead of `char *spec` → C++-compatible without hacks
- `extern "C"` guards already present

The submodule is registered in `.gitmodules` but not yet initialised:
```
vendor/fidlib   → https://github.com/JamesHight/fidlib.git
vendor/mkfilter → https://github.com/billthefarmer/mkfilter.git
```

### What already exists

```
vendor/fiview/      ← fiview 0.9.10 source complete (incl. embedded fidlib)
vendor/gmeteor/     ← gmeteor-0.95.tar.gz
doc/reference/      ← Audio EQ Cookbook (HTML), fidlib.txt, firun.txt (archived offline)
doc/examples/       ← fiview_log.txt (Butterworth LP example output)
doc/fidlib-fork-analysis.md   ← upstream decision documented
doc/sources-to-secure.md      ← inventory of sources at risk
```

### Recommended next steps (from the analysis)

1. `git submodule update --init` — initialise submodules
2. Build system: create cmake for fidlib
3. Clean fidlib.c/h to C99 (sanitiser-clean)
4. First smoke test: Butterworth LP 4th order, impulse response against fiview reference

---

## G — Merge safety (MANDATORY)

Git `ort` can **silently delete** content without conflict markers when both branches
have changed the same file in nearby but not identical lines.

### After EVERY `git merge` (without exception):

```bash
git diff HEAD~1..HEAD          # What did the merge commit change?
git diff ORIG_HEAD..HEAD       # Same, more robust after merge
```

If a file has shrunk → **investigate immediately, do not push.**

### For non-trivial merges: use `--no-commit`

```bash
git merge --no-ff --no-commit feature/<X>
# → inspect manually
git diff HEAD                  # staged changes of the merge commit
git merge --continue
```

### Protective mechanisms in the repo

- `.gitattributes`: `TODO.md` and `CHANGELOG.md` have `merge=union` →
  lines can never silently disappear (duplicates instead of loss)
- `scripts/hooks/post-merge`: warns automatically on line loss > 4
- Set up hook: `bash scripts/install-hooks.sh` (once after clone)

---

## H — Linux admin (Raspberry Pi 5)

- Only `aptitude`, never `apt`
- Always `--simulate` before a real aptitude run
- Never stop services without confirmation

---

## Quick reference

| What | Where |
|---|---|
| Upstream fidlib (JamesHight) | `fidlib/` |
| fidlib reference docs | `doc/reference/fidlib.txt` |
| firun reference docs | `doc/reference/firun.txt` |
| Audio EQ Cookbook | `doc/reference/audio-eq-cookbook.html` |
| Fork analysis | `doc/fidlib-fork-analysis.md` |
