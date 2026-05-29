// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025-2026 Kai Dieki
/**
 * @file fid_opencl.h
 * @brief OpenCL Compute FIR engine — GPU-accelerated FIR batch convolution (FP32).
 *
 * Included by fidrf_cmdlist.h when FIDLIB_OPENCL is defined.
 * Provides RunOCL / RunOCLBuf and filter_step_ocl() for large FIR filters.
 *
 * Design constraints:
 * - FP32 only: broad OpenCL compatibility (CL 1.1+); FP64 needs cl_khr_fp64.
 * - Batch processing: collects FIDLIB_OPENCL_BATCH samples, dispatches once.
 *   Inherent latency = FIDLIB_OPENCL_BATCH - 1 samples.
 * - Public API stays double(void*,double): FP32 conversion at the boundary.
 * - Lazy init: CL context created on first filter_step_ocl() call.
 *   Falls back to NEON/scalar if no GPU device found (e.g. RPi5 w/o Rusticl).
 * - Kernel source embedded as string; compiled at runtime via clBuildProgram.
 *
 * @ingroup fidlib_run
 */

#ifndef FID_OPENCL_H
#define FID_OPENCL_H

#ifdef __APPLE__
#  include <OpenCL/opencl.h>
#else
#  include <CL/opencl.h>
#endif

#include <string.h>
#include <stdlib.h>

/* ── Magic sentinels ────────────────────────────────────────────────────── */

#define RUN_MAGIC_OCL    0x4F434C00u  /* RunOCL shared state  */
#define RUNBUF_MAGIC_OCL 0x4F434C01u  /* RunOCLBuf per-channel state */

/* ── Embedded kernel source ─────────────────────────────────────────────── */

static const char *fir_dot_cl_src =
"__kernel void fir_dot(\n"
"    __global const float *coef,\n"
"    __global const float *x,\n"
"    __global float       *y,\n"
"    const int             M,\n"
"    const int             B)\n"
"{\n"
"    int i = get_global_id(0);\n"
"    if (i >= B) return;\n"
"    float sum  = 0.0f;\n"
"    int   base = i + M - 1;\n"
"    for (int k = 0; k < M; k++)\n"
"        sum += coef[k] * x[base - k];\n"
"    y[i] = sum;\n"
"}\n";

/* ── Global OpenCL context (lazy-initialized, shared) ────────────────────── */

typedef struct FidOclCtx {
    cl_platform_id   platform;
    cl_device_id     device;
    cl_context       ctx;
    cl_command_queue queue;
    cl_program       program;
    cl_kernel        kernel;
    int              ok;
} FidOclCtx;

static FidOclCtx g_ocl = {0};

/* ── Lazy CL context init ─────────────────────────────────────────────────── */

static int
ocl_init(void)
{
    if (g_ocl.ok) return 1;

    /* Find a platform with a GPU device; fall back to any device */
    cl_uint np = 0;
    clGetPlatformIDs(0, NULL, &np);
    if (np == 0) return 0;

    cl_platform_id *plats = (cl_platform_id *)alloca((size_t)np * sizeof(*plats));
    clGetPlatformIDs(np, plats, NULL);

    g_ocl.device   = NULL;
    g_ocl.platform = NULL;

    /* Prefer GPU, then CPU, then Accelerator */
    cl_device_type prefer[] = {
        CL_DEVICE_TYPE_GPU,
        CL_DEVICE_TYPE_ACCELERATOR,
        CL_DEVICE_TYPE_CPU,
    };
    for (size_t ti = 0; ti < sizeof(prefer)/sizeof(prefer[0]) && !g_ocl.device; ti++) {
        for (cl_uint pi = 0; pi < np && !g_ocl.device; pi++) {
            cl_uint nd = 0;
            if (clGetDeviceIDs(plats[pi], prefer[ti], 0, NULL, &nd) != CL_SUCCESS) continue;
            if (nd == 0) continue;
            cl_device_id dev;
            if (clGetDeviceIDs(plats[pi], prefer[ti], 1, &dev, NULL) != CL_SUCCESS) continue;
            g_ocl.device   = dev;
            g_ocl.platform = plats[pi];
        }
    }
    if (!g_ocl.device) return 0;

    /* Create context and queue */
    cl_int err;
    g_ocl.ctx = clCreateContext(NULL, 1, &g_ocl.device, NULL, NULL, &err);
    if (err != CL_SUCCESS) return 0;

    g_ocl.queue = clCreateCommandQueue(g_ocl.ctx, g_ocl.device, 0, &err);
    if (err != CL_SUCCESS) { clReleaseContext(g_ocl.ctx); return 0; }

    /* Build kernel */
    g_ocl.program = clCreateProgramWithSource(g_ocl.ctx, 1, &fir_dot_cl_src, NULL, &err);
    if (err != CL_SUCCESS) goto fail_queue;

    if (clBuildProgram(g_ocl.program, 1, &g_ocl.device, "-cl-fast-relaxed-math", NULL, NULL)
        != CL_SUCCESS) goto fail_program;

    g_ocl.kernel = clCreateKernel(g_ocl.program, "fir_dot", &err);
    if (err != CL_SUCCESS) goto fail_program;

    g_ocl.ok = 1;
    return 1;

fail_program:
    clReleaseProgram(g_ocl.program);
fail_queue:
    clReleaseCommandQueue(g_ocl.queue);
    clReleaseContext(g_ocl.ctx);
    return 0;
}

/* ── Shared read-only OpenCL filter state ────────────────────────────────── */

typedef struct RunOCL {
    unsigned int magic;      /* RUN_MAGIC_OCL */
    int          M;          /* FIR tap count */
    int          B;          /* batch size */
    cl_mem       coef_buf;   /* FP32 coefficient buffer on device */
} RunOCL;

/* ── Per-channel OpenCL state ────────────────────────────────────────────── */

typedef struct RunOCLBuf {
    unsigned int type_tag;   /* RUNBUF_MAGIC_OCL */
    RunOCL      *ocl;
    int          in_pos;
    int          out_pos;
    int          out_avail;
    float       *x_host;     /* input staging [M-1 + B] */
    float       *y_host;     /* output staging [B] */
    cl_mem       x_buf;
    cl_mem       y_buf;
} RunOCLBuf;

/* ── forward declarations ────────────────────────────────────────────────── */
static double filter_step_ocl(void *rbuf, double in);

/* ── ocl_run_new ─────────────────────────────────────────────────────────── */

static void *
ocl_run_new(const FidFilter *filt, double (**funcpp)(void *, double))
{
    if (!ocl_init()) return NULL;

    /* Collect FIR taps */
    int M = 0;
    const FidFilter *ff;
    for (ff = filt; ff->len; ff = FFCNEXT(ff))
        if (ff->typ == 'F' && ff->len > 1) M += ff->len;
    if (M < 2) return NULL;

    double *h_d  = (double *)Alloc((size_t)M * sizeof(double));
    double  gain = 1.0;
    int     hi   = 0;
    for (ff = filt; ff->len; ff = FFCNEXT(ff)) {
        if (ff->typ == 'F' && ff->len == 1)
            gain *= ff->val[0];
        else if (ff->typ == 'F') {
            memcpy(h_d + hi, ff->val, (size_t)ff->len * sizeof(double));
            hi += ff->len;
        }
    }
    if (gain != 1.0)
        for (int i = 0; i < M; i++) h_d[i] *= gain;

    const int B = FIDLIB_OPENCL_BATCH;

    /* Allocate device coefficient buffer */
    float *h_fp32 = (float *)Alloc((size_t)M * sizeof(float));
    for (int i = 0; i < M; i++) h_fp32[i] = (float)h_d[i];
    free(h_d);

    cl_int err;
    cl_mem coef_buf = clCreateBuffer(g_ocl.ctx,
                                     CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     (size_t)M * sizeof(float), h_fp32, &err);
    free(h_fp32);
    if (err != CL_SUCCESS) return NULL;

    RunOCL *ro = (RunOCL *)Alloc(sizeof(RunOCL));
    ro->magic    = RUN_MAGIC_OCL;
    ro->M        = M;
    ro->B        = B;
    ro->coef_buf = coef_buf;

    *funcpp = filter_step_ocl;
    return ro;
}

/* ── ocl_run_free ────────────────────────────────────────────────────────── */

static void
ocl_run_free(void *run)
{
    RunOCL *ro = (RunOCL *)run;
    if (ro && g_ocl.ok) clReleaseMemObject(ro->coef_buf);
    free(run);
}

/* ── ocl_run_bufsize ─────────────────────────────────────────────────────── */

static int
ocl_run_bufsize(void *run)
{
    RunOCL *ro = (RunOCL *)run;
    return (int)(sizeof(RunOCLBuf)
               + (size_t)(ro->M - 1 + ro->B) * sizeof(float)
               + (size_t)(ro->B)              * sizeof(float));
}

/* ── ocl_run_initbuf ─────────────────────────────────────────────────────── */

static void
ocl_run_initbuf(void *run, void *buf)
{
    RunOCL    *ro   = (RunOCL *)run;
    RunOCLBuf *rb   = (RunOCLBuf *)buf;
    char      *base = (char *)(rb + 1);

    rb->type_tag  = RUNBUF_MAGIC_OCL;
    rb->ocl       = ro;
    rb->in_pos    = 0;
    rb->out_pos   = 0;
    rb->out_avail = 0;
    rb->x_host    = (float *)base;
    rb->y_host    = rb->x_host + (ro->M - 1 + ro->B);
    memset(base, 0, (size_t)(ro->M - 1 + ro->B + ro->B) * sizeof(float));

    cl_int err;
    rb->x_buf = clCreateBuffer(g_ocl.ctx, CL_MEM_READ_WRITE,
                               (size_t)(ro->M - 1 + ro->B) * sizeof(float), NULL, &err);
    rb->y_buf = clCreateBuffer(g_ocl.ctx, CL_MEM_WRITE_ONLY,
                               (size_t)ro->B * sizeof(float), NULL, &err);
}

/* ── ocl_run_newbuf ──────────────────────────────────────────────────────── */

static void *
ocl_run_newbuf(void *run)
{
    RunOCLBuf *rb = (RunOCLBuf *)Alloc((size_t)ocl_run_bufsize(run));
    ocl_run_initbuf(run, rb);
    return rb;
}

/* ── ocl_run_freebuf ─────────────────────────────────────────────────────── */

static void
ocl_run_freebuf(void *buf)
{
    RunOCLBuf *rb = (RunOCLBuf *)buf;
    if (rb && g_ocl.ok) {
        clReleaseMemObject(rb->x_buf);
        clReleaseMemObject(rb->y_buf);
    }
    free(buf);
}

/* ── ocl_run_zapbuf ──────────────────────────────────────────────────────── */

static void
ocl_run_zapbuf(void *buf)
{
    RunOCLBuf *rb = (RunOCLBuf *)buf;
    RunOCL    *ro = rb->ocl;
    rb->in_pos    = 0;
    rb->out_pos   = 0;
    rb->out_avail = 0;
    memset(rb->x_host, 0, (size_t)(ro->M - 1 + ro->B + ro->B) * sizeof(float));
}

/* ── filter_step_ocl ─────────────────────────────────────────────────────── */

static double
filter_step_ocl(void *rbuf, double in)
{
    RunOCLBuf *rb = (RunOCLBuf *)rbuf;
    RunOCL    *ro = rb->ocl;

    rb->x_host[ro->M - 1 + rb->in_pos] = (float)in;
    rb->in_pos++;

    if (rb->in_pos == ro->B) {
        /* Upload input to device */
        clEnqueueWriteBuffer(g_ocl.queue, rb->x_buf, CL_FALSE, 0,
                             (size_t)(ro->M - 1 + ro->B) * sizeof(float),
                             rb->x_host, 0, NULL, NULL);

        /* Set kernel args and dispatch */
        cl_kernel k = g_ocl.kernel;
        cl_int M_cl = (cl_int)ro->M;
        cl_int B_cl = (cl_int)ro->B;
        clSetKernelArg(k, 0, sizeof(cl_mem), &ro->coef_buf);
        clSetKernelArg(k, 1, sizeof(cl_mem), &rb->x_buf);
        clSetKernelArg(k, 2, sizeof(cl_mem), &rb->y_buf);
        clSetKernelArg(k, 3, sizeof(cl_int), &M_cl);
        clSetKernelArg(k, 4, sizeof(cl_int), &B_cl);

        size_t gws = (size_t)((ro->B + 63) / 64 * 64);
        size_t lws = 64;
        clEnqueueNDRangeKernel(g_ocl.queue, k, 1, NULL, &gws, &lws, 0, NULL, NULL);

        /* Read back results */
        clEnqueueReadBuffer(g_ocl.queue, rb->y_buf, CL_TRUE, 0,
                            (size_t)ro->B * sizeof(float), rb->y_host, 0, NULL, NULL);

        /* Slide overlap */
        memmove(rb->x_host, rb->x_host + ro->B, (size_t)(ro->M - 1) * sizeof(float));
        rb->in_pos    = 0;
        rb->out_pos   = 0;
        rb->out_avail = ro->B;
    }

    if (rb->out_avail > 0) {
        rb->out_avail--;
        return (double)rb->y_host[rb->out_pos++];
    }
    return 0.0;
}

#endif /* FID_OPENCL_H */
