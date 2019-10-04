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
// Additive synthesis with 32 partials.

#include "plaits/dsp/engine/additive_engine.h"

#include <algorithm>

#include "stmlib/dsp/cosine_oscillator.h"

#include "plaits/resources.h"


namespace plaits {

using namespace std;
using namespace stmlib;

void AdditiveEngine::Init(BufferAllocator* allocator) {
  fill(
      &amplitudes_[0],
      &amplitudes_[kNumHarmonics],
      0.0);
  for (int i = 0; i < kNumHarmonicOscillators; ++i) {
    harmonic_oscillator_[i].Init();
  }
}

void AdditiveEngine::Reset() {
    
}

void AdditiveEngine::UpdateAmplitudes(
    double centroid,
    double slope,
    double bumps,
    double* amplitudes,
    const int* harmonic_indices,
    size_t num_harmonics) {
  const double n = (static_cast<double>(num_harmonics) - 1.0);
  const double margin = (1.0 / slope - 1.0) / (1.0 + bumps);
  const double center = centroid * (n + margin) - 0.5 * margin;

  double sum = 0.001;

  for (size_t i = 0; i < num_harmonics; ++i) {
    double order = fabs(static_cast<double>(i) - center) * slope;
    double gain = 1.0 - order;
    gain += fabs(gain);
    gain *= gain;

    double b = 0.25 + order * bumps;
    double bump_factor = 1.0 + InterpolateWrap(lut_sine, b, 1024.0);

    gain *= bump_factor;
    gain *= gain;
    gain *= gain;
    
    int j = harmonic_indices[i];
    
    // Warning about the following line: this is not a proper LP filter because
    // of the normalization. But in spite of its strange working, this line
    // turns out ot be absolutely essential.
    //
    // I have tried both normalizing the LP-ed spectrum, and LP-ing the
    // normalized spectrum, and both of them cause more annoyances than this
    // "incorrect" solution.
    
    ONE_POLE(amplitudes[j], gain, 0.001);
    sum += amplitudes[j];
  }

  sum = 1.0 / sum;

  for (size_t i = 0; i < num_harmonics; ++i) {
    amplitudes[harmonic_indices[i]] *= sum;
  }
}

inline double Bump(double x, double centroid, double slope) {
  double d = fabs(x - centroid);
  double bump = 1.0 - d * slope;
  return bump + fabs(bump);
}

const int integer_harmonics[24] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23
};

const int organ_harmonics[8] = {
  0, 1, 2, 3, 5, 7, 9, 11
};

void AdditiveEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  const double f0 = NoteToFrequency(parameters.note);

  const double centroid = parameters.timbre;
  const double raw_bumps = parameters.harmonics;
  const double raw_slope = (1.0 - 0.6 * raw_bumps) * parameters.morph;
  const double slope = 0.01 + 1.99 * raw_slope * raw_slope * raw_slope;
  const double bumps = 16.0 * raw_bumps * raw_bumps;
  UpdateAmplitudes(
      centroid,
      slope,
      bumps,
      &amplitudes_[0],
      integer_harmonics,
      24);
  harmonic_oscillator_[0].Render<1>(f0, &amplitudes_[0], out, size);
  harmonic_oscillator_[1].Render<13>(f0, &amplitudes_[12], out, size);

  UpdateAmplitudes(
      centroid,
      slope,
      bumps,
      &amplitudes_[24],
      organ_harmonics,
      8);

  harmonic_oscillator_[2].Render<1>(f0, &amplitudes_[24], aux, size);
}

}  // namespace plaits
