/**
 * @file graphics.h
 * @brief Low-level SDL pixel-buffer drawing primitives.
 *
 * Provides an SDL-version-agnostic (SDL1 / SDL2) drawing layer over a 16- or
 * 32-bit pixel buffer.  Colours are stored as packed 0xRRGGBB integers and
 * mapped at init time via map_rgb().
 *
 * @defgroup fiview_graphics Graphics subsystem
 * @ingroup fiview
 * @{
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#ifdef T_SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif

extern int   colour_data[];       /**< Logical colour definitions (filled by graphics_init()). */
extern int   suspend_update;      /**< Non-zero: suppress calls to SDL_UpdateRect/RenderPresent. */
extern int   pure_hue_src[][4];   /**< Source RGB data for the pure-hue colour ramps. */
extern Uint8 pure_hue_mem[];      /**< Pixel memory backing #pure_hue_data. */
extern Uint8 *pure_hue_data[][3]; /**< R/G/B channel pointers into the hue colour ramps. */
extern int   cint_table[];        /**< Pre-computed colour-intensity lookup table. */
extern const char *font6x8[];     /**< 6×8 pixel font glyph bitmaps. */
extern const char *font8x16[];    /**< 8×16 pixel font glyph bitmaps. */
void graphics_init(int sx, int sy, int bpp); /**< Open the SDL window and set up the pixel buffer. */
int  map_rgb(int col);                       /**< Map a 0xRRGGBB colour to the native pixel format. */
void update(int xx, int yy, int sx, int sy);       /**< Request a screen refresh for the given rectangle (coalesced). */
void update_force(int xx, int yy, int sx, int sy); /**< Immediately flush a rectangle to screen. */
void mouse_pointer(int on);                        /**< Show (@p on=1) or hide (@p on=0) the hardware mouse cursor. */
void clear_rect(int xx, int yy, int sx, int sy, int val);                /**< Fill a rectangle with colour @p val. */
void alpha_rect(int xx, int yy, int sx, int sy, int val, int opac);      /**< Alpha-blend a rectangle with colour @p val at opacity @p opac/255. */
void vline(int xx, int yy, int sy, int val);                              /**< Draw a vertical line of height @p sy. */
void hline(int xx, int yy, int sx, int val);                              /**< Draw a horizontal line of width @p sx. */
int  get_point(int xx, int yy);                                           /**< Read the pixel colour at (@p xx, @p yy). */
void plot(int xx, int yy, int val);                                       /**< Set a single pixel. */
void drawtext(int siz, int xx, int yy, const char *str);                  /**< Draw a string using the built-in bitmap font (siz=6 or 8). */
void init_pure_hues(void);                                                     /**< Pre-compute the hue colour ramps. */
void plot_hue(int xx, int yy, int sy, double ii, double hh);                   /**< Draw a vertical hue bar at (@p xx, @p yy). */
void init_cint_table(void);                                                    /**< Pre-compute the colour-intensity table. */
void plot_cint(int xx, int yy, int sy, double ii);                             /**< Draw a colour-intensity bar at (@p xx, @p yy). */
void plot_cint_bar(int xx, int yy, int sx, int sy, int unit, double ii);       /**< Draw a multi-column colour-intensity block. */
void plot_gray(int xx, int yy, int sy, double ii);                             /**< Draw a grayscale bar at (@p xx, @p yy). */

/** @} */ /* end fiview_graphics */

#endif
