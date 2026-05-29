/**
 * @file fidrf_combined.h
 * @brief Combined (flattened) filter execution engine — alternative backend.
 *
 * Convolves all IIR and FIR sub-filters into one large IIR/FIR pair and
 * runs the combined filter through a simple scalar loop.  This sacrifices
 * numerical accuracy for simplicity and can be fast for short filters.
 *
 * **Not the default backend.**  fidlib.c includes fidrf_cmdlist.h by default.
 * Swap the include to use this alternative.
 *
 * **Internal header — included directly into `fidlib.c`.  Not a public API.**
 *
 * @warning High-order filters may become numerically unstable when flattened
 *          because coefficient magnitudes span many orders of magnitude.
 *
 * @author  Jim Peters
 * @copyright LGPL 2.1
 * @ingroup fidlib_run
 */

//
//	Combined-filter based filter-running code.
//
//        Copyright (c) 2002-2003 Jim Peters <http://uazu.net/>.  This
//        file is released under the GNU Lesser General Public License
//        (LGPL) version 2.1 as published by the Free Software
//        Foundation.  See the file COPYING_LIB for details, or visit
//        <http://www.fsf.org/licenses/licenses.html>.
//
//	Convolves all the filters into a single IIR/FIR pair, and runs
//	that directly through static code.  Compiled with GCC -O6 on
//	ix86 this is surprisingly fast -- at worst half the speed of
//	assember code, at best matching it.  The downside of
//	convolving all the sub-filters together like this is loss of
//	accuracy and instability in some kinds of filters, especially
//	high-order ones.  The one big advantage of this approach is
//	that the code is easy to understand.
//

#ifndef FIDCOMBINED_H
#define FIDCOMBINED_H

/**
 * @brief Compiled combined filter — shared, read-only after fid_run_new().
 *
 * Stores the result of flattening all sub-filters into a single IIR/FIR pair
 * via fid_flatten().
 * @ingroup fidlib_run
 */
typedef struct Run {
   int       magic;  /**< Validity sentinel: must equal @c 0x64966325. */
   double   *fir;    /**< FIR coefficient array (points into @c filt allocation). */
   int       n_fir;  /**< Length of the FIR coefficient array. */
   double   *iir;    /**< IIR coefficient array (points into @c filt allocation). */
   int       n_iir;  /**< Length of the IIR coefficient array. */
   int       n_buf;  /**< Delay-line length = max(n_fir, n_iir). */
   FidFilter *filt;  /**< Heap-allocated flattened filter (owned by this Run). */
} Run;

/**
 * @brief Per-channel filter state for the combined backend.
 * @ingroup fidlib_run
 */
typedef struct RunBuf {
   Run   *run;    /**< Back-pointer to the shared @ref Run program. */
   double buf[1]; /**< Delay-line buffer — extended at allocation time to @c n_buf entries.
                       C++ does not allow @c [0]; @c [1] is portable. */
} RunBuf;

static double 
filter_step(void *rb, double val) {
   Run *rr= ((RunBuf*)rb)->run;
   double *buf= ((RunBuf*)rb)->buf;
   int a;

   // Shift the whole internal array up one
   memmove(buf+1, buf, (rr->n_buf-1)*sizeof(buf[0]));
   
   // Do IIR
   for (a= 1; a<rr->n_iir; a++) val -= rr->iir[a] * buf[a];
   buf[0]= val;

   // Do FIR
   val= 0;
   for (a= 0; a<rr->n_fir; a++) val += rr->fir[a] * buf[a];

   return val;
}


//
//	Create an instance of a filter, ready to run.  This returns a
//	void* handle, and a function to call to execute the filter.
//	Working buffers for the filter instances must be allocated
//	separately using fid_run_newbuf().  This allows many
//	simultaneous instances of the filter to be run.  
//
//	The returned handle must be released using fid_run_free().
//

void *
fid_run_new(FidFilter *filt, double (**funcpp)(void *,double)) {
   Run *rr= ALLOC(Run);
   FidFilter *ff;

   rr->magic= 0x64966325;
   rr->filt= fid_flatten(filt);

   ff= rr->filt;
   if (ff->typ != 'I') goto bad;
   rr->n_iir= ff->len;
   rr->iir= ff->val;	
   ff= FFNEXT(ff);
   if (ff->typ != 'F') goto bad;
   rr->n_fir= ff->len;
   rr->fir= ff->val;
   ff= FFNEXT(ff);
   if (ff->len) goto bad;
   
   rr->n_buf= rr->n_fir > rr->n_iir ? rr->n_fir : rr->n_iir;
   
   *funcpp= filter_step;
   
   return rr;
   
 bad:
   error("Internal error: fid_run_new() expecting IIR+FIR in flattened filter");
   return 0;
}

//
//	Create a new instance of the given filter
//

void *
fid_run_newbuf(void *run) {
   Run *rr= run;
   RunBuf *rb;

   if (rr->magic != 0x64966325)
      error("Bad handle passed to fid_run_newbuf()");
   
   rb= Alloc(sizeof(RunBuf) + rr->n_buf * sizeof(double));
   rb->run= run;
   // rb->buf[] already zerod

   return rb;
}

//
//	Reinitialise an instance ready to start afresh
//

void
fid_run_zapbuf(void *buf) {
   RunBuf *rb;
   Run *rr= rb->run;
   memset(rb->buf, 0, rr->n_buf * sizeof(double));
}

//
//	Delete an instance
//

void 
fid_run_freebuf(void *runbuf) {
   free(runbuf);
}

//
//	Delete the filter
//

void 
fid_run_free(void *run) {
   Run *rr= run;
   free(rr->filt);
   free(rr);
}

// END //
#endif

