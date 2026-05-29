/**
 * @file fidrf_cmdlist.h
 * @brief Command-list filter execution engine — default high-performance backend.
 *
 * Compiles a @ref FidFilter chain into a compact bytecode program (a sequence
 * of opcode bytes + coefficient doubles) and executes it sample-by-sample via
 * filter_step().  Sub-filters are kept structurally separate, which gives better
 * numerical accuracy than the combined approach while still being very fast on
 * modern out-of-order CPUs.
 *
 * ### Memory layout of a RunBuf
 * All hot-path data is allocated in one contiguous block for cache locality:
 * @code
 * [ RunBuf header | buf[buf_size] | coef[coef_cnt] | cmd[cmd_cnt] ]
 *    ↑ returned to caller           ↑ private copy   ↑ private copy
 * @endcode
 *
 * **Internal header — included directly into `fidlib.c`.  Not a public API.**
 *
 * @author  Jim Peters
 * @copyright LGPL 2.1
 * @ingroup fidlib_run
 */

//
//	Command-list based filter-running code.
//
//        Copyright (c) 2002-2003 Jim Peters <http://uazu.net/>.  This
//        file is released under the GNU Lesser General Public License
//        (LGPL) version 2.1 as published by the Free Software
//        Foundation.  See the file COPYING_LIB for details, or visit
//        <http://www.fsf.org/licenses/licenses.html>.
//
//	This version of the filter-running code is based on getting
//	the filter to go as fast as possible with a pre-compiled
//	routine, but without flattening the filter structure.  This
//	gives greater accuracy than the combined filter.  The result
//	is mostly faster than the combined filter (tested on ix86 with
//	gcc -O6), except where the combined filter gets a big
//	advantage from flattening the filter list.  This code is also
//	portable (unlike the JIT option).
//

/* Execution precision — defaults to double; override at build time via -DFID_REAL=float */
#ifndef FID_REAL
#  define FID_REAL double
#endif
#ifdef FIDLIB_PRECISION_F32
#  pragma message "fidlib: FIDLIB_PRECISION_F32 aktiv — IIR-Filter koennen numerisch instabil werden!"
#endif

/**
 * @brief Compiled filter program — shared, read-only after fid_run_new().
 *
 * Produced once per filter design by fid_run_new().  May be shared among
 * many concurrent RunBuf instances (one per audio channel or thread).
 * @ingroup fidlib_run
 */
typedef struct Run {
   int    magic;    /**< Validity sentinel: must equal @c 0x64966325. */
   int    buf_size; /**< Delay-line length in `double` elements. */
   int    coef_cnt; /**< Number of coefficient doubles in the program. */
   int    cmd_cnt;  /**< Number of command bytes including the terminal @c 0. */
   double *coef;    /**< Pointer into the same allocation — coefficient array. */
   char   *cmd;     /**< Pointer into the same allocation — opcode array. */
} Run;

/**
 * @brief Per-channel filter state — one per concurrent audio stream.
 *
 * Allocated by fid_run_newbuf() or placed into a caller-supplied buffer via
 * fid_run_initbuf().  All hot-path data lives in a single contiguous block:
 * @code
 *  [ RunBuf | buf[] | coef-copy[] | cmd-copy[] ]
 * @endcode
 * @rtSafe Once initialised, no heap operations occur inside filter_step().
 * @ingroup fidlib_run
 */
typedef struct RunBuf {
   const FID_REAL *coef;   /**< Private coef copy, converted to FID_REAL at init. */
   const char     *cmd;    /**< Private copy of the opcode array (cache-local). */
   size_t          mov_cnt; /**< Bytes to shift per sample: (buf_size-1)*sizeof(FID_REAL). */
   FID_REAL       *buf;    /**< Delay-line buffer (immediately follows this struct). */
} RunBuf;


/**
 * @brief Execute one filter sample — the hot inner loop.
 *
 * Dispatches through a compact switch on the pre-compiled opcode stream.
 * The use of memmove() to shift the delay line first, followed by a tight
 * switch loop, was found empirically to be faster than shifting in-place
 * on GCC/ix86.
 *
 * @par Opcode table
 * | Code | Meaning                                      | IIR taps | FIR taps |
 * |-----:|----------------------------------------------|:--------:|:--------:|
 * |  0   | END                                          | —        | —        |
 * |  1   | 1× IIR                                       | 1        | 0        |
 * |  2   | 2× IIR                                       | 2        | 0        |
 * |  3   | 3× IIR                                       | 3        | 0        |
 * |  4   | 4N× IIR (N follows)                          | 4N       | 0        |
 * |  5   | 1× FIR                                       | 0        | 1        |
 * |  6   | 2× FIR                                       | 0        | 2        |
 * |  7   | 3× FIR                                       | 0        | 3        |
 * |  8   | 4N× FIR (N follows)                          | 0        | 4N       |
 * |  9   | 1× IIR+FIR                                   | 1        | 1        |
 * | 10   | 2× IIR+FIR                                   | 2        | 2        |
 * | 11   | 3× IIR+FIR                                   | 3        | 3        |
 * | 12   | 4N× IIR+FIR (N follows)                      | 4N       | 4N       |
 * | 13   | End-stage pure-IIR (no FIR at all)           | 1        | 0        |
 * | 14   | End-stage FIR only                           | 0        | 2        |
 * | 15   | End-stage IIR+FIR                            | 1        | 2        |
 * | 16   | IIR + pure-IIR end-stage                    | 2        | 0        |
 * | 17   | FIR + FIR end-stage                          | 0        | 3        |
 * | 18   | IIR+FIR + IIR+FIR end-stage (most common)   | 2        | 3        |
 * | 19   | N× (IIR + pure-IIR end-stage) (N follows)   | 2        | 0        |
 * | 20   | N× (FIR + FIR end-stage) (N follows)         | 0        | 3        |
 * | 21   | N× (IIR+FIR + IIR+FIR end-stage) (N follows)| 2        | 3        |
 * | 22   | Gain coefficient                             | 0        | 1        |
 *
 * @param fbuf  Pointer to a fully-initialised @ref RunBuf.
 * @param iir   Input sample.
 * @return      Filtered output sample.
 * @rtSafe
 * @ingroup fidlib_run
 */

//
//	Step commands:
//	  0  END
//	  1  IIR coefficient (1+0)
//	  2  2x IIR coefficient (2+0)
//	  3  3x IIR coefficient (3+0)
//	  4  4Nx IIR coefficient (4N+0)
//	  5  FIR coefficient (0+1)
//	  6  2x FIR coefficient (0+2)
//	  7  3x FIR coefficient (0+3)
//	  8  4Nx FIR coefficient (0+4N)
//	  9  IIR+FIR coefficients (1+1)
//	 10  2x IIR+FIR coefficients (2+2)
//	 11  3x IIR+FIR coefficients (3+3)
//	 12  4Nx IIR+FIR coefficients (4N+4N)
//	 13  End-stage, pure IIR, assume no FIR done at all (1+0)
//	 14  End-stage with just FIR coeff (0+2)
//	 15  End-stage with IIR+FIR coeff (1+2)
//	 16  IIR + pure-IIR endstage (2+0)
//	 17  FIR + FIR end-stage (0+3)
//	 18  IIR+FIR + IIR+FIR end-stage (2+3)
//	 19  Nx (IIR + pure-IIR endstage) (2+0)
//	 20  Nx (FIR + FIR end-stage) (0+3)
//	 21  Nx (IIR+FIR + IIR+FIR end-stage) (2+3)
//	 22  Gain coefficient (0+1)
//
//	Most filters are made up of 2x2 IIR/FIR pairs, which means a
//	list of command 18 bytes.  The other big job would be long FIR
//	filters.  These have to be handled with a list of 7,6,5
//	commands, plus a 13 command.
//

typedef unsigned char uchar;

#ifdef FIDLIB_SIMD
#include "fid_simd.h"
#endif

#ifdef FIDLIB_FFT
#include "fid_fft.h"
#endif

#ifdef FIDLIB_VULKAN
#include "fid_vulkan.h"
#endif

#ifdef FIDLIB_OPENCL
#include "fid_opencl.h"
#endif

static FID_REAL
filter_step(void *fbuf, FID_REAL iir) {
   const FID_REAL *coef= ((RunBuf*)fbuf)->coef;
   const char     *cmd = ((RunBuf*)fbuf)->cmd;
   FID_REAL       *buf = ((RunBuf*)fbuf)->buf;
   char ch;
   FID_REAL fir= (FID_REAL)0;
   FID_REAL tmp= buf[0];
   int cnt;

   // Using a memmove first is faster on gcc -O6 / ix86 than moving
   // the values whilst working through the buffers.
   memmove(buf, buf+1, ((RunBuf*)fbuf)->mov_cnt);

#define IIR \
       iir -= *coef++ * tmp; \
       tmp= *buf++;
#define FIR \
       fir += *coef++ * tmp; \
       tmp= *buf++;
#define BOTH \
       iir -= *coef++ * tmp; \
       fir += *coef++ * tmp; \
       tmp= *buf++;
#define ENDIIR \
       iir -= *coef++ * tmp; \
       tmp= *buf++; \
       buf[-1]= iir;
#define ENDFIR \
       fir += *coef++ * tmp; \
       tmp= *buf++; \
       buf[-1]= iir; \
       iir= fir + *coef++ * iir; \
       fir= 0
#define ENDBOTH \
       iir -= *coef++ * tmp; \
       fir += *coef++ * tmp; \
       tmp= *buf++; \
       buf[-1]= iir; \
       iir= fir + *coef++ * iir; \
       fir= 0
#define GAIN \
       iir *= *coef++

   while (FID_LIKELY((ch= *cmd++))) switch (ch) {
    case 1:
       IIR; break;
    case 2:
       IIR; IIR; break;
    case 3:
       IIR; IIR; IIR; break;
    case 4:
       cnt= *cmd++; 
       do { IIR; IIR; IIR; IIR; } while (--cnt > 0);
       break;
    case 5:
       FIR; break;
    case 6:
       FIR; FIR; break;
    case 7:
       FIR; FIR; FIR; break;
#ifdef FIDLIB_SIMD
    case 8: {
       /* SIMD dot product: fid_fir_dot_T selects FP32 or FP64 via FIDLIB_PRECISION_F32. */
       int n = (int)(unsigned char)*cmd++ * 4;
       buf[-1] = tmp;
       fir += fid_fir_dot_T(coef, buf - 1, n);
       coef += n;
       buf  += n;
       tmp   = buf[-1];
       break;
    }
#else
    case 8:
       cnt= *cmd++;
       do { FIR; FIR; FIR; FIR; } while (--cnt > 0);
       break;
#endif
    case 9:
       BOTH; break;
    case 10:
       BOTH; BOTH; break;
    case 11:
       BOTH; BOTH; BOTH; break;
    case 12:
       cnt= *cmd++; 
       do { BOTH; BOTH; BOTH; BOTH; } while (--cnt > 0);
       break;
    case 13:
       ENDIIR; break;
    case 14:
       ENDFIR; break;
    case 15:
       ENDBOTH; break;
    case 16:
       IIR; ENDIIR; break;
    case 17:
       FIR; ENDFIR; break;
    case 18:
       BOTH; ENDBOTH; break;
    case 19:
       cnt= *cmd++; 
       do { IIR; ENDIIR; } while (--cnt > 0);
       break;
    case 20:
       cnt= *cmd++; 
       do { FIR; ENDFIR; } while (--cnt > 0);
       break;
    case 21:
       cnt= *cmd++; 
       do { BOTH; ENDBOTH; } while (--cnt > 0);
       break;
    case 22:
       GAIN; break;
   }

#undef IIR
#undef FIR
#undef BOTH
#undef ENDIIR
#undef ENDFIR
#undef ENDBOTH
#undef GAIN

   return iir;
}


//
//	Create an instance of a filter, ready to run.  This returns a
//	void* handle, and a function to call to execute the filter.
//	Working buffers for the filter instances must be allocated
//	separately using fid_run_newbuf().  This allows many
//	simultaneous instances of the filter to be run.  
//
//	The sub-filters are executed in the precise order that they
//	are given.  This may lead to some inefficiency.  Normally when
//	an IIR filter is followed by an FIR filter, the buffers can be
//	shared.  However, if the sub-filters are not in IIR/FIR pairs,
//	then extra memory accesses are required.
//
//	In any case, factors are extracted from IIR filters (so that
//	the first coefficient is 1), and single-element FIR filters
//	are merged into the global gain factor, and are ignored.
//
//	The returned handle must be released using fid_run_free().
//

#ifdef FIDLIB_PRECISION_F32
/* Public-API adapter: keeps double interface while running FP32 internally. */
static double
filter_step_pub(void *fbuf, double iir) {
   return (double)filter_step(fbuf, (float)iir);
}
#endif

void *
fid_run_new(const FidFilter *filt, double (**funcpp)(void *,double)) {
#ifdef FIDLIB_OPENCL
   {
      /* OpenCL GPU FIR: pure-FIR chains above OpenCL threshold */
      int tap_cnt = 0, is_fir = 1;
      const FidFilter *ff2;
      for (ff2 = filt; ff2->len; ff2 = FFCNEXT(ff2)) {
         if (ff2->typ == 'I') { is_fir = 0; break; }
         if (ff2->typ == 'F' && ff2->len > 1) tap_cnt += ff2->len;
      }
      if (is_fir && tap_cnt >= FIDLIB_OPENCL_THRESHOLD) {
         void *r = ocl_run_new(filt, funcpp);
         if (r) return r;
      }
   }
#endif
#ifdef FIDLIB_VULKAN
   {
      /* Vulkan GPU FIR: pure-FIR chains above Vulkan threshold */
      int tap_cnt = 0, is_fir = 1;
      const FidFilter *ff2;
      for (ff2 = filt; ff2->len; ff2 = FFCNEXT(ff2)) {
         if (ff2->typ == 'I') { is_fir = 0; break; }
         if (ff2->typ == 'F' && ff2->len > 1) tap_cnt += ff2->len;
      }
      if (is_fir && tap_cnt >= FIDLIB_VULKAN_THRESHOLD) {
         void *r = vk_run_new(filt, funcpp);
         if (r) return r;  /* falls through to FFT/Run on GPU unavailable */
      }
   }
#endif
#ifdef FIDLIB_FFT
   {
      /* Detect pure-FIR chains long enough to benefit from FFT convolution */
      int tap_cnt = 0, is_fir = 1;
      const FidFilter *ff2;
      for (ff2 = filt; ff2->len; ff2 = FFCNEXT(ff2)) {
         if (ff2->typ == 'I') { is_fir = 0; break; }
         if (ff2->typ == 'F' && ff2->len > 1) tap_cnt += ff2->len;
      }
      if (is_fir && tap_cnt >= FIDLIB_FFT_THRESHOLD) {
         void *r = ola_run_new(filt, funcpp);
         if (r) return r;
      }
   }
#endif
   int buf_size= 0;
   uchar *cp, prev;
   const FidFilter *ff;
   double *dp;
   double gain= 1.0;
   int a;
   double *coef_tmp;
   uchar *cmd_tmp;
   int coef_cnt, coef_max;
   int cmd_cnt, cmd_max;
   int filt_cnt= 0;
   Run *rr;

   for (ff= filt; ff->len; ff= FFCNEXT(ff))
      filt_cnt += ff->len;

   // Allocate worst-case sizes for temporary arrays
   coef_tmp= ALLOC_ARR(coef_max= filt_cnt + 1, double);
   cmd_tmp= (uchar*)ALLOC_ARR(cmd_max= filt_cnt + 4, char);
   dp= coef_tmp;
   cp= cmd_tmp;
   prev= 0;

   // Generate command and coefficient lists
   while (filt->len) {
      int n_iir, n_fir, cnt;
      const double *iir, *fir;
      double adj = 0.0;
      if (filt->typ == 'F' && filt->len == 1) {
	 gain *= filt->val[0];
	 filt= FFCNEXT(filt);
	 continue;
      }
      if (filt->typ == 'F') {
	 iir= 0; n_iir= 0;
	 fir= filt->val; n_fir= filt->len;
	 filt= FFCNEXT(filt);
      } else if (filt->typ == 'I') {
	 iir= filt->val; n_iir= filt->len;
	 fir= 0; n_fir= 0;
	 filt= FFCNEXT(filt);
	 while (filt->typ == 'F' && filt->len == 1) {
	    gain *= filt->val[0];
	    filt= FFCNEXT(filt);
	 }
	 if (filt->typ == 'F') {
	    fir= filt->val; n_fir= filt->len;
	    filt= FFCNEXT(filt);
	 }
      } else
	 error("Internal error: fid_run_new can only handle IIR + FIR types");
      
      // Okay, we now have an IIR/FIR pair to process, possibly with
      // n_iir or n_fir == 0 if one half is missing
      cnt= n_iir > n_fir ? n_iir : n_fir;
      buf_size += cnt-1;
      if (n_iir) {
	 adj= 1.0 / iir[0];
	 gain *= adj;
      }
      if (n_fir == 3 && n_iir == 3) {
	 if (prev == 18) { cp[-1]= prev= 21; *cp++= 2; }
	 else if (prev == 21) { cp[-1]++; }
	 else *cp++= prev= 18;
	 *dp++= iir[2]*adj; *dp++= fir[2];
	 *dp++= iir[1]*adj; *dp++= fir[1];
	 *dp++= fir[0];
      } else if (n_fir == 3 && n_iir == 0) {
	 if (prev == 17) { cp[-1]= prev= 20; *cp++= 2; }
	 else if (prev == 20) { cp[-1]++; }
	 else *cp++= prev= 17;
	 *dp++= fir[2];
	 *dp++= fir[1];
	 *dp++= fir[0];
      } else if (n_fir == 0 && n_iir == 3) {
	 if (prev == 16) { cp[-1]= prev= 19; *cp++= 2; }
	 else if (prev == 19) { cp[-1]++; }
	 else *cp++= prev= 16;
	 *dp++= iir[2]*adj;
	 *dp++= iir[1]*adj;
      } else {
	 prev= 0;	// Just cancel 'prev' as we only use it for 16-18,19-21
	 if (cnt > n_fir) {
	    a= 0; 
	    while (cnt > n_fir && cnt > 2) {
	       *dp++= iir[--cnt] * adj; a++;
	    }
	    while (a >= 4) { 
	       int nn= a/4; if (nn > 255) nn= 255;
	       *cp++= 4; *cp++= (uchar)nn; a -= nn*4;
	    }
	    if (a) *cp++= (uchar)a;
	 }
	 if (cnt > n_iir) {
	    a= 0; 
	    while (cnt > n_iir && cnt > 2) {
	       *dp++= fir[--cnt]; a++;
	    }
	    while (a >= 4) { 
	       int nn= a/4; if (nn > 255) nn= 255;
	       *cp++= 8; *cp++= (uchar)nn; a -= nn*4;
	    }
	    if (a) *cp++= (uchar)(4+a);
	 }
	 a= 0;
	 while (cnt > 2) {
	    cnt--; a++;
	    *dp++= iir[cnt]*adj; *dp++= fir[cnt];
	 }
	 while (a >= 4) { 
	    int nn= a/4; if (nn > 255) nn= 255;
	    *cp++= 12; *cp++= (uchar)nn; a -= nn*4;
	 }
	 if (a) *cp++= (uchar)(8+a);

	 if (!n_fir) {
	    *cp++= 13;
	    *dp++= iir[1];
	 } else if (!n_iir) {
	    *cp++= 14;
	    *dp++= fir[1];
	    *dp++= fir[0];
	 } else {
	    *cp++= 15;
	    *dp++= iir[1];
	    *dp++= fir[1];
	    *dp++= fir[0];
	 }
      }
   }
   
   if (gain != 1.0) {
      *cp++= 22;
      *dp++= gain;
   }
   *cp++= 0;

   // Sanity checks
   coef_cnt= (int)(dp-coef_tmp);
   cmd_cnt= (int)(cp-cmd_tmp);
   if (coef_cnt > coef_max ||
       cmd_cnt > cmd_max) 
      error("fid_run_new internal error; arrays exceeded");

   // Allocate the final Run structure to return (coef + cmd in same block)
   rr= (Run*)Alloc(sizeof(Run) +
		   (size_t)coef_cnt*sizeof(double) +
		   (size_t)cmd_cnt*sizeof(char));
   rr->magic    = 0x64966325;
   rr->buf_size = buf_size;
   rr->coef_cnt = coef_cnt;
   rr->cmd_cnt  = cmd_cnt;
   rr->coef= (double*)(rr+1);
   rr->cmd= (char*)(rr->coef + coef_cnt);
   memcpy(rr->coef, coef_tmp, (size_t)coef_cnt*sizeof(double));
   memcpy(rr->cmd,  cmd_tmp,  (size_t)cmd_cnt*sizeof(char));

   //DEBUG   {
   //DEBUG      int a;
   //DEBUG      for (cp= cmd_tmp; *cp; cp++) printf("%d ", *cp);
   //DEBUG      printf("\n");
   //DEBUG      //for (a= 0; a<coef_cnt; a++) printf("%g ", coef_tmp[a]);
   //DEBUG      //printf("\n");
   //DEBUG   }
   
   free(coef_tmp);
   free(cmd_tmp);

#ifdef FIDLIB_PRECISION_F32
   *funcpp= filter_step_pub;
#else
   *funcpp= filter_step;
#endif
   return rr;
}

//
//	Create a new instance of the given filter
//

void *
fid_run_newbuf(void *run) {
#ifdef FIDLIB_OPENCL
   {  unsigned int m; memcpy(&m, run, sizeof(m));
      if (m == RUN_MAGIC_OCL) return ocl_run_newbuf(run); }
#endif
#ifdef FIDLIB_VULKAN
   {  unsigned int m; memcpy(&m, run, sizeof(m));
      if (m == RUN_MAGIC_VK) return vk_run_newbuf(run); }
#endif
#ifdef FIDLIB_FFT
   if (((RunOLA*)run)->magic == RUN_MAGIC_OLA) return ola_run_newbuf(run);
#endif
   Run *rr= (Run*)run;
   int siz;
   size_t buf_bytes, coef_bytes, cmd_bytes;
   RunBuf *rb;
   char *base;

   if (FID_UNLIKELY(rr->magic != 0x64966325))
      error("Bad handle passed to fid_run_newbuf()");

   siz        = rr->buf_size ? rr->buf_size : 1;
   coef_bytes = (size_t)rr->coef_cnt * sizeof(FID_REAL);
   cmd_bytes  = (size_t)rr->cmd_cnt;
#ifdef FIDLIB_SIMD
   buf_bytes  = ((size_t)siz + 1) * sizeof(FID_REAL);
#else
   buf_bytes  = (size_t)siz * sizeof(FID_REAL);
#endif

   rb   = (RunBuf*)Alloc(sizeof(RunBuf) + buf_bytes + coef_bytes + cmd_bytes);
   base = (char*)(rb + 1);
#ifdef FIDLIB_SIMD
   rb->buf  = (FID_REAL*)base + 1;
#else
   rb->buf  = (FID_REAL*)base;
#endif
   rb->coef    = (const FID_REAL*)(base + buf_bytes);
   rb->cmd     = (const char*)    (base + buf_bytes + coef_bytes);
   rb->mov_cnt = (size_t)(siz - 1) * sizeof(FID_REAL);
#ifdef FIDLIB_PRECISION_F32
   {  /* convert double design coefficients to float execution coefficients */
      float *dst = (float*)(base + buf_bytes);
      int ci;
      for (ci = 0; ci < rr->coef_cnt; ci++) dst[ci] = (float)rr->coef[ci];
   }
#else
   memcpy((FID_REAL*)(base + buf_bytes), rr->coef, coef_bytes);
#endif
   memcpy((char*)rb->cmd, rr->cmd, cmd_bytes);
   /* rb->buf[] (and sentinel) zeroed by Alloc/calloc */

   return rb;
}

//
//	Find the space required for a filter buffer (for fid_run_initbuf).
//

int
fid_run_bufsize(void *run) {
#ifdef FIDLIB_OPENCL
   {  unsigned int m; memcpy(&m, run, sizeof(m));
      if (m == RUN_MAGIC_OCL) return ocl_run_bufsize(run); }
#endif
#ifdef FIDLIB_VULKAN
   {  unsigned int m; memcpy(&m, run, sizeof(m));
      if (m == RUN_MAGIC_VK) return vk_run_bufsize(run); }
#endif
#ifdef FIDLIB_FFT
   if (((RunOLA*)run)->magic == RUN_MAGIC_OLA) return ola_run_bufsize(run);
#endif
   Run *rr= (Run*)run;
   int siz;

   if (FID_UNLIKELY(rr->magic != 0x64966325))
      error("Bad handle passed to fid_run_bufsize()");

   siz= rr->buf_size ? rr->buf_size : 1;
#ifdef FIDLIB_SIMD
   siz++;   /* sentinel FID_REAL */
#endif
   return (int)(sizeof(RunBuf)
              + (size_t)siz          * sizeof(FID_REAL)
              + (size_t)rr->coef_cnt * sizeof(FID_REAL)
              + (size_t)rr->cmd_cnt);
}

//
//	Initialise a pre-allocated filter buffer (RT-safe: zero heap allocations).
//	Use fid_run_bufsize() to determine the required size, allocate from a pool
//	or arena, then call this once before the run phase.
//	Equivalent to fid_run_newbuf_inplace() — see fidlib.h.
//

void
fid_run_initbuf(void *run, void *buf) {
#ifdef FIDLIB_OPENCL
   {  unsigned int m; memcpy(&m, run, sizeof(m));
      if (m == RUN_MAGIC_OCL) { ocl_run_initbuf(run, buf); return; } }
#endif
#ifdef FIDLIB_VULKAN
   {  unsigned int m; memcpy(&m, run, sizeof(m));
      if (m == RUN_MAGIC_VK) { vk_run_initbuf(run, buf); return; } }
#endif
#ifdef FIDLIB_FFT
   if (((RunOLA*)run)->magic == RUN_MAGIC_OLA) { ola_run_initbuf(run, buf); return; }
#endif
   Run *rr= (Run*)run;
   RunBuf *rb= (RunBuf*)buf;
   int siz;
   size_t buf_bytes, coef_bytes;
   char *base;

   if (FID_UNLIKELY(rr->magic != 0x64966325))
      error("Bad handle passed to fid_run_initbuf()");

   siz        = rr->buf_size ? rr->buf_size : 1;
   coef_bytes = (size_t)rr->coef_cnt * sizeof(FID_REAL);
   base       = (char*)(rb + 1);
#ifdef FIDLIB_SIMD
   buf_bytes  = ((size_t)siz + 1) * sizeof(FID_REAL);
   rb->buf    = (FID_REAL*)base + 1;
#else
   buf_bytes  = (size_t)siz * sizeof(FID_REAL);
   rb->buf    = (FID_REAL*)base;
#endif
   rb->coef    = (const FID_REAL*)(base + buf_bytes);
   rb->cmd     = (const char*)    (base + buf_bytes + coef_bytes);
   rb->mov_cnt = (size_t)(siz - 1) * sizeof(FID_REAL);
#ifdef FIDLIB_PRECISION_F32
   {
      float *dst = (float*)(base + buf_bytes);
      int ci;
      for (ci = 0; ci < rr->coef_cnt; ci++) dst[ci] = (float)rr->coef[ci];
   }
#else
   memcpy((FID_REAL*)(base + buf_bytes), rr->coef, coef_bytes);
#endif
   memcpy((char*)rb->cmd, rr->cmd, (size_t)rr->cmd_cnt);
   memset(base, 0, buf_bytes);   /* zeroes sentinel + buf[] */
}

//
//	Reinitialise an instance of the filter, allowing it to start
//	afresh.  It assumes that the buffer was correctly initialised
//	previously, either through a call to fid_run_newbuf() or
//	fid_run_initbuf().
//

void
fid_run_zapbuf(void *buf) {
#ifdef FIDLIB_OPENCL
   {  unsigned int tag; memcpy(&tag, buf, sizeof(tag));
      if (tag == RUNBUF_MAGIC_OCL) { ocl_run_zapbuf(buf); return; } }
#endif
#ifdef FIDLIB_VULKAN
   {  unsigned int tag; memcpy(&tag, buf, sizeof(tag));
      if (tag == RUNBUF_MAGIC_VK) { vk_run_zapbuf(buf); return; } }
#endif
#ifdef FIDLIB_FFT
   {  unsigned int tag; memcpy(&tag, buf, sizeof(tag));
      if (tag == RUNBUF_MAGIC_OLA) { ola_run_zapbuf(buf); return; } }
#endif
   RunBuf *rb= (RunBuf*)buf;
#ifdef FIDLIB_SIMD
   rb->buf[-1] = (FID_REAL)0;   /* zero sentinel slot */
#endif
   memset(rb->buf, 0, rb->mov_cnt + sizeof(FID_REAL));
}   
   

//
//	Delete an instance
//

void
fid_run_freebuf(void *runbuf) {
#ifdef FIDLIB_OPENCL
   {  unsigned int tag; memcpy(&tag, runbuf, sizeof(tag));
      if (tag == RUNBUF_MAGIC_OCL) { ocl_run_freebuf(runbuf); return; } }
#endif
#ifdef FIDLIB_VULKAN
   {  unsigned int tag; memcpy(&tag, runbuf, sizeof(tag));
      if (tag == RUNBUF_MAGIC_VK) { vk_run_freebuf(runbuf); return; } }
#endif
   free(runbuf);
}

//
//	Delete the filter
//

void
fid_run_free(void *run) {
#ifdef FIDLIB_OPENCL
   {  unsigned int m; memcpy(&m, run, sizeof(m));
      if (m == RUN_MAGIC_OCL) { ocl_run_free(run); return; } }
#endif
#ifdef FIDLIB_VULKAN
   {  unsigned int m; memcpy(&m, run, sizeof(m));
      if (m == RUN_MAGIC_VK) { vk_run_free(run); return; } }
#endif
   free(run);
}

// END //
