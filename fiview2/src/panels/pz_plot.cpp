// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "pz_plot.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace fiview2::panels {

static constexpr float k_view = 1.6f;  // ±k_view units in z-plane

static ImVec2 z_to_screen(std::complex<double> z,
                           ImVec2 centre, float scale)
{
    return {
        centre.x + static_cast<float>(z.real()) * scale,
        centre.y - static_cast<float>(z.imag()) * scale   // y flipped
    };
}

void draw_pz_plot(FilterState& state)
{
    if (!ImGui::Begin("Poles / Zeros")) { ImGui::End(); return; }

    ImVec2 avail  = ImGui::GetContentRegionAvail();
    float  side   = std::min(avail.x, avail.y) - 4.0f;
    if (side < 30.0f) { ImGui::End(); return; }

    ImVec2 p0     = ImGui::GetCursorScreenPos();
    ImVec2 centre = {p0.x + side * 0.5f, p0.y + side * 0.5f};
    float  scale  = side * 0.5f / k_view;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, {p0.x+side, p0.y+side}, IM_COL32(18,18,25,255));
    dl->AddRect      (p0, {p0.x+side, p0.y+side}, IM_COL32(80,80,80,200));

    // Axes
    dl->AddLine({p0.x, centre.y}, {p0.x+side, centre.y}, IM_COL32(80,80,80,180));
    dl->AddLine({centre.x, p0.y}, {centre.x, p0.y+side}, IM_COL32(80,80,80,180));

    // Unit circle
    {
        const int N = 128;
        for (int i = 0; i < N; ++i) {
            float a1 = 2.0f * static_cast<float>(std::numbers::pi) * static_cast<float>(i)     / static_cast<float>(N);
            float a2 = 2.0f * static_cast<float>(std::numbers::pi) * static_cast<float>(i + 1) / static_cast<float>(N);
            ImVec2 c1 = {centre.x + std::cos(a1)*scale,
                         centre.y - std::sin(a1)*scale};
            ImVec2 c2 = {centre.x + std::cos(a2)*scale,
                         centre.y - std::sin(a2)*scale};
            dl->AddLine(c1, c2, IM_COL32(100,100,200,160));
        }
    }

    // Draws poles (×) and zeros (○) for a set of PoleZero entries
    auto draw_pz = [&](const std::vector<PoleZero>& pz,
                        ImU32 col_pole, ImU32 col_zero,
                        const char* slot_label)
    {
        for (const auto& item : pz) {
            ImVec2 sc = z_to_screen(item.value, centre, scale);
            if (sc.x < p0.x || sc.x > p0.x+side ||
                sc.y < p0.y || sc.y > p0.y+side) continue;

            if (item.is_pole) {
                float r = 5.0f;
                dl->AddLine({sc.x-r, sc.y-r}, {sc.x+r, sc.y+r}, col_pole, 1.5f);
                dl->AddLine({sc.x+r, sc.y-r}, {sc.x-r, sc.y+r}, col_pole, 1.5f);
            } else {
                dl->AddCircle(sc, 5.0f, col_zero, 16, 1.5f);
            }

            if (std::hypot(ImGui::GetMousePos().x - sc.x,
                           ImGui::GetMousePos().y - sc.y) < 8.0f) {
                ImGui::BeginTooltip();
                if (slot_label) ImGui::TextDisabled("%s", slot_label);
                ImGui::Text("%s  %.4f %+.4fi",
                    item.is_pole ? "Pole" : "Zero",
                    item.value.real(), item.value.imag());
                ImGui::Text("|z| = %.4f  ∠%.1f°",
                    std::abs(item.value),
                    std::arg(item.value) * 180.0 / std::numbers::pi);
                ImGui::EndTooltip();
            }
        }
    };

    // Comparison slots first (background)
    for (const auto& c : state.compare()) {
        if (!c.active || c.poles_zeros.empty()) continue;
        auto fc = [](float v){ return static_cast<int>(v * 255); };
        ImU32 col = IM_COL32(fc(c.color[0]), fc(c.color[1]), fc(c.color[2]), 160);
        draw_pz(c.poles_zeros, col, col, c.label.c_str());
    }

    // Active filter (foreground, standard colours)
    draw_pz(state.result().poles_zeros,
            IM_COL32(255,80,80,220), IM_COL32(80,220,255,220), nullptr);

    // Stability indicator
    bool stable = state.result().valid;
    if (stable) {
        for (const auto& item : state.result().poles_zeros)
            if (item.is_pole && std::abs(item.value) >= 1.0)
                { stable = false; break; }
    }
    ImVec2 ind_pos = {p0.x + side - 80.0f, p0.y + 4.0f};
    dl->AddRectFilled(ind_pos, {ind_pos.x+76, ind_pos.y+18},
        stable ? IM_COL32(20,120,20,200) : IM_COL32(160,20,20,200), 3.0f);
    dl->AddText(ind_pos, IM_COL32(255,255,255,230),
        stable ? "  STABLE  " : " UNSTABLE ");

    // Reserve space
    ImGui::InvisibleButton("##pz", {side, side});
    ImGui::End();
}

} // namespace fiview2::panels
