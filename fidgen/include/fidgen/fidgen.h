// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
//
// fidgen.h — C API for the fidgen filter code generator
//
// Usage (C):
//   char* code = NULL;
//   fidgen_error_t rc = fidgen_generate("LpBu4/1000", 44100.0,
//                                       "c99", "lpfilter", NULL, &code);
//   if (rc == FIDGEN_OK) { puts(code); fidgen_free(code); }
//
// NOTE: fidlib calls exit() for completely unknown filter type strings
// (e.g. "Xyz/1000"). fidgen_generate() can only return FIDGEN_ERR_DESIGN_FAILED
// for specs that fidlib recognises but cannot design (bad parameters).
// Passing a garbage spec string may terminate the process.

#ifndef FIDGEN_H
#define FIDGEN_H

#ifdef __cplusplus
extern "C" {
#endif

// ── Error codes ───────────────────────────────────────────────────────────────

typedef enum fidgen_error {
    FIDGEN_OK                = 0,
    FIDGEN_ERR_UNKNOWN_LANG  = 1,  // unknown language key
    FIDGEN_ERR_DESIGN_FAILED = 2,  // fid_design returned NULL
    FIDGEN_ERR_UNSTABLE      = 3,  // filter has poles outside unit circle
    FIDGEN_ERR_OUT_OF_MEMORY = 4,  // malloc failed
} fidgen_error_t;

// ── SIMD level ────────────────────────────────────────────────────────────────

typedef enum fidgen_simd {
    FIDGEN_SIMD_NONE = 0,   // scalar only
    FIDGEN_SIMD_NEON = 1,   // ARM NEON float64x2_t (2-channel)
    FIDGEN_SIMD_SSE2 = 2,   // x86 SSE2 __m128d (2-channel)
    FIDGEN_SIMD_AVX2 = 3,   // x86 AVX2 __m256d (4-channel)
    FIDGEN_SIMD_AUTO = 4,   // emit all variants, runtime-guarded
} fidgen_simd_t;

// ── Options ───────────────────────────────────────────────────────────────────

typedef struct fidgen_options {
    fidgen_simd_t simd;       // SIMD variant to generate (default: NONE)
    int  with_guard;          // 0/1 — emit include guard (default: 1)
    int  with_test;           // 0/1 — append self-test code (default: 0)
    int  fpga_bits;           // Verilog data+coef bit width (default: 24)
    int  fpga_frac;           // Verilog fractional bits (-1 = auto, default: -1)
} fidgen_options_t;

// ── API ───────────────────────────────────────────────────────────────────────

// Generate filter code.
//
//   spec     — fidlib spec string, e.g. "LpBu4/1000", "BpBu2/500-2000"
//   rate     — sample rate in Hz (e.g. 44100.0)
//   lang     — language key: "c99", "cpp20", "python"/"py", "rust"/"rs",
//              "matlab"/"octave"/"m", "julia"/"jl", "verilog"/"v",
//              "systemverilog"/"sv"
//   name     — C identifier prefix (NULL or "" = auto-derive from spec)
//   opts     — generator options (NULL = defaults)
//   out_code — receives a malloc'd NUL-terminated string; caller must
//              release with fidgen_free()
//
// Returns FIDGEN_OK on success, error code otherwise.
// On error *out_code is set to NULL.
fidgen_error_t fidgen_generate(
    const char*              spec,
    double                   rate,
    const char*              lang,
    const char*              name,
    const fidgen_options_t*  opts,
    char**                   out_code
);

// Release memory returned by fidgen_generate().
void fidgen_free(char* code);

// Version string, e.g. "0.1.0".
const char* fidgen_version(void);

// Human-readable description of an error code.
const char* fidgen_error_str(fidgen_error_t err);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // FIDGEN_H
