#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-2026 Kai Dieki
#
# Smoke-Test für fidgen CLI — alle Sprachen, SIMD, Sonderfälle.
# Aufruf: bash scripts/test-fidgen.sh [BUILD_DIR]

set -euo pipefail

BUILD="${1:-build}"
FIDGEN="$BUILD/bin/fidgen"

if [[ ! -x "$FIDGEN" ]]; then
    echo "FEHLER: $FIDGEN nicht gefunden. Erst bauen:" >&2
    echo "  cmake --build $BUILD --target fidgen" >&2
    exit 1
fi

pass=0
fail=0

check() {
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "  OK  $desc"
        (( pass++ )) || true
    else
        echo "FAIL  $desc"
        (( fail++ )) || true
    fi
}

check_output() {
    local desc="$1"; local pattern="$2"; shift 2
    local out
    out=$("$@" 2>&1)
    if echo "$out" | grep -q "$pattern"; then
        echo "  OK  $desc"
        (( pass++ )) || true
    else
        echo "FAIL  $desc  (pattern '$pattern' nicht gefunden)"
        (( fail++ )) || true
    fi
}

echo "=== fidgen smoke tests ($FIDGEN) ==="
echo ""

# ── Meta ──────────────────────────────────────────────────────────────────────
echo "-- Meta"
check_output "--version"      "fidgen"      "$FIDGEN" --version
check_output "--list-langs"   "c99"         "$FIDGEN" --list-langs
check_output "--list-langs"   "verilog"     "$FIDGEN" --list-langs
check_output "--check"        "Stable:  yes" "$FIDGEN" --check "LpBu4/1000"

# ── Sprachgeneratoren ─────────────────────────────────────────────────────────
echo ""
echo "-- Generatoren"
check "c99         LpBu4/1000"         "$FIDGEN" -l c99          "LpBu4/1000"
check "cpp20       LpBu4/1000"         "$FIDGEN" -l cpp20         "LpBu4/1000"
check "python      LpBu4/1000"         "$FIDGEN" -l python        "LpBu4/1000"
check "rust        LpBu4/1000"         "$FIDGEN" -l rust          "LpBu4/1000"
check "matlab      LpBu4/1000"         "$FIDGEN" -l matlab        "LpBu4/1000"
check "julia       LpBu4/1000"         "$FIDGEN" -l julia         "LpBu4/1000"
check "verilog     LpBu4/1000"         "$FIDGEN" -l verilog       "LpBu4/1000"
check "systemverilog LpBu4/1000"       "$FIDGEN" -l systemverilog "LpBu4/1000"

# Sprachaliasnamen
check "alias: c"      "$FIDGEN" -l c      "LpBu4/1000"
check "alias: py"     "$FIDGEN" -l py     "LpBu4/1000"
check "alias: rs"     "$FIDGEN" -l rs     "LpBu4/1000"
check "alias: octave" "$FIDGEN" -l octave "LpBu4/1000"
check "alias: m"      "$FIDGEN" -l m      "LpBu4/1000"
check "alias: jl"     "$FIDGEN" -l jl     "LpBu4/1000"
check "alias: v"      "$FIDGEN" -l v      "LpBu4/1000"
check "alias: sv"     "$FIDGEN" -l sv     "LpBu4/1000"

# ── Filtertypen ───────────────────────────────────────────────────────────────
echo ""
echo "-- Filtertypen"
check "Butterworth HP   HpBu4/8000"       "$FIDGEN" -l c99 "HpBu4/8000"
check "Butterworth BP   BpBu4/500-2000"   "$FIDGEN" -l c99 "BpBu4/500-2000"
check "Butterworth BS   BsBu4/500-2000"   "$FIDGEN" -l c99 "BsBu4/500-2000"
check "Bessel LP        LpBe4/1000"       "$FIDGEN" -l c99 "LpBe4/1000"
check "Chebyshev LP     LpCh4/-1/1000"    "$FIDGEN" -l c99 "LpCh4/-1/1000"
check "Ordnung 8        LpBu8/1000"       "$FIDGEN" -l c99 "LpBu8/1000"
check "Ordnung 1        LpBu1/1000"       "$FIDGEN" -l c99 "LpBu1/1000"

# ── Optionen ──────────────────────────────────────────────────────────────────
echo ""
echo "-- Optionen"
check "Sample-Rate -r 48000"             "$FIDGEN" -l c99 -r 48000  "LpBu4/1000"
check "Custom name -n my_filter"         "$FIDGEN" -l c99 -n my_filter "LpBu4/1000"
check "--no-guard"                       "$FIDGEN" -l c99 --no-guard "LpBu4/1000"
check "--fpga-bits 16"                   "$FIDGEN" -l verilog --fpga-bits 16 "LpBu4/1000"
check "--fpga-bits 32 --fpga-frac 20"   "$FIDGEN" -l verilog --fpga-bits 32 --fpga-frac 20 "LpBu4/1000"

# ── SIMD ──────────────────────────────────────────────────────────────────────
echo ""
echo "-- SIMD"
check "simd none"   "$FIDGEN" -l c99 --simd none "LpBu4/1000"
check "simd neon"   "$FIDGEN" -l c99 --simd neon "LpBu4/1000"
check "simd sse2"   "$FIDGEN" -l c99 --simd sse2 "LpBu4/1000"
check "simd avx2"   "$FIDGEN" -l c99 --simd avx2 "LpBu4/1000"
check "simd auto"   "$FIDGEN" -l c99 --simd auto "LpBu4/1000"

check_output "simd neon enthält __ARM_NEON"  "__ARM_NEON" "$FIDGEN" -l c99 --simd neon "LpBu4/1000"
check_output "simd sse2 enthält __SSE2__"    "__SSE2__"   "$FIDGEN" -l c99 --simd sse2 "LpBu4/1000"
check_output "simd avx2 enthält __AVX2__"    "__AVX2__"   "$FIDGEN" -l c99 --simd avx2 "LpBu4/1000"

# ── Dateiausgabe ──────────────────────────────────────────────────────────────
echo ""
echo "-- Dateiausgabe"
TMP=$(mktemp /tmp/fidgen_test_XXXXXX.h)
check "-o Datei"  "$FIDGEN" -l c99 "LpBu4/1000" -o "$TMP"
check_output "-o Datei enthält Code" "typedef struct" cat "$TMP"
rm -f "$TMP"

# ── Ergebnis ──────────────────────────────────────────────────────────────────
echo ""
echo "=== Ergebnis: $pass OK, $fail FAIL ==="
[[ $fail -eq 0 ]]
