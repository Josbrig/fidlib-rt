// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "export_panel.hpp"
#include "imgui.h"
#include <fidgen/generator.hpp>
#include <fidgen/filter_descriptor.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fiview2::panels {

static const char* k_langs[] = {
    "c99", "cpp20", "python", "rust", "matlab", "julia", "verilog", "systemverilog"
};
static const char* k_lang_labels[] = {
    "C99", "C++20", "Python", "Rust", "MATLAB/Octave", "Julia", "Verilog", "SystemVerilog"
};
static const char* k_simd_names[] = {"None", "SSE2", "AVX2", "NEON", "Auto"};

void draw_export_panel(FilterState& state)
{
    if (!ImGui::Begin("fidgen Export")) { ImGui::End(); return; }

    static int  s_lang     = 0;   // c99
    static int  s_simd     = 0;   // None
    static bool s_with_test = false;
    static bool s_no_guard  = false;
    static char s_name[64]  = "";
    static std::string s_code;
    static std::string s_error;
    static char s_outpath[256] = "filter_output";

    const auto& res = state.result();

    ImGui::SeparatorText("Language");
    for (int i = 0; i < 8; ++i) {
        if (i > 0 && i % 4 != 0) ImGui::SameLine();
        bool sel = (s_lang == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(k_lang_labels[i])) s_lang = i;
        if (sel) ImGui::PopStyleColor();
    }

    ImGui::SeparatorText("Options");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("SIMD", &s_simd, k_simd_names, 5);
    ImGui::SameLine(); ImGui::Checkbox("--with-test", &s_with_test);
    ImGui::SameLine(); ImGui::Checkbox("--no-guard",  &s_no_guard);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("Name (optional)", s_name, sizeof(s_name));

    ImGui::SeparatorText("Spec");
    if (res.valid)
        ImGui::TextColored({0.4f,1,0.4f,1}, "%s @ %.0f Hz",
                           res.spec_str.c_str(), state.params().rate);
    else
        ImGui::TextColored({1,0.4f,0.4f,1}, "No valid filter");

    ImGui::Separator();

    bool can_gen = res.valid;
    if (!can_gen) ImGui::BeginDisabled();

    if (ImGui::Button("Generate")) {
        s_error.clear();
        try {
            static const fidgen::SimdLevel k_simd_map[] = {
                fidgen::SimdLevel::None, fidgen::SimdLevel::Sse2,
                fidgen::SimdLevel::Avx2, fidgen::SimdLevel::Neon,
                fidgen::SimdLevel::Auto
            };
            auto desc = fidgen::FilterDescriptor::from_spec(
                res.spec_str, state.params().rate,
                -1.0, -1.0, std::string(s_name));
            auto gen  = fidgen::Generator::create(k_langs[s_lang]);
            fidgen::GeneratorOptions opts;
            opts.simd      = k_simd_map[s_simd];
            opts.with_test = s_with_test;
            opts.with_guard = !s_no_guard;
            std::ostringstream oss;
            gen->generate(oss, desc, opts);
            s_code = oss.str();
        } catch (const std::exception& e) {
            s_error = e.what();
            s_code.clear();
        }
    }

    if (!can_gen) ImGui::EndDisabled();

    if (!s_error.empty())
        ImGui::TextColored({1,0.3f,0.3f,1}, "Error: %s", s_error.c_str());

    if (!s_code.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Copy")) ImGui::SetClipboardText(s_code.c_str());
        ImGui::SameLine();

        // Build default filename
        std::string ext = (s_lang == 1) ? ".hpp"
                        : (s_lang == 3) ? ".rs"
                        : (s_lang == 4) ? ".m"
                        : (s_lang == 5) ? ".jl"
                        : (s_lang >= 6) ? ".v"
                        : (s_lang == 2) ? ".py"
                        :                 ".h";
        if (ImGui::Button("Save")) {
            std::string path = std::string(s_outpath) + ext;
            std::ofstream f(path);
            if (f) { f << s_code; ImGui::SetTooltip("Saved: %s", path.c_str()); }
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText(("##outpath"+ext).c_str(), s_outpath, sizeof(s_outpath));

        // CLI command hint
        ImGui::Separator();
        std::string cli = std::string("fidgen -l ") + k_langs[s_lang] +
                          " -r " + std::to_string(static_cast<int>(state.params().rate)) +
                          (s_name[0] ? std::string(" -n ") + s_name : "") +
                          " \"" + res.spec_str + "\"";
        ImGui::TextDisabled("CLI: %s", cli.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy##cli")) ImGui::SetClipboardText(cli.c_str());

        ImGui::Separator();
        // Code preview (scrollable)
        float preview_h = std::max(ImGui::GetContentRegionAvail().y - 8.0f, 60.0f);
        ImGui::InputTextMultiline("##code",
            s_code.data(), s_code.size(),
            {-1, preview_h},
            ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::End();
}

} // namespace fiview2::panels
