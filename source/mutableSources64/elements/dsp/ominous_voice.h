// Copyright 2014 Emilie Gillet.
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
// "Ominous" is a dark 2x2-op FM synth (based on Braids' FM and FBFM modes).

#ifndef ELEMENTS_DSP_OMINOUS_VOICE_H_
#define ELEMENTS_DSP_OMINOUS_VOICE_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"

#include "elements/dsp/dsp.h"
#include "elements/dsp/multistage_envelope.h"
#include "elements/dsp/patch.h"
#include "elements/resources.h"

namespace elements {

const double kOversamplingDownMidi = -36.0;
const size_t kOversamplingUp = 8;

const size_t kNumOscillators = 2;

template<int32_t filter_size, int32_t buffer_size, int32_t ratio>
class FIRDownsampler {
 public:
  FIRDownsampler() { }
  ~FIRDownsampler() { }
  void Init(const double* filter_coefficients) {
    coefficients_ = filter_coefficients;
    std::fill(&buffer_[0], &buffer_[buffer_size * 2], 0.0);
    ptr_ = 0;
  }
  // size is expected to be a multiple of the downsampling ratio.
  void Process(const double* in, double* out, size_t size) {
    while (size) {
      for (int32_t i = 0; i < ratio; ++i) {
        buffer_[ptr_ + buffer_size] = buffer_[ptr_] = *in++;
        ptr_ = (ptr_ + (buffer_size - 1)) & (buffer_size - 1);
        size--;
      }
      double s = 0.0;
      for (int32_t i = 0; i < filter_size; ++i) {
        s += buffer_[ptr_ + i + 1] * coefficients_[i];
      }
      *out++ = s;
    }
  }
  
 private:
  int32_t ptr_;
  const double* coefficients_;
  double buffer_[buffer_size * 2];
  
  DISALLOW_COPY_AND_ASSIGN(FIRDownsampler);
};

class Spatializer {
 public:
  Spatializer() { }
  ~Spatializer() { }
  void Init(double fixed_position);

  inline void Rotate(double rotation_speed) {
    angle_ += rotation_speed;
    if (angle_ >= 1.0) {
      angle_ -= 1.0;
    }
    if (angle_ < 0.0) {
      angle_ += 1.0;
    }
  }
  
  inline void set_distance(double distance) {
    distance_ = distance;
  }

  void Process(double* source, double* center, double* sides, size_t size);

 private:
  double behind_[kMaxBlockSize];
  double left_;
  double right_;
  double angle_;
  double distance_;
  double fixed_position_;
  
  stmlib::NaiveSvf behind_filter_;
  
  DISALLOW_COPY_AND_ASSIGN(Spatializer);
};

class FmOscillator {
 public:
  FmOscillator() { }
  ~FmOscillator() { }
  void Init() {
    fm_amount_ = 0.0;
    previous_sample_ = 0.0;
  }

  void Process(double frequency,
      double ratio,
      double feedback_amount,
      double target_fm_amount,
      const double* external,
      double* destination,
      size_t size);

 private:
  inline double midi_to_increment(double midi_pitch) const {
    int32_t pitch = static_cast<int32_t>(midi_pitch * 256.0);
    pitch = 32768 + stmlib::Clip16(pitch - 20480);
    double increment = lut_midi_to_increment_high[pitch >> 8] * \
        lut_midi_to_f_low[pitch & 0xff];
    return increment;
  }
  
  inline double Sine(uint32_t phase) const {
    uint32_t integral = phase >> 20;
    double fractional = static_cast<double>(phase << 12) / 4294967296.0;
    double a = lut_sine[integral];
    double b = lut_sine[integral + 1];
    return a + (b - a) * fractional;
  }
  
  inline double SineFm(uint32_t phase, double fm) const {
    phase += static_cast<uint32_t>(fm * 2147483648.0);
    uint32_t integral = phase >> 20;
    double fractional = static_cast<double>(phase << 12) / 4294967296.0;
    double a = lut_sine[integral];
    double b = lut_sine[integral + 1];
    return a + (b - a) * fractional;
  }
  
  double fm_amount_;
  double previous_sample_;
  uint32_t phase_carrier_;
  uint32_t phase_mod_;
   
  DISALLOW_COPY_AND_ASSIGN(FmOscillator);
};

class OminousVoice {
 public:
  OminousVoice() { }
  ~OminousVoice() { }
  
  void Init();
  void Process(
      const Patch& patch,
      double frequency,
      double strength,
      const bool gate_in,
      const double* blow_in,
      const double* strike_in,
      double* raw,
      double* center,
      double* sides,
      size_t size);
  
 private:
  void ConfigureEnvelope(const Patch& patch);

  template<int up>
  void Upsample(
      double* state,
      const double* source,
      double* destination,
      size_t source_size) {
    const double down = 1.0 / double(up);
    double s = *state;
    for (size_t i = 0; i < source_size; ++i) {
      double increment = (source[i] - s) * down;
      for (size_t j = 0; j < up; ++j) {
        *destination++ = s;
        s += increment;
      }
    }
    *state = s;
  }

  inline uint8_t GetGateFlags(bool gate_in) {
    uint8_t flags = 0;
    if (gate_in) {
      if (!previous_gate_) {
        flags |= ENVELOPE_FLAG_RISING_EDGE;
      }
      flags |= ENVELOPE_FLAG_GATE;
    } else if (previous_gate_) {
      flags = ENVELOPE_FLAG_FALLING_EDGE;
    }
    previous_gate_ = gate_in;
    return flags;
  }
  
  inline double midi_to_frequency(double midi_pitch) const {
    if (midi_pitch < -12.0) {
      midi_pitch = -12.0;
    }
    int32_t pitch = static_cast<int32_t>(midi_pitch * 256.0);
    pitch = 32768 + stmlib::Clip16(pitch - 20480);
    //return lut_midi_to_f_high[pitch >> 8] * lut_midi_to_f_low[pitch & 0xff];
      return lut_midi_to_f_high[pitch >> 8] * lut_midi_to_f_low[pitch & 0xff] * Dsp::getSrFactor();  // vb
  }
  
  double external_fm_oversampled_[kOversamplingUp * kMaxBlockSize];
  double osc_oversampled_[kOversamplingUp * kMaxBlockSize];
  double osc_[kMaxBlockSize];
  
  bool previous_gate_;
  MultistageEnvelope envelope_;

  double level_[kMaxBlockSize];
  double level_state_;
  double damping_;
  
  double feedback_;
  
  double osc_level_[kNumOscillators];

  double external_fm_state_[kNumOscillators];
  
  FmOscillator oscillator_[kNumOscillators];
  
  stmlib::NaiveSvf iir_downsampler_[kNumOscillators];
  FIRDownsampler<101, 128, kOversamplingUp> fir_downsampler_[kNumOscillators];

  stmlib::Svf filter_[kNumOscillators];
  
  Spatializer spatializer_[kNumOscillators];
  
  DISALLOW_COPY_AND_ASSIGN(OminousVoice);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_OMINOUS_VOICE_H_
