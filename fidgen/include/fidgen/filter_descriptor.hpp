// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

namespace fidgen {

// ── Stage ─────────────────────────────────────────────────────────────────────
//
// One second-order (or first-order) section from the SOS cascade.
//
// Sign convention (matches fidlib execution engine):
//   w[n] = x[n] - a[1]*w[n-1] - a[2]*w[n-2]     (IIR, subtracted)
//   y[n] = b[0]*w[n] + b[1]*w[n-1] + b[2]*w[n-2] (FIR, added)
//
// a[1] may be NEGATIVE (e.g. -1.88 for a Butterworth LP pole pair).
// a[0] = 1.0 (implied, always).
//
struct Stage {
    std::array<double, 3> b{{1.0, 0.0, 0.0}};  // FIR numerator: b0, b1, b2
    std::array<double, 3> a{{1.0, 0.0, 0.0}};  // IIR denominator: 1, a1, a2
    int    order  = 2;                          // 1 = first-order, 2 = biquad
    double b_norm = 1.0;                        // denominator leading coef (for normalization)
};

// ── FilterDescriptor ──────────────────────────────────────────────────────────
//
// Complete model of a designed digital filter: SOS cascade + gain + metadata.
// Constructed via the static factory from_spec() — never by direct construction.
//
class FilterDescriptor {
public:
    // Factory: design a filter from a fidlib spec string.
    //   spec:      fispec string, e.g. "LpBu4/1000", "BpBu2/500-2000"
    //   rate:      sample rate in Hz (default: 44100 Hz)
    //   freq0/1:   corner-frequency overrides (-1.0 = use spec-embedded value)
    //   name:      C identifier prefix (empty = auto-derived from spec)
    //
    // Throws std::runtime_error on fid_design() failure or invalid spec.
    [[nodiscard]] static FilterDescriptor from_spec(
        std::string_view spec,
        double rate   = 44100.0,
        double freq0  = -1.0,
        double freq1  = -1.0,
        std::string_view name = {}
    );

    // ── Stability check ──────────────────────────────────────────────────────
    // Returns true iff all poles are strictly inside the unit circle.
    // Jury criterion for biquad:  |a2| < 1  AND  |a1| < 1 + a2
    [[nodiscard]] bool is_stable() const noexcept;

    // ── Accessors ─────────────────────────────────────────────────────────────
    [[nodiscard]] const std::string& spec()      const noexcept { return spec_; }
    [[nodiscard]] const std::string& func_name() const noexcept { return func_name_; }
    [[nodiscard]] double gain()  const noexcept { return gain_; }
    [[nodiscard]] double rate()  const noexcept { return rate_; }
    [[nodiscard]] double freq0() const noexcept { return freq0_; }
    [[nodiscard]] double freq1() const noexcept { return freq1_; }

    [[nodiscard]] const std::vector<Stage>& stages() const noexcept { return stages_; }
    [[nodiscard]] int n_stages() const noexcept {
        return static_cast<int>(stages_.size());
    }

    // Pure-FIR accessors (non-empty only for windowed FIR: LpBl, LpHm, LpHn, LpBa)
    [[nodiscard]] bool is_fir() const noexcept { return !taps_.empty(); }
    [[nodiscard]] const std::vector<double>& taps() const noexcept { return taps_; }
    [[nodiscard]] int n_taps() const noexcept {
        return static_cast<int>(taps_.size());
    }

    // Total delay-line slots (FIR: n_taps-1; IIR: 2 per biquad, 1 per first-order)
    [[nodiscard]] int n_slots() const noexcept;

    // Human-readable summary (for --check output)
    [[nodiscard]] std::string summary() const;

private:
    // Private constructor — use from_spec()
    FilterDescriptor(std::string spec, std::string func_name,
                     double gain, double rate, double freq0, double freq1,
                     std::vector<Stage> stages,
                     std::vector<double> taps = {}) noexcept
        : spec_(std::move(spec))
        , func_name_(std::move(func_name))
        , gain_(gain)
        , rate_(rate)
        , freq0_(freq0)
        , freq1_(freq1)
        , stages_(std::move(stages))
        , taps_(std::move(taps))
    {}

    std::string        spec_;
    std::string        func_name_;
    double             gain_  = 1.0;
    double             rate_  = 44100.0;
    double             freq0_ = -1.0;
    double             freq1_ = -1.0;
    std::vector<Stage>  stages_;
    std::vector<double> taps_;   // non-empty for pure FIR
};

// ── Free helpers ──────────────────────────────────────────────────────────────

// Convert a fispec string to a valid lowercase C identifier.
// Non-alphanumeric chars become '_'; runs collapse; leading digits get 'f' prefix.
[[nodiscard]] std::string spec_to_ident(std::string_view spec);

// "foo_bar_baz" → "FooBarBaz"   (CapitalCase type name)
[[nodiscard]] std::string to_type_name(std::string_view ident);

// "foo_bar" → "FOO_BAR"
[[nodiscard]] std::string to_upper(std::string_view ident);

// Format a double as a compact round-trip literal (17 significant digits)
[[nodiscard]] std::string fmt_double(double v);

} // namespace fidgen
