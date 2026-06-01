/*
 * test_scratch.c — unit tests for fiview/src/scratch.c
 *
 * Jede Funktion/Macro wird in Isolation getestet.
 * stubs.c liefert error() (longjmp-basiert) und die Globals.
 */

#include "test_all.h"
#include <setjmp.h>
#include <assert.h>

static int g_failed = 0;

static int
chk(const char *lbl, int cond)
{
    if (cond) {
        printf("PASS  %s\n", lbl);
        return 0;
    }
    fprintf(stderr, "FAIL  %s\n", lbl);
    g_failed++;
    return 1;
}

static int
chk_str(const char *lbl, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) {
        printf("PASS  %s\n", lbl);
        return 0;
    }
    fprintf(stderr, "FAIL  %s  got=\"%s\"  want=\"%s\"\n", lbl, got, want);
    g_failed++;
    return 1;
}

static int
chk_d(const char *lbl, double got, double lo, double hi)
{
    if (got >= lo && got <= hi) {
        printf("PASS  %-40s  %.8f\n", lbl, got);
        return 0;
    }
    fprintf(stderr, "FAIL  %-40s  %.8f  expected [%.6f, %.6f]\n",
            lbl, got, lo, hi);
    g_failed++;
    return 1;
}


/* ── 1. scr_zap ─────────────────────────────────────────────────────────── */

static void
test_zap(void)
{
    puts("── scr_zap ──");
    scr_zap();
    chk("scr_len == 0 after zap",   scr_len == 0);
    chk("scr_wid == 78",             scr_wid == 78);
    chk("scr_indlen == 0",           scr_indlen == 0);
    /* Writing the empty string forces NUL at position 0 */
    scr_pr("");
    chk("scratch[0] == '\\0' after pr(\"\")", scratch[0] == '\0');
}


/* ── 2. scr_pr basic formatting ─────────────────────────────────────────── */

static void
test_pr(void)
{
    puts("── scr_pr ──");

    scr_zap();
    scr_pr("hello");
    chk_str("scr_pr literal",    scratch, "hello");
    chk("scr_len == 5",          scr_len == 5);

    scr_pr(" world");
    chk_str("scr_pr concat",     scratch, "hello world");

    scr_zap();
    scr_pr("%d", 42);
    chk_str("scr_pr int",        scratch, "42");

    scr_zap();
    scr_pr("%.3f", 3.14159);
    chk_str("scr_pr float",      scratch, "3.142");

    scr_zap();
    scr_pr("%s/%s", "foo", "bar");
    chk_str("scr_pr two strings", scratch, "foo/bar");
}


/* ── 3. scr_lf ──────────────────────────────────────────────────────────── */

static void
test_lf(void)
{
    puts("── scr_lf ──");
    scr_zap();
    scr_pr("line");
    scr_lf();
    chk("scr_lf appends '\\n'", scratch[scr_len - 1] == '\n');
    chk("scr_lf NUL-terminates", scratch[scr_len] == '\0');
}


/* ── 4. SCR_PUTC ────────────────────────────────────────────────────────── */

static void
test_putc(void)
{
    puts("── SCR_PUTC ──");
    scr_zap();
    SCR_PUTC('A');
    SCR_PUTC('B');
    SCR_PUTC('C');
    chk_str("SCR_PUTC abc",      scratch, "ABC");
    chk("SCR_PUTC NUL-term",     scratch[scr_len] == '\0');
}


/* ── 5. scr_zap_pr ──────────────────────────────────────────────────────── */

static void
test_zap_pr(void)
{
    puts("── scr_zap_pr ──");
    scr_zap();
    scr_pr("old content");
    scr_zap_pr("fresh %d", 99);
    chk_str("scr_zap_pr clears and writes", scratch, "fresh 99");
}


/* ── 6. scr_prw — word wrap ─────────────────────────────────────────────── */

static int
max_line_len(const char *s)
{
    int maxlen = 0, cur = 0;
    for (; *s; s++) {
        if (*s == '\n') {
            if (cur > maxlen) maxlen = cur;
            cur = 0;
        } else {
            cur++;
        }
    }
    if (cur > maxlen) maxlen = cur;
    return maxlen;
}

static void
test_prw(void)
{
    puts("── scr_prw ──");

    /* Normal wrap */
    scr_zap();
    scr_wrap(30, "  ");
    scr_prw("this is a long sentence that must be wrapped at thirty characters");
    chk("prw: max line <= 30", max_line_len(scratch) <= 30);
    /* Continuation lines start with "  " */
    char *nl = strchr(scratch, '\n');
    if (nl && *(nl + 1)) {
        chk("prw: continuation starts with spaces",
            (nl[1] == ' ' && nl[2] == ' '));
    }

    /* Empty string — no crash */
    scr_zap();
    scr_wrap(40, "");
    scr_prw("");
    chk("prw: empty string no crash", scr_len == 0);

    /* Word longer than line width — no infinite loop, no UB */
    scr_zap();
    scr_wrap(10, "");
    scr_prw("averylongwordthatdoesnotfit");
    chk("prw: long word does not crash", scr_len > 0);
    chk("prw: long word NUL-terminated", scratch[scr_len] == '\0');

    /* Single word exactly at width — no spurious wrap */
    scr_zap();
    scr_wrap(5, "> ");
    scr_prw("hello");
    chk("prw: exact-width word no wrap", strchr(scratch, '\n') == NULL);
}


/* ── 7. scr_inc / scr_wrD / scr_wrI (binary data) ──────────────────────── */

static void
test_binary(void)
{
    puts("── scr_inc / scr_wrD / scr_wrI ──");

    scr_zap();
    void *p = scr_inc(16);
    chk("scr_inc != NULL",        p != NULL);
    chk("scr_inc len advanced",   scr_len == 16);
    /* Memory must be zeroed */
    unsigned char *bp = (unsigned char *)p;
    int all_zero = 1;
    for (int i = 0; i < 16; i++) if (bp[i]) { all_zero = 0; break; }
    chk("scr_inc zeroed",          all_zero);

    /* scr_wrD round-trip */
    scr_zap();
    double dval = 3.141592653589793;
    scr_wrD(dval);
    chk("scr_wrD len == sizeof(double)", scr_len == (int)sizeof(double));
    double dout;
    memcpy(&dout, scratch, sizeof(double));
    chk_d("scr_wrD round-trip", dout, dval - 1e-15, dval + 1e-15);

    /* scr_wrI round-trip */
    scr_zap();
    int ival = (int)0xDEADBEEF;
    scr_wrI(ival);
    chk("scr_wrI len == sizeof(int)", scr_len == (int)sizeof(int));
    int iout;
    memcpy(&iout, scratch, sizeof(int));
    chk("scr_wrI round-trip", iout == ival);
}


/* ── 8. scr_dup ─────────────────────────────────────────────────────────── */

static void
test_dup(void)
{
    puts("── scr_dup ──");
    scr_zap_pr("duplicate me");
    char *s = scr_dup();
    chk("scr_dup != NULL",            s != NULL);
    chk_str("scr_dup content",        s, "duplicate me");
    chk("scr_dup is independent copy", s != scratch);
    free(s);

    /* Binary round-trip */
    scr_zap();
    scr_wrI((int)0x12345678);
    scr_wrI((int)0xABCDEF01);
    char *bin = scr_dup();
    chk("scr_dup binary len == 2*sizeof(int)", scr_len == 2 * (int)sizeof(int));
    int a, b;
    memcpy(&a, bin,                  sizeof(int));
    memcpy(&b, bin + sizeof(int),    sizeof(int));
    chk("scr_dup binary[0]", a == (int)0x12345678);
    chk("scr_dup binary[1]", b == (int)0xABCDEF01);
    free(bin);
}


/* ── 9. scr_realloc — capacity growth ──────────────────────────────────── */

static void
test_realloc_growth(void)
{
    puts("── scr_realloc capacity growth ──");

    /* Write 40000 bytes to force at least one doubling past 32768 */
    scr_zap();
    for (int i = 0; i < 40000; i++)
        SCR_PUTC((char)('A' + (i % 26)));

    chk("after 40000 chars: scr_len == 40000", scr_len == 40000);
    chk("scr_max >= 65536", scr_max >= 65536);
    chk("NUL-terminated", scratch[scr_len] == '\0');

    /* Verify a sample of the content */
    chk("scratch[0] == 'A'",          scratch[0] == 'A');
    chk("scratch[25] == 'Z'",         scratch[25] == 'Z');
    chk("scratch[39999] correct",
        scratch[39999] == 'A' + (39999 % 26));
}


/* ── 10. scr_wrap state ──────────────────────────────────────────────────── */

static void
test_wrap_state(void)
{
    puts("── scr_wrap state ──");
    scr_zap();
    scr_wrap(60, "    ");
    chk("scr_wid == 60",       scr_wid == 60);
    chk("scr_indlen == 4",     scr_indlen == 4);
    chk("scr_ind[0] == ' '",   scr_ind[0] == ' ');
}


/* ── main ────────────────────────────────────────────────────────────────── */

int
main(void)
{
    test_zap();
    test_pr();
    test_lf();
    test_putc();
    test_zap_pr();
    test_prw();
    test_binary();
    test_dup();
    test_realloc_growth();
    test_wrap_state();

    if (g_failed == 0) {
        printf("\nALL PASSED (%s)\n", __FILE__);
        return 0;
    }
    fprintf(stderr, "\n%d FAILURE(S) in %s\n", g_failed, __FILE__);
    return 1;
}
