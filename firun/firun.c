/**
 * @file firun.c
 * @brief firun — command-line filter execution tool.
 *
 * Reads sample data from stdin, applies one or more fidlib filter specifications,
 * and writes the filtered output to stdout.  Both ASCII and packed binary formats
 * are supported; multi-channel operation maps one filter per output channel.
 *
 * ### Usage
 * @code
 * firun [options] <rate> <format> <filter-spec> [<filter-spec> ...]
 * @endcode
 *
 * ### Format codes
 * | Code | Type                              |
 * |------|-----------------------------------|
 * | `a`  | ASCII floating-point              |
 * | `b`  | Unsigned 8-bit                    |
 * | `c`  | Signed 8-bit                      |
 * | `w`  | Unsigned 16-bit little-endian     |
 * | `W`  | Unsigned 16-bit big-endian        |
 * | `s`  | Signed 16-bit little-endian       |
 * | `S`  | Signed 16-bit big-endian          |
 * | `f`  | 32-bit float (machine order)      |
 * | `d`  | 64-bit double (machine order)     |
 * | `_`  | Skip / dummy input byte           |
 * | `%I` | Synthetic impulse (special input) |
 * | `%S` | Synthetic step    (special input) |
 *
 * @author  Jim Peters
 * @copyright GPL 2.0
 * @ingroup firun
 */

/**
 * @defgroup firun firun — CLI filter executor
 * @brief Command-line tool that pipes raw/ASCII sample data through fidlib filters.
 */

//
//	Run fidlib filters on raw data
//
//        Copyright (c) 2004 Jim Peters <http://uazu.net/>.
//        Released under the GNU GPL version 2 as published by the
//        Free Software Foundation.  See the file COPYING for details,
//        or visit <http://www.gnu.org/copyleft/gpl.html>.
//
//	Note that this is fast, but speed increases can still be
//	obtained with special-purpose code.  As an example, one tested
//	set of filters for working on stereo 16-bit streams were twice
//	as fast when generated with fiview (using the fixed
//	coefficient example code) and put in a special-purpose wrapper
//	that only handled 16-bit integers.
//

#define NL "\n"

static const char *const usage_text=
"Usage: firun [options] <sampling_rate> <in_out_formats> <filter-specs...>"
NL ""
NL "Reads ASCII or raw data from STDIN, filters it, writes ASCII or raw data "
NL "to STDOUT.  See firun.txt for full details.  Brief summary:"
NL ""
NL "  <in_out_formats>: <format>  or  <format>/<format>"
NL "  <format>: %I | %R | ([a_bwWcsSfd]<count-digit>*)+"
NL ""
NL "Format characters:"
NL "  a   ASCII text-formatted value       _   Dummy input byte"
NL "  b   Unsigned byte                    c   Signed byte (C 'char')"
NL "  w   Unsigned little-endian 16-bit    s   Signed little-endian 16-bit"
NL "  W   Unsigned big-endian 16-bit       S   Signed big-endian 16-bit (C 'short')"
NL "  f   Machine-format 32-bit float      d   Machine-format 64-bit double"
NL ""
NL "Special input formats:"
NL "  %I  Ignore STDIN, generate impulse as input: 1 0 0 0 0 0 ..."
NL "  %S  Ignore STDIN, generate step as input: 1 1 1 1 1 1 ..."
NL ""
NL "Options:"
NL "  -d <dur>   Limit output to given duration, specified as:"
NL "               [<hours>h][<minutes>m][<seconds>s][<samples>], for example:"
NL "               '1h20m' or '250s' or '45m' or '88200' (samples)."
NL "  -D         Dump filters along with the signal delay in samples."
NL "  -n <N>     Shorthand: expand single-char format to N channels (e.g. -n2 f = ff)."
NL "  -r <cnt>   Ignore STDIN, output response of filters."
NL "  -rp <cnt>  Ignore STDIN, output response of filters, including phase."
NL "  -s         Streaming mode: disable stdout buffering for low latency."
NL "  -L         Ignore following arguments, display list of filter types."
;


//
//	Includes
//

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include "fidlib.h"

#ifdef _MSC_VER
#  include <io.h>
   typedef long long ssize_t;
#  define read  _read
#else
#  include <unistd.h>
#endif

typedef unsigned char uchar;

//
//	Support code
//

static FID_NORETURN void
error(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   fprintf(stderr, "firun: ");
   vfprintf(stderr, fmt, ap);
   fprintf(stderr, "\n");
   exit(1);
}

static FID_NORETURN void
usage(void) {
   error("based on fidlib version %s.\n"
	 "Copyright (c) 2004 Jim Peters http://uazu.net, licensed under the GNU GPL v2.\n"
	 "\n%s", fid_version(), usage_text);
}

static void *
Alloc(size_t size) {
   void *vp= calloc(1, size);
   if (!vp) error("Out of memory");
   return vp;
}

#define ALLOC(type) ((type*)Alloc(sizeof(type)))
#define ALLOC_ARR(cnt, type) ((type*)Alloc((size_t)(cnt) * sizeof(type)))


/** @name Command-line flags */
const char *f_dur;     /**< Raw duration string from -d, or NULL. */
int dur;               /**< Output sample limit, -1 = unlimited. */
int f_dump;            /**< -D: dump filter structure and signal delay then exit. */
int f_resp;            /**< -r N: output frequency response for N points (0 = off). */
int f_phase;           /**< -rp: include phase alongside amplitude response. */
int f_impulse;         /**< Input source is a synthetic impulse (1, 0, 0, …). */
int f_step;            /**< Input source is a synthetic step (1, 1, 1, …). */
int f_streaming;       /**< -s: disable stdout buffering for low-latency piping. */
int f_channels;        /**< -n N: expand single-char format to N channels (0 = off). */

double rate;           /**< Sampling rate in Hz. */

/** @name I/O format state */
char *ispec;           /**< Expanded input format string (one char per channel), or NULL for synthetic inputs. */
char *ospec;           /**< Expanded output format string (one char per channel). */
int   n_chan;          /**< Number of output channels derived from @c ospec. */

/** @name Filter list */
int        n_filt;     /**< Number of loaded filter specifications. */
FidFilter **filt;      /**< Array of @c n_filt heap-allocated @ref FidFilter pointers. */

/** @name Input buffer */
char *inbuf;           /**< 16 KB circular read buffer. */
char *inbufend;        /**< One past the last byte of @c inbuf (NUL sentinel). */
char *inp;             /**< Current read position within @c inbuf. */
char *inend;           /**< One past the last loaded byte in @c inbuf. */
int   ineof;           /**< Non-zero once stdin has returned EOF. */

/**
 * @brief Parse a format spec string and expand it into a NUL-terminated channel list.
 *
 * E.g. `"s2"` → `"ss"`, `"af"` → `"af"`, `"%I"` → sets #f_impulse and returns NULL.
 *
 * @param spec  Format string from the command line.
 * @param in    Non-zero for an input spec (allows `_` skip and `%I`/`%S` specials).
 * @return Heap-allocated expanded string, or NULL for synthetic-input specials.
 *         Caller owns the allocation.
 * @ingroup firun
 */
static char *
decode_spec(const char *spec, int in) {
   const char *p;
   int len= 0;
   char *rv= NULL, *sp= NULL;
   int pass;

   // Check for specials
   if (0 == strcmp(spec, "%I")) { 
      if (!in) error("%%I not valid as an output spec");
      f_impulse= 1; return 0; 
   }
   if (0 == strcmp(spec, "%S")) { 
      if (!in) error("%%S not valid as an output spec");
      f_step= 1; return 0; 
   }

   // First pass to check everything and count length
   // Second pass to fill in string
   for (pass= 0; pass<2; pass++) {
      for (p= spec; *p; ) {
	 int cnt= 0;
	 int typ= *p++;
	 if (!strchr(in ? "_abwWcsSfd" : "abwWcsSfd", typ))
	    error("Bad %s format, unknown type '%c': %s",
		  in ? "input" : "output", typ, spec);
	 if (isdigit(*p)) {
	    while (isdigit(*p)) cnt= cnt*10 + *p++ - '0';
	 } else {
	    cnt= 1;
	 }
	 if (pass == 0) {
	    len += cnt;
	 } else {
	    while (cnt-- > 0) 
	       *sp++= (char)typ;
	 }
      }
      if (pass == 0) {
	 rv= ALLOC_ARR(len+1, char);
	 sp= rv;
      }
   }

   return rv;
}

/**
 * @brief Count the number of data channels in an expanded format string.
 *
 * Skip (`_`) bytes are not counted as data channels.
 *
 * @param spec  Expanded format string as returned by decode_spec().
 * @return Number of non-skip channels.
 * @ingroup firun
 */
static int
spec_count(const char *spec) {
   int cnt= 0;
   char typ;
   while ((typ= *spec++)) 
      if (typ != '_') cnt++;
   return cnt;
}

/**
 * @brief Parse one or more comma-separated filter specs and populate #filt / #n_filt.
 *
 * Calls fid_parse() for each token in @p txt.  Aborts via error() on any
 * malformed specification.
 *
 * @param txt  Mutable, NUL-terminated string containing space- or comma-separated
 *             fidlib filter specifications (e.g. `"LpBu4/1000 HpBu2/200"`).
 * @ingroup firun
 */
static void
parse_filters(char *txt) {
   FidFilter *ff, **arr;
   n_filt= 0;

   while (*txt) {
      char *err= fid_parse(rate, &txt, &ff);
      if (err) error("Bad filter-specification:\n  %s", err);

      // Realloc each time, but not big hit
      arr= ALLOC_ARR(n_filt+1, FidFilter*);
      if (filt) {
	 memcpy(arr, filt, (size_t)n_filt * sizeof(FidFilter*));
	 free(filt);
      }
      filt= arr;
      filt[n_filt++]= ff;

      while (isspace(*txt)) txt++;
      if (*txt == ',') txt++;
      while (isspace(*txt)) txt++;
   }
}

/**
 * @brief Write one sample value to stdout in the requested format.
 *
 * Advances the format pointer @p op by one character (the format code),
 * writes the formatted bytes, and returns the updated pointer.
 *
 * @param op   Pointer into the current position of the expanded output spec.
 * @param val  Sample value in the range [-1.0, +1.0] for integer formats.
 * @return     @p op advanced past the consumed format character.
 * @ingroup firun
 */
static char *
output(char *op, double val) {
   char buf[32];
   int len= 0;
   int iv;

   switch (*op++) {
    case 'a': 
       len= sprintf(buf, "%.16g%c", val, *op ? ' ' : '\n'); break;
    case 'b': iv= (int)((val+1) * 128); iv= iv<0 ? 0 : iv>255 ? 255 : iv;
       buf[0]= (char)(unsigned char)iv; len= 1; break;
    case 'w': iv= (int)((val+1) * 32768); iv= iv<0 ? 0 : iv>65535 ? 65535 : iv;
       buf[0]= (char)(unsigned char)iv; buf[1]= (char)(unsigned char)(iv >> 8); len= 2; break;
    case 'W': iv= (int)((val+1) * 32768); iv= iv<0 ? 0 : iv>65535 ? 65535 : iv;
       buf[1]= (char)(unsigned char)iv; buf[0]= (char)(unsigned char)(iv >> 8); len= 2; break;
    case 'c': iv= (int)(val * 128); iv= iv<-128 ? -128 : iv>127 ? 127 : iv;
       buf[0]= (char)iv; len= 1; break;
    case 's': iv= (int)(val * 32768); iv= iv<-32768 ? -32768 : iv>32767 ? 32767 : iv;
       buf[0]= (char)(unsigned char)iv; buf[1]= (char)(unsigned char)(iv >> 8); len= 2; break;
    case 'S': iv= (int)(val * 32768); iv= iv<-32768 ? -32768 : iv>32767 ? 32767 : iv;
       buf[1]= (char)(unsigned char)iv; buf[0]= (char)(unsigned char)(iv >> 8); len= 2; break;
    case 'f':
       *(float*)&buf[0]= (float)val; len= (int)sizeof(float); break;
    case 'd':
       memcpy(buf, &val, sizeof(double)); len= (int)sizeof(double); break;
    case 0:
       error("Internal error in output() -- ran out of format characters"); break;
    default: 
       error("Internal error in output() -- bad format '%c'", op[-1]); break;
   }

   if (1 != fwrite(buf, (size_t)len, 1, stdout))
      error("Write error");

   return op;
}

/**
 * @brief Refill #inbuf from stdin as much as possible.
 *
 * Compacts unread data to the front of the buffer, then fills the remainder
 * with read() calls.  Sets #ineof on EOF.  Initialises the buffer on first call.
 * @ingroup firun
 */
static void
refill_input(void) {
   if (!inbuf) {
      int len= 16384;	// 100ms of data at 44100Hz 16-bit stereo
      inbuf= ALLOC_ARR(len+1, char);
      inbufend= inbuf+len;
      *inbufend= 0;   // Trailing NUL so that string-reading functions can never overrun
      inp= inend= inbuf;
      ineof= 0;
   }

   // Shift down unread data
   memmove(inbuf, inp, (size_t)(inend-inp));
   inend -= (inp-inbuf);
   inp= inbuf;

   // Try to fill the rest of the space
   while (inend != inbufend && !ineof) {
      ssize_t rv= read(0, inend, (size_t)(inbufend-inend));
      if (rv <= 0) {
	 if (rv == 0) { ineof= 1; continue; }
	 if (errno == EINTR || errno == EAGAIN) continue;
	 error("Read error on stdin: %s", strerror(errno));
      }
      inend += rv;
   }
}

//
//	Input a value from STDIN.  In the case of format-code 'a',
//	care is taken not to read more than one trailing character, so
//	in theory it should be possible to mix text formats and binary
//	formats reliably (but why would anyone want to do that?)
//

static inline double
input(char **ipp) {
   char *ip= *ipp;
   double val;
   float fv;
   int ch, ch1, ch2;
   int avail;

   if (inend - inp < 32 && !ineof)
      refill_input();

   avail= (int)(inend-inp);

   while (1) {
      switch (*ip++) {
       case '_':
	  if (avail < 1) goto badeof;
	  inp++; continue;
       case 'a':
	  {
	     char *tmp;

	     // Skip WS (maybe lots of it)
	     while (1) {
		while (inp < inend && (ch= *inp) && 
		       (isspace(ch) || ch == ',' || ch == ';')) 
		   inp++;
		if (inp != inend) break;
		refill_input();
	     }
	     if (inend - inp < 128 && !ineof) refill_input();
	     if (inp == inend) goto badeof;

	     val= strtod(inp, &tmp);
	     if (inp == tmp) 
		error("Bad floating-point value:\n %.20s", inp);
	     inp= tmp;
	     if (inp < inend && isspace(*inp)) inp++;
	     break;
	  }
       case 'b':
	  if (avail < 1) goto badeof;
	  ch= *inp++;
	  val= (*inp++ - 128) / 128.0; 
	  break;
       case 'w':
	  if (avail < 2) goto badeof;
	  ch1= *inp++; ch2= *inp++;
	  val= (ch1 + (ch2<<8) - 32768) / 32768.0;
	  break;
       case 'W':
	  if (avail < 2) goto badeof;
	  ch1= *inp++; ch2= *inp++;
	  val= ((ch1<<8) + ch2 - 32768) / 32768.0;
	  break;
       case 'c':
	  if (avail < 1) goto badeof;
	  ch= *inp++;
	  val= ((ch^128) - 128) / 128.0; 
	  break;
       case 's':
	  if (avail < 2) goto badeof;
	  ch1= *inp++; ch2= *inp++;
	  val= (((ch1 + (ch2<<8)) ^ 32768) - 32768) / 32768.0;
	  break;
       case 'S':
	  if (avail < 2) goto badeof;
	  ch1= *inp++; ch2= *inp++;
	  val= ((((ch2<<8) + ch1) ^ 32768) - 32768) / 32768.0;
	  break;
       case 'f':
	  if (avail < (int)sizeof(float)) goto badeof;
	  memcpy((void*)&fv, inp, sizeof(float));
	  inp += sizeof(float);
	  val= fv;
	  break;
       case 'd':
	  if (avail < (int)sizeof(double)) goto badeof;
	  memcpy(&val, inp, sizeof(double));
	  inp += sizeof(double);
	  break;
       case 0:
	  error("Ran out of input format characters in input()");
	  break;
       default:
	  error("Bad format code: '%c'", ip[-1]);
      }
      
      // We've read a value.  Now handle trailing '_'s
      while (*ip == '_') { 
	 if (inp == inend) goto badeof;
	 inp++; ip++; 
      }
      *ipp= ip;
      return val;
   }

 badeof:
   error("End of file within an input record");
   return 0;
}

//
//	Main
//

static void
fid_err_handler(const char *msg) {
   fprintf(stderr, "firun: fidlib: %s\n", msg);
   exit(1);
}

int
main(int ac, char **av) {
   char dmy;
   int a, b;

   fid_set_error_handler(fid_err_handler);
#ifdef SIGPIPE
   signal(SIGPIPE, SIG_IGN);
#endif

   // Decode arguments
   ac--; av++;
   while (ac > 0 && av[0][0] == '-') {
      char ch, *p= 1 + *av++; ac--;
      while ((ch= *p++)) {
	 switch (ch) {
	  case 'd':
	     if (ac <= 0) usage();
	     ac--; f_dur= *av++;
	     break;
	  case 'D':
	     f_dump= 1;
	     break;
	  case 'r':
	     if (ac-- <= 0) usage();
	     if (1 != sscanf(*av++, "%d %c", &f_resp, &dmy) ||
		 f_resp <= 0)
		error("Bad argument to -r: %s", av[-1]);
	     break;
	  case 'p':
	     f_phase= 1;
	     break;
	  case 'n':
	     if (ac-- <= 0) usage();
	     if (1 != sscanf(*av++, "%d %c", &f_channels, &dmy) || f_channels <= 0)
		error("Bad argument to -n: %s", av[-1]);
	     break;
	  case 's':
	     f_streaming= 1;
	     break;
	  case 'L':
	     fid_list_filters(stdout);
	     return 0;
	  default:
	     usage();
	     break;
	 }
      }
   }
   
   if (f_streaming)
      setvbuf(stdout, NULL, _IONBF, 0);

   // Get sampling rate
   {
      if (ac-- < 1) usage();
      if (1 != sscanf(*av++, "%lf %c", &rate, &dmy))
	 usage();
      if (rate <= 0) error("Bad sampling rate: %g", rate);
   }

   // Get in/out spec
   {
      char *p, *spec= *av++;
      if (ac-- < 1) usage();
      // -n N: expand single format char to N channels (e.g. 'f' -> 'f2')
      char expanded[64];
      if (f_channels > 0 && !strchr(spec, '/') && spec[0] && !spec[1]) {
	 snprintf(expanded, sizeof(expanded), "%c%d", (int)spec[0], f_channels);
	 spec= expanded;
      }
      p= strchr(spec, '/');
      if (p) {
	 p[0]= 0;
	 ispec= decode_spec(spec, 1);
	 ospec= decode_spec(p+1, 0);
      } else {
	 ispec= decode_spec(spec, 1);
	 ospec= decode_spec(spec, 0);
      }
   }

   // Join remaining arguments into one big string and parse
   {
      size_t len= 0;
      int aa;
      char *arg, *p;

      for (aa= 0; aa<ac; aa++)
	 len += 1U + strlen(av[aa]);

      arg= (char*)Alloc(len+1U);
      for (p= arg; ac>0; ac--, av++)
	 p += sprintf(p, "%s ", *av);
      p[-1]= 0;

      parse_filters(arg);
      free(arg);
   }

   // Dump filters, calculate delay
   if (f_dump) {
      for (a= 0; a<n_filt; a++) {
	 FidFilter *ff= filt[a];
	 int val= fid_calc_delay(ff);
	 printf("# Filter %d signal delay: %d samples\n", a+1, val);
	 while (ff->typ) {
	    printf(ff->typ == 'F' ? "  x" : "  /");
	    for (b= 0; b<ff->len; b++)
	       printf(" %.16g", ff->val[b]);
	    ff= FFNEXT(ff);
	    printf(ff->typ ? "\n" : ";\n");
	 }
      }
      return 0;
   }

   // Check everything
   {
      n_chan= spec_count(ospec);
      if (ispec && spec_count(ispec) != n_chan) 
	 error("Input and output formats must have the same number of channels");
      
      if (f_resp) {
	 int cnt= n_filt * (f_phase ? 2 : 1);
	 if (n_chan != cnt && n_chan != cnt+1)
	    error("Expecting %d or %d output channels for response output, not %d",
		  cnt, cnt+1, n_chan);
      } else {
	 if (n_filt != 1 && n_chan != n_filt) 
	    error("Number of filters (%d) must match number of channels (%d) if more than \n"
		  "one filter is specified", n_filt, n_chan);
      }
   }

   // Decode duration
   dur= -1;
   if (f_dur) {
      int val;
      const char *p= f_dur;
      
      dur= 0;
      while (*p) {
	 val= 0;
	 if (!isdigit(*p)) error("Bad duration specification: '%s'", f_dur);
	 while (isdigit(*p)) val= val * 10 + *p++ - '0';
	 switch (*p) {
	  case 'h': dur += (int)(val * 60 * 60 * rate); p++; break;
	  case 'm': dur += (int)(val * 60 * rate); p++; break;
	  case 's': dur += (int)(val * rate); p++; break;
	  default: dur += (int)val; break;
	 }
      }
   }

   // Handle response calculation
   if (f_resp) {
      int f_freq= n_chan == (n_filt * (f_phase ? 2 : 1) + 1);
      for (a= 0; a<=f_resp; a++) {
	 double freq= a * 0.5 / f_resp;
	 char *op= ospec;
	 if (f_freq) op= output(op, rate * freq);
	 for (b= 0; b<n_filt; b++) {
	    double pha;
	    double amp= fid_response_pha(filt[b], freq, &pha);
	    op= output(op, amp);
	    if (f_phase) op= output(op, pha);
	 }
      }
      return 0;
   }

   // Handle filtering operation
   {
      int noin= f_impulse || f_step;
      double val= 1.0;
      double nextval= f_step ? 1.0 : 0.0;

      FidRun **run= ALLOC_ARR(n_chan, FidRun*);
      FidFunc **dostep= ALLOC_ARR(n_chan, FidFunc*);
      void **buf= ALLOC_ARR(n_chan, void*);

      for (a= 0; a<n_chan; a++) {
	 run[a]= fid_run_new(filt[n_filt==1 ? 0 : a], &dostep[a]);
	 buf[a]= fid_run_newbuf(run[a]);
      }

      while (dur != 0) {
	 if (dur > 0) dur--;
	 
	 // Handle impulse and step response generation
	 if (noin) {
	    char *op= ospec;
	    for (a= 0; a<n_chan; a++) 
	       op= output(op, dostep[a](buf[a], val));
	    val= nextval;
	    continue;
	 }

	 // Read from input
	 {
	    char *ip= ispec;
	    char *op= ospec;
	    int chan= 0;

	    if (inend - inp < 128 && !ineof)
	       refill_input();

	    if (inend == inp) return 0;

	    while (*ip) {
	       op= output(op, dostep[chan](buf[chan], input(&ip)));
	       chan++;
	    }
	 }
      }
   }

   return 0;
}

// END //	  

