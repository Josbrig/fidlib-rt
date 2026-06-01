// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "freq_plot.hpp"
#include "../util/math_utils.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace fiview2::panels {

// Slot colours (muted)
static const ImVec4 k_slot_cols[] = {
    {0.5f, 0.8f, 1.0f, 0.7f},
    {1.0f, 0.7f, 0.4f, 0.7f},
    {0.5f, 1.0f, 0.6f, 0.7f},
    {1.0f, 0.5f, 0.8f, 0.7f},
    {0.8f, 0.8f, 0.3f, 0.7f},
    {0.3f, 0.9f, 0.9f, 0.7f},
    {0.9f, 0.5f, 0.3f, 0.7f},
    {0.7f, 0.4f, 1.0f, 0.7f},
};

// Rounds up to the next "nice" step size
static double nice_step(double range)
{
    if (range <= 0.0) return 10.0;
    // Candidates: 1, 2, 3, 5, 6, 10, 12, 20, 24, 30, 40, 60 dB
    const double steps[] = {1,2,3,5,6,10,12,20,24,30,40,60};
    double target = range / 8.0;   // target ~8 grid lines
    for (double s : steps)
        if (s >= target) return s;
    return 60.0;
}

// Computes sensible DB_MIN / DB_MAX from curve data.
// Minimum range: 40 dB. One step margin top and bottom.
static void compute_db_range(const std::vector<FreqPoint>& pts,
                              double& db_min, double& db_max)
{
    for (const auto& p : pts) {
        if (!std::isfinite(p.magnitude_db)) continue;
        db_min = std::min(db_min, p.magnitude_db);
        db_max = std::max(db_max, p.magnitude_db);
    }
}

static void finalise_db_range(double& db_min, double& db_max)
{
    if (!std::isfinite(db_min) || !std::isfinite(db_max)) {
        db_min = -120.0; db_max = 20.0; return;
    }

    double step = nice_step(db_max - db_min);
    // Top: only 3 dB headroom — peak visible, no wasted space
    // Bottom: one full step — noise/stopband needs context
    db_max = std::ceil (db_max / step) * step + 3.0;
    db_min = std::floor(db_min / step) * step - step;

    // Minimum range
    if (db_max - db_min < 40.0) {
        double mid = (db_max + db_min) * 0.5;
        db_max = mid + 20.0;
        db_min = mid - 20.0;
    }
    // Reasonable floor (ignore noise below -160 dB)
    db_min = std::max(db_min, -160.0);
}

static void draw_grid(ImDrawList* dl, ImVec2 p0, float w, float h,
                      double f_lo, double f_hi,
                      double db_min, double db_max)
{
    const ImU32 col_grid  = IM_COL32(80,  80,  80, 120);
    const ImU32 col_label = IM_COL32(160, 160, 160, 200);

    // Frequency grid lines
    static const double freqs[] = {
        10,20,30,50,100,200,300,500,1e3,2e3,3e3,5e3,1e4,2e4,3e4,5e4,1e5
    };
    for (double f : freqs) {
        if (f < f_lo || f > f_hi) continue;
        float x = p0.x + math::log_normalize(f, f_lo, f_hi) * w;
        dl->AddLine({x, p0.y}, {x, p0.y + h}, col_grid);
        char buf[16]; std::snprintf(buf, sizeof(buf),
            f >= 1000.0 ? "%.0fk" : "%.0f",
            f >= 1000.0 ? f/1000.0 : f);
        dl->AddText({x + 2, p0.y + h - 14}, col_label, buf);
    }

    // dB grid lines — adaptive based on db_min/db_max
    double step = nice_step(db_max - db_min);
    double db = std::ceil(db_min / step) * step;
    while (db <= db_max + 0.01) {
        float y = p0.y + math::db_to_y(db, db_min, db_max, h);
        if (y >= p0.y - 1.0f && y <= p0.y + h + 1.0f) {
            ImU32 col_line = (std::fabs(db) < 0.01)
                ? IM_COL32(255,255,255,60) : col_grid;
            dl->AddLine({p0.x, y}, {p0.x + w, y}, col_line);
            char buf[16]; std::snprintf(buf, sizeof(buf), "%.4g dB", db);
            dl->AddText({p0.x + 2, y - 12}, col_label, buf);
        }
        db += step;
    }

    // Nyquist marker
    float xnyq = p0.x + w;
    dl->AddLine({xnyq, p0.y}, {xnyq, p0.y+h}, IM_COL32(255,100,100,100));
}

static void draw_curve(ImDrawList* dl, ImVec2 p0, float w, float h,
                       const std::vector<FreqPoint>& pts,
                       double f_lo, double f_hi,
                       double db_min, double db_max,
                       ImU32 col, bool show_phase)
{
    if (pts.empty()) return;

    // Magnitude
    for (size_t i = 1; i < pts.size(); ++i) {
        const auto& a = pts[i-1];
        const auto& b = pts[i];
        if (!std::isfinite(a.magnitude_db) || !std::isfinite(b.magnitude_db)) continue;
        float x1 = p0.x + math::log_normalize(a.freq_hz, f_lo, f_hi) * w;
        float y1 = p0.y + math::db_to_y(a.magnitude_db, db_min, db_max, h);
        float x2 = p0.x + math::log_normalize(b.freq_hz, f_lo, f_hi) * w;
        float y2 = p0.y + math::db_to_y(b.magnitude_db, db_min, db_max, h);
        // Clamp with 2 px margin — no hard clipping
        y1 = std::clamp(y1, p0.y - 2.0f, p0.y + h + 2.0f);
        y2 = std::clamp(y2, p0.y - 2.0f, p0.y + h + 2.0f);
        dl->AddLine({x1,y1}, {x2,y2}, col, 1.5f);
    }

    if (!show_phase) return;
    const ImU32 col_ph = (col & 0x00FFFFFF) | 0x70000000;
    for (size_t i = 1; i < pts.size(); ++i) {
        const auto& a = pts[i-1];
        const auto& b = pts[i];
        float x1 = p0.x + math::log_normalize(a.freq_hz, f_lo, f_hi) * w;
        float y1 = p0.y + static_cast<float>((180.0 - a.phase_deg) / 360.0) * h;
        float x2 = p0.x + math::log_normalize(b.freq_hz, f_lo, f_hi) * w;
        float y2 = p0.y + static_cast<float>((180.0 - b.phase_deg) / 360.0) * h;
        dl->AddLine({x1,y1}, {x2,y2}, col_ph, 1.0f);
    }
}

void draw_freq_plot(FilterState& state)
{
    if (!ImGui::Begin("Frequency Response")) { ImGui::End(); return; }

    static bool show_phase   = false;
    static bool show_cascade = true;
    static bool show_slots   = true;
    static bool auto_scale   = true;

    ImGui::Checkbox("Phase",      &show_phase);
    ImGui::SameLine(); ImGui::Checkbox("Cascade",    &show_cascade);
    ImGui::SameLine(); ImGui::Checkbox("Individual", &show_slots);
    ImGui::SameLine(); ImGui::Checkbox("Auto Y",     &auto_scale);

    // Spec
    const auto& res = state.result();
    if (res.valid)
        ImGui::TextColored({0.4f,1.0f,0.4f,1.0f}, "Spec: %s", res.spec_str.c_str());
    else
        ImGui::TextColored({1.0f,0.4f,0.4f,1.0f}, "Error: %s", res.error_msg.c_str());

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float  w = avail.x - 4.0f;
    float  h = std::max(avail.y - 4.0f, 60.0f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const double f_lo = 1.0;
    const double f_hi = state.nyquist();

    // ── determine dB range ──────────────────────────────────────────────────
    double db_min =  std::numeric_limits<double>::infinity();
    double db_max = -std::numeric_limits<double>::infinity();

    if (auto_scale) {
        // Scan all visible curves
        for (const auto& c : state.compare())
            if (c.active) compute_db_range(c.freq_response, db_min, db_max);
        if (show_slots)
            for (const auto& slot : state.slots())
                if (slot.enabled && slot.result.valid)
                    compute_db_range(slot.result.freq_response, db_min, db_max);
        if (show_cascade)
            compute_db_range(state.cascade_response(), db_min, db_max);
        // Fallback if no data available
        if (!std::isfinite(db_min)) { db_min = -120.0; db_max = 20.0; }
        finalise_db_range(db_min, db_max);
    } else {
        db_min = -120.0;
        db_max =   20.0;
    }

    // ── draw ────────────────────────────────────────────────────────────────
    dl->AddRectFilled(p0, {p0.x+w, p0.y+h}, IM_COL32(20,20,25,255));
    dl->AddRect      (p0, {p0.x+w, p0.y+h}, IM_COL32(80,80,80,200));

    draw_grid(dl, p0, w, h, f_lo, f_hi, db_min, db_max);

    for (const auto& c : state.compare()) {
        if (!c.active) continue;
        ImU32 col = IM_COL32(
            static_cast<int>(c.color[0]*255),
            static_cast<int>(c.color[1]*255),
            static_cast<int>(c.color[2]*255), 180);
        draw_curve(dl, p0, w, h, c.freq_response,
                   f_lo, f_hi, db_min, db_max, col, show_phase);
    }

    if (show_slots) {
        int ci = 0;
        for (const auto& slot : state.slots()) {
            if (!slot.enabled || !slot.result.valid) { ++ci; continue; }
            ImU32 col = ImGui::ColorConvertFloat4ToU32(
                k_slot_cols[ci % std::size(k_slot_cols)]);
            draw_curve(dl, p0, w, h, slot.result.freq_response,
                       f_lo, f_hi, db_min, db_max, col, show_phase);
            ++ci;
        }
    }

    if (show_cascade && !state.cascade_response().empty())
        draw_curve(dl, p0, w, h, state.cascade_response(),
                   f_lo, f_hi, db_min, db_max,
                   IM_COL32(255,255,255,230), show_phase);

    // ── crosshair and tooltip ──────────────────────────────────────────────
    ImGui::InvisibleButton("##plot", {w, h});
    if (ImGui::IsItemHovered()) {
        ImVec2 mp = ImGui::GetMousePos();
        float  tx = std::clamp((mp.x - p0.x) / w, 0.0f, 1.0f);
        double freq = math::log_denormalize(tx, f_lo, f_hi);

        dl->AddLine({mp.x, p0.y}, {mp.x, p0.y+h}, IM_COL32(255,255,100,100));

        ImGui::BeginTooltip();
        ImGui::Text("%.2f Hz", freq);
        for (const auto& slot : state.slots()) {
            if (!slot.enabled || !slot.result.valid) continue;
            const auto& fr = slot.result.freq_response;
            size_t lo_i = 0, hi_i = fr.size() - 1;
            while (lo_i < hi_i) {
                size_t mid = (lo_i + hi_i) / 2;
                if (math::log_normalize(fr[mid].freq_hz, f_lo, f_hi) < tx)
                    lo_i = mid + 1;
                else hi_i = mid;
            }
            ImGui::Text("  %s: %.2f dB  %.1f°",
                slot.label.c_str(),
                fr[lo_i].magnitude_db,
                fr[lo_i].phase_deg);
        }
        ImGui::EndTooltip();
    }

    ImGui::End();
}

} // namespace fiview2::panels
