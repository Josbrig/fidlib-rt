// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "impulse_panel.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace fiview2::panels {

static void draw_signal(ImDrawList* dl, ImVec2 p0, float w, float h,
                        const std::vector<double>& sig,
                        int view_n, ImU32 col)
{
    if (sig.empty() || view_n < 2) return;
    double mn = *std::min_element(sig.begin(), sig.begin() + view_n);
    double mx = *std::max_element(sig.begin(), sig.begin() + view_n);
    if (mx - mn < 1e-30) mx = mn + 1.0;
    double range = mx - mn;

    for (int i = 1; i < view_n; ++i) {
        float x1 = p0.x + static_cast<float>(i - 1) / static_cast<float>(view_n - 1) * w;
        float x2 = p0.x + static_cast<float>(i)     / static_cast<float>(view_n - 1) * w;
        float y1 = p0.y + h - (float)((sig[static_cast<size_t>(i-1)] - mn) / range) * h;
        float y2 = p0.y + h - (float)((sig[static_cast<size_t>(i  )] - mn) / range) * h;
        dl->AddLine({x1,y1},{x2,y2}, col, 1.5f);
    }
    // zero line
    if (mn < 0.0 && mx > 0.0) {
        float y0 = p0.y + h - (float)((0.0 - mn) / range) * h;
        dl->AddLine({p0.x,y0},{p0.x+w,y0}, IM_COL32(100,100,100,100));
    }
}

void draw_impulse_panel(FilterState& state)
{
    if (!ImGui::Begin("Impulse / Step")) { ImGui::End(); return; }

    static bool   show_step  = false;
    static bool   log_amp    = false;
    static int    view_n     = 256;
    static char   export_path[256] = "impulse_response.txt";

    ImGui::Checkbox("Step response", &show_step);
    ImGui::SameLine(); ImGui::Checkbox("Log amplitude", &log_amp);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderInt("Samples", &view_n, 16, 1024);

    const auto& res = state.result();
    if (!res.valid) {
        ImGui::TextColored({1,0.4f,0.4f,1}, "No valid filter");
        ImGui::End(); return;
    }

    const auto& sig = show_step ? res.step : res.impulse;
    int n = std::min(view_n, static_cast<int>(sig.size()));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float w = avail.x - 4.0f;
    float h = std::max(avail.y - 50.0f, 40.0f);

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, {p0.x+w, p0.y+h}, IM_COL32(18,18,25,255));
    dl->AddRect      (p0, {p0.x+w, p0.y+h}, IM_COL32(80,80,80,200));

    draw_signal(dl, p0, w, h, sig, n,
                show_step ? IM_COL32(100,200,255,230) : IM_COL32(100,255,150,230));

    // Y-axis label
    if (!sig.empty()) {
        double peak = *std::max_element(sig.begin(), sig.begin() + n,
            [](double a, double b){ return std::fabs(a) < std::fabs(b); });
        dl->AddText({p0.x+2, p0.y+2}, IM_COL32(160,160,160,200),
            show_step ? "step" : "impulse");
        char buf[32]; std::snprintf(buf,sizeof(buf),"peak=%.3g", peak);
        dl->AddText({p0.x+2, p0.y+h-14}, IM_COL32(160,160,160,200), buf);
    }

    ImGui::InvisibleButton("##ir", {w,h});

    ImGui::Separator();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##path", export_path, sizeof(export_path));
    ImGui::SameLine();
    if (ImGui::Button("Export ASCII")) {
        std::ofstream f(export_path);
        if (f) {
            f << "# " << (show_step ? "Step" : "Impulse")
              << " response: " << res.spec_str << "\n";
            f << "# sample value\n";
            for (int i = 0; i < n; ++i)
                f << i << " " << sig[static_cast<size_t>(i)] << "\n";
        }
    }

    ImGui::End();
}

} // namespace fiview2::panels
