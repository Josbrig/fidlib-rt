# Sources to Secure — Inventory

Date: 2026-05-26  
Context: All external resources referenced by fiview/fidlib, assessed by
availability and relevance to the project.

---

## Status Overview

| Resource | URL / Source | Status | Priority |
|---|---|---|---|
| fidlib.txt (API docs) | uazu.net/fidlib/fidlib.txt | ✅ secured | ✅ done |
| firun.txt (CLI docs) | uazu.net/fidlib/firun.txt | ✅ secured | ✅ done |
| fidlib-0.9.10.tgz (original) | uazu.net/fidlib/ | ✅ reachable | medium |
| mkfilter source code | University of York | ❌ **dead** | **high** |
| mkfilter (billthefarmer fork) | github.com/billthefarmer/mkfilter | ✅ reachable | **high** |
| Audio EQ Cookbook (original) | harmony-central.com | ❌ **dead** (casino site) | — |
| Audio EQ Cookbook (W3C canonical) | webaudio.github.io/Audio-EQ-Cookbook | ✅ secured | ✅ done |
| GMeteor 0.95 | gmeteor.sourceforge.net | ✅ secured | ✅ done |
| OpenEEG | openeeg.sf.net | ⚠️ SourceForge | low |
| fiview_log.txt | uazu.net/fiview/fiview_log.txt | ✅ secured | ✅ done |
| fiview-0.9.10 source code | uazu.net/fiview/ | ✅ secured | ✅ done |
| fidlib (JamesHight) | github.com/JamesHight/fidlib | ✅ in fidlib/ | ✅ done |
| SDL 2 | github.com/libsdl-org/SDL | ✅ local mirror available | ✅ done |
| SDL 1.2 | github.com/libsdl-org/SDL-1.2 | mirror pending | open |

---

## Detailed Assessment

### 1. fidlib.txt — API Documentation (HIGH)

**URL:** https://uazu.net/fidlib/fidlib.txt (25 KB)

The only complete reference for all 47 filter types, the fispec string syntax,
the complete C API, and the internal data structures (`FidFilter`, `FidRun`).
Not included in the JamesHight GitHub repo (only as a compressed part of the `.tgz`).

→ **Secure:** `doc/reference/fidlib.txt`

---

### 2. firun.txt — CLI Documentation (HIGH)

**URL:** https://uazu.net/fidlib/firun.txt (5 KB)

Complete description of all firun options, data formats (`a`, `b`, `s`, `S`, `f`, …),
multi-channel filter chaining syntax and test modes (impulse, step, frequency response).
Also not in the JamesHight repo, but in the vendor/fiview tarball as `README.firun`.

→ **Secure:** `doc/reference/firun.txt` (original version from uazu.net)

---

### 3. mkfilter — Mathematical Core (HIGH, AT RISK)

**Original URL:** http://www-users.cs.york.ac.uk/~fisher/mkfilter — **DEAD**

Tony Fisher (University of York) is deceased. All URLs under `cs.york.ac.uk/~fisher/`
have been replaced by a 301 redirect to the computer science department's general news page.
The source code is no longer officially available.

**Why critical:** `fidmkf.h` in fidlib is a direct derivative of mkfilter. The
algorithm for Butterworth, Bessel and Chebyshev filters (pole calculation, bilinear
transform, prewarping) comes entirely from mkfilter. Without the source, the
mathematical basis of the project can no longer be traced.

**Available archives:**
- `github.com/billthefarmer/mkfilter` — 22 commits, C++, slightly modernized for
  current GCC versions, PNG plot support added. Contains original docs
  (`doc.pdf`). **Best available source.**
- `github.com/minimum-necessary-change/mkfilter` — port for modern compilers,
  references York URL as source.
- `github.com/MikeCurrington/mkfilter` — includes bugfixes from Miriam Ruiz (Debian).

→ **Secure:** `billthefarmer/mkfilter` as second submodule under `vendor/mkfilter`

---

### 4. Audio EQ Cookbook — Biquad Reference (MEDIUM)

**Original URL:** http://www.harmony-central.com/Computer/Programming/Audio-EQ-Cookbook.txt  
→ **DEAD** (domain now shows a French online casino site)

**Canonical replacement URL (W3C/WebAudio WG):**  
https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html  
Published with permission of Robert Bristow-Johnson, edited by Raymond Toy
and Doug Schepers. Contains all biquad coefficient formulas (LP, HP, BP, Notch,
Allpass, Peaking EQ, Low-Shelf, High-Shelf) with complete mathematical derivation.

fidlib uses these formulas for the Audio EQ filter types (`LpBq`, `HpBq`, `PkBq`,
`LsBq`, `HsBq` etc.).

→ **Secure:** `doc/reference/audio-eq-cookbook.html` (local copy of the W3C version)

---

### 5. GMeteor 0.95 — FIR Constraint Design (MEDIUM, AT RISK)

**URL:** https://gmeteor.sourceforge.net/

Tool for equiripple FIR filters with linear phase according to arbitrary
frequency response specifications (via Guile/Scheme scripts). Last release: 2013.
SourceForge projects are historically at risk (downtime, link rot,
adware injections into binaries a few years ago).

Relevance: fidlib contains no FIR constraint design algorithms — GMeteor fills
this gap for complex FIR requirements.

→ **Secure:** source tarball `gmeteor-0.95.tar.gz` under `vendor/gmeteor/`

---

### 6. fidlib-0.9.10.tgz — Original Jim Peters (MEDIUM)

**URL:** https://uazu.net/fidlib/fidlib-0.9.10.tgz (86 KB)

Contains the original version without JamesHight patches, plus the precompiled
Linux binary of firun. Relevant as a diff reference: what changes did JamesHight
make relative to the original?

The tarball also contains `fidlib.txt` and `firun.txt` — if uazu.net ever goes
offline, this is an additional backup.

→ **Secure:** Can be saved as a direct download or the source code extracted from
the original tarball and placed in `vendor/fidlib-orig/`.

---

### 7. OpenEEG — Historical Application Context (LOW)

**URL:** http://openeeg.sf.net/

The OpenEEG project was the original occasion for the development of fidlib.
Contains EEG hardware schematics and software. Historically interesting as
application context, but not relevant to the filter design project itself.

→ No securing needed

---

## Recommended Securing Order

```
1. vendor/mkfilter/     ← billthefarmer/mkfilter as submodule  (CRITICAL)
2. doc/reference/fidlib.txt                                       (important)
3. doc/reference/firun.txt                                        (important)
4. doc/reference/audio-eq-cookbook.html                           (important)
5. vendor/gmeteor/      ← tarball from SourceForge               (risk-driven)
6. vendor/fidlib-orig/  ← uazu.net original tarball               (optional)
```
