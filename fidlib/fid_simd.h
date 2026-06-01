// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
/**
 * @file fid_simd.h
 * @brief SIMD detection and vectorised FIR dot product (NEON / SSE2 / scalar).
 *
 * Included by fidrf_cmdlist.h when FIDLIB_SIMD is defined.
 * Platform detection is performed at compile time via preprocessor macros.
 *
 * @ingroup fidlib_run
 */

#ifndef FID_SIMD_H
#define FID_SIMD_H

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  define FID_SIMD_NEON 1
#  include <arm_neon.h>
#elif defined(__SSE2__)
#  define FID_SIMD_SSE2 1
#  include <emmintrin.h>
#endif

/**
 * @brief Vectorised FIR dot product: Σ coef[i]·data[i] for i = 0…n-1.
 *
 * On AArch64 with NEON: dual-accumulating vfmaq_f64 loop (4-wide unrolling).
 * On x86_64 with SSE2: _mm_mul_pd/_mm_add_pd (4-wide unrolling).
 * Fallback: scalar C. n ≥ 1 is required.
 *
 * @param coef  Pointer to n coefficients (no alignment requirement).
 * @param data  Pointer to n data values  (no alignment requirement).
 * @param n     Number of elements (≥ 1).
 * @return      Dot product as double.
 * @rtSafe
 * @ingroup fidlib_run
 */
static inline double
fid_fir_dot(const double *coef, const double *data, int n)
{
#ifdef FID_SIMD_NEON
    float64x2_t acc0 = vdupq_n_f64(0.0);
    float64x2_t acc1 = vdupq_n_f64(0.0);
    int i = 0;
    for (; i <= n - 4; i += 4) {
        acc0 = vfmaq_f64(acc0, vld1q_f64(coef + i),     vld1q_f64(data + i));
        acc1 = vfmaq_f64(acc1, vld1q_f64(coef + i + 2), vld1q_f64(data + i + 2));
    }
    acc0 = vaddq_f64(acc0, acc1);
    if (i <= n - 2) {
        acc0 = vfmaq_f64(acc0, vld1q_f64(coef + i), vld1q_f64(data + i));
        i += 2;
    }
    double r = vgetq_lane_f64(acc0, 0) + vgetq_lane_f64(acc0, 1);
    if (i < n) r += coef[i] * data[i];
    return r;

#elif defined(FID_SIMD_SSE2)
    __m128d acc0 = _mm_setzero_pd();
    __m128d acc1 = _mm_setzero_pd();
    int i = 0;
    for (; i <= n - 4; i += 4) {
        acc0 = _mm_add_pd(acc0, _mm_mul_pd(_mm_loadu_pd(coef + i),
                                            _mm_loadu_pd(data + i)));
        acc1 = _mm_add_pd(acc1, _mm_mul_pd(_mm_loadu_pd(coef + i + 2),
                                            _mm_loadu_pd(data + i + 2)));
    }
    acc0 = _mm_add_pd(acc0, acc1);
    if (i <= n - 2) {
        acc0 = _mm_add_pd(acc0, _mm_mul_pd(_mm_loadu_pd(coef + i),
                                            _mm_loadu_pd(data + i)));
        i += 2;
    }
    {
        __m128d t = _mm_shuffle_pd(acc0, acc0, 1);
        double r = _mm_cvtsd_f64(_mm_add_sd(acc0, t));
        if (i < n) r += coef[i] * data[i];
        return r;
    }

#else  /* skalarer Fallback */
    double r = 0.0;
    int i;
    for (i = 0; i < n; i++) r += coef[i] * data[i];
    return r;
#endif
}

/**
 * @brief Vectorised FIR dot product (FP32): Σ coef[i]·data[i] for i = 0…n-1.
 *
 * Auf AArch64/NEON: float32x4_t, 8-wide unrolling (2× Durchsatz vs FP64).
 * Auf x86_64/SSE2:  __m128 (4×float), 8-wide unrolling.
 * Fallback: skalares C.
 *
 * @ingroup fidlib_run
 */
static inline float
fid_fir_dot_f32(const float *coef, const float *data, int n)
{
#ifdef FID_SIMD_NEON
    float32x4_t acc0 = vdupq_n_f32(0.f);
    float32x4_t acc1 = vdupq_n_f32(0.f);
    int i = 0;
    for (; i <= n - 8; i += 8) {
        acc0 = vfmaq_f32(acc0, vld1q_f32(coef + i),     vld1q_f32(data + i));
        acc1 = vfmaq_f32(acc1, vld1q_f32(coef + i + 4), vld1q_f32(data + i + 4));
    }
    acc0 = vaddq_f32(acc0, acc1);
    if (i <= n - 4) {
        acc0 = vfmaq_f32(acc0, vld1q_f32(coef + i), vld1q_f32(data + i));
        i += 4;
    }
    {
        float32x2_t t = vadd_f32(vget_low_f32(acc0), vget_high_f32(acc0));
        t = vpadd_f32(t, t);
        float r = vget_lane_f32(t, 0);
        for (; i < n; i++) r += coef[i] * data[i];
        return r;
    }

#elif defined(FID_SIMD_SSE2)
    __m128 acc0 = _mm_setzero_ps();
    __m128 acc1 = _mm_setzero_ps();
    int i = 0;
    for (; i <= n - 8; i += 8) {
        acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(coef + i),
                                            _mm_loadu_ps(data + i)));
        acc1 = _mm_add_ps(acc1, _mm_mul_ps(_mm_loadu_ps(coef + i + 4),
                                            _mm_loadu_ps(data + i + 4)));
    }
    acc0 = _mm_add_ps(acc0, acc1);
    if (i <= n - 4) {
        acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(coef + i),
                                            _mm_loadu_ps(data + i)));
        i += 4;
    }
    {
        __m128 t = _mm_add_ps(acc0, _mm_movehl_ps(acc0, acc0));
        t = _mm_add_ss(t, _mm_shuffle_ps(t, t, 1));
        float r = _mm_cvtss_f32(t);
        for (; i < n; i++) r += coef[i] * data[i];
        return r;
    }

#else
    float r = 0.f;
    int i;
    for (i = 0; i < n; i++) r += coef[i] * data[i];
    return r;
#endif
}

/* Generischer Dispatch: FIDLIB_PRECISION_F32 → FP32-Variante, sonst FP64. */
#ifdef FIDLIB_PRECISION_F32
#  define fid_fir_dot_T fid_fir_dot_f32
#else
#  define fid_fir_dot_T fid_fir_dot
#endif

#endif /* FID_SIMD_H */
