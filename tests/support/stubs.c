/*
 * Stub implementations for fiview.c globals and utility functions.
 * Linked into every test that compiles fiview source modules.
 */
#include "test_all.h"

/* ── fiview.c globals ───────────────────────────────────────────────────── */

double  s_rate = 44100.0;
double  a_f0   = -1.0;
double  a_f1   = -1.0;
int     a_adj  = 0;
int     n_filt = 0;
Filter *curr   = NULL;

/* ── longjmp state for error-handler tests ──────────────────────────────── */

jmp_buf test_error_jmp;
int     test_error_triggered = 0;

/* ── error() — longjmps when test sets up a jmp_buf ────────────────────── */

void
error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    test_error_triggered = 1;
    longjmp(test_error_jmp, 1);
}

/* ── warn() — silenced in tests ─────────────────────────────────────────── */

void
warn(const char *fmt, ...)
{
    (void)fmt;
}

/* ── Alloc() — checked malloc ───────────────────────────────────────────── */

void *
Alloc(size_t size)
{
    void *p = calloc(1, size);
    if (!p) {
        fprintf(stderr, "Alloc: out of memory\n");
        abort();
    }
    return p;
}

/* ── StrDup() — checked strdup ──────────────────────────────────────────── */

char *
StrDup(const char *str)
{
    char *p = strdup(str);
    if (!p) {
        fprintf(stderr, "StrDup: out of memory\n");
        abort();
    }
    return p;
}

/* ── display.c stubs (progress bar — no SDL needed in tests) ────────────── */

void
progress_init(Progress *pr, int max, const char *txt, int wid)
{
    (void)txt; (void)wid;
    pr->cnt   = 0;
    pr->upd   = 1;
    pr->max   = max;
    pr->force = 0;
}

void
progress_update(Progress *pr)
{
    (void)pr;
}
