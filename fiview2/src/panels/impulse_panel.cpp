// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "impulse_panel.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
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

// Draw Y-axis ticks left of the plot area.
static void draw_y_axis(ImDrawList* dl, ImVec2 p0, float axis_w, float h,
                        double mn, double mx)
{
    const ImU32 col_tick  = IM_COL32(80,  80,  80, 180);
    const ImU32 col_label = IM_COL32(160, 160, 160, 220);

    double range = mx - mn;
    double raw   = range / 5.0;
    double mag   = std::pow(10.0, std::floor(std::log10(std::fabs(raw) + 1e-30)));
    double step;
    if      (raw / mag < 1.5) step = mag;
    else if (raw / mag < 3.5) step = 2.0 * mag;
    else if (raw / mag < 7.5) step = 5.0 * mag;
    else                      step = 10.0 * mag;

    float x_line = p0.x + axis_w;
    dl->AddLine({x_line, p0.y}, {x_line, p0.y + h}, col_tick);

    double v = std::ceil(mn / step) * step;
    while (v <= mx + step * 0.01) {
        float y = p0.y + h - (float)((v - mn) / range) * h;
        if (y >= p0.y - 1.0f && y <= p0.y + h + 1.0f) {
            dl->AddLine({x_line - 4.0f, y}, {x_line, y}, col_tick);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.3g", v);
            float tw = ImGui::CalcTextSize(buf).x;
            dl->AddText({x_line - 6.0f - tw, y - 6.0f}, col_label, buf);
        }
        v += step;
    }
}

// Draw time axis ticks below the plot area.
// t_lo_ms / t_hi_ms = visible time range in milliseconds.
static void draw_time_axis(ImDrawList* dl, ImVec2 p0, float w, float axis_y,
                           double t_lo_ms, double t_hi_ms)
{
    const ImU32 col_tick  = IM_COL32(80,  80,  80, 180);
    const ImU32 col_label = IM_COL32(160, 160, 160, 220);

    double span = t_hi_ms - t_lo_ms;
    if (span <= 0.0) return;

    // Pick a nice step
    double raw = span / 6.0;
    double mag = std::pow(10.0, std::floor(std::log10(raw + 1e-30)));
    double step_ms;
    if      (raw / mag < 1.5) step_ms = mag;
    else if (raw / mag < 3.5) step_ms = 2.0 * mag;
    else if (raw / mag < 7.5) step_ms = 5.0 * mag;
    else                      step_ms = 10.0 * mag;

    bool use_ms = (t_hi_ms < 2000.0);

    dl->AddLine({p0.x, axis_y}, {p0.x + w, axis_y}, col_tick);

    double t_start = std::ceil(t_lo_ms / step_ms) * step_ms;
    for (double t = t_start; t <= t_hi_ms + step_ms * 0.01; t += step_ms) {
        float x = p0.x + (float)((t - t_lo_ms) / span) * w;
        if (x < p0.x - 1.0f || x > p0.x + w + 1.0f) continue;
        dl->AddLine({x, axis_y}, {x, axis_y + 4.0f}, col_tick);
        char buf[24];
        if (use_ms)
            std::snprintf(buf, sizeof(buf), "%.4gms", t);
        else
            std::snprintf(buf, sizeof(buf), "%.4gs", t / 1000.0);
        dl->AddText({x + 2.0f, axis_y + 2.0f}, col_label, buf);
    }
}

// Persistent zoom state for the impulse panel (fractions of view_n)
static double s_t_lo_frac = 0.0;
static double s_t_hi_frac = 1.0;

void draw_impulse_panel(FilterState& state)
{
    if (!ImGui::Begin("Impulse / Step")) { ImGui::End(); return; }

    static bool   show_step  = false;
    static bool   log_amp    = false;
    static int    view_n     = 256;
    static char   export_path[256] = "impulse_response.txt";

    ImGui::Checkbox("Step response", &show_step);
    ImGui::SameLine(); ImGui::Checkbox("Log amplitude", &log_amp);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset Zoom")) { s_t_lo_frac = 0.0; s_t_hi_frac = 1.0; }
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::SliderInt("Samples", &view_n, 16, 1024))
        { s_t_lo_frac = 0.0; s_t_hi_frac = 1.0; }  // reset zoom on range change

    const auto& res = state.result();
    if (!res.valid) {
        ImGui::TextColored({1,0.4f,0.4f,1}, "No valid filter");
        ImGui::End(); return;
    }

    const auto& sig = show_step ? res.step : res.impulse;
    int n = std::min(view_n, static_cast<int>(sig.size()));

    // Clamp zoom fractions
    s_t_lo_frac = std::clamp(s_t_lo_frac, 0.0, 1.0);
    s_t_hi_frac = std::clamp(s_t_hi_frac, 0.0, 1.0);
    if (s_t_hi_frac - s_t_lo_frac < 4.0 / n) s_t_hi_frac = s_t_lo_frac + 4.0 / n;
    if (s_t_hi_frac > 1.0) { s_t_hi_frac = 1.0; s_t_lo_frac = std::max(0.0, 1.0 - 4.0/n); }

    int v_lo = static_cast<int>(s_t_lo_frac * (n - 1));
    int v_hi = static_cast<int>(s_t_hi_frac * (n - 1));
    if (v_hi <= v_lo) v_hi = v_lo + 1;
    v_hi = std::min(v_hi, n - 1);

    const double rate   = state.params().rate;
    const float axis_h  = 18.0f;
    const float y_ax_w  = 44.0f;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float total_w = avail.x - 4.0f;
    float w = total_w - y_ax_w;
    float h = std::max(avail.y - 50.0f - axis_h, 40.0f);

    ImVec2 orig = ImGui::GetCursorScreenPos();
    ImVec2 p0   = {orig.x + y_ax_w, orig.y};

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, {p0.x+w, p0.y+h}, IM_COL32(18,18,25,255));
    dl->AddRect      (p0, {p0.x+w, p0.y+h}, IM_COL32(80,80,80,200));

    // Draw only the zoomed slice
    std::vector<double> view_sig(sig.begin() + v_lo, sig.begin() + v_hi + 1);
    int view_count = static_cast<int>(view_sig.size());

    double mn = 0.0, mx = 1.0;
    if (!view_sig.empty()) {
        mn = *std::min_element(view_sig.begin(), view_sig.end());
        mx = *std::max_element(view_sig.begin(), view_sig.end());
        if (mx - mn < 1e-30) mx = mn + 1.0;
    }

    draw_signal(dl, p0, w, h, view_sig, view_count,
                show_step ? IM_COL32(100,200,255,230) : IM_COL32(100,255,150,230));

    dl->AddText({p0.x + 4.0f, p0.y + 2.0f}, IM_COL32(160,160,160,200),
                show_step ? "step" : "impulse");

    draw_y_axis(dl, orig, y_ax_w, h, mn, mx);

    double t_lo_ms = v_lo * 1000.0 / rate;
    double t_hi_ms = v_hi * 1000.0 / rate;
    draw_time_axis(dl, p0, w, p0.y + h, t_lo_ms, t_hi_ms);

    // ── interaction: zoom / pan ────────────────────────────────────────────
    ImGui::InvisibleButton("##ir", {total_w, h + axis_h});
    const bool ir_hovered = ImGui::IsItemHovered();
    const bool ir_active  = ImGui::IsItemActive();

    if (ir_hovered || ir_active) {
        ImVec2 mp = ImGui::GetMousePos();
        float  tx = std::clamp((mp.x - p0.x) / w, 0.0f, 1.0f);
        double cur_frac = s_t_lo_frac + static_cast<double>(tx) * (s_t_hi_frac - s_t_lo_frac);

        // Scroll zoom centred on cursor
        float scroll = ImGui::GetIO().MouseWheel;
        if (ir_hovered && scroll != 0.0f) {
            double factor = (scroll > 0.0f) ? 1.0 / 1.35 : 1.35;
            double new_lo = cur_frac + (s_t_lo_frac - cur_frac) * factor;
            double new_hi = cur_frac + (s_t_hi_frac - cur_frac) * factor;
            new_lo = std::max(new_lo, 0.0);
            new_hi = std::min(new_hi, 1.0);
            if (new_hi - new_lo >= 4.0 / n)
                { s_t_lo_frac = new_lo; s_t_hi_frac = new_hi; }
        }

        // Drag pan
        if (ir_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            float dx = ImGui::GetIO().MouseDelta.x;
            if (dx != 0.0f) {
                double range = s_t_hi_frac - s_t_lo_frac;
                double shift = -(static_cast<double>(dx) / static_cast<double>(w)) * range;
                double new_lo = s_t_lo_frac + shift;
                double new_hi = s_t_hi_frac + shift;
                if (new_lo >= 0.0 && new_hi <= 1.0)
                    { s_t_lo_frac = new_lo; s_t_hi_frac = new_hi; }
            }
        }

        // Double-click reset
        if (ir_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            { s_t_lo_frac = 0.0; s_t_hi_frac = 1.0; }
    }

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
