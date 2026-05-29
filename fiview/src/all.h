//
//	Common include file — included by every translation unit in fiview.
//
//        Copyright (c) 2002-2003 Jim Peters <http://uazu.net/>.
//        Released under the GNU GPL version 2 as published by the
//        Free Software Foundation.  See the file COPYING for details,
//        or visit <http://www.gnu.org/copyleft/gpl.html>.
//
//	One of the target macros must be defined on the compiler command line:
//	  -DT_LINUX   Linux / POSIX
//	  -DT_MINGW   MinGW / Windows (GCC)
//	  -DT_MSVC    MSVC  / Windows
//
//	SDL version:
//	  -DT_SDL2    use SDL 2.x  (default: SDL 1.2)
//
//	C++ migration note:
//	  C++20 compilation is available via -DFIVIEW_CXX20_COMPAT.
//	  All public headers use (void) for empty parameter lists and avoid
//	  C++ reserved words.  When splitting modules into a library, wrap
//	  the C declarations in extern "C" { }.
//

#ifndef ALL_H
#define ALL_H

#define VERSION  "0.9.10"
#define PROGNAME "fiview"

// ── Standard library ─────────────────────────────────────────────────────────

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>

#ifndef T_MSVC
#include <unistd.h>
#include <sys/time.h>
#endif

// ── Platform timing ──────────────────────────────────────────────────────────

#ifdef T_LINUX
#  define UNIX_TIME
#endif
#ifdef T_APPLE
#  define UNIX_TIME
#endif
#ifdef T_MINGW
#  define WIN_TIME
#endif
#ifdef T_MSVC
#  define WIN_TIME
#endif

#ifdef UNIX_TIME
#  include <sys/ioctl.h>
#  include <sys/times.h>
#endif
#ifdef WIN_TIME
#  include <windows.h>
#  include <mmsystem.h>
#endif

// ── SDL ──────────────────────────────────────────────────────────────────────

#ifdef T_SDL2
#  include <SDL2/SDL.h>
#else
#  include <SDL/SDL.h>
#endif

// ── Platform portability shims ───────────────────────────────────────────────

#ifdef T_MSVC
#  include <float.h>
#  define NAN         nan_global
#  define isnan(v)    _isnan(v)
#  define vsnprintf   _vsnprintf
#  define snprintf    _snprintf
#endif
#ifdef T_MINGW
#  define vsnprintf   _vsnprintf
#  define snprintf    _snprintf
#endif

#ifndef NAN
#  define NAN (0.0/0.0)
#endif
#ifndef M_PI
#  define M_PI    3.14159265358979323846
#endif
#ifndef M_LN10
#  define M_LN10  2.30258509299404568402
#endif

// ── Project headers (order matters: fidlib → filter → display → fiview) ─────

#include <fidlib/fidlib.h>
#include "filter.h"
#include "display.h"
#include "scratch.h"
#include "graphics.h"
#include "helptext.h"
#include "fiview.h"

// ── Constants ────────────────────────────────────────────────────────────────

#define M301DB  (0.707106781186548)   // -3.01 dB  = sqrt(0.5)
#define M602DB  (0.50)                // -6.02 dB  = 0.5

// ── Debug helper ─────────────────────────────────────────────────────────────

#ifndef DEBUG_ON
#  define DEBUG_ON 0
#endif
#define DEBUG  if (DEBUG_ON) warn

#endif
