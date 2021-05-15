// Copyright 2012 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// DSP utility routines.

#ifndef STMLIB_UTILS_DSP_DSP_H_
#define STMLIB_UTILS_DSP_DSP_H_

#include "stmlib/stmlib.h"

#include <cmath>
#include <math.h>

namespace stmlib {

#define MAKE_INTEGRAL_FRACTIONAL(x) \
  int32_t x ## _integral = static_cast<int32_t>(x); \
  double x ## _fractional = x - static_cast<double>(x ## _integral);

inline double Interpolate(const double* table, double index, double size) {
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  double a = table[index_integral];
  double b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}


inline double InterpolateHermite(const double* table, double index, double size) {
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  const double xm1 = table[index_integral - 1];
  const double x0 = table[index_integral + 0];
  const double x1 = table[index_integral + 1];
  const double x2 = table[index_integral + 2];
  const double c = (x1 - xm1) * 0.5;
  const double v = x0 - x1;
  const double w = c + v;
  const double a = w + v + (x2 - x0) * 0.5;
  const double b_neg = w + a;
  const double f = index_fractional;
  return (((a * f) - b_neg) * f + c) * f + x0;
}
/*
inline double InterpolateWrap(const float* table, float index, float size) {
  index -= static_cast<float>(static_cast<int32_t>(index));
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  float a = table[index_integral];
  float b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}
  */
inline double InterpolateWrap(const double* table, double index, double size) {
    index -= static_cast<double>(static_cast<int32_t>(index));
    index *= size;
    MAKE_INTEGRAL_FRACTIONAL(index)
    double a = table[index_integral];
    double b = table[index_integral + 1];
    return a + (b - a) * index_fractional;
}

#define ONE_POLE(out, in, coefficient) out += (coefficient) * ((in) - out);
#define SLOPE(out, in, positive, negative) { \
  double error = (in) - out; \
  out += (error > 0 ? positive : negative) * error; \
}
#define SLEW(out, in, delta) { \
  double error = (in) - out; \
  double d = (delta); \
  if (error > d) { \
    error = d; \
  } else if (error < -d) { \
    error = -d; \
  } \
  out += error; \
}

inline double Crossfade(double a, double b, double fade) {
  return a + (b - a) * fade;
}

inline double SoftLimit(double x) {
  return x * (27.0 + x * x) / (27.0 + 9.0 * x * x);
}

inline double SoftClip(double x) {
  if (x < -3.0) {
    return -1.0;
  } else if (x > 3.0) {
    return 1.0;
  } else {
    return SoftLimit(x);
  }
}

#ifdef TEST
  inline int32_t Clip16(int32_t x) {
    if (x < -32768) {
      return -32768;
    } else if (x > 32767) {
      return 32767;
    } else {
      return x;
    }
  }
  inline uint16_t ClipU16(int32_t x) {
    if (x < 0) {
      return 0;
    } else if (x > 65535) {
      return 65535;
    } else {
      return x;
    }
  }
#else
  inline int32_t Clip16(int32_t x) {
      
    int32_t result = 0;
    __asm ("ssat %0, %1, %2" : "=r" (result) :  "I" (16), "r" (x) );
    return result;
  }
  inline uint32_t ClipU16(int32_t x) {
    uint32_t result;
    __asm ("usat %0, %1, %2" : "=r" (result) :  "I" (16), "r" (x) );
    return result;
  }
#endif
  
#ifdef TEST
  inline double Sqrt(double x) {
    return sqrt(x);
  }
#else
  inline double Sqrt(double x) {
    double result;
    __asm ("vsqrt.f32 %0, %1" : "=w" (result) : "w" (x) );
    return result;
  }
#endif

inline int16_t SoftConvert(double x) {
  return Clip16(static_cast<int32_t>(SoftLimit(x * 0.5) * 32768.0));
}

}  // namespace stmlib

#endif  // STMLIB_UTILS_DSP_DSP_H_
