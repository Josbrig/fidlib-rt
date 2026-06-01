// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
//
// test_fidgen_rust_smoke — numerical smoke test for generated Rust code
//
// cmake kompiliert via rustc die generierten .rs-Dateien zu Binaries.
// Jedes Binary gibt 512 Impulsantwort-Samples auf stdout aus (eine f64 pro Zeile).
// Dieser Test startet jedes Binary via popen(), liest die Werte und vergleicht
// gegen fidlib-Runtime.
//
// Toleranz: max_abs_diff <= 1e-10

#include <stdio.h>
#include <fidlib/fidlib.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef RUST_SMOKE_BINDIR
#  error "RUST_SMOKE_BINDIR must be defined by cmake"
#endif

static constexpr int    N_SAMPLES = 512;
static constexpr double ABS_TOL   = 1e-10;

static int g_pass = 0;
static int g_fail = 0;

static double max_abs_diff(const std::vector<double>& a, const std::vector<double>& b)
{
    double m = 0.0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

static std::vector<double> run_fidlib(const char* spec, double rate)
{
    FidFilter* ff = fid_design(spec, rate, -1.0, -1.0, 0, nullptr);
    if (!ff) return {};
    double (*step_fn)(void*, double) = nullptr;
    void* run = fid_run_new(ff, &step_fn);
    void* buf = fid_run_newbuf(run);

    std::vector<double> out(N_SAMPLES);
    double x = 1.0;
    for (int i = 0; i < N_SAMPLES; ++i) {
        out[static_cast<std::size_t>(i)] = step_fn(buf, x);
        x = 0.0;
    }
    fid_run_freebuf(buf);
    fid_run_free(run);
    std::free(ff);
    return out;
}

static std::vector<double> run_rust_binary(const char* name)
{
    std::string path = std::string(RUST_SMOKE_BINDIR) + "/" + name;
    FILE* pipe = popen(path.c_str(), "r");
    if (!pipe) {
        std::fprintf(stderr, "popen failed for %s\n", path.c_str());
        return {};
    }
    std::vector<double> out;
    out.reserve(N_SAMPLES);
    char line[64];
    while (std::fgets(line, sizeof(line), pipe)) {
        double v;
        if (std::sscanf(line, "%lf", &v) == 1)
            out.push_back(v);
    }
    pclose(pipe);
    return out;
}

static void run_test(const char* spec, double rate, const char* bin_name)
{
    auto ref = run_fidlib(spec, rate);
    auto got = run_rust_binary(bin_name);

    if (got.size() < static_cast<std::size_t>(N_SAMPLES)) {
        ++g_fail;
        std::fprintf(stderr, "FAIL %s: Rust binary produced %zu samples (expected %d)\n",
                     spec, got.size(), N_SAMPLES);
        return;
    }

    double mad = max_abs_diff(ref, got);
    if (mad <= ABS_TOL) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr,
            "FAIL %s @ %.0f Hz: max_abs_diff=%.3e (tol=%.0e)\n",
            spec, rate, mad, ABS_TOL);
    }
}

int main()
{
    run_test("LpBu4/1000",     44100.0, "lpbu4_1000");
    run_test("LpBu6/1000",     44100.0, "lpbu6_1000");
    run_test("LpBu8/1000",     44100.0, "lpbu8_1000");
    run_test("HpBu2/8000",     44100.0, "hpbu2_8000");
    run_test("BpBu4/500-2000", 44100.0, "bpbu4_500_2000");
    run_test("BsBu4/500-2000", 44100.0, "bsbu4_500_2000");
    run_test("LpBe4/1000",     44100.0, "lpbe4_1000");
    run_test("LpCh4/-1/1000",  44100.0, "lpch4_1000");
    run_test("LpHm/1000",      44100.0, "lphm_1000");
    run_test("LpHn/1000",      44100.0, "lphn_1000");
    run_test("LpBa/1000",      44100.0, "lpba_1000");

    std::printf("fidgen rust smoke: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
