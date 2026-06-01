/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de> */
/*
 * test_fidgen_c99_smoke — compile and run smoke test for generated C99 code
 *
 * Ablauf:
 *   cmake generiert die drei Headers zur Build-Zeit per fidgen (add_custom_command).
 *   Dieser Test kompiliert sie zusammen mit fidlib und vergleicht:
 *     Pfad A — fidlib fid_run_new() runtime
 *     Pfad B — generierter C99 step-Code (direkt als statische Funktion)
 *
 * Impulsantwort, N=512 Samples, Toleranz max_abs <= 1e-10.
 *
 * Testfilter:
 *   lpbu4_1000   @ 44100 Hz  (LP Butterworth 4. Ordnung, 2 Biquads)
 *   hpbu2_8000   @ 44100 Hz  (HP Butterworth 2. Ordnung, 1 Biquad)
 *   bpbu4_500_2000 @ 44100 Hz (BP Butterworth 4. Ordnung, 4 Biquads)
 */

#include <stdio.h>
#include <stdlib.h>
#include <fidlib/fidlib.h>

/* Generated headers — erzeugt durch cmake add_custom_command via fidgen */
#include "fidgen_smoke_gen/lpbu4_1000.h"
#include "fidgen_smoke_gen/lpbu6_1000.h"
#include "fidgen_smoke_gen/lpbu8_1000.h"
#include "fidgen_smoke_gen/hpbu2_8000.h"
#include "fidgen_smoke_gen/bpbu4_500_2000.h"
#include "fidgen_smoke_gen/bsbu4_500_2000.h"
#include "fidgen_smoke_gen/lpbe4_1000.h"
#include "fidgen_smoke_gen/lpch4_1000.h"
#include "fidgen_smoke_gen/lphm_1000.h"
#include "fidgen_smoke_gen/lphn_1000.h"
#include "fidgen_smoke_gen/lpba_1000.h"

#define N_SAMPLES 512
#define ABS_TOL   1e-10

static int g_pass = 0;
static int g_fail = 0;

static double max_abs_diff(const double *a, const double *b, int n)
{
    double m = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        double d = a[i] - b[i]; if (d < 0.0) d = -d;
        if (d > m) m = d;
    }
    return m;
}

/* ── fidlib reference path ──────────────────────────────────────────────── */

static void run_fidlib(const char *spec, double rate,
                       const double *in, double *out, int n)
{
    FidFilter *ff = fid_design(spec, rate, -1.0, -1.0, 0, NULL);
    double (*step)(void *, double) = NULL;
    void *run = fid_run_new(ff, &step);
    void *buf = fid_run_newbuf(run);
    int i;
    for (i = 0; i < n; i++) out[i] = step(buf, in[i]);
    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── Testfunktion ────────────────────────────────────────────────────────── */

#define RUN_SMOKE(spec, rate, state_t, coef_var, reset_fn, step_fn)         \
do {                                                                          \
    static double impulse[N_SAMPLES];                                         \
    static double ref[N_SAMPLES];                                             \
    static double got[N_SAMPLES];                                             \
    int _i;                                                                   \
    impulse[0] = 1.0;                                                         \
    for (_i = 1; _i < N_SAMPLES; _i++) impulse[_i] = 0.0;                   \
    run_fidlib(spec, rate, impulse, ref, N_SAMPLES);                          \
    {                                                                         \
        state_t st;                                                           \
        reset_fn(&st);                                                        \
        for (_i = 0; _i < N_SAMPLES; _i++)                                   \
            got[_i] = step_fn(&st, &coef_var, impulse[_i]);                  \
    }                                                                         \
    {                                                                         \
        double mad = max_abs_diff(ref, got, N_SAMPLES);                       \
        if (mad <= ABS_TOL) {                                                 \
            ++g_pass;                                                         \
        } else {                                                              \
            ++g_fail;                                                         \
            fprintf(stderr,                                                   \
                "FAIL %s @ %.0f Hz: max_abs_diff=%.3e (tol=%.0e)\n",         \
                spec, rate, mad, ABS_TOL);                                   \
        }                                                                     \
    }                                                                         \
} while(0)

int main(void)
{
    RUN_SMOKE("LpBu4/1000",    44100.0,
              Lpbu41000State,    lpbu4_1000_coef,
              lpbu4_1000_reset,  lpbu4_1000_step);

    RUN_SMOKE("LpBu6/1000",    44100.0,
              Lpbu61000State,    lpbu6_1000_coef,
              lpbu6_1000_reset,  lpbu6_1000_step);

    RUN_SMOKE("LpBu8/1000",    44100.0,
              Lpbu81000State,    lpbu8_1000_coef,
              lpbu8_1000_reset,  lpbu8_1000_step);

    RUN_SMOKE("HpBu2/8000",    44100.0,
              Hpbu28000State,    hpbu2_8000_coef,
              hpbu2_8000_reset,  hpbu2_8000_step);

    RUN_SMOKE("BpBu4/500-2000", 44100.0,
              Bpbu45002000State,    bpbu4_500_2000_coef,
              bpbu4_500_2000_reset, bpbu4_500_2000_step);

    RUN_SMOKE("BsBu4/500-2000", 44100.0,
              Bsbu45002000State,    bsbu4_500_2000_coef,
              bsbu4_500_2000_reset, bsbu4_500_2000_step);

    RUN_SMOKE("LpBe4/1000", 44100.0,
              Lpbe41000State,    lpbe4_1000_coef,
              lpbe4_1000_reset,  lpbe4_1000_step);

    RUN_SMOKE("LpCh4/-1/1000", 44100.0,
              Lpch41000State,    lpch4_1000_coef,
              lpch4_1000_reset,  lpch4_1000_step);

    RUN_SMOKE("LpHm/1000", 44100.0,
              Lphm1000State,    lphm_1000_coef,
              lphm_1000_reset,  lphm_1000_step);

    RUN_SMOKE("LpHn/1000", 44100.0,
              Lphn1000State,    lphn_1000_coef,
              lphn_1000_reset,  lphn_1000_step);

    RUN_SMOKE("LpBa/1000", 44100.0,
              Lpba1000State,    lpba_1000_coef,
              lpba_1000_reset,  lpba_1000_step);

    printf("fidgen c99 smoke: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
