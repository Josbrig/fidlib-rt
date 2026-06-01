// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "serialization.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace fiview2 {

// ── FilterParams JSON ─────────────────────────────────────────────────────────

static json params_to_json(const FilterParams& p)
{
    return json{
        {"family",   static_cast<int>(p.family)},
        {"passband", static_cast<int>(p.passband)},
        {"order",    p.order},
        {"rate",     p.rate},
        {"fc1",      p.fc1},
        {"fc2",      p.fc2},
        {"ripple_db",p.ripple_db},
        {"q_factor", p.q_factor},
        {"gain_db",  p.gain_db},
    };
}

static FilterParams params_from_json(const json& j)
{
    FilterParams p;
    p.family    = static_cast<FilterFamily>(j.value("family",   0));
    p.passband  = static_cast<FilterPassband>(j.value("passband",0));
    p.order     = j.value("order",    4);
    p.rate      = j.value("rate",     44100.0);
    p.fc1       = j.value("fc1",      1000.0);
    p.fc2       = j.value("fc2",      2000.0);
    p.ripple_db = j.value("ripple_db",-1.0);
    p.q_factor  = j.value("q_factor", 1.0);
    p.gain_db   = j.value("gain_db",  6.0);
    return p;
}

// ── state JSON ────────────────────────────────────────────────────────────────

static json state_to_json(const FilterState& state)
{
    json j;
    j["version"] = 1;
    json slots = json::array();
    for (const auto& s : state.slots()) {
        slots.push_back({
            {"params",  params_to_json(s.params)},
            {"enabled", s.enabled},
            {"label",   s.label},
        });
    }
    j["slots"]       = slots;
    j["active_slot"] = state.active_slot();

    json compare = json::array();
    for (const auto& c : state.compare()) {
        compare.push_back({
            {"active", c.active},
            {"label",  c.label},
            {"params", params_to_json(c.params)},
        });
    }
    j["compare"] = compare;
    return j;
}

static bool json_to_state(FilterState& state, const json& j)
{
    try {
        const auto& slots_j = j.at("slots");
        auto& slots = state.slots();
        slots.clear();
        for (const auto& sj : slots_j) {
            FilterSlot s;
            s.params  = params_from_json(sj.at("params"));
            s.enabled = sj.value("enabled", true);
            s.label   = sj.value("label", "Filter");
            slots.push_back(std::move(s));
        }
        if (slots.empty()) slots.emplace_back();
        state.set_active_slot(j.value("active_slot", 0));

        if (j.contains("compare")) {
            int idx = 0;
            for (const auto& cj : j.at("compare")) {
                if (idx >= 4) break;
                auto& c  = state.compare()[static_cast<size_t>(idx++)];
                c.active = cj.value("active", false);
                c.label  = cj.value("label", "");
                if (cj.contains("params"))
                    c.params = params_from_json(cj.at("params"));
            }
        }
        state.update();
        return true;
    } catch (...) {
        return false;
    }
}

// ── public API ────────────────────────────────────────────────────────────────

bool save_state(const FilterState& state, const std::string& path)
{
    std::ofstream f(path);
    if (!f) return false;
    f << state_to_json(state).dump(2);
    return f.good();
}

bool load_state(FilterState& state, const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;
    try {
        json j = json::parse(f);
        return json_to_state(state, j);
    } catch (...) {
        return false;
    }
}

// ── Base64 encode/decode (RFC 4648 URL-safe) ─────────────────────────────────

static const char k_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string state_to_base64(const FilterState& state)
{
    std::string raw = state_to_json(state).dump();
    std::string out;
    out.reserve((raw.size() * 4 + 2) / 3);
    size_t i = 0;
    while (i < raw.size()) {
        uint32_t b  = static_cast<uint8_t>(raw[i++]) << 16;
        if (i < raw.size()) b |= static_cast<uint8_t>(raw[i++]) << 8;
        if (i < raw.size()) b |= static_cast<uint8_t>(raw[i++]);
        out += k_b64[(b >> 18) & 63];
        out += k_b64[(b >> 12) & 63];
        out += k_b64[(b >>  6) & 63];
        out += k_b64[(b >>  0) & 63];
    }
    // Remove padding
    while (!out.empty() && out.back() == 'A' && raw.size() % 3 != 0)
        out.pop_back();
    return out;
}

bool base64_to_state(FilterState& state, const std::string& b64)
{
    static const int8_t dec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    };
    std::string raw;
    raw.reserve(b64.size() * 3 / 4);
    size_t i = 0;
    while (i + 3 < b64.size()) {
        int8_t a = dec[static_cast<uint8_t>(b64[i])];
        int8_t b = dec[static_cast<uint8_t>(b64[i+1])];
        int8_t c = dec[static_cast<uint8_t>(b64[i+2])];
        int8_t d = dec[static_cast<uint8_t>(b64[i+3])];
        if (a<0||b<0||c<0||d<0) break;
        uint32_t v = (static_cast<uint32_t>(a)<<18)|(static_cast<uint32_t>(b)<<12)|
                     (static_cast<uint32_t>(c)<< 6)|(static_cast<uint32_t>(d));
        raw += static_cast<char>((v>>16)&0xFF);
        raw += static_cast<char>((v>> 8)&0xFF);
        raw += static_cast<char>((v    )&0xFF);
        i += 4;
    }
    try {
        return json_to_state(state, json::parse(raw));
    } catch (...) {
        return false;
    }
}

} // namespace fiview2
