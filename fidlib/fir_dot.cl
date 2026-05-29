// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025-2026 Kai Dieki
/*
 * fir_dot.cl — FP32 FIR dot-product OpenCL kernel.
 *
 * One work item per output sample. Computes:
 *   y[i] = Σ_{k=0}^{M-1} coef[k] * x[i + M - 1 - k]
 *
 * Input layout:
 *   x[0..M-2]        = overlap (last M-1 samples from previous block)
 *   x[M-1..M+B-2]    = B new input samples
 * Output:
 *   y[0..B-1]        = B convolution results
 *
 * Arguments: coef[M], x[M-1+B], y[B], M, B.
 */

__kernel void fir_dot(
    __global const float *coef,
    __global const float *x,
    __global float       *y,
    const int             M,
    const int             B)
{
    int i = get_global_id(0);
    if (i >= B) return;

    float sum  = 0.0f;
    int   base = i + M - 1;
    for (int k = 0; k < M; k++)
        sum += coef[k] * x[base - k];
    y[i] = sum;
}
