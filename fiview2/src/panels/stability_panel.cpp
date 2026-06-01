// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "stability_panel.hpp"
#include "imgui.h"
#include <cmath>
#include <cstdio>

namespace fiview2::panels {

// ── Stability panel (Phase 5.4) ───────────────────────────────────────────────

void draw_stability_panel(FilterState& state)
{
    if (!ImGui::Begin("Stability")) { ImGui::End(); return; }

    const auto& res = state.result();
    auto& p = state.params();

    if (!res.valid) {
        ImGui::TextColored({1,0.4f,0.4f,1}, "No valid filter to analyse.");
        ImGui::End(); return;
    }

    // Check stability from poles
    bool stable = true;
    double max_pole_mag = 0.0;
    for (const auto& pz : res.poles_zeros) {
        if (!pz.is_pole) continue;
        double m = std::abs(pz.value);
        if (m >= 1.0) stable = false;
        max_pole_mag = std::max(max_pole_mag, m);
    }

    // Stability indicator
    if (stable) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f,1.0f,0.2f,1.0f));
        ImGui::Text("✓ STABLE");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.2f,0.2f,1.0f));
        ImGui::Text("✗ UNSTABLE");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();

    // Stability margin
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Max pole magnitude: %.4f", max_pole_mag);
    ImGui::Text("%s", buf);

    if (max_pole_mag > 0.0) {
        float margin = static_cast<float>(1.0 - max_pole_mag);
        ImGui::Text("Stability margin: %.4f", static_cast<double>(margin));

        // Progress bar: 0 = unstable, 1 = very stable
        float bar = std::clamp(static_cast<float>(1.0 - max_pole_mag), 0.0f, 1.0f);
        ImVec4 bar_col = (bar > 0.1f) ? ImVec4(0.2f,0.8f,0.2f,1.0f)
                                       : ImVec4(0.9f,0.3f,0.1f,1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_col);
        ImGui::ProgressBar(bar, {-1, 8}, "");
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // FP32 quantisation warning for high-order filters
    int ord = p.order;
    if (ord >= 8) {
        ImGui::TextColored({1.0f,0.8f,0.0f,1.0f},
            "⚠ Order %d: floating-point\n"
            "  quantisation may cause\n"
            "  instability in fixed-point.", ord);
    }

    // Auto-suggestion: reduce order if unstable
    if (!stable && ord > 2) {
        ImGui::Spacing();
        ImGui::TextColored({1.0f,0.7f,0.2f,1.0f},
            "Suggestion: reduce order to stabilise.");
        if (ImGui::Button("Order - 2")) {
            p.order = std::max(1, p.order - 2);
            state.update();
        }
    }

    ImGui::End();
}

// ── Guided mode (Phase 5.3) ───────────────────────────────────────────────────

struct GuidedState {
    int  step         = 0;   // 0=closed, 1=use-case, 2=what_bothers, 3=done
    int  use_case     = -1;  // 0=audio, 1=sensor, 2=eeg, 3=custom
    int  what_bothers = -1;  // 0=hf_noise, 1=dc_drift, 2=mains_hum, 3=vibration
    bool shown        = false;
};

static GuidedState g_gs;

void draw_guided_mode(FilterState& state)
{
    if (!g_gs.shown) {
        if (!ImGui::Begin("Guided Mode (Wizard)")) { ImGui::End(); return; }
        if (ImGui::Button("Open Wizard")) g_gs.step = 1;
        ImGui::End();
        return;
    }

    if (g_gs.step == 0) return;

    ImGui::SetNextWindowSize({380, 260}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, {0.5f, 0.5f});
    if (!ImGui::Begin("Filter Wizard", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
    { ImGui::End(); return; }

    ImGui::TextDisabled("Step %d / 2", g_gs.step);
    ImGui::Separator();

    if (g_gs.step == 1) {
        ImGui::Text("What are you filtering?");
        ImGui::Spacing();
        static const char* k_uc[] = {"Audio signal","Sensor data","EEG / biosignal","Custom"};
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(k_uc[i], g_gs.use_case == i))
                g_gs.use_case = i;
        }
        ImGui::Spacing();
        if (g_gs.use_case >= 0 && ImGui::Button("Next →")) g_gs.step = 2;
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { g_gs.step = 0; g_gs.shown = false; }
    }
    else if (g_gs.step == 2) {
        ImGui::Text("What are you trying to remove?");
        ImGui::Spacing();
        static const char* k_wb[] = {
            "High-frequency noise","DC drift / offset",
            "Mains hum (50/60 Hz)","Vibration / low-freq rumble"
        };
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(k_wb[i], g_gs.what_bothers == i))
                g_gs.what_bothers = i;
        }
        ImGui::Spacing();
        if (ImGui::Button("← Back")) g_gs.step = 1;
        ImGui::SameLine();
        if (g_gs.what_bothers >= 0 && ImGui::Button("Apply")) {
            // Build a sensible default based on selections
            auto& p = state.params();
            double rate = (g_gs.use_case == 0) ? 44100.0
                        : (g_gs.use_case == 2) ? 1000.0
                        :                        8000.0;
            p.rate = rate;
            if (g_gs.what_bothers == 0) {
                // LP Butterworth 4th order, fc = rate/8
                p.family   = FilterFamily::Butterworth;
                p.passband = FilterPassband::LP;
                p.order    = 4;
                p.fc1      = rate / 8.0;
            } else if (g_gs.what_bothers == 1) {
                // HP Butterworth 2nd order, fc = 0.5 Hz
                p.family   = FilterFamily::Butterworth;
                p.passband = FilterPassband::HP;
                p.order    = 2;
                p.fc1      = 0.5;
            } else if (g_gs.what_bothers == 2) {
                // BS Butterworth 2nd order around 50/60 Hz
                p.family   = FilterFamily::Butterworth;
                p.passband = FilterPassband::BS;
                p.order    = 2;
                p.fc1      = 48.0;
                p.fc2      = 52.0;
            } else {
                // HP Butterworth 2nd order, 5 Hz
                p.family   = FilterFamily::Butterworth;
                p.passband = FilterPassband::HP;
                p.order    = 2;
                p.fc1      = 5.0;
            }
            state.update();
            g_gs.step  = 0;
            g_gs.shown = false;
        }
    }

    ImGui::End();
}

} // namespace fiview2::panels
