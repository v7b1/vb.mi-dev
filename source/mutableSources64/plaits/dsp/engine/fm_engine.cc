// Copyright 2016 Olivier Gillet.
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
// Classic 2-op FM found in Braids, Rings and Elements.

#include "plaits/dsp/engine/fm_engine.h"

#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/resources.h"


namespace plaits {

using namespace stmlib;

void FMEngine::Init(BufferAllocator* allocator) {
  carrier_phase_ = 0;
  modulator_phase_ = 0;
  sub_phase_ = 0;

    //int a0 = plaits::Dsp::getA0();
  previous_carrier_frequency_ = a0;
  previous_modulator_frequency_ = a0;
  previous_amount_ = 0.0;
  previous_feedback_ = 0.0;
  previous_sample_ = 0.0;
}

void FMEngine::Reset() {
  
}

inline double FMEngine::SinePM(uint32_t phase, double fm) const {
  phase += (static_cast<uint32_t>((fm + 4.0) * 536870912.0)) << 3;
  uint32_t integral = phase >> 22;
  double fractional = static_cast<double>(phase << 10) / 4294967296.0;
  double a = lut_sine[integral];
  double b = lut_sine[integral + 1];
  return a + (b - a) * fractional;
}

const size_t kOversampling = 4;
const size_t kFirHalfSize = 4;

static const double fir_coefficient[kFirHalfSize] = {
    0.02442415, 0.09297315, 0.16712938, 0.21547332,
};

class Downsampler {
 public:
  Downsampler(double* state) {
    head_ = *state;
    tail_ = 0.0;
    state_ = state;
  }
  ~Downsampler() {
    *state_ = head_;
  }
  inline void Accumulate(int i, double sample) {
    head_ += sample * fir_coefficient[3 - (i & 3)];
    tail_ += sample * fir_coefficient[i & 3];
  }
  
  inline double Read() {
    double value = head_;
    head_ = tail_;
    tail_ = 0.0;
    return value;
  }
 private:
  double head_;
  double tail_;
  double* state_;

  DISALLOW_COPY_AND_ASSIGN(Downsampler);
};

void FMEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  
  // 4x oversampling
  const double note = parameters.note - 24.0;
  
  const double ratio = Interpolate(
      lut_fm_frequency_quantizer,
      parameters.harmonics,
      128.0);
  
  double modulator_note = note + ratio;
  double target_modulator_frequency = NoteToFrequency(modulator_note);
  CONSTRAIN(target_modulator_frequency, 0.0, 0.5);

  // Reduce the maximum FM index for high pitched notes, to prevent aliasing.
  double hf_taming = 1.0 - (modulator_note - 72.0) * 0.025;
  CONSTRAIN(hf_taming, 0.0, 1.0);
  hf_taming *= hf_taming;
  
  ParameterInterpolator carrier_frequency(
      &previous_carrier_frequency_, NoteToFrequency(note), size);
  ParameterInterpolator modulator_frequency(
      &previous_modulator_frequency_, target_modulator_frequency, size);
  ParameterInterpolator amount_modulation(
      &previous_amount_,
      2.0 * parameters.timbre * parameters.timbre * hf_taming,
      size);
  ParameterInterpolator feedback_modulation(
      &previous_feedback_, 2.0 * parameters.morph - 1.0, size);
  
  Downsampler carrier_downsampler(&carrier_fir_);
  Downsampler sub_downsampler(&sub_fir_);
  
  while (size--) {
    const double amount = amount_modulation.Next();
    const double feedback = feedback_modulation.Next();
    double phase_feedback = feedback < 0.0 ? 0.5 * feedback * feedback : 0.0;
    const uint32_t carrier_increment = static_cast<uint32_t>(
        4294967296.0 * carrier_frequency.Next());
    double _modulator_frequency = modulator_frequency.Next();

    for (size_t j = 0; j < kOversampling; ++j) {
      modulator_phase_ += static_cast<uint32_t>(4294967296.0 * \
           _modulator_frequency * (1.0 + previous_sample_ * phase_feedback));
      carrier_phase_ += carrier_increment;
      sub_phase_ += carrier_increment >> 1;
      double modulator_fb = feedback > 0.0 ? 0.25 * feedback * feedback : 0.0;
      double modulator = SinePM(
          modulator_phase_, modulator_fb * previous_sample_);
      double carrier = SinePM(carrier_phase_, amount * modulator);
      double sub = SinePM(sub_phase_, amount * carrier * 0.25);
      ONE_POLE(previous_sample_, carrier, 0.05);
      carrier_downsampler.Accumulate(j, carrier);
      sub_downsampler.Accumulate(j, sub);
    }
    
    *out++ = carrier_downsampler.Read();
    *aux++ = sub_downsampler.Read();
  }
}

}  // namespace plaits
