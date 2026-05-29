/**
 * @file scratch.h
 * @brief Single shared scratch buffer with printf-style and binary-append helpers.
 *
 * The scratch buffer grows automatically (doubles each time it fills) and
 * is always NUL-terminated.  It is **not** thread-safe; callers must avoid
 * re-entrant use while building a string.
 *
 * ### Typical usage
 * @code
 * scr_zap();
 * scr_wrap(72, "  ");
 * scr_pr("Value: %g\n", val);
 * char *result = scr_dup();   // caller owns the returned string
 * @endcode
 *
 * @defgroup fiview_scratch Scratch buffer
 * @ingroup fiview
 * @{
 */

#ifndef SCRATCH_H
#define SCRATCH_H

#include <stdarg.h>

extern char       *scratch;    /**< Current scratch buffer base pointer. */
extern int         scr_len;    /**< Number of bytes currently in use (excluding NUL). */
extern int         scr_max;    /**< Allocated capacity of @c scratch. */
extern int         scr_wid;    /**< Line width used by scr_prw() word-wrap. */
extern const char *scr_ind;    /**< Indentation prefix inserted after each wrap. */
extern int         scr_indlen; /**< Byte length of @c scr_ind. */

void  scr_realloc(void);                    /**< Double the scratch buffer capacity (or initialise to 32 KB). */
void  scr_zap(void);                        /**< Reset scratch to an empty string (length 0, NUL at [0]). */
void  scr_wrap(int wid, const char *ind);   /**< Set word-wrap width and indentation for scr_prw(). */
void  scr_vpr(const char *fmt, va_list ap); /**< vprintf-style append to scratch. */
void  scr_pr(const char *fmt, ...);         /**< printf-style append to scratch. */
void  scr_prw(const char *fmt, ...);        /**< printf-style append with word-wrapping at #scr_wid. */
void  scr_zap_pr(const char *fmt, ...);     /**< Shorthand: scr_zap() then scr_pr(). */
void  scr_lf(void);                         /**< Append a single newline character. */
char *scr_dup(void);                        /**< Duplicate the current scratch content; caller must free(). */
void *scr_inc(int len);                     /**< Reserve @p len zeroed bytes and return a pointer to them. */
void  scr_wrD(double dval);                 /**< Append a raw `double` (binary, not text). */
void  scr_wrI(int ival);                    /**< Append a raw `int` (binary, not text). */

/**
 * @brief Append a single character to the scratch buffer.
 *
 * Ensures there is space for at least one more byte plus the NUL terminator,
 * reallocating if necessary.
 *
 * @param ch  Character to append (must fit in `char`).
 */
#define SCR_PUTC(ch) \
   do { \
      if (scr_len + 2 >= scr_max) scr_realloc(); \
      scratch[scr_len++] = (ch); \
      scratch[scr_len]   = 0; \
   } while (0)

/** @} */ /* end fiview_scratch */

#endif
