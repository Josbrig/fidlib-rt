// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025-2026 Jörg Simbrig
/*
 * bench_fir_backends.c — FIR backend throughput benchmark
 *
 * Measures throughput (Msamples/s) for the FIR execution path selected at
 * compile time (NEON / OLA-FFT / Vulkan / OpenCL / scalar). The path is
 * auto-selected by fid_run_new() based on tap count and build options.
 *
 * Output: CSV to stdout + human-readable summary to stderr.
 * CSV columns: backend_flags, M, N_samples, time_ns, Msps, ns_per_sample
 *
 * Usage:
 *   ./bin/bench_fir_backends                  # default tap list
 *   ./bin/bench_fir_backends 16 64 512 2048   # custom tap counts
 *
 * Build:
 *   cmake -DBUILD_BENCHMARKS=ON [-DFIDLIB_FFT=ON] [-DFIDLIB_SIMD=ON] ...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "fidlib.h"

/* ── Timing ─────────────────────────────────────────────────────────────── */

static long long
now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ── Backend flags string ────────────────────────────────────────────────── */

static const char *
backend_flags(void)
{
    static char buf[128];
    buf[0] = '\0';
#ifdef FIDLIB_SIMD
#  ifdef FID_SIMD_NEON
    strcat(buf, "NEON");
#  elif defined(FID_SIMD_SSE2)
    strcat(buf, "SSE2");
#  else
    strcat(buf, "SIMD");
#  endif
#else
    strcat(buf, "scalar");
#endif
#ifdef FIDLIB_FFT
    strcat(buf, "+FFT");
#endif
#ifdef FIDLIB_VULKAN
    strcat(buf, "+VK");
#endif
#ifdef FIDLIB_OPENCL
    strcat(buf, "+OCL");
#endif
#ifdef FIDLIB_PRECISION_F32
    strcat(buf, "/FP32");
#else
    strcat(buf, "/FP64");
#endif
    return buf;
}

/* ── Make boxcar FIR filter ──────────────────────────────────────────────── */

static FidFilter *
make_boxcar(int M)
{
    double *arr = (double *)malloc((size_t)(M + 3) * sizeof(double));
    if (!arr) { perror("malloc"); exit(1); }
    arr[0] = (double)'F';
    arr[1] = (double)M;
    double w = 1.0 / (double)M;
    for (int i = 0; i < M; i++) arr[2 + i] = w;
    arr[2 + M] = 0.0;
    FidFilter *ff = fid_cv_array(arr);
    free(arr);
    return ff;
}

/* ── Single benchmark run ─────────────────────────────────────────────────── */

static void
bench_one(int M, int N_samples, const char *flags)
{
    FidFilter *ff  = make_boxcar(M);
    FidFunc   *fn  = NULL;
    void      *run = fid_run_new(ff, &fn);
    void      *buf = fid_run_newbuf(run);

    /* Warm-up: 10% of N_samples */
    int warmup = N_samples / 10;
    for (int t = 0; t < warmup; t++) fn(buf, (double)(t & 1));
    fid_run_zapbuf(buf);

    /* Timed run */
    double dummy = 0.0;
    long long t0 = now_ns();
    for (int t = 0; t < N_samples; t++) dummy += fn(buf, (double)(t & 1));
    long long t1 = now_ns();

    /* Keep dummy alive to prevent dead-code elimination */
    if (dummy != -9999.0) { /* always true */ }

    long long elapsed_ns = t1 - t0;
    double msps          = (double)N_samples / ((double)elapsed_ns * 1e-3);
    double ns_per_sample = (double)elapsed_ns / (double)N_samples;
    /* cast for printf format compatibility */
    long long elapsed_ns_ll = elapsed_ns;

    /* CSV */
    printf("%s,%d,%d,%lld,%.3f,%.3f\n",
           flags, M, N_samples, elapsed_ns_ll, msps, ns_per_sample);

    /* Human-readable to stderr */
    fprintf(stderr, "  M=%-6d  %8.2f Msps  %6.2f ns/sample\n",
            M, msps, ns_per_sample);

    fid_run_freebuf(buf);
    fid_run_free(run);
    free(ff);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *flags = backend_flags();

    /* Default tap sizes covering scalar < SIMD < OLA crossover ranges */
    static const int default_taps[] = { 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
    static const int default_n      = (int)(sizeof(default_taps) / sizeof(default_taps[0]));

    int   *taps;
    int    ntaps;
    int    buf[32];

    if (argc > 1) {
        ntaps = argc - 1;
        taps  = buf;
        if (ntaps > 32) ntaps = 32;
        for (int i = 0; i < ntaps; i++) taps[i] = atoi(argv[i + 1]);
    } else {
        ntaps = default_n;
        taps  = (int *)default_taps;
    }

    /* Header */
    printf("backend,M,N_samples,time_ns,Msps,ns_per_sample\n");
    fprintf(stderr, "=== bench_fir_backends  backend=%s ===\n", flags);

    for (int i = 0; i < ntaps; i++) {
        int M = taps[i];
        if (M < 1) continue;

        /* Choose N_samples to run for ~0.5s per configuration */
        /* Rough estimate: scalar ~100 Msps for small M, falls to ~1 Msps for large M */
        double target_ns  = 500000000.0; /* 0.5s */
        double est_ns_per = (double)M * 0.05 + 5.0;
        int N_samples = (int)(target_ns / est_ns_per);
        if (N_samples < 100000)   N_samples = 100000;
        if (N_samples > 50000000) N_samples = 50000000;

        fprintf(stderr, "M=%d  N=%d ...\n", M, N_samples);
        bench_one(M, N_samples, flags);
    }

    fprintf(stderr, "Done.\n");
    return 0;
}
