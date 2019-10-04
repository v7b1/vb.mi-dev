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
// Extended Karplus-Strong, with all the niceties from Rings.

#include "plaits/dsp/physical_modelling/string_voice.h"

#include <algorithm>


#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/noise/dust.h"



namespace plaits {

using namespace std;
using namespace stmlib;

void StringVoice::Init(BufferAllocator* allocator) {
  excitation_filter_.Init();
  string_.Init(allocator);
  remaining_noise_samples_ = 0;
}

void StringVoice::Reset() {
  string_.Reset();
}

void StringVoice::Render(
    bool sustain,
    bool trigger,
    double accent,
    double f0,
    double structure,
    double brightness,
    double damping,
    double* temp,
    double* out,
    double* aux,
    size_t size) {
  const double density = brightness * brightness;

  brightness += 0.25 * accent * (1.0 - brightness);
  damping += 0.25 * accent * (1.0 - damping);
  
  // Synthesize excitation signal.
  if (trigger || sustain) {
    const double range = 72.0;
    const double f = 4.0 * f0;
    const double cutoff = min(
        f * SemitonesToRatio((brightness * (2.0 - brightness) - 0.5) * range),
        0.499);
    const double q = sustain ? 1.0 : 0.5;
    remaining_noise_samples_ = static_cast<size_t>(1.0 / f0);
    excitation_filter_.set_f_q<FREQUENCY_DIRTY>(cutoff, q);
  }

  if (sustain) {
    const double dust_f = 0.00005 + 0.99995 * density * density;
    for (size_t i = 0; i < size; ++i) {
      temp[i] = Dust(dust_f) * (8.0 - dust_f * 6.0) * accent;
    }
  } else if (remaining_noise_samples_) {
    size_t noise_samples = min(remaining_noise_samples_, size);
    remaining_noise_samples_ -= noise_samples;
    size_t tail = size - noise_samples;
    double* start = temp;
    while (noise_samples--) {
      *start++ = 2.0 * Random::GetFloat() - 1.0;
    }
    while (tail--) {
      *start++ = 0.0;
    }
  } else {
    fill(&temp[0], &temp[size], 0.0);
  }
  
  excitation_filter_.Process<FILTER_MODE_LOW_PASS>(temp, temp, size);
  for (size_t i = 0; i < size; ++i) {
    aux[i] += temp[i];
  }
  
  double non_linearity = structure < 0.24
      ? (structure - 0.24) * 4.166
      : (structure > 0.26 ? (structure - 0.26) * 1.35135 : 0.0);
  string_.Process(f0, non_linearity, brightness, damping, temp, out, size);
}

}  // namespace plaits
