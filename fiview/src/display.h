/**
 * @file display.h
 * @brief Fiview display layout, drawing routines, and progress indicator.
 *
 * Defines the @ref Rect screen region descriptor, the @ref Progress bar helper,
 * time-slot mapping macros, and all drawing function prototypes.
 *
 * @defgroup fiview_display Display subsystem
 * @ingroup fiview
 * @{
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <math.h>

/** @note Avoids name clash with macOS QuickDraw's @c Rect. */
#define Rect FVRect

/**
 * @brief Axis-aligned screen rectangle.
 */
typedef struct Rect Rect;
struct Rect {
   int xx; /**< Left edge, in pixels. */
   int yy; /**< Top edge, in pixels. */
   int sx; /**< Width, in pixels. */
   int sy; /**< Height, in pixels. */
};

/**
 * @brief Console progress bar state.
 */
typedef struct Progress Progress;
struct Progress {
   int         cnt;   /**< Samples processed so far. */
   int         upd;   /**< Next value at which to redraw. */
   int         max;   /**< Total expected sample count. */
   int         wid;   /**< Character width of the progress bar. */
   int         tim0;  /**< Wall-clock start time (SDL ticks). */
   int         step;  /**< Redraw interval in samples. */
   const char *txt;   /**< Label prefix shown before the bar. */
   int         force; /**< Non-zero: force redraw on next progress_update(). */
};

/**
 * @brief Start sample index for pixel column @p xx in a time-axis display.
 *
 * Samples are counted leftward from zero (the current sample at the right edge).
 * Returns the first sample index (most recent) that maps to pixel @p xx.
 *
 * @param xx    Pixel column (0 = rightmost).
 * @param sx    Total display width in pixels.
 * @param tim0  Leftmost sample index (negative, most distant in time).
 * @param tim1  Rightmost sample index (negative, closest in time — usually near 0).
 */
#define TSLOT_S(xx,sx,tim0,tim1) ((int)floor(-tim1+((tim1-tim0)*xx)/sx))

/**
 * @brief End sample index (exclusive) for pixel column @p xx in a time-axis display.
 * @see TSLOT_S
 */
#define TSLOT_E(xx,sx,tim0,tim1) ((int)floor(-tim1+((tim1-tim0)*(xx+0.999))/sx))

void arrange_display(void);    /**< Recompute and redraw the full window layout. */
void draw_status(void);        /**< Redraw the bottom status bar. */
void status(const char *fmt, ...); /**< Print a formatted message to the status bar. */
void update_all(void);         /**< Flush all pending display updates to screen. */
void draw_mini_info(void);     /**< Redraw the mini information panel. */
void draw_mini_freq(void);     /**< Redraw the mini frequency-response thumbnail. */
void draw_mini_time(void);     /**< Redraw the mini time-domain thumbnail. */
void draw_time(void);          /**< Draw the main time-domain panel. */
void draw_time_info(void);     /**< Draw the time-domain info overlay. */
void show_time_status(int xx, int yy); /**< Show sample value at pixel (@p xx, @p yy). */
void draw_label(Rect *rr, int ox, int oy, int vert, double val0, double val1); /**< Draw axis labels inside @p rr. */
int  mapval(double val, double max, int sy); /**< Map a floating-point value to a pixel row. */
void draw_freq(void);          /**< Draw the main frequency-response panel. */
void draw_freq_info(void);     /**< Draw the frequency-response info overlay. */
void show_freq_status(int xx, int yy); /**< Show dB / phase at pixel (@p xx, @p yy). */
void setup_pager(const char *txt, const char *inf, int typ); /**< Load @p txt into the built-in pager. */
void draw_pager(void);         /**< Render the current pager page. */
void draw_info(void);          /**< Render the filter analysis info view. */
void draw_help(void);          /**< Render the help screen. */
int  calcNow(void);            /**< Return current SDL tick count in milliseconds. */
void progress_init(Progress *pr, int max, const char *txt, int wid); /**< Initialise a @ref Progress bar. */
void progress_update(Progress *pr); /**< Advance and redraw the progress bar if due. */

/** @} */ /* end fiview_display */

#endif
