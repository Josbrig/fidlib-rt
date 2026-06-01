// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "param_panel.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fiview2::panels {

static const char* k_family_names[] = {
    "Butterworth", "Bessel", "Chebyshev",
    "FIR Hann", "FIR Hamming", "FIR Blackman", "FIR Bartlett",
    "Peaking EQ", "Allpass Biquad", "BP Resonator"
};
static const char* k_passband_names[] = {"Low-pass", "High-pass", "Band-pass", "Band-stop"};

// Slider with adjacent input field for double values.
// Returns true if the value changed.
static bool param_double(const char* id, const char* label,
                          double& val, double lo, double hi,
                          const char* slider_fmt, const char* input_fmt,
                          bool logarithmic = false)
{
    bool ch = false;
    float avail = ImGui::GetContentRegionAvail().x;
    float input_w = 75.0f;
    float slider_w = std::max(60.0f, avail - input_w - 6.0f);

    ImGui::TextUnformatted(label);

    char sid[64], iid[64];
    std::snprintf(sid, sizeof(sid), "##s_%s", id);
    std::snprintf(iid, sizeof(iid), "##i_%s", id);

    ImGui::SetNextItemWidth(slider_w);
    ImGuiSliderFlags flags = logarithmic ? ImGuiSliderFlags_Logarithmic : 0;
    if (ImGui::SliderScalar(sid, ImGuiDataType_Double, &val, &lo, &hi, slider_fmt, flags)) {
        val = std::clamp(val, lo, hi);
        ch = true;
    }
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputDouble(iid, &val, 0.0, 0.0, input_fmt);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        val = std::clamp(val, lo, hi);
        ch = true;
    }
    return ch;
}

void draw_param_panel(FilterState& state)
{
    if (!ImGui::Begin("Parameters")) { ImGui::End(); return; }

    FilterParams p = state.params();
    bool changed = false;

    // ── Sample rate ──────────────────────────────────────────────────────────
    ImGui::SeparatorText("Sample rate");
    static const double k_rates[]       = {8000,22050,44100,48000,96000,192000,250,1000,10000};
    static const char*  k_rate_labels[] = {"8k","22.05k","44.1k","48k","96k","192k","250","1k","10k"};
    for (int i = 0; i < 9; ++i) {
        if (i > 0) ImGui::SameLine();
        bool sel = (std::fabs(p.rate - k_rates[i]) < 1.0);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                     ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(k_rate_labels[i])) {
                p.rate = k_rates[i];
                double new_nyq = p.rate * 0.5;
                p.fc1 = std::clamp(p.fc1, 1.0, new_nyq - 1.0);
                p.fc2 = std::clamp(p.fc2, p.fc1 + 0.01, new_nyq - 1.0);
                changed = true;
            }
        if (sel) ImGui::PopStyleColor();
    }
    {
        double r = p.rate;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputDouble("##rate", &r, 0, 0, "%.2f Hz");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            p.rate = std::clamp(r, 100.0, 384000.0);
            double new_nyq = p.rate * 0.5;
            p.fc1 = std::clamp(p.fc1, 1.0, new_nyq - 1.0);
            p.fc2 = std::clamp(p.fc2, p.fc1 + 0.01, new_nyq - 1.0);
            changed = true;
        }
    }

    // ── Filter family & passband ─────────────────────────────────────────────
    ImGui::SeparatorText("Filter type");
    int fam = static_cast<int>(p.family);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::Combo("##family", &fam, k_family_names, 10)) {
        p.family = static_cast<FilterFamily>(fam);
        changed = true;
    }

    bool is_fir     = (fam >= 3 && fam <= 6);
    bool is_special = (fam >= 7);

    if (!is_fir && !is_special) {
        int pb = static_cast<int>(p.passband);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##passband", &pb, k_passband_names, 4)) {
            p.passband = static_cast<FilterPassband>(pb);
            changed = true;
        }
    }

    // ── Order ────────────────────────────────────────────────────────────────
    bool has_order = !is_fir && p.family != FilterFamily::BandpassResonator;
    if (has_order) {
        ImGui::SeparatorText("Order");
        float avail = ImGui::GetContentRegionAvail().x;
        float input_w = 40.0f;
        float slider_w = std::max(60.0f, avail - input_w - 6.0f);
        int max_order = (p.family == FilterFamily::Bessel) ? 10 : 20;
        int ord = p.order;
        ImGui::SetNextItemWidth(slider_w);
        if (ImGui::SliderInt("##order", &ord, 1, max_order)) {
            p.order = ord; changed = true;
        }
        ImGui::SameLine(0, 4);
        ImGui::SetNextItemWidth(input_w);
        ImGui::InputInt("##ordi", &ord, 0, 0);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            p.order = std::clamp(ord, 1, max_order); changed = true;
        }
    }

    // ── Frequencies ──────────────────────────────────────────────────────────
    ImGui::SeparatorText("Frequency");
    double nyq = p.rate * 0.5;

    bool bp_or_bs = !is_fir && !is_special &&
                    (p.passband == FilterPassband::BP || p.passband == FilterPassband::BS);

    // fc1 label depends on context
    const char* fc1_label =
        is_special                              ? "Center frequency (Hz)" :
        is_fir                                  ? "Cutoff (Hz)" :
        bp_or_bs                                ? "Lower cutoff (Hz)" :
        (p.passband == FilterPassband::LP ||
         p.passband == FilterPassband::HP)      ? "Cutoff (Hz)" : "Cutoff (Hz)";

    {
        double fc = p.fc1;
        if (param_double("fc1", fc1_label, fc, 1.0, nyq - 1.0,
                         "%.1f", "%.4f", true)) {
            p.fc1 = std::clamp(fc, 1.0, nyq - 1.0);
            if (bp_or_bs)
                p.fc2 = std::max(p.fc2, p.fc1 + 1.0);  // keep fc2 above fc1
            changed = true;
        }
    }

    if (bp_or_bs) {
        double fc2 = p.fc2;
        if (param_double("fc2", "Upper cutoff (Hz)", fc2,
                         p.fc1 + 0.01, nyq - 1.0, "%.1f", "%.4f", true)) {
            p.fc2 = std::clamp(fc2, p.fc1 + 0.01, nyq - 1.0);
            changed = true;
        }
    }

    // ── Chebyshev ripple ─────────────────────────────────────────────────────
    if (p.family == FilterFamily::Chebyshev) {
        ImGui::SeparatorText("Chebyshev");
        double rip = p.ripple_db;
        if (param_double("ripple", "Passband ripple (dB)",
                         rip, -60.0, -0.001, "%.2f", "%.4f")) {
            p.ripple_db = rip;
            changed = true;
        }
    }

    // ── Q / Gain ─────────────────────────────────────────────────────────────
    if (p.family == FilterFamily::PeakingEQ ||
        p.family == FilterFamily::AllpassBiquad ||
        p.family == FilterFamily::BandpassResonator) {
        ImGui::SeparatorText("Biquad");
        double q = p.q_factor;
        if (param_double("q", "Q factor", q, 0.01, 100.0,
                         "%.3f", "%.6f", true)) {
            p.q_factor = q;
            changed = true;
        }
    }
    if (p.family == FilterFamily::PeakingEQ) {
        double g = p.gain_db;
        if (param_double("gain", "Gain (dB)", g, -40.0, 40.0,
                         "%.2f", "%.4f")) {
            p.gain_db = g;
            changed = true;
        }
    }

    // ── Spec ─────────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Spec (fidlib)");

    // Direkteingabe
    static char raw_buf[256] = {};
    bool using_raw = state.using_raw_spec();

    // Sync buffer when not in raw mode
    if (!using_raw) {
        std::string computed = FilterState::build_spec(p);
        std::strncpy(raw_buf, computed.c_str(), sizeof(raw_buf) - 1);
    }

    float avail_spec = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(avail_spec - 60.0f);
    if (using_raw)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.18f, 0.05f, 1.0f));

    bool spec_entered = ImGui::InputText("##rawspec", raw_buf, sizeof(raw_buf),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (using_raw) ImGui::PopStyleColor();

    if (spec_entered && raw_buf[0] != '\0') {
        state.set_raw_spec(raw_buf);
        changed = false;  // set_raw_spec ruft update() selbst auf
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Copy"))
        ImGui::SetClipboardText(raw_buf);
    ImGui::SameLine();
    if (using_raw) {
        if (ImGui::SmallButton("Params"))
            state.clear_raw_spec();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Back to parameter-based mode");
    }

    if (using_raw)
        ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "Direktmodus (Enter = anwenden)");
    else
        ImGui::TextDisabled("Enter = Direktmodus aktivieren");

    if (changed) {
        state.clear_raw_spec();   // Parameter change exits raw mode
        state.params() = p;
        state.update();
    }

    ImGui::End();
}

} // namespace fiview2::panels
