// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "filter_state.hpp"

#include <stdio.h>
#include "../fidlib/fidlib.h"
#include <fidgen/filter_descriptor.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <sstream>

namespace fiview2 {

static constexpr int   N_FREQ  = 512;   // frequency response points
static constexpr int   N_RESP  = 1024;  // impulse/step response length

// ── spec builder ─────────────────────────────────────────────────────────────

std::string FilterState::build_spec(const FilterParams& p)
{
    std::string fam;
    bool is_fir = false;

    switch (p.family) {
        case FilterFamily::Butterworth:      fam = "Bu"; break;
        case FilterFamily::Bessel:           fam = "Be"; break;
        case FilterFamily::Chebyshev:        fam = "Ch"; break;
        case FilterFamily::FIR_Hann:         fam = "Hm"; is_fir = true; break;
        case FilterFamily::FIR_Hamming:      fam = "Hn"; is_fir = true; break;
        case FilterFamily::FIR_Blackman:     fam = "Bl"; is_fir = true; break;
        case FilterFamily::FIR_Bartlett:     fam = "Ba"; is_fir = true; break;
        case FilterFamily::PeakingEQ:        fam = "Bq"; break;
        case FilterFamily::AllpassBiquad:    fam = "Bq"; break;
        case FilterFamily::BandpassResonator:fam = "Re"; break;
    }

    std::string pb;
    if (!is_fir && p.family != FilterFamily::PeakingEQ &&
        p.family != FilterFamily::AllpassBiquad &&
        p.family != FilterFamily::BandpassResonator) {
        switch (p.passband) {
            case FilterPassband::LP: pb = "Lp"; break;
            case FilterPassband::HP: pb = "Hp"; break;
            case FilterPassband::BP: pb = "Bp"; break;
            case FilterPassband::BS: pb = "Bs"; break;
        }
    } else if (is_fir) {
        pb = "Lp";  // windowed FIR only LP
    } else if (p.family == FilterFamily::PeakingEQ) {
        pb = "Pk";
    } else if (p.family == FilterFamily::AllpassBiquad) {
        pb = "Ap";
    } else {
        pb = "Bp";  // BandpassResonator
    }

    std::ostringstream s;

    // Frequency format: up to 6 significant digits, no unnecessary .0
    auto fmt_freq = [](double f) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", f);
        return buf;
    };

    if (p.family == FilterFamily::FIR_Hann || p.family == FilterFamily::FIR_Hamming ||
        p.family == FilterFamily::FIR_Blackman || p.family == FilterFamily::FIR_Bartlett) {
        s << pb << fam << "/" << fmt_freq(p.fc1);
    } else if (p.family == FilterFamily::PeakingEQ) {
        s << pb << fam << p.order << "/" << fmt_freq(p.fc1)
          << "/" << p.q_factor << "/" << p.gain_db;
    } else if (p.family == FilterFamily::AllpassBiquad) {
        s << pb << fam << p.order << "/" << p.q_factor << "/" << fmt_freq(p.fc1);
    } else if (p.family == FilterFamily::BandpassResonator) {
        s << pb << fam << "/" << p.q_factor << "/" << fmt_freq(p.fc1);
    } else if (p.family == FilterFamily::Chebyshev) {
        s << pb << fam << p.order << "/" << p.ripple_db << "/" << fmt_freq(p.fc1);
        if (p.passband == FilterPassband::BP || p.passband == FilterPassband::BS)
            s << "-" << fmt_freq(p.fc2);
    } else {
        s << pb << fam << p.order << "/" << fmt_freq(p.fc1);
        if (p.passband == FilterPassband::BP || p.passband == FilterPassband::BS)
            s << "-" << fmt_freq(p.fc2);
    }

    return s.str();
}

void FilterState::set_raw_spec(std::string spec)
{
    raw_spec_ = std::move(spec);
    update();
}

void FilterState::clear_raw_spec()
{
    raw_spec_.clear();
    update();
}

// ── recompute one slot ────────────────────────────────────────────────────────

void FilterState::recompute_slot(FilterSlot& slot)
{
    FilterResult& r = slot.result;
    r.valid     = false;
    r.error_msg.clear();
    r.freq_response.clear();
    r.poles_zeros.clear();
    r.impulse.clear();
    r.step.clear();
    r.ff = {nullptr, nullptr};

    const FilterParams& p = slot.params;
    // raw_spec_ overrides build_spec when set (active slot only)
    r.spec_str = (!raw_spec_.empty() && &slot == &slots_[static_cast<size_t>(active_slot_)])
                 ? raw_spec_ : build_spec(p);

    // ── design filter ──────────────────────────────────────────────────────
    FidFilter* ff_raw = fid_design(r.spec_str.c_str(), p.rate,
                                   -1.0, -1.0, 0, nullptr);
    if (!ff_raw) {
        r.error_msg = "fid_design failed for: " + r.spec_str;
        return;
    }
    r.ff = {ff_raw, [](FidFilter* fp){ std::free(fp); }};

    // ── frequency response ─────────────────────────────────────────────────
    const double nyq = p.rate * 0.5;
    r.freq_response.reserve(N_FREQ);

    for (int k = 0; k < N_FREQ; ++k) {
        // Log-spaced from 1 Hz to Nyquist
        double freq = std::exp(std::log(1.0) +
            (double)k / (N_FREQ - 1) * std::log(nyq / 1.0));
        double phase = 0.0;
        double mag   = fid_response_pha(ff_raw, freq / p.rate, &phase);
        double mag_db = (mag > 1e-30) ? 20.0 * std::log10(mag) : -600.0;

        // Group delay: finite difference
        double eps = freq * 0.001 + 0.01;
        double ph2 = 0.0;
        fid_response_pha(ff_raw, (freq + eps) / p.rate, &ph2);
        double gd = -(ph2 - phase) / (2.0 * std::numbers::pi * eps);

        r.freq_response.push_back({freq, mag_db,
            phase * 180.0 / std::numbers::pi, gd});
    }

    // ── poles and zeros from biquad coefficients ───────────────────────────
    {
        // Use fidgen FilterDescriptor to extract biquad stages, then compute
        // roots of numerator (zeros) and denominator (poles) polynomials.
        try {
            auto desc = fidgen::FilterDescriptor::from_spec(r.spec_str, p.rate);
            auto quad_roots = [](double b, double c)
                -> std::pair<std::complex<double>, std::complex<double>>
            {
                // roots of z^2 + b*z + c = 0
                std::complex<double> disc{b*b - 4.0*c, 0.0};
                std::complex<double> sq  = std::sqrt(disc);
                return {(-b + sq) / 2.0, (-b - sq) / 2.0};
            };

            if (!desc.is_fir()) {
                for (const auto& s : desc.stages()) {
                    if (s.order >= 2) {
                        // poles: z^2 + a1*z + a2 = 0
                        auto [p1, p2] = quad_roots(s.a[1], s.a[2]);
                        r.poles_zeros.push_back({p1, true,  1});
                        r.poles_zeros.push_back({p2, true,  1});
                        // zeros: b0*z^2 + b1*z + b2 = 0  →  z^2 + (b1/b0)*z + (b2/b0)
                        if (std::fabs(s.b[0]) > 1e-30) {
                            auto [z1, z2] = quad_roots(s.b[1]/s.b[0], s.b[2]/s.b[0]);
                            r.poles_zeros.push_back({z1, false, 1});
                            r.poles_zeros.push_back({z2, false, 1});
                        }
                    } else {
                        // first-order: pole at -a1, zero at -b1/b0
                        r.poles_zeros.push_back({{-s.a[1], 0.0}, true,  1});
                        if (std::fabs(s.b[0]) > 1e-30)
                            r.poles_zeros.push_back({{-s.b[1]/s.b[0], 0.0}, false, 1});
                    }
                }
            }
        } catch (...) {
            // pole-zero display optional — ignore errors
        }
    }

    // ── impulse + step response ────────────────────────────────────────────
    {
        double (*step_fn)(void*, double) = nullptr;
        void* run = fid_run_new(ff_raw, &step_fn);
        void* buf = fid_run_newbuf(run);

        r.impulse.resize(N_RESP);
        r.step.resize(N_RESP);
        double x = 1.0;
        double acc = 0.0;
        for (int i = 0; i < N_RESP; ++i) {
            double y = step_fn(buf, x);
            r.impulse[static_cast<size_t>(i)] = y;
            acc += y;
            r.step[static_cast<size_t>(i)] = acc;
            x = 0.0;
        }

        fid_run_freebuf(buf);
        fid_run_free(run);
    }

    r.valid = true;
}

// ── cascade combined response ─────────────────────────────────────────────────

void FilterState::recompute_cascade()
{
    cascade_response_.clear();
    if (slots_.empty()) return;

    // Collect enabled slots with valid results
    std::vector<const FilterResult*> active;
    for (const auto& s : slots_)
        if (s.enabled && s.result.valid)
            active.push_back(&s.result);

    if (active.empty()) return;

    const size_t n = active[0]->freq_response.size();
    cascade_response_.resize(n);

    for (size_t k = 0; k < n; ++k) {
        double total_db = 0.0;
        double total_phase = 0.0;
        double freq = active[0]->freq_response[k].freq_hz;

        for (const auto* res : active) {
            if (k < res->freq_response.size()) {
                total_db    += res->freq_response[k].magnitude_db;
                total_phase += res->freq_response[k].phase_deg;
            }
        }
        cascade_response_[k] = {freq, total_db, total_phase, 0.0};
    }
}

// ── FilterState ───────────────────────────────────────────────────────────────

FilterState::FilterState()
{
    FilterSlot s;
    s.label = "Filter 1";
    slots_.push_back(std::move(s));
    update();
}

void FilterState::set_active_slot(int i)
{
    if (i >= 0 && i < static_cast<int>(slots_.size()))
        active_slot_ = i;
}

void FilterState::add_slot()
{
    FilterSlot s;
    s.label = "Filter " + std::to_string(slots_.size() + 1);
    slots_.push_back(std::move(s));
    recompute_slot(slots_.back());
    recompute_cascade();
    notify();
}

void FilterState::remove_slot(int i)
{
    if (slots_.size() <= 1) return;
    slots_.erase(slots_.begin() + i);
    if (active_slot_ >= static_cast<int>(slots_.size()))
        active_slot_ = static_cast<int>(slots_.size()) - 1;
    recompute_cascade();
    notify();
}

void FilterState::update()
{
    for (auto& s : slots_)
        recompute_slot(s);
    recompute_cascade();
    notify();
}

void FilterState::freeze_to_compare(int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= 4) return;
    const auto& src = slots_[static_cast<size_t>(active_slot_)];
    auto& dst = compare_[static_cast<size_t>(slot_idx)];
    dst.params        = src.params;
    dst.freq_response = src.result.freq_response;
    dst.poles_zeros   = src.result.poles_zeros;
    dst.label         = std::string("ABCD")[static_cast<size_t>(slot_idx)]
                        + std::string(": ") + src.result.spec_str;
    dst.active        = true;
    static const float cols[4][4] = {
        {1.0f, 0.4f, 0.4f, 1.0f},
        {0.4f, 1.0f, 0.4f, 1.0f},
        {0.4f, 0.4f, 1.0f, 1.0f},
        {1.0f, 1.0f, 0.4f, 1.0f},
    };
    std::copy(cols[static_cast<size_t>(slot_idx)],
              cols[static_cast<size_t>(slot_idx)] + 4, dst.color);
}

void FilterState::notify()
{
    if (on_change_) on_change_();
}

} // namespace fiview2
