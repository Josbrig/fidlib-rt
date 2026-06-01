/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de> */
/*
 * test_fidgen_simd_smoke — numerical smoke test for SIMD-generated C99 code
 *
 * cmake generiert c99-Header mit --simd sse2 und kompiliert diesen Test mit -msse2.
 * Each filter is tested via both paths:
 *   Pfad A — skalarer step (gleiche API wie non-SIMD)
 *   Path B — step_2ch_sse2 (2-channel SSE2), both channels receive the same impulse
 *
 * Beide Ergebnisse werden gegen fidlib-Runtime verglichen, Toleranz 1e-10.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fidlib/fidlib.h>

/* Generated headers — erzeugt durch cmake add_custom_command via fidgen --simd sse2 */
#include "fidgen_simd_smoke_gen/lpbu4_1000_sse2.h"
#include "fidgen_simd_smoke_gen/lpbu6_1000_sse2.h"
#include "fidgen_simd_smoke_gen/lpbu8_1000_sse2.h"
#include "fidgen_simd_smoke_gen/hpbu2_8000_sse2.h"
#include "fidgen_simd_smoke_gen/bpbu4_500_2000_sse2.h"
#include "fidgen_simd_smoke_gen/bsbu4_500_2000_sse2.h"
#include "fidgen_simd_smoke_gen/lpbe4_1000_sse2.h"
#include "fidgen_simd_smoke_gen/lpch4_1000_sse2.h"

#define N_SAMPLES 512
#define ABS_TOL   1e-10

static int g_pass = 0;
static int g_fail = 0;

static double max_abs(const double *a, const double *b, int n)
{
    double m = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        double d = a[i] - b[i]; if (d < 0.0) d = -d;
        if (d > m) m = d;
    }
    return m;
}

static void run_fidlib_ref(const char *spec, double rate, double *out)
{
    FidFilter *ff = fid_design(spec, rate, -1.0, -1.0, 0, NULL);
    double (*step)(void *, double) = NULL;
    void *run = fid_run_new(ff, &step);
    void *buf = fid_run_newbuf(run);
    int i; double x = 1.0;
    for (i = 0; i < N_SAMPLES; i++) { out[i] = step(buf, x); x = 0.0; }
    fid_run_freebuf(buf); fid_run_free(run); free(ff);
}

#define CHECK_MAD(spec, rate, label, got)  do {                          \
    double mad = max_abs(ref, got, N_SAMPLES);                            \
    if (mad <= ABS_TOL) { ++g_pass; }                                     \
    else { ++g_fail;                                                      \
        fprintf(stderr, "FAIL %s %s @ %.0f Hz: max_abs=%.3e (tol=%.0e)\n",\
                label, spec, (double)(rate), mad, ABS_TOL); }             \
} while(0)

/* ── Scalar path ── */
#define RUN_SCALAR(spec, rate, StateT, coef_var, reset_fn, step_fn)  \
do {                                                                   \
    static double ref[N_SAMPLES], got[N_SAMPLES], imp[N_SAMPLES];     \
    int _i; imp[0] = 1.0;                                              \
    for (_i = 1; _i < N_SAMPLES; _i++) imp[_i] = 0.0;                \
    run_fidlib_ref(spec, rate, ref);                                   \
    { StateT st; reset_fn(&st);                                        \
      for (_i = 0; _i < N_SAMPLES; _i++)                              \
          got[_i] = step_fn(&st, &coef_var, imp[_i]); }               \
    CHECK_MAD(spec, rate, "scalar", got);                              \
} while(0)

/* ── SSE2 2-channel path ── */
#define RUN_SSE2(spec, rate, StateT, coef_var, reset_fn, step2ch_fn)  \
do {                                                                    \
    static double ref[N_SAMPLES], got0[N_SAMPLES], got1[N_SAMPLES];   \
    static double imp[N_SAMPLES];                                       \
    int _i; imp[0] = 1.0;                                              \
    for (_i = 1; _i < N_SAMPLES; _i++) imp[_i] = 0.0;                \
    run_fidlib_ref(spec, rate, ref);                                   \
    { StateT st[2]; double xin[2], yout[2];                            \
      reset_fn(&st[0]); reset_fn(&st[1]);                              \
      for (_i = 0; _i < N_SAMPLES; _i++) {                            \
          xin[0] = xin[1] = imp[_i];                                  \
          step2ch_fn(st, &coef_var, xin, yout);                        \
          got0[_i] = yout[0]; got1[_i] = yout[1]; } }                 \
    CHECK_MAD(spec, rate, "sse2_ch0", got0);                           \
    CHECK_MAD(spec, rate, "sse2_ch1", got1);                           \
} while(0)

int main(void)
{
#define TEST(spec, rate, ST, coef, rst, step, step2ch)  \
    RUN_SCALAR(spec, rate, ST, coef, rst, step);         \
    RUN_SSE2(spec, rate, ST, coef, rst, step2ch)

    TEST("LpBu4/1000",     44100.0, Lpbu41000Sse2State,    lpbu4_1000_sse2_coef,
         lpbu4_1000_sse2_reset, lpbu4_1000_sse2_step, lpbu4_1000_sse2_step_2ch_sse2);

    TEST("LpBu6/1000",     44100.0, Lpbu61000Sse2State,    lpbu6_1000_sse2_coef,
         lpbu6_1000_sse2_reset, lpbu6_1000_sse2_step, lpbu6_1000_sse2_step_2ch_sse2);

    TEST("LpBu8/1000",     44100.0, Lpbu81000Sse2State,    lpbu8_1000_sse2_coef,
         lpbu8_1000_sse2_reset, lpbu8_1000_sse2_step, lpbu8_1000_sse2_step_2ch_sse2);

    TEST("HpBu2/8000",     44100.0, Hpbu28000Sse2State,    hpbu2_8000_sse2_coef,
         hpbu2_8000_sse2_reset, hpbu2_8000_sse2_step, hpbu2_8000_sse2_step_2ch_sse2);

    TEST("BpBu4/500-2000", 44100.0, Bpbu45002000Sse2State, bpbu4_500_2000_sse2_coef,
         bpbu4_500_2000_sse2_reset, bpbu4_500_2000_sse2_step, bpbu4_500_2000_sse2_step_2ch_sse2);

    TEST("BsBu4/500-2000", 44100.0, Bsbu45002000Sse2State, bsbu4_500_2000_sse2_coef,
         bsbu4_500_2000_sse2_reset, bsbu4_500_2000_sse2_step, bsbu4_500_2000_sse2_step_2ch_sse2);

    TEST("LpBe4/1000",     44100.0, Lpbe41000Sse2State,    lpbe4_1000_sse2_coef,
         lpbe4_1000_sse2_reset, lpbe4_1000_sse2_step, lpbe4_1000_sse2_step_2ch_sse2);

    TEST("LpCh4/-1/1000",  44100.0, Lpch41000Sse2State,    lpch4_1000_sse2_coef,
         lpch4_1000_sse2_reset, lpch4_1000_sse2_step, lpch4_1000_sse2_step_2ch_sse2);

#undef TEST

    printf("fidgen simd smoke: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
