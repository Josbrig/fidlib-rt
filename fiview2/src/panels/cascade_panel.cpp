// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "cascade_panel.hpp"
#include "imgui.h"
#include <cstdio>

namespace fiview2::panels {

void draw_cascade_panel(FilterState& state)
{
    if (!ImGui::Begin("Filter Cascade")) { ImGui::End(); return; }

    auto& slots = state.slots();

    // Add / remove buttons
    if (ImGui::SmallButton("+ Add")) state.add_slot();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu filter(s) in cascade", slots.size());

    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto& s = slots[static_cast<size_t>(i)];

        ImGui::PushID(i);

        // Active highlight
        bool is_active = (i == state.active_slot());
        if (is_active) ImGui::PushStyleColor(ImGuiCol_Header,
            ImVec4(0.2f, 0.4f, 0.8f, 0.6f));

        // Collapsing header = select + label
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "%s  [%s]",
                      s.label.c_str(),
                      s.result.valid ? s.result.spec_str.c_str() : "error");

        bool open = ImGui::CollapsingHeader(hdr,
            ImGuiTreeNodeFlags_DefaultOpen |
            (is_active ? ImGuiTreeNodeFlags_Selected : 0));

        if (ImGui::IsItemClicked()) state.set_active_slot(i);
        if (is_active) ImGui::PopStyleColor();

        if (open) {
            // Enable / Bypass toggle
            bool en = s.enabled;
            if (ImGui::Checkbox("Enabled", &en)) {
                s.enabled = en;
                state.update();  // recompute cascade
            }
            ImGui::SameLine();

            // Remove (not if only one)
            if (slots.size() > 1) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f,0.1f,0.1f,1.0f));
                if (ImGui::SmallButton("Remove")) state.remove_slot(i);
                ImGui::PopStyleColor();
            }

            // Label edit
            char lbuf[64];
            std::snprintf(lbuf, sizeof(lbuf), "%s", s.label.c_str());
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputText("Label", lbuf, sizeof(lbuf)))
                s.label = lbuf;

            // Short status
            if (s.result.valid)
                ImGui::TextDisabled("Spec: %s", s.result.spec_str.c_str());
            else
                ImGui::TextColored({1,0.3f,0.3f,1}, "%s", s.result.error_msg.c_str());
        }

        ImGui::PopID();
    }

    ImGui::End();
}

} // namespace fiview2::panels
