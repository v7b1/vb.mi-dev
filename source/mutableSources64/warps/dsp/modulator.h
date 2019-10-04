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
// Modulator.

#ifndef WARPS_DSP_MODULATOR_H_
#define WARPS_DSP_MODULATOR_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "warps/dsp/oscillator.h"
#include "warps/dsp/parameters.h"
#include "warps/dsp/quadrature_oscillator.h"
#include "warps/dsp/quadrature_transform.h"
#include "warps/dsp/sample_rate_converter.h"
#include "warps/dsp/vocoder.h"
#include "warps/resources.h"

namespace warps {

const size_t kMaxBlockSize = 96;
const size_t kOversampling = 6;
const size_t kNumOscillators = 1;

//typedef struct { short l; short r; } ShortFrame;
//typedef struct { double l; double r; } doubleFrame;

class SaturatingAmplifier {
 public:
  SaturatingAmplifier() { }
  ~SaturatingAmplifier() { }
  void Init() {
    drive_ = 0.0;
  }
  
  void Process(
      double drive,
      double limit,
      double* in,
      double* out,
      double* out_raw,
      size_t in_stride,
      size_t size) {
    // Process noise gate and compute raw output
    stmlib::ParameterInterpolator drive_modulation(&drive_, drive, size);
    double level = level_;
    for (size_t i = 0; i < size; ++i) {
      //double s = static_cast<double>(*in) / 32768.0;
        double s = in[i]; //(*in);
      //double error = s * s - level;
      //level += error * (error > 0.0 ? 0.1: 0.0001);
      //s *= level <= 0.0001 ? (1.0 / 0.0001) * level : 1.0;
      out[i] = s;
      out_raw[i] += s * drive_modulation.Next();
      //in += in_stride;
    }
    level_ = level;
    
    // Process overdrive / gain
    double drive_2 = drive * drive;
    double pre_gain_a = drive * 0.5;
    double pre_gain_b = drive_2 * drive_2 * drive * 24.0;
    double pre_gain = pre_gain_a + (pre_gain_b - pre_gain_a) * drive_2;
    double drive_squished = drive * (2.0 - drive);
    double post_gain = 1.0 / stmlib::SoftClip(
          0.33 + drive_squished * (pre_gain - 0.33));
    stmlib::ParameterInterpolator pre_gain_modulation(
        &pre_gain_,
        pre_gain,
        size);
    stmlib::ParameterInterpolator post_gain_modulation(
        &post_gain_,
        post_gain,
        size);
    
    for (size_t i = 0; i < size; ++i) {
      double pre = pre_gain_modulation.Next() * out[i];
      double post = stmlib::SoftClip(pre) * post_gain_modulation.Next();
      out[i] = pre + (post - pre) * limit;
    }
  }

 private:
  double level_;
  double drive_;
  double post_gain_;
  double pre_gain_;
  double unclipped_gain_;

  DISALLOW_COPY_AND_ASSIGN(SaturatingAmplifier);
};

enum XmodAlgorithm {
  ALGORITHM_XFADE,
  ALGORITHM_FOLD,
  ALGORITHM_ANALOG_RING_MODULATION,
  ALGORITHM_DIGITAL_RING_MODULATION,
  ALGORITHM_XOR,
  ALGORITHM_COMPARATOR,
  ALGORITHM_NOP,
  ALGORITHM_LAST
};

class Modulator {
 public:
  typedef void (Modulator::*XmodFn)(
      double balance,
      double balance_end,
      double parameter,
      double parameter_end,
      const double* in_1,
      const double* in_2,
      double* out,
      size_t size);

  Modulator() { }
  ~Modulator() { }

  void Init(double sample_rate);
  //void Process(ShortFrame* input, ShortFrame* output, size_t size);
    void Process(double** input, double** output, size_t size);
  //void ProcessEasterEgg(ShortFrame* input, ShortFrame* output, size_t size);
    void ProcessEasterEgg(double** input, double** output, size_t size);
  inline Parameters* mutable_parameters() { return &parameters_; }
  inline const Parameters& parameters() { return parameters_; }
  
  inline bool bypass() const { return bypass_; }
  inline void set_bypass(bool bypass) { bypass_ = bypass; }

  inline bool easter_egg() const { return easter_egg_; }
  inline void set_easter_egg(bool easter_egg) { easter_egg_ = easter_egg; }
  
 private:
  template<XmodAlgorithm algorithm_1, XmodAlgorithm algorithm_2>
  void ProcessXmod(
      double balance,
      double balance_end,
      double parameter,
      double parameter_end,
      const double* in_1,
      const double* in_2,
      double* out,
      size_t size) {
    double step = 1.0 / static_cast<double>(size);
    double parameter_increment = (parameter_end - parameter) * step;
    double balance_increment = (balance_end - balance) * step;
    while (size) {
      {
        const double x_1 = *in_1++;
        const double x_2 = *in_2++;
        double a = Xmod<algorithm_1>(x_1, x_2, parameter);
        double b = Xmod<algorithm_2>(x_1, x_2, parameter);
        *out++ = a + (b - a) * balance;
        parameter += parameter_increment;
        balance += balance_increment;
        size--;
      }
      {
        const double x_1 = *in_1++;
        const double x_2 = *in_2++;
        double a = Xmod<algorithm_1>(x_1, x_2, parameter);
        double b = Xmod<algorithm_2>(x_1, x_2, parameter);
        *out++ = a + (b - a) * balance;
        parameter += parameter_increment;
        balance += balance_increment;
        size--;
      }
      {
        const double x_1 = *in_1++;
        const double x_2 = *in_2++;
        double a = Xmod<algorithm_1>(x_1, x_2, parameter);
        double b = Xmod<algorithm_2>(x_1, x_2, parameter);
        *out++ = a + (b - a) * balance;
        parameter += parameter_increment;
        balance += balance_increment;
        size--;
      }
    }
  }
  
  template<XmodAlgorithm algorithm>
  static double Xmod(double x_1, double x_2, double parameter);
  
  static double Diode(double x);
  
  bool bypass_;
  bool easter_egg_;
  
  Parameters parameters_;
  Parameters previous_parameters_;
  
  SaturatingAmplifier amplifier_[2];
  Oscillator xmod_oscillator_;
  Oscillator vocoder_oscillator_;
  QuadratureOscillator quadrature_oscillator_;
  
    //SRC_UP, ratio, filter_size
  SampleRateConverter<SRC_UP, kOversampling, 48> src_up_[2];
  SampleRateConverter<SRC_DOWN, kOversampling, 48> src_down_;

  Vocoder vocoder_;
  QuadratureTransform quadrature_transform_[2];
  
  double internal_modulation_[kMaxBlockSize];
  double buffer_[3][kMaxBlockSize];
  double src_buffer_[2][kMaxBlockSize * kOversampling];

  double feedback_sample_;
  
  static XmodFn xmod_table_[];
  
  DISALLOW_COPY_AND_ASSIGN(Modulator);
};

}  // namespace warps

#endif  // WARPS_DSP_MODULATOR_H_
