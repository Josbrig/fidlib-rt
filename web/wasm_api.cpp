// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
//
// WASM API — exports filter design, frequency response, code generation
// to JavaScript via Emscripten.

#include "filter_state.hpp"
#include "fidlib.h"
#include <fidgen/filter_descriptor.hpp>
#include <fidgen/generator.hpp>

#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

// ── Error capture (replaces fidlib's exit(1) via longjmp) ────────────────────

static char      s_last_error[512] = {};
static jmp_buf   s_jmp_buf;
static bool      s_jmp_active = false;

static void capture_error(const char* msg) {
    std::snprintf(s_last_error, sizeof(s_last_error), "%s", msg);
    if (s_jmp_active) {
        s_jmp_active = false;
        std::longjmp(s_jmp_buf, 1);  // skip fidlib's exit(1)
    }
}

// Macro: wrap any fidlib call that might call error()
#define FIDLIB_TRY  s_jmp_active = true; if (setjmp(s_jmp_buf) != 0)
#define FIDLIB_DONE s_jmp_active = false;

// Install error handler once at module init
__attribute__((constructor))
static void wasm_init() {
    fid_set_error_handler(capture_error);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Returns malloc'd C-string — caller must call wasm_free_string()
static char* strdup_to_heap(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

static constexpr int N_FREQ  = 512;

// ── Public API ────────────────────────────────────────────────────────────────

extern "C" {

// Returns last error message (static buffer, no free needed)
const char* wasm_last_error() {
    return s_last_error;
}

// Clear last error
void wasm_clear_error() {
    s_last_error[0] = '\0';
}

// List all available filter types as JSON array of strings
char* wasm_list_filters() {
    // Returns JSON array of fidlib filter spec prefixes
    const char* json =
        "[\"LpBu\",\"HpBu\",\"BpBu\",\"BsBu\","
        "\"LpBe\",\"HpBe\",\"BpBe\",\"BsBe\","
        "\"LpCh\",\"HpCh\",\"BpCh\",\"BsCh\","
        "\"LpHn\",\"LpHm\",\"LpBl\",\"LpBa\","
        "\"BpBq\",\"ApBq\",\"BpRe\"]";
    return strdup_to_heap(json);
}

// Compute frequency response JSON: [{freq,mag_db,phase_deg,group_delay}, ...]
// spec: fidlib spec string, rate: sample rate Hz
char* wasm_freq_response_json(const char* spec, double rate) {
    s_last_error[0] = '\0';
    if (!spec || rate <= 0.0) {
        std::snprintf(s_last_error, sizeof(s_last_error), "invalid arguments");
        return nullptr;
    }

    FidFilter* ff = nullptr;
    FIDLIB_TRY { FIDLIB_DONE; return nullptr; }
    ff = fid_design(spec, rate, -1.0, -1.0, 0, nullptr);
    FIDLIB_DONE;
    if (!ff) {
        if (s_last_error[0] == '\0')
            std::snprintf(s_last_error, sizeof(s_last_error),
                          "fid_design failed for: %s", spec);
        return nullptr;
    }

    const double nyq = rate * 0.5;
    std::ostringstream ss;
    ss << "[";
    for (int k = 0; k < N_FREQ; ++k) {
        double freq = std::exp(
            std::log(1.0) + (double)k / (N_FREQ - 1) * std::log(nyq / 1.0));
        double phase = 0.0;
        double mag   = fid_response_pha(ff, freq / rate, &phase);
        double mag_db = (mag > 1e-30) ? 20.0 * std::log10(mag) : -600.0;

        double eps = freq * 0.001 + 0.01;
        double ph2 = 0.0;
        fid_response_pha(ff, (freq + eps) / rate, &ph2);
        double gd = -(ph2 - phase) / (2.0 * std::numbers::pi * eps);

        if (k > 0) ss << ",";
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "{\"f\":%.4g,\"m\":%.4g,\"p\":%.4g,\"g\":%.4g}",
            freq,
            std::isfinite(mag_db) ? mag_db : -600.0,
            phase * 180.0 / std::numbers::pi,
            std::isfinite(gd) ? gd : 0.0);
        ss << buf;
    }
    ss << "]";
    std::free(ff);
    return strdup_to_heap(ss.str());
}

// Compute impulse response JSON: [y0,y1,...,yN]
char* wasm_impulse_json(const char* spec, double rate, int n) {
    s_last_error[0] = '\0';
    if (!spec || rate <= 0.0 || n < 2) return nullptr;
    if (n > 4096) n = 4096;

    FidFilter* ff = nullptr;
    FIDLIB_TRY { FIDLIB_DONE; return nullptr; }
    ff = fid_design(spec, rate, -1.0, -1.0, 0, nullptr);
    FIDLIB_DONE;
    if (!ff) return nullptr;

    double (*step_fn)(void*, double) = nullptr;
    void* run = fid_run_new(ff, &step_fn);
    if (!run) { std::free(ff); return nullptr; }
    void* buf = fid_run_newbuf(run);

    std::ostringstream ss;
    ss << "[";
    double x = 1.0;
    for (int i = 0; i < n; ++i) {
        double y = step_fn(buf, x);
        if (!std::isfinite(y)) y = 0.0;
        if (i > 0) ss << ",";
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.8g", y);
        ss << tmp;
        x = 0.0;
    }
    ss << "]";

    fid_run_freebuf(buf);
    fid_run_free(run);
    std::free(ff);
    return strdup_to_heap(ss.str());
}

// Compute step response JSON: [y0,y1,...,yN]
char* wasm_step_json(const char* spec, double rate, int n) {
    s_last_error[0] = '\0';
    if (!spec || rate <= 0.0 || n < 2) return nullptr;
    if (n > 4096) n = 4096;

    FidFilter* ff = nullptr;
    FIDLIB_TRY { FIDLIB_DONE; return nullptr; }
    ff = fid_design(spec, rate, -1.0, -1.0, 0, nullptr);
    FIDLIB_DONE;
    if (!ff) return nullptr;

    double (*step_fn)(void*, double) = nullptr;
    void* run = fid_run_new(ff, &step_fn);
    if (!run) { std::free(ff); return nullptr; }
    void* buf = fid_run_newbuf(run);

    std::ostringstream ss;
    ss << "[";
    double x = 1.0, acc = 0.0;
    for (int i = 0; i < n; ++i) {
        double y = step_fn(buf, x);
        if (!std::isfinite(y)) break;
        acc += y;
        if (i > 0) ss << ",";
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.8g", acc);
        ss << tmp;
        x = 0.0;
    }
    ss << "]";

    fid_run_freebuf(buf);
    fid_run_free(run);
    std::free(ff);
    return strdup_to_heap(ss.str());
}

// Compute poles/zeros JSON: [{re,im,pole},...] (pole=1 for pole, 0 for zero)
char* wasm_poles_zeros_json(const char* spec, double rate) {
    s_last_error[0] = '\0';
    if (!spec || rate <= 0.0) return nullptr;

    std::ostringstream ss;
    ss << "[";
    bool first = true;

    try {
        auto desc = fidgen::FilterDescriptor::from_spec(spec, rate);
        auto quad_roots = [](double b, double c)
            -> std::pair<std::complex<double>, std::complex<double>>
        {
            std::complex<double> disc{b*b - 4.0*c, 0.0};
            std::complex<double> sq = std::sqrt(disc);
            return {(-b + sq) / 2.0, (-b - sq) / 2.0};
        };

        if (!desc.is_fir()) {
            for (const auto& s : desc.stages()) {
                auto emit = [&](std::complex<double> z, bool is_pole) {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf),
                        "%s{\"re\":%.8g,\"im\":%.8g,\"pole\":%d}",
                        first ? "" : ",", z.real(), z.imag(), is_pole ? 1 : 0);
                    ss << buf;
                    first = false;
                };
                if (s.order >= 2) {
                    auto [p1, p2] = quad_roots(s.a[1], s.a[2]);
                    emit(p1, true); emit(p2, true);
                    if (std::fabs(s.b[0]) > 1e-30) {
                        auto [z1, z2] = quad_roots(s.b[1]/s.b[0], s.b[2]/s.b[0]);
                        emit(z1, false); emit(z2, false);
                    }
                } else {
                    emit({-s.a[1], 0.0}, true);
                    if (std::fabs(s.b[0]) > 1e-30)
                        emit({-s.b[1]/s.b[0], 0.0}, false);
                }
            }
        }
    } catch (...) {}

    ss << "]";
    return strdup_to_heap(ss.str());
}

// Generate filter code in given language
// lang: "c99","cpp20","python","rust","matlab","julia","verilog","systemverilog"
char* wasm_generate_code(const char* spec, double rate, const char* lang) {
    s_last_error[0] = '\0';
    if (!spec || !lang || rate <= 0.0) return nullptr;

    try {
        auto desc = fidgen::FilterDescriptor::from_spec(spec, rate);
        auto gen  = fidgen::Generator::create(lang);
        fidgen::GeneratorOptions opts;
        std::ostringstream ss;
        gen->generate(ss, desc, opts);
        return strdup_to_heap(ss.str());
    } catch (const std::exception& e) {
        std::snprintf(s_last_error, sizeof(s_last_error), "%s", e.what());
        return nullptr;
    }
}

// Free a string returned by any wasm_* function
void wasm_free_string(char* s) {
    std::free(s);
}

// Build a fidlib spec string from parameters (mirrors FilterState::build_spec)
// Returns malloc'd string or nullptr on error
char* wasm_build_spec(int family, int passband, int order,
                      double fc1, double fc2,
                      double ripple_db, double q_factor, double gain_db)
{
    using namespace fiview2;
    FilterParams p;
    p.family    = static_cast<FilterFamily>(family);
    p.passband  = static_cast<FilterPassband>(passband);
    p.order     = order;
    p.fc1       = fc1;
    p.fc2       = fc2;
    p.ripple_db = ripple_db;
    p.q_factor  = q_factor;
    p.gain_db   = gain_db;
    return strdup_to_heap(FilterState::build_spec(p));
}

} // extern "C"
