// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>

#include <fidgen/filter_descriptor.hpp>

#include <stdio.h>        // must precede fidlib.h (FILE* in fid_list_filters)
#include <fidlib/fidlib.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace fidgen {

// ── Free helpers ──────────────────────────────────────────────────────────────

std::string spec_to_ident(std::string_view spec)
{
    std::string out;
    out.reserve(spec.size() + 1);

    if (!spec.empty() && std::isdigit(static_cast<unsigned char>(spec[0])))
        out += 'f';

    for (unsigned char c : spec) {
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
        } else {
            if (!out.empty() && out.back() != '_')
                out += '_';
        }
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();

    return out;
}

std::string to_type_name(std::string_view ident)
{
    std::string out{ident};
    bool up_next = true;
    std::string result;
    result.reserve(ident.size());
    for (char c : ident) {
        if (c == '_') {
            up_next = true;
        } else if (up_next) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            up_next = false;
        } else {
            result += c;
        }
    }
    return result;
}

std::string to_upper(std::string_view ident)
{
    std::string out{ident};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string fmt_double(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

// ── FilterDescriptor::from_spec ───────────────────────────────────────────────

FilterDescriptor FilterDescriptor::from_spec(
    std::string_view spec_sv,
    double rate,
    double freq0,
    double freq1,
    std::string_view name_sv)
{
    const std::string spec{spec_sv};

    // ── design filter — RAII ownership ──
    struct FidFree { void operator()(FidFilter* p) const noexcept { std::free(p); } };
    std::unique_ptr<FidFilter, FidFree> ff{
        fid_design(spec.c_str(), rate, freq0, freq1, 0, nullptr)
    };
    if (!ff)
        throw std::runtime_error("fid_design failed for spec '" + spec + "'");

    double gain = 1.0;
    std::vector<Stage>  stages;
    std::vector<double> taps;
    int current = -1;  // index into stages of the open (not-yet-closed) stage

    // ── Detect filter kind ────────────────────────────────────────────────────
    // Pure FIR (windowed: LpBl/LpHm/LpHn/LpBa) has no 'I' nodes at all.
    bool has_iir_nodes = false;
    for (FidFilter* p = ff.get(); p->typ != 0; p = FFNEXT(p))
        if (p->typ == 'I') { has_iir_nodes = true; break; }

    if (!has_iir_nodes) {
        // ── Pure FIR path ─────────────────────────────────────────────────────
        for (FidFilter* p = ff.get(); p->typ != 0; p = FFNEXT(p)) {
            if (p->typ != 'F') continue;
            if (p->len == 1 && taps.empty()) {
                gain *= p->val[0];          // leading scalar gain (rare)
            } else {
                for (int k = 0; k < p->len; ++k)
                    taps.push_back(p->val[k]);
            }
        }
    } else {
        // ── IIR / SOS path ────────────────────────────────────────────────────
        for (FidFilter* f = ff.get(); f->typ != 0; f = FFNEXT(f)) {

            if (f->typ == 'F' && f->len == 1 && current < 0) {
                // Leading gain node
                gain *= f->val[0];

            } else if (f->typ == 'I') {
                // New IIR (denominator) stage
                Stage s;
                const double a0 = (f->len >= 1 && f->val[0] != 0.0) ? f->val[0] : 1.0;
                s.a[0] = 1.0;
                s.a[1] = (f->len >= 2) ? f->val[1] / a0 : 0.0;
                s.a[2] = (f->len >= 3) ? f->val[2] / a0 : 0.0;
                s.order = (f->len >= 3) ? 2 : 1;
                s.b_norm = a0;  // remember denominator scale for numerator normalization
                stages.push_back(std::move(s));
                current = static_cast<int>(stages.size()) - 1;

            } else if (f->typ == 'F') {
                // FIR (numerator) coefficients for current biquad stage.
                // Must be normalized by the same a0 as the denominator so that
                // both sides share the same scale (Direct Form II assumption).
                if (current < 0) {
                    stages.push_back(Stage{});
                    stages.back().a[0] = 1.0;
                    stages.back().b_norm = 1.0;
                    current = static_cast<int>(stages.size()) - 1;
                }
                Stage& s = stages[static_cast<std::size_t>(current)];
                const double bn = (s.b_norm != 0.0) ? s.b_norm : 1.0;
                s.b[0] = (f->len >= 1) ? f->val[0] / bn : 0.0;
                s.b[1] = (f->len >= 2) ? f->val[1] / bn : 0.0;
                s.b[2] = (f->len >= 3) ? f->val[2] / bn : 0.0;
                if (s.order == 0)
                    s.order = (f->len >= 3) ? 2 : 1;
                current = -1;  // stage complete
            }
        }
    }

    std::string func_name = name_sv.empty()
        ? spec_to_ident(spec_sv)
        : std::string{name_sv};

    return FilterDescriptor(spec, std::move(func_name),
                             gain, rate, freq0, freq1,
                             std::move(stages), std::move(taps));
}

// ── FilterDescriptor methods ──────────────────────────────────────────────────

bool FilterDescriptor::is_stable() const noexcept
{
    if (!taps_.empty()) return true;  // FIR: all poles at origin, always stable
    for (const auto& s : stages_) {
        const double a1 = s.a[1], a2 = s.a[2];
        if (s.order == 1) {
            if (std::fabs(a1) >= 1.0) return false;
        } else {
            if (std::fabs(a2) >= 1.0)      return false;
            if (std::fabs(a1) >= 1.0 + a2) return false;
        }
    }
    return true;
}

int FilterDescriptor::n_slots() const noexcept
{
    if (!taps_.empty()) {
        const int n = static_cast<int>(taps_.size());
        return (n > 1) ? (n - 1) : 1;
    }
    int total = 0;
    for (const auto& s : stages_)
        total += (s.order >= 2) ? 2 : 1;
    return (total > 0) ? total : 1;
}

std::string FilterDescriptor::summary() const
{
    std::ostringstream oss;
    oss << "Spec:    " << spec_    << "\n"
        << "Rate:    " << rate_    << " Hz\n"
        << "Name:    " << func_name_ << "\n";

    if (!taps_.empty()) {
        oss << "Type:    FIR (windowed tapped delay line)\n"
            << "Taps:    " << n_taps() << "\n"
            << "Gain:    " << fmt_double(gain_) << "\n"
            << "Stable:  yes (FIR)\n";
        oss << "\nTap values (h[0] = current sample):\n";
        for (int k = 0; k < n_taps(); ++k)
            oss << "  h[" << k << "] = " << fmt_double(taps_[static_cast<std::size_t>(k)]) << "\n";
    } else {
        oss << "Stages:  " << n_stages() << "\n"
            << "Gain:    " << fmt_double(gain_) << "\n"
            << "Stable:  " << (is_stable() ? "yes" : "NO (poles outside unit circle)") << "\n";
        for (int i = 0; i < n_stages(); ++i) {
            const auto& s = stages_[static_cast<std::size_t>(i)];
            oss << "\nStage " << i << " (order " << s.order << "):\n"
                << "  b: [" << s.b[0] << ", " << s.b[1] << ", " << s.b[2] << "]\n"
                << "  a: [1.0, " << s.a[1] << ", " << s.a[2] << "]\n";
        }
    }
    return oss.str();
}

} // namespace fidgen
