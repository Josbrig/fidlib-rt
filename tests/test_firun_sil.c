/*
 * test_firun_sil.c — SIL tests for firun as black-box (popen)
 *
 * Deckt ab: Impulsantwort, Frequenzgang (-r), Format Round-Trip (float64),
 *           Mehrkanaligkeit (-n 2).
 *
 * firun Syntax: firun [options] <rate> <in/out-format> <filter...>
 * Note: options (-d, -r, -n) MUST come before <rate>.
 *
 * FIRUN_BIN is passed as -DFIRUN_BIN="..." (CMake).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef FIRUN_BIN
#  error "FIRUN_BIN must be defined (path to firun binary)"
#endif

#define RATE  "44100"
#define SPEC  "LpBu4/400"
#define N_IMP  2000   /* impulse samples to generate */

static int g_failed = 0;

/* ── helpers ────────────────────────────────────────────────────────────── */

static int
chk(const char *lbl, double got, double lo, double hi)
{
    if (got >= lo && got <= hi) {
        printf("PASS  %-45s  %.8f  [%.6f,%.6f]\n", lbl, got, lo, hi);
        return 0;
    }
    fprintf(stderr, "FAIL  %-45s  %.8f  expected [%.6f,%.6f]\n",
            lbl, got, lo, hi);
    g_failed++;
    return 1;
}

static int
chk_true(const char *lbl, int cond)
{
    if (cond) { printf("PASS  %s\n", lbl); return 0; }
    fprintf(stderr, "FAIL  %s\n", lbl);
    g_failed++;
    return 1;
}


/* ══════════════════════════════════════════════════════════════════════════
 * 1. Impulse response — %I input, ASCII output
 *    Checks: peak in [0.001, 2.0], last sample near 0
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_impulse_ascii(void)
{
    puts("── firun: impulse response (ASCII) ──");

    char cmd[512];
    /* Options before rate: -d N */
    snprintf(cmd, sizeof(cmd),
             FIRUN_BIN " -d %d " RATE " %%I/a " SPEC, N_IMP);

    char (*lines)[256] = calloc((size_t)N_IMP + 10, 256);
    if (!lines) { fprintf(stderr, "FAIL  calloc\n"); g_failed++; return; }

    FILE *f = popen(cmd, "r");
    if (!f) {
        fprintf(stderr, "FAIL  popen failed: %s\n", cmd);
        g_failed++;
        free(lines);
        return;
    }
    int n = 0;
    while (n < N_IMP + 9 && fgets(lines[n], 256, f))
        n++;
    pclose(f);

    chk_true("impulse: got output",  n > 0);
    if (n <= 0) { free(lines); return; }

    /* Find peak amplitude */
    double peak = 0.0;
    for (int i = 0; i < n; i++) {
        double v;
        if (sscanf(lines[i], "%lf", &v) == 1 && fabs(v) > peak)
            peak = fabs(v);
    }
    chk("impulse peak in [0.001, 2.0]", peak, 0.001, 2.0);

    /* Last sample should be near zero — filter decayed */
    double last = 0.0;
    sscanf(lines[n - 1], "%lf", &last);
    chk("impulse tail (last sample) < 0.01", fabs(last), 0.0, 0.01);

    /* Total output lines matches requested count */
    chk_true("impulse: n_lines == N_IMP", n == N_IMP);

    free(lines);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 2. Frequency response — -r mode
 *    Output format "aa": frequency[Hz] response
 *    DC ≈ 1.0, fc ≈ 0.707, 10·fc < 0.01
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_freq_response(void)
{
    puts("── firun: frequency response (-r) ──");

    char cmd[256];
    /* "aa" = 2 ASCII channels: freq[Hz] and response amplitude */
    snprintf(cmd, sizeof(cmd),
             FIRUN_BIN " -r 1000 " RATE " aa " SPEC);

    char lines[1010][256];
    int n = 0;
    FILE *f = popen(cmd, "r");
    if (!f) { fprintf(stderr, "FAIL  popen: %s\n", cmd); g_failed++; return; }
    while (n < 1009 && fgets(lines[n], 256, f)) n++;
    pclose(f);

    chk_true("freq response: got output", n > 0);
    if (n <= 0) return;

    /* Line 0: freq=0 → response near 1.0 */
    double freq0 = 0.0, resp0 = 0.0;
    if (sscanf(lines[0], "%lf %lf", &freq0, &resp0) == 2)
        chk("freq resp at DC ≈ 1.0", resp0, 0.990, 1.010);
    else {
        fprintf(stderr, "FAIL  freq resp line[0] not parseable: '%s'\n", lines[0]);
        g_failed++;
    }

    /* Find line closest to fc = 400 Hz */
    double best_fc = 1e30, resp_fc = 0.0;
    for (int i = 0; i < n; i++) {
        double f_hz, r;
        if (sscanf(lines[i], "%lf %lf", &f_hz, &r) == 2) {
            double d = fabs(f_hz - 400.0);
            if (d < best_fc) { best_fc = d; resp_fc = r; }
        }
    }
    chk("freq resp at fc ≈ 0.707", resp_fc, 0.60, 0.80);

    /* Find line closest to 10·fc = 4000 Hz */
    double best_4k = 1e30, resp_4k = 0.0;
    for (int i = 0; i < n; i++) {
        double f_hz, r;
        if (sscanf(lines[i], "%lf %lf", &f_hz, &r) == 2) {
            double d = fabs(f_hz - 4000.0);
            if (d < best_4k) { best_4k = d; resp_4k = r; }
        }
    }
    chk("freq resp at 10·fc < 0.01", resp_4k, 0.0, 0.01);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 3. Format round-trip: float64 (%I/d → d/a via unity filter)
 *    ASCII output from direct path == ASCII from d→a path
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_roundtrip_float64(void)
{
    puts("── firun: format round-trip float64 ──");

    char cmd_direct[256], cmd_bin[256], cmd_rt[256];

    /* Direct ASCII */
    snprintf(cmd_direct, sizeof(cmd_direct),
             FIRUN_BIN " -d 10 " RATE " %%I/a " SPEC);

    /* Write float64 to tmp file */
    snprintf(cmd_bin, sizeof(cmd_bin),
             FIRUN_BIN " -d 10 " RATE " %%I/d " SPEC
             " > /tmp/firun_rt_test.bin");

    /* Read back via unity-gain FIR (x 1 = identity) */
    snprintf(cmd_rt, sizeof(cmd_rt),
             FIRUN_BIN " " RATE " d/a 'x 1' < /tmp/firun_rt_test.bin");

    /* Generate binary file first */
    FILE *fb = popen(cmd_bin, "r");
    if (fb) pclose(fb);

    /* Read direct lines */
    char lines_d[15][256];
    int nd = 0;
    FILE *fd = popen(cmd_direct, "r");
    if (fd) { while (nd < 14 && fgets(lines_d[nd], 256, fd)) nd++; pclose(fd); }

    /* Read round-trip lines */
    char lines_rt[15][256];
    int nrt = 0;
    FILE *fr = popen(cmd_rt, "r");
    if (fr) { while (nrt < 14 && fgets(lines_rt[nrt], 256, fr)) nrt++; pclose(fr); }

    chk_true("round-trip: direct has output",   nd  > 0);
    chk_true("round-trip: rt has output",        nrt > 0);
    if (nd <= 0 || nrt <= 0) return;

    int mismatches = 0;
    int compare = nd < nrt ? nd : nrt;
    for (int i = 0; i < compare; i++) {
        double vd = 0.0, vrt = 0.0;
        sscanf(lines_d[i],  "%lf", &vd);
        sscanf(lines_rt[i], "%lf", &vrt);
        if (fabs(vd - vrt) > 1e-9) {
            fprintf(stderr, "  mismatch at line %d: direct=%.12f rt=%.12f\n",
                    i, vd, vrt);
            mismatches++;
        }
    }
    chk_true("round-trip float64: no mismatches", mismatches == 0);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 4. Multi-channel: -n 2 with %I/aa → 2 values per line, both equal
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_multichannel(void)
{
    puts("── firun: multichannel (-n 2) ──");

    char cmd1[256], cmd2[256];

    /* 1 channel */
    snprintf(cmd1, sizeof(cmd1),
             FIRUN_BIN " -d 5 " RATE " %%I/a " SPEC);

    /* 2 channels: -n 2 expands 'a' to 'a2' for both in and out
     * But %I/aa needs explicit output format for 2 channels */
    snprintf(cmd2, sizeof(cmd2),
             FIRUN_BIN " -n 2 -d 5 " RATE " %%I/aa " SPEC);

    char lines1[8][256], lines2[8][256];
    int n1 = 0, n2 = 0;

    FILE *f1 = popen(cmd1, "r");
    if (f1) { while (n1 < 7 && fgets(lines1[n1], 256, f1)) n1++; pclose(f1); }
    FILE *f2 = popen(cmd2, "r");
    if (f2) { while (n2 < 7 && fgets(lines2[n2], 256, f2)) n2++; pclose(f2); }

    chk_true("1-ch: got output",    n1 > 0);
    chk_true("2-ch: got output",    n2 > 0);
    if (n1 <= 0 || n2 <= 0) return;

    /* 1-ch: line has exactly 1 value */
    double a1, b1, c1;
    int cnt1 = sscanf(lines1[0], "%lf %lf", &a1, &b1);
    chk_true("1-ch: one value per line",  cnt1 == 1);

    /* 2-ch: line has exactly 2 values */
    int cnt2 = sscanf(lines2[0], "%lf %lf %lf", &a1, &b1, &c1);
    chk_true("2-ch: two values per line", cnt2 == 2);

    /* Both channels same filter/input → identical values */
    if (cnt2 == 2)
        chk_true("2-ch: both channels equal", fabs(a1 - b1) < 1e-12);
}


/* ── main ────────────────────────────────────────────────────────────────── */

int
main(void)
{
    test_impulse_ascii();
    test_freq_response();
    test_roundtrip_float64();
    test_multichannel();

    if (g_failed == 0) {
        printf("\nALL PASSED (%s)\n", __FILE__);
        return 0;
    }
    fprintf(stderr, "\n%d FAILURE(S) in %s\n", g_failed, __FILE__);
    return 1;
}
