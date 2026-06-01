// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025-2026 Jörg Simbrig
/*
 * test_fidlib_precision.c — FP32 vs. FP64 precision comparison
 *
 * Checks that:
 *   1. FIR filter (32-tap Boxcar) in current precision mode produces correct
 *      Ergebnisse liefert (Toleranz je nach FID_REAL).
 *   2. IIR-Filter (Butterworth LP 4. Ordnung) im aktuellen Modus stabil
 *      remains stable and shows plausible attenuation (stability check).
 *   3. The precision mode is correctly detected and reported.
 *
 * In FP64 build: tolerance 1e-12 for FIR.
 * In FP32 build: tolerance 1e-5 for FIR (FP32 has ~7 decimal digits).
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "fidlib.h"

static int g_failed = 0;

static int
chk_near(const char *lbl, double got, double want, double tol)
{
    double err = fabs(got - want);
    if (err <= tol) {
        printf("PASS  %-52s  got=%.10g  want=%.10g\n", lbl, got, want);
        return 0;
    }
    fprintf(stderr, "FAIL  %-52s  got=%.10g  want=%.10g  err=%.3e  tol=%.3e\n",
            lbl, got, want, err, tol);
    g_failed++;
    return 1;
}

static int
chk_range(const char *lbl, double got, double lo, double hi)
{
    if (got >= lo && got <= hi) {
        printf("PASS  %-52s  got=%.10g  in [%.3g, %.3g]\n", lbl, got, lo, hi);
        return 0;
    }
    fprintf(stderr, "FAIL  %-52s  got=%.10g  expected in [%.3g, %.3g]\n",
            lbl, got, lo, hi);
    g_failed++;
    return 1;
}

/* ── 1. FIR precision test: 32-tap Boxcar ──────────────────────────────── */

static void
test_fir_precision(void)
{
#ifdef FIDLIB_PRECISION_F32
    const double TOL = 1e-5;   /* FP32: ~7 Dezimalstellen */
    printf("Modus: FP32 (float)  — Toleranz %.0e\n\n", TOL);
#else
    const double TOL = 1e-12;  /* FP64: ~15 Dezimalstellen */
    printf("Modus: FP64 (double) — Toleranz %.0e\n\n", TOL);
#endif

    const int N = 32;
    const double w = 1.0 / (double)N;

    double arr[N + 3];
    arr[0] = (double)'F';
    arr[1] = (double)N;
    for (int i = 0; i < N; i++) arr[2 + i] = w;
    arr[2 + N] = 0.0;

    FidFilter *ff = fid_cv_array(arr);
    FidFunc   *fn = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    /* Impuls bei t=0: erwarte N Ausgaben von w, dann 0 */
    char label[64];
    for (int t = 0; t < N; t++) {
        double in  = (t == 0) ? 1.0 : 0.0;
        double out = fn(buf, in);
        snprintf(label, sizeof(label), "boxcar32 impulse t=%d", t);
        chk_near(label, out, w, TOL);
    }
    for (int t = N; t < N + 4; t++) {
        double out = fn(buf, 0.0);
        snprintf(label, sizeof(label), "boxcar32 tail    t=%d", t);
        chk_near(label, out, 0.0, TOL);
    }

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── 2. IIR stability test: Butterworth LP 4th order ────────────────────── */

static void
test_iir_stability(void)
{
    /* LpBu4 bei 1000 Hz, Abtastrate 44100 Hz */
    FidFilter *ff  = fid_design("LpBu4/1000", 44100.0, -1.0, -1.0, 0, NULL);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    /* Impulsantwort: 2000 Samples; Energie endlich, kein Divergenz */
    double energy   = 0.0;
    double peak_late = 0.0;   /* max |out| nach Sample 500 */
    int    exploded  = 0;
    for (int t = 0; t < 2000; t++) {
        double in  = (t == 0) ? 1.0 : 0.0;
        double out = fn(buf, in);
        energy += out * out;
        if (t > 500) peak_late = fmax(peak_late, fabs(out));
        if (fabs(out) > 1e6) { exploded = 1; break; }
    }

    chk_range("LpBu4 impulse energy",    energy,    1e-10, 1e4);
    chk_range("LpBu4 late-stage decay",  peak_late, 0.0,   1e-4);

    if (!exploded)
        printf("PASS  %-52s  no divergence\n", "LpBu4 no explosion");
    else {
        fprintf(stderr, "FAIL  %-52s  output diverged (|out| > 1e6)\n", "LpBu4 no explosion");
        g_failed++;
    }

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── 3. Sinus-Durchlasstest: DC und Nyquist ─────────────────────────────── */

static void
test_fir_dc_nyquist(void)
{
#ifdef FIDLIB_PRECISION_F32
    const double TOL = 5e-5;
#else
    const double TOL = 1e-10;
#endif

    /* 16-tap Boxcar: DC gain = 1, Nyquist gain = 0 for even N */
    const int N = 16;
    const double w = 1.0 / (double)N;

    double arr[N + 3];
    arr[0] = (double)'F';
    arr[1] = (double)N;
    for (int i = 0; i < N; i++) arr[2 + i] = w;
    arr[2 + N] = 0.0;

    FidFilter *ff  = fid_cv_array(arr);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    /* DC: pumpe 200 Samples mit in=1.0, letzter Output soll 1.0 sein */
    double dc_out = 0.0;
    for (int t = 0; t < 200; t++) dc_out = fn(buf, 1.0);
    chk_near("boxcar16 DC gain", dc_out, 1.0, TOL);

    /* Nyquist (alternierend +1/-1): Output soll ~0 sein */
    fid_run_zapbuf(buf);
    double nyq_out = 0.0;
    for (int t = 0; t < 200; t++) nyq_out = fn(buf, (t % 2 == 0) ? 1.0 : -1.0);
    chk_near("boxcar16 Nyquist attenuation", nyq_out, 0.0, TOL);

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== FIR precision test (32-tap Boxcar) ===\n");
    test_fir_precision();
    printf("\n");

    printf("=== IIR stability test (LpBu4) ===\n");
    test_iir_stability();
    printf("\n");

    printf("=== FIR DC/Nyquist-Test (16-tap Boxcar) ===\n");
    test_fir_dc_nyquist();
    printf("\n");

    if (g_failed == 0)
        printf("ALL TESTS PASSED\n");
    else
        fprintf(stderr, "%d TEST(S) FAILED\n", g_failed);

    return g_failed ? 1 : 0;
}
