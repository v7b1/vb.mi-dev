// Copyright 2021 Emilie Gillet.
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
// Various "magic" conversion functions for DX7 patch data.

#ifndef PLAITS_DSP_DX_UNITS_H_
#define PLAITS_DSP_DX_UNITS_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include <algorithm>
#include <cmath>

#include "plaits/dsp/fm/patch.h"

namespace plaits {

namespace fm {

extern const double lut_cube_root[17];
extern const double lut_amp_mod_sensitivity[4];
extern const double lut_pitch_mod_sensitivity[8];
extern const double lut_coarse[32];

// Computes 2^x by using a polynomial approximation of 2^frac(x) and directly
// incrementing the exponent of the IEEE 754 representation of the result
// by int(x). Depending on the use case, the order of the polynomial
// approximation can be chosen.
template<int order>
inline double Pow2Fast(double x) {
  union {
    double f;
    int32_t w;
  } r;


  if (order == 1) {
    r.w = double(1 << 23) * (127.0 + x);
    return r.f;
  }

  int32_t x_integral = static_cast<int32_t>(x);
  if (x < 0.0) {
    --x_integral;
  }
  x -= static_cast<double>(x_integral);

  if (order == 1) {
    r.f = 1.0 + x;
  } else if (order == 2) {
    r.f = 1.0 + x * (0.6565 + x * 0.3435);
  } else if (order == 3) {
    r.f = 1.0 + x * (0.6958 + x * (0.2251 + x * 0.0791));
  }
  r.w += x_integral << 23;
  return r.f;
}

// Convert an operator (envelope) level from 0-99 to the complement of the
// "TL" value.
//   0 =   0  (TL = 127)
//  20 =  48  (TL =  79)
//  50 =  78  (TL =  49)
//  99 = 127  (TL =   0)
inline int OperatorLevel(int level) {
  int tlc = int(level);
  if (level < 20) {
    tlc = tlc < 15 ? (tlc * (36 - tlc)) >> 3 : 27 + tlc;
  } else {
    tlc += 28;
  }
  return tlc;
}

// Convert an envelope level from 0-99 to an octave shift.
//  0 = -4 octave
// 18 = -1 octave
// 50 =  0
// 82 = +1 octave
// 99 = +4 octave
inline double PitchEnvelopeLevel(int level) {
  double l = (double(level) - 50.0) / 32.0;
  double tail = std::max(std::abs(l + 0.02) - 1.0, 0.0);
  return l * (1.0 + tail * tail * 5.3056);
}

// Convert an operator envelope rate from 0-99 to a frequency.
inline double OperatorEnvelopeIncrement(int rate) {
  int rate_scaled = (rate * 41) >> 6;
  int mantissa = 4 + (rate_scaled & 3);
  int exponent = 2 + (rate_scaled >> 2);
  return double(mantissa << exponent) / double(1 << 24);
}

// Convert a pitch envelope rate from 0-99 to a frequency.
inline double PitchEnvelopeIncrement(int rate) {
  double r = double(rate) * 0.01;
  return (1.0 + 192.0 * r * (r * r * r * r + 0.3333)) / (21.3 * 44100.0);
}

const double kMinLFOFrequency = 0.005865;

// Convert an LFO rate from 0-99 to a frequency.
inline double LFOFrequency(int rate) {
  int rate_scaled = rate == 0 ? 1 : (rate * 165) >> 6;
  rate_scaled *= rate_scaled < 160 ? 11 : (11 + ((rate_scaled - 160) >> 4));
  return double(rate_scaled) * kMinLFOFrequency;
}

// Convert an LFO delay from 0-99 to the two increments.
inline void LFODelay(int delay, double increments[2]) {
  if (delay == 0) {
    increments[0] = increments[1] = 100000.0;
  } else {
    int d = 99 - delay;
    d = (16 + (d & 15)) << (1 + (d >> 4));
    increments[0] = double(d) * kMinLFOFrequency;
    increments[1] = double(std::max(0x80, d & 0xff80)) * kMinLFOFrequency;
  }
}

// Pre-process the velocity to easily compute the velocity scaling.
inline double NormalizeVelocity(double velocity) {
  // double cube_root = stmlib::Sqrt(
  //     0.7f * stmlib::Sqrt(velocity) + 0.3f * velocity);
  const double cube_root = stmlib::Interpolate(lut_cube_root, velocity, 16);
  return 16.0 * (cube_root - 0.918);
}

// MIDI note to envelope increment ratio.
inline double RateScaling(double note, int rate_scaling) {
  return Pow2Fast<1>(
      double(rate_scaling) * (note * 0.33333 - 7.0) * 0.03125);
}

// Operator amplitude modulation sensitivity (0-3).
inline double AmpModSensitivity(int amp_mod_sensitivity) {
  return lut_amp_mod_sensitivity[amp_mod_sensitivity];
}

// Pitch modulation sensitivity (0-7).
inline double PitchModSensitivity(int pitch_mod_sensitivity) {
  return lut_pitch_mod_sensitivity[pitch_mod_sensitivity];
}

// Keyboard tracking to TL adjustment.
inline double KeyboardScaling(double note, const Patch::KeyboardScaling& ks) {
  const double x = note - double(ks.break_point) - 15.0;
  const int curve = x > 0.0 ? ks.right_curve : ks.left_curve;

  double t = std::abs(x);
  if (curve == 1 || curve == 2) {
    t = std::min(t * 0.010467f, 1.0);
    t = t * t * t;
    t *= 96.0;
  }
  if (curve < 2) {
    t = -t;
  }

  double depth = double(x > 0.0 ? ks.right_depth : ks.left_depth);
  return t * depth * 0.02677;
}

inline double FrequencyRatio(const Patch::Operator& op) {
  const double detune = op.mode == 0 && op.fine
      ? 1.0 + 0.01 * double(op.fine)
      : 1.0;

  double base = op.mode == 0
      ? lut_coarse[op.coarse]
      : double(int(op.coarse & 3) * 100 + op.fine) * 0.39864;
  base += (double(op.detune) - 7.0) * 0.015;

  return stmlib::SemitonesToRatioSafe(base) * detune;
}

}  // namespace fm

}  // namespace plaits

#endif  // PLAITS_DSP_DX_UNITS_H_
