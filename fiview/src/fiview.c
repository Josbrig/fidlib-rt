/**
 * @file fiview.c
 * @brief fiview — main entry point, global state, and SDL event loop.
 *
 * Defines all global variables declared in fiview.h, handles command-line
 * argument parsing, SDL initialisation, the keyboard/mouse event loop,
 * and the filter-reload / overlay / CSV-export features added in the
 * SDL2 migration (F5, 'o', 'e' keys).
 *
 * @defgroup fiview fiview — interactive filter viewer
 * @brief SDL-based interactive frequency/time-domain filter visualiser.
 *
 * @dot
 * digraph fiview_arch {
 *   rankdir=TB;
 *   node [shape=box, fontname=Helvetica, fontsize=10];
 *   main [label="main()\nSDL init + event loop", style=filled, fillcolor=lightblue];
 *   disp [label="display.c\nLayout + drawing", style=filled, fillcolor=lightyellow];
 *   filt [label="filter.c\nLoad + analysis + run", style=filled, fillcolor=lightyellow];
 *   gfx  [label="graphics.c\nPixel primitives", style=filled, fillcolor=lightyellow];
 *   scr  [label="scratch.c\nText buffer", style=filled, fillcolor=lightyellow];
 *   fidl [label="fidlib\nFilter design engine", style=filled, fillcolor=lightgreen];
 *   main -> disp;
 *   main -> filt;
 *   disp -> gfx;
 *   disp -> scr;
 *   filt -> fidl;
 *   filt -> scr;
 * }
 * @enddot
 *
 * @author  Jim Peters
 * @copyright GPL 2.0
 */

//
//	Fiview -- Filter design/viewing/comparison tool
//
//        Copyright (c) 2002-2003 Jim Peters <http://uazu.net/>.
//        Released under the GNU GPL version 2 as published by the
//        Free Software Foundation.  See the file COPYING for details,
//        or visit <http://www.gnu.org/copyleft/gpl.html>.
//

#include "all.h"

//
//	Globals
//

// Display-related
#ifndef T_SDL2
SDL_Surface *disp;	// Display (SDL1 only)
#endif
Uint32 *disp_pix32;	// Pixel data for 32-bit display, else 0
Uint16 *disp_pix16;	// Pixel data for 16-bit display, else 0
int disp_my;            // Display pitch, in pixels
int disp_sx, disp_sy;   // Display size

int disp_rl, disp_rs;	// Red component transformation
int disp_gl, disp_gs;	// Green component transformation
int disp_bl, disp_bs;	// Blue component transformation
int disp_am;		// Alpha mask

int font_sx, font_sy;	// Default font size
int *colour;		// Array of colours mapped to current display: see colour_data

// Display areas
Rect d_info;		// Top-left info display
Rect d_freq;		// Mini frequency display
Rect d_time;		// Mini time display
Rect d_minf;		// Main area info line
Rect d_main;		// Main display area

// Temporary buffer for display routines, each disp_sx entries long
double *d_wrk1;

// Arguments
double a_f0, a_f1;	// Frequency range given by -f command-line arguments
int a_adj;		// Automatically adjust frequencies to meet 50% levels

// Settings
Filter *curr;		// Currently displayed filter
double s_rate;		// Base sampling rate, or 0 if not set
int s_main;		// Main display type: 'F' freq, 'T' time, 'I' info, 'H' help

int s_tmzoom;		// Time mini-display zoom (samples/pixel if +ve, pixels/sample if -ve)
int s_zoom;		// Special x10 zoom for freq + time displays
int s_minmax;		// Add min/max points to freq display

double s_freq0;		// Frequency range for main frequency display
double s_freq1;		//  (if shown)
int s_ftmod;		// Frequency testing mode (0 off, 'w' wavelet, 's' slide)
double s_ftarg;		// Argument value for frequency testing mode
int s_logsc;		// Logscale mode, or 0 for normal

double s_tim0;		// Time range for main display (if shown)
double s_tim1;		//  tim0 -> tim1  (these are sample-counts, <= 0)

int s_pager_lin;	// Current top-line in pager
char *s_pager_txt;	// Current pager text
char *s_pager_inf;	// Info line for pager
int s_pager_cnt;	// Number of lines in current text
int s_pager_at_end;	// Reached end of pager text
int s_pager_typ;	// s_main type that pager reflects

// Currently buffered time-display
double c_tim0;
double c_tim1;
int c_tim_sx;
Filter *c_tim_filt;
float *c_tim_buf;

// Currently buffered freq-display
double c_freq0;
double c_freq1;
int c_freq_sx;
Filter *c_freq_filt;
double *c_freq_buf;
int c_ftmod;
double c_ftarg;
double *c_ftbuf;

// Currently buffered info-display
Filter *c_info_filt;

// Main loop
int rearrange;		// Set if a rearrange of the screen is necessary
int redraw;		// Set to request a redraw of the screen
int part_cmd= 0;	// Partial command status, or 0
int n_filt= 0;		// Number of filters loaded
int s_overlay= 0;	// Show all loaded filters overlaid in freq view

// Prompt-line and grabkey handling
char *prompt;		// StrDup'd prompt, or 0 if prompt-line not active
void (*prompt_cb)(char *str);	// Callback to use when entry is complete; 0 passed if aborted
char prompt_buf[128];	// Data being entered
int prompt_len;		// Number of characters in buf currently
int grabkey;		// Grabkey active?
void (*grabkey_cb)(int, int); // Callback to use when key has been grabbed

// Hacks
double nan_global;	// Used on MSVC because 0.0/0.0 as a constant is not understood

// Misc
char *helptext;		// Generated help-text


//
//      Utility functions
//

void
error(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   fprintf(stderr, PROGNAME ": ");
   vfprintf(stderr, fmt, ap);
   fprintf(stderr, "\n");
   exit(1);
}

void
errorSDL(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   fprintf(stderr, PROGNAME ": ");
   vfprintf(stderr, fmt, ap);
   fprintf(stderr, "\n  %s\n", SDL_GetError());
   exit(1);
}

#define NL "\n"

void
usage() {
   error("Filter Design, Viewing and Comparison tool, version " VERSION
	 NL "Copyright (c) 2002-2003 Jim Peters, http://uazu.net/, all rights "
	 NL "  reserved, released under the GNU GPL v2.  See file COPYING."
	 NL "LibSDL code from the SDL project (http://libsdl.org/) is released under"
	 NL "  the GNU LGPL version 2 or later.  See file COPYING_LIB."
	 NL "Filter design code derived from the mkfilter package by Tony Fisher"
	 NL "  See here: <http://www-users.cs.york.ac.uk/~fisher/mkfilter/>"
	 NL
	 NL "Usage: fiview [options] [<sample-rate>] [<filenames>] [-i <spec-words> ...]"
	 NL
	 NL "Options:"
	 NL "  -D            Generate a simple set of demonstration filters"
	 NL "  -F <mode>     Run full-screen with the given mode, <wid>x<hgt>x<bpp>"
	 NL "                <bpp> may be 16 or 32.  For example: 800x600x16"
	 NL "  -W <size>     Run as a window with the given size: <wid>x<hgt>"
	 NL "  -f <freq>[-<freq>]    Specify the default frequency or frequency range for"
	 NL "                        any filter specifications used."
	 NL "  -h            Dump help-text to STDOUT and exit"
	 NL "  -Q            Just analyse filters and then quit"
	 NL ""
	 NL "Filter specifications are read from all the filenames given, and also from the"
	 NL "remainder of the command line if the -i (immediate) option is given."
	 NL "An analysis log (with example code) is written to 'fiview.log' in the current "
	 NL "directory, and IIR/FIR filter coefficients are written to 'fiview.coef'"
	 NL ""
	 NL ">>> Type \"fiview -D\" to load a demonstration set of band-pass filters."
	 NL ""
	 );
}

void warn(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   fprintf(stderr, "\n");
}

void *Alloc(size_t size) {
   void *vp= calloc(1, size);
   if (!vp) error("Out of memory");
   return vp;
}

char *StrDup(const char *str) {
   char *cp= strdup(str);
   if (!cp) error("Out of memory");
   return cp;
}


//
//	Main routine
//

int 
main(int ac, char **av) {
   int sx= 800, sy= 600, bpp= 0;	// Default is 800x600 resizable window
   SDL_Event ev;
   int motion_xx, motion_yy;
   int immed= 0;
   int quit= 0;
   static const char *demo_av[]= {
      "44100", "-f", "1000-1500", "-i", "BpBu1,BpBu2,BpBu3,BpBu5,BpBu7,BpBu10"
   };

   // Generate helptext
   helptext= gen_helptext();

   // Process arguments
   s_rate= 0;
   a_f0= a_f1= -1.0;		// Undefined
   ac--; av++;

   while (ac > 0 && !immed) {
      double val;
      char dmy;

      // Handle a sampling rate, if provided
      if (av[0][0] != '-' &&
	  s_rate == 0 &&
	  1 == sscanf(av[0], "%lf %c", &val, &dmy)) {
	 s_rate= val;
	 ac--; av++;
	 continue;
      }

      // Handle any options
      if (av[0][0] == '-' && av[0][1]) {
	 char ch, *p= *av++ + 1, *q; 
	 ac--;
	 while ((ch= *p++)) switch (ch) {
	  case 'D':
	     if (ac) error("-D must be the final argument");
	     av= (char**)demo_av;
	     ac= sizeof(demo_av) / sizeof(demo_av[0]);
	     break;
	  case 'F':
	     if (ac-- < 1) usage();
	     if (3 != sscanf(*av++, "%dx%dx%d %c", &sx, &sy, &bpp, &dmy) ||
		 (bpp != 16 && bpp != 32))
		error("Bad mode-spec: %s", av[-1]);
	     break;
	  case 'W':
	     if (ac-- < 1) usage();
	     if (2 != sscanf(*av++, "%dx%d %c", &sx, &sy, &dmy))
		error("Bad window size: %s", av[-1]);
	     break;
	  case 'a':
	     error("-a option now supported through '=' prefix to -f option frequencies");
	     break;
	  case 'f':
	     if (ac-- < 1) usage();
	     q= av[0]; if ((a_adj= (*q == '='))) q++;
	     if (1 == sscanf(q, "%lf %c", &a_f0, &dmy)) {
		a_f1= a_f0;
	     } else if (2 == sscanf(q, "%lf-%lf %c", &a_f0, &a_f1, &dmy)) {
		;
	     } else 
		error("Bad frequency or frequency range: %s", av[0]);
	     av++;
	     break;
	  case 'h':
	     printf("%s", helptext);
	     exit(0);
	  case 'i':
	     immed= 1;
	     break;
	  case 'Q':
	     quit= 1;
	     break;
	  default:	
	     error("Unknown option '%c'", ch);
	 }
	 continue;
      }
      
      // Something else, must be getting into filenames
      break;
   }

   // Watch for no useful arguments at all
   if (ac == 0) usage();
      
   // Load up all the filter files
   if (!immed) {
      while (ac && 0 != strcmp("-i", av[0])) {
	 n_filt= filter_load_file(*av++);
	 ac--;
      }
      if (ac) { immed= 1; ac--; av++; }
   }

   // Load up any immediate filter specified on the command line
   if (immed) {
      char *txt;
      scr_zap();
      while (ac > 0) { scr_pr("%s ", *av++); ac--; }
      txt= scr_dup();
      filter_load_immed(txt);
      free(txt);
   }

   // Dump all the loaded filters
   {
      FILE *log_fp;
      Filter *ff;
      int a;

      if (!(log_fp= fopen("fiview.log", "w")))
	 error("Can't create file: fiview.log");

      for (a= 1; (ff= filter_find(a)); a++) {
	 fputs("// Generated by Fiview " VERSION 
	       " <http://uazu.net/fiview/>.  \n", log_fp);
	 fputs("// All generated example code below is in the public domain.\n", log_fp);
	 fputs(ff->dump, log_fp);
      }
      
      fputs(filter_standard_code(), log_fp);
      if (fclose(log_fp))
	 error("Error writing file: fiview.log");
   }

   // Dump the coefficients of the loaded filters
   {
      FILE *log_fp;
      Filter *ff;
      int a;

      if (!(log_fp= fopen("fiview.coef", "w")))
	 error("Can't create file: fiview.coef");

      for (a= 1; (ff= filter_find(a)); a++)
	 dump_filter_coef(ff->ff, log_fp);
      
      if (fclose(log_fp))
	 error("Error writing file: fiview.coef");
   }

   // Quit now before starting GUI if requested
   if (quit) exit(0);

   // Initialize SDL
   if (0 > SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE))
      errorSDL("Couldn't initialize SDL");
   atexit(SDL_Quit);
   //SDL_EnableUNICODE(1);		// So we get translated keypresses
#ifndef T_SDL2
   SDL_EnableKeyRepeat(200, 40);
#endif

   // Initialise graphics
   graphics_init(sx, sy, bpp);
   font_sy= 16;
   font_sx= 8;
   curr= filter_find(1);
   s_main= 'H';
   rearrange= 1;

   // Main loop
   status("+");
   while (1) {
      if (rearrange) {
	 arrange_display();
	 set_s_tmzoom();
	 auto_adjust_time();
	 auto_adjust_freq();
	 rearrange= 0;
	 redraw= 1;
      }
      if (redraw) {
	 // Suspend update during redraw so that we get a clean change
	 suspend_update= 1;
	 draw_mini_info();
	 draw_mini_freq();
	 draw_mini_time();
	 switch (s_main) {
	  case 'T': draw_time(); break;
	  case 'F': draw_freq(); break;
	  case 'I': draw_info(); break;
	  case 'H': draw_help(); break;
	 }
	 draw_status();
	 suspend_update= 0;
	 update_all();
	 redraw= 0;
      }

      // Wait for an event
      if (!SDL_WaitEvent(0)) 
	 errorSDL("Unexpected error waiting for events");

      // Process all outstanding events
      motion_yy= motion_xx= -1;
      while (SDL_PollEvent(&ev)) {
	 int key, mod;
	 switch (ev.type) {
	  case SDL_KEYUP:
	     if (s_zoom || s_minmax) {
		s_zoom= s_minmax= 0;
		redraw= 1;
		continue;
	     }
	     break;
	  case SDL_KEYDOWN:
	     key= ev.key.keysym.sym;
	     mod= ev.key.keysym.mod;
	     //With translation only: key= ev.key.keysym.unicode;

	     if (grabkey) {
		grabkey= 0;
		status("");
		grabkey_cb(key, mod);
		continue;
	     }

	     if (prompt) {
		if (key == SDLK_ESCAPE || 
		    (key == 'G' && (mod&KMOD_CTRL))) {
		   status("");
		   free(prompt); prompt= 0;
		   prompt_cb(0);
		   continue;
		}
		if (key == SDLK_RETURN) {
		   status("");
		   free(prompt); prompt= 0;
		   prompt_buf[prompt_len]= 0;
		   prompt_cb(prompt_buf);
		   continue;
		}		   
		if (key == SDLK_BACKSPACE &&
		    prompt_len > 0) {
		   prompt_buf[--prompt_len]= 0;
		   display_prompt();
		   continue;
		}
		if (key >= ' ' && key <= '~' &&
		    (size_t)prompt_len < sizeof(prompt_buf)-1U) {
		   prompt_buf[prompt_len++]= (char)key;
		   display_prompt();
		   continue;
		}
		// Can't handle it; ignore
		warn("Unknown/ignored prompt key: %d %d", key, mod);
		continue;
	     }

	     if (!s_zoom && !s_minmax)
		status("");	// Use keypress to clear temporary messages from status line

	     // Switching between filters
	     if (key >= '0' && key <= '9') {
		int dig= key - '0';
		Filter *ff= filter_find(dig ? dig : 10);
		if (ff) { curr= ff; redraw= 1; }
		continue;
	     }

	     // General keys
	     switch (key) {
	      case SDLK_F1:
		 //case 'h':
		 s_main= 'H';
		 redraw= 1;
		 continue;
	      case SDLK_F2:
		 //case 'i':
		 s_main= 'I';
		 redraw= 1;
		 continue;
	      case SDLK_F3:
		 //case 'f':
		 s_main= 'F';
		 redraw= 1;
		 continue;
	      case SDLK_F4:
		 //case 't':
		 s_main= 'T';
		 redraw= 1;
		 continue;
	      case SDLK_F5:
		 run_prompt("Load filter spec:", load_filter_prompt_cb);
		 continue;
	      case 'o':
		 s_overlay= !s_overlay;
		 if (s_main == 'F') draw_freq();
		 continue;
	      case SDLK_ESCAPE:
	      case 'q':
		 exit(0);
	     }

	     switch (s_main) {
	      case 'F':		// Frequency display handling
		 switch (key) {
		    double adj, wid, mid;
		  case 'a': 
		     auto_adjust_freq(); 
		     draw_freq(); draw_mini_freq();
		     continue;
		  case SDLK_LEFT:
		     adj= 0.25 * (s_freq1-s_freq0);
		     s_freq0 -= adj; s_freq1 -= adj;
		     draw_freq(); draw_mini_freq();
		     continue;
		  case SDLK_RIGHT:
		     adj= 0.25 * (s_freq1-s_freq0);
		     s_freq0 += adj; s_freq1 += adj;
		     draw_freq(); draw_mini_freq();
		     continue;
		  case SDLK_UP:
		     wid= s_freq1-s_freq0;
		     mid= (s_freq0 + s_freq1) * 0.5;
		     s_freq0= mid-wid; s_freq1= mid+wid;
		     draw_freq(); draw_mini_freq();
		     continue;
		  case SDLK_DOWN:
		     wid= (s_freq1-s_freq0) / 4.0;
		     mid= (s_freq0 + s_freq1) * 0.5;
		     s_freq0= mid-wid; s_freq1= mid+wid;
		     draw_freq(); draw_mini_freq();
		     continue;
		  case SDLK_BACKSPACE:
		  case SDLK_PAGEUP:
		     adj= (s_freq1-s_freq0);
		     s_freq0 -= adj; s_freq1 -= adj;
		     draw_freq(); draw_mini_freq();
		     continue;
		  case SDLK_SPACE:
		  case SDLK_PAGEDOWN:
		     adj= (s_freq1-s_freq0);
		     s_freq0 += adj; s_freq1 += adj;
		     draw_freq(); draw_mini_freq();
		     continue;
		  case 'z':
		     if (s_zoom != 1) {
			s_zoom= 1;
			draw_freq();
		     }
		     continue;
		  case 'x':
		     if (s_zoom != -1) {
			s_zoom= -1;
			draw_freq();
		     }
		     continue;
		  case 'm':
		     if (!s_minmax) {
			s_minmax= 1;
			draw_freq();
		     }
		     continue;
		  case 't':	// Set test-mode
		     run_prompt("Enter test-mode:", set_test_mode);
		     continue;
		  case 'e':	// Export frequency response to CSV
		     export_freq_csv();
		     continue;
		  case 'l':	// Set log-scale view
		     run_grabkey("Press 1-9 for a log-scale view, or any other key for normal",
				 set_logscale);
		     continue;
		 }
		 break;
	      case 'T':		// Time display handling
		 switch (key) {
		    double adj, mid, wid;
		  case 'a': 
		     auto_adjust_time(); 
		     draw_time(); draw_mini_time(); 
		     continue;
		  case SDLK_LEFT:
		     adj= (s_tim1 - s_tim0) * 0.25;
		     s_tim0 -= adj; s_tim1 -= adj;
		     draw_time(); draw_mini_time(); 
		     continue;
		  case SDLK_RIGHT:
		     adj= (s_tim1 - s_tim0) * 0.25;
		     s_tim0 += adj; s_tim1 += adj;
		     draw_time(); draw_mini_time(); 
		     continue;
		  case SDLK_UP:
		     wid= s_tim1-s_tim0;
		     mid= (s_tim1 + s_tim0) * 0.5;
		     s_tim0= mid-wid; s_tim1= mid+wid;
		     draw_time(); draw_mini_time(); 
		     continue;
		  case SDLK_DOWN:
		     wid= (s_tim1-s_tim0) * 0.25;
		     mid= (s_tim1 + s_tim0) * 0.5;
		     s_tim0= mid-wid; s_tim1= mid+wid;
		     draw_time(); draw_mini_time(); 
		     continue;
		  case SDLK_BACKSPACE:
		  case SDLK_PAGEUP:
		     adj= (s_tim1 - s_tim0);
		     s_tim0 -= adj; s_tim1 -= adj;
		     draw_time(); draw_mini_time(); 
		     continue;
		  case SDLK_SPACE:
		  case SDLK_PAGEDOWN:
		     adj= (s_tim1 - s_tim0);
		     s_tim0 += adj; s_tim1 += adj;
		     draw_time(); draw_mini_time(); 
		     continue;
		  case 'z':
		     if (!s_zoom) {
			s_zoom= 1;
			draw_time();
		     }
		     continue;
		 }
		 break;
	      case 'I':		// Pager display handling
	      case 'H':
		 switch (key) {
		  case SDLK_UP:
		     if (s_pager_lin) {
			s_pager_lin--;
			draw_pager();
		     }
		     continue;
		  case SDLK_RETURN:
		  case SDLK_DOWN:
		     if (!s_pager_at_end) {
			s_pager_lin++;
			draw_pager();
		     }
		     continue;
		  case SDLK_BACKSPACE:
		  case SDLK_PAGEUP:
		     if (s_pager_lin) {
			s_pager_lin -= d_main.sy/font_sy - 2;
			if (s_pager_lin < 0) s_pager_lin= 0;
			draw_pager();
		     }
		     continue;
		  case SDLK_SPACE:
		  case SDLK_PAGEDOWN:
		     if (!s_pager_at_end) {
			s_pager_lin += (d_main.sy/font_sy - 2);
			draw_pager();
		     }
		     continue;
		  case 'd':
		     if (!s_pager_at_end) {
			s_pager_lin += (d_main.sy/font_sy - 2) / 2;
			draw_pager();
		     }
		     continue;
		 }
		 break;
	     }
	     break;
	  case SDL_MOUSEMOTION:
	     // Save just the last motion until the end of the loop to
	     // avoid incredible slowdown, because some of the
	     // calculations might be too heavy for a stream of motion
	     // events.
	     motion_xx= ev.motion.x;
	     motion_yy= ev.motion.y;
	     continue;
	  case SDL_MOUSEBUTTONDOWN:
	     if (inRect(&d_info, ev.button.x, ev.button.y) && s_main != 'I') {
		s_main= 'I'; redraw= 1; continue;
	     }
	     if (inRect(&d_freq, ev.button.x, ev.button.y) && s_main != 'F') {
		s_main= 'F'; redraw= 1; continue;
	     }
	     if (inRect(&d_time, ev.button.x, ev.button.y) && s_main != 'T') {
		s_main= 'T'; redraw= 1; continue;
	     }
	     continue;
#ifdef T_SDL2
	  case SDL_WINDOWEVENT:
	     if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
		sx= ev.window.data1;
		sy= ev.window.data2;
		graphics_init(sx, sy, 0);
		rearrange= 1;
	     }
	     continue;
#else
	  case SDL_VIDEORESIZE:
	     sx= ev.resize.w;
	     sy= ev.resize.h;
	     graphics_init(sx, sy, 0);
	     rearrange= 1;
	     continue;
#endif
	  case SDL_QUIT:
	     exit(0);
	 }
      }

      // Handle motion events now
      if (motion_yy >= d_main.yy &&
	  motion_yy - d_main.yy < d_main.sy) {
	 switch (s_main) {
	  case 'F':
	     show_freq_status(motion_xx - d_main.xx, 
			      motion_yy - d_main.yy);    
	     break;
	  case 'T':
	     show_time_status(motion_xx - d_main.xx, 
			      motion_yy - d_main.yy);    
	     break;
	 }
      }

      // Loop ...
   }

   return 0;
}

//
//	Check to see if a point is within the given rectangle
//

int 
inRect(Rect *rr, int xx, int yy) {
   return (xx >= rr->xx &&
	   xx < rr->xx + rr->sx &&
	   yy >= rr->yy &&
	   yy < rr->yy + rr->sy);
}

//
//	Automatically adjust filter viewing settings to show most of
//	what is interesting.
//

void 
auto_adjust_time() {
   // Adjust time display to show up to 95% point
   s_tim0= -d_main.sx;
   s_tim1= 0;

   if (curr->cnt95 > 0) {
      while (-curr->cnt95 < s_tim0) 
	 s_tim0 *= 2;
      while (-curr->cnt95 > s_tim0*0.5) 
	 s_tim0 *= 0.5;
   }
}

void 
auto_adjust_freq() {
   // Adjust frequency display to show all the interesting 50% points
   s_freq0= 0;
   s_freq1= 0.5;
   
   if (curr->n_m3db) {
      int i0= 0;
      int i1= curr->n_m3db-1;
      double mid;
      double wid;
      double f0, f1;

      if (i1-i0 >= 3 && curr->m3db[i0] == 0 && curr->m3db[i1] == 0.5) {
	 // Don't include start and end if both are present, because
	 // this could be a band-cut filter, in which case we need to
	 // focus on the cut.
	 i0++; i1--;
      }

      f0= curr->m3db[i0];
      f1= curr->m3db[i1];

      if (i0+1 == i1 && (f0 != 0.0 || f1 != 0.5)) {
	 // Adjust to most interesting end of range
	 if (f0 == 0.0 && f1 > 0.25) {
	    f0= f1; f1= 0.5;
	 } else if (f1 == 0.5 && f0 < 0.25) {
	    f1= f0; f0= 0.0;
	 }
      }
      
      mid= 0.5 * (f0 + f1);
      wid= (f1-f0) * 1.7;		// Allow 70% extra space
      f0= mid-wid; 
      f1= mid+wid;
      if (f0 < 0.0) f0= 0.0;
      if (f1 > 0.5) f1= 0.5;

      // Start at 0.0-0.5 view
      s_freq0= 0.0;
      s_freq1= 0.5;

      // Zoom in stages, emulating user actions
      while (1) {
	 double tf0, tf1, inc;	// Trial frequency range
	 tf0= s_freq0;
	 tf1= (s_freq0+s_freq1) * 0.5;
	 inc= (tf1-tf0) * 0.25;
	 if (inc == 0) break;
	 while (tf0 + inc < f0 && tf1 < s_freq1) { tf0 += inc; tf1 += inc; }
	 if (tf1 < f1) break;		// Doesn't fit
	 s_freq0= tf0;
	 s_freq1= tf1;
      }
   }
}

//
//	Find the zoom level required for the mini time-display in
//	order to show at least 99% of the impulse response of all the
//	loaded filters.
//

void 
set_s_tmzoom() {
   Filter *ff;
   s_tmzoom= -d_time.sx;

   for (ff= filters; ff; ff= ff->nxt) {
      int tmz= 0;
      if (d_time.sx >= 2 * (ff->cnt99+1))
	 tmz= -(d_time.sx / (ff->cnt99+1));
      else 
	 tmz= (ff->cnt99 - 1) / d_time.sx + 1;
      if (s_tmzoom < tmz) s_tmzoom= tmz;
   }

   // For infinite filters, 99% point is 0, so display one per pixel
   if (s_tmzoom == -d_time.sx) s_tmzoom= 1;
}

//
//	Display the prompt line
//   

void 
display_prompt() {
   prompt_buf[prompt_len]= 0;	// Sanity check
   status("\x98%s\x80%s\x9a \x80\n", prompt, prompt_buf);
}

//
//	Start prompt
//

void
run_prompt(const char *txt, void (*rout)(char*)) {
   prompt= StrDup(txt);
   prompt_cb= rout;
   prompt_len= 0;
   prompt_buf[0]= 0;
   display_prompt();
}        

//
//	Start grabkey
//

void
run_grabkey(const char *txt, void (*rout)(int,int)) {
   status("\x98%s\x80\n", txt);
   grabkey= 1;
   grabkey_cb= rout;
}        

//
//	Set the test mode for the frequency display
//

void
set_test_mode(char *str) {
   double val;
   char dmy;

   if (!str) return;	// Abort -> no change
   while (isspace(*str)) str++;
   if (!str[0]) {
      s_ftmod= 0;
   } else if (1 == sscanf(str, " w%lf %c", &val, &dmy)) {
      s_ftmod= 'w';
      s_ftarg= val;
   } else if (1 == sscanf(str, " s%lf %c", &val, &dmy)) {
      s_ftmod= 's';
      // Need to change from Hz/sec (s^-2) to /s_rate/s_rate
      s_ftarg= val / (s_rate ? s_rate*s_rate : 1.0);
   } else {
      status("\x9b Unknown testing mode: %s \x80", str);
      return;
   }

   draw_freq();
}

//
//	Set logscale view
//

void 
set_logscale(int key, int mod) {
   (void)mod;
   if (key >= '1' && key <= '9')
      s_logsc= key - '0';
   else 
      s_logsc= 0;
   draw_freq();
}

//
//	Runtime filter load — callback for run_prompt (F5 key)
//

void
load_filter_prompt_cb(char *spec) {
   int prev;
   Filter *ff;
   if (!spec || !spec[0]) return;
   prev= n_filt;
   n_filt= filter_load_immed(spec);
   if (n_filt <= prev) {
      warn("Could not load filter spec");
      return;
   }
   // Find newly added filter (highest index)
   for (ff= filters; ff && ff->nxt; ff= ff->nxt) ;
   if (ff) { curr= ff; rearrange= 1; }
}

//
//	Export frequency response of current filter to CSV
//

void
export_freq_csv(void) {
   FILE *fp;
   int a;

   if (!c_freq_buf || c_freq_sx <= 0) {
      warn("No frequency data to export");
      return;
   }
   fp= fopen("fiview_freq.csv", "w");
   if (!fp) { warn("Cannot open fiview_freq.csv"); return; }
   fprintf(fp, "freq_hz,resp_min,resp_max\n");
   for (a= 0; a<c_freq_sx; a++) {
      double *dp= c_freq_buf + a * 4;
      double freq= (c_freq0 + (c_freq1-c_freq0) * a / (double)(c_freq_sx-1)) * s_rate;
      fprintf(fp, "%.6g,%.10g,%.10g\n", freq, dp[0], dp[1]);
   }
   fclose(fp);
   warn("Exported %d points to fiview_freq.csv", c_freq_sx);
}

// END //
