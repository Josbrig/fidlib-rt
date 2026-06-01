// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "compare_view.hpp"
#include "imgui.h"
#include <cstdio>

namespace fiview2::panels {

void draw_compare_view(FilterState& state)
{
    if (!ImGui::Begin("A/B Comparison")) { ImGui::End(); return; }

    static const char* k_slot_names[] = {"A", "B", "C", "D"};

    ImGui::TextDisabled("Freeze current filter into a compare slot:");
    ImGui::Spacing();

    for (int i = 0; i < 4; ++i) {
        auto& c = state.compare()[static_cast<size_t>(i)];

        ImGui::PushID(i);

        // Freeze button with slot colour
        ImVec4 col(c.color[0], c.color[1], c.color[2], 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            {col.x*1.2f, col.y*1.2f, col.z*1.2f, 1.0f});
        char btn[8]; std::snprintf(btn, sizeof(btn), "→ %s", k_slot_names[i]);
        if (ImGui::Button(btn)) state.freeze_to_compare(i);
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        // Active toggle
        bool act = c.active;
        if (ImGui::Checkbox("##active", &act)) c.active = act;
        ImGui::SameLine();

        // Clear
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.1f,0.1f,0.8f));
        if (ImGui::SmallButton("X")) { c.active = false; c.label.clear(); }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (c.active && !c.label.empty())
            ImGui::TextColored(col, "%s", c.label.c_str());
        else
            ImGui::TextDisabled("(empty)");

        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Active slots appear in the Frequency Response plot.");
    ImGui::TextDisabled("Their colour matches the freeze button above.");

    ImGui::End();
}

} // namespace fiview2::panels
