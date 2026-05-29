/**
 * @file filter.c
 * @brief Filter loading, analysis, gain/crossing computation, and C-code generation.
 *
 * Implements all @ref fiview_filter functions: loading from file or string,
 * impulse-response analysis (settling time, gain), frequency response metrics
 * (−3/−6 dB crossings, min/max peaks), wavelet/slide frequency-test, and
 * C boilerplate code generation for hard-coding a designed filter.
 *
 * @author  Jim Peters
 * @copyright GPL 2.0
 * @ingroup fiview_filter
 */

//
//	Filter loading/storing/processing
//
//        Copyright (c) 2002-2003 Jim Peters <http://uazu.net/>.
//        Released under the GNU GPL version 2 as published by the
//        Free Software Foundation.  See the file COPYING for details,
//        or visit <http://www.gnu.org/copyleft/gpl.html>.
//

//
//	Typical usage:
//	-------------
//
//	Filter *ff;
//	int cnt;
//	double resp;
//	double phase;
//	RunFilter *rr;
//
//	n_filt= filter_load_file(fnam);
//
//	  Loads a filter spec-file and adds filters contained in it to
//	  the list in memory.  Drops out with error message if there
//	  is a problem.  Also generates a load of analysis on the
//	  filter at this point.  Returns the total number of filters
//	  loaded.
//
//	n_filt= filter_load_immed(txt);
//
//	  Loads filters from the given string.  Otherwise identical to
//	  filter_load().
//
//	ff= filter_find(index);
//
//	  Look up a filter by index number (counting from 1),
//	  returning 0 if not found.
//
//	cnt= filter_complete(ff, proportion, max);
//
//	  Looks for the point where the filter calculations are
//	  'proportion' complete (usually 0.90 or 0.999 or whatever).
//	  The number of samples required to reach that point is
//	  returned, or 'max' if more than 'max' samples are required.
//
//	resp= filter_response(ff, freq, &phase);
//
//	  Finds the response of the filter at the given frequency.
//	  Frequency is measured as a proportion of the sampling
//	  frequency, so the useful range is 0 to 0.5.  The phase of
//	  the response is returned in 'phase', if a non-zero pointer
//	  is passed.  The phase is represented on the scale 0-1 for
//	  the range 0 to 2*PI.
//
//	rr= filter_run(ff);
//	
//	  Sets up a RunFilter from the given filter, ready to run.
//
//	out= runfilter_step(rr, in);
//
//	  Process one sample through the given filter
//
//	runfilter_free(rr);
//
//	  Delete the RunFilter once it has been used.
//

#include "all.h"

Filter *filters= 0;

const char *filt_typename[]= {
   "low-pass",
   "high-pass",
   "band-pass",
   "band-stop",
   "all-pass"
};


//
//	Load a file as a single string into memory
//

static char *
load_file(char *fnam) {
   int len;
   char *rv;
   FILE *in;

   if (!(in= fopen(fnam, "rb")))	// "b" binary for Windows
      error("Can't open file: %s", fnam);

   if (0 != fseek(in, 0, SEEK_END) ||
       0 > (len= (int)ftell(in)))
      error("Can't find file length: %s", fnam);

   fseek(in, 0, SEEK_SET);

   rv= (char*)Alloc((size_t)len + 1U);
   if (1 != fread(rv, (size_t)len, 1, in) ||
       0 != fclose(in))
      error("Read error on file: %s", fnam);

   rv[len]= 0;
   return rv;
}

//
//	Skip white space (including comments)
//

static void 
skipWS(char **pp) {
   char *p= *pp;

   while (*p) {
      if (isspace(*p)) { p++; continue; }
      if (*p == '#') {
	 while (*p && *p != '\n') p++;
	 continue;
      }
      break;
   }
   *pp= p;
}

//
//	Grab a word from the input into the given buffer.  Returns 0:
//	end of file or error, else 1: success.  Error is indicated
//	when the word doesn't fit in the buffer.
//

static int 
grabWord(char **pp, char *buf, int buflen) {
   char *p, *q;
   int len; 
   
   skipWS(pp);
   p= *pp;
   if (!*p) return 0;

   q= p;
   if (*q == ',' || *q == ';') {
      q++;
   } else {
      while (*q && *q != '#' && !isspace(*q) && (*q != ',' && *q != ';')) q++;
   }
   len= (int)(q-p);
   if (len >= buflen) return 0;

   memcpy(buf, p, (size_t)len);
   buf[len]= 0;
   
   *pp= q;
   return 1;
}

//
//	Grab a floating point number.  Returns: 0: error (invalid
//	format or EOF), else 1: success.
//   
   
static int  
grabFP(char **pp, double *rv) {
   char buf[80];
   char dmy;
   
   if (!grabWord(pp, buf, sizeof(buf)))
      return 0;

   if (1 != sscanf(buf, "%lf %c", rv, &dmy))
      return 0;

   return 1;
}


//
//	Load just one filter out of the given string
//

static void
filter_load_part(char **txtp, char *txt, const char *fnam, int *cntp) {
   Filter *ff;
   char *p, *q, *prev;
   char buf[80];
   int typ, cnt, a, first, lin;
   double tmp;
   SubFilt *sf, **sf_prvp;

   // Skip ';' or ','
   while (1) {
      p= *txtp;
      if (!grabWord(&p, buf, sizeof(buf))) {
	 if (*p) goto syntax;
	 *txtp= p;
	 return;
      }
      if (0 != strcmp(buf, ",") &&
	  0 != strcmp(buf, ";"))
	 break;
      *txtp= p;
   }

   // We have something to read, so create a new filter
   ff= ALLOC(Filter);
   ff->nxt= filters;
   ff->ii= filters ? filters->ii + 1 : 1;
   scr_zap_pr("%s#%d", fnam, (*cntp)++);
   ff->name= scr_dup();
   ff->filt= 0;
   filters= ff;

   // Interpret into a list of sub-filters
   sf_prvp= &ff->filt;
   p= *txtp;
   first= 1;
   while (1) {
      prev= p;
      if (!grabWord(&p, buf, sizeof(buf))) {
	 if (*p) goto syntax;
	 break;
      }
      if (0 == strcmp(buf, ",") ||
	  0 == strcmp(buf, ";")) break;

      typ= 0;
      if (0 == strcmp(buf, "x")) typ= 'x';
      if (0 == strcmp(buf, "/")) typ= '/';
      if (!typ && first) { p= prev; typ= 'x'; }
      if (!typ) { p= prev; goto syntax; }
      first= 0;

      // Look to see if we have a predefined filter
      prev= p;
      if (!grabWord(&p, buf, sizeof(buf))) {
	 p= prev; goto syntax;
      }
      if (isalpha(buf[0]) && isalpha(buf[1])) {
	 // Okay, we have a predefined filter to handle
	 char *desc;
	 FidFilter *filt= fid_design(buf, s_rate ? s_rate : 1.0, a_f0, a_f1, a_adj, &desc);
	 FidFilter *ffscan;

	 if (typ != 'x') error("You can't use '/' with a predefined filter");

	 // Create subfilter
	 sf= ALLOC(SubFilt);
	 *sf_prvp= sf;
	 sf_prvp= &sf->nxt;
	 sf->nxt= 0;
	 sf->filt= filt;

	 // Find the length
	 for (ffscan= filt; ffscan->typ; ffscan= FFNEXT(ffscan)) ;
	 sf->filt_len= (int)(((char*)ffscan) - ((char*)filt));

	 // Add description 
	 fid_rewrite_spec(buf, a_f0, a_f1, a_adj, &sf->desc0, 
			  &sf->minsp, &sf->minf0, &sf->minf1, &sf->minadj);
	 sf->desc1= desc;

	 continue;
      }
      p= prev;
	 
      // Scan to see how many FP values we have following
      prev= p;
      for (cnt= 0; grabFP(&p, &tmp); ) cnt++;
      if (cnt == 0) { p= prev; goto syntax; }
      
      // Create sub-filter
      sf= ALLOC(SubFilt); 
      *sf_prvp= sf;
      sf_prvp= &sf->nxt;
      sf->nxt= 0;
      sf->filt= FFALLOC(1, cnt);
      sf->filt->typ= (typ == 'x') ? 'F' : 'I';
      sf->filt->cbm= ~0;
      sf->filt->len= cnt;
      sf->filt_len= (int)FFSIZE(cnt);
      
      // Pick up all the FP values
      p= prev;
      for (a= 0; a<cnt; a++)
	 grabFP(&p, &sf->filt->val[a]);
   }
   *txtp= p;
   
   if (!ff->filt)
      error("No filters specified in the file: %s", fnam);

   warn("  Checking impulse response");
   filter_setup_run(ff);
   filter_setup_cnt(ff);
   warn("  Checking frequency response");
   filter_setup_gain(ff);
   ff->dump= filter_dump(ff);
   
   return;

 syntax:
   lin= 1;
   for (q= txt; q<p; q++) {
      if (*q == '\n') {
	 prev= q+1;
	 lin++;
      }
   }
   while (*q && *q != '\n') q++;
   *q= 0;

   if (0 == strcmp(fnam, "-i"))
      error("Syntax error in immediate filter-spec at line %d:\n  %s",
	    lin, prev);
   else 
      error("Syntax error in spec-file %s at line %d:\n  %s",
	    fnam, lin, prev);

   // Doesn't reach this point
   return;
}

//
//	Load the filters in the given string into memory
//

static int
filter_load(char *txt, const char *fnam, int *cntp) {
   char *p= txt;
   while (*p)
      filter_load_part(&p, txt, fnam, cntp);
   return filters ? filters->ii : 0;
}

//
//	Load the filters in the filter-spec file into memory
//

int 
filter_load_file(char *fnam) {
   char *txt= load_file(fnam);
   int cnt= 1;
   warn("Loading %s ...", fnam);
   return filter_load(txt, fnam, &cnt);
}

//
//	Load the filters in the filter-spec file into memory
//

int 
filter_load_immed(char *txt) {
   int cnt= 1;
   warn("Loading immediate filter-spec ...");
   return filter_load(txt, "-i ", &cnt);
}

//
//	Find the filter with the given index
//

Filter *
filter_find(int index) {
   Filter *rv= filters;

   while (rv && rv->ii != index) rv= rv->nxt;
   return rv;
}

//
//	Do a convolution of parameters in place
//

static inline int 
convolve(double *dst, int n_dst, double *src, int n_src) {
   int len= n_dst + n_src - 1;
   int a, b;
   
   //   printf("Convolve\n");
   //   for (a= 0; a<n_src; a++) printf(" %g", src[a]);
   //   printf("\n");
   //   for (a= 0; a<n_dst; a++) printf(" %g", dst[a]);
   //   printf("\n");

   for (a= len-1; a>=0; a--) {
      double val= 0;
      for (b= 0; b<n_src; b++)
	 if (a-b >= 0 && a-b < n_dst)
	    val += src[b] * dst[a-b];
      dst[a]= val;
   }

   //   for (a= 0; a<len; a++) printf(" %g", dst[a]);
   //   printf("\n");

   return len;
}

//
//	Setup the run info for the filter
//

void 
filter_setup_run(Filter *filt) {
   SubFilt *sf;
   int len= 0;
   FidFilter *ff;
   char *cp;

   // Merge all the FidFilters into a single FidFilter
   for (sf= filt->filt; sf; sf= sf->nxt) 
      len += sf->filt_len;

   ff= (FidFilter*)Alloc((size_t)len + FFSIZE(0));

   cp= (char*)ff;
   for (sf= filt->filt; sf; sf= sf->nxt) {
      memcpy(cp, sf->filt, (size_t)sf->filt_len);
      cp += sf->filt_len;
   }
   
   // Setup the run-handler
   filt->ff= ff;
   filt->run= fid_run_new(ff, &filt->funcp);
}

//
//	Setup a RunFilter 
//

RunFilter *
filter_run(Filter *ff) {
   RunFilter *rr= ALLOC(RunFilter);
   rr->buf= fid_run_newbuf(ff->run);
   rr->funcp= ff->funcp;
   return rr;
}
 
//
//	Release a RunFilter
//     

void 
runfilter_free(RunFilter *rr) {
   free(rr->buf);
   free(rr);
}

//
//	Process the filter through one step
//

inline double 
runfilter_step(RunFilter *rr, double val) {
   return rr->funcp(rr->buf, val);
}

//
//	Find the sample-time at which a certain proportion of the
//	filter calculations are complete (e.g. 0.999).  Do this by
//	running two impulse responses in parallel, one at 4 times the
//	speed of the other.  When the slower one reaches the given
//	proportion of the faster one, then we consider that we have
//	reached the correct point in the slower one.
//

int 
filter_complete(Filter *ff, double prop, int max) {
   RunFilter *r1, *r2;
   double tot1= 0, tot2= 0;
   int cnt;

   r1= filter_run(ff);
   r2= filter_run(ff);
   
   tot1 += fabs(runfilter_step(r1, 1.0));
   tot2 += fabs(runfilter_step(r2, 1.0));
   tot2 += fabs(runfilter_step(r2, 0.0));
   tot2 += fabs(runfilter_step(r2, 0.0));
   tot2 += fabs(runfilter_step(r2, 0.0));

   for (cnt= 1; cnt < max; cnt++) {
      tot1 += fabs(runfilter_step(r1, 0.0));
      tot2 += fabs(runfilter_step(r2, 0.0));
      tot2 += fabs(runfilter_step(r2, 0.0));
      tot2 += fabs(runfilter_step(r2, 0.0));
      tot2 += fabs(runfilter_step(r2, 0.0));
      
      if (tot1/tot2 >= prop) {
	 runfilter_free(r1);
	 runfilter_free(r2);
	 return cnt;
      }
   }

   runfilter_free(r1);
   runfilter_free(r2);
   return max;
}

//
//	Setup the 90%/etc points in the Filter structure.  
//

void 
filter_setup_cnt(Filter *ff) {
   int MAX= 1000000;
   RunFilter *r1, *r2;
   double tot1= 0, tot2= 0;
   int cnt;
   double max_resp, resp, impmin, impmax;
   int max_resp_cnt;
   double target;
   double val;

   // Run through to pick up 99.99% point and 100% total (our estimate
   // at least)
   r1= filter_run(ff);
   r2= filter_run(ff);
   
   tot1 += fabs(runfilter_step(r1, 1.0));
   tot2 += fabs(runfilter_step(r2, 1.0));
   tot2 += fabs(runfilter_step(r2, 0.0));
   tot2 += fabs(runfilter_step(r2, 0.0));
   tot2 += fabs(runfilter_step(r2, 0.0));

   for (cnt= 1; cnt < MAX; cnt++) {
      tot1 += fabs(runfilter_step(r1, 0.0));
      tot2 += fabs(runfilter_step(r2, 0.0));
      tot2 += fabs(runfilter_step(r2, 0.0));
      tot2 += fabs(runfilter_step(r2, 0.0));
      tot2 += fabs(runfilter_step(r2, 0.0));
      
      if (tot1/tot2 >= 0.9999)
	 break;
   }

   runfilter_free(r1);
   runfilter_free(r2);

   ff->cnt9999= cnt;
   ff->tot100= tot2;
   MAX= cnt;

   // Run through to pick up remaining points, and impulse maximum
   r1= filter_run(ff);
   val= runfilter_step(r1, 1.0);
   tot1= 0;
   max_resp= 0.0;
   impmin= impmax= 0.0;
   target= 0.50;
   for (cnt= 0; cnt<=MAX; cnt++) {
      if (val < impmin) impmin= val;
      if (val > impmax) impmax= val;
      resp= fabs(val);
      val= runfilter_step(r1, 0.0);
      if (resp > max_resp) { max_resp= resp; max_resp_cnt= cnt; }
      tot1 += resp;
      while (tot1/tot2 >= target) {
	 if (target == 0.50) { ff->cnt50= cnt; target= 0.90; }
	 else if (target == 0.90) { ff->cnt90= cnt; target= 0.95; }
	 else if (target == 0.95) { ff->cnt95= cnt; target= 0.99; }
	 else if (target == 0.99) { ff->cnt99= cnt; target= 0.999; }
	 else { ff->cnt999= cnt; goto done; }
      }
   }
 done:
   runfilter_free(r1);
   ff->cnt_max= max_resp_cnt;
   ff->impmin= impmin;
   ff->impmax= impmax;
}

//
//	Complex multiply: aa *= bb;
//

static inline void 
cmul(double *aa, double *bb) {
   double rr= aa[0] * bb[0] - aa[1] * bb[1];
   double ii= aa[0] * bb[1] + aa[1] * bb[0];
   aa[0]= rr;
   aa[1]= ii;
}

//
//	Complex divide: aa /= bb;
//

static inline void 
cdiv(double *aa, double *bb) {
   double cc[2], fact;
   cc[0]= bb[0];
   cc[1]= -bb[1];
   cmul(aa, cc);
   fact= 1 / (cc[0] * cc[0] + cc[1] * cc[1]);
   aa[0] *= fact;
   aa[1] *= fact;
}

//
//      Complex imaginary exponent: aa= e^i.theta
//

static inline void 
cexpj(double *aa, double theta) {
   aa[0]= cos(theta);
   aa[1]= sin(theta);
}


//
//	Evaluate a complex polynomial given the coefficients.
//	rv[0]+i*rv[1] is the result, in[0]+i*in[1] is the input value.
//	Coefficients are real values.
//

static inline void 
evaluate(double *rv, double *coef, int n_coef, double *in) {
   double pz[2];	// Powers of Z

   // Handle first iteration by hand
   rv[0]= *coef++;
   rv[1]= 0;

   if (--n_coef > 0) {
      // Handle second iteration by hand
      pz[0]= in[0];
      pz[1]= in[1];
      rv[0] += *coef * pz[0];
      rv[1] += *coef * pz[1];
      coef++; n_coef--;

      // Loop for remainder
      while (n_coef > 0) {
	 cmul(pz, in);
	 rv[0] += *coef * pz[0];
	 rv[1] += *coef * pz[1];
	 coef++;
	 n_coef--;
      }
   }
}

//	This is the old version.  I switched to the current method
//	(above) so that the increasing powers of Z were independent
//	from the coefficients, hopefully keeping them more 'pure' in
//	the face of large changes of scale between the coefficients.
//	In brief tests, this gives a slight improvement, hardly
//	noticeable, though.

//static inline void 
//evaluate(double *rv, double *coef, int n_coef, double *in) {
//   // Handle first iteration by hand
//   rv[0]= *coef++;
//   rv[1]= 0;
//   n_coef--;
//   
//   while (n_coef > 0) {
//      cmul(rv, in);
//      rv[0] += *coef++;
//      n_coef--;
//   }
//}

//
//	Find the response phase/magnitude of the filter at the given
//	frequency.  Phase is returned only if 'phase' != 0.  Phase is
//	adjusted to the range 0-1 for 0-2PI
//


double
filter_response(Filter *filt, double freq, double *phase) {
   double top[2], bot[2];
   double theta= freq * 2 * M_PI;
   double zz[2];
   FidFilter *ff= filt->ff;

   //   warn("filter_response %-20.10g", freq);

   top[0]= 1;
   top[1]= 0;
   bot[0]= 1;
   bot[1]= 0;
   zz[0]= cos(theta);
   zz[1]= sin(theta);

   while (ff->len) {
      double resp[2];
      int cnt= ff->len;
      evaluate(resp, ff->val, cnt, zz);
      if (ff->typ == 'I')
         cmul(bot, resp);
      else if (ff->typ == 'F')
         cmul(top, resp);
      else
         error("Unknown filter type %d in filter_response()", ff->typ);
      ff= FFNEXT(ff);
   }

   cdiv(top, bot);
   
   if (phase) {
      double pha= atan2(top[1], top[0]) / (2 * M_PI);
      if (pha < 0) pha += 1.0;
      *phase= pha;
   }

   return hypot(top[1], top[0]);
}


// // Old filter response based on combined filters
// double 
// filter_response(Filter *ff, double freq, double *phase) {
//    double top[2], bot[2];
//    double theta= freq * 2 * M_PI;
//    double zz[2];
//    double mag, pha;
// 
//    zz[0]= cos(theta);
//    zz[1]= sin(theta);
// 
//    evaluate(top, ff->fir, ff->n_fir, zz);
//    evaluate(bot, ff->iir, ff->n_iir, zz);
//    
//    cdiv(top, bot);
//    
//    mag= hypot(top[1], top[0]);
//    pha= atan2(top[1], top[0]) / (2 * M_PI);
//    if (pha < 0) pha += 1.0;
//  
//    if (phase) *phase= pha;
//    return mag;
// }

//
//	Detailed scan as part of filter_resp_range() below.
//

static void 
detailed_resp_scan(Filter *ff, double *dp, double freq0, double freq1, int slots) {
   double delta= (freq1-freq0) / slots;
   int a;

   //warn("detailed_resp_scan %g-%g, %d", freq0, freq1, slots);

   for (a= 1; a<slots; a++) {
      double resp= filter_response(ff, freq0 + delta * a, 0);
      if (resp < dp[0]) dp[0]= resp;
      if (resp > dp[1]) dp[1]= resp;
   }
}

//
//	Generate a list of frequency responses for the given range of
//	frequency values.  Looks for both minimum and maximum values
//	of phase and response for each band.  Takes care to search
//	down the actual accurate minimum/maximum response values if
//	there is a min/max within the band, down to an accuracy of
//	'subslots' per slot.  Returns an allocated array of doubles
//	with values in quads: resp-min, resp-max, pha-min, pha-max.
//

double *
filter_resp_range(Filter *ff, double freq0, double freq1, int slots, int subslots) {
   double delta= (freq1-freq0) / slots;
   double *rv= ALLOC_ARR(slots*4, double);
   double *dp;
   double prev_resp, prev_pha;
   int prev_cmp;
   int a;

   //warn("filter_resp_range(%g,%g,%d,%d);", freq0, freq1, slots, subslots);

   prev_resp= filter_response(ff, freq0, &prev_pha);
   prev_cmp= 0;
   for (dp= rv, a= 0; a<slots; a++, dp += 4) {
      double pha, pha0, pha1;
      double resp= filter_response(ff, freq0 + (a+1) * delta, &pha);
      int cmp= (fabs(resp - prev_resp) < 1e-8 * resp) ? 
	 0 : (resp > prev_resp) ? 1 : -1;
      dp[0]= cmp > 0 ? prev_resp : resp;
      dp[1]= cmp < 0 ? prev_resp : resp;

      // Try to increase accuracy by scanning around maxima/minima
      if (cmp != prev_cmp && cmp && prev_cmp) {
	 if (a > 0) detailed_resp_scan(ff, dp-4, freq0 + (a-1)*delta, freq0 + a*delta, subslots);
	 detailed_resp_scan(ff, dp, freq0 + a*delta, freq0 + (a+1)*delta, subslots);
      }

      // Get phases into correct order, even if the 'lower' appears to
      // be greater due to wrap-around
      pha0= prev_pha; 
      pha1= pha;
      if ((pha0 > pha1 && pha0-pha1 < 0.5) ||
          (pha0 < pha1 && pha1-pha0 > 0.5)) {
         double tmp= pha0; pha0= pha1; pha1= tmp;
      }
      dp[2]= pha0;
      dp[3]= pha1;

      prev_resp= resp;
      prev_pha= pha;
      prev_cmp= cmp;
   }

   return rv;
}	 


//
//	Do a binary search for a particular frequency response value.
//	'prec' is the precision to which the answer is required.
//	Returns the frequency, or -1 if the parameters were bad.
//

static double 
resp_bin_search(Filter *ff, double target, double p0, double p2, double prec) {
   double p1;
   double gain;
   double gain0, gain2;
   int cnt= 0;
   double diff= fabs(p2-p0);

   while (diff > prec) { cnt++; diff *= 0.5; }

   gain0= filter_response(ff, p0, 0);
   gain2= filter_response(ff, p2, 0);

   if ((gain0 > target && gain2 > target) ||
       (gain0 < target && gain2 < target)) {
      //error("Bad call to resp_bin_search(%g,%g,%g) %g-%g", target, p0, p2, gain0, gain2);
      return -1;
   }

   if (gain0 > gain2) {
      double tmp= p2; p2= p0; p0= tmp;
   }
   
   while (cnt-- > 0) {
      p1= 0.5 * (p0+p2);
      gain= filter_response(ff, p1, 0);
      if (gain < target) 
	 p0= p1;
      else 
	 p2= p1;
   }

   return (p0+p2)*0.5;
}

   
//
//	Setup frequency-response related stuff in the Filter structure
//

void 
filter_setup_gain(Filter *ff) {
   double gainDC, gainHF, gainMAX, gainMIN, gain100;
   int a;
   int dc, hf, stop;
   double *tmp, *dp;
   const int CNT= 10000;
   int n_m3db, m_m3db;
   int n_m6db, m_m6db;
   int n_mm, m_mm;

   gainDC= filter_response(ff, 0.0, 0);
   gainHF= filter_response(ff, 0.5, 0);
   gainMAX= gainDC;
   gainMIN= gainDC;
   gain100= 0.0;

   tmp= filter_resp_range(ff, 0.0, 0.5, CNT, 100);

   //for (a= 0, dp= tmp; a<CNT; a++, dp += 4)
   //   printf("  %03d %-12g %-12g %-12g %-12g\n", a, dp[0], dp[1], dp[2], dp[3]);

   for (a= 0, dp= tmp; a<CNT; a++, dp += 4) {
      if (dp[0] < gainMIN) gainMIN= dp[0];
      if (dp[1] > gainMAX) gainMAX= dp[1];
   }

   // Try and figure out which type of filter we have here
   dc= (gainDC/gainMAX >= 0.5);
   hf= (gainHF/gainMAX >= 0.5);
   stop= (gainMIN/gainMAX <= 0.5);
   ff->typ= ((dc && !hf) ? 0 :		// low-pass
	     (!dc && hf) ? 1 : 		// high-pass
	     (!dc && !hf) ? 2 :		// band-pass
	     (stop) ? 3 :		// band-stop
	     4);			// all-pass
   ff->typstr= filt_typename[ff->typ];

   switch (ff->typ) {
    case 0: gain100= gainDC; break;
    case 1: gain100= gainHF; break;
    case 2: gain100= gainMAX; break;
    case 3: gain100= gainDC; break;
    case 4: gain100= gainDC; break;
   }
   
   ff->gain100= gain100;
   ff->gain= gainMAX;

   // Now search for -3dB crossing points
   n_m3db= 0;
   m_m3db= 16;
   ff->m3db= ALLOC_ARR(m_m3db, double);
   if (dc) ff->m3db[n_m3db++]= 0.0;
   for (a= 0, dp= tmp; a<CNT; a++, dp += 4) {
      if (dp[0]/gain100 <= M301DB && dp[1]/gain100 > M301DB) {
	 double freq;
	 if (n_m3db + 1 >= m_m3db) {	// Always leave space for 1 extra, for 0.5 entry
	    double *grow= ALLOC_ARR(m_m3db*2, double);
	    memcpy(grow, ff->m3db, (size_t)n_m3db*sizeof(double));
	    free(ff->m3db); ff->m3db= grow; m_m3db *= 2;
	 }

	 freq= resp_bin_search(ff, gain100 * M301DB, a*(0.5/CNT), (a+1)*(0.5/CNT), 1e-7);
	 if (freq >= 0) 
	    ff->m3db[n_m3db++]= freq;
	 else 
	    warn("  WARNING: missed a very narrow -3.01dB region");
      }
   }

   // This is what we were leaving the extra space for
   if (hf) ff->m3db[n_m3db++]= 0.5;
   ff->n_m3db= n_m3db;

   // Now search for -6.02dB crossing points
   n_m6db= 0;
   m_m6db= 16;
   ff->m6db= ALLOC_ARR(m_m6db, double);
   if (dc) ff->m6db[n_m6db++]= 0.0;
   for (a= 0, dp= tmp; a<CNT; a++, dp += 4) {
      if (dp[0]/gain100 <= M602DB && dp[1]/gain100 > M602DB) {
	 double freq;
	 if (n_m6db + 1 >= m_m6db) {	// Always leave space for 1 extra, for 0.5 entry
	    double *grow= ALLOC_ARR(m_m6db*2, double);
	    memcpy(grow, ff->m6db, (size_t)n_m6db*sizeof(double));
	    free(ff->m6db); ff->m6db= grow; m_m6db *= 2;
	 }

	 freq= resp_bin_search(ff, gain100 * M602DB, a*(0.5/CNT), (a+1)*(0.5/CNT), 1e-7);
	 if (freq >= 0) 
	    ff->m6db[n_m6db++]= freq;
	 else 
	    warn("  WARNING: missed a very narrow -6.02dB region");
      }
   }

   // This is what we were leaving the extra space for
   if (hf) ff->m6db[n_m6db++]= 0.5;
   ff->n_m6db= n_m6db;

   // Now search for min/max points (used to make sure the
   // mini-display is good)
   n_mm= 0;
   m_mm= 16;
   ff->minmax= ALLOC_ARR(m_mm*2, double);
   for (a= 1, dp= tmp+4; a<CNT-1; a++, dp += 4) {
      int min= (dp[-4] > dp[0] && dp[4] > dp[0]);
      int max= (dp[-3] < dp[1] && dp[5] < dp[1]);
      if (!min && !max) continue;

      if (n_mm == m_mm) {
	 double *grow= ALLOC_ARR(m_mm*2*2, double);
	 memcpy(grow, ff->minmax, (size_t)(n_mm*2)*sizeof(double));
	 free(ff->minmax); ff->minmax= grow; m_mm *= 2;
      }
	 
      ff->minmax[n_mm*2]= (a+0.5)/CNT * 0.5;
      ff->minmax[n_mm*2+1]= min ? dp[0] : dp[1];
      n_mm++;
   }
   ff->n_minmax= n_mm;

   free(tmp);
}


//
//	Write log for filter.  
//

char *
filter_dump(Filter *ff) {
   int a;
   char *rv;
   SubFilt *sf;
   int predef;	// Any predefined filters?
   double adj;
   FidFilter *tmp;

   //   time_t now;
   //   time(&now);

#define pr scr_pr
#define prw scr_prw

   scr_zap();
   scr_wrap(78, "//   ");

   pr("// Filter %d\n", ff->ii);
   pr("// File: %s\n", ff->name);
   pr("// Guessed type: %s\n", filt_typename[ff->typ]);
   pr("//\n");
   pr("// Frequency-response:\n");
   pr("//   Peak gain: %g\n", ff->gain);
   pr("//   Guessed 100%% gain: %g\n", ff->gain100);
   if (ff->n_m3db) {
      pr("//   Regions between half-power points (70.71%% response or -3.01dB):\n");
      for (a= 0; a<ff->n_m3db-1; a++) {
	 double f0, f1;
	 int nmp= 0;
	 f0= ff->m3db[a];
	 f1= ff->m3db[a+1];
	 if (f0 == 0.0) nmp= 1;
	 if (f1 == 0.5) nmp= 1;
	 if (s_rate) 
	    pr(nmp ? 
	       "//     %gHz -> %gHz  (width %gHz)\n" :
	       "//     %gHz -> %gHz  (width %gHz, midpoint %gHz)\n", 
	       f0 * s_rate, f1 * s_rate,
	       (f1-f0) * s_rate, (f1+f0) * 0.5 * s_rate);
	 else 
	    pr(nmp ? 
	       "//     %g -> %g  (width %g)\n" :
	       "//     %g -> %g  (width %g, midpoint %g)\n", 
	       f0, f1, (f1-f0), (f1+f0) * 0.5);
      }
   }
   if (ff->n_m6db) {
      pr("//   Regions between quarter-power points (50%% response or -6.02dB):\n");
      for (a= 0; a<ff->n_m6db-1; a++) {
	 double f0, f1;
	 int nmp= 0;
	 f0= ff->m6db[a];
	 f1= ff->m6db[a+1];
	 if (f0 == 0.0) nmp= 1;
	 if (f1 == 0.5) nmp= 1;
	 if (s_rate) 
	    pr(nmp ? 
	       "//     %gHz -> %gHz  (width %gHz)\n" :
	       "//     %gHz -> %gHz  (width %gHz, midpoint %gHz)\n", 
	       f0 * s_rate, f1 * s_rate,
	       (f1-f0) * s_rate, (f1+f0) * 0.5 * s_rate);
	 else 
	    pr(nmp ? 
	       "//     %g -> %g  (width %g)\n" :
	       "//     %g -> %g  (width %g, midpoint %g)\n", 
	       f0, f1, (f1-f0), (f1+f0) * 0.5);
      }
   }
   pr("//\n");
   pr("// Time-response:\n");
   if (s_rate) pr("//   Sampling rate is %gHz\n", s_rate); 
   pr(s_rate ? 
      "//   50%% complete after %d samples (%gs)\n" : 
      "//   50%% complete after %d samples\n",
      ff->cnt50, ff->cnt50/s_rate);
   pr(s_rate ? 
      "//   90%% complete after %d samples (%gs)\n" : 
      "//   90%% complete after %d samples\n",
      ff->cnt90, ff->cnt90/s_rate);
   pr(s_rate ?
      "//   95%% complete after %d samples (%gs)\n" :
      "//   95%% complete after %d samples\n", 
      ff->cnt95, ff->cnt95/s_rate);
   pr(s_rate ?
      "//   99%% complete after %d samples (%gs)\n" :
      "//   99%% complete after %d samples\n", 
      ff->cnt99, ff->cnt99/s_rate);
   pr(s_rate ?
      "//   99.9%% complete after %d samples (%gs)\n" :
      "//   99.9%% complete after %d samples\n", 
      ff->cnt999, ff->cnt999/s_rate);
   pr(s_rate ?
      "//   99.99%% complete after %d samples (%gs)\n" :
      "//   99.99%% complete after %d samples\n", 
      ff->cnt9999, ff->cnt9999/s_rate);
   pr(s_rate ? 
      "//   Impulse response max deviation is at %d samples (%gs)\n" :
      "//   Impulse response max deviation is at %d samples\n",
      ff->cnt_max, ff->cnt_max/s_rate);
   pr("//   Impulse response ranges between %g and %g\n",
      ff->impmin, ff->impmax);
   pr("//\n");

   predef= 0;
   pr("// Original filter:\n");
   scr_wrap(78, "//     ");
   for (sf= ff->filt; sf; sf= sf->nxt) {
      FidFilter *fd= sf->filt;
      while (fd->typ) {
	 pr("//   %c", (fd->typ == 'I') ? '/' : (fd->typ == 'F') ? 'x' : fd->typ);
	 for (a= 0; a<fd->len; a++)
	    prw(" %.15g", fd->val[a]);
	 fd= FFNEXT(fd);
	 if (!fd->typ && sf->desc0) {
	    prw(" (%s)", sf->desc0);
	    predef= 1;
	 }
	 pr("\n");
      }
   }
   pr("//\n");

   if (predef) {
      pr("// Filter descriptions:\n");
      for (sf= ff->filt; sf; sf= sf->nxt) 
	 if (sf->desc0) {
	    pr("//   %s == ", sf->desc0);
	    prw("%s", sf->desc1);
	    pr("\n");
	 }
      pr("//\n");
   }

   //   adj= 1.0/ff->gain100;
   //   pr("// Combined filter:\n");
   //   filter_dump_filter(ff, 1.0);
   //   if (adj != 1.0) {
   //      pr("//\n");
   //      pr("// Combined filter with unity gain for '100%%' level:\n");
   //      filter_dump_filter(ff, adj);
   //   }

   pr("\n");

   // Code to run the filter
   pr("// Example code (readable version)\n");
   adj= 1.0/ff->gain100;
   filter_dump_code(ff->ff, adj, 0);

   pr("// Example code (functionally the same as the above code, but \n"
      "//  optimised for cleaner compilation to efficient machine code)\n");
   adj= 1.0/ff->gain100;
   filter_dump_code(ff->ff, adj, 1);

   // Runtime variable code
   filter_dump_var_code(ff);

   // Fidlib code
   pr("// Example using direct fidlib calls, and using the fidlib run-filter\n");
   pr("//  code for execution, for maximum flexibility.\n");
   filter_dump_fidlib_calls(ff);

   // Combined code
   pr("// Example code using combined stages.  WARNING: combined stages are   \n"
      "//   less accurate, and can also be unstable for high-order filters.\n");
   tmp= fid_flatten(ff->ff);
   filter_dump_code(tmp, adj, 0);
   free(tmp);

   //   pr("// Filter parameters for filter_* code (see end of fiview.log)\n");
   //   filter_dump_code2(ff, 1.0);
   //   if (adj != 1.0) {
   //      pr("// Filter parameters for filter_* code with unity gain for '100%%' level\n");
   //      filter_dump_code2(ff, adj);
   //   }

   rv= scr_dup();
   return rv;
}

typedef struct FiltPair FiltPair;
struct FiltPair {
   int n_iir;		// Number of IIR coefficients, >= 0
   int n_fir;		// Number of FIR coefficients, >= 1, or 0 to mark end of list
   double *iir;		// IIR coefficients
   double *fir;		// FIR coefficients
};

//
//	Convert a FidFilter into a list of coefficients for paired-up
//	IIR/FIR filters.  Expects all the IIR first coefficients to
//	already have been adjusted to 1.0 (these coefficients are
//	omitted from the output list).
//
static FiltPair*
pair_filters(FidFilter *ff0) {
   int cnt= 1;
   FidFilter *ff;
   int prev_iir= 0;
   FiltPair *rv;
   static double const_one= 1.0;	// Used to point to for missing FIR

   // Count how many slots we will need
   for (ff= ff0; ff->typ; ff= FFNEXT(ff)) {
      switch (ff->typ) {
       case 'F':
	  if (!prev_iir) cnt++;
	  prev_iir= 0;
	  break;
       case 'I':
	  cnt++;
	  prev_iir= 1;
	  break;
       default:
	  error("pair_filters -- can't handle filter type %c", ff->typ);
      }
   }
   
   rv= ALLOC_ARR(cnt, FiltPair);

   // Fill in the slots
   cnt= 0;
   prev_iir= 0;
   for (ff= ff0; ff->typ; ff= FFNEXT(ff)) {
      switch (ff->typ) {
       case 'F':
	  if (prev_iir) {
	     rv[cnt-1].n_fir= ff->len;
	     rv[cnt-1].fir= ff->val;
	  } else {
	     rv[cnt].n_iir= 0;
	     rv[cnt].n_fir= ff->len;
	     rv[cnt].fir= ff->val;
	     cnt++;
	  }
	  prev_iir= 0;
	  break;
       case 'I':
	  if (fabs(ff->val[0] - 1.0) > 1e-14)	// was: (ff->val[0] != 1.0)
	     error("pair_filters called with an unadjusted IIR filter -- internal error");
	  rv[cnt].n_iir= ff->len-1;
	  rv[cnt].iir= ff->val+1;
	  rv[cnt].n_fir= 1;
	  rv[cnt].fir= &const_one;
	  cnt++;
	  prev_iir= 1;
	  break;
      }
   }
   
   // Final slot is already 0 from allocation
   return rv;
}


// Dump the readable version of the process() routine, for both
// constant and runtime-variable coefficients.  Variable coefficients
// should be marked by setting them to NAN.  The gain should also be
// NAN to pick up a coef[] entry instead of a constant.
static void
dump_readable_process(FiltPair *fpair, double gain, double adj, int n_buf, int rtvar) {
   int n_coef= 0;
   FiltPair *fp;
   int buf_off;
   int init_off= scr_len;	// Initial offset in scratch buffer
   double val;
   char buf[16];
   int siz, a;

   // Output code
   scr_wrap(78, "     ");
   if (!rtvar) {
      pr("double\n");
      pr("process(register double val) {\n");
      if (n_buf)
	 pr("   static double buf[%d];\n", n_buf);
   } else {
      pr("double\n");
      pr("process(double *coef, double *buf, register double val) {\n");
   }
   pr("   register double tmp, fir, iir;\n");
   // I'm using tmp to store the value that we've just overwritten in
   // the buffer.  It also takes the same role between one stage and
   // the next.  Note that a memmove on ix86 is *much* more efficient
   // than the alternatives -- including copying the memory as longs.
   // Copying as floating point numbers is incredibly slow by
   // comparison due to the conversions required in and out of the
   // FPU's internal 80-bit floating point format.
   if (n_buf > 0) {
      pr("   tmp= buf[0];");
      if (n_buf > 1)
	 pr(" memmove(buf, buf+1, %d*sizeof(double));", n_buf-1);
      pr("\n");
   }
   if (adj != 1.0) 
      pr("   // use %.16g below for unity gain at 100%% level\n", gain * adj);
   if (rtvar && isnan(gain))
      pr("   val *= coef[%d];\n", n_coef++);
   else 
      pr("   val *= %.16g;\n", gain);

#define PRVALBUF \
   if (val == -2.0) prw("-%s-%s", buf, buf); \
   else if (val == -1.0) prw("-%s", buf); \
   else if (val == 0.0) ; \
   else if (val == 1.0) prw("+%s", buf); \
   else if (val == 2.0) prw("+%s+%s", buf, buf); \
   else if (rtvar && isnan(val)) prw("+coef[%d]*%s", n_coef++, buf); \
   else prw("%+.16g*%s", val, buf);

   buf_off= 0;
   for (fp= fpair; fp->n_fir; fp++) {
      // IIR part of stage
      pr("   iir= val");
      for (a= 0; a<fp->n_iir; a++) {
	 sprintf(buf, (a == (fp->n_iir-1) ? "tmp" : "buf[%d]"),
		 fp->n_iir-2-a + buf_off);
	 val= -fp->iir[a];
	 PRVALBUF;
      }
      prw(";\n");

      // FIR part of stage
      pr("   fir= ");
      for (a= 0; a<fp->n_fir; a++) {
	 sprintf(buf, (a == 0 ? "iir" :
		       a == (fp->n_fir-1) ? "tmp" : 
		       "buf[%d]"),
		 fp->n_fir-2-a + buf_off);
	 val= fp->fir[a];
	 PRVALBUF;
      }
      prw(";\n");

      // Clean-up part of stage
      siz= fp->n_iir;
      if (fp->n_fir-1 > siz) siz= fp->n_fir-1;
      buf_off += siz;
      pr("  ");
      if (siz && fp[1].n_fir)
	 pr(" tmp= buf[%d];", buf_off-1);
      if (siz)
	 pr(" buf[%d]= iir;", buf_off-1);
      pr(" val= fir;\n");
   }
	 
   pr("   return val;\n");
   pr("}\n\n");

   // Zip through scratch cleaning up "fir= +" -> "fir= "
   {
      char *p= scratch + init_off;
      char *q= p;
      char *end= scratch + scr_len;
      
      while (p < end) {
	 if (p[0] == 'f' && p[1] == 'i' && p[2] == 'r' && 
	     p[3] == '=' && p[4] == ' ' && p[5] == '+') {
	    *q++= *p++; *q++= *p++; *q++= *p++; 
	    *q++= *p++; *q++= *p++; 
	    p++;	// Skip the '+'
	    continue;
	 }
	 *q++= *p++;
      }
      scr_len= (int)(q-scratch);
   }

#undef PRVALBUF
}


// Dump the optimised process() routine, for both constant and
// runtime-variable coefficients.  Variable coefficients should be
// marked by setting them to NAN.  The gain should also be NAN to pick
// up a coef[] entry instead of a constant.  The operations are
// ordered to give the compiler little opportunity to get it wrong.
static void
dump_optimised_process(FiltPair *fpair, double gain, double adj, int n_buf, int rtvar) {
   int n_coef= 0;
   FiltPair *fp;
   int buf_off;
   double val;
   char buf[16];
   int a;
   int first= 1;

   // Output code
   scr_wrap(78, "     ");
   if (!rtvar) {
      pr("double\n");
      pr("process(register double val) {\n");
      if (n_buf) 
	 pr("   static double buf[%d];\n", n_buf);
   } else {
      pr("double\n");
      pr("process(double *coef, double *buf, register double val) {\n");
   }
   pr("   register double tmp, fir, iir;\n");
   // I'm using tmp to store the value that we've just overwritten in
   // the buffer.  It also takes the same role between one stage and
   // the next.  Note that a memmove on ix86 is *much* more efficient
   // than the alternatives -- including copying the memory as longs.
   // Copying as floating point numbers is incredibly slow by
   // comparison due to the conversions required in and out of the
   // FPU's internal 80-bit floating point format.
   if (n_buf > 0) {
      pr("   tmp= buf[0];");
      if (n_buf > 1) 
	 pr(" memmove(buf, buf+1, %d*sizeof(double));", n_buf-1);
      pr("\n");
   }
   if (adj != 1.0) 
      pr("   // use %.16g below for unity gain at 100%% level\n", gain * adj);

   buf_off= 0;
   for (fp= fpair; fp->n_fir; fp++) {
      int cnt;
      int init_fir= 0;

      // Setup variables
      if (!first)
	 pr("   iir= val;\n");
      else {
	 if (rtvar && isnan(gain))
	    pr("   iir= val * coef[%d];\n", n_coef++);
	 else 
	    pr("   iir= val * %.16g;\n", gain);
	 first= 0;
      } 

      // Run through coefficients backwards
      cnt= fp->n_iir > fp->n_fir-1 ? fp->n_iir : fp->n_fir-1;
      for (a= cnt; a>=0; a--) {
	 int rewind= scr_len;
	 sprintf(buf, a == 0 ? "iir" : a==cnt ? "tmp" : "buf[%d]", cnt-1-a+buf_off);
	 pr("  ");
	 if (a > 0 && a-1 < fp->n_iir) {
	    val= fp->iir[a-1];	
	    // We want -val really, so signs are opposite below
	    if (val == -2.0) prw(" iir += %s+%s;", buf, buf); 
	    else if (val == -1.0) prw(" iir += %s;", buf); 
	    else if (val == 0.0) ; 
	    else if (val == 1.0) prw(" iir -= %s;", buf); 
	    else if (val == 2.0) prw(" iir -= %s+%s;", buf, buf); 
	    else if (rtvar && isnan(val)) prw(" iir -= coef[%d]*%s;", n_coef++, buf); 
	    else prw(" iir -= %.16g*%s;", val, buf);
	 } 
	 if (a < fp->n_fir) {
	    char ass[16];
	    int old_init_fir= init_fir;
	    val= fp->fir[a];
	    strcpy(ass, init_fir ? "fir +=" : "fir="); init_fir= 1;
	    if (val == -2.0) prw(" %s -%s-%s;", ass, buf, buf); 
	    else if (val == -1.0) prw(" %s -%s;", ass, buf); 
	    else if (val == 0.0) init_fir= old_init_fir;
	    else if (val == 1.0) prw(" %s %s;", ass, buf); 
	    else if (val == 2.0) prw(" %s %s+%s;", ass, buf, buf); 
	    else if (rtvar && isnan(val)) prw(" %s coef[%d]*%s;", ass, n_coef++, buf); 
	    else prw(" %s %.16g*%s;", ass, val, buf);
	 }
	 pr("\n");
	 if (scr_len == rewind+3) scr_len= rewind;	// Eliminate blank lines
      }
      
      if (!init_fir)
	 error("Pointless filter with guaranteed 0 output");

      // Clean-up part of stage
      buf_off += cnt;
      pr("  ");
      if (cnt && fp[1].n_fir)
	 pr(" tmp= buf[%d];", buf_off-1);
      if (cnt)
	 pr(" buf[%d]= iir;", buf_off-1);
      pr(" val= fir;\n");
   }

   pr("   return val;\n");
   pr("}\n\n");
}

// Counts the number of buffer spaces required for the filter.
// Returns 0 if none are required, so watch out!  Most of the code
// originally assumed that there would be at least one buffer entry.
static int 
count_reqd_buffer_spaces(FidFilter *tmpff) {
   FidFilter *ff0, *ff1;
   int n_buf= 0;
   for (ff0= tmpff; ff0->typ; ) {
      ff1= FFNEXT(ff0);
      if (ff0->typ == 'F') {
	 n_buf += ff0->len - 1;
	 ff0= ff1;
	 continue;
      }
      if (ff0->typ == 'I' && ff1->typ == 'F') {
	 n_buf += (ff0->len > ff1->len ? ff0->len : ff1->len) - 1;
	 ff0= FFNEXT(ff1);
	 continue;
      }
      if (ff0->typ == 'I') {
	 n_buf += ff0->len - 1;
	 ff0= ff1;
	 continue;
      }
      error("count_reqd_buffer_spaces() internal error -- don't know how to handle type %c",
	    ff0->typ);
   }
   return n_buf;
}

// opt == 1 for compiler-optimised layout of code
void 
filter_dump_code(FidFilter *inpfilt, double adj, int opt) {
   FidFilter *ff0, *ff1, *tmpff;
   FiltPair *fp;
   double gain= 1.0;
   int a, n_buf;

   // Allocate enough space for a copy of the FidFilter
   for (ff0= inpfilt; ff0->typ; ff0= FFNEXT(ff0)) ;
   ff0= FFNEXT(ff0);
   tmpff= (FidFilter*)Alloc((size_t)((char*)ff0 - (char*)inpfilt));
   
   // Copy the filter over, cleaning it up as we go
   ff1= tmpff;
   for (ff0= inpfilt; ff0->typ; ff0= FFNEXT(ff0)) {
      if (ff0->typ == 'F' && ff0->len == 1) {
	 gain *= ff0->val[0];
	 continue;
      }
      memcpy(ff1, ff0, FFSIZE(ff0->len));
      if (ff1->typ == 'I') {
	 double scale= 1.0 / ff1->val[0];
	 if (ff1->val[0] == 0.0)
	    error("Can't handle IIR filters with zero first coefficient");
	 for (a= 0; a<ff1->len; a++)
	    ff1->val[a] *= scale;
	 gain *= scale;
      }
      ff1= FFNEXT(ff1);
   }
   ff1->typ= 0;
   ff1->len= 0;

   // Work out how many buffer slots we require
   n_buf= count_reqd_buffer_spaces(tmpff);

   // Output code
   fp= pair_filters(tmpff);
   if (opt) 
      dump_optimised_process(fp, gain, adj, n_buf, 0);
   else 
      dump_readable_process(fp, gain, adj, n_buf, 0);
   free(fp);
   free(tmpff);
}   

//
//	Dump code for generating general filters of this class at
//	runtime.
//	

void 
filter_dump_var_code(Filter *filt) {
   SubFilt *sf;
   int len= 0;
   FidFilter *tmpff, *ff, *gg;
   FiltPair *fp;
   double gain= 1.0;
   int n_buf;
   int n_coef;
   int a;

   // Merge all the FidFilters from the SubFilt list into a single
   // big FidFilter, marking variable coefficients with NAN
   for (sf= filt->filt; sf; sf= sf->nxt) 
      len += sf->filt_len;

   tmpff= (FidFilter*)Alloc((size_t)len + FFSIZE(0));
   ff= tmpff;

   n_coef= 1;	// 1 for initial gain coefficient
   for (sf= filt->filt; sf; sf= sf->nxt) {
      if (!sf->minsp) {
	 // Constant, so just copy it over, cleaning it up as we go
	 for (gg= sf->filt; gg->typ; gg= FFNEXT(gg)) {
	    if (gg->typ == 'F' && gg->len == 1) {
	       gain *= gg->val[0];
	       continue;
	    }
	    memcpy(ff, gg, FFSIZE(gg->len));
	    if (ff->typ == 'I') {
	       double adj= 1.0 / ff->val[0];
	       if (ff->val[0] == 0.0) 
		  error("Can't handle IIR filters with a zero first coefficient");
	       for (a= 0; a<ff->len; a++)
		  ff->val[a] *= adj;
	       gain *= adj;
	    }
	    ff= FFNEXT(ff);
	 }
      } else {
	 // This will be generated at run-time; copy it over, but
	 // replace variable stuff with NAN
	 int nc= 0;
	 for (gg= sf->filt; gg->typ; gg= FFNEXT(gg)) {
	    if (gg->typ == 'F' && gg->len == 1) {
	       // Ignore -- taken care of in fid_design_coef()
	       continue;
	    }
	    memcpy(ff, gg, FFSIZE(gg->len));
	    for (a= 0; a<ff->len; a++) {
	       if (!(ff->cbm & (1<<(a<15?a:15)))) {
		  ff->val[a]= NAN;
		  nc++;
	       }
	    }
	    if (ff->typ == 'I') {
	       // Adjustment of first IIR element is handled by
	       // fid_design_coef()
	       if (isnan(ff->val[0])) nc--;
	       ff->val[0]= 1.0; 
	    }
	    ff= FFNEXT(ff);
	 }
	 
	 sf->n_var= nc;
	 n_coef += nc;
      }
   }

   // Okay, we now have a usable FidFilter.
   ff->len= 0;
   ff->typ= 0;

   // Drop out now if this filter doesn't actually have anything we
   // can vary at run-time
   if (n_coef == 1) {
      free(tmpff);
      return;
   }

   // Work out how many buffer slots we require
   n_buf= count_reqd_buffer_spaces(tmpff);

   // Dump out initial code and setup() routine 
   pr("// Example code for generating any of this class of filters at run-time\n");
   pr("double coef[%d], buf[%d];\n", n_coef, n_buf ? n_buf : 1);
   {
      int nc= 1;
      pr("void\n");
      pr("setup(double *coef) {\n");
      pr("   coef[0]= %.16g", gain);
      for (sf= filt->filt; sf; sf= sf->nxt) 
	 if (sf->minsp) {
	    pr(" *\n     fid_design_coef(coef+%d, %d, \"%s\", %g, %g, %g, %d)",
	       nc, sf->n_var, sf->minsp, s_rate ? s_rate : 1.0, 
	       sf->minf0, sf->minf1, sf->minadj);
	    nc += sf->n_var;
	 }
      pr(";\n");
      pr("}\n");
   }

   // Dump out process() routine
   fp= pair_filters(tmpff);
   dump_optimised_process(fp, NAN, 1.0, n_buf, 1);
   free(fp);
   free(tmpff);
}      

void 
filter_dump_fidlib_calls(Filter *filt) {
   SubFilt *sf;
   FidFilter *ff;
   int cnt= 0;
   int a;

   pr("#include \"fidlib/fidlib.h\"    // May need adjusting\n");
   pr("FidFilter *\n");
   pr("setup() {\n");
   for (sf= filt->filt; sf; sf= sf->nxt) {
      if (sf->minsp) {
	 pr("   FidFilter *filt%d= fid_design(\"%s\", %g, %g, %g, %d, 0);\n",
	    cnt++, sf->minsp, s_rate ? s_rate : 1, sf->minf0, sf->minf1, sf->minadj);
	 continue;
      }
      
      // Need to dump out IIR/FIR coefficients
      scr_wrap(78, "     ");
      pr("   static double array%d[]= {", cnt);
      while (1) {
	 for (ff= sf->filt; ff->typ; ff= FFNEXT(ff)) {
	    prw(" '%c',", ff->typ);
	    prw(" %d,", ff->len);
	    for (a= 0; a<ff->len; a++) 
	       prw(" %.15g,", ff->val[a]);
	 }
	 if (sf->nxt && !sf->nxt->minsp) {
	    sf= sf->nxt;	// Skip onto next if we can put it in the same array
	    continue;
	 }
	 break;
      }
      prw(" 0 };\n");
      pr("   FidFilter *filt%d= fid_cv_array(array%d);\n", cnt, cnt);
      cnt++;
   }

   if (cnt == 1) {
      pr("   return filt0;\n");
   } else {
      pr("   return fid_cat(1,");
      for (a= 0; a<cnt; a++) 
	 prw(" filt%d,", a);
      prw(" (void*)0);\n");
   }
   pr("}\n");
   pr("//\n");
   pr("// Run a couple of instances using fidlib:\n");
   pr("//\n");
   pr("//   FidFilter *filt= setup();\n");
   pr("//   FidFunc *funcp;\n");
   pr("//   FidRun *run= fid_run_new(filt, &funcp);\n");
   pr("//   void *fbuf1= fid_run_newbuf(run);\n");
   pr("//   void *fbuf2= fid_run_newbuf(run);\n");
   pr("//   while (...) {\n");
   pr("//      out_1= funcp(fbuf1, in_1);\n");
   pr("//      out_2= funcp(fbuf2, in_2);\n");
   pr("//      if (restart_required) fid_run_zapbuf(fbuf1);\n");
   pr("//      ...\n");
   pr("//   }\n");
   pr("//   fid_run_freebuf(fbuf2);\n");
   pr("//   fid_run_freebuf(fbuf1);\n");
   pr("//   fid_run_free(run);\n");
   pr("//\n");
   pr("// Or check the frequency response:\n");
   pr("//   resp= fid_response(filt, freq/rate);\n");
   pr("//\n");
   pr("\n");
}

//
//	Dump the given FidFilter in a reloadable format
//

void 
dump_filter_coef(FidFilter *ff, FILE *out) {
   int a;
   scr_zap();
   scr_wrap(78, "  ");
   while (ff->typ) {
      if (ff->typ == 'I') 
	 pr("/");
      else if (ff->typ == 'F') 
	 pr("x");
      else 
	 error("Unknown type '%c' in dump_filter_coef", ff->typ);
      for (a= 0; a<ff->len; a++) 
	 prw(" %.15g", ff->val[a]);
      ff= FFNEXT(ff);
      if (ff->typ)
	 pr("\n");
      else 
	 pr(";\n\n");
   }
   SCR_PUTC(0);
   fputs(scratch, out);
}
      

//void 
//filter_dump_filter(Filter *ff, double adj) {
//   int a;
//   scr_wrap(78, "//     ");
//   if (ff->n_iir != 1) {
//      pr("//   /");
//      for (a= 0; a<ff->n_iir; a++)
//	 prw(" %.15g", ff->iir[a]);
//      pr("\n");
//   }
//   pr("//   x");
//   for (a= 0; a<ff->n_fir; a++) 
//      prw(" %.15g", adj * ff->fir[a]);
//   pr("\n");
//}

//void 
//filter_dump_code1(Filter *ff, double adj) {
//   char buf[128];
//   double val;
//   int a, first;
//
//   scr_wrap(78, "     ");
//   pr("double\n");
//   pr("process(double val) {\n");
//   pr("   static double buf[%d];\n", ff->n_buf);
//   if (ff->n_buf > 30)
//      pr("   memmove(buf+1, buf, %d*sizeof(buf[0]));\n", ff->n_buf-1);
//   else {
//      pr("   ");
//      for (a= ff->n_buf-2; a>=0; a--)
//	 prw("buf[%d]= buf[%d]; ", a+1, a);
//      pr("\n");
//   }
//   pr("   buf[0]= val");
//   for (a= 1; a<ff->n_iir; a++) {
//      val= -ff->iir[a];
//      if (val != 0.0) {
//	 if (val == 1.0) sprintf(buf, "+buf[%d]", a);
//	 else if (val == -1.0) sprintf(buf, "-buf[%d]", a);
//	 else sprintf(buf, "%+.15g*buf[%d]", val, a);
//	 prw("%s", buf);
//      }
//   }
//   pr(";\n");
//   pr("   return ");
//   first= 1;
//   for (a= 0; a<ff->n_fir; a++) {
//      val= adj * ff->fir[a];
//      if (val != 0.0) {
//	 if (val == 1.0) sprintf(buf, "+buf[%d]", a);
//	 else if (val == -1.0) sprintf(buf, "-buf[%d]", a);
//	 else sprintf(buf, "%+.15g*buf[%d]", val, a);
//	 prw("%s", (first && buf[0] == '+') ? buf+1 : buf);
//	 first= 0;
//      }
//   }
//   pr(";\n");
//   pr("}\n");
//   pr("\n");
//}
//
//void 
//filter_dump_code2(Filter *ff, double adj) {
//   int a; 
//
//   scr_wrap(78, "   ");
//   pr("double spec%d%s[]= {\n", ff->ii, adj == 1.0 ? "" : "_unity");
//   pr("   %d, %d,\n   ", ff->n_iir-1, ff->n_fir);
//   for (a= 1; a<ff->n_iir; a++)
//      prw("%.15g, ", ff->iir[a]);
//   for (a= 0; a<ff->n_fir; a++)
//      prw("%.15g, ", adj * ff->fir[a]);
//   pr("\n};\n\n");
//}

#define NL "\n"

const char *
filter_standard_code() {
   return 
      NL "// Notes on use of example code:"
      NL "// ----------------------------"
      NL "//"
      NL "// The example code above should be easy to adapt to your application."
      NL "// It is designed to be readable, and any unnecessary assignments used"
      NL "// for readability's sake should optimise away on any decent compiler."
      NL "// memmove() is used for shifting the buffer.  This was found to be"
      NL "// faster than all other portable alternatives on GCC for ix86,"
      NL "// including copying longs.  Copying doubles is slowest of all due to"
      NL "// conversion to/from the FPU's internal 80-bit format."
      NL "//"
      NL "// The compilation-optimised version reorders the calculations into an"
      NL "// order that minimises the number of values the compiler has to"
      NL "// remember at any one time, giving us a better chance of decent code."
      NL "// I dare say it could all be squeezed more, but you'd need to do"
      NL "// benchmarking to be sure that you are getting a real gain.  Many"
      NL "// 'obvious' speed-up changes I tried actually slowed the code down --"
      NL "// the behaviour of modern processors seems quite hard to predict in"
      NL "// this regard."
      NL "//"
      NL "// Note that the code for handling runtime generated filters receives"
      NL "// clues from the filter-design code about which values are constant"
      NL "// and which vary, but it can't easily pick up other patterns within"
      NL "// the coefficients.  So, perhaps this also leaves some room for"
      NL "// optimisation if squeezing out every last bit of performance is"
      NL "// important for your application."
      NL "//"
      NL "// For generating filters at runtime, call setup(coef) first to"
      NL "// generate the coefficients, then use val= process(coef, buf, val)"
      NL "// for each sample.  Zero the buf[] to reinitialise the filter at any"
      NL "// time.  This should be easy to adapt to create banks or arrays of"
      NL "// similar filters.  Within setup() you can safely change any of the"
      NL "// frequencies (including the sampling rate), but not the spec-string."
      NL "//"
      NL "// To use the example code that generates filters at runtime, you will"
      NL "// also need the following extern declaration as a minimum:"
      NL ""
      NL "extern double fid_design_coef(double *coef, int n_coef, char *spec, double rate,"
      NL "                              double freq0, double freq1, int adj);"
      NL ""
      NL "// Alternatively you could #include the fidlib.h file.  You will also"
      NL "// need to link with the fidlib.o object file.  Probably it is easiest"
      NL "// just to copy the fidlib directory into your own application and"
      NL "// build it as part of it."
      NL "//"
      NL "// The most general solution, using fidlib for both creating and"
      NL "// running the filters, has the advantage of flexibility (all the"
      NL "// filter parameters can be changed), with the cost of slightly lower"
      NL "// performance and memory efficiency."
      NL "//"
      NL "// Remember that the fidlib code is covered under the LGPL, and this"
      NL "// means that you need to abide by the terms of the LGPL if you link"
      NL "// your application with the fidlib.o object or any other fidlib code"
      NL "// (which is needed if you use anything other than the hard-coded"
      NL "// example routines above).  For one thing, this means that you need"
      NL "// to make the fidlib source available when you distribute your"
      NL "// completed app, including any changes to fidlib you may have made."
      NL "// However, the LGPL doesn't stop you keeping your own (separate)"
      NL "// source code closed (more notes at http://uazu.net/license/).  See"
      NL "// the COPYING_LIB file for full details of the license."
      NL "//"
      NL "// Note that none of this code comes with any kind of legal warranty"
      NL "// for correctness or whatever.  As Dr Tony Fisher put it on his page:"
      NL "// \"Don't blame me if your aircraft falls out of the sky!\""
      NL ""
      NL ""
      NL "";
   //	"//
   //	//	Code to handle a general FIR/IIR filter encoded in a double[].
   //	//	This code is meant to be compact rather than readable.  No
   //	//	effort has been made to optimise it for any particular target,
   //	//	or any particular class of filters, but it should go fairly
   //	//	fast.  This code is in the public domain, so adapt it freely
   //	//	to your needs.
   //	//
   //	//	Example usage:
   //	//
   //	//	  Filter *ff= filter_new(spec1_unity);
   //	//	  while (1) {
   //	//	    double val= get_new_input_value();
   //	//	    val= filter_step(ff, val);
   //	//	    write_new_output_value(val);
   //	//	  }
   //	//	  filter_free(ff);
   //	//
   //	
   //	typedef struct Filter {
   //	   int n_iir, n_fir;  // Numbers of coefficients
   //	   int n_buf;         // Size of buffer
   //	   double *coef;
   //	   double *buf;
   //	} Filter;
   //	
   //	Filter *
   //	filter_new(double *spec) {
   //	   Filter *ff= malloc(sizeof(Filter));
   //	   if (!ff) return 0;
   //	   ff->n_iir= (int)*spec++;
   //	   ff->n_fir= (int)*spec++;
   //	   ff->n_buf= (ff->n_fir > ff->n_iir+1 ? ff->n_fir : ff->n_iir+1);
   //	   ff->coef= spec;
   //	   ff->buf= calloc(ff->n_buf, sizeof(double));
   //	   if (!ff->buf) { free(ff); return 0; }
   //	   return ff;
   //	}
   //	
   //	double 
   //	filter_step(Filter *ff, double val) {
   //	   int a;
   //	   double *dp, *coef= ff->coef;
   //	   for (a= ff->n_buf-1, dp= ff->buf + a; a>0; a--, dp--) dp[0]= dp[-1];
   //	   for (a= ff->n_iir, dp= ff->buf+1; a>0; a--) val -= *coef++ * *dp++;
   //	   buf[0]= val; val= 0;
   //	   for (a= ff->n_fir, dp= ff->buf; a>0; a--) val += *coef++ * *dp++;
   //	   return val;
   //	}
   //	
   //	void 
   //	filter_del(Filter *ff) {
   //	   free(ff->buf);
   //	   free(ff);
   //	}
   //	
   //	";
}


//
//	Run a filter test over the given interval
//

double *
do_filter_test(Filter *filt, int ftmod, double ftarg, double freq0, double freq1, int slots) {
   double delta= (freq1-freq0) / slots;
   double freq= freq0 + delta * 0.5;
   double *rv= ALLOC_ARR(slots, double);
   double *dp;
   Progress pr;
   FidFunc *funcp;
   FidRun *run;
   void *buf1, *buf2;
   int a;

   progress_init(&pr, slots, "Calculating: ", 40);
   run= fid_run_new(filt->ff, &funcp);
   buf1= fid_run_newbuf(run);
   buf2= fid_run_newbuf(run);
   
   for (a= 0, dp= rv; a<slots; a++, freq += delta, dp++) {
      if (++pr.cnt >= pr.upd) progress_update(&pr);

      if (ftmod == 'w') {
	 // Wavelet test.  We just run the wavelet through, and watch
	 // for the maximum value.
	 Wavelet ww;
	 double mag, max= 0.0;
	 int cnt= wavelet_init(&ww, freq, ftarg);
	 if (cnt > 5000) { 	// Too long to calculate
	    dp[0]= NAN;
	    pr.force= 1;	// Because timings will be wrong now
	    continue;
	 }
	 fid_run_zapbuf(buf1);
	 fid_run_zapbuf(buf2);
	 while (cnt-- >= 0) {	// 1 over doesn't matter
	    wavelet_gen(&ww);
	    mag= hypot(funcp(buf1, ww.out[0]), funcp(buf2, ww.out[1]));
	    if (mag > max) max= mag;
	 }
	 cnt= filt->cnt90;	// We should have definitely seen the peak by 90%
	 while (cnt-- > 0) {
	    mag= hypot(funcp(buf1, 0.0), funcp(buf2, 0.0));
	    if (mag > max) max= mag;
	 }
	 dp[0]= max;
      } else if (ftmod == 's') {
	 // Sweep test.  We will test the filter over its 99.9% region
	 // with a changing-pitch complex oscillator.  We need the
	 // pitch to be at frequency 'freq' at the 50% point.
	 int cnt= filt->cnt999 + 5;	// extra five is to help with very short FIRs
	 double f0= freq - ftarg * (cnt - filt->cnt50);
	 double out[2];
	 double osc[2];
	 double inc[2];
	 double incinc[2];
	 osc[0]= 1.0; osc[1]= 0.0;
	 cexpj(inc, 2 * M_PI * f0);
	 cexpj(incinc, 2 * M_PI * ftarg);
	 fid_run_zapbuf(buf1);
	 fid_run_zapbuf(buf2);
	 while (cnt-- > 0) {
	    cmul(inc, incinc);		// Increase oscillator frequency
	    cmul(osc, inc);		// Oscillator continues around cycle
	    out[0]= funcp(buf1, osc[0]);
	    out[1]= funcp(buf2, osc[1]);
	 }
	 dp[0]= hypot(out[0], out[1]);	 
      }
   }
   
   fid_run_freebuf(buf1);
   fid_run_freebuf(buf2);
   fid_run_free(run);
   return rv;
}

//
//	Blackman window generator initialisation.  'wid' is total
//	end-to-end width of window required, which may be fractional.
//	Returns the number of samples which will be output.
//   

int 
blackman_init(Blackman *bl, double wid) {
   int iwid= (int)floor(fabs(wid/2));
   double step= M_PI / (wid * 0.5);
   bl->cnt= iwid * 2 + 1;
   bl->gen1[0]= sin(2 * step) / sin(step);
   bl->gen1[1]= 0.5 * cos(step * (iwid+1));
   bl->gen1[2]= 0.5 * cos(step * (iwid+2));
   bl->gen2[0]= sin(4 * step) / sin(2 * step);
   bl->gen2[1]= 0.08 * cos(2 * step * (iwid+1));
   bl->gen2[2]= 0.08 * cos(2 * step * (iwid+2));
   return bl->cnt;
}

//
//	Blackman generator.  Outputs blackman window sample by sample.
//	When window is complete, outputs zeros forever.
//   

double 
blackman_gen(Blackman *bl) {
   double out1, out2;
   if (bl->cnt-- <= 0) return 0;
   out1= bl->gen1[0] * bl->gen1[1] - bl->gen1[2];
   bl->gen1[2]= bl->gen1[1]; bl->gen1[1]= out1;
   out2= bl->gen2[0] * bl->gen2[1] - bl->gen2[2];
   bl->gen2[2]= bl->gen2[1]; bl->gen2[1]= out2;
   return 0.42 + out1 + out2;
}

//
//	Initialise a Wavelet structure ready to output a wavelet at
//	the given frequency (0-0.5) and with the given length in
//	wave-periods.  A Blackman window will be used as the wavelet
//	envelope.  The number of samples that will be output is
//	returned.  In the case of very low frequencies, the wavelet
//	can be immensely long; 'max' if non-0 can be used to limit the
//	wavelet length in samples.
//

int 
wavelet_init(Wavelet *ww, double freq, double len) {
   double step, dmy;
   freq= fabs(-0.5 + fabs(modf(freq+0.5, &dmy)));
   step= 2 * M_PI * freq;
   ww->osc[0]= 1.0; 
   ww->osc[1]= 0.0;
   cexpj(ww->inc, step);
   return blackman_init(&ww->bl, len / freq);
}

//
//	Generate the next output value for the wavelet.  The output
//	values are available as ww->out[0] and ww->out[1].
//

void 
wavelet_gen(Wavelet *ww) {
   double amp= blackman_gen(&ww->bl);
   cmul(ww->osc, ww->inc);
   ww->out[0]= ww->osc[0] * amp;
   ww->out[1]= ww->osc[1] * amp;
}

// END //

