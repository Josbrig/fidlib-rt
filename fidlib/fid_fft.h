// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025-2026 Kai Dieki
/**
 * @file fid_fft.h
 * @brief Overlap-Save FFT convolution engine for long FIR filters.
 *
 * Included by fidrf_cmdlist.h when FIDLIB_FFT is defined.
 * Provides RunOLA / RunOLABuf and filter_step_ola().
 *
 * Algorithm: Overlap-Save (OLS) with radix-2 Cooley-Tukey FFT.
 * - FFT size N = next power of 2 >= 2*M (M = tap count)
 * - Block size B = N - M + 1
 * - H[k] = forward FFT of zero-padded impulse response (precomputed once)
 * - Per block: FFT(x) → multiply H → IFFT → output last B samples
 * - Inherent latency: B samples (one full block)
 *
 * IIR filters are never dispatched here (OLA is FIR-only).
 *
 * @ingroup fidlib_run
 */

#ifndef FID_FFT_H
#define FID_FFT_H

#include <math.h>
#include <string.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/* ── Magic sentinels ────────────────────────────────────────────────────── */

#define RUN_MAGIC_OLA    0x4F4C4100u  /* RunOLA shared state */
#define RUNBUF_MAGIC_OLA 0x4F4C4201u  /* RunOLABuf per-channel state */

/* ── Shared read-only filter state ──────────────────────────────────────── */

/**
 * @brief Shared read-only OLA state (one per filter design).
 *
 * Memory layout: sizeof(RunOLA) + 2*N*sizeof(double).
 * H (2*N interleaved complex doubles) follows the struct immediately.
 */
typedef struct RunOLA {
    int magic; /**< RUN_MAGIC_OLA */
    int N;     /**< FFT size (power of 2, >= 2*M) */
    int M;     /**< FIR tap count */
    int B;     /**< block size = N - M + 1 */
    /* double H[2*N] follows (interleaved re/im), accessed via OLA_H() */
} RunOLA;

#define OLA_H(ro) ((double *)((ro) + 1))

/* ── Per-channel mutable state ───────────────────────────────────────────── */

/**
 * @brief Per-channel OLA state.
 *
 * Memory layout after header: x[N], y[B], z[2*N].
 * The type_tag field lets fid_run_zapbuf() distinguish this from RunBuf
 * without requiring a back-pointer dereference.
 */
typedef struct RunOLABuf {
    unsigned int type_tag; /**< RUNBUF_MAGIC_OLA — type discriminant */
    RunOLA      *ola;
    int          in_pos;    /**< new samples buffered since last block (0..B-1) */
    int          out_pos;   /**< next read index in y[] */
    int          out_avail; /**< valid samples waiting in y[] */
    double      *x;         /**< input + overlap buffer [N] */
    double      *y;         /**< output buffer [B] */
    double      *z;         /**< FFT scratch buffer [2*N], interleaved re/im */
} RunOLABuf;

/* ── Radix-2 Cooley-Tukey DIT FFT ───────────────────────────────────────── */

/**
 * In-place FFT on z[2*N] (interleaved complex doubles).
 * inv=0: forward  X[k] = Σ x[n]·exp(-2πi·n·k/N)
 * inv=1: inverse  x[n] = (1/N)·Σ X[k]·exp(+2πi·n·k/N)
 */
static void
ola_fft(double *z, int N, int inv)
{
    /* bit-reversal permutation */
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double tr = z[2*i], ti = z[2*i+1];
            z[2*i]   = z[2*j];   z[2*i+1] = z[2*j+1];
            z[2*j]   = tr;        z[2*j+1] = ti;
        }
    }
    /* butterfly stages */
    for (int len = 2; len <= N; len <<= 1) {
        double ang  = (inv ? 2.0 : -2.0) * M_PI / (double)len;
        double wre  = cos(ang), wim = sin(ang);
        for (int i = 0; i < N; i += len) {
            double cur_re = 1.0, cur_im = 0.0;
            for (int j = 0; j < len / 2; j++) {
                int    u   = i + j, v = i + j + len / 2;
                double ure = z[2*u], uim = z[2*u+1];
                double vre = cur_re * z[2*v] - cur_im * z[2*v+1];
                double vim = cur_re * z[2*v+1] + cur_im * z[2*v];
                z[2*u]   = ure + vre;  z[2*u+1] = uim + vim;
                z[2*v]   = ure - vre;  z[2*v+1] = uim - vim;
                double nr = cur_re * wre - cur_im * wim;
                double ni = cur_re * wim + cur_im * wre;
                cur_re = nr; cur_im = ni;
            }
        }
    }
    if (inv) {
        double inv_N = 1.0 / (double)N;
        for (int i = 0; i < 2 * N; i++) z[i] *= inv_N;
    }
}

/* ── Helper ─────────────────────────────────────────────────────────────── */

static int
ola_next_pow2(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

/* ── filter_step_ola ─────────────────────────────────────────────────────── */

/**
 * @brief Sample-by-sample OLA filter step.
 *
 * Accepts one input sample, returns one output sample.
 * During the first B-sample latency period, returns 0.0.
 * @rtSafe  No heap operations.
 */
static double
filter_step_ola(void *rbuf, double in)
{
    RunOLABuf *rb = (RunOLABuf *)rbuf;
    RunOLA    *ro = rb->ola;

    /* store incoming sample at the B-segment of the input buffer */
    rb->x[ro->M - 1 + rb->in_pos] = in;
    rb->in_pos++;

    /* when a full block of B new samples is ready, process */
    if (rb->in_pos == ro->B) {
        const double *H = OLA_H(ro);
        int N = ro->N;
        double *z = rb->z;

        /* load x into complex scratch (imaginary = 0) */
        for (int k = 0; k < N; k++) {
            z[2*k]   = rb->x[k];
            z[2*k+1] = 0.0;
        }

        ola_fft(z, N, 0);            /* forward FFT */

        /* complex multiply Z[k] *= H[k] */
        for (int k = 0; k < N; k++) {
            double ar = z[2*k], ai = z[2*k+1];
            double br = H[2*k], bi = H[2*k+1];
            z[2*k]   = ar * br - ai * bi;
            z[2*k+1] = ar * bi + ai * br;
        }

        ola_fft(z, N, 1);            /* inverse FFT */

        /* copy the B valid output samples (discard first M-1 corrupted) */
        for (int k = 0; k < ro->B; k++)
            rb->y[k] = z[2 * (ro->M - 1 + k)];

        /* slide overlap: keep last M-1 input samples for next block */
        memmove(rb->x, rb->x + ro->B, (size_t)(ro->M - 1) * sizeof(double));

        rb->in_pos    = 0;
        rb->out_pos   = 0;
        rb->out_avail = ro->B;
    }

    if (rb->out_avail > 0) {
        rb->out_avail--;
        return rb->y[rb->out_pos++];
    }
    return 0.0; /* latency fill */
}

/* ── ola_run_new ─────────────────────────────────────────────────────────── */

/**
 * Creates a RunOLA from a pure-FIR FidFilter chain.
 * Returns NULL if tap extraction fails (caller falls through to Run path).
 */
static void *
ola_run_new(const FidFilter *filt, double (**funcpp)(void *, double))
{
    /* count actual FIR taps (skip single-tap gain elements) */
    int M = 0;
    const FidFilter *ff;
    for (ff = filt; ff->len; ff = FFCNEXT(ff))
        if (ff->typ == 'F' && ff->len > 1) M += ff->len;

    if (M < 2) return NULL; /* degenerate — let regular path handle it */

    /* collect taps and accumulate gain */
    double *h    = (double *)Alloc((size_t)M * sizeof(double));
    double  gain = 1.0;
    int     hi   = 0;
    for (ff = filt; ff->len; ff = FFCNEXT(ff)) {
        if (ff->typ == 'F' && ff->len == 1) {
            gain *= ff->val[0];
        } else if (ff->typ == 'F') {
            memcpy(h + hi, ff->val, (size_t)ff->len * sizeof(double));
            hi += ff->len;
        }
    }
    if (gain != 1.0)
        for (int i = 0; i < M; i++) h[i] *= gain;

    /* N = next power of 2 >= 2*M, B = N - M + 1 */
    int N = ola_next_pow2(2 * M);
    int B = N - M + 1;

    /* allocate RunOLA + H[2*N] in one contiguous block */
    RunOLA *ro = (RunOLA *)Alloc(sizeof(RunOLA) + (size_t)(2 * N) * sizeof(double));
    ro->magic  = RUN_MAGIC_OLA;
    ro->N      = N;
    ro->M      = M;
    ro->B      = B;

    /* build H: zero-pad h to N, compute forward FFT */
    double *z = OLA_H(ro);
    /* z is already zeroed (Alloc uses calloc) */
    for (int i = 0; i < M; i++) {
        z[2*i]   = h[i];
        z[2*i+1] = 0.0;
    }
    ola_fft(z, N, 0);

    free(h);
    *funcpp = filter_step_ola;
    return ro;
}

/* ── ola_run_bufsize ─────────────────────────────────────────────────────── */

static int
ola_run_bufsize(void *run)
{
    RunOLA *ro = (RunOLA *)run;
    return (int)(sizeof(RunOLABuf)
               + (size_t)(ro->N)       * sizeof(double)   /* x */
               + (size_t)(ro->B)       * sizeof(double)   /* y */
               + (size_t)(2 * ro->N)   * sizeof(double)); /* z */
}

/* ── ola_run_initbuf ─────────────────────────────────────────────────────── */

static void
ola_run_initbuf(void *run, void *buf)
{
    RunOLA    *ro   = (RunOLA *)run;
    RunOLABuf *rb   = (RunOLABuf *)buf;
    char      *base = (char *)(rb + 1);

    rb->type_tag  = RUNBUF_MAGIC_OLA;
    rb->ola       = ro;
    rb->in_pos    = 0;
    rb->out_pos   = 0;
    rb->out_avail = 0;
    rb->x         = (double *)base;
    rb->y         = rb->x + ro->N;
    rb->z         = rb->y + ro->B;
    memset(base, 0, (size_t)(ro->N + ro->B + 2 * ro->N) * sizeof(double));
}

/* ── ola_run_newbuf ──────────────────────────────────────────────────────── */

static void *
ola_run_newbuf(void *run)
{
    RunOLABuf *rb = (RunOLABuf *)Alloc((size_t)ola_run_bufsize(run));
    ola_run_initbuf(run, rb);
    return rb;
}

/* ── ola_run_zapbuf ──────────────────────────────────────────────────────── */

static void
ola_run_zapbuf(void *buf)
{
    RunOLABuf *rb = (RunOLABuf *)buf;
    RunOLA    *ro = rb->ola;
    rb->in_pos    = 0;
    rb->out_pos   = 0;
    rb->out_avail = 0;
    memset(rb->x, 0, (size_t)(ro->N + ro->B + 2 * ro->N) * sizeof(double));
}

#undef OLA_H

#endif /* FID_FFT_H */
