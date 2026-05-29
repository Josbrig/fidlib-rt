/**
 * @file fidlib.h
 * @brief Public API for the fidlib runtime IIR/FIR filter library.
 *
 * fidlib designs and executes digital filters at runtime from a compact
 * specification string (the *fispec* DSL).  No recompilation is required
 * when changing filter type, order, or corner frequencies.
 *
 * @par Supported filter families
 * | Prefix | Family |
 * |--------|--------|
 * | `LpBu`, `HpBu`, `BpBu`, `BsBu` | Butterworth low/high/band-pass/stop |
 * | `LpBe`, `HpBe`, ... | Bessel (maximally flat group delay) |
 * | `LpCh`, `HpCh`, ... | Chebyshev type I (equiripple passband) |
 * | `LpChZ`, ...        | Chebyshev, matched-z transform |
 * | `BpRe`, `BsRe`, `ApRe` | Resonator band-pass/stop/allpass |
 * | `LpBq`, `HpBq`, `BpBq`, `NoBq`, `ApBq`, `PkBq`, `LsBq`, `HsBq` | Biquad (Audio EQ Cookbook) |
 * | `x`                 | FIR identity (gain 1, or custom coefficient) |
 *
 * @par Usage — three-phase model
 * @code
 * // ── Alloc phase (not RT-safe, allocates) ─────────────────────────────
 * FidFilter *filt = fid_design("LpBu4/1000", 44100.0, -1.0, -1.0, 0, NULL);
 * FidFunc   *funcp;
 * void      *run  = fid_run_new(filt, &funcp);
 * void      *buf  = fid_run_newbuf(run);   // heap alloc
 * free(filt);
 *
 * // ── Run phase (RT-safe, zero-alloc) ──────────────────────────────────
 * double out = funcp(buf, in_sample);
 *
 * // ── Free phase ────────────────────────────────────────────────────────
 * fid_run_freebuf(buf);
 * fid_run_free(run);
 * @endcode
 *
 * @par Fispec DSL — format
 * @code
 * <type><order>/<freq>          e.g.  LpBu4/400
 * <type><order>/<freq0>-<freq1> e.g.  BpBu2/1000-2000
 * <type><order>/<ripple>/<freq> e.g.  LpCh4/-0.5/1000
 * @endcode
 * Frequencies are in Hz when a sample rate is provided, or as a
 * fraction of the sample rate (0 … 0.5) when rate = 1.0.
 *
 * @author  Jim Peters <http://uazu.net/> — original fidlib
 * @author  JamesHight — community patches, const-correctness, C++ guards
 * @copyright LGPL 2.1
 */

#ifndef FIDLIB_H
#define FIDLIB_H

/**
 * @defgroup fidlib fidlib — Runtime Filter Library
 * @{
 */

//
//	Portability macros
//

/**
 * @defgroup fidlib_macros Portability and helper macros
 * @ingroup fidlib
 * @{
 */

#if defined(__GNUC__) || defined(__clang__)
/** @brief Mark a function as non-returning (GCC/Clang). */
#  define FID_NORETURN    __attribute__((noreturn))
/** @brief Branch-prediction hint: condition is likely true (GCC/Clang). */
#  define FID_LIKELY(x)   __builtin_expect(!!(x), 1)
/** @brief Branch-prediction hint: condition is likely false (GCC/Clang). */
#  define FID_UNLIKELY(x) __builtin_expect(!!(x), 0)
#elif defined(_MSC_VER)
#  define FID_NORETURN    __declspec(noreturn)
#  define FID_LIKELY(x)   (x)
#  define FID_UNLIKELY(x) (x)
#else
#  define FID_NORETURN
#  define FID_LIKELY(x)   (x)
#  define FID_UNLIKELY(x) (x)
#endif

/** @} */ /* fidlib_macros */

/**
 * @defgroup fidlib_types Core data types
 * @ingroup fidlib
 * @{
 */

/**
 * @brief One stage (sub-filter) in a cascade filter chain.
 *
 * A complete FidFilter is a **linked list** of FidFilter nodes terminated by
 * a node with `typ == 0` and `len == 0`.  Each node represents one second-
 * order section (biquad) or first-order section of the complete filter.
 *
 * Memory layout of a node with `len` coefficients:
 * @code
 * [ FidFilter header (typ, cbm, len) ][ val[0] … val[len-1] ]
 * @endcode
 *
 * All nodes in a filter are allocated in a single contiguous block; the
 * terminator node immediately follows the last coefficient array.
 *
 * @dot
 * digraph FidFilterChain {
 *   rankdir=LR;
 *   node [shape=record, fontname=Helvetica, fontsize=10];
 *   n0 [label="{typ='I'|cbm|len=3|val[0..2]}"];
 *   n1 [label="{typ='F'|cbm|len=3|val[0..2]}"];
 *   n2 [label="{typ=0|len=0}"];
 *   n0 -> n1 [label="FFNEXT"];
 *   n1 -> n2 [label="FFNEXT"];
 * }
 * @enddot
 *
 * @ingroup fidlib_types
 */
typedef struct FidFilter {
   short typ;  /**< Node type: `'I'` = IIR section, `'F'` = FIR section, `0` = end-of-list. */
   short cbm;  /**< Constant-coefficient bitmap.  Bit @e k is set if `val[k]` is a
                    structural constant (e.g. the leading `1.0` in a biquad denominator)
                    and need not be stored in the running-filter buffer. */
   int   len;  /**< Number of `double` values in @ref val.  Zero for the terminator node. */
   double val[1]; /**< Coefficient array (flexible in practice; `val[1]` for C++ portability). */
} FidFilter;

/**
 * @brief Advance to the next sub-filter node (mutable pointer).
 *
 * Equivalent to `(FidFilter*)((ff)->val + (ff)->len)`.
 *
 * @param ff  Pointer to the current FidFilter node.
 * @return    Pointer to the immediately following node.
 */
#define FFNEXT(ff)  ((FidFilter*)(      (ff)->val + (ff)->len))

/**
 * @brief Advance to the next sub-filter node (const-correct pointer).
 *
 * @param ff  `const FidFilter *` pointer to the current node.
 * @return    `const FidFilter *` pointer to the following node.
 */
#define FFCNEXT(ff) ((const FidFilter*)((ff)->val + (ff)->len))

/**
 * @brief Byte size of a single FidFilter node carrying @p cnt coefficients.
 *
 * @param cnt  Number of `double` values to store in @ref FidFilter::val.
 */
#define FFSIZE(cnt) (sizeof(FidFilter) - sizeof(double) + (size_t)(cnt) * sizeof(double))

/**
 * @brief Byte size of a contiguous block holding a complete filter chain.
 *
 * Computes the memory needed for a flat array of @p n_head FidFilter nodes
 * sharing @p n_val `double` coefficients, plus one end-of-list terminator.
 *
 * @param n_head  Number of non-terminator filter nodes.
 * @param n_val   Total number of coefficient doubles across all nodes.
 */
#define FFCSIZE(n_head,n_val) \
   ((sizeof(FidFilter)-sizeof(double))*((size_t)(n_head)+1U) + sizeof(double)*(size_t)(n_val))

/**
 * @brief Allocate a new FidFilter chain block via the internal `Alloc()` function.
 *
 * @note  Internal use only; requires `Alloc()` to be in scope (i.e. inside fidlib.c).
 * @param n_head  Number of non-terminator nodes.
 * @param n_val   Total number of coefficient doubles.
 */
#define FFALLOC(n_head,n_val) (FidFilter*)Alloc(FFCSIZE(n_head, n_val))

/**
 * @brief Opaque handle returned by fid_run_new().
 *
 * Holds the compiled filter execution state (coefficient table, command
 * sequence).  One `FidRun` can service any number of independent channel
 * buffers created with fid_run_newbuf().
 */
typedef void FidRun;

/**
 * @brief Signature of the per-sample filter function.
 *
 * A pointer to a function of this type is returned by fid_run_new() and
 * must be called once per input sample during the run phase.
 *
 * @param buf    Channel state buffer allocated by fid_run_newbuf().
 * @param input  Input sample value.
 * @return       Filtered output sample.
 *
 * @rtSafe
 */
typedef double (FidFunc)(void *buf, double input);

/** @} */ /* fidlib_types */

/**
 * @defgroup fidlib_api Public API functions
 * @ingroup fidlib
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup fidlib_error Error handling
 * @ingroup fidlib_api
 * @{
 */

/**
 * @brief Install a custom error handler.
 *
 * By default fidlib prints the error message to `stderr` and calls `exit(1)`.
 * For real-time or embedded use, install a handler that sets an error flag
 * instead; the handler **may return** (fidlib will then return `NULL` or zero
 * from the failing call).
 *
 * @param rout  Handler function receiving a NUL-terminated message string, or
 *              `NULL` to restore the default handler.
 *
 * @par Example — non-aborting RT handler
 * @code
 * static volatile int fid_error_flag = 0;
 * static void my_handler(const char *msg) {
 *     fprintf(stderr, "fidlib: %s\n", msg);
 *     fid_error_flag = 1;
 * }
 * // ...
 * fid_set_error_handler(my_handler);
 * @endcode
 */
extern void fid_set_error_handler(void(*rout)(const char *));

/** @} */ /* fidlib_error */

/**
 * @defgroup fidlib_info Library information
 * @ingroup fidlib_api
 * @{
 */

/**
 * @brief Return the fidlib version string.
 *
 * @return  Statically allocated version string, e.g. `"0.9.11"`.
 */
extern const char *fid_version(void);

/**
 * @brief Print a human-readable list of all supported filter types to @p out.
 *
 * Each line contains the spec prefix, a parameter description, and a brief
 * description.  Useful for `--help` output in CLI tools.
 *
 * @param out  Output stream (e.g. `stdout`).
 * @threadUnsafe
 */
extern void fid_list_filters(FILE *out);

/**
 * @brief Write the filter type list into a caller-supplied buffer.
 *
 * @param buf     Start of the destination buffer.
 * @param bufend  One past the last writable byte.
 * @return        Number of characters written (excluding NUL terminator).
 * @threadUnsafe
 */
extern int fid_list_filters_buf(char *buf, char *bufend);

/** @} */ /* fidlib_info */

/**
 * @defgroup fidlib_design Filter design
 * @ingroup fidlib_api
 * @{
 */

/**
 * @brief Design a filter from a fispec string.
 *
 * Parses @p spec, designs the requested filter for the given sample rate and
 * corner frequencies, and returns a newly allocated FidFilter chain.
 *
 * @p freq0 and @p freq1 override frequencies embedded in @p spec; pass `-1.0`
 * to use the spec-embedded values.  When @p f_adj is non-zero the corner
 * frequencies are auto-adjusted so that the −3 dB point falls exactly at the
 * requested frequency (Butterworth and Bessel only).
 *
 * @param spec   Fispec string, e.g. `"LpBu4/400"`, `"BpBu2/1000-2000"`.
 * @param rate   Sample rate in Hz (e.g. `44100.0`).  Use `1.0` to specify
 *               corner frequencies as fractions of the sample rate.
 * @param freq0  First corner frequency override in Hz, or `-1.0`.
 * @param freq1  Second corner frequency override in Hz (band filters), or `-1.0`.
 * @param f_adj  Non-zero to auto-adjust corner frequency for exact −3 dB.
 * @param descp  If non-NULL, receives a pointer to a newly allocated human-
 *               readable description string (caller must `free()`).
 * @return  Newly allocated FidFilter chain (caller must `free()`), or `NULL`
 *          on error if a custom error handler has been installed.
 *
 * @notRtSafe
 *
 * @par Examples
 * @fidspec{LpBu4/400}
 * 4th-order Butterworth lowpass, −3 dB at 400 Hz, 44100 Hz sample rate.
 *
 * @fidspec{BsBu10/239-247}
 * 10th-order Butterworth bandstop, notch 239–247 Hz.
 *
 * @fidspec{LpCh4/-0.5/1000}
 * 4th-order Chebyshev lowpass, 0.5 dB passband ripple, −3 dB at 1000 Hz.
 */
extern FidFilter *fid_design(const char *spec, double rate, double freq0, double freq1,
                             int f_adj, char **descp);

/**
 * @brief Fill a plain `double` array with filter coefficients.
 *
 * Alternative to fid_design() for codecs or plugins that expect a flat
 * coefficient array rather than a FidFilter chain.  The array size @p n_coef
 * must match the number of non-constant coefficients exactly; use a known
 * filter order to determine this, or call fid_design() first and inspect the
 * chain.
 *
 * @param coef    Output array of length @p n_coef.
 * @param n_coef  Exact number of non-constant coefficients expected.
 * @param spec    Fispec string.
 * @param rate    Sample rate in Hz.
 * @param freq0   First corner frequency in Hz.
 * @param freq1   Second corner frequency in Hz (or −1.0).
 * @param adj     Auto-adjust flag (see fid_design()).
 * @return  Overall gain factor.
 *
 * @notRtSafe
 */
extern double fid_design_coef(double *coef, int n_coef, const char *spec,
                              double rate, double freq0, double freq1, int adj);

/**
 * @brief Reconstruct a FidFilter chain from a flat coefficient array.
 *
 * Inverse of fid_design_coef().  The array must have been produced by a
 * prior call to fid_design_coef() or follow the same layout convention.
 *
 * @param arr  Flat coefficient array (NUL-terminated by a trailing zero).
 * @return  Newly allocated FidFilter chain (caller must `free()`).
 *
 * @notRtSafe
 */
extern FidFilter *fid_cv_array(double *arr);

/**
 * @brief Flatten a multi-stage filter into a single IIR/FIR pair.
 *
 * Convolves all sub-filters in @p filt into one combined filter with a
 * single IIR section and a single FIR section.  The result is faster to
 * execute but less numerically stable than the cascade form, especially
 * for high-order filters.
 *
 * @param filt  Filter to flatten (caller retains ownership).
 * @return  Newly allocated flattened FidFilter chain (caller must `free()`).
 *
 * @notRtSafe
 */
extern FidFilter *fid_flatten(FidFilter *filt);

/**
 * @brief Concatenate two or more filters into a series cascade.
 *
 * The resulting filter is equivalent to applying each input filter in
 * series.  Pass the filters as a `NULL`-terminated variadic argument list.
 *
 * @param freeme  If non-zero, all input FidFilter pointers are `free()`d
 *                after concatenation.
 * @param ...     `FidFilter *` arguments terminated by `NULL`.
 * @return  Newly allocated concatenated FidFilter chain (caller must `free()`).
 *
 * @notRtSafe
 *
 * @par Example — series lowpass + highpass (bandpass equivalent)
 * @code
 * FidFilter *lp = fid_design("LpBu2/1000", rate, -1, -1, 0, NULL);
 * FidFilter *hp = fid_design("HpBu2/500",  rate, -1, -1, 0, NULL);
 * FidFilter *bp = fid_cat(1, lp, hp, NULL);  // frees lp and hp
 * @endcode
 */
extern FidFilter *fid_cat(int freeme, ...);

/**
 * @brief Rewrite a fispec string with substituted frequency values.
 *
 * Expands @p freq0 / @p freq1 / @p adj into @p spec, producing a full
 * canonical spec string and, optionally, a minimum-parameter variant.
 *
 * @param spec    Input spec (may contain placeholder frequencies).
 * @param freq0   First corner frequency to substitute.
 * @param freq1   Second corner frequency to substitute.
 * @param adj     Adjustment flag.
 * @param spec1p  Receives full spec string (caller must `free()`).
 * @param spec2p  Receives minimum spec string (caller must `free()`), or `NULL`.
 * @param freq0p  Receives resolved freq0.
 * @param freq1p  Receives resolved freq1.
 * @param adjp    Receives resolved adj flag.
 */
extern void fid_rewrite_spec(const char *spec, double freq0, double freq1, int adj,
                             char **spec1p, char **spec2p,
                             double *freq0p, double *freq1p, int *adjp);

/**
 * @brief Parse a multi-filter spec string, designing each filter in turn.
 *
 * Walks the string pointer @p *pp through a comma- or space-separated list
 * of fispec tokens, designing each filter and linking the results via
 * fid_cat().  Stops at end-of-string or an unrecognised token.
 *
 * @param rate  Sample rate in Hz.
 * @param pp    Pointer to the current parse position; updated on return.
 * @param ffp   Receives the designed FidFilter (caller must `free()`).
 * @return  Error message string, or `NULL` on success.
 *
 * @notRtSafe
 */
extern char *fid_parse(double rate, char **pp, FidFilter **ffp);

/** @} */ /* fidlib_design */

/**
 * @defgroup fidlib_analysis Filter analysis
 * @ingroup fidlib_api
 * @{
 */

/**
 * @brief Compute the magnitude response (and optionally phase) at a frequency.
 *
 * Evaluates the transfer function of @p filt at the normalised frequency
 * @p freq (0 = DC, 0.5 = Nyquist).  Thread-safe; no allocation.
 *
 * @param filt   Filter chain to analyse.
 * @param freq   Normalised frequency in [0, 0.5].
 * @param phase  If non-NULL, receives the phase response normalised to [0, 1]
 *               (0 = 0°, 0.5 = ±180°).
 * @return  Magnitude response (linear, not dB).  0 = full attenuation, 1 = unity gain.
 *
 * @rtSafe
 *
 * @par Example — −3 dB check
 * @code
 * double mag = fid_response(filt, 400.0 / 44100.0);
 * assert(fabs(mag - 0.7071) < 0.01);
 * @endcode
 */
extern double fid_response_pha(const FidFilter *filt, double freq, double *phase);

/**
 * @brief Compute the magnitude response at a normalised frequency.
 *
 * Convenience wrapper around fid_response_pha() that discards the phase.
 *
 * @param filt  Filter chain.
 * @param freq  Normalised frequency in [0, 0.5].
 * @return  Magnitude response (linear).
 *
 * @rtSafe
 */
extern double fid_response(const FidFilter *filt, double freq);

/**
 * @brief Estimate the group delay (impulse-response length) of a filter.
 *
 * Returns the number of samples after which the impulse response has
 * substantially decayed.  Useful for latency calculations and buffer sizing.
 *
 * @param filt  Filter chain.
 * @return  Estimated delay in samples.
 */
extern int fid_calc_delay(const FidFilter *filt);

/** @} */ /* fidlib_analysis */

/**
 * @defgroup fidlib_run Filter execution (run phase)
 * @ingroup fidlib_api
 *
 * @par Three-phase execution model
 * @dot
 * digraph RunPhases {
 *   rankdir=LR;
 *   node [shape=box, fontname=Helvetica, fontsize=10, style=filled];
 *   A [label="fid_design()\nfid_run_new()", fillcolor="#d4e8ff"];
 *   B [label="fid_run_newbuf()\nor\nfid_run_initbuf()", fillcolor="#d4e8ff"];
 *   C [label="funcp(buf, sample)\n[RT-safe, repeating]", fillcolor="#d4ffd4"];
 *   D [label="fid_run_freebuf()\nfid_run_free()\nfree(filt)", fillcolor="#ffd4d4"];
 *   A -> B [label="alloc phase"];
 *   B -> C [label="run phase"];
 *   C -> C [label="per sample"];
 *   C -> D [label="shutdown"];
 * }
 * @enddot
 *
 * @{
 */

/**
 * @brief Compile a filter chain for execution.
 *
 * Translates the FidFilter coefficient list into an optimised command
 * sequence for the run phase.  One FidRun can serve multiple independent
 * channel buffers.
 *
 * @param filt    Filter chain produced by fid_design().  The filter is
 *                inspected here; it may be `free()`d after this call.
 * @param funcpp  Receives a pointer to the per-sample step function.
 *                Call `(*funcpp)(buf, sample)` during the run phase.
 * @return  Opaque FidRun handle (pass to fid_run_newbuf(), fid_run_free()).
 *
 * @notRtSafe
 */
extern void *fid_run_new(const FidFilter *filt, double(**funcpp)(void *, double));

/**
 * @brief Allocate a per-channel state buffer.
 *
 * Each independent audio channel (or independent filter instance) needs its
 * own buffer.  All buffers derived from the same FidRun share the compiled
 * coefficient table; only the delay-line state is per-buffer.
 *
 * @param run  FidRun handle from fid_run_new().
 * @return  Newly heap-allocated, zero-initialised buffer (pass to the step
 *          function; free with fid_run_freebuf()).
 *
 * @notRtSafe
 */
extern void *fid_run_newbuf(void *run);

/**
 * @brief Return the required byte size for a channel state buffer.
 *
 * Use this together with fid_run_initbuf() to initialise a buffer in
 * pre-allocated memory (pool allocator, stack, shared memory, …).
 *
 * @param run  FidRun handle.
 * @return  Number of bytes required.
 */
extern int fid_run_bufsize(void *run);

/**
 * @brief Initialise a channel state buffer in caller-supplied memory.
 *
 * Equivalent to fid_run_newbuf() but operates on memory supplied by the
 * caller.  The memory must be at least fid_run_bufsize() bytes and must
 * remain valid for the lifetime of the buffer.
 *
 * @param run  FidRun handle.
 * @param buf  Caller-supplied memory block of at least fid_run_bufsize() bytes.
 *
 * @rtSafe (no allocation)
 */
extern void fid_run_initbuf(void *run, void *buf);

/**
 * @brief Reset a channel buffer to the zero-state (silence).
 *
 * Clears the internal delay line to all zeros.  Equivalent to discarding
 * the buffer and creating a fresh one, but without allocation.
 *
 * @param buf  Channel buffer to reset.
 *
 * @rtSafe
 */
extern void fid_run_zapbuf(void *buf);

/**
 * @brief Free a channel state buffer allocated by fid_run_newbuf().
 *
 * Do **not** call this on buffers initialised with fid_run_initbuf()
 * (inplace); manage their memory directly.
 *
 * @param runbuf  Buffer to free.
 */
extern void fid_run_freebuf(void *runbuf);

/**
 * @brief Free a compiled FidRun object.
 *
 * All channel buffers derived from @p run must be freed before this call.
 *
 * @param run  FidRun handle to free.
 */
extern void fid_run_free(void *run);

/**
 * @brief RT-safe in-place buffer initialisation (alias for fid_run_initbuf()).
 *
 * @param run  FidRun handle.
 * @param mem  Pre-allocated memory of at least fid_run_bufsize() bytes.
 *
 * @rtSafe
 */
#define fid_run_newbuf_inplace(run, mem) fid_run_initbuf((run), (mem))

/** @} */ /* fidlib_run */

/** @} */ /* fidlib_api */

/** @} */ /* fidlib */

#ifdef __cplusplus
}
#endif

#endif /* FIDLIB_H */
