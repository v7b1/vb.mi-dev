// Copyright 2014 Olivier Gillet.
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Fast reciprocal of square-root routines.

#ifndef STMLIB_DSP_RSQRT_H_
#define STMLIB_DSP_RSQRT_H_

#include "stmlib/stmlib.h"

namespace stmlib {

template<typename To, typename From>
struct unsafe_bit_cast_t {
  union {
    From from;
    To to;
  };
};

template<typename To, typename From>
To unsafe_bit_cast(From from) {
    unsafe_bit_cast_t<To, From> u;
    u.from = from;
    return u.to;
}


static inline double fast_rsqrt_carmack(double x) {
  uint32_t i;
  double x2, y;
  const double threehalfs = 1.5;
  y = x;
  i = unsafe_bit_cast<uint32_t, double>(y);
  i = 0x5f3759df - (i >> 1);
  y = unsafe_bit_cast<double, uint32_t>(i);
  x2 = x * 0.5;
  y = y * (threehalfs - (x2 * y * y));
	return y;
}

static inline double fast_rsqrt_accurate(double fp0) {
  double _min = 1.0e-38;
  double _1p5 = 1.5;
  double fp1, fp2, fp3;

  uint32_t q = unsafe_bit_cast<uint32_t, double>(fp0);
  fp2 = unsafe_bit_cast<double, uint32_t>(0x5F3997BB - ((q >> 1) & 0x3FFFFFFF));
  fp1 = _1p5 * fp0 - fp0;
  fp3 = fp2 * fp2;
  if (fp0 < _min) {
    return fp0 > 0 ? fp2 : 1000.0;
  }
  fp3 = _1p5 - fp1 * fp3;
  fp2 = fp2 * fp3;
  fp3 = fp2 * fp2;
  fp3 = _1p5 - fp1 * fp3;
  fp2 = fp2 * fp3;
  fp3 = fp2 * fp2;
  fp3 = _1p5 - fp1 * fp3;
  return fp2 * fp3;
}

}  // namespace stmlib

#endif  // STMLIB_DSP_RSQRT_H_
