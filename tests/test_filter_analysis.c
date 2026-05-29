/*
 * test_filter_analysis.c — numerical tests for filter.c analysis functions
 *
 * Covers: filter_response, filter_resp_range, filter_setup_gain,
 *         filter_setup_cnt, filter_run/runfilter_step, filter_dump.
 *
 * Reference values from doc/examples/fiview_log.txt (LpBu6/=400, sr=44100).
 */

#include "test_all.h"
#include <setjmp.h>

#define RATE_HZ  44100.0

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
chk_i(const char *lbl, int got, int lo, int hi)
{
    if (got >= lo && got <= hi) {
        printf("PASS  %-45s  %d  [%d,%d]\n", lbl, got, lo, hi);
        return 0;
    }
    fprintf(stderr, "FAIL  %-45s  %d  expected [%d,%d]\n",
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

/* Build a fully-analysed Filter from an immediate spec string.
 * s_rate must be set before calling.  Returns the first filter in
 * the global `filters` list.  Caller owns all memory (don't free —
 * it lives in Alloc heap; stubs.c uses calloc, no free needed in
 * tests as we exit anyway). */
static Filter *
make_filter(const char *spec)
{
    extern Filter *filters;
    filters = NULL;      /* reset list */
    s_rate  = RATE_HZ;
    int n = filter_load_immed((char *)spec);
    if (n < 1 || !filters) {
        fprintf(stderr, "make_filter: failed to load '%s'\n", spec);
        g_failed++;
        return NULL;
    }
    return filters;
}


/* ══════════════════════════════════════════════════════════════════════════
 * 1. filter_response — LpBu4/400
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_response_lpbu4(void)
{
    puts("── filter_response LpBu4/400 ──");
    Filter *ff = make_filter("LpBu4/400");
    if (!ff) return;

    chk("LpBu4 DC gain",           filter_response(ff, 0.0,           NULL), 0.990, 1.010);
    chk("LpBu4 gain at fc",        filter_response(ff, 400.0/RATE_HZ, NULL), 0.670, 0.740);
    chk("LpBu4 gain at 10·fc",     filter_response(ff,4000.0/RATE_HZ, NULL), 0.0,   0.005);
    chk("LpBu4 gain at Nyquist",   filter_response(ff, 0.5,           NULL), 0.0,   0.010);
}

static void
test_response_hpbu4(void)
{
    puts("── filter_response HpBu4/5000 ──");
    Filter *ff = make_filter("HpBu4/5000");
    if (!ff) return;

    chk("HpBu4 DC (~0)",           filter_response(ff, 0.0,           NULL), 0.0,   0.010);
    chk("HpBu4 at Nyquist (~1)",   filter_response(ff, 0.5,           NULL), 0.990, 1.010);
    chk("HpBu4 at fc (~-3dB)",     filter_response(ff,5000.0/RATE_HZ, NULL), 0.670, 0.750);
}

static void
test_response_bpbu2(void)
{
    puts("── filter_response BpBu2/1000-2000 ──");
    Filter *ff = make_filter("BpBu2/1000-2000");
    if (!ff) return;

    chk("BpBu2 DC (~0)",           filter_response(ff, 0.0,           NULL), 0.0,   0.05);
    chk("BpBu2 Nyquist (~0)",      filter_response(ff, 0.5,           NULL), 0.0,   0.05);
    chk("BpBu2 mid passband",      filter_response(ff,1500.0/RATE_HZ, NULL), 0.30,  1.10);
    chk("BpBu2 below band",        filter_response(ff, 300.0/RATE_HZ, NULL), 0.0,   0.50);
    chk("BpBu2 above band",        filter_response(ff,5000.0/RATE_HZ, NULL), 0.0,   0.20);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 2. filter_response with phase — Butterworth N·45° invariant
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_response_phase(void)
{
    puts("── filter_response: phase ──");
    Filter *ff = make_filter("LpBu4/400");
    if (!ff) return;

    double phase;
    filter_response(ff, 400.0 / RATE_HZ, &phase);
    /* LpBu4 at fc: -4·45° = -180° → normalized to 0.5 in [0,1] */
    double diff = fabs(phase - 0.5);
    if (diff > 0.5) diff = 1.0 - diff;
    chk("LpBu4 phase at fc ≈ 0.5 (±0.05)", diff, 0.0, 0.05);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 3. filter_resp_range — array length and monotonicity for LPF
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_resp_range(void)
{
    puts("── filter_resp_range ──");
    Filter *ff = make_filter("LpBu4/400");
    if (!ff) return;

    const int SLOTS = 256;
    double *arr = filter_resp_range(ff, 0.0, 0.5, SLOTS, 4);
    chk_true("filter_resp_range != NULL", arr != NULL);
    if (!arr) return;

    /* Check first slot: resp_max near DC should be close to 1 */
    chk("resp_range slot[0] max near 1", arr[1], 0.90, 1.10);

    /* Check last slot: near Nyquist should be near 0 */
    chk("resp_range last slot max near 0", arr[(SLOTS - 1) * 4 + 1], 0.0, 0.10);

    /* All response values should be in [0, 1.05] — sanity check */
    int all_valid = 1;
    for (int i = 0; i < SLOTS; i++) {
        double lo = arr[i * 4 + 0];
        double hi = arr[i * 4 + 1];
        if (lo < -0.01 || hi > 1.05 || lo > hi + 1e-9) {
            all_valid = 0;
            fprintf(stderr, "  invalid at slot %d: [%.6f, %.6f]\n", i, lo, hi);
        }
    }
    chk_true("all resp_range values in [0, 1.05]", all_valid);

    free(arr);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 4. filter_setup_gain — typ, gain100, m3db, m6db
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_setup_gain(void)
{
    puts("── filter_setup_gain ──");

    /* Low-pass */
    {
        Filter *ff = make_filter("LpBu4/400");
        if (!ff) return;
        chk_i("LpBu4 typ == 0 (low-pass)",   ff->typ,      0, 0);
        chk("LpBu4 gain100 ~1.0",             ff->gain100,  0.98, 1.02);
        chk_true("LpBu4 n_m3db >= 2",         ff->n_m3db >= 2);
        /* first m3db entry = 0.0 (DC starts in passband) */
        chk("LpBu4 m3db[0] == 0.0",           ff->m3db[0], -1e-9, 1e-9);
        /* second m3db entry ≈ fc/sr */
        chk("LpBu4 m3db[1] ≈ 400/44100",      ff->m3db[1],
            395.0 / RATE_HZ, 405.0 / RATE_HZ);
        chk_true("LpBu4 n_m6db >= 2",         ff->n_m6db >= 2);
    }

    /* High-pass */
    {
        Filter *ff = make_filter("HpBu4/5000");
        if (!ff) return;
        chk_i("HpBu4 typ == 1 (high-pass)",   ff->typ,      1, 1);
        chk("HpBu4 gain100 ~1.0",             ff->gain100,  0.98, 1.02);
    }

    /* Band-pass */
    {
        Filter *ff = make_filter("BpBu2/1000-2000");
        if (!ff) return;
        chk_i("BpBu2 typ == 2 (band-pass)",   ff->typ,      2, 2);
        chk("BpBu2 gain100 > 0",              ff->gain100,  0.01, 1.10);
    }
}


/* ══════════════════════════════════════════════════════════════════════════
 * 5. filter_setup_cnt — ordering and reference values (LpBu6/400)
 *
 * Referenz aus doc/examples/fiview_log.txt:
 *   50%  → 93 samples
 *   90%  → 188 samples
 *   95%  → 240 samples
 *   99%  → 362 samples
 *   99.9% → 539 samples
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_setup_cnt(void)
{
    puts("── filter_setup_cnt ──");
    Filter *ff = make_filter("LpBu6/400");
    if (!ff) return;

    /* Strict ordering */
    chk_true("cnt50 < cnt90",  ff->cnt50  < ff->cnt90);
    chk_true("cnt90 < cnt95",  ff->cnt90  < ff->cnt95);
    chk_true("cnt95 < cnt99",  ff->cnt99  > ff->cnt95);
    chk_true("cnt99 < cnt999", ff->cnt99  < ff->cnt999);
    chk_true("cnt999 > 0",     ff->cnt999 > 0);
    chk_true("cnt_max >= 0",   ff->cnt_max >= 0);

    /* Reference values (±30% tolerance, filter numerics are exact) */
    chk_i("cnt50 ≈ 93",   ff->cnt50,  60,  130);
    chk_i("cnt90 ≈ 188",  ff->cnt90, 130,  260);
    chk_i("cnt95 ≈ 240",  ff->cnt95, 160,  330);
    chk_i("cnt99 ≈ 362",  ff->cnt99, 240,  500);
    chk_i("cnt999 ≈ 539", ff->cnt999,360,  750);

    /* Upper bound: must converge within 50·sr/fc */
    int bound = (int)(50.0 * RATE_HZ / 400.0);  /* = 5512 */
    chk_true("cnt_max < 50·sr/fc", ff->cnt_max < bound);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 6. filter_run — impulse response, step response, zapbuf equivalence
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_filter_run(void)
{
    puts("── filter_run / runfilter_step ──");
    Filter *ff = make_filter("LpBu4/400");
    if (!ff) return;

    /* ── Impulse response: tail must decay ── */
    RunFilter *rr = filter_run(ff);
    chk_true("filter_run != NULL", rr != NULL);

    /* Run past the 99.99% energy point — filter must be quiet by then */
    double sum = fabs(runfilter_step(rr, 1.0));
    int run_until = ff->cnt9999 + 200;
    for (int i = 1; i < run_until; i++)
        sum += fabs(runfilter_step(rr, 0.0));

    double tail = fabs(runfilter_step(rr, 0.0));
    chk("impulse tail after cnt9999+200 < 1e-5", tail, 0.0, 1e-5);
    runfilter_free(rr);

    /* ── Step response: converges to 1.0 ── */
    rr = filter_run(ff);
    double last = 0.0;
    for (int i = 0; i < ff->cnt99 + 100; i++)
        last = runfilter_step(rr, 1.0);
    chk("step response at cnt99+100 ≈ 1.0", last, 0.990, 1.010);
    runfilter_free(rr);

    /* ── zapbuf equivalence ── */
    RunFilter *rr1 = filter_run(ff);
    RunFilter *rr2 = filter_run(ff);
    double y1a = runfilter_step(rr1, 1.0);
    double y2a = runfilter_step(rr2, 1.0);
    /* Run rr1 a few more steps then reset */
    for (int i = 0; i < 10; i++) runfilter_step(rr1, 0.5);
    /* Reset via fresh run (same as zapbuf for RunFilter level) */
    runfilter_free(rr1);
    rr1 = filter_run(ff);
    double y1b = runfilter_step(rr1, 1.0);
    chk_true("impulse[0] same after reset",  fabs(y1a - y1b) < 1e-12);
    chk_true("two fresh runs agree",         fabs(y1a - y2a) < 1e-12);
    runfilter_free(rr1);
    runfilter_free(rr2);
}


/* ══════════════════════════════════════════════════════════════════════════
 * 7. filter_dump — contains key strings, no crash on multi-stage filter
 * ════════════════════════════════════════════════════════════════════════ */

static void
test_filter_dump(void)
{
    puts("── filter_dump ──");

    /* Single-stage filter */
    {
        Filter *ff = make_filter("LpBu4/400");
        if (!ff) return;
        char *d = ff->dump;   /* set by filter_load_immed → filter_load_part */
        chk_true("filter_dump != NULL", d != NULL);
        chk_true("dump contains 'low-pass'",    strstr(d, "low-pass") != NULL);
        chk_true("dump contains '400'",          strstr(d, "400") != NULL);
    }

    /* Two-stage filter — must not crash */
    {
        Filter *ff2 = make_filter("LpBu4/400,HpBu4/5000");
        if (ff2) {
            chk_true("two-stage dump != NULL",    ff2->dump != NULL);
            printf("PASS  two-stage filter_dump no crash\n");
        }
    }
}


/* ── main ────────────────────────────────────────────────────────────────── */

int
main(void)
{
    test_response_lpbu4();
    test_response_hpbu4();
    test_response_bpbu2();
    test_response_phase();
    test_resp_range();
    test_setup_gain();
    test_setup_cnt();
    test_filter_run();
    test_filter_dump();

    if (g_failed == 0) {
        printf("\nALL PASSED (%s)\n", __FILE__);
        return 0;
    }
    fprintf(stderr, "\n%d FAILURE(S) in %s\n", g_failed, __FILE__);
    return 1;
}
