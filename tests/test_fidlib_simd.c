// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025-2026 Jörg Simbrig
/*
 * test_fidlib_simd.c — Korrektheit der SIMD-Beschleunigung (NEON/SSE2)
 *
 * Testet:
 *   1. fid_fir_dot() direkt gegen skalare Referenzimplementierung
 *      (only when FIDLIB_SIMD is defined, otherwise skipped)
 *   2. 16-Tap Boxcar-FIR mit Impulsantwort — Ende-zu-Ende
 *      (always runs; triggers opcode 8 in the command-list backend)
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "fidlib.h"

#ifdef FIDLIB_SIMD
#include "fid_simd.h"
#endif

#define RATE    44100.0
#define N_TAPS  16
#define TOL     1e-13

static int g_failed = 0;

static int
chk_near(const char *lbl, double got, double want, double tol)
{
    double err = fabs(got - want);
    if (err <= tol) {
        printf("PASS  %-48s  got=%.10f  want=%.10f\n", lbl, got, want);
        return 0;
    }
    fprintf(stderr, "FAIL  %-48s  got=%.10f  want=%.10f  err=%.3e\n",
            lbl, got, want, err);
    g_failed++;
    return 1;
}

/* ── 1. fid_fir_dot direkter Primitiv-Test ─────────────────────────────── */

#ifdef FIDLIB_SIMD
static void
test_fir_dot_primitive(void)
{
    /* Scalar reference */
    static const double coef[16] = {
        1.0,  2.0,  3.0,  4.0,  5.0,  6.0,  7.0,  8.0,
        9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0
    };
    static const double data[16] = {
        0.1,  0.2,  0.3,  0.4,  0.5,  0.6,  0.7,  0.8,
        0.9,  1.0,  1.1,  1.2,  1.3,  1.4,  1.5,  1.6
    };

    /* Known scalar result */
    double ref = 0.0;
    for (int i = 0; i < 16; i++) ref += coef[i] * data[i];

    /* Test n=4,8,12,16 (4-wide boundaries) and n=3,7,13 (odd remainders) */
    static const int sizes[] = { 3, 4, 7, 8, 12, 13, 15, 16 };
    char label[64];
    for (int si = 0; si < (int)(sizeof(sizes)/sizeof(sizes[0])); si++) {
        int n = sizes[si];
        double scalar = 0.0;
        for (int i = 0; i < n; i++) scalar += coef[i] * data[i];
        double got = fid_fir_dot(coef, data, n);
        snprintf(label, sizeof(label), "fid_fir_dot n=%d", n);
        chk_near(label, got, scalar, TOL);
    }
    (void)ref;
}
#endif /* FIDLIB_SIMD */

/* ── 2. Boxcar-FIR Impulsantwort ──────────────────────────────────────── */

static void
test_boxcar_impulse(void)
{
    /* 16-Tap Boxcar: alle Koeffizienten = 1/16
     * Impulsantwort: 16 Ausgaben je 1/16, dann Null. */
    const double w = 1.0 / (double)N_TAPS;

    /* array format for fid_cv_array: type, len, val[0..len-1], 0 */
    double arr[N_TAPS + 3];
    arr[0] = (double)'F';
    arr[1] = (double)N_TAPS;
    for (int i = 0; i < N_TAPS; i++) arr[2 + i] = w;
    arr[2 + N_TAPS] = 0.0;

    FidFilter *ff = fid_cv_array(arr);

    FidFunc *fn = NULL;
    void    *run = fid_run_new(ff, &fn);
    void    *buf = fid_run_newbuf(run);

    /* Impuls bei t=0 */
    char label[64];
    for (int t = 0; t < N_TAPS; t++) {
        double in  = (t == 0) ? 1.0 : 0.0;
        double out = fn(buf, in);
        snprintf(label, sizeof(label), "boxcar impulse t=%d", t);
        chk_near(label, out, w, TOL);
    }
    /* Dann Null */
    for (int t = N_TAPS; t < N_TAPS + 4; t++) {
        double out = fn(buf, 0.0);
        snprintf(label, sizeof(label), "boxcar tail    t=%d", t);
        chk_near(label, out, 0.0, TOL);
    }

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── 3. Boxcar-FIR mit fid_run_bufsize / fid_run_initbuf ─────────────── */

static void
test_boxcar_initbuf(void)
{
    const double w = 1.0 / (double)N_TAPS;

    double arr[N_TAPS + 3];
    arr[0] = (double)'F';
    arr[1] = (double)N_TAPS;
    for (int i = 0; i < N_TAPS; i++) arr[2 + i] = w;
    arr[2 + N_TAPS] = 0.0;

    FidFilter *ff  = fid_cv_array(arr);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);

    int   sz  = fid_run_bufsize(run);
    void *mem = calloc(1, (size_t)sz);
    fid_run_initbuf(run, mem);

    char label[64];
    for (int t = 0; t < N_TAPS; t++) {
        double in  = (t == 0) ? 1.0 : 0.0;
        double out = fn(mem, in);
        snprintf(label, sizeof(label), "initbuf impulse t=%d", t);
        chk_near(label, out, w, TOL);
    }
    for (int t = N_TAPS; t < N_TAPS + 4; t++) {
        double out = fn(mem, 0.0);
        snprintf(label, sizeof(label), "initbuf tail    t=%d", t);
        chk_near(label, out, 0.0, TOL);
    }

    /* fid_run_zapbuf reset — re-run same impulse */
    fid_run_zapbuf(mem);
    for (int t = 0; t < N_TAPS; t++) {
        double in  = (t == 0) ? 1.0 : 0.0;
        double out = fn(mem, in);
        snprintf(label, sizeof(label), "zapbuf impulse  t=%d", t);
        chk_near(label, out, w, TOL);
    }

    free(mem);
    fid_run_free(run);
    free(ff);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
#ifdef FIDLIB_SIMD
    printf("=== fid_fir_dot Primitiv-Test");
#  ifdef FID_SIMD_NEON
    printf(" [NEON]");
#  elif defined(FID_SIMD_SSE2)
    printf(" [SSE2]");
#  else
    printf(" [scalar fallback]");
#  endif
    printf(" ===\n");
    test_fir_dot_primitive();
    printf("\n");
#else
    printf("(FIDLIB_SIMD not active — primitive test skipped)\n\n");
#endif

    printf("=== Boxcar-FIR Impulsantwort (opcode-8 Pfad) ===\n");
    test_boxcar_impulse();
    printf("\n");

    printf("=== Boxcar-FIR mit fid_run_initbuf / fid_run_zapbuf ===\n");
    test_boxcar_initbuf();
    printf("\n");

    if (g_failed == 0)
        printf("ALL TESTS PASSED\n");
    else
        fprintf(stderr, "%d TEST(S) FAILED\n", g_failed);

    return g_failed ? 1 : 0;
}
