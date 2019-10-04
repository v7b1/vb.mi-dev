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
// Vocoder.

#include "warps/dsp/vocoder.h"

#include <algorithm>

#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/units.h"

namespace warps {

using namespace std;
using namespace stmlib;

void Vocoder::Init(double sample_rate) {
  modulator_filter_bank_.Init(sample_rate);
  carrier_filter_bank_.Init(sample_rate);
  limiter_.Init();

  release_time_ = 0.5;
  formant_shift_ = 0.5;
  
  BandGain zero;
  zero.carrier = 0.0;
  zero.vocoder = 0.0;
  fill(&previous_gain_[0], &previous_gain_[kNumBands], zero);
  fill(&gain_[0], &gain_[kNumBands], zero);
  
  for (int32_t i = 0; i < kNumBands; ++i) {
    follower_[i].Init();
  }
}

void Vocoder::Process(
    const double* modulator,
    const double* carrier,
    double* out,
    size_t size) {
  // Run through filter banks.
    
    modulator_filter_bank_.Analyze(modulator, size);
    carrier_filter_bank_.Analyze(carrier, size);        
  
  // Set the attack/release release_time of envelope followers.
  double f = 80.0 * SemitonesToRatio(-72.0 * release_time_);
  for (int32_t i = 0; i < kNumBands; ++i) {
    double decay = f / modulator_filter_bank_.band(i).sample_rate;
    follower_[i].set_attack(decay * 2.0);
    follower_[i].set_decay(decay * 0.5);
    follower_[i].set_freeze(release_time_ > 0.995);
    f *= 1.2599;  // 2 ** (4/12.0), a third octave.
  }
  
  // Compute the amplitude (or modulation amount) in all bands.
  double formant_shift_amount = 2.0 * fabs(formant_shift_ - 0.5);
  formant_shift_amount *= (2.0 - formant_shift_amount);
  formant_shift_amount *= (2.0 - formant_shift_amount);
  double envelope_increment = 4.0 * SemitonesToRatio(-48.0 * formant_shift_);
  double envelope = 0.0;
  const double kLastBand = kNumBands - 1.0001;
  for (int32_t i = 0; i < kNumBands; ++i) {
    double source_band = envelope;
    CONSTRAIN(source_band, 0.0, kLastBand);
    MAKE_INTEGRAL_FRACTIONAL(source_band);
    double a = follower_[source_band_integral].peak();
    double b = follower_[source_band_integral + 1].peak();
    double band_gain = (a + (b - a) * source_band_fractional);
    double attenuation = envelope - kLastBand;
    if (attenuation >= 0.0) {
      band_gain *= 1.0 / (1.0 + 1.0 * attenuation);
    }
    envelope += envelope_increment;

    gain_[i].carrier = band_gain * formant_shift_amount;
    gain_[i].vocoder = 1.0 - formant_shift_amount;
  }
        
  for (int32_t i = 0; i < kNumBands; ++i) {
    size_t band_size = size / modulator_filter_bank_.band(i).decimation_factor;
    const double step = 1.0 / static_cast<double>(band_size);

    double* carrier = carrier_filter_bank_.band(i).samples;
    double* modulator = modulator_filter_bank_.band(i).samples;
      double* envelope = tmp_;

    follower_[i].Process(modulator, envelope, band_size);
    
    double vocoder_gain = previous_gain_[i].vocoder;
    double vocoder_gain_increment = (gain_[i].vocoder - vocoder_gain) * step;
    double carrier_gain = previous_gain_[i].carrier;
    double carrier_gain_increment = (gain_[i].carrier - carrier_gain) * step;
    for (size_t j = 0; j < band_size; ++j) {
      carrier[j] *= (carrier_gain + vocoder_gain * envelope[j]);
      vocoder_gain += vocoder_gain_increment;
      carrier_gain += carrier_gain_increment;
    }
    
    previous_gain_[i] = gain_[i];
  }
   

  carrier_filter_bank_.Synthesize(out, size);
  limiter_.Process(out, 1.4, size);
     
}

}  // namespace warps
