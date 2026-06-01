// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
//
// test_fidgen_cpp20_smoke — numerical smoke test for generated C++20 code
//
// cmake generiert die Headers zur Build-Zeit via fidgen (-l cpp20).
// Dieser Test kompiliert sie und vergleicht Impulsantworten gegen fidlib-Runtime.
//
// Pfad A: fidlib fid_run_new() runtime
// Pfad B: generierter C++20 FilterClass::step() (static, constexpr coef)
//
// Toleranz: max_abs_diff <= 1e-10
//
// Testfilter:
//   lpbu4_1000   — LP Butterworth 4. Ordnung @ 44100 Hz
//   hpbu2_8000   — HP Butterworth 2. Ordnung @ 44100 Hz
//   bpbu4_500_2000 — BP Butterworth 4. Ordnung @ 44100 Hz
//   bsbu4_500_2000 — BS Butterworth 4. Ordnung @ 44100 Hz
//   lpbe4_1000   — LP Bessel 4. Ordnung @ 44100 Hz
//   lpch4_1000   — LP Chebyshev 4. Ordnung @ 44100 Hz
//   lphm_1000    — LP Hann FIR @ 44100 Hz

#include <stdio.h>
#include <fidlib/fidlib.h>

/* Generated headers — erzeugt durch cmake add_custom_command via fidgen */
#include "fidgen_cpp20_smoke_gen/lpbu4_1000.hpp"
#include "fidgen_cpp20_smoke_gen/lpbu6_1000.hpp"
#include "fidgen_cpp20_smoke_gen/lpbu8_1000.hpp"
#include "fidgen_cpp20_smoke_gen/hpbu2_8000.hpp"
#include "fidgen_cpp20_smoke_gen/bpbu4_500_2000.hpp"
#include "fidgen_cpp20_smoke_gen/bsbu4_500_2000.hpp"
#include "fidgen_cpp20_smoke_gen/lpbe4_1000.hpp"
#include "fidgen_cpp20_smoke_gen/lpch4_1000.hpp"
#include "fidgen_cpp20_smoke_gen/lphm_1000.hpp"
#include "fidgen_cpp20_smoke_gen/lphn_1000.hpp"
#include "fidgen_cpp20_smoke_gen/lpba_1000.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static constexpr int    N_SAMPLES = 512;
static constexpr double ABS_TOL   = 1e-10;

static int g_pass = 0;
static int g_fail = 0;

static double max_abs_diff(const double* a, const double* b, int n)
{
    double m = 0.0;
    for (int i = 0; i < n; ++i) {
        double d = std::fabs(a[i] - b[i]);
        if (d > m) m = d;
    }
    return m;
}

static void run_fidlib(const char* spec, double rate,
                       const double* in, double* out, int n)
{
    FidFilter* ff = fid_design(spec, rate, -1.0, -1.0, 0, nullptr);
    double (*step_fn)(void*, double) = nullptr;
    void* run = fid_run_new(ff, &step_fn);
    void* buf = fid_run_newbuf(run);
    for (int i = 0; i < n; ++i)
        out[i] = step_fn(buf, in[i]);
    fid_run_freebuf(buf);
    fid_run_free(run);
    std::free(ff);
}

#define RUN_SMOKE_CPP20(spec, rate, FilterClass)                              \
do {                                                                           \
    static double impulse[N_SAMPLES];                                          \
    static double ref[N_SAMPLES];                                              \
    static double got[N_SAMPLES];                                              \
    impulse[0] = 1.0;                                                          \
    for (int _i = 1; _i < N_SAMPLES; ++_i) impulse[_i] = 0.0;                \
    run_fidlib(spec, rate, impulse, ref, N_SAMPLES);                           \
    {                                                                          \
        FilterClass::State st;                                                 \
        FilterClass::reset(st);                                                \
        for (int _i = 0; _i < N_SAMPLES; ++_i)                                \
            got[_i] = FilterClass::step(st, impulse[_i]);                     \
    }                                                                          \
    {                                                                          \
        double mad = max_abs_diff(ref, got, N_SAMPLES);                        \
        if (mad <= ABS_TOL) {                                                  \
            ++g_pass;                                                          \
        } else {                                                               \
            ++g_fail;                                                          \
            std::fprintf(stderr,                                               \
                "FAIL %s @ %.0f Hz: max_abs_diff=%.3e (tol=%.0e)\n",          \
                spec, (double)(rate), mad, ABS_TOL);                          \
        }                                                                      \
    }                                                                          \
} while(0)

int main()
{
    RUN_SMOKE_CPP20("LpBu4/1000",     44100.0, Lpbu41000Filter);
    RUN_SMOKE_CPP20("LpBu6/1000",     44100.0, Lpbu61000Filter);
    RUN_SMOKE_CPP20("LpBu8/1000",     44100.0, Lpbu81000Filter);
    RUN_SMOKE_CPP20("HpBu2/8000",     44100.0, Hpbu28000Filter);
    RUN_SMOKE_CPP20("BpBu4/500-2000", 44100.0, Bpbu45002000Filter);
    RUN_SMOKE_CPP20("BsBu4/500-2000", 44100.0, Bsbu45002000Filter);
    RUN_SMOKE_CPP20("LpBe4/1000",     44100.0, Lpbe41000Filter);
    RUN_SMOKE_CPP20("LpCh4/-1/1000",  44100.0, Lpch41000Filter);
    RUN_SMOKE_CPP20("LpHm/1000",      44100.0, Lphm1000Filter);
    RUN_SMOKE_CPP20("LpHn/1000",      44100.0, Lphn1000Filter);
    RUN_SMOKE_CPP20("LpBa/1000",      44100.0, Lpba1000Filter);

    std::printf("fidgen cpp20 smoke: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
