// Copyright 2015 Emilie Gillet.
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
// FM Voice.

#include "rings/dsp/fm_voice.h"

#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

#include "rings/resources.h"


namespace rings {

using namespace stmlib;

void FMVoice::Init() {
  set_frequency(220.0 / Dsp::getSr());
  set_ratio(0.5);
  set_brightness(0.5);
  set_damping(0.5);
  set_position(0.5);
  set_feedback_amount(0.0);
  
  previous_carrier_frequency_ = carrier_frequency_;
  previous_modulator_frequency_ = carrier_frequency_;
  previous_brightness_ = brightness_;
  previous_damping_ = damping_;
  previous_feedback_amount_ = feedback_amount_;
  
  amplitude_envelope_ = 0.0;
  brightness_envelope_ = 0.0;
  
  carrier_phase_ = 0;
  modulator_phase_ = 0;
  gain_ = 0.0;
  fm_amount_ = 0.0;
  
  follower_.Init(
      8.0 / Dsp::getSr(),
      160.0 / Dsp::getSr(),
      1600.0 / Dsp::getSr());
}

void FMVoice::Process(const double* in, double* out, double* aux, size_t size) {
  // Interpolate between the "oscillator" behaviour and the "FMLPGed thing"
  // behaviour.
  double envelope_amount = damping_ < 0.9 ? 1.0 : (1.0 - damping_) * 10.0;
  double amplitude_rt60 = 0.1 * SemitonesToRatio(damping_ * 96.0) * Dsp::getSr();
  double amplitude_decay = 1.0 - pow(0.001, 1.0 / amplitude_rt60);

  double brightness_rt60 = 0.1 * SemitonesToRatio(damping_ * 84.0) * Dsp::getSr();
  double brightness_decay = 1.0 - pow(0.001, 1.0 / brightness_rt60);
  
  double ratio = Interpolate(lut_fm_frequency_quantizer, ratio_, 128.0);
  double modulator_frequency = carrier_frequency_ * SemitonesToRatio(ratio);
  
  if (modulator_frequency > 0.5) {
    modulator_frequency = 0.5;
  }
  
  double feedback = (feedback_amount_ - 0.5) * 2.0;
  
  ParameterInterpolator carrier_increment(
      &previous_carrier_frequency_, carrier_frequency_, size);
  ParameterInterpolator modulator_increment(
      &previous_modulator_frequency_, modulator_frequency, size);
  ParameterInterpolator brightness(
      &previous_brightness_, brightness_, size);
  ParameterInterpolator feedback_amount(
      &previous_feedback_amount_, feedback, size);

  uint32_t carrier_phase = carrier_phase_;
  uint32_t modulator_phase = modulator_phase_;
  double previous_sample = previous_sample_;
  
  while (size--) {
    // Envelope follower and internal envelope.
    double amplitude_envelope, brightness_envelope;
    follower_.Process(
        *in++,
        &amplitude_envelope,
        &brightness_envelope);
    
    brightness_envelope *= 2.0 * amplitude_envelope * (2.0 - amplitude_envelope);
    
    SLOPE(amplitude_envelope_, amplitude_envelope, 0.05, amplitude_decay);
    SLOPE(brightness_envelope_, brightness_envelope, 0.01, brightness_decay);
    
    // Compute envelopes.
    double brightness_value = brightness.Next();
    brightness_value *= brightness_value;
    double fm_amount_min = brightness_value < 0.5
        ? 0.0
        : brightness_value * 2.0 - 1.0;
    double fm_amount_max = brightness_value < 0.5
        ? 2.0 * brightness_value
        : 1.0;
    double fm_envelope = 0.5 + envelope_amount * (brightness_envelope_ - 0.5);
    double fm_amount = (fm_amount_min + fm_amount_max * fm_envelope) * 2.0;
    SLEW(fm_amount_, fm_amount, 0.005 + fm_amount_max * 0.015);

    // FM synthesis in itself
    double phase_feedback = feedback < 0.0 ? 0.5 * feedback * feedback : 0.0;
    modulator_phase += static_cast<uint32_t>(4294967296.0 * \
      modulator_increment.Next() * (1.0 + previous_sample * phase_feedback));
    carrier_phase += static_cast<uint32_t>(4294967296.0 * \
        carrier_increment.Next());

    double feedback = feedback_amount.Next();
    double modulator_fb = feedback > 0.0 ? 0.25 * feedback * feedback : 0.0;
    double modulator = SineFm(modulator_phase, modulator_fb * previous_sample);
    double carrier = SineFm(carrier_phase, fm_amount_ * modulator);
    ONE_POLE(previous_sample, carrier, 0.1);

    // Compute amplitude envelope.
    double gain = 1.0 + envelope_amount * (amplitude_envelope_ - 1.0);
    ONE_POLE(gain_, gain, 0.005 + 0.045 * fm_amount_);
    
    *out++ = (carrier + 0.5 * modulator) * gain_;
    *aux++ = 0.5 * modulator * gain_;
  }
  carrier_phase_ = carrier_phase;
  modulator_phase_ = modulator_phase;
  previous_sample_ = previous_sample;
}

}  // namespace rings
