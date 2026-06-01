# Code Generation in fidlib / fiview — Analysis

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Jim Peters built three distinct mechanisms for generating executable filter
code from a filter specification. This document describes all three, their
current status in fidlib-rt, and a potential future direction.

---

## 1. `fid_design_coef()` — Coefficient extraction to `double[]`

**File:** `fidlib/fidlib.c:1749`  
**Status:** Present and working in fidlib-rt.

This function designs a filter from a spec string and dumps all
non-constant coefficients into a caller-supplied `double[]` array:

```c
#define N_COEF 8   // must match the filter exactly
double coef[N_COEF];
double gain = fid_design_coef(coef, N_COEF, "LpBe4/1000", 44100.0, 0, 0, 0);
```

The returned array can be embedded verbatim as a C static initialiser,
together with a hand-written or fiview-generated filter loop.
The companion function `fid_cv_array()` reconstructs a `FidFilter` from
such a pre-prepared `double[]` at runtime — meaning the filter structure
is completely described by the array and needs no spec-string parser at
runtime:

```c
// Pre-designed Butterworth LP, 4th order — coefficients known at compile time
static double butter_lp[] = { 'I', 3, 1.0, 0.55, 0.77, 'F', 3, 1, -2, 1, 0 };
FidFilter *filt = fid_cv_array(butter_lp);
```

This is described in `doc/reference/fidlib.txt:259-284` as:
> "The purpose of this routine is to allow pre-prepared filters to be
> included conveniently in C source as a double array."

**Use case:** Embedded targets where the filter type is fixed at compile time
and fidlib's spec-string parser is undesirable overhead. The filter loop
is hand-written or generated; only the coefficient values come from fidlib.

---

## 2. fiview — C source code generator for maximum performance

**File:** fiview GUI (interactive feature, menu item)  
**Status:** Feature present in the original fiview; not yet ported to fidlib-rt.

The original fiview application could export a **custom C source file** for a
specific filter class. The documentation (`doc/reference/fidlib.txt:21-24`) states:

> "See Fiview for a more interactive introduction to the filters, more related
> documentation, and for the opportunity to generate even higher performing
> filters through **generating C code** for a single class of filters, compiling
> it, and at run-time filling in the coefficients using fidlib."

And at line 358:
> "For really time-critical applications, to gain maximum speed for a particular
> filter type, Fiview can be used to generate a special-purpose routine."

### How it worked

1. The user designs a filter in fiview's GUI.
2. fiview generates a `.c` file containing a hardcoded filter loop — no generic
   dispatch, no command list, no function pointers. Just a tight, inlined
   biquad cascade or FIR loop for exactly the designed filter order and structure.
3. The generated code is compiled into the target application.
4. At runtime, `fid_design_coef()` fills the `coef[]` array with the actual
   coefficient values for any frequency/rate combination within that filter class.
   The structure (IIR order, FIR length) is fixed at compile time; only the
   numerical values are determined at runtime.

### Why it is faster than the command-list engine

The cmdlist engine (`fidrf_cmdlist.h`) is general-purpose: it iterates over
an opcode list, dispatching to different code fragments for each biquad stage.
This involves indirect branches and loop overhead that a modern CPU cannot
fully eliminate.

The fiview-generated code has none of this: it is a straight-line or
minimally-looped biquad cascade, with the number of stages known to the
compiler, enabling full unrolling, register allocation, and instruction
scheduling.

### Current state in fidlib-rt

`fid_design_coef()` is present and used; the fiview C-code generator itself
is part of fiview's GUI code and has not been extracted into a standalone API.
The capability is implicitly present — a `fid_export_c()` function would
be a natural addition (see Section 4 below).

---

## 3. `RF_JIT` (`fidrf_jit.h`) — Runtime JIT to native x86 machine code

**Status:** Abandoned upstream; file absent from JamesHight fork and fidlib-rt.

Jim Peters wrote a JIT compiler that, at `fid_run_new()` time, wrote x86
machine instructions directly into an executable memory page — a fully
native filter loop with coefficients baked in as immediate values.

From `doc/reference/fidlib.txt:351-355`:
> "Two approaches were attempted, a JIT-compiler approach, based on writing
> ix86 instructions to memory for the filter core, and a command-list method,
> switching between fragments of pre-generated code. The JIT version only
> gave a **20% advantage** and was non-portable, so it was abandoned."

The 20% advantage was not worth the maintenance cost and the x86-only
constraint. The command-list engine with modern compiler optimisations (`-O2`)
closes most of the gap.

**Modern equivalent:** fidlib-rt's SIMD backends (NEON/SSE2) and the
Vulkan/OpenCL compute backends achieve far greater speedup than the JIT
ever could, for the appropriate filter sizes.

---

## 4. Future direction: `fid_export_c()`

The fiview C-code generation feature addresses a genuine use case:
**embedded targets that must not carry a filter design engine at runtime**,
but still need parameter-adjustable filters (different sample rates,
different cutoff frequencies, same filter order).

A `fid_export_c()` function in fidlib-rt could expose this as a
programmatic API:

```c
// Proposed API (not yet implemented)
int fid_export_c(
    FILE        *out,        // write generated .c source here
    const char  *spec,       // filter specification string
    double       rate,       // sample rate (determines filter order/structure)
    double       freq0,      // frequency parameter
    double       freq1,      // second frequency parameter (bandpass/bandstop)
    const char  *func_name   // name of the generated filter function
);
```

Output would be a self-contained `.c` file containing:
- A `static const int N_COEF = <n>;` declaration
- A `double <func_name>_step(double *state, const double *coef, double x)`
  function — a tight, fully-unrolled biquad cascade or FIR loop
- A comment block documenting the filter type, order, and design parameters

The companion runtime call would remain `fid_design_coef()` to fill the
`coef[]` array at startup.

### Target use case

```
[Design machine (PC/RPi)]
   fid_export_c("LpBe4", ...) → lpbe4_filter.c

[Build system]
   gcc -O2 lpbe4_filter.c myapp.c -o myapp
   # No fidlib required at runtime on the target

[Embedded target at runtime]
   // Only coefficient calculation needed — could use a tiny lookup table
   // or a stripped-down fid_design_coef() with no spec parser
   double coef[N_COEF];
   fid_design_coef(coef, N_COEF, "LpBe4/...", rate, freq, 0, 0);
   // run generated loop:
   output = lpbe4_filter_step(state, coef, input);
```

This is **not yet implemented** in fidlib-rt. It is documented here as a
known gap relative to the original fiview feature set.

---

## Summary table

| Mechanism | Where | Status in fidlib-rt | Use case |
|-----------|-------|---------------------|----------|
| `fid_design_coef()` | `fidlib/fidlib.c:1749` | ✅ Present | Extract coefficients to `double[]` for static embedding |
| `fid_cv_array()` | `fidlib/fidlib.c` | ✅ Present | Reconstruct filter from pre-designed `double[]` at runtime |
| fiview C-code generator | fiview GUI | ⚠️ Not ported | Generate hardcoded filter loop for embedded targets |
| `RF_JIT` x86 JIT | `fidrf_jit.h` (absent) | ❌ Abandoned | Runtime machine code generation (superseded by SIMD) |
| `fid_export_c()` | — | 🔲 Not yet implemented | Programmatic C-code export API |

---

*Sources: `doc/reference/fidlib.txt`, `fidlib/fidlib.c`, `fidlib/fidrf_cmdlist.h`*
