/**
 * @file display.c
 * @brief All fiview screen layout and drawing functions.
 *
 * Handles the adaptive display layout (arrange_display()), all sub-panel
 * rendering (frequency, time, info, pager, help), axis labels, the status
 * bar, and the console progress indicator.
 *
 * @author  Jim Peters
 * @copyright GPL 2.0
 * @ingroup fiview_display
 */

//
//	Code to setup and draw display
//
//        Copyright (c) 2002-2003 Jim Peters <http://uazu.net/>.
//        Released under the GNU GPL version 2 as published by the
//        Free Software Foundation.  See the file COPYING for details,
//        or visit <http://www.gnu.org/copyleft/gpl.html>.
//

#include "all.h"

//
//	Arrange the display, based on current display size and font
//	size
//

void 
arrange_display() {
   int wid;
   
   // Top-left info display
   wid= disp_sx / 5 / font_sx;
   if (wid < 10) wid= 10;
   d_info.xx= 0;
   d_info.yy= 0;
   d_info.sy= 4 * font_sy;
   d_info.sx= wid * font_sx;

   // Mini freq and time displays
   d_freq.xx= d_info.xx + d_info.sx;
   d_freq.yy= 0;
   d_freq.sx= (disp_sx - d_freq.xx) / 2;
   d_freq.sy= d_info.sy;
   d_time.xx= d_freq.xx + d_freq.sx;
   d_time.yy= 0;
   d_time.sx= disp_sx - d_time.xx;
   d_time.sy= d_info.sy;

   // Main display info line
   d_minf.xx= 0; 
   d_minf.yy= d_info.yy + d_info.sy;
   d_minf.sx= disp_sx;
   d_minf.sy= font_sy;

   // Main display area
   d_main.xx= 0; 
   d_main.yy= d_minf.yy + d_minf.sy;
   d_main.sx= disp_sx;
   d_main.sy= disp_sy - font_sy - d_main.yy;

   if (d_wrk1) free(d_wrk1);
   d_wrk1= ALLOC_ARR(disp_sx, double);
}

//
//	Update a rectangle
//

#define UPDATE(rect) update(rect.xx, rect.yy, rect.sx, rect.sy)

//
//	Draw status line (redraw it, really)
//

static char *status_str= 0;

void 
draw_status() {
   drawtext(font_sy, 0, disp_sy-font_sy, status_str);
   update_force(0, disp_sy-font_sy, disp_sx, font_sy);
}

//
//      Display a status line.  Colours may be selected using
//      characters from 128 onwards (see drawtext()).  There are two
//      types of status lines -- temporary ones and permanent ones.
//      Permanent ones have a '+' at the front of the formatted text
//      (although this is not displayed).  If the status is cleared
//      using status(""), then a permanent message will not go away.
//      However, it will go away if any other message is displayed,
//      including status("+").
//

void
status(const char *fmt, ...) {
   va_list ap;
   char buf[4096], *p;
   static int c_perm= 0;
   int perm= 0;

   if (fmt[0] == '+') { fmt++; perm= 1; }
   if (!fmt[0] && c_perm && !perm) return;
   c_perm= perm;

   va_start(ap, fmt);
   buf[0]= (char)128;   // Select white on black
   vsprintf(buf+1, fmt, ap);
   p= strchr(buf, 0);
   *p++= (char)0x80;    // Restore white on black
   *p++= '\n';          // Blank to end of line
   *p= 0;

   if (status_str) free(status_str);
   status_str= StrDup(buf);
   draw_status();
}

//
//	Update the whole display
//

void 
update_all() {
   update(0, 0, disp_sx, disp_sy);
}

//
//	Draw info region
//

void 
draw_mini_info() {
   int xx= d_info.xx;
   int yy= d_info.yy;
   int sx= d_info.sx;
   int sy= d_info.sy;
   char buf[128];
   int wid= sx/font_sx;

   clear_rect(xx, yy, sx, sy, colour[0]);
   
   sprintf(buf, "\x8e%-*.*s", wid, wid, "Fiview " VERSION);
   drawtext(font_sy, xx, yy, buf);
   sprintf(buf, "Filter %d:", curr->ii);
   drawtext(font_sy, xx, yy += font_sy, buf);
   sprintf(buf, " %-*.*s", wid-1, wid-1, curr->name);
   drawtext(font_sy, xx, yy += font_sy, buf);
   sprintf(buf, " %s", curr->typstr);
   drawtext(font_sy, xx, yy += font_sy, buf);

   UPDATE(d_info);
}

//
//	Draw mini frequency region
//

void 
draw_mini_freq() {
   int xx= d_freq.xx;
   int yy= d_freq.yy;
   int sx= d_freq.sx;
   int sy= d_freq.sy;
   double *arr, *dp, *mmp;
   int o0, o1, a;

   drawtext(font_sy, xx, yy, "\x8c""F");
   drawtext(font_sy, xx, yy+font_sy, "\x8c""R");
   drawtext(font_sy, xx, yy+2*font_sy, "\x8c""E");
   drawtext(font_sy, xx, yy+3*font_sy, "\x8c""Q");
   xx += font_sx;
   sx -= font_sx;
   
   arr= filter_resp_range(curr, 0.0, 0.5, sx, 10);

   // Put in all the min/max values that we might have missed with our
   // low-res scan
   for (a= 0, mmp= curr->minmax; a<curr->n_minmax; a++, mmp+=2) {
      int off= (int)floor(sx * mmp[0] / 0.5);
      dp= &arr[off*4];
      if (mmp[1] > dp[1]) dp[1]= mmp[1];
      if (mmp[1] < dp[0]) dp[0]= mmp[1];
   }

   if (s_main == 'F') {
      o0= (int)floor(s_freq0 / 0.5 * sx);
      o1= (int)floor(s_freq1 / 0.5 * sx);
   } else {
      o0= o1= -1;
   }
   
   for (a= 0, dp= arr; a<sx; a++, xx++, dp += 4) {
      int *col= &colour[(a >= o0 && a <= o1) ? 9 : 6];
      int y0= (sy-1) - (int)floor((sy-1) * (dp[1] / curr->gain));
      int y1= (sy-1) - (int)floor((sy-1) * (dp[0] / curr->gain));
      vline(xx, yy, sy, col[0]);
      vline(xx, yy + y1, sy-y1, col[1]);
      vline(xx, yy + y0, y1-y0+1, col[2]);
   }
      
   free(arr);

   UPDATE(d_freq);
}

//
//	Draw mini time region
//

void 
draw_mini_time() {
   int xx= d_time.xx;
   int yy= d_time.yy;
   int sx= d_time.sx;
   int sy= d_time.sy;
   RunFilter *rr;
   double val;
   double val0, val1;
   int a, b, cnt;
   
   drawtext(font_sy, xx, yy, "\x8c""T");
   drawtext(font_sy, xx, yy+font_sy, "\x8c""I");
   drawtext(font_sy, xx, yy+2*font_sy, "\x8c""M");
   drawtext(font_sy, xx, yy+3*font_sy, "\x8c""E");
   xx += font_sx;
   sx -= font_sx;
   
   val0= curr->impmin;
   val1= curr->impmax;
   if (val0 > -0.2 * val1) val0= -0.2 * val1;
   if (val1 < -0.2 * val0) val1= -0.2 * val0;

   rr= filter_run(curr);
   val= runfilter_step(rr, 1.0);
   cnt= 0;

   if (s_tmzoom > 0) {
      // Multiple samples per pixel
      for (a= sx-1; a>=0; a--) {
	 int *col= &colour[(s_main == 'T' && cnt >= s_tim0 && cnt-s_tmzoom+1 <= s_tim1) ? 9 : 6];
	 double min, max;
	 int y0, y1, y2;
	 min= max= val;
	 
	 for (b= 0; b<s_tmzoom; b++) {
	    if (val > max) max= val;
	    if (val < min) min= val;
	    val= runfilter_step(rr, 0.0);
	    cnt--;
	 }
      
	 y0= (sy-1) - (int)((sy-1) * (0-val0)/(val1-val0));
	 y1= (sy-1) - (int)((sy-1) * (min-val0)/(val1-val0));
	 y2= (sy-1) - (int)((sy-1) * (max-val0)/(val1-val0));
	 
	 vline(xx+a, yy, sy, col[0]);
	 if (y0 < y1) vline(xx+a, yy + y0, y1-y0+1, col[1]);
	 else vline(xx+a, yy + y1, y0-y1+1, col[1]);
	 if (y1 < y2) vline(xx+a, yy + y1, y2-y1+1, col[2]);
	 else vline(xx+a, yy + y2, y1-y2+1, col[2]);
      }
   } else {
      // Multiple pixels per sample
      a= sx-1;
      while (a >= 0) {
	 int *col= &colour[(s_main == 'T' && cnt >= s_tim0 && cnt <= s_tim1) ? 9 : 6];
	 int y0, y1;
	 y0= (sy-1) - (int)((sy-1) * (0-val0)/(val1-val0));
	 y1= (sy-1) - (int)((sy-1) * (val-val0)/(val1-val0));

	 for (b= -s_tmzoom; b>0 && a>=0; b--, a--) {
	    vline(xx+a, yy, sy, col[0]);
	    if (y0 < y1) vline(xx+a, yy + y0, y1-y0+1, col[1]);
	    else vline(xx+a, yy + y1, y0-y1+1, col[1]);
	    vline(xx+a, yy + y1, 1, col[2]);
	 }

	 val= runfilter_step(rr, 0.0);
	 cnt--;
      }	 
   }
   
   runfilter_free(rr);

   UPDATE(d_time);
}

//
//	Check the proportion of time-display points that are within
//	range.
//
   
static double 
check_time_prop(float *dp, int cnt, double val0, double val1) {
   int a;
   int miss= 0;
   for (a= 0; a<cnt; a++, dp+=3) {
      if ((double)dp[0] < val0 || (double)dp[1] < val0 ||
	  (double)dp[0] > val1 || (double)dp[1] > val1)
	 miss++;
   }
   return 1.0 - (miss * 1.0 / cnt);
}

//
//	Draw main time-response display
//

void 
draw_time() {
   int xx= d_main.xx;
   int yy= d_main.yy;
   int sx= d_main.sx;
   int sy= d_main.sy;
   float *buf, *dp;
   double val0, val1;
   int a, b;

   // Check that settings are in range
   if (s_tim0 + s_tim1 > 0) {
      double adj= (s_tim0 + s_tim1) * 0.5;
      s_tim0 -= adj;
      s_tim1 -= adj;
   }

   // Regenerate buffer if anything has changed
   buf= c_tim_buf;
   if (c_tim0 != s_tim0 ||
       c_tim1 != s_tim1 ||
       c_tim_sx != sx  ||
       c_tim_filt != curr
       ) {
      // Regenerate buffer
      RunFilter *rr= filter_run(curr);
      double val;
      double tot= 0;
      int cnt;

      if (c_tim_buf) free(c_tim_buf);
      buf= c_tim_buf= ALLOC_ARR(disp_sx * 3, float);
      c_tim0= s_tim0;
      c_tim1= s_tim1;
      c_tim_sx= sx;
      c_tim_filt= curr;

      // 'val' always holds the output for sample 'cnt'
      val= runfilter_step(rr, 1.0);
      cnt= 0;
      
      dp= buf;
      for (a= 0; a<sx; a++) {
	 int targS= TSLOT_S(a,sx,c_tim0,c_tim1);
	 int targE= TSLOT_E(a,sx,c_tim0,c_tim1);
	 double min, max;
	 
	 if (targE < cnt)
	    min= max= 0;
	 else {
	    while (cnt < targS) {
	       tot += fabs(val);
	       val= runfilter_step(rr, 0.0);
	       cnt++;
	    }
	    min= max= val;
	    while (cnt < targE) {
	       if (val < min) min= val;
	       if (val > max) max= val;
	       tot += fabs(val);
	       val= runfilter_step(rr, 0.0);
	       cnt++;
	    }
	    if (val < min) min= val;
	    if (val > max) max= val;
	 }
	 *dp++= (float)min;
	 *dp++= (float)max;
	 *dp++= (float)tot;
      }
      runfilter_free(rr);
   }

   // Set the viewing range
   val0= curr->impmin;
   val1= curr->impmax;
   if (val0 > -0.2 * val1) val0= -0.2 * val1;
   if (val1 < -0.2 * val0) val1= -0.2 * val0;

   // Adjust the viewing range for ZOOM mode so that at least 95% of
   // the points are included
   if (s_zoom) {
      double mid, wid, min, max;
      for (a= 0; a<20; a++) {
	 double best= 0;
	 int n_100= 0, tot_100= 0;
	 double best_prop= 0;
	 double delta= (val1-val0) / 8;
	 for (b= 0; b<=4; b++) {
	    double prop= check_time_prop(buf, sx, val0 + delta * b, val1 - delta * (4-b));
	    if (prop > best_prop) { best_prop= prop; best= b; }
	    if (prop == 1.0) { n_100++; tot_100 += b; }
	 }
	 if (best_prop < 0.95) break;
	 if (n_100) best= tot_100 * 1.0 / n_100;
	 val0= val0 + delta * best;
	 val1= val1 - delta * (4-best);
      }
      // Now we've cut out the peaks, expand range a little
      mid= (val0+val1) * 0.5;
      wid= (val1-val0) * 0.5;
      val0= mid - wid * 2;
      val1= mid + wid * 2;

      // Now find all the min/max within this range
      min= max= mid;
      for (a= 0, dp= buf; a<sx; a++, dp += 3) {
	 if ((double)dp[0] >= val0 && (double)dp[0] < min) min= (double)dp[0];
	 if ((double)dp[1] <= val1 && (double)dp[1] > max) max= (double)dp[1];
      }
      val0= min;
      val1= max;
   }

   // Draw it
   clear_rect(xx, yy, sx, sy, colour[0]);
   dp= buf;
   for (a= sx-1; a>=0; a--, dp+=3) {
      int y0, y1, y2;

      y0= (sy-1) - (int)((sy-1) * (0-val0)/(val1-val0));
      y1= (sy-1) - (int)((sy-1) * ((double)dp[0]-val0)/(val1-val0));
      y2= (sy-1) - (int)((sy-1) * ((double)dp[1]-val0)/(val1-val0));
      
      if (y0 < y1) vline(xx+a, yy + y0, y1-y0+1, colour[7]);
      else vline(xx+a, yy + y1, y0-y1+1, colour[7]);
      if (y1 < y2) vline(xx+a, yy + y1, y2-y1+1, colour[8]);
      else vline(xx+a, yy + y2, y1-y2+1, colour[8]);
   }

   draw_time_info();

   UPDATE(d_main);
}

//
//	Draw the time-display info line
//

void 
draw_time_info() {
   char buf[128];
   int sx= d_main.sx;
   if (s_rate == 0) 
      sprintf(buf, 
	      "\x82TIME RESPONSE: zoom/\x84%g\x82, time \x84%d\x82 to \x84%d\x82, "
	      "response \x84%g\x82 to \x84%g\x82\n",
	      (s_tim1-s_tim0)/sx, 
	      TSLOT_E(sx-1,sx,s_tim0,s_tim1),
	      TSLOT_S(0,sx,s_tim0,s_tim1),
	      curr->impmin, curr->impmax);
   else 
      sprintf(buf, 
	      "\x82TIME RESPONSE: zoom/\x84%g\x82, time \x84%g\x82s to \x84%g\x82s, "
	      "response \x84%g\x82 to \x84%g\x82\n",
	      (s_tim1-s_tim0)/d_main.sx, 
	      TSLOT_E(sx-1,sx,s_tim0,s_tim1) * 1.0 / s_rate,
	      TSLOT_S(0,sx,s_tim0,s_tim1) * 1.0 / s_rate,
	      curr->impmin, curr->impmax);
   
   drawtext(font_sy, d_minf.xx, d_minf.yy, buf);
   UPDATE(d_minf);
}

//
//	Show the status for a particular position in the time-display
//

void 
show_time_status(int xx, int yy) {
   (void)yy;
   int off= d_main.sx-1-xx;
   int cnt= TSLOT_E(off, d_main.sx, s_tim0, s_tim1);
   float *dp= c_tim_buf + 3 * off;

   if (s_rate) 
      status("\x8e" "CURSOR:\x80 sample \x84%d\x80, time \x84%gs\x80, "
	     "values \x84%g to %g\x80, \x84%g%%\x80 complete",
	     cnt, cnt / s_rate, (double)dp[0], (double)dp[1], 100.0 * (double)dp[2] / curr->tot100);
   else
      status("\x8e" "CURSOR:\x80 sample \x84%d\x80, "
	     "values \x84%g to %g\x80, \x84%g%%\x80 complete",
	     cnt, (double)dp[0], (double)dp[1], 100.0 * (double)dp[2] / curr->tot100);
}

//
//	Draw phase pixel
//
	 
static inline void 
phase_pixel(int xx, int yy) {
   int val= get_point(xx, yy);
   
   if (val == colour[6]) val= colour[16];
   else if (val == colour[30]) val= colour[31];
   else if (val == colour[7]) val= colour[17];
   else val= colour[18];

   plot(xx, yy, val);
}

//
//	Draw a numerical label, using scratch to maintain a list of
//	used rectangles on the screen from one drawing operation to
//	the next.
//

void 
draw_label(Rect *rr, int ox, int oy, int vert, double val0, double val1) {
   char buf[32];
   int txx, tyy, tsx, tsy;	// Text region 
   int scr_save;		// Place to rollback scr_len to
   char *flag;			// Flag array
   Rect *rp, *rpend;
   int a;

   { // Trim off value beyond accuracy represented in range before displaying
      double val, err;
      val= (val0+val1)*0.5;
      err= fabs((val0-val1)*0.5);
      if (err > fabs(val) * 1e-10) {
	 err= exp(floor(log(err) / log(10)) * log(10));
	 val= err * floor(0.5 + (val / err));
      }
      sprintf(buf, "%.6g", val);
   }
   
   // Okay, 'buf' contains the label we want to print
   tsx= 8 * (int)strlen(buf);
   tsy= 16;
   txx= ox; 
   tyy= oy;
   if (txx < 0) txx= 0; 
   if (txx >= rr->sx) txx= rr->sx-1;
   if (tyy < 0) tyy= 0; 
   if (tyy >= rr->sy) tyy= rr->sy-1;
   scr_save= scr_len;
   flag= (char*)scr_inc(rr->sy > rr->sx ? rr->sy : rr->sx);
   rp= (Rect*)scratch;
   rpend= (Rect*)(scratch + scr_save);

   if (!vert) {
      // Search horizontally
      if (tyy + 16 > rr->sy) tyy -= 15;
      for (; rp < rpend; rp++) {
	 int r0, r1;
	 if (rp->yy + rp->sy <= tyy) continue;
	 if (rp->yy >= tyy + tsy) continue;
	 r0= rp->xx; r1= rp->xx + rp->sx;
	 r0 -= tsx+7; if (r0 < 0) r0= 0;
	 r1 += 8; if (r1 > rr->sx) r1= rr->sx;
	 memset(flag + r0, 1, (size_t)(r1-r0));
      }
      if (flag[txx]) 
	 for (a= txx+1; a <= rr->sx-tsx; a++)
	    if (!flag[a]) { txx= a; break; }
      if (flag[txx])
	 for (a= txx-1; a>=0; a--)
	    if (!flag[a]) { txx= a; break; }
      // If flag[txx] still not clear, just put it here anyway
      if (txx > rr->sx - tsx) txx= 0;	// Sanity check
   } else {
      // Search vertically
      if (txx + tsx > rr->sx) txx -= tsx-1;
      if (txx < 0) return;
      for (; rp < rpend; rp++) {
	 int r0, r1;
	 if (rp->xx + rp->sx <= txx-8) continue;
	 if (rp->xx >= txx + tsx + 8) continue;
	 r0= rp->yy; r1= rp->yy + rp->sy;
	 r0 -= 15; if (r0 < 0) r0= 0;
	 if (r1 > rr->sy) r1= rr->sy;
	 memset(flag + r0, 1, (size_t)(r1-r0));
      }
      if (flag[tyy]) 
	 for (a= tyy+1; a <= rr->sy-16; a++)
	    if (!flag[a]) { tyy= a; break; }
      if (flag[tyy])
	 for (a= tyy-1; a>=0; a--)
	    if (!flag[a]) { tyy= a; break; }
      // If flag[tyy] still not clear, just put it here anyway
      if (tyy > rr->sy - 16) tyy= 0;	// Sanity check
   }

   alpha_rect(rr->xx + txx, rr->yy + tyy, tsx, tsy, colour[23], 50);
   drawtext(16, rr->xx + txx, rr->yy + tyy, buf);

   scr_len= scr_save;
   rp= (Rect*)scr_inc(sizeof(*rp));
   rp->xx= txx;
   rp->yy= tyy;
   rp->sx= tsx;
   rp->sy= tsy;
}
   

//
//	Draw freq-response display
//

#define GAIN_FACT 1.02		// Leave a little headroom in display

inline int 
mapval(double val, double max, int sy) {
   if (s_logsc) 
      return (sy-1) - (int)((sy-1) * ((log(val/max)/M_LN10 + s_logsc) / 
				      (s_logsc * GAIN_FACT)));
   else 
      return (sy-1) - (int)((sy-1) * val/(max*GAIN_FACT));
}

void 
draw_freq() {
   int xx= d_main.xx;
   int yy= d_main.yy;
   int sx= d_main.sx;
   int sy= d_main.sy;
   int a, b;
   double *buf, *dp, *dp2;
   double max= curr->gain;	// Maximum expected
   double mid;

   // Adjust settings to make sure they are in range
   mid= (s_freq0+s_freq1)*0.5;
   if (mid < 0.0) {
      s_freq0 -= mid;
      s_freq1 -= mid;
   } else if (mid > 0.5) {
      s_freq0 -= mid-0.5;
      s_freq1 -= mid-0.5;
   }
   // Maximum zoomed-out
   if (s_freq1-s_freq0 > 1.0) {
      s_freq0= -0.25;
      s_freq1= 0.75;
   }

   // Update cache if anything has changed
   buf= c_freq_buf;
   if (c_freq0 != s_freq0 ||
       c_freq1 != s_freq1 ||
       c_freq_sx != sx  ||
       c_freq_filt != curr ||
       c_ftmod != s_ftmod ||
       (c_ftmod && c_ftarg != s_ftarg)
       ) {
      // Regenerate buffer
      c_freq0= s_freq0;
      c_freq1= s_freq1;
      c_freq_sx= sx;
      c_freq_filt= curr;
      c_ftmod= s_ftmod;
      c_ftarg= s_ftarg;
      if (c_freq_buf) free(c_freq_buf);
      buf= c_freq_buf= filter_resp_range(curr, c_freq0, c_freq1, sx, 10);
      if (c_ftbuf) { free(c_ftbuf); c_ftbuf= 0; }
      if (c_ftmod) c_ftbuf= do_filter_test(curr, c_ftmod, c_ftarg, c_freq0, c_freq1, sx);
   }

   // Handle ZOOM/EXPAND mode
   if (s_zoom) {
      if (s_zoom > 0) max= 0;
      for (a= 0, dp= buf, dp2= c_ftbuf; a<sx; a++, dp += 4) {
	 if (dp[0] > max)
	    max= dp[0];
	 if (dp[1] > max)
	    max= dp[1];
	 if (dp2) {
	    if (dp2[0] > max)
	       max= dp2[0];
	    dp2++;
	 }
      }
   }

   // Draw it
   clear_rect(xx, yy, sx, sy, colour[6]);
   if (s_logsc) {	// Grey stripes for log view
      double val= 1.0;
      for (a= 1; a<s_logsc; a += 2) {
	 int y0= mapval(1.0, val*=10, sy);
	 int y1= mapval(1.0, val*=10, sy);
	 if (y1 >= sy) y1= sy;
	 clear_rect(xx, yy+y0, sx, y1-y0, colour[30]);
      }
   }

   // Overlay: draw all non-current filters before the current one
   if (s_overlay) {
      Filter *ff;
      for (ff= filters; ff; ff= ff->nxt) {
	 double *obuf, *odp;
	 if (ff == curr) continue;
	 obuf= filter_resp_range(ff, c_freq0, c_freq1, sx, 10);
	 for (a= 0, odp= obuf; a<sx; a++, odp += 4) {
	    int oy= mapval((odp[0]+odp[1])*0.5, max, sy);
	    if (oy >= 0 && oy < sy) vline(xx+a, yy+oy, 1, colour[10]);
	 }
	 free(obuf);
      }
   }

   for (a= 0, dp= buf; a<sx; a++, dp += 4) {
      int y0, y1;
      int p0, p1;

      y0= mapval(dp[1], max, sy);
      y1= mapval(dp[0], max, sy);

      if (y1 < 0) {		// Off top
	 vline(xx+a, yy+0, sy, colour[7]);
      } else if (y0 >= sy)	// Off bottom
	 ;
      else {			// Somewhere in middle
	 if (y0 < 0) y0= 0;
	 if (y1 >= sy) y1= sy-1;
	 vline(xx+a, yy+y1, sy-y1, colour[7]);
	 vline(xx+a, yy+y0, y1-y0+1, colour[8]);
      }

      // Phases
      p0= 32767 & (int)(dp[2] * 65536);
      p1= 32767 & (int)(dp[3] * 65536);
      p0= (p0*sy) >> 16;
      p1= (p1*sy) >> 16;

      if (p1 < p0) {
	 for (b= 0; b<=p1; b++) 
	    phase_pixel(xx+a, yy+sy-1-b);
	 p1 += sy/2;
      }

      for (b= p0; b<=p1; b++) {
	 int py0= yy+sy-1-b;
	 int py1= yy+sy/2-1-b;
	 phase_pixel(xx+a, py0);
	 if (py1 >= yy) phase_pixel(xx+a, py1);
      }
   }

   // Testing lines
   if (c_ftbuf) for (a= 0, dp= c_ftbuf; a<sx; a++, dp++) {
      int y0, y1;
      if (isnan(dp[0])) continue;
      y0= y1= mapval(dp[0], max, sy);
      y0--; y1++;

      if (y1 >= 0 && y0 < sy) {
	 if (y0 < 0) y0= 0;
	 if (y1 >= sy) y1= sy-1;
	 vline(xx+a, yy+y0, y1-y0+1, colour[29]);
	 if (a > 0) vline(xx+a-1, yy+y0, y1-y0+1, colour[29]);
	 if (a < sx-1) vline(xx+a+1, yy+y0, y1-y0+1, colour[29]);
      }
   }

   draw_freq_info();

   // Draw the min/max/-3dB/-6dB points 
   if (s_minmax) {
      double *mm, *mp, *mmend;
      double *prev;
      int n_mm, bx;
      scr_zap();
      
      // Scan for minima
      for (a= 0, prev= 0, dp= buf; a<sx; prev= dp, a++, dp += 4) {
	 double freq0= (a + 0.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 double freq1;
	 while (a < sx-1 && dp[0] == dp[4]) {
	    dp += 4; a++; 
	    freq1= (a + 1.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 }
	 freq1= (a + 1.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 if (!prev || a >= sx-1) continue;

	 if (prev[0] > dp[0] && dp[4] > dp[0]) {
	    scr_wrD(freq0);
	    scr_wrD(freq1);
	    scr_wrD(dp[0]);
	    scr_wrD(dp[0]);
	 }
      }

      // Scan for maxima
      for (a= 0, prev= 0, dp= buf; a<sx; prev= dp, a++, dp += 4) {
	 double freq0= (a + 0.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 double freq1;
	 while (a < sx-1 && dp[1] == dp[5]) {
	    dp += 4; a++; 
	    freq1= (a + 1.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 }
	 freq1= (a + 1.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 if (!prev || a >= sx-1) continue;

	 if (prev[1] < dp[1] && dp[5] < dp[1]) {
	    scr_wrD(freq0);
	    scr_wrD(freq1);
	    scr_wrD(dp[1]);
	    scr_wrD(dp[1]);
	 }
      }

      // Scan for -3dB points
      for (a= 0, dp= buf; a<sx; a++, dp += 4) {
	 double freq0= (a + 0.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 double freq1= (a + 1.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 double pp= curr->gain100 * M301DB;

	 if ((dp[0] < pp) != (dp[1] < pp)) {
	    scr_wrD(freq0);
	    scr_wrD(freq1);
	    scr_wrD(pp);
	    scr_wrD(pp);
	 }
      }

      // Scan for -6dB points
      for (a= 0, dp= buf; a<sx; a++, dp += 4) {
	 double freq0= (a + 0.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 double freq1= (a + 1.0) / sx * (s_freq1-s_freq0) + s_freq0;
	 double pp= curr->gain100 * M602DB;

	 if ((dp[0] < pp) != (dp[1] < pp)) {
	    scr_wrD(freq0);
	    scr_wrD(freq1);
	    scr_wrD(pp);
	    scr_wrD(pp);
	 }
      }

      // Dup it
      n_mm= scr_len / (int)sizeof(double) / 4;
      mm= (double*)scr_dup();
      mmend= &mm[n_mm * 4];

      // Okay, we have a list of min/max/-3dB/-6dB points.  Now we're
      // going to draw them on the display and label them.  The
      // labelling code will use scratch to avoid writing overlapping
      // labels.
      scr_zap();
      for (bx= 0; bx<3; bx++) {
	 for (mp= mm; mp < mmend; mp += 4) {
	    int ox= (int)floor(sx * (((mp[0] + mp[1]) * 0.5 - s_freq0) / (s_freq1 - s_freq0)));
	    int oy= mapval((mp[2]+mp[3]) * 0.5, max, sy);

	    if (oy >= 0 && oy < sy) switch (bx) {
	     case 0:
		for (a= 0; a<sy; a += 8)
		   vline(xx + ox, yy + a, (sy-a) < 4 ? (sy-a) : 4, colour[1]);
		for (a= 0; a<sx; a += 8)
		   hline(xx + a, yy + oy, (sx-a) < 4 ? (sx-a) : 4, colour[1]);
		break;
	     case 1:
		draw_label(&d_main, ox, oy, 0, mp[2], mp[3]);
		break;
	     case 2:
		draw_label(&d_main, ox, sy*4/5, 1,
			   mp[0] * (s_rate ? s_rate : 1),
			   mp[1] * (s_rate ? s_rate : 1));
	    }
	 }
      }
      free(mm);
   }

   UPDATE(d_main);
}


//
//	Draw freq-display info line
//

void 
draw_freq_info() {
   char buf[256];
   char *p= buf;
   
   p += sprintf(p, 
		"\x82""FREQ RESPONSE: zoom*\x84%g\x82, "
		"freq \x84%g%s\x82 to \x84%g%s\x82, "
		"response \x84%g\x82 to \x84%g\x82",
		0.5 / (s_freq1-s_freq0),
		s_freq0 * (s_rate ? s_rate : 1.0), s_rate ? "Hz" : "",
		s_freq1 * (s_rate ? s_rate : 1.0), s_rate ? "Hz" : "",
		0.0, curr->gain);

   if (s_ftmod) switch (s_ftmod) {
    case 'w':
       p += sprintf(p, ", testing \x84%g\x82-period wavelets", s_ftarg); 
       break;
    case 's':
       p += sprintf(p, ", testing \x84%g\x82Hz/sec sweeps", 
		    s_ftarg * (s_rate ? s_rate*s_rate : 1.0)); 
       break;
    default:	
       error("Unknown testing mode: %c", s_ftmod);
   }
   p += sprintf(p, "\n");

   drawtext(font_sy, d_minf.xx, d_minf.yy, buf);
   UPDATE(d_minf);
}

//
//	Show status line for a particular point in the frequency display
//

void 
show_freq_status(int xx, int yy) {
   (void)yy;
   double freq= (xx + 0.5) / d_main.sx * (s_freq1-s_freq0) + s_freq0;
   double *dp= c_freq_buf + 4 * xx;

   if (c_freq_sx != d_main.sx) return;

   status("\x8e" "CURSOR:\x80 freq \x84%g%s\x80, response \x84%g to %g\x80 "
	  "(\x84%.2f%% to %.2f%%\x80)",
	  freq * (s_rate ? s_rate : 1.0), s_rate ? "Hz" : "", 
	  dp[0], dp[1], 
	  100.0 * dp[0]/curr->gain100, 
	  100.0 * dp[1]/curr->gain100);
}

//
//	Setup pager display
//

void
setup_pager(const char *txt, const char *inf, int typ) {
   char *p;

   s_pager_typ= typ;
   s_pager_lin= 0;
   if (s_pager_txt) free(s_pager_txt);
   s_pager_txt= StrDup(txt);
   if (s_pager_inf) free(s_pager_inf);
   s_pager_inf= StrDup(inf);

   s_pager_cnt= 0;
   for (p= s_pager_txt; *p; p++) 
      if (*p == '\n') 
	 s_pager_cnt++;
   if (p[-1] != '\n') 
      s_pager_cnt++;
   
   draw_pager();
}

//
//	Draw the main pager display
//

void 
draw_pager() {
   int xx= d_main.xx;
   int yy= d_main.yy;
   int sx= d_main.sx;
   int sy= d_main.sy;
   char *p;
   int a;
   int lin, r0, r1;
   int col= (s_pager_typ == 'I') ? 0x93 : 0x95;
   char buf[128];

   drawtext(font_sy, d_minf.xx, d_minf.yy, s_pager_inf);
   UPDATE(d_minf);
   
   // Find the line we are starting on
   p= s_pager_txt;
   for (a= 0; a<s_pager_lin && *p; a++) 
      while (*p && *p++ != '\n') ;
   
   // Draw the text
   clear_rect(xx, yy, sx, sy, colour[col-128]);
   lin= sy / font_sy - 1;
   for (a= 0; a<lin; a++, yy += font_sy) {
      char *q;
      int len;
      if (!*p) continue;
      for (q= p; *q && *q != '\n'; q++) ;
      len= (int)(q-p);
      if ((size_t)(len+3) > sizeof(buf)) len= (int)sizeof(buf)-3;
      buf[0]= (char)col; buf[1]= ' '; memcpy(buf+2, p, (size_t)len);
      buf[len+2]= 0;
      drawtext(font_sy, xx, yy, buf);
      p= q+1;
   }
   
   // Draw the final MORE line
   r0= s_pager_lin + 1;
   r1= s_pager_lin + lin;
   s_pager_at_end= r1 >= s_pager_cnt;
   if (s_pager_at_end)
      sprintf(buf, "\x84 END %d-%d/%d\n", r0, s_pager_cnt, s_pager_cnt);
   else 
      sprintf(buf, "\x84 MORE %d-%d/%d\n", r0, r1, s_pager_cnt);
   drawtext(font_sy, xx, yy, buf);

   UPDATE(d_main);
}

//
//	Draw info main display
//

void 
draw_info() {
   // Init pager if required
   if (s_pager_typ != 'I' ||
       c_info_filt != curr) {
      setup_pager(curr->dump, "\x82INFORMATION DUMP (also written to 'fiview.log')\n", 'I');
      c_info_filt= curr;
   }

   draw_pager();
}

//
//	Draw help main display
//

void 
draw_help() {
   // Init pager if required
   if (s_pager_typ != 'H') {
      char buf[128];
      sprintf(buf, "\x82HELP TEXT\n");
      setup_pager(helptext, buf, 'H');
   }

   draw_pager();
}

//
//	Get the current time in milliseconds (without any specific
//	reference for '0' time)
//

#ifdef UNIX_TIME
inline int  
calcNow() {
   struct timeval tv;
   if (0 != gettimeofday(&tv, 0)) error("Can't get current time");
   return (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
#endif

#ifdef WIN_TIME
inline int  
calcNow() {
   SYSTEMTIME st;
   GetLocalTime(&st);
   return st.wMilliseconds + 1000*st.wSecond + 60000*st.wMinute + 3600000*st.wHour;
}
#endif
   

//
//	Initialise a progress bar.  This is designed to put the
//	minimum overhead on something that runs fast (just two time
//	calls), but to step in when something is running slowly and
//	give the user some indication of what is going on.  Assuming
//	the 'max' value has been specified correctly, the status
//	progress bar should disappear correctly as well.
//

void
progress_init(Progress *pr, int max, const char *txt, int wid) {
   pr->cnt= 0;
   pr->upd= (max < 40) ? (max*2) : max / 20;	// Never, or at 5%
   pr->max= max;
   pr->wid= wid;
   pr->tim0= calcNow();
   pr->step= 0;
   pr->txt= txt;
   pr->force= 0;
}

//
//	Update the progress bar.  This should be called as follows in
//	the main loop:
//
//	  if (++pr.cnt >= pr.upd) progress_update(&pr);
//

void 
progress_update(Progress *pr) {
   int pos;
   char buf[128];
   if (!pr->step) {
      int now= calcNow();
      double eta= (now-pr->tim0) * pr->max / pr->cnt;
      if (pr->force) eta= pr->wid * 250;
      if (eta < 250) { // Less than quarter of a second, don't bother with bar
	 pr->upd= pr->max * 2;
	 return;
      }
      pr->step= pr->max / pr->wid;
      //pr->step= pr->max / (eta / 250);	// For updates 4 times a second
   }
   if (pr->cnt == pr->max) { status(""); return; }
   if (pr->cnt > pr->max) { status("%s MAX EXCEEDED: INTERNAL ERROR"); return; }
   pr->upd += pr->step;
   if (pr->upd > pr->max) pr->upd= pr->max;
   pos= pr->cnt * pr->wid / pr->max;

   if ((size_t)pr->wid > sizeof(buf)-1U)
      error("progress_update internal error -- buffer too small");
   memset(buf, '-', (size_t)pr->wid);
   memset(buf, '#', (size_t)pos);
   buf[pr->wid]= 0;
   status("%s%s", pr->txt, buf);
}


// END //

