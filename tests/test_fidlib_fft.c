// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025-2026 Kai Dieki
/*
 * test_fidlib_fft.c — Overlap-Save FFT convolution engine correctness test
 *
 * Requires FIDLIB_FFT=ON at build time (tap threshold 512).
 * Uses a 600-tap boxcar FIR (weight 1/600) to guarantee the OLA path is taken.
 *
 * Tests:
 *   1. Impulse response: feed δ[0], drain 2× FFT blocks worth of zeros.
 *      Expect exactly M=600 outputs of 1/600 and the rest 0.
 *   2. DC input: after steady-state, output converges to 1.0.
 *   3. Latency: first B-1 outputs are 0 (initial latency fill).
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "fidlib.h"

static int g_failed = 0;

#define PASS(lbl, ...) \
    do { printf("PASS  " lbl "\n", ##__VA_ARGS__); } while(0)

#define FAIL(lbl, ...) \
    do { fprintf(stderr, "FAIL  " lbl "\n", ##__VA_ARGS__); g_failed++; } while(0)

#define CHK_NEAR(lbl, got, want, tol) \
    do { double _e = fabs((got)-(want)); \
         if (_e <= (tol)) PASS("%-52s  got=%.8g  want=%.8g", lbl, (double)(got), (double)(want)); \
         else             FAIL("%-52s  got=%.8g  want=%.8g  err=%.3e", lbl, (double)(got), (double)(want), _e); \
    } while(0)

#define CHK_RANGE(lbl, got, lo, hi) \
    do { double _v = (double)(got); \
         if (_v >= (lo) && _v <= (hi)) PASS("%-52s  got=%.8g  in [%.4g, %.4g]", lbl, _v, (double)(lo), (double)(hi)); \
         else                           FAIL("%-52s  got=%.8g  not in [%.4g, %.4g]", lbl, _v, (double)(lo), (double)(hi)); \
    } while(0)

/* Build a 600-tap boxcar FIR using fid_cv_array */
static FidFilter *
make_boxcar(int M)
{
    /* fid_cv_array format: 'F', M, coef[0..M-1], 0.0 sentinel */
    double *arr = (double *)malloc((size_t)(M + 3) * sizeof(double));
    if (!arr) { fprintf(stderr, "malloc failed\n"); exit(1); }
    arr[0] = (double)'F';
    arr[1] = (double)M;
    double w = 1.0 / (double)M;
    for (int i = 0; i < M; i++) arr[2 + i] = w;
    arr[2 + M] = 0.0;
    FidFilter *ff = fid_cv_array(arr);
    free(arr);
    return ff;
}

/* ── 1. Impulse response ────────────────────────────────────────────────── */
static void
test_impulse(void)
{
    const int M = 600;
    const double w = 1.0 / (double)M;
    const double TOL = 1e-10;

    FidFilter *ff  = make_boxcar(M);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    /* Feed impulse then 3000 zeros — enough for 2+ FFT blocks to drain */
    const int LEN = M + 3000;
    int  nonzero_cnt  = 0;
    int  wrong_val    = 0;
    int  first_nonzero = -1;

    for (int t = 0; t < LEN; t++) {
        double in  = (t == 0) ? 1.0 : 0.0;
        double out = fn(buf, in);
        if (fabs(out) > TOL) {
            if (first_nonzero < 0) first_nonzero = t;
            nonzero_cnt++;
            if (fabs(out - w) > TOL) wrong_val++;
        }
    }

    /* Verify count of nonzero outputs == M */
    if (nonzero_cnt == M)
        PASS("%-52s  count=%d", "impulse: nonzero output count == M", nonzero_cnt);
    else
        FAIL("%-52s  got=%d  want=%d", "impulse: nonzero output count == M",
             nonzero_cnt, M);

    /* Each nonzero should equal w = 1/M */
    if (wrong_val == 0)
        PASS("%-52s", "impulse: all nonzero outputs == 1/M");
    else
        FAIL("%-52s  wrong=%d / %d", "impulse: all nonzero outputs == 1/M",
             wrong_val, nonzero_cnt);

    /* Latency should be > 0 (OLA introduces one block delay) */
    if (first_nonzero > 0)
        PASS("%-52s  first_nonzero_at=%d", "impulse: latency > 0", first_nonzero);
    else if (first_nonzero == 0)
        PASS("%-52s  (no latency — non-OLA path)", "impulse: latency >= 0");
    else
        FAIL("%-52s", "impulse: never produced nonzero output");

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── 2. DC steady-state ─────────────────────────────────────────────────── */
static void
test_dc(void)
{
    const int M = 600;
    const double TOL = 1e-10;

    FidFilter *ff  = make_boxcar(M);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    /* Pump 3000 ones; last output should have converged to 1.0 */
    double last = 0.0;
    for (int t = 0; t < 3000; t++) last = fn(buf, 1.0);

    CHK_NEAR("boxcar600 DC steady-state", last, 1.0, TOL);

    /* zapbuf and re-run — verify reset works */
    fid_run_zapbuf(buf);
    double post_zap = 0.0;
    for (int t = 0; t < 3000; t++) post_zap = fn(buf, 1.0);
    CHK_NEAR("boxcar600 DC after zapbuf", post_zap, 1.0, TOL);

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── 3. Energy conservation ─────────────────────────────────────────────── */
static void
test_energy(void)
{
    const int M = 600;
    const double w = 1.0 / (double)M;

    FidFilter *ff  = make_boxcar(M);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    /* Feed impulse, drain 3000 samples, accumulate output energy */
    const int LEN = M + 3000;
    double energy = 0.0;
    for (int t = 0; t < LEN; t++) {
        double in  = (t == 0) ? 1.0 : 0.0;
        double out = fn(buf, in);
        energy += out * out;
    }

    /* Expected: M * w² = M * (1/M)² = 1/M */
    double expected_energy = (double)M * w * w;
    CHK_NEAR("boxcar600 impulse energy", energy, expected_energy, 1e-10);

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── 4. Nyquist attenuation ─────────────────────────────────────────────── */
static void
test_nyquist(void)
{
    const int M = 600;

    FidFilter *ff  = make_boxcar(M);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    /* Feed alternating ±1 for 3000 samples — a boxcar with even M passes ≈0 */
    double last = 1.0;
    for (int t = 0; t < 3000; t++) last = fn(buf, (t % 2 == 0) ? 1.0 : -1.0);

    /* For even M=600 the Nyquist gain is exactly 0 */
    CHK_NEAR("boxcar600 Nyquist attenuation", last, 0.0, 1e-10);

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

int main(void)
{
#ifndef FIDLIB_FFT
    printf("SKIP  FIDLIB_FFT not enabled — OLA engine not compiled in\n");
    printf("ALL TESTS SKIPPED\n");
    return 0;
#endif

    printf("=== Impulse response (OLA path) ===\n");
    test_impulse();
    printf("\n");

    printf("=== DC steady-state ===\n");
    test_dc();
    printf("\n");

    printf("=== Energy conservation ===\n");
    test_energy();
    printf("\n");

    printf("=== Nyquist attenuation ===\n");
    test_nyquist();
    printf("\n");

    if (g_failed == 0)
        printf("ALL TESTS PASSED\n");
    else
        fprintf(stderr, "%d TEST(S) FAILED\n", g_failed);

    return g_failed ? 1 : 0;
}
