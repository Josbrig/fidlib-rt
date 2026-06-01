// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#pragma once

#include <array>
#include <complex>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
struct FidFilter;

namespace fiview2 {

// ── FilterType ────────────────────────────────────────────────────────────────

enum class FilterFamily {
    Butterworth, Bessel, Chebyshev, FIR_Hann, FIR_Hamming, FIR_Blackman, FIR_Bartlett,
    PeakingEQ, AllpassBiquad, BandpassResonator
};

enum class FilterPassband { LP, HP, BP, BS };

struct FilterParams {
    FilterFamily  family    = FilterFamily::Butterworth;
    FilterPassband passband = FilterPassband::LP;
    int           order     = 4;
    double        rate      = 44100.0;
    double        fc1       = 1000.0;   // Hz
    double        fc2       = 2000.0;   // Hz (BP/BS only)
    double        ripple_db = -1.0;     // Chebyshev only
    double        q_factor  = 1.0;      // PeakingEQ / Resonator
    double        gain_db   = 6.0;      // PeakingEQ only

    bool operator==(const FilterParams&) const = default;
};

// ── Computed filter data ──────────────────────────────────────────────────────

struct FreqPoint {
    double freq_hz;
    double magnitude_db;
    double phase_deg;
    double group_delay_samples;
};

struct PoleZero {
    std::complex<double> value;
    bool is_pole;  // false = zero
    int  multiplicity;
};

struct FilterResult {
    bool        valid    = false;
    std::string spec_str;
    std::string error_msg;

    // Frequency response (log-spaced from 1 Hz to Nyquist)
    std::vector<FreqPoint> freq_response;

    // Pole-zero data
    std::vector<PoleZero> poles_zeros;

    // Impulse response
    std::vector<double> impulse;

    // Step response
    std::vector<double> step;

    // fidlib internal (for audio playback)
    std::unique_ptr<FidFilter, void(*)(FidFilter*)> ff{nullptr, nullptr};
};

// ── FilterSlot — one filter in the cascade ───────────────────────────────────

struct FilterSlot {
    FilterParams params;
    bool         enabled  = true;
    std::string  label;             // user-visible name
    FilterResult result;
};

// ── CompareSlot — frozen A/B/C/D snapshots ────────────────────────────────────

struct CompareSlot {
    FilterParams           params;
    std::vector<FreqPoint> freq_response;
    std::vector<PoleZero>  poles_zeros;
    std::string            label;
    bool                   active = false;
    float                  color[4]{1.0f, 1.0f, 0.0f, 1.0f};
};

// ── FilterState — application-wide shared state ──────────────────────────────

class FilterState {
public:
    FilterState();

    // Active design (slot 0 = primary)
    FilterParams& params()       { return slots_[active_slot_].params; }
    const FilterParams& params() const { return slots_[active_slot_].params; }
    const FilterResult& result() const { return slots_[active_slot_].result; }
    const std::string& spec()    const { return slots_[active_slot_].result.spec_str; }

    // Cascade
    std::vector<FilterSlot>& slots()       { return slots_; }
    const std::vector<FilterSlot>& slots() const { return slots_; }
    int  active_slot() const { return active_slot_; }
    void set_active_slot(int i);
    void add_slot();
    void remove_slot(int i);

    // Compare A/B/C/D
    std::array<CompareSlot, 4>& compare() { return compare_; }
    const std::array<CompareSlot, 4>& compare() const { return compare_; }
    void freeze_to_compare(int slot_idx);  // 0=A, 1=B, 2=C, 3=D

    // Recompute if params changed
    void update();

    // Rebuild cascade combined frequency response
    const std::vector<FreqPoint>& cascade_response() const { return cascade_response_; }

    // Change notification
    using ChangeCallback = std::function<void()>;
    void on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    // Build fidlib spec string from params
    static std::string build_spec(const FilterParams& p);

    // Direct spec input: bypasses param-based build_spec for active slot.
    // spec="" clears raw override and returns to param-based mode.
    void set_raw_spec(std::string spec);
    void clear_raw_spec();
    bool using_raw_spec() const { return !raw_spec_.empty(); }
    const std::string& raw_spec() const { return raw_spec_; }

    // Nyquist
    double nyquist() const { return params().rate * 0.5; }

private:
    void recompute_slot(FilterSlot& slot);
    void recompute_cascade();
    void notify();

    std::vector<FilterSlot>         slots_;
    int                             active_slot_ = 0;
    std::string                     raw_spec_;          // leer = param-basiert
    std::array<CompareSlot, 4>      compare_;
    std::vector<FreqPoint>          cascade_response_;
    ChangeCallback                  on_change_;
};

} // namespace fiview2
