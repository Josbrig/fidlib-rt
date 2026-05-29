/*
 * Test-only replacement for fiview/src/all.h.
 * Pre-included via -include to suppress SDL and provide stubs.
 * Defining ALL_H here makes the real all.h a no-op.
 */
#ifndef ALL_H
#define ALL_H

#define VERSION  "0.9.10"
#define PROGNAME "fiview"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <setjmp.h>

#ifndef NAN
#  define NAN (0.0/0.0)
#endif
#ifndef M_PI
#  define M_PI    3.14159265358979323846
#endif
#ifndef M_LN10
#  define M_LN10  2.30258509299404568402
#endif

#define M301DB  (0.707106781186548)
#define M602DB  (0.50)

#ifndef DEBUG_ON
#  define DEBUG_ON 0
#endif
#define DEBUG if (DEBUG_ON) warn

/* fidlib — use project-root-relative path: <fidlib/fidlib.h> */
#include <fidlib/fidlib.h>

/* fiview modules we compile into tests (no SDL, no fiview.h) */
#include "filter.h"
#include "scratch.h"
#include "display.h"

/* Macros from fiview.h */
#define ALLOC(type)          ((type*)Alloc(sizeof(type)))
#define ALLOC_ARR(cnt, type) ((type*)Alloc((size_t)(cnt) * sizeof(type)))

#ifdef __cplusplus
extern "C" {
#endif

/* Stubs provided by tests/support/stubs.c */
void  FID_NORETURN error(const char *fmt, ...);
void  warn(const char *fmt, ...);
void *Alloc(size_t size);
char *StrDup(const char *str);

/* Globals from fiview.c needed by filter.c */
extern double  s_rate;
extern double  a_f0, a_f1;
extern int     a_adj;
extern int     n_filt;
extern Filter *curr;

/* longjmp target for error-handler tests */
extern jmp_buf test_error_jmp;
extern int     test_error_triggered;

#ifdef __cplusplus
}
#endif

#endif /* ALL_H */
