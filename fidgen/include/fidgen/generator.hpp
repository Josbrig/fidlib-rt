// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
#pragma once

#include "filter_descriptor.hpp"
#include <iosfwd>
#include <memory>
#include <string_view>

namespace fidgen {

// ── SIMD level for generated C/C++ output ─────────────────────────────────────
enum class SimdLevel {
    None,   // scalar only
    Neon,   // ARM NEON float64x2_t  (2-channel parallel)
    Sse2,   // x86 SSE2  __m128d     (2-channel parallel)
    Avx2,   // x86 AVX2  __m256d     (4-channel parallel)
    Auto,   // emit all variants guarded by #ifdef
};

// ── GeneratorOptions ──────────────────────────────────────────────────────────
struct GeneratorOptions {
    bool      with_guard  = true;    // wrap in #ifndef/#define/#endif
    bool      with_test   = false;   // append self-test code
    SimdLevel simd        = SimdLevel::None;
    int       fpga_bits   = 24;      // Verilog data+coef bit width
    int       fpga_frac   = -1;      // -1 = auto-compute from coef range
};

// ── Generator ─────────────────────────────────────────────────────────────────
//
// Abstract base for all language backends.
//
// Usage:
//   auto gen = Generator::create("c99");
//   gen->generate(std::cout, desc, opts);
//
class Generator {
public:
    virtual ~Generator() = default;

    // Write the complete generated code to out.
    virtual void generate(std::ostream& out,
                          const FilterDescriptor& f,
                          const GeneratorOptions& opts) const = 0;

    // Factory: returns the appropriate Generator for the given language key.
    // Supported keys: "c99", "cpp20", "python" / "py", "rust" / "rs",
    //                 "matlab" / "octave" / "m", "julia" / "jl",
    //                 "verilog" / "v", "systemverilog" / "sv"
    // Throws std::invalid_argument for unknown keys.
    [[nodiscard]] static std::unique_ptr<Generator> create(std::string_view lang);

    // List all supported language keys, one per line, to out.
    static void list_langs(std::ostream& out);
};

} // namespace fidgen
