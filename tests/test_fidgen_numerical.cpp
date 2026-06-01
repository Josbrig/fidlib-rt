// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
//
// test_fidgen_numerical — numerische Verifikation der fidgen-Koeffizientenextraktion
//
// Beide Pfade verarbeiten identischen Input (Impulsantwort, N=512 Samples):
//
//   Pfad A — fidlib runtime:
//     fid_design() → fid_run_new() → step-Funktion N-mal aufrufen
//
//   Pfad B — fidgen FilterDescriptor:
//     FilterDescriptor::from_spec() → Direct Form II per Hand, selbe Koeffizienten
//
// Wenn Koeffizientenextraktion + Vorzeichen-Konvention korrekt sind,
// A and B must be bit-identical or within FP rounding error.
//
// Testfilter:
//   LpBu4/1000  @ 44100 Hz  (4. Ordnung, 2 Biquads)
//   HpBu4/8000  @ 44100 Hz  (Hochpass — andere Pol-Lage)
//   BpBu4/500-2000 @ 44100 Hz (Bandpass — freq1 aktiv)
//   LpBu2/100   @ 8000  Hz  (schmales LP, empfindlich auf Koeff-Fehler)
//   LpBe4/1000  @ 44100 Hz  (Bessel — andere Koeff-Struktur)

#include <fidgen/filter_descriptor.hpp>

#include <stdio.h>        // before fidlib.h: FILE* for fid_list_filters
#include <fidlib/fidlib.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ── Minimales Testframework ───────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; \
        std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

#define CHECK_NEAR(a, b, tol, msg) do { \
    double _a = (a), _b = (b), _t = (tol); \
    if (std::fabs(_a - _b) <= _t) { ++g_pass; } \
    else { ++g_fail; \
        std::fprintf(stderr, \
            "FAIL [%s:%d] %s\n  A=%.17g  B=%.17g  |A-B|=%.3e  tol=%.3e\n", \
            __FILE__, __LINE__, msg, _a, _b, std::fabs(_a-_b), _t); } \
} while(0)

// ── Pfad A: fidlib runtime ────────────────────────────────────────────────────

static std::vector<double> run_fidlib(const std::string& spec, double rate,
                                       const std::vector<double>& input)
{
    FidFilter* ff = fid_design(spec.c_str(), rate, -1.0, -1.0, 0, nullptr);
    if (!ff) {
        std::fprintf(stderr, "fid_design failed for '%s'\n", spec.c_str());
        return {};
    }

    double (*step_fn)(void*, double) = nullptr;
    void* run = fid_run_new(ff, &step_fn);
    void* buf = fid_run_newbuf(run);

    std::vector<double> output(input.size());
    for (std::size_t k = 0; k < input.size(); ++k)
        output[k] = step_fn(buf, input[k]);

    fid_run_freebuf(buf);
    fid_run_free(run);
    std::free(ff);
    return output;
}

// ── Pfad B: fidgen FilterDescriptor, Direct Form II ──────────────────────────
//
// Implementiert exakt dieselbe Formel wie die generierten Funktionen:
//   w[n] = x - a1*w[n-1] - a2*w[n-2]
//   y[n] = b0*w[n] + b1*w[n-1] + b2*w[n-2]
// a1, a2 stored with natural sign (may be negative).

static std::vector<double> run_fidgen(const fidgen::FilterDescriptor& d,
                                       const std::vector<double>& input)
{
    std::vector<double> output(input.size());

    // ── FIR: tapped delay line ────────────────────────────────────────────────
    if (d.is_fir()) {
        const int N = d.n_taps();
        const int D = N - 1;
        std::vector<double> st(static_cast<std::size_t>(D), 0.0);
        const auto& h = d.taps();

        for (std::size_t k = 0; k < input.size(); ++k) {
            double x = input[k];
            double y = h[0] * x;
            for (int j = 1; j < N; ++j)
                y += h[static_cast<std::size_t>(j)] * st[static_cast<std::size_t>(j-1)];
            for (int j = D - 1; j > 0; --j)
                st[static_cast<std::size_t>(j)] = st[static_cast<std::size_t>(j-1)];
            if (D > 0) st[0] = x;
            output[k] = d.gain() * y;
        }
        return output;
    }

    // ── IIR: Direct Form II biquad cascade ───────────────────────────────────
    const int n  = d.n_stages();
    const int ns = d.n_slots();
    std::vector<double> st(static_cast<std::size_t>(ns), 0.0);

    for (std::size_t k = 0; k < input.size(); ++k) {
        double x = input[k];
        int slot = 0;
        for (int i = 0; i < n; ++i) {
            const auto& s = d.stages()[static_cast<std::size_t>(i)];
            const int s0 = slot;
            if (s.order >= 2) {
                const int s1 = slot + 1;
                double w = x - s.a[1]*st[s0] - s.a[2]*st[s1];
                double y = s.b[0]*w + s.b[1]*st[s0] + s.b[2]*st[s1];
                st[s1] = st[s0];
                st[s0] = w;
                x = y;
                slot += 2;
            } else {
                double w = x - s.a[1]*st[s0];
                double y = s.b[0]*w + s.b[1]*st[s0];
                st[s0] = w;
                x = y;
                slot += 1;
            }
        }
        output[k] = d.gain() * x;
    }
    return output;
}

// ── Vergleich ─────────────────────────────────────────────────────────────────

// Maximum absolute deviation over all N samples
static double max_abs_diff(const std::vector<double>& a,
                            const std::vector<double>& b)
{
    double m = 0.0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

// Maximale relative Abweichung (nur bei |ref| > threshold)
static double max_rel_diff(const std::vector<double>& ref,
                            const std::vector<double>& got,
                            double threshold = 1e-10)
{
    double m = 0.0;
    for (std::size_t i = 0; i < ref.size() && i < got.size(); ++i) {
        if (std::fabs(ref[i]) > threshold)
            m = std::max(m, std::fabs(ref[i] - got[i]) / std::fabs(ref[i]));
    }
    return m;
}

// ── test cases ────────────────────────────────────────────────────────────

static constexpr int    N_SAMPLES  = 512;
static constexpr double ABS_TOL    = 1e-12;   // < 1 ULP for typical filter values
static constexpr double REL_TOL    = 1e-10;   // 0.1 ppb relative Toleranz

static std::vector<double> make_impulse(int n)
{
    std::vector<double> v(static_cast<std::size_t>(n), 0.0);
    v[0] = 1.0;
    return v;
}

static std::vector<double> make_step(int n)
{
    return std::vector<double>(static_cast<std::size_t>(n), 1.0);
}

static void run_test(const char* spec, double rate,
                     double abs_tol = ABS_TOL, double rel_tol = REL_TOL)
{
    const std::string s{spec};
    auto d = fidgen::FilterDescriptor::from_spec(s, rate);

    // Impulsantwort
    {
        auto inp = make_impulse(N_SAMPLES);
        auto ref = run_fidlib(s, rate, inp);
        auto got = run_fidgen(d, inp);

        char msg_abs[128], msg_rel[128];
        std::snprintf(msg_abs, sizeof(msg_abs),
            "%s @%.0fHz impulse: max_abs_diff <= %.0e", spec, rate, abs_tol);
        std::snprintf(msg_rel, sizeof(msg_rel),
            "%s @%.0fHz impulse: max_rel_diff <= %.0e", spec, rate, rel_tol);

        double ad = max_abs_diff(ref, got);
        double rd = max_rel_diff(ref, got);

        CHECK(ad <= abs_tol, msg_abs);
        if (ad > abs_tol)
            std::fprintf(stderr, "  (max_abs_diff = %.3e)\n", ad);

        CHECK(rd <= rel_tol, msg_rel);
        if (rd > rel_tol)
            std::fprintf(stderr, "  (max_rel_diff = %.3e)\n", rd);
    }

    // step response — test steady state
    {
        auto inp = make_step(N_SAMPLES);
        auto ref = run_fidlib(s, rate, inp);
        auto got = run_fidgen(d, inp);

        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "%s @%.0fHz step: max_abs_diff <= %.0e", spec, rate, abs_tol);

        double ad = max_abs_diff(ref, got);
        CHECK(ad <= abs_tol, msg);
        if (ad > abs_tol)
            std::fprintf(stderr, "  (max_abs_diff = %.3e)\n", ad);
    }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main()
{
    // Butterworth LP — Referenzfall
    run_test("LpBu4/1000",    44100.0);
    run_test("LpBu2/1000",    44100.0);
    run_test("LpBu6/1000",    44100.0);
    run_test("LpBu8/1000",    44100.0);

    // Hochpass
    run_test("HpBu4/8000",    44100.0);
    run_test("HpBu2/8000",    44100.0);

    // Bandpass
    run_test("BpBu4/500-2000", 44100.0);
    run_test("BpBu2/500-2000", 44100.0);

    // Bandsperre
    run_test("BsBu4/500-2000", 44100.0);

    // Schmalbandiges LP — empfindlich auf Koeff-Fehler
    run_test("LpBu2/100",      8000.0);
    run_test("LpBu4/100",      8000.0);

    // Bessel
    run_test("LpBe4/1000",    44100.0);

    // Chebyshev
    run_test("LpCh4/-1/1000", 44100.0);

    // Hohe Sample-Rate
    run_test("LpBu4/1000",   192000.0);

    // Peaking EQ, Resonator, Allpass
    // PkBq: near-singular filter (poles close to unit circle) → looser tolerance
    run_test("PkBq2/1000/1/6", 44100.0, 1e-9, 1e-4);
    run_test("BpRe/0.5/1000",  44100.0);
    run_test("ApBq2/1/1000",   44100.0, 1e-12, 1e-9);

    // Windowed FIR
    run_test("LpHm/1000",    44100.0);   // Hann
    run_test("LpBl/1000",    44100.0);   // Blackman
    run_test("LpHn/1000",    44100.0);   // Hamming
    run_test("LpBa/1000",    44100.0);   // Bartlett

    std::printf("fidgen numerical tests: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
