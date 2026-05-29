// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025-2026 Kai Dieki
/*
 * test_fidlib_vulkan.c — Vulkan Compute FIR engine correctness test
 *
 * Skips gracefully when FIDLIB_VULKAN not compiled in.
 * When a GPU is available at runtime, fid_run_new auto-selects the Vulkan
 * path for tap counts >= FIDLIB_VULKAN_THRESHOLD. The test verifies output
 * correctness through the public API regardless of which path is taken.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "fidlib.h"

#ifdef FIDLIB_VULKAN
static int g_failed = 0;

static void
chk_near(const char *lbl, double got, double want, double tol)
{
    double e = fabs(got - want);
    if (e <= tol)
        printf("PASS  %-52s  got=%.8g\n", lbl, got);
    else {
        fprintf(stderr, "FAIL  %-52s  got=%.8g  want=%.8g  err=%.3e\n",
                lbl, got, want, e);
        g_failed++;
    }
}

static FidFilter *
make_boxcar(int M)
{
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

static void
test_impulse(int M)
{
    const double w   = 1.0 / (double)M;
    const double TOL = 1e-5;  /* FP32 GPU path: ~7 decimal digits */

    FidFilter *ff  = make_boxcar(M);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    const int LEN    = M + 3000;
    int  nonzero_cnt = 0;
    int  wrong_val   = 0;
    int  first_nz    = -1;

    for (int t = 0; t < LEN; t++) {
        double in  = (t == 0) ? 1.0 : 0.0;
        double out = fn(buf, in);
        if (fabs(out) > TOL * 0.1) {
            if (first_nz < 0) first_nz = t;
            nonzero_cnt++;
            if (fabs(out - w) > TOL) wrong_val++;
        }
    }

    if (nonzero_cnt == M)
        printf("PASS  %-52s  count=%d\n", "impulse: nonzero count == M", nonzero_cnt);
    else {
        fprintf(stderr, "FAIL  %-52s  got=%d  want=%d\n",
                "impulse: nonzero count == M", nonzero_cnt, M);
        g_failed++;
    }

    if (wrong_val == 0)
        printf("PASS  %-52s\n", "impulse: all nonzero outputs == 1/M");
    else {
        fprintf(stderr, "FAIL  %-52s  wrong=%d / %d  TOL=%.0e\n",
                "impulse: all nonzero outputs == 1/M", wrong_val, nonzero_cnt, TOL);
        g_failed++;
    }

    if (first_nz >= 0)
        printf("PASS  %-52s  first_nz_at=%d\n", "impulse: saw nonzero output", first_nz);
    else {
        fprintf(stderr, "FAIL  %-52s\n", "impulse: never saw nonzero output");
        g_failed++;
    }

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

static void
test_dc(int M)
{
    const double TOL = 1e-5;

    FidFilter *ff  = make_boxcar(M);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    double last = 0.0;
    for (int t = 0; t < 3000; t++) last = fn(buf, 1.0);
    chk_near("boxcar DC steady-state", last, 1.0, TOL);

    fid_run_zapbuf(buf);
    double post_zap = 0.0;
    for (int t = 0; t < 3000; t++) post_zap = fn(buf, 1.0);
    chk_near("boxcar DC after zapbuf", post_zap, 1.0, TOL);

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

#endif /* FIDLIB_VULKAN */

int main(void)
{
#ifndef FIDLIB_VULKAN
    printf("SKIP  FIDLIB_VULKAN not compiled in\n");
    printf("ALL TESTS SKIPPED\n");
    return 0;
#else
    const int M = FIDLIB_VULKAN_THRESHOLD + 1;
    printf("M=%d  threshold=%d\n\n", M, FIDLIB_VULKAN_THRESHOLD);

    printf("=== Impulse response ===\n");
    test_impulse(M);
    printf("\n");

    printf("=== DC steady-state + zapbuf ===\n");
    test_dc(M);
    printf("\n");

    if (g_failed == 0)
        printf("ALL TESTS PASSED\n");
    else
        fprintf(stderr, "%d TEST(S) FAILED\n", g_failed);

    return g_failed ? 1 : 0;
#endif
}
