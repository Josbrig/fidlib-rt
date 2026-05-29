/**
 * @file helptext.h
 * @brief Help text generator for the built-in fiview pager.
 * @ingroup fiview
 */

#ifndef HELPTEXT_H
#define HELPTEXT_H

/**
 * @brief Generate the complete help text string.
 *
 * Assembles the full help document (keyboard shortcuts, usage notes) from the
 * static @c helptext_src template together with dynamically discovered filter
 * types from fidlib.
 *
 * @return Heap-allocated NUL-terminated help string.  The caller is responsible
 *         for freeing it.
 * @ingroup fiview
 */
char *gen_helptext(void);

#endif
