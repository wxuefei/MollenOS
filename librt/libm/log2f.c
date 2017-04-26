/*
* ====================================================
* Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
*
* Developed at SunPro, a Sun Microsystems, Inc. business.
* Permission to use, copy, modify, and distribute this
* software is freely granted, provided that this notice
* is preserved.
* ====================================================
*/

/*
* Float version of e_log2.c.  See the latter for most comments.
*/

#include "private.h"
#include <math.h>

#ifdef _MSC_VER
#pragma warning(disable:4723)
#endif

// VBS
#define float_t float

/*
* Float version of k_log.h.  See the latter for most comments.
*/

static const float
Lg1 = 6.666666666666735130e-01f,  /* 3FE55555 55555593 */
Lg2 = 3.999999999940941908e-01f,  /* 3FD99999 9997FA04 */
Lg3 = 2.857142874366239149e-01f,  /* 3FD24924 94229359 */
Lg4 = 2.222219843214978396e-01f;  /* 3FCC71C5 1D8E78AF */

float
k_log1pf(float f)
{
	float hfsq, s, z, R, w, t1, t2;

	s = f / ((float)2.0 + f);
	z = s*s;
	w = z*z;
	t1 = w*(Lg2 + w*Lg4);
	t2 = z*(Lg1 + w*Lg3);
	R = t2 + t1;
	hfsq = (float)0.5*f*f;
	return s*(hfsq + R);
}

static const float
two25 = 3.3554432000e+07f, /* 0x4c000000 */
ivln2hi = 1.4428710938e+00f, /* 0x3fb8b000 */
ivln2lo = -1.7605285393e-04f; /* 0xb9389ad4 */

static const float zero = 0.0f;

float
__ieee754_log2f(float x)
{
	float f, hfsq, hi, lo, r, y;
	int32_t i, k, hx;

	GET_FLOAT_WORD(hx, x);

	k = 0;
	if (hx < 0x00800000) {			/* x < 2**-126  */
		if ((hx & 0x7fffffff) == 0)
			return -two25 / zero;		/* log(+-0)=-inf */
		if (hx<0) return (x - x) / zero;	/* log(-#) = NaN */
		k -= 25; x *= two25; /* subnormal number, scale up x */
		GET_FLOAT_WORD(hx, x);
	}
	if (hx >= 0x7f800000) return x + x;
	if (hx == 0x3f800000)
		return zero;			/* log(1) = +0 */
	k += (hx >> 23) - 127;
	hx &= 0x007fffff;
	i = (hx + (0x4afb0d)) & 0x800000;
	SET_FLOAT_WORD(x, hx | (i ^ 0x3f800000));	/* normalize x or x/2 */
	k += (i >> 23);
	y = (float)k;
	f = x - (float)1.0;
	hfsq = (float)0.5*f*f;
	r = k_log1pf(f);

	/*
	* We no longer need to avoid falling into the multi-precision
	* calculations due to compiler bugs breaking Dekker's theorem.
	* Keep avoiding this as an optimization.  See e_log2.c for more
	* details (some details are here only because the optimization
	* is not yet available in double precision).
	*
	* Another compiler bug turned up.  With gcc on i386,
	* (ivln2lo + ivln2hi) would be evaluated in float precision
	* despite runtime evaluations using double precision.  So we
	* must cast one of its terms to float_t.  This makes the whole
	* expression have type float_t, so return is forced to waste
	* time clobbering its extra precision.
	*/
	if (sizeof(float_t) > sizeof(float))
		return (r - hfsq + f) * ((float_t)ivln2lo + ivln2hi) + y;

	hi = f - hfsq;
	GET_FLOAT_WORD(hx, hi);
	SET_FLOAT_WORD(hi, hx & 0xfffff000);
	lo = (f - hi) - hfsq + r;
	return (lo + hi)*ivln2lo + lo*ivln2hi + hi*ivln2hi + y;
}

#ifdef _MSC_VER
#pragma warning(default:4723)
#endif
