/**
 * @file scratch.c
 * @brief Implementation of the global scratch buffer.
 *
 * The scratch buffer starts at 32 KB and doubles whenever capacity is exceeded.
 * It is @b not thread-safe; callers must serialise access.
 *
 * @author  Jim Peters
 * @copyright GPL 2.0
 * @ingroup fiview_scratch
 */

//
//	Scratch buffer handling
//
//        Copyright (c) 2002-2003 Jim Peters <http://uazu.net/>.
//        Released under the GNU GPL version 2 as published by the
//        Free Software Foundation.  See the file COPYING for details,
//        or visit <http://www.gnu.org/copyleft/gpl.html>.
//

#include "all.h"

//
//	The scratch buffer is a single buffer that any routine may
//	make use of.  However, it should take care that it doesn't
//	call other routines that use it whilst it is still using it.
//	This is definitely NOT thread-safe.  The buffer starts off at
//	32K, and doubles in size as necessary.
//
//	Typical usage:
//
//	  scr_zap();			// Set to a blank string
//	  scr_wrap(72, ind);		// Set the width and indentation string for wrapping
//	  scr_pr(fmt, ...);		// Append a printf-string
//	  scr_prw(fmt, ...);		// Append a string with word-wrapping
//	  scr_lf();			// Append a '\n'
//	  SCR_PUTC(ch);			// Append a character (macro)
//	  str= scr_dup();		// Strdup the buffer contents (binary data works too)
//
//	Alternatively calls:
//
//	  scr_zap_pr(fmt, ...);		// Equivalent to _zap() then _pr()
//	  str= scr_dup();
//
//	Calls to support writing binary data or structures to scratch:
//
//	  vp= scr_inc(len);		// Make space for zero'd structure, return pointer
//	  scr_wrD(dval);		// Write a double to scratch
//	  scr_wrI(ival);		// Write an int to scratch
//

char *scratch;
int scr_len;		// Length in use
int scr_max;		// Actual allocated size of scratch buffer
int scr_wid;		// Wrap width, initially 78
const char *scr_ind;	// Wrap indentaion, initially ""
int scr_indlen;		// Wrap indentaion length

//
//	Reallocate the scratch buffer, or initialise it the first time
//

void 
scr_realloc() {
   if (!scr_max) {
      scr_max= 32768;
      scratch= (char*)malloc((size_t)scr_max);
   } else {
      scr_max *= 2;
      scratch= (char*)realloc(scratch, (size_t)scr_max);
   }
   if (!scratch) error("Out of memory");
}

//
//	Clear the scratch buffer
//

void 
scr_zap() {
   if (!scr_max) scr_realloc();
   scr_len= 0;
   scr_wid= 78;
   scr_ind= "";
   scr_indlen= 0;
}

//
//	Setup wrapping parameters
//

void
scr_wrap(int wid, const char *ind) {
   scr_wid= wid;
   scr_ind= ind;
   scr_indlen= (int)strlen(ind);
}

//
//	Vararg-list version of scr_pr
//

void
scr_vpr(const char *fmt, va_list ap) {
   while (1) {
      int cnt= vsnprintf(scratch+scr_len, (size_t)(scr_max-scr_len), fmt, ap);
      if (cnt < 0 || cnt + scr_len >= scr_max -1) {
	 scr_realloc();
	 continue;
      }
      scr_len += cnt;
      break;
   }
}

//
//	Append a formatted string to the scratch buffer
//

void
scr_pr(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   scr_vpr(fmt, ap);
}

//
//	Append a formatted string to the scratch buffer with word
//	wrapping.  An NL/indentation break is considered before the
//	start of the string and at the start of every word.
//

void
scr_prw(const char *fmt, ...) {
   va_list ap;
   int i0, i1;
   int nl;		// Index of last NL we know about or -1
   int ww;		// Word-start
   int a, dd;
   char ch;

   va_start(ap, fmt);

   i0= scr_len;
   scr_vpr(fmt, ap);
   i1= scr_len;

   // Initialise 'nl'
   for (nl= i0; nl > 0 && i0-nl-1 < scr_wid && scratch[nl] != '\n'; nl--) ;

   while (i0 < i1) {
      // This is a potential break-point
      for (a= i0; a<i1 && ((ch= scratch[a]) == ' ' || ch == '\t'); a++) ;
      if (scratch[a] == '\n') {
	 if (a != i0) memmove(scratch+i0, scratch+a, (size_t)(i1-a));
	 i1 -= (a-i0);
	 nl= i0; i0++;
	 continue;
      }
      ww= a;
      while (a<i1 && !isspace(scratch[a])) a++;

      // Wrap?
      if (a-nl > scr_wid) {
	 // Each wrap inserts 1+scr_indlen bytes; ensure capacity before memmove
	 while (i1 + 1 + scr_indlen + 1 >= scr_max) scr_realloc();
	 memmove(scratch+i0+1+scr_indlen, scratch+ww, (size_t)(i1-ww));
	 dd= ww-(i0+1+scr_indlen);
	 i1 -= dd; a -= dd;
	 scratch[i0]= '\n'; nl= i0++;
	 memcpy(scratch+i0, scr_ind, (size_t)scr_indlen);
      }
      i0= a;
   }
   scratch[i1]= 0;
   scr_len= i1;
}


//
//	Clear the buffer, then write a formatted string (convenience
//	function)
//

void
scr_zap_pr(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);

   scr_zap();
   scr_vpr(fmt, ap);
}

//
//	Append a newline character
//

void 
scr_lf() {
   if (scr_len + 2 >= scr_max) scr_realloc();
   scratch[scr_len++]= '\n';
   scratch[scr_len]= 0;
}

//
//	Generate a StrDup'd string from the buffer.  This handles
//	binary data as well, although an extra NUL is always added.
//

char *
scr_dup() {
   char *rv= (char*)malloc((size_t)scr_len + 1U);
   if (!rv) error("Out of memory");
   memcpy(rv, scratch, (size_t)scr_len);
   rv[scr_len]= 0;
   return rv;
}

//
//	Reserve space for a structure, zero it, and increase the
//	end-pointer.  Returns a void* for the structure space.
//	Remember that scratch memory may be copied around, so don't
//	store this pointer anywhere -- just use it for filling in the
//	structure.
//

void *
scr_inc(int len) {
   void *rv;
   while (scr_len + len >= scr_max)
      scr_realloc();
   rv= scratch + scr_len;
   memset(rv, 0, (size_t)len);
   scr_len += len;
   return rv;
}

//
//	Write a double to the scratch buffer
//

void 
scr_wrD(double dval) {
   if (scr_len + (int)sizeof(double) >= scr_max) scr_realloc();
   memcpy(scratch+scr_len, &dval, sizeof(double));
   scr_len += (int)sizeof(double);
}

//
//	Write a double to the scratch buffer
//

void 
scr_wrI(int ival) {
   if (scr_len + (int)sizeof(int) >= scr_max) scr_realloc();
   memcpy(scratch+scr_len, &ival, sizeof(int));
   scr_len += (int)sizeof(int);
}

// END //
