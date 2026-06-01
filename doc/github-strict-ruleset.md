# GitHub / Git Strict Ruleset — fidlib-rt

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- BINDING: This document is law for all contributors, human and AI alike. -->

This ruleset is the binding standard for all version-control and publication
activity in this repository. It synthesises the strictest applicable requirements
from IEC 62304 (medical device software), DO-178C (airborne systems), ISO 26262
(automotive functional safety), MISRA C, the OWASP Software Component Verification
Standard, REUSE 3.0 / SPDX, and the project-owner's own rules where these are
stricter than any industry standard.

**Hierarchy of authority:**
1. Project-owner instruction (current session)
2. This document
3. Industry standards cited herein
4. Common sense

Where this document and an industry standard conflict, the stricter rule applies.

---

## 1 — Legal and Licence Compliance

### 1.1 SPDX identifier on every file

Every source file committed to this repository **must** carry an SPDX licence
identifier as the first or second comment line:

```c
// SPDX-License-Identifier: GPL-2.0-or-later
```

No file without a valid SPDX identifier may be merged into `develop` or `main`.
Files lacking an identifier are considered legally ambiguous and constitute a
compliance defect blocking merge.

### 1.2 Copyright notices

Original copyright notices must be preserved verbatim. Modifying, removing, or
obscuring a copyright notice — even by reformatting — is prohibited.

When adding new files:

```c
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) <year> <Legal Name> <<email>>
```

### 1.3 Licence compatibility matrix

Before any new dependency (library, header, snippet) is introduced, its licence
must be verified against the compatibility matrix:

| Existing licence | Compatible with | Incompatible with |
|-----------------|-----------------|-------------------|
| GPL-2.0-only | GPL-2.0-only, LGPL-2.1 | Apache-2.0, GPL-3.0 |
| GPL-2.0-or-later | GPL-2.0+, LGPL-2.1, GPL-3.0, Apache-2.0 | Proprietary |
| LGPL-2.1-only | LGPL-2.1, GPL-2.0+ | Apache-2.0 (if linked statically) |

**This project currently contains GPL-2.0-only files (fiview/firun, Jim Peters).**
Introducing Apache-2.0 headers (e.g. Vulkan SDK) into the same binary is
**prohibited** until the relicensing grant from Jim Peters is received and
committed to `doc/legal/`.

Any licence change requires:
1. Written permission from every copyright holder of the affected files
2. Permission stored as `doc/legal/<name>-<topic>-grant.eml` (original email)
3. SPDX identifier updated in every affected file
4. Commit with type `legal(scope): ...`

### 1.4 Developer Certificate of Origin (DCO)

All commits by contributors other than the project owner must include a
`Signed-off-by:` trailer certifying the [DCO v1.1](https://developercertificate.org/):

```
Signed-off-by: Full Name <email@example.com>
```

The project owner's commits are implicitly covered by ownership; AI co-author
commits are covered by the `Co-Authored-By:` trailer (which is not a DCO
sign-off and carries no legal weight on its own).

### 1.5 No proprietary code

No code, snippet, or algorithm derived from a proprietary source may be
committed. "Inspired by" rewriting of proprietary code is also prohibited unless
the original source is confirmed to be under a compatible free licence.

### 1.6 Export control

Cryptographic code or code with dual-use potential must be reviewed for
export-control compliance (EAR, ITAR) before publication. Flag with
`# EXPORT-REVIEW-REQUIRED` comment until cleared.

---

## 2 — Branch Model

### 2.1 Hierarchy (MANDATORY, no exceptions)

```
main            ← production-ready releases only (internal)
  ↑ --no-ff merge, via PR, CI green on develop first
develop         ← integration and stability anchor
  ↑ --no-ff merge, via PR, CI green on feature branch
feature/<X>     ← all development work
hotfix/<X>      ← critical production fixes only
release/<X>     ← release stabilisation and clean-up

publish/main    ← exact mirror of GitHub's main (see §2.5)
publish/<X.Y.Z> ← preview of a specific GitHub release
```

### 2.2 Branch naming

| Prefix | Use | Example |
|--------|-----|---------|
| `feature/` | New functionality | `feature/fftw3-backend` |
| `fix/` | Bug fix (non-critical) | `fix/sse2-alignment` |
| `hotfix/` | Critical production fix | `hotfix/cve-2026-1234` |
| `release/` | Release stabilisation | `release/v1.0.0` |
| `docs/` | Documentation only | `docs/api-reference` |
| `chore/` | Build, CI, tooling | `chore/update-actions` |
| `refactor/` | Code restructuring | `refactor/fidlib-c99` |
| `publish/` | GitHub mirror branches | `publish/main`, `publish/v1.0.0` |

Branch names: lowercase, hyphen-separated, no spaces, no underscores, no uppercase.
Maximum 50 characters.

### 2.3 Protected branches

`main` and `develop` are **protected**. The following are unconditionally prohibited:

- Direct commit (bypass PR)
- `git push --force` or `git push --force-with-lease`
- `git push -d` (deletion)
- `git rebase` that rewrites published history
- `--no-verify` (skip hooks)

**The only permitted exception:** a one-time, explicitly-authorised history
cleanup on a newly-public repository with no forks and no downstream consumers,
approved by the project owner in writing (session log or email). Such exceptions
must be documented in `doc/legal/` or the commit message of the force-push itself.

### 2.4 Merge rules

- `git merge --no-ff` ALWAYS — no fast-forward, no squash-merge into develop/main
- Squash is permitted only within a feature branch before opening a PR
- Sub-repository merges before umbrella repository merges
- Delete the feature branch after merge (keeps remote clean)

### 2.5 No skipping

```
feature/* → develop → main
```

There is no direct path from feature to main. A hotfix goes to `main` AND
`develop` (in separate PRs, or a cherry-pick — document which).

### 2.6 `publish/*` — GitHub mirror branches

`publish/main` and `publish/<X.Y.Z>` are special branches that represent
**exactly what is (or will be) visible on GitHub**. They live in the internal
self-hosted internal repository and serve as a local preview and staging area.

#### Purpose

- `publish/main` is the **staging branch** where cleanup happens.
  It starts as a copy of `release/*` and is iteratively cleaned:
  translated, squashed, stripped of internal traces — until it is
  ready for GitHub.
- `github/main` is the **local mirror** of what is actually on GitHub right now.
  It is updated by `git fetch github` and `bash scripts/sync-github-mirror.sh`.
  It is never edited directly.
- The final publication step collapses `publish/main` into **exactly one new commit**
  on top of `github/main`, then pushes that single commit to the real GitHub.

**One release = one commit on GitHub. No exceptions.**

#### Setting up the `github` remote (once, per clone)

```bash
git remote add github https://github.com/Josbrig/fidlib-rt.git
# Fetch to create remote-tracking refs without pushing anything:
git fetch github
```

`origin` points to your internal repository (self-hosted or private).
`github` is the read-mostly remote for GitHub — push only during a release act.

#### Workflow: from release/* to a single GitHub commit

```bash
# Step 1 — Load publish/main with current release content
#           (resets publish/main; existing content is discarded)
git checkout -B publish/main release/fiview2

# Step 2 — Iterative cleanup on publish/main
#           Translate, remove internal traces, fix comments.
#           Each fix: git add + git commit --amend  (or new commit — squash later)

# Step 3 — Squash everything into ONE commit on top of github/main
git fetch github                          # update local mirror
git reset --soft github/main              # collapse all publish/main work to staged changes
git commit -m "feat: fidlib-rt v0.1.0"   # one clean commit — this is what GitHub will show

# Step 4 — Final inspection (this is exactly what GitHub will receive)
git log --oneline                         # should show: 1 new commit + github/main history
git diff github/main HEAD                 # full diff of everything that will be published
git show --stat HEAD                      # files changed in the release commit

# Step 5 — Update local github/main mirror
git checkout github/main
git merge --ff-only publish/main          # fast-forward only — no merge commits

# Step 6 — Explicit release act (separate session, explicit authorisation)
git push github github/main:main
git tag -s v0.1.0 -m "Release v0.1.0" github/main
git push github v0.1.0
git fetch github && git branch -f github/main github/main  # re-sync mirror
```

#### Pushing to GitHub (release act only)

```bash
# Push the clean branch as GitHub's main:
git push github publish/main:main

# Create and push the signed tag:
git tag -s v1.0.0 -m "Release v1.0.0" publish/main
git push github v1.0.0

# After push: fetch to keep publish/main in sync with github/main
git fetch github
git branch -f publish/main github/main
```

#### Rules for `publish/*` branches

- **Never** commit work-in-progress directly to `publish/*`.
- `publish/*` branches may use fast-forward merges among themselves —
  `--no-ff` is not required here because they are never merged into
  `develop` or `main`.
- Only the project owner pushes `publish/main` to GitHub.
- After every GitHub push, `publish/main` is immediately synchronised
  with `github/main` (see above) so it stays an accurate mirror.
- `publish/<X.Y.Z>` is deleted locally after the release is tagged.

---

## 3 — Commit Standards

### 3.1 Format (Conventional Commits, mandatory)

```
type(scope): short description          ← max 72 characters

Body: explains WHY, not what. The diff already shows what.
Reference issues: Fixes #123, Relates-to #456.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

**Allowed types:**

| Type | Use |
|------|-----|
| `feat` | New feature visible to users |
| `fix` | Bug fix |
| `perf` | Performance improvement |
| `refactor` | Code restructuring without behaviour change |
| `test` | Test additions or changes |
| `docs` | Documentation only |
| `build` | Build system, cmake, dependencies |
| `ci` | CI/CD configuration |
| `chore` | Maintenance, tooling, no production code |
| `legal` | Licence, copyright, DCO |
| `security` | Security fix (may omit details if CVE pending) |
| `revert` | Reverts a previous commit |

**Prohibited commit messages:**
- `fix`, `WIP`, `wip`, `temp`, `test`, `asdf`, `update`, `changes`, `misc`
- Single-word messages
- Messages that describe what the diff already shows ("add function X")
- Messages referencing a person's name without context

### 3.2 Atomic commits

Each commit must be:
- **Self-contained:** compiles and passes tests on its own
- **Single-purpose:** one logical change, not a bundle of unrelated fixes
- **Reversible:** `git revert` must produce a clean, working state

Commits that break the build, even temporarily, are **prohibited** on
`develop` and `main`. On feature branches they are tolerated only as
clearly-marked WIP squashed before PR.

### 3.3 Commit author

```bash
GIT_AUTHOR_NAME="Jörg Simbrig"
GIT_AUTHOR_EMAIL="Josbrig@simbrig.de"
```

AI co-author on every commit:
```
Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

### 3.4 No secrets, ever

**Absolute prohibition.** No commit may contain:
- API keys, tokens, passwords, private keys, certificates
- Database connection strings with credentials
- Personally identifiable information (PII) beyond what is strictly necessary
- Internal hostnames, IP addresses, or network topology that is not public

If a secret is accidentally committed:
1. Revoke the secret **immediately** (before anything else)
2. Force-push to remove it (this is one of the few permitted force-push cases)
3. Notify all people with repo access
4. Document the incident in `doc/legal/security-incident-<date>.md`
5. Audit all systems that might have cloned or cached the repo

Git history does not "forget" — the secret must be considered fully compromised
from the moment it was pushed, regardless of subsequent removal.

### 3.5 FetchContent and submodule pins

- FetchContent `GIT_TAG`: full 40-character SHA1 — **never** a branch name or `HEAD`
- Submodule SHA: pinned, reviewed before update
- Dependency updates require their own `chore(deps):` commit with justification

---

## 4 — Pre-Merge Quality Gates

Every PR targeting `develop` or `main` must pass **all** of the following
before merge is permitted. No exceptions. No "we'll fix it in the next PR."

### 4.1 Build gate (both modes mandatory)

```bash
# Release — catches -O2-level warnings invisible in Debug
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_FIVIEW=OFF -DFIDLIB_FFT=ON -DFIDLIB_SIMD=ON \
  -S . -B build_release
cmake --build build_release -j$(nproc)
ctest --test-dir build_release --output-on-failure

# Debug + sanitisers — catches UB, memory errors, races
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_FIVIEW=OFF -DFIDLIB_FFT=ON -DFIDLIB_SIMD=ON \
  -S . -B build
cmake --build build -j$(nproc)
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build --output-on-failure
```

Both must report **0 test failures, 0 compiler warnings** (all warnings are errors).

### 4.2 CI gate

GitHub Actions CI must show a green checkmark on the PR's head commit before
merge. A green badge on a **different** commit is not sufficient.

CI failing on `develop` or `main` is a **P0 incident** — all other work stops
until CI is green again.

### 4.3 Code review

- Minimum 1 review approval from a person other than the PR author
- For AI-authored commits: the project owner is the reviewer
- Review must cover: correctness, security, licence compliance, test coverage
- All review comments must be resolved or explicitly accepted before merge
- "Looks good" without reading the diff is not a valid review

### 4.4 Test coverage

Every new function or behaviour must have at least one corresponding test.
Tests must:
- Be deterministic (no random seeds without fixed initialization)
- Not depend on network access
- Not depend on wall-clock time beyond a generous timeout
- Produce the same result on every platform (x86-64 and aarch64)

### 4.5 Static analysis (recommended, mandatory for security-sensitive code)

```bash
cppcheck --enable=all --error-exitcode=1 --suppress=missingIncludeSystem \
  fidlib/ firun/ fiview/src/
```

Findings rated `error` or `warning` block merge. `style` and `performance`
findings must be addressed or explicitly documented as accepted risk.

### 4.6 No TODO/FIXME without ticket

Committed `TODO` or `FIXME` comments must reference an open issue:
```c
// TODO(#42): replace with SIMD path once Vulkan backend is stable
```
Uncommitted `TODO` without a ticket reference is a merge blocker.

---

## 5 — Pull Request Rules

### 5.1 PR checklist (author responsibility)

Before opening a PR, the author confirms:

- [ ] Feature branch is up-to-date with target branch (`git rebase` or merge)
- [ ] Release build passes locally (Section 4.1)
- [ ] Debug + sanitiser build passes locally (Section 4.1)
- [ ] All new code has tests
- [ ] SPDX identifier present on all new files
- [ ] No secrets, no credentials, no PII
- [ ] Commit messages follow Section 3.1
- [ ] Branch name follows Section 2.2
- [ ] CHANGELOG.md updated (for `feat` and `fix` PRs targeting `develop`)

### 5.2 PR title and description

Title: follows Conventional Commits format (Section 3.1), max 72 characters.

Description must contain:
- **What** changed and **why** (not just a repeat of the commit messages)
- **How it was tested** (which tests, which platforms)
- **Risk assessment** (could this break anything?)
- **Licence note** (any new dependency? licence reviewed?)

### 5.3 Self-merge prohibition

The author of a PR may not merge their own PR without a second review, except:
- Documentation-only PRs with no code changes
- Emergency hotfixes with documented justification

### 5.4 Stale PRs

A PR not updated for 30 days is automatically stale. Stale PRs must be either:
- Rebased and updated
- Closed with a note explaining why

### 5.5 PR size

PRs should be small enough to be reviewed in one sitting (≤ 400 lines of
non-test code changed as a guideline). Larger PRs require explicit justification
in the description. "It's one feature" is not sufficient — features can be split
into reviewable increments.

---

## 6 — Release Process

### 6.1 Semantic versioning

All releases follow SemVer 2.0.0:

```
MAJOR.MINOR.PATCH[-prerelease][+build]
```

| Increment | When |
|-----------|------|
| PATCH | Backwards-compatible bug fix |
| MINOR | New backwards-compatible functionality |
| MAJOR | Breaking API or ABI change |

Pre-release: `v1.0.0-alpha.1`, `v1.0.0-rc.2`

### 6.2 GitHub publication policy

**GitHub is a read-only publication mirror. It is not a development platform.**

- All development work happens on the internal repository (internal git server or local).
- GitHub receives only clean, finalised releases — never work-in-progress commits,
  never internal tooling commits, never publication-process traces.
- No `git push` to GitHub outside of a deliberate, authorised release act.
- No rebasing, amending, or force-pushing on GitHub after publication.
- If a fix is needed post-release, it follows the full internal cycle first,
  then a new release is published to GitHub. GitHub history is never patched.

### 6.3 History squash before publication

The internal `release/*` branch accumulates work-in-progress commits
(tooling fixes, email changes, incremental adjustments). These must **not**
appear on GitHub.

Before pushing to GitHub, squash the release branch into a minimal set of
meaningful, self-contained commits that describe **what the software is**,
not how it was developed:

```bash
# Example: squash all release-branch work into one clean release commit
git rebase -i $(git merge-base release/vX.Y.Z main)
# or: git reset --soft <first-commit-sha> && git commit
```

Permitted commit types in the public GitHub history:

| Type | Example |
|------|---------|
| `feat` | New feature, complete and working |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `build` | Build system, dependencies |
| `legal` | Licence, copyright, disclaimer |

**Prohibited in public history:**
- Internal tooling commits (`chore: update SHA`, `fix: email address`)
- Intermediate refactors that were superseded
- Process traces (`legal: Kai Dieki → Jörg Simbrig`)
- Any commit whose message reveals internal infrastructure

### 6.4 English language requirement

**All content published to GitHub must be in English.**

This applies without exception to:
- All source code comments
- All commit messages in the public history
- All documentation files (`doc/`, `manuals/`, `README.md`, `CHANGELOG.md`)
- `CMakeLists.txt` comments and option descriptions
- Any `TODO` or `FIXME` comments left in published code

German-language content in the internal repository is acceptable.
On the `release/*` branch, all German text must be translated before
the history is squashed and pushed to GitHub.

Verify before squash:
```bash
# Spot-check for German words in published files
grep -rn " und \| oder \| mit \| für \| ist \| bitte \| wird " \
  --include="*.cpp" --include="*.hpp" --include="*.h" --include="*.c" \
  --include="*.md" --include="CMakeLists.txt" .
```

### 6.5 Release checklist

Before tagging a release:

1. `develop` CI is green
2. All planned issues for the milestone are closed or explicitly deferred
3. `CHANGELOG.md` updated with full release notes (English)
4. Version number bumped in `CMakeLists.txt` and any version headers
5. All source comments, docs and commit messages translated to English (§6.4)
6. History squashed to clean public commits (§6.3)
7. Release PR from `develop` to `main` opened, reviewed, approved
8. PR merged with `--no-ff`
9. Tag created on `main`: `git tag -s v<X.Y.Z> -m "Release v<X.Y.Z>"`
10. Tag pushed to GitHub: `git push github v<X.Y.Z>`
11. GitHub Release created with the CHANGELOG section as description
12. `develop` updated to next development version
13. GitHub remote is never used for day-to-day `git push` — only for releases

### 6.6 Signed release tags

Release tags must be GPG-signed (`git tag -s`). Unsigned release tags on `main`
are not permitted for published releases.

If GPG signing is not available, document why in the release notes and use
SHA256 checksums of the release artifacts as a minimum integrity measure.

### 6.7 No "fix the release" commits on main

If a bug is found after release, the fix goes through the normal
`feature/fix → develop → main` cycle or a `hotfix/` branch.
Never commit directly to `main` to "quickly fix" a release.

---

## 7 — Security Rules

### 7.1 Dependency pinning

All external dependencies (FetchContent, submodules, system packages) must be
pinned to exact, reviewed versions. "Latest" or branch-tracked dependencies are
prohibited on `develop` and `main`.

### 7.2 Dependency provenance

Before adding a new dependency:
- Verify the source repository is the canonical upstream (not a fork)
- Check the last commit date (abandoned projects = risk)
- Review the licence (Section 1.3)
- Check for known CVEs: `nvd.nist.gov`, `osv.dev`
- Document the decision in the PR description

### 7.3 No dynamic loading of untrusted code

`dlopen()`, `eval()`, shell injection via `system()`, and similar runtime code
loading are prohibited unless explicitly justified and reviewed.

### 7.4 Input validation at all trust boundaries

All input from outside the process boundary (command-line arguments, files,
network, environment variables) must be validated before use. No format string
from external input may reach `printf`-family functions without sanitisation.

### 7.5 Compiler hardening flags (Release)

The Release build must include, where supported by the target compiler:

```cmake
-D_FORTIFY_SOURCE=2
-fstack-protector-strong
-fPIE (for executables)
```

These are in addition to the standard `-Wall -Wextra -Wconversion -Wshadow -Werror`.

### 7.6 No shell=true with user input

CMake `execute_process`, `add_custom_command`, and similar must never construct
shell commands by string-concatenating user-controlled values.

---

## 8 — AI Agent Rules (specific to Claude Code workflows)

### 8.1 No autonomous commit or push

The AI agent **never** commits or pushes without an explicit instruction from
the project owner in the current session. Phrases like "task completed",
"file created", "CI is green" are **not** commit or push instructions.

Explicit instructions only: "commit", "commit this", "mach einen Commit", "los",
"push", "push to github", "release now".

### 8.2 Push instructions are single-use

A push instruction authorises exactly one push for the explicitly named
batch/branch/target. It does not authorise future pushes in the same session.

### 8.3 Force-push requires explicit, named authorisation

The AI agent may only force-push when the project owner explicitly states:
- The target branch
- That a force-push is intended
- The reason

"Clean up the history" without naming the branch and confirming force-push
is **not** sufficient authorisation.

### 8.4 Branch rule enforcement

The AI agent must refuse to commit directly to `main` or `develop` unless
the project owner has explicitly confirmed this is intentional and has
accepted the consequences. The agent should suggest the correct branch.

### 8.5 Commit author identity

Every commit uses the fixed author identity from Section 3.3. The AI agent
must not use its own identity as the sole author; it is always a co-author.

---

## 9 — Industry Standards Compliance Reference

This section documents how this project's rules map to external standards.
Full compliance with these standards is **not claimed** — they are referenced
as the basis for the strictest applicable rules.

### 9.1 IEC 62304 — Medical Device Software (mapped rules)

| IEC 62304 clause | This ruleset |
|-----------------|-------------|
| 5.1 Software development planning | `doc/` planning documents, CLAUDE.md |
| 5.2 Software requirements analysis | Feature branch with requirements in PR |
| 5.5 Software unit implementation | Section 3 (commit standards) |
| 5.5.4 Evaluate software unit test procedures | Section 4 (quality gates) |
| 5.6 Software integration testing | Section 4.1 (both build modes) |
| 5.7 Software system testing | CI on every PR |
| 5.8 Software release | Section 6 (release process) |
| 6.1 Software maintenance plan | CHANGELOG.md, semantic versioning |
| 7.4 Change control | PR process, no direct commits to protected branches |
| 8.1 Configuration management | Branch model, pinned dependencies |

### 9.2 DO-178C — Software in Airborne Systems (mapped rules)

| DO-178C objective | This ruleset |
|------------------|-------------|
| OBJ-1 High-level requirements | Feature branch description, PR body |
| OBJ-5 Software architecture | `doc/` architecture documents |
| OBJ-9 Source code — standards | Section 4.5 (static analysis), Section E (C coding) |
| OBJ-10 Source code — verifiable | No VLAs, `extern "C"`, deterministic tests |
| OBJ-13 Normal-range tests | ctest suite, Section 4.1 |
| OBJ-14 Robustness tests | ASan/UBSan, Section 4.1 |
| OBJ-15 Software test coverage | Section 4.4 (every function covered) |
| OBJ-17 Problem reporting | GitHub Issues, Section 4.6 (TODO/ticket) |
| OBJ-18 Change control | Section 5 (PR process) |
| OBJ-19 Change review | Section 5.3 (review, no self-merge) |
| OBJ-21 Configuration baseline | Git tags, Section 6 (release process) |

### 9.3 ISO 26262 — Automotive Functional Safety (mapped rules)

| ISO 26262 requirement | This ruleset |
|----------------------|-------------|
| Part 6, 7.4.5 — Coding guidelines | MISRA C subset, Section E |
| Part 6, 7.4.6 — Code review | Section 5.3 (mandatory review) |
| Part 6, 7.4.7 — Dynamic analysis | ASan/UBSan mandatory (Section 4.1) |
| Part 6, 7.4.8 — Static analysis | Section 4.5 (cppcheck) |
| Part 6, 8.4.4 — Software unit testing | 100% decision coverage target |
| Part 8 — Supporting processes | CI/CD, version control, Section 2–6 |

### 9.4 MISRA C:2012 (mandatory rules applied)

| MISRA rule | Requirement | Status |
|-----------|-------------|--------|
| Dir 4.1 | Initialise all objects with automatic storage | **Enforced** (Section 4.1, GCC -Wmaybe-uninitialized) |
| Dir 4.7 | Check return values of functions | Enforced by code review |
| Rule 1.3 | No undefined behaviour | **Enforced** (UBSan mandatory) |
| Rule 14.4 | Controlling expressions shall be essentially Boolean | Code review |
| Rule 15.5 | Function should have a single point of exit | Guideline (legacy code exempt) |
| Rule 17.3 | No function pointers to functions with non-prototype declarations | Enforced by C99 + -Wall |
| Rule 21.6 | Standard I/O functions shall not be used in production code | Not applicable (CLI tool) |

---

## 10 — Violations and Enforcement

### 10.1 Blocking violations (merge/push refused)

The following constitute hard blockers. Work stops until resolved:

- Missing SPDX identifier on new file
- Licence-incompatible dependency introduced
- Secret or credential committed
- CI red on `develop` or `main`
- Force-push to `main` or `develop` without authorisation
- Direct commit to `main` or `develop`
- Build fails in Release mode
- Test fails in any mode
- Compiler warning (`-Werror` prevents this from being a human decision)

### 10.2 Non-blocking violations (must be fixed before next release)

- Missing test for new functionality
- TODO/FIXME without ticket
- Commit message not following Section 3.1
- Static analysis finding rated `warning`
- PR description incomplete

### 10.3 Incident documentation

Security incidents (secret committed, force-push without authorisation,
licence violation discovered post-merge) are documented in `doc/legal/`
with:
- Date and time
- What happened
- Immediate remediation taken
- Root cause
- Process improvement to prevent recurrence

---

## 11 — GitHub Repository Settings (configure once, verify periodically)

### Branch protection for `main` and `develop`

Enable in GitHub → Settings → Branches → Branch protection rules:

- [x] Require a pull request before merging
- [x] Require approvals: 1
- [x] Dismiss stale pull request approvals when new commits are pushed
- [x] Require status checks to pass before merging: `CI / Debug`, `CI / Release`
- [x] Require branches to be up to date before merging
- [x] Do not allow bypassing the above settings
- [x] Restrict force pushes: nobody (including admins)
- [x] Restrict deletions

### Recommended additional settings

- [ ] Require signed commits (enable when GPG workflow is established)
- [ ] Require linear history (optional — conflicts with --no-ff policy, leave off)
- [ ] Automatically delete head branches after merge

---

*Last updated: 2026-05-29*
*Authoritative version: `doc/github-strict-ruleset.md` in the `main` branch*
