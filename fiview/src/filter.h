/**
 * @file filter.h
 * @brief Filter loading, analysis, and run-time execution types and prototypes.
 *
 * Defines the central @ref Filter and @ref SubFilt data structures used throughout
 * fiview, plus @ref RunFilter for sample-by-sample execution.  Also declares
 * Blackman-window and wavelet generator helpers used by the frequency-test modes.
 *
 * @defgroup fiview_filter Filter subsystem
 * @ingroup fiview
 * @{
 */

#ifndef FILTER_H
#define FILTER_H

#include <stdio.h>
#include <fidlib/fidlib.h>

typedef struct SubFilt SubFilt;
typedef struct Filter Filter;
typedef struct RunFilter RunFilter;

/**
 * @brief One sub-filter within a multi-spec filter definition.
 *
 * A @ref Filter loaded from a `.filt` file may contain several sub-specs
 * (e.g. LP + HP cascade), each represented as a SubFilt node.
 */
struct SubFilt {
   SubFilt   *nxt;     /**< Next sub-filter in the chain, or NULL. */
   char      *desc0;   /**< Short human-readable description, or NULL. */
   char      *desc1;   /**< Long description / notes, or NULL. */
   char      *minsp;   /**< Minimum-spec string for frequency adjustment, or NULL. */
   double     minf0;   /**< Lower frequency bound for adjustment. */
   double     minf1;   /**< Upper frequency bound for adjustment. */
   int        minadj;  /**< Non-zero if automatic frequency adjustment is requested. */
   int        n_var;   /**< Number of variable (non-constant) FIR coefficients. */
   FidFilter *filt;    /**< Designed filter (owned by this node). */
   int        filt_len;/**< Byte length of @c filt excluding the end-marker. */
};

/**
 * @brief Top-level filter object — one per filter spec loaded by fiview.
 *
 * Carries the complete analysis of a filter: impulse response statistics,
 * frequency response metrics (−3 dB / −6 dB crossings, min/max points),
 * and a ready-to-run @ref FidRun instance.
 */
struct Filter {
   Filter    *nxt;      /**< Next filter in the global list, or NULL. */
   char      *name;     /**< Spec string as given on the command-line. */
   int        ii;       /**< 1-based index within the global list. */
   SubFilt   *filt;     /**< Linked list of sub-filter definitions. */
   char      *dump;     /**< Heap-allocated textual analysis report. */

   FidFilter *ff;       /**< Combined @ref FidFilter chain (alloc phase). */
   FidRun    *run;      /**< Compiled run object (alloc phase). */
   FidFunc   *funcp;    /**< Pointer to the per-sample step function. */

   double  tot100;      /**< Sum of |impulse| across all samples (normalisation base). */
   int     cnt50;       /**< Samples to reach 50 % of @c tot100. */
   int     cnt90;       /**< Samples to reach 90 % of @c tot100. */
   int     cnt95;       /**< Samples to reach 95 % of @c tot100. */
   int     cnt99;       /**< Samples to reach 99 % of @c tot100. */
   int     cnt999;      /**< Samples to reach 99.9 % of @c tot100. */
   int     cnt9999;     /**< Samples to reach 99.99 % of @c tot100. */
   int     cnt_max;     /**< Total samples computed for impulse analysis. */
   double  impmin;      /**< Minimum impulse-response sample value. */
   double  impmax;      /**< Maximum impulse-response sample value. */

   int         typ;     /**< Filter type: 0=LP, 1=HP, 2=BP, 3=BS, 4=AP. */
   const char *typstr;  /**< Human-readable type string from @c filt_typename[]. */
   double      gain;    /**< DC (LP) or passband (other) gain. */
   double      gain100; /**< Gain value treated as 1.0 for display normalisation. */
   int         n_m6db;  /**< Number of −6.02 dB crossing frequencies. */
   double     *m6db;    /**< Array of @c n_m6db frequencies at −6 dB. */
   int         n_m3db;  /**< Number of −3.01 dB crossing frequencies. */
   double     *m3db;    /**< Array of @c n_m3db frequencies at −3 dB. */
   int         n_minmax;/**< Number of (freq, response) extremum pairs. */
   double     *minmax;  /**< Array of @c n_minmax × 2 doubles: (freq, amplitude) pairs. */
};

/**
 * @brief Lightweight per-channel run state used by the display loop.
 */
struct RunFilter {
   void    *buf;    /**< Heap-allocated @ref RunBuf for this channel. */
   FidFunc *funcp;  /**< Per-sample step function pointer. */
};

/** @brief Blackman-window oscillator used to generate windowed frequency sweeps. */
typedef struct Blackman Blackman;
struct Blackman {
   double gen1[3]; /**< Cosine generator state for 0.5·cos(x) term. */
   double gen2[3]; /**< Cosine generator state for 0.08·cos(2x) term. */
   int    cnt;     /**< Remaining samples in the current window. */
};

/** @brief Complex wavelet oscillator for the wavelet frequency-test mode. */
typedef struct Wavelet Wavelet;
struct Wavelet {
   Blackman bl;     /**< Windowing envelope generator. */
   double   osc[2]; /**< Complex oscillator state (real, imag). */
   double   inc[2]; /**< Per-sample complex rotation increment. */
   double   out[2]; /**< Current complex output sample (real, imag). */
};

extern Filter     *filters;        /**< Head of the global filter list. */
extern const char *filt_typename[];/**< Human-readable filter-type strings (LP/HP/BP/BS/AP). */

int     filter_load_file(char *fnam);  /**< Load filters from a `.filt` file; returns new total count. */
int     filter_load_immed(char *txt);  /**< Load filters from an inline spec string; returns new total count. */
Filter *filter_find(int index);        /**< Look up a @ref Filter by 1-based index; returns NULL if not found. */
void       filter_setup_run(Filter *filt); /**< Compile @c filt->ff into a @ref FidRun ready for sample processing. */
RunFilter *filter_run(Filter *ff);         /**< Allocate and initialise a @ref RunFilter from @p ff. */
void       runfilter_free(RunFilter *rr);  /**< Release a @ref RunFilter allocated by filter_run(). */
double     runfilter_step(RunFilter *rr, double val); /**< Process one sample through @p rr. @rtSafe */
int     filter_complete(Filter *ff, double prop, int max);     /**< Sample count to reach @p prop fraction of impulse energy. */
void    filter_setup_cnt(Filter *ff);                          /**< Compute all cnt50/cnt90/… fields from the impulse response. */
double  filter_response(Filter *filt, double freq, double *phase); /**< Response amplitude (and optionally phase) at @p freq. */
double *filter_resp_range(Filter *ff, double freq0, double freq1, int slots, int subslots); /**< Response curve over a frequency range. */
void    filter_setup_gain(Filter *ff);                         /**< Compute gain, type, −3/−6 dB crossings and min/max points. */
char       *filter_dump(Filter *ff);                           /**< Generate a full human-readable analysis string. */
void        filter_dump_code(FidFilter *inpfilt, double adj, int opt); /**< Dump C code for hard-coding this filter. */
void        filter_dump_var_code(Filter *filt);                /**< Dump variable-coefficient C code. */
void        filter_dump_fidlib_calls(Filter *filt);            /**< Dump fidlib API calls to reproduce this filter. */
void        dump_filter_coef(FidFilter *ff, FILE *out);        /**< Write raw coefficient table to @p out. */
const char *filter_standard_code(void);                        /**< Return the standard boilerplate C run-time code. */
double *do_filter_test(Filter *filt, int ftmod, double ftarg,
                       double freq0, double freq1, int slots); /**< Run wavelet/slide frequency test over a range. */
int    blackman_init(Blackman *bl, double wid); /**< Initialise Blackman window of width @p wid samples; returns 1 on success. */
double blackman_gen(Blackman *bl);              /**< Generate next window sample; returns 0 when window is exhausted. */
int    wavelet_init(Wavelet *ww, double freq, double len); /**< Initialise complex wavelet at @p freq for @p len cycles. */
void   wavelet_gen(Wavelet *ww);                /**< Advance wavelet by one sample; result in @c ww->out[]. */

/** @} */ /* end fiview_filter */

#endif
