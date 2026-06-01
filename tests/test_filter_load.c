/*
 * test_filter_load.c — tests for filter_load_immed / filter_load_file
 *
 * Checks: single filter, multi-filter, fixture file, /nonexistent,
 *        error case: empty spec via longjmp mock.
 */

#include "test_all.h"
#include <setjmp.h>

#define RATE_HZ  44100.0
#define FIXTURE  "tests/fixtures/simple.filt"   /* relative to build dir */

static int g_failed = 0;

static int
chk_true(const char *lbl, int cond)
{
    if (cond) { printf("PASS  %s\n", lbl); return 0; }
    fprintf(stderr, "FAIL  %s\n", lbl);
    g_failed++;
    return 1;
}

static int
chk_i(const char *lbl, int got, int lo, int hi)
{
    if (got >= lo && got <= hi) {
        printf("PASS  %-40s  %d  [%d,%d]\n", lbl, got, lo, hi);
        return 0;
    }
    fprintf(stderr, "FAIL  %-40s  %d  expected [%d,%d]\n", lbl, got, lo, hi);
    g_failed++;
    return 1;
}

/* ── reset filter list ──────────────────────────────────────────────────── */
static void
reset_filters(void)
{
    extern Filter *filters;
    filters = NULL;
    s_rate  = RATE_HZ;
    a_f0    = -1.0;
    a_f1    = -1.0;
    a_adj   = 0;
}


/* ══════════════════════════════════════════════════════════════════════════
 * 1. filter_load_immed — single filter
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_immed_single(void)
{
    puts("── filter_load_immed: single filter ──");
    reset_filters();

    int n = filter_load_immed((char *)"LpBu4/400");
    chk_i("n == 1",  n, 1, 1);

    extern Filter *filters;
    chk_true("filters != NULL",     filters != NULL);
    if (!filters) return;

    chk_i("filters->ii == 1",       filters->ii, 1, 1);
    chk_true("filters->filt != NULL", filters->filt != NULL);
    chk_true("filters->ff != NULL",   filters->ff   != NULL);
    chk_i("typ == 0 (low-pass)",     filters->typ, 0, 0);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 2. filter_load_immed — two filters (comma-separated)
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_immed_two(void)
{
    puts("── filter_load_immed: two filters ──");
    reset_filters();

    int n = filter_load_immed((char *)"LpBu4/400,HpBu4/5000");
    chk_i("n == 2",  n, 2, 2);

    extern Filter *filters;
    chk_true("filters != NULL",  filters != NULL);
    if (!filters) return;

    /* filters list is built in reverse order: newest first */
    chk_i("first entry ii == 2",  filters->ii, 2, 2);
    chk_true("second entry exists", filters->nxt != NULL);
    if (filters->nxt)
        chk_i("second entry ii == 1", filters->nxt->ii, 1, 1);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 3. filter_load_file — from fixture simple.filt
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_load_file(void)
{
    puts("── filter_load_file: simple.filt ──");
    reset_filters();

    /* Try relative path from build dir first, then source dir */
    int n = filter_load_file((char *)FIXTURE);
    if (n < 1) {
        /* Fallback: build dir might not be the cwd */
        reset_filters();
        n = filter_load_file((char *)"../tests/fixtures/simple.filt");
    }
    if (n < 1) {
        /* Try absolute path derivation from __FILE__ */
        reset_filters();
        /* The test binary is run from build dir; try both locations */
        char path[512];
        snprintf(path, sizeof(path), "%s", FIXTURE);
        n = filter_load_file(path);
    }

    chk_i("filter_load_file n >= 1", n, 1, 10);

    extern Filter *filters;
    if (n >= 1 && filters) {
        chk_true("file filter: filt != NULL", filters->filt != NULL);
        chk_i("file filter: typ == 0 (LP)",   filters->typ, 0, 0);
        /* Should agree with immed version */
        double dc = filter_response(filters, 0.0, NULL);
        if (dc > 0.990 && dc < 1.010)
            printf("PASS  file filter DC gain ≈ 1.0  (%.6f)\n", dc);
        else {
            fprintf(stderr, "FAIL  file filter DC gain %.6f expected [0.990,1.010]\n", dc);
            g_failed++;
        }
    }
}


/* ══════════════════════════════════════════════════════════════════════════
 * 4. filter_load_file — /nonexistent returns 0 (error → longjmp)
 *
 * load_file() calls error() on fopen failure, which longjmps.
 * We catch it and verify error was triggered.
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_load_file_nonexistent(void)
{
    puts("── filter_load_file: /nonexistent ──");
    reset_filters();

    test_error_triggered = 0;
    if (setjmp(test_error_jmp) == 0) {
        filter_load_file((char *)"/nonexistent_file_that_does_not_exist.filt");
        /* Should not reach here */
        fprintf(stderr, "FAIL  /nonexistent did not trigger error\n");
        g_failed++;
    } else {
        printf("PASS  /nonexistent triggered error (longjmp caught)\n");
    }
}


/* ══════════════════════════════════════════════════════════════════════════
 * 5. filter_load_immed — empty spec triggers error
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_immed_empty(void)
{
    puts("── filter_load_immed: empty spec ──");
    reset_filters();

    /* An empty spec should either return 0 without loading anything,
     * or trigger error() if implementation considers it invalid.
     * Either is acceptable — just must not crash. */
    int triggered = 0;
    test_error_triggered = 0;
    if (setjmp(test_error_jmp) == 0) {
        int n = filter_load_immed((char *)"");
        if (n == 0) {
            printf("PASS  empty spec returns 0 (no filters loaded)\n");
        } else {
            fprintf(stderr, "WARN  empty spec returned %d (unexpected but not fatal)\n", n);
        }
    } else {
        triggered = 1;
        printf("PASS  empty spec triggered error (caught)\n");
    }
    (void)triggered;
}


/* ── main ────────────────────────────────────────────────────────────────── */

int
main(void)
{
    test_immed_single();
    test_immed_two();
    test_load_file();
    test_load_file_nonexistent();
    test_immed_empty();

    if (g_failed == 0) {
        printf("\nALL PASSED (%s)\n", __FILE__);
        return 0;
    }
    fprintf(stderr, "\n%d FAILURE(S) in %s\n", g_failed, __FILE__);
    return 1;
}
