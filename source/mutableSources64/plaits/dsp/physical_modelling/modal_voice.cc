// Copyright 2016 Emilie Gillet.
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
// Simple modal synthesis voice with a mallet exciter:
// click -> LPF -> resonator.
// 
// The click is replaced by continuous white noise when the trigger input
// of the module is not patched.

#include "plaits/dsp/physical_modelling/modal_voice.h"

#include <algorithm>

#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/noise/dust.h"



namespace plaits {

using namespace std;
using namespace stmlib;

void ModalVoice::Init() {
  excitation_filter_.Init();
  resonator_.Init(0.015, kMaxNumModes);
}

void ModalVoice::Render(
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
  
  const double range = sustain ? 36.0 : 60.0;
  const double f = sustain ? 4.0 * f0 : 2.0 * f0;
  const double cutoff = min(
      f * SemitonesToRatio((brightness * (2.0 - brightness) - 0.5) * range),
      0.499);
  const double q = sustain ? 0.7 : 1.5;
  
  // Synthesize excitation signal.
  if (sustain) {
    const double dust_f = 0.00005 + 0.99995 * density * density;
    for (size_t i = 0; i < size; ++i) {
      temp[i] = Dust(dust_f) * (4.0 - dust_f * 3.0) * accent;
    }
  } else {
    fill(&temp[0], &temp[size], 0.0);
    if (trigger) {
      const double attenuation = 1.0 - damping * 0.5;
      const double amplitude = (0.12f + 0.08f * accent) * attenuation;
      temp[0] = amplitude * SemitonesToRatio(cutoff * cutoff * 24.0) / cutoff;
    }
  }
  const double one = 1.0;
  excitation_filter_.Process<FILTER_MODE_LOW_PASS, false>(
      &cutoff, &q, &one, temp, temp, size);
  for (size_t i = 0; i < size; ++i) {
    aux[i] += temp[i];
  }
  
  resonator_.Process(f0, structure, brightness, damping, temp, out, size);
}

}  // namespace plaits
