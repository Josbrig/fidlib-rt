/**
 * @file fiview.h
 * @brief Global state declarations and application-level prototypes for fiview.
 *
 * This is the central "glue" header that every fiview translation unit includes
 * (via `all.h`).  It declares all shared globals organised by concern:
 * display surface, layout rectangles, settings, pager state, cache invalidation
 * flags, and UI callbacks.
 *
 * @defgroup fiview_globals fiview global state
 * @ingroup fiview
 * @{
 */

#ifndef FIVIEW_H
#define FIVIEW_H

#include "display.h"
#include "filter.h"

#ifdef T_SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif


#ifndef T_SDL2
extern SDL_Surface *disp;     /**< SDL1 display surface (absent in SDL2 build). */
#endif
extern Uint32 *disp_pix32;    /**< Pixel pointer for 32-bit display mode, else NULL. */
extern Uint16 *disp_pix16;    /**< Pixel pointer for 16-bit display mode, else NULL. */
extern int disp_my;           /**< Display row stride in pixels. */
extern int disp_sx, disp_sy;  /**< Display width and height in pixels. */
extern int disp_rl, disp_rs;  /**< Red channel left-shift and right-shift for pack/unpack. */
extern int disp_gl, disp_gs;  /**< Green channel left-shift and right-shift. */
extern int disp_bl, disp_bs;  /**< Blue channel left-shift and right-shift. */
extern int disp_am;           /**< Alpha mask (0 if alpha not supported). */
extern int font_sx, font_sy;  /**< Default font character size in pixels. */
extern int *colour;           /**< Mapped colour array (indexed by logical colour ID). */


extern Rect d_info;    /**< Top-left filter information panel. */
extern Rect d_freq;    /**< Mini frequency-response thumbnail. */
extern Rect d_time;    /**< Mini time-domain thumbnail. */
extern Rect d_minf;    /**< Main-area info line. */
extern Rect d_main;    /**< Main display area (frequency or time). */
extern double *d_wrk1; /**< Scratch buffer for display routines, @c disp_sx entries. */


extern double a_f0, a_f1;  /**< Frequency range given by -f flag. */
extern int    a_adj;        /**< Non-zero: auto-adjust frequencies to ±50 % levels. */


extern Filter *curr;           /**< Currently displayed filter. */
extern double  s_rate;         /**< Sampling rate in Hz (0 if unset). */
extern int     s_main;         /**< Main view: 'F'=frequency, 'T'=time, 'I'=info, 'H'=help. */
extern int     s_tmzoom;       /**< Time mini-display zoom (>0 = samples/pixel, <0 = pixels/sample). */
extern int     s_zoom;         /**< ×10 zoom toggle for freq and time views. */
extern int     s_minmax;       /**< Non-zero: overlay min/max markers on freq display. */
extern double  s_freq0, s_freq1; /**< Frequency range for the main frequency display. */
extern int     s_ftmod;        /**< Frequency-test mode: 0=off, 'w'=wavelet, 's'=slide. */
extern double  s_ftarg;        /**< Argument for the active frequency-test mode. */
extern int     s_logsc;        /**< Non-zero: use logarithmic frequency scale. */
extern double  s_tim0, s_tim1; /**< Time range for the main time display (sample counts, ≤ 0). */


extern int    s_pager_lin;     /**< Index of the top line currently visible. */
extern char  *s_pager_txt;     /**< Current pager body text (newline-separated). */
extern char  *s_pager_inf;     /**< One-line info string shown above the pager. */
extern int    s_pager_cnt;     /**< Total number of lines in @c s_pager_txt. */
extern int    s_pager_at_end;  /**< Non-zero when the last line is visible. */
extern int    s_pager_typ;     /**< @c s_main type that the pager represents. */


extern double   c_tim0, c_tim1;  /**< Cached time range used for the last time buffer. */
extern int      c_tim_sx;        /**< Cached time display width used for the last buffer. */
extern Filter  *c_tim_filt;      /**< Filter for which the time buffer was last computed. */
extern float   *c_tim_buf;       /**< Cached impulse-response sample array. */
extern double   c_freq0, c_freq1;/**< Cached frequency range for the last freq buffer. */
extern int      c_freq_sx;       /**< Cached freq display width. */
extern Filter  *c_freq_filt;     /**< Filter for which the freq buffer was last computed. */
extern double  *c_freq_buf;      /**< Cached frequency-response amplitude array. */
extern int      c_ftmod;         /**< Cached frequency-test mode for the test buffer. */
extern double   c_ftarg;         /**< Cached frequency-test argument. */
extern double  *c_ftbuf;         /**< Cached frequency-test result array. */
extern Filter  *c_info_filt;     /**< Filter for which the info panel was last rendered. */


extern int   rearrange;          /**< Non-zero: window was resized, redo layout next frame. */
extern int   redraw;             /**< Non-zero: schedule a full redraw next frame. */
extern int   part_cmd;           /**< Non-zero: a multi-key command sequence is in progress. */
extern int   n_filt;             /**< Total number of loaded filters. */
extern int   s_overlay;          /**< Non-zero: show all filters overlaid (o-key toggle). */
extern char *prompt;             /**< Active prompt label string, or NULL. */
extern void (*prompt_cb)(char *str); /**< Callback invoked when the prompt is confirmed. */
extern char  prompt_buf[];       /**< Buffer receiving typed prompt input. */
extern int   prompt_len;         /**< Current length of @c prompt_buf. */
extern int   grabkey;            /**< Non-zero: next keypress is routed to @c grabkey_cb. */
extern void (*grabkey_cb)(int key, int mod); /**< Callback receiving the grabbed key. */
extern char *helptext;           /**< Heap-allocated full help text (gen_helptext()). */
extern double nan_global;        /**< NaN substitute for MSVC (see all.h). */


FID_NORETURN void error(const char *fmt, ...);    /**< Print message to stderr and exit(1). */
FID_NORETURN void errorSDL(const char *fmt, ...); /**< Like error() but appends SDL_GetError(). */
FID_NORETURN void usage(void);                    /**< Print usage text and exit(1). */
void  warn(const char *fmt, ...);    /**< Print a non-fatal warning to stderr. */
void *Alloc(size_t size);            /**< calloc wrapper — aborts on allocation failure. */
char *StrDup(const char *str);       /**< strdup wrapper — aborts on allocation failure. */


int  inRect(Rect *rr, int xx, int yy);              /**< Test whether pixel (@p xx, @p yy) lies inside @p rr. */
void auto_adjust_time(void);                         /**< Auto-scale the time axis to the current filter's delay. */
void auto_adjust_freq(void);                         /**< Auto-scale the frequency axis to the filter's pass-band. */
void set_s_tmzoom(void);                             /**< Recalculate #s_tmzoom from the current time range. */
void display_prompt(void);                           /**< Render the active input prompt to the status bar. */
void run_prompt(const char *txt, void (*rout)(char *));             /**< Start a text-input prompt. */
void run_grabkey(const char *txt, void (*rout)(int key, int mod));  /**< Wait for the next keypress via callback. */
void set_test_mode(char *str);          /**< Parse and apply a frequency-test mode string. */
void set_logscale(int key, int mod);    /**< Toggle or cycle the log-frequency scale mode. */
void load_filter_prompt_cb(char *spec); /**< Prompt callback: load a new filter from the typed spec. */
void export_freq_csv(void);             /**< Export the current frequency response to @c fiview_freq.csv. */


#define ALLOC(type)          ((type*)Alloc(sizeof(type)))           /**< Allocate and zero one instance of @p type. */
#define ALLOC_ARR(cnt, type) ((type*)Alloc((size_t)(cnt) * sizeof(type))) /**< Allocate and zero @p cnt elements of @p type. */

/** @} */ /* end fiview_globals */

#endif
