/**
 * @file graphics.c
 * @brief SDL pixel-buffer drawing primitives — init, update, primitives, colour plots.
 *
 * Supports SDL1 and SDL2 (selected at compile time via @c T_SDL2).  Only 16 bpp
 * and 32 bpp surfaces are handled natively; other depths fall back to SDL's
 * software emulation.
 *
 * @author  Jim Peters
 * @copyright GPL 2.0
 * @ingroup fiview_graphics
 */

//
//	Graphics handling using libSDL
//
//        Copyright (c) 2002-2003 Jim Peters <http://uazu.net/>.
//        Released under the GNU GPL version 2 as published by the
//        Free Software Foundation.  See the file COPYING for details,
//        or visit <http://www.gnu.org/copyleft/gpl.html>.
//
//	We handle only 16bpp and 32bpp displays directly, and let SDL
//	emulate the other modes.  Most of this is "fast enough" but
//	not really optimised, except for the most critical bits.
//

#include "all.h"

//
//      Predefined colours in 0xRRGGBB form.  Put any fixed colours
//      required in here, and they will automatically be converted to
//      pixel values in colour[] when SDL starts up.
//
int colour_data[]= {
   0x000000,    // 0 - black (0x80: white on black)
   0xFFFFFF,    // 1 - white
   0x773322,    // 2 - blue (0x82: grey on blue)
   0xCCCCCC,    // 3 - grey
   0x773322,    // 4 - blue (0x84: white on blue)
   0xFFFFFF,    // 5 - white
   0x000000,    // 6 - freq/time background
   0xBBBB00,    // 7 - freq/time colour between 0 and levels
   0xFFFFFF,    // 8 - freq/time colour for active range
   0x555555,    // 9 - freq/time inverse colour
   0xFFFF55,    // 10 - freq/time inverse colour
   0xFFFFFF,    // 11 - freq/time inverse colour
   0x773322,    // 12 - freq/time text colours (0x8c)
   0x668800,    // 13 -  ::
   0x773322,    // 14 - blue (0x8e: white on blue)
   0xFFFFFF,    // 15 - white
   0x0000FF,	// 16 - freq/time phase on background
   0xCCCCFF,	// 17 - freq/time phase on between colour
   0xFFFFFF,	// 18 - freq/time phase on active colour
   0x000000,	// 19 - 'I' pager text (\x93)
   0xEEEECC,	// 20 - 
   0x332200,	// 21 - 'H' pager text (\x95)
   0xEEEECC,	// 22 - 
   0x808080,	// 23 - Grey for label backgrounds
   0x0000CC,	// 24 - Prompt text, bright white on blue (\x98)
   0xFFFFFF,	// 25 - 
   0x00FF00,	// 26 - Prompt cursor (green) (\x9A)
   0xCC0000,	// 27 - Error text (white/red) (\x9b)
   0xFFFFFF,	// 28 - 
   0xCC0000,	// 29 - Testing data for frequency display
   0x222222,	// 30 - Background for log-scale stripes
   0x2222FF,	// 31 - freq/time phase on log-scale stripes
};

//
//	Globals
//

int suspend_update= 0;		// If non-zero, update() calls will have no effect

#ifdef T_SDL2
static SDL_Window   *sdl_window   = NULL;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture  *sdl_texture  = NULL;
#endif

//
//	Initialise SDL for graphics.  The initial window/screen size
//	is passed as arguments, and all the disp_* and colour[]
//	globals are set up.  The 'bpp' argument may be 0 for a normal
//	window using whatever bpp is current, or 16 or 32 to run the
//	app in full-screen mode.
//

void
graphics_init(int sx, int sy, int bpp) {
   int len, a;

#ifdef T_SDL2
   int full= (bpp != 0);

   if (sdl_texture)  { SDL_DestroyTexture(sdl_texture); sdl_texture= NULL; }

   if (!sdl_window) {
      Uint32 wflags= (Uint32)(SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
      if (full) wflags |= (Uint32)SDL_WINDOW_FULLSCREEN_DESKTOP;
      sdl_window= SDL_CreateWindow(PROGNAME,
	 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, sx, sy, wflags);
      if (!sdl_window) errorSDL("Couldn't create window");
      sdl_renderer= SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_SOFTWARE);
      if (!sdl_renderer) errorSDL("Couldn't create renderer");
   }

   SDL_GetWindowSize(sdl_window, &disp_sx, &disp_sy);

   sdl_texture= SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, disp_sx, disp_sy);
   if (!sdl_texture) errorSDL("Couldn't create texture");

   free(disp_pix32);
   disp_pix32= (Uint32*)calloc((size_t)disp_sx * (size_t)disp_sy, sizeof(Uint32));
   if (!disp_pix32) error("Out of memory");
   disp_pix16= NULL;
   disp_my=    disp_sx;

   // ARGB8888: A[31:24] R[23:16] G[15:8] B[7:0]
   disp_rl= 0; disp_rs= 16;
   disp_gl= 0; disp_gs= 8;
   disp_bl= 0; disp_bs= 0;
   disp_am= (int)0xFF000000u;

#else
   // SDL1 path
   {
      SDL_VideoInfo const *vid;
      int full= bpp != 0;

      if (!full) {
	 vid= SDL_GetVideoInfo();
	 bpp= vid->vfmt->BitsPerPixel;
	 if (bpp <= 16) bpp= 16; else bpp= 32;
      }

      disp_sx= sx;
      disp_sy= sy;
      if (!(disp= SDL_SetVideoMode(disp_sx, disp_sy, bpp, SDL_SWSURFACE |
				 (full ? SDL_FULLSCREEN : SDL_RESIZABLE))))
	 errorSDL("Couldn't set video mode");

      if (SDL_MUSTLOCK(disp))
	 error("OOPS: According to the docs this shouldn't happen -- "
	       "I'm supposed to lock this SDL_SWSURFACE display");

      if (disp->format->BytesPerPixel == 2) {
	 disp_pix16= (Uint16*)disp->pixels;
	 disp_pix32= 0;
	 if (disp->pitch & 1)
	    error("Display pitch isn't a multiple of 2: %d", disp->pitch);
	 disp_my= disp->pitch >> 1;
      } else if (disp->format->BytesPerPixel == 4) {
	 disp_pix16= 0;
	 disp_pix32= (Uint32*)disp->pixels;
	 if (disp->pitch & 3)
	    error("Display pitch isn't a multiple of 4: %d", disp->pitch);
	 disp_my= disp->pitch >> 2;
      } else
	 error("Unexpected error -- SDL didn't give me the number of bpp I asked for");

      disp_rl= disp->format->Rloss;
      disp_gl= disp->format->Gloss;
      disp_bl= disp->format->Bloss;
      disp_rs= disp->format->Rshift;
      disp_gs= disp->format->Gshift;
      disp_bs= disp->format->Bshift;
      disp_am= disp->format->Amask;

      // Turn 16-bit 5-6-5 into 5-5-1-5 to avoid alternate red-green in grey
      while (disp_gl < disp_rl) disp_gl++, disp_gs++;
   }
#endif

   // Setup the static colours I'll be needing
   len= sizeof(colour_data) / sizeof(int);
   if (!colour) colour= ALLOC_ARR(len, int);
   for (a= 0; a<len; a++) {
      int cc= colour_data[a];
      colour[a]= (cc == -1) ? -1 : map_rgb(cc);
   }

   // Initialise pure hues and colour-intensity table
   init_pure_hues();
   init_cint_table();

   // Clear the screen
   clear_rect(0, 0, disp_sx, disp_sy, colour[0]);
   update(0, 0, disp_sx, disp_sy);
}

//
//	Map an 0xRRGGBB colour into whatever is required for the
//	current video mode.
//

int 
map_rgb(int col) {
   int r= 255 & (col >> 16);
   int g= 255 & (col >> 8);
   int b= 255 & col;

   return disp_am | 
      ((r >> disp_rl) << disp_rs) |
      ((g >> disp_gl) << disp_gs) |
      ((b >> disp_bl) << disp_bs);
}

//
//	Update a rectangle directly to the screen
//

void
update(int xx, int yy, int sx, int sy) {
   if (!suspend_update) {
#ifdef T_SDL2
      SDL_Rect r; r.x= xx; r.y= yy; r.w= sx; r.h= sy;
      SDL_UpdateTexture(sdl_texture, &r,
	 (const Uint8*)(disp_pix32 + yy * disp_sx + xx),
	 disp_sx * (int)sizeof(Uint32));
      SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
      SDL_RenderPresent(sdl_renderer);
#else
      SDL_UpdateRect(disp, xx, yy, sx, sy);
#endif
   }
}

//
//	Force an update through even if updates are suspended -- only
//	used for status line.
//

void
update_force(int xx, int yy, int sx, int sy) {
#ifdef T_SDL2
   SDL_Rect r; r.x= xx; r.y= yy; r.w= sx; r.h= sy;
   SDL_UpdateTexture(sdl_texture, &r,
      (const Uint8*)(disp_pix32 + yy * disp_sx + xx),
      disp_sx * (int)sizeof(Uint32));
   SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
   SDL_RenderPresent(sdl_renderer);
#else
   SDL_UpdateRect(disp, xx, yy, sx, sy);
#endif
}

//
//	Show/hide mouse cursor
//

void 
mouse_pointer(int on) {
   SDL_ShowCursor(on ? SDL_ENABLE : SDL_DISABLE);
}

//
//	Clear a rectangle to the given colour
//

void 
clear_rect(int xx, int yy, int sx, int sy, int val) {
   int a;

   if (xx < 0) { sx += xx; xx= 0; }
   if (yy < 0) { sy += yy; yy= 0; }
   if (xx + sx > disp_sx) sx= disp_sx - xx;
   if (yy + sy > disp_sy) sy= disp_sy - yy;
   if (sx <= 0 || sy <= 0) return;

   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      while (sy-- > 0) {
	 for (a= sx; a>0; a--) *dp++= val;
	 dp += disp_my - sx;
      }
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      while (sy-- > 0) {
	 for (a= sx; a>0; a--) *dp++= val;
	 dp += disp_my - sx;
      }
   }
}      

//
//	Apply a colour transparently to the given rectangle.  An
//	opacity of 100 is opaque, 0 means fully transparent (which
//	would be a pointless operation in this case).
//

void 
alpha_rect(int xx, int yy, int sx, int sy, int val, int opac) {
   int a;
   int r0, r1, g0, g1, b0, b1;

   if (xx < 0) { sx += xx; xx= 0; }
   if (yy < 0) { sy += yy; yy= 0; }
   if (xx + sx > disp_sx) sx= disp_sx - xx;
   if (yy + sy > disp_sy) sy= disp_sy - yy;
   if (sx <= 0 || sy <= 0) return;

   r0= ((val >> disp_rs) << disp_rl) & 255;
   g0= ((val >> disp_gs) << disp_gl) & 255;
   b0= ((val >> disp_bs) << disp_bl) & 255;

   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      while (sy-- > 0) {
	 for (a= sx; a>0; a--) {
	    r1= ((dp[0] >> disp_rs) << disp_rl) & 255;
	    g1= ((dp[0] >> disp_gs) << disp_gl) & 255;
	    b1= ((dp[0] >> disp_bs) << disp_bl) & 255;
	    r1 += (r0-r1) * opac / 100;
	    g1 += (g0-g1) * opac / 100;
	    b1 += (b0-b1) * opac / 100;
	    *dp++= disp_am | 
	       ((r1 >> disp_rl) << disp_rs) |
	       ((g1 >> disp_gl) << disp_gs) |
	       ((b1 >> disp_bl) << disp_bs);
	 }
	 dp += disp_my - sx;
      }
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      while (sy-- > 0) {
	 for (a= sx; a>0; a--) {
	    r1= ((dp[0] >> disp_rs) << disp_rl) & 255;
	    g1= ((dp[0] >> disp_gs) << disp_gl) & 255;
	    b1= ((dp[0] >> disp_bs) << disp_bl) & 255;
	    r1 += (r0-r1) * opac / 100;
	    g1 += (g0-g1) * opac / 100;
	    b1 += (b0-b1) * opac / 100;
	    *dp++= disp_am | 
	       ((r1 >> disp_rl) << disp_rs) |
	       ((g1 >> disp_gl) << disp_gs) |
	       ((b1 >> disp_bl) << disp_bs);
	 }
	 dp += disp_my - sx;
      }
   }
}      

//
//	Vertical line
//

void 
vline(int xx, int yy, int sy, int val) {
   if (xx < 0) return;
   if (yy < 0) { sy += yy; yy= 0; }
   if (yy + sy > disp_sy) sy= disp_sy - yy;
   if (sy <= 0) return;

   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      while (sy-- > 0) { *dp= val; dp += disp_my; }
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      while (sy-- > 0) { *dp= val; dp += disp_my; }
   }
}

//
//	Horizontal line
//

void 
hline(int xx, int yy, int sx, int val) {
   if (xx < 0) { sx += xx; xx= 0; }
   if (yy < 0) return;
   if (xx + sx > disp_sx) sx= disp_sx - xx;
   if (sx <= 0) return;

   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      while (sx-- > 0) *dp++= val;
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      while (sx-- > 0) *dp++= val;
   }
}

//
//	Get the colour value of a point.  This is only useful for
//	replotting or comparing to colour[] values, as it might be in
//	any of several packed formats.
//

int 
get_point(int xx, int yy) {
   if (xx < 0 || 
       yy < 0 ||
       xx >= disp_sx ||
       yy >= disp_sy) return 0;

   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      return *dp;
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      return *dp;
   }
   return 0;
}   
   

//
//	Plot a single point
//

void 
plot(int xx, int yy, int val) {
   if (xx < 0 || 
       yy < 0 ||
       xx >= disp_sx ||
       yy >= disp_sy) return;

   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      *dp= val;
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      *dp= val;
   }
}   
   

//
//	Draw text on the current display (as specified in global
//	variables 'disp_*').  'siz' is 8 for 6x8 text, or 16 for 8x16
//	text.  If the text will run off the right end of the screen,
//	it is truncated.  If the text is in any other way off the edge
//	of the screen, nothing is written at all.
//
//	The string can only use ASCII characters 32-127, and is
//	displayed as white on transparent by default.  However, a
//	character >= 128 allows a different colour-pair to be
//	selected.  The colour-pair is made up of colour[ch-128] for
//	background and colour[ch-128+1] for foreground.
//
//	A character in the range 1-31 has a special effect -- it
//	outputs an endless number of spaces (i.e. until the right edge
//	of the screen is reached).  That means a '\n' blanks to EOL.
//
//	Note the code for 16-bit versus 32-bit is identical except for
//	the type of 'dp'.
//

void
drawtext(int siz, int xx, int yy, const char *str) {
   int fg= colour[1];
   int bg= -1;
   int big= (siz == 16);
   int sx= big ? 8 : 6;
   int sy= big ? 16 : 8;
   const char **font_base= big ? font8x16 : font6x8;
   int cnt;
   const char **font; const char *p;
   int ch, a, b;

   if (xx < 0 || yy < 0 || yy + sy > disp_sy) return;
   cnt= (disp_sx - xx) / sx;

   // 32-bit display
   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      
      while (cnt > 0 && (ch= (255 & *str++))) {
	 if (ch >= 128) {
	    bg= colour[ch-128];
	    fg= colour[ch-128+1];
	    continue;
	 }
	 ch -= 32; cnt--;
	 if (ch < 0) { ch= 0; str--; }
	 
	 font= font_base + sy * (ch >> 3);
	 if (bg != -1)
	    for (a= sy; a>0; a--) {
	       p= *font++ + (ch&7) * sx;
	       for (b= sx; b>0; b--)
		  *dp++= (*p++ == '0') ? fg : bg;
	       dp += disp_my - sx;
	    }
	 else 
	    for (a= sy; a>0; a--) {
	       p= *font++ + (ch&7) * sx;
	       for (b= sx; b>0; b--) {
		  if (*p++ == '0') *dp= fg;
		  dp++;
	       }
	       dp += disp_my - sx;
	    }
	 
	 dp += sx - disp_my * sy;
      }
   }

   // 16-bit display
   if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      
      while (cnt > 0 && (ch= (255 & *str++))) {
	 if (ch >= 128) {
	    bg= colour[ch-128];
	    fg= colour[ch-128+1];
	    continue;
	 }
	 ch -= 32; cnt--;
	 if (ch < 0) { ch= 0; str--; }
	 
	 font= font_base + sy * (ch >> 3);
	 if (bg != -1)
	    for (a= sy; a>0; a--) {
	       p= *font++ + (ch&7) * sx;
	       for (b= sx; b>0; b--)
		  *dp++= (*p++ == '0') ? fg : bg;
	       dp += disp_my - sx;
	    }
	 else 
	    for (a= sy; a>0; a--) {
	       p= *font++ + (ch&7) * sx;
	       for (b= sx; b>0; b--) {
		  if (*p++ == '0') *dp= fg;
		  dp++;
	       }
	       dp += disp_my - sx;
	    }
	 
	 dp += sx - disp_my * sy;
      }
   }
}

//
//      Generate a pure hues (red,yellow,green,cyan,blue,magenta) in
//      various intensities.
//
//	For hue 'hue' (0-6:red,yellow,green,cyan,blue,magenta,red),
//	and colour component 'rgb' (0-2:red,green,blue), and intensity
//	level 'lev' (0-255), pure_hue_dat[hue][rgb][lev] gives the RGB
//	component level required.  (hue==6 is added as a copy of
//	hue==0 to simplify some code later on).
//

int pure_hue_src[6][4]= {
   { 1, 0, 0, 30 * 255 / 100 },
   { 1, 1, 0, 89 * 255 / 100 },
   { 0, 1, 0, 59 * 255 / 100 },
   { 0, 1, 1, 70 * 255 / 100 },
   { 0, 0, 1, 11 * 255 / 100 },
   { 1, 0, 1, 41 * 255 / 100 }
};

Uint8 pure_hue_mem[6 * 2 * 256];
Uint8 *pure_hue_data[7][3];

void 
init_pure_hues() {
   Uint8 *dat= pure_hue_mem;
   Uint8 *p, *q;
   int a, b;
   for (a= 0; a<6; a++) {
      p= dat; dat += 256;
      q= dat; dat += 256;
      for (b= 0; b<3; b++)
	 pure_hue_data[a][b]= pure_hue_src[a][b] ? q : p;
      for (b= 0; b<256; b++) {
	 int sat= pure_hue_src[a][3];
	 if (b < sat) {
	    *p++= 0;
	    *q++= b * 255 / sat;
	 } else {
	    *p++= (b-sat) * 255 / (255-sat);
	    *q++= 255;
	 }
      }
   }

   // First hue repeated -- saves extra code
   pure_hue_data[6][0]= pure_hue_data[0][0];
   pure_hue_data[6][1]= pure_hue_data[0][1];
   pure_hue_data[6][2]= pure_hue_data[0][2];
}      

//
//	Plot a point using the given intensity and hue.  The hue is
//	taken modulo 1, i.e. 0-1 goes around the full colour circle,
//	same 1-2, etc.  Intensities >= 1 always give white.
//

void 
plot_hue(int xx, int yy, int sy, double ii, double hh) {
   double tmp;
   int hue, rat;
   int v0, v1, val;
   int r,g,b;
   int mag= (int)(ii * 255.0);

   if (mag < 0) mag= 0;
   if (mag > 255) mag= 255;
   rat= (int)floor(256 * modf(hh * 6.0, &tmp));
   hue= (int)tmp;

   if (hh < 0) { rat += 256.0; hue--; }
   hue %= 6;
   if (hue < 0) hue += 6;

   v0= pure_hue_data[hue][0][mag];
   v1= pure_hue_data[hue+1][0][mag];
   r= v0 + (((v1-v0) * rat) >> 8);

   v0= pure_hue_data[hue][1][mag];
   v1= pure_hue_data[hue+1][1][mag];
   g= v0 + (((v1-v0) * rat) >> 8);

   v0= pure_hue_data[hue][2][mag];
   v1= pure_hue_data[hue+1][2][mag];
   b= v0 + (((v1-v0) * rat) >> 8);

   val= disp_am | 
      ((r >> disp_rl) << disp_rs) |
      ((g >> disp_gl) << disp_gs) |
      ((b >> disp_bl) << disp_bs);
   
   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      while (sy-- > 0) { *dp= val; dp += disp_my; }
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      while (sy-- > 0) { *dp= val; dp += disp_my; }
   }
}

//
//	Prepare the colour intensity table for plotting using
//	plot_cint().  Note that first column gives intensity %age
//	mapped to 0-1024, and next three give RGB components.  Also
//	note that (30R+59G+11B)/255 gives the intensity %age.
//

static int cint_src[]= {
   0 * 1024/100, 	0, 0, 0,		// Black
   8 * 1024/100, 	0, 0, 192,		// Blue
   20 * 1024/100,	192, 0, 192,		// Magenta
   26 * 1024/100, 	224, 0, 0,		// Red
   45 * 1024/100,	224, 192, 0,		// Yellow
   59 * 1024/100, 	0, 255, 0,		// Green
   80 * 1024/100, 	0, 255, 255,		// Cyan
   100 * 1024/100, 	255, 255, 255,		// White

   //0 * 1024/100, 	0, 0, 0,		// Black
   //5 * 1024/100, 	0, 0, 128,		// Blue
   //15 * 1024/100, 	82, 0, 128,		// Magenta(ish)
   //30 * 1024/100, 	255, 0, 0,		// Red
   //45 * 1024/100, 	255, 65, 0, 		// Yellow(ish)
   //59 * 1024/100, 	0, 255, 0,		// Green
   //70 * 1024/100, 	0, 255, 255,		// Cyan
   //100 * 1024/100, 	255, 255, 255,		// White
};

int cint_table[1024];

void 
init_cint_table() {
   int a;
   int *ip= cint_src;
   int R, G, B;
   int mul1, mul2, div;

   for (a= 0; a<1024; a++) {
      if (a >= ip[4]) ip += 4;	// Move onto next pair
      div= ip[4]-ip[0];
      mul2= a - ip[0];
      mul1= div - mul2;
      R= (ip[1] * mul1 + ip[5] * mul2) / div;
      G= (ip[2] * mul1 + ip[6] * mul2) / div;
      B= (ip[3] * mul1 + ip[7] * mul2) / div;
      cint_table[a]= disp_am | 
	 ((R >> disp_rl) << disp_rs) |
	 ((G >> disp_gl) << disp_gs) |
	 ((B >> disp_bl) << disp_bs);
   }
}      
   

//
//	Plot a point with the given intensity (0->1), using a colour
//	table to map intensity to colours, allowing a wider range of
//	values to be visualised than by using a simple gray scale.
//	Intensities >=1 give white.  (cint == colour intensity)
//

void 
plot_cint(int xx, int yy, int sy, double ii) {
   int vv= (int)(1024.0 * ii);
   int val;

   if (vv >= 1024)
      val= colour[1];
   else if (vv < 0)
      val= colour[0];
   else 
      val= cint_table[vv];

   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      while (sy-- > 0) { *dp= val; dp += disp_my; }
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      while (sy-- > 0) { *dp= val; dp += disp_my; }
   }
}      

//
//	Plot a bar of colour-intensity, going from xx+0 to just before
//	xx+sx, at the very most (sx should be -ve for a left-leaning
//	bar).  Height of unit amplitude is 'unit' (unit being 1.0,
//	displayed as white), and the value to display is 'ii'.  The
//	last pixel of the resulting bar carries the accurate
//	colour-intensity for the value 'ii', so that no detail is
//	lost.
//

void 
plot_cint_bar(int xx, int yy, int sx, int sy, int unit, double ii) {
   int inc= (1024 * 256) / unit;
   int curr= 0;
   int target= (int)(ii * (1024 * 256));
   int dx= sx < 0 ? -1 : 1;
   int mx= xx + sx;
   int a;

   while (curr <= target && xx != mx) {
      int vv, val;
      if (curr + inc <= target)
	 vv= (curr + inc/2) >> 8;
      else 
	 vv= target >> 8;
      
      if (vv >= 1024)
	 val= colour[1];
      else if (vv < 0)
	 val= colour[0];
      else 
	 val= cint_table[vv];

      if (disp_pix32) {
	 Uint32 *dp= disp_pix32 + xx + yy * disp_my;
	 for (a= sy; a-- > 0; ) { *dp= val; dp += disp_my; }
      } else if (disp_pix16) {
	 Uint16 *dp= disp_pix16 + xx + yy * disp_my;
	 for (a= sy; a-- > 0; ) { *dp= val; dp += disp_my; }
      }
      
      xx += dx;
      curr += inc;
   }
}


//
//	Plot a point using the given intensity.  A gray-scale is used.
//	If the intensity is >= 1, white is plotted.
//

void 
plot_gray(int xx, int yy, int sy, double ii) {
   int val;
   int v= (int)(255.0 * ii);
   if (v > 255) v= 255;
   
   val= disp_am | 
      ((v >> disp_rl) << disp_rs) |
      ((v >> disp_gl) << disp_gs) |
      ((v >> disp_bl) << disp_bs);
   
   if (disp_pix32) {
      Uint32 *dp= disp_pix32 + xx + yy * disp_my;
      while (sy-- > 0) { *dp= val; dp += disp_my; }
   } else if (disp_pix16) {
      Uint16 *dp= disp_pix16 + xx + yy * disp_my;
      while (sy-- > 0) { *dp= val; dp += disp_my; }
   }
}


     
//
//	Two simple plain-ASCII mono-spaced fonts
//

const char *font6x8[]= {
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "----- --0-- -0-0- -0-0- --0-- 00--0 --0-- --0-- ",
   "----- --0-- -0-0- 00000 00000 00-0- -000- --0-- ",
   "----- --0-- ----- -0-0- 0-0-- --0-- --0-- ----- ",
   "----- --0-- ----- -0-0- 00000 --0-- -00-0 ----- ",
   "----- ----- ----- 00000 --0-0 -0-00 0--0- ----- ",
   "===== ==0== ===== =0=0= 00000 0==00 =00=0 ===== ",
   "----- ----- ----- ----- --0-- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "---0- -0--- ----- ----- ----- ----- ----- ----- ",
   "--0-- --0-- -0-0- --0-- ----- ----- ----- ----0 ",
   "--0-- --0-- --0-- --0-- ----- ----- ----- ---0- ",
   "--0-- --0-- 00000 00000 ----- 00000 ----- --0-- ",
   "--0-- --0-- --0-- --0-- -00-- ----- -00-- -0--- ",
   "===0= =0=== =0-0= ==0== =00== ===== =00== 0-=== ",
   "----- ----- ----- ----- -0--- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "-000- --0-- -000- -000- ---0- 00000 -000- 00000 ",
   "0---0 -00-- 0---0 0---0 --00- 0---- 0---- ----0 ",
   "0-0-0 --0-- ----0 --00- -0-0- 0000- 0000- ---0- ",
   "0-0-0 --0-- -000- ----0 0--0- ----0 0---0 --0-- ",
   "0---0 --0-- 0---- 0---0 00000 0---0 0---0 --0-- ",
   "=000= 00000 00000 =000= ==-0= =000= =000= ==0== ",
   "----- ----- ----- ----- ----- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "-000- -000- ----- ----- ----- ----- ----- -000- ",
   "0---0 0---0 -00-- -00-- ---0- ----- -0--- 0---0 ",
   "-000- 0---0 -00-- -00-- --0-- 00000 --0-- ---0- ",
   "0---0 -0000 ----- ----- -0--- ----- ---0- --0-- ",
   "0---0 ----0 -00-- -00-- --0-- 00000 --0-- ----- ",
   "=000= =000= =00== =00== ===0= ===== =0-== ==0== ",
   "----- ----- ----- -0--- ----- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "-000- -000- 0000- -000- 000-- 00000 00000 -000- ",
   "0---0 0---0 0---0 0---0 0--0- 0---- 0---- 0---0 ",
   "0-000 0---0 0000- 0---- 0---0 0000- 0000- 0---- ",
   "0-000 00000 0---0 0---- 0---0 0---- 0---- 0-000 ",
   "0---- 0---0 0---0 0---0 0--0- 0---- 0---- 0---0 ",
   "=000= 0===0 0000= =000= 000== 00000 0==== =000= ",
   "----- ----- ----- ----- ----- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "0---0 00000 ----0 0--0- 0---- 0---0 0---0 -000- ",
   "0---0 --0-- ----0 0-0-- 0---- 00-00 00--0 0---0 ",
   "00000 --0-- ----0 00--- 0---- 0-0-0 0-0-0 0---0 ",
   "0---0 --0-- 0---0 0-0-- 0---- 0---0 0-0-0 0---0 ",
   "0---0 --0-- 0---0 0--0- 0---- 0---0 0--00 0---0 ",
   "0===0 00000 =000= 0===0 00000 0===0 0===0 =000= ",
   "----- ----- ----- ----- ----- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "0000- -000- 0000- -000- 00000 0---0 0---0 0---0 ",
   "0---0 0---0 0---0 0---- --0-- 0---0 0---0 0---0 ",
   "0---0 0---0 0---0 -000- --0-- 0---0 0---0 0---0 ",
   "0000- 0-0-0 0000- ----0 --0-- 0---0 0---0 0-0-0 ",
   "0---- 0--00 0--0- 0---0 --0-- 0---0 -0-0- 0-0-0 ",
   "0==== =00-0 0===0 =000= ==0== =000= ==0== =0=0= ",
   "----- ----- ----- ----- ----- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "0---0 0---0 00000 --000 ----- 000-- --0-- ----- ",
   "-0-0- -0-0- ---0- --0-- 0---- --0-- -0-0- ----- ",
   "--0-- --0-- --0-- --0-- -0--- --0-- 0---0 ----- ",
   "--0-- --0-- --0-- --0-- --0-- --0-- ----- ----- ",
   "-0-0- --0-- -0--- --0-- ---0- --0-- ----- ----- ",
   "0===0 ==0== 00000 ==000 ===-0 000== ==-== ===== ",
   "----- ----- ----- ----- ----- ----- ----- 00000 ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "-0--- ----- 0---- ----- ----0 ----- --000 ----- ",
   "--0-- -000- 0---- -000- ----0 -000- -0--- -0000 ",
   "---0- ----0 0000- 0---0 -0000 0---0 -000- 0---0 ",
   "----- -0000 0---0 0---- 0---0 0000- -0--- 0---0 ",
   "----- 0---0 0---0 0---0 0---0 0---- -0--- -0000 ",
   "----- =0000 0000= =000= =0000 =000= =0-== ===-0 ",
   "----- ----- ----- ----- ----- ----- ----- -000- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "0---- --0-- ---0- 0---- --0-- ----- ----- ----- ",
   "0---- ----- ----- 0---- --0-- 00-0- 0000- -000- ",
   "0000- -00-- ---0- 0--0- --0-- 0-0-0 0---0 0---0 ",
   "0---0 --0-- ---0- 000-- --0-- 0-0-0 0---0 0---0 ",
   "0---0 --0-- ---0- 0--0- --0-- 0-0-0 0---0 0---0 ",
   "0==-0 =000= 0-=0= 0-=-0 =000= 0=0-0 0==-0 =000- ",
   "----- ----- -00-- ----- ----- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ===== ",
   "----- ----- ----- ----- -0--- ----- ----- ----- ",
   "0000- -0000 -0000 -000- 0000- 0---0 0---0 0---0 ",
   "0---0 0---0 0---- 0---- -0--- 0---0 0---0 0-0-0 ",
   "0---0 0---0 0---- -000- -0--- 0---0 -0-0- 0-0-0 ",
   "0000- -0000 0---- ----0 -0--- 0---0 -0-0- 0-0-0 ",
   "0==== ===-0 0-=== 0000= ==00= =000= ==0== =000= ",
   "0---- ----00----- ----- ----- ----- ----- ----- ",
   "===== ===== ===== ===== ===== ===== ===== ----- ",
   "----- ----- ----- ---00 --0-- 00--- -0--- 0-0-0 ",
   "0---0 0---0 00000 --0-- --0-- --0-- 0-0-0 -0-0- ",
   "-0-0- 0---0 ---0- ---0- --0-- -0--- ---0- 0-0-0 ",
   "--0-- 0---0 --0-- -00-- --0-- -000- ----- -0-0- ",
   "-0-0- -0000 -0--- ---0- --0-- -0--- ----- 0-0-0 ",
   "0==-0 ===-0 00000 ==0-- ==0== --0== ===== -0-0- ",
   "----- -000- ----- ---00 ----- 00--- ----- ----- ",
};

const char *font8x16[]= {
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ===00== =00=00= =00=00= ===00== ======= ==000== ===00== ",
   "------- --0000- -00-00- -00-00- ---00-- 000--00 -00-00- ---00-- ",
   "------- --0000- -00-00- -00-00- -00000- 000--00 -00-00- ---00-- ",
   "------- --0000- --0-0-- 0000000 00---00 00--00- --000-- --00--- ",
   "------- ---00-- ------- -00-00- -00---- ---00-- -000--0 ------- ",
   "------- ---00-- ------- 0000000 --000-- --00--- -000000 ------- ",
   "------- ------- ------- -00-00- ----00- -00--00 00--00- ------- ",
   "------- ---00-- ------- -00-00- 00---00 00--000 00--00- ------- ",
   "======= ===00== ======= =00=00= =00000= 00==000 =000=00 ======= ",
   "------- ------- ------- ------- ---00-- ------- ------- ------- ",
   "------- ------- ------- ------- ---00-- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "====00= ==00=== ======= ======= ======= ======= ======= =====00 ",
   "---00-- ---00-- ---0--- ------- ------- ------- ------- -----00 ",
   "--00--- ----00- -0-0-0- ---00-- ------- ------- ------- ----00- ",
   "--00--- ----00- --000-- ---00-- ------- ------- ------- ----00- ",
   "--00--- ----00- 0000000 -000000 ------- 0000000 ------- ---00-- ",
   "--00--- ----00- --000-- ---00-- ------- ------- ------- ---00-- ",
   "--00--- ----00- -0-0-0- ---00-- ------- ------- ------- --00--- ",
   "--00--- ----00- ---0--- ------- ---00-- ------- ---00-- --00--- ",
   "==00=== ====00= ======= ======= ===00== ======= ===00== =00==== ",
   "---00-- ---00-- ------- ------- ---00-- ------- ------- -00---- ",
   "----00- --00--- ------- ------- --00--- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- -----0- ------- ------- ------- ",
   "=00000= ===00== =00000= =00000= ====00= 0000000 =00000= 0000000 ",
   "00---00 -0000-- 00---00 00---00 ---000- 00----- 00---00 00---00 ",
   "00---00 ---00-- 00---00 -----00 --0000- 00----- 00----- -----00 ",
   "00-0-00 ---00-- -----00 -----00 -00-00- 000000- 00----- -----00 ",
   "00-0-00 ---00-- ---000- --0000- 00--00- -----00 000000- ----00- ",
   "00-0-00 ---00-- -000--- -----00 0000000 -----00 00---00 ---00-- ",
   "00---00 ---00-- 00----- -----00 ----00- -----00 00---00 --00--- ",
   "00---00 ---00-- 00---00 00---00 ----00- 00---00 00---00 --00--- ",
   "=00000= =000000 0000000 =00000= ===0000 =00000= =00000= ==00=== ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- -00000- ",
   "=00000= =00000= ======= ======= =====00 ======= 00===== 00===00 ",
   "00---00 00---00 ------- ------- ----00- ------- -00---- 00---00 ",
   "00---00 00---00 ------- ------- ---00-- ------- --00--- 00---00 ",
   "00---00 00---00 ---00-- ---00-- --00--- 0000000 ---00-- -----00 ",
   "-00000- -000000 ---00-- ---00-- -00---- ------- ----00- ---000- ",
   "00---00 -----00 ------- ------- --00--- 0000000 ---00-- ---00-- ",
   "00---00 -----00 ------- ------- ---00-- ------- --00--- ------- ",
   "00---00 00---00 ---00-- ---00-- ----00- ------- -00---- ---00-- ",
   "=00000= =00000= ===00== ===00== =====00 ======= 00===== ===00== ",
   "------- ------- ------- ---00-- ------- ------- ------- ------- ",
   "------- ------- ------- --00--- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "=00000= ==000== 000000= ==0000= 00000== 0000000 0000000 =00000= ",
   "00---00 -00-00- 00---00 -00--00 00--00- 00----0 00----0 00---00 ",
   "00---00 00---00 00---00 00----0 00---00 00----- 00----- 00----- ",
   "00---00 00---00 00---00 00----- 00---00 00----- 00----- 00----- ",
   "00-0000 00---00 000000- 00----- 00---00 000000- 000000- 00----- ",
   "00-0000 0000000 00---00 00----- 00---00 00----- 00----- 00--000 ",
   "00-000- 00---00 00---00 00----0 00---00 00----- 00----- 00---00 ",
   "00----- 00---00 00---00 -00--00 00--00- 00----0 00----- 00---00 ",
   "=000000 00===00 000000= ==0000= 00000== 0000000 000==== =00000= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "00===00 =000000 ==00000 00===00 000==== 00===00 00===00 =00000= ",
   "00---00 ---00-- -----00 00--00- 00----- 00---00 00---00 00---00 ",
   "00---00 ---00-- -----00 00-00-- 00----- 000-000 000--00 00---00 ",
   "00---00 ---00-- -----00 0000--- 00----- 0000000 0000-00 00---00 ",
   "0000000 ---00-- -----00 000---- 00----- 00-0-00 00-0000 00---00 ",
   "00---00 ---00-- -----00 0000--- 00----- 00-0-00 00--000 00---00 ",
   "00---00 ---00-- 00---00 00-00-- 00----- 00---00 00---00 00---00 ",
   "00---00 ---00-- 00---00 00--00- 00----0 00---00 00---00 00---00 ",
   "00===00 =000000 =00000= 00===00 0000000 00===00 00===00 =00000= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "000000= =00000= 000000= =00000= 0000000000===00 00===00 00===00 ",
   "00---00 00---00 00---00 00---00 0--00--000---00 00---00 00---00 ",
   "00---00 00---00 00---00 00----- ---00-- 00---00 00---00 00---00 ",
   "00---00 00---00 00---00 -00---- ---00-- 00---00 00---00 00-0-00 ",
   "00---00 00---00 00--00- --000-- ---00-- 00---00 00---00 00-0-00 ",
   "000000- 00---00 00000-- ----00- ---00-- 00---00 00---00 0000000 ",
   "00----- 00-0-00 00--00- -----00 ---00-- 00---00 -00-00- 000-000 ",
   "00----- 00-00-0 00---00 00---00 ---00-- 00---00 --000-- 000-000 ",
   "000==== =00000= 00===00 =00000= ==0000= =00000= ===0=== =00=00= ",
   "------- -----00 ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ---0--- ------- ",
   "00===00 00====000000000 =00000= =00==== =00000= ==000== ======= ",
   "00---00 00----000----00 -00---- -00---- ----00- -00-00- ------- ",
   "00---00 00----00-----00 -00---- --00--- ----00- 00---00 ------- ",
   "-00-00- 00----00----00- -00---- --00--- ----00- ------- ------- ",
   "--000-- -00--00 ---00-- -00---- ---00-- ----00- ------- ------- ",
   "-00-00- --0000- --00--- -00---- ---00-- ----00- ------- ------- ",
   "00---00 ---00-- -00---- -00---- ----00- ----00- ------- ------- ",
   "00---00 ---00-- 00----0 -00---- ----00- ----00- ------- ------- ",
   "00===00 ==0000= 0000000 =00000= =====00 =00000= ======= ======= ",
   "------- ------- ------- ------- -----00 ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- 0000000 ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "==00=== ======= 000==== ======= ====000 ======= ===0000 ======= ",
   "--00--- ------- 00----- ------- -----00 ------- --00--- ------- ",
   "--00--- --0000- 00-000- --0000- --00000 --0000- --00--- --00000 ",
   "---00-- -----00 000--00 -00--00 -00--00 -00--00 --00--- -00--00 ",
   "------- -000000 00---00 00----- 00---00 00---00 -000000 00---00 ",
   "------- 00---00 00---00 00----- 00---00 000000- --00--- 00---00 ",
   "------- 00---00 00---00 00----- 00---00 00----- --00--- 00---00 ",
   "------- 00--000 00---00 00---00 00--000 00---00 --00--- 00--000 ",
   "======= =000=00 000000= =00000= =000=00 =00000= ==00=== =000=00 ",
   "------- ------- ------- ------- ------- ------- ------- ----00- ",
   "------- ------- ------- ------- ------- ------- ------- -0000-- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "000==== ===00== ====00= 00===== ==000== ======= ======= ======= ",
   "00----- ------- ------- 00----- ---00-- ------- ------- ------- ",
   "00-000- --000-- ---000- 00---00 ---00-- 000-00- 00-000- --0000- ",
   "000--00 ---00-- ----00- 00---00 ---00-- 0000000 000--00 -00--00 ",
   "00---00 ---00-- ----00- 00--00- ---00-- 00-0-00 00---00 00---00 ",
   "00---00 ---00-- ----00- 00000-- ---00-- 00-0-00 00---00 00---00 ",
   "00---00 ---00-- ----00- 00--00- ---00-- 00-0-00 00---00 00---00 ",
   "00---00 ---00-- ----00- 00---00 ---00-- 00---00 00---00 00---00 ",
   "00===00 ==0000= ====00= 00===00 ==0000= 00===00 00===00 =00000= ",
   "------- ------- 00--00- ------- ------- ------- ------- ------- ",
   "------- ------- -0000-- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= =00==== ======= ======= ======= ",
   "------- ------- ------- ------- -00---- ------- ------- ------- ",
   "00-000- --00000 00-000- -00000- 000000- 00---00 00---00 00---00 ",
   "000--00 -00--00 000--00 00---00 -00---- 00---00 00---00 00---00 ",
   "00---00 00---00 00---00 -00---- -00---- 00---00 00---00 00---00 ",
   "00---00 00---00 00----- --000-- -00---- 00---00 00---00 00-0-00 ",
   "00---00 00---00 00----- ----00- -00---- 00---00 -00-00- 00-0-00 ",
   "00---00 00---00 00----- 00---00 -00--00 00--000 --000-- 0000000 ",
   "000000= =000000 000==== =00000= ==0000= =000=00 ===0=== =00=00= ",
   "00----- -----00 ------- ------- ------- ------- ------- ------- ",
   "000---- -----000------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ===000= ===00== =000=== =000=== =0=0=0= ",
   "------- ------- ------- --00--- ---00-- ---00-- 00-00-000-0-0-0 ",
   "00---00 00---00 0000000 --00--- ---00-- ---00-- ----000 -0-0-0- ",
   "-00-00- 00---00 0----00 --00--- ---00-- ---00-- ------- 0-0-0-0 ",
   "--000-- 00---00 ----00- 000---- ------- ----000 ------- -0-0-0- ",
   "---0--- 00---00 ---00-- --00--- ---00-- ---00-- ------- 0-0-0-0 ",
   "--000-- 00---00 --00--- --00--- ---00-- ---00-- ------- -0-0-0- ",
   "-00-00- 00--000 -00---0 --00--- ---00-- ---00-- ------- 0-0-0-0 ",
   "00===00 =000=00 0000000 ===000= ===00== =000=== ======= =0=0=0= ",
   "------- ----00- ------- ------- ------- ------- ------- 0-0-0-0 ",
   "------- -0000-- ------- ------- ------- ------- ------- -0-0-0- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "------- ------- ------- ------- ------- ------- ------- ------- ",
   "======= ======= ======= ======= ======= ======= ======= ======= ",
};


// END //


