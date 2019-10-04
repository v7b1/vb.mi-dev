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
// Clocked noise processed by a filter.

#include "plaits/dsp/engine/particle_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

void ParticleEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < kNumParticles; ++i) {
    particle_[i].Init();
  }
  diffuser_.Init(allocator->Allocate<uint16_t>(8192));
  post_filter_.Init();
}

void ParticleEngine::Reset() {
  diffuser_.Clear();
}

void ParticleEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  const double f0 = NoteToFrequency(parameters.note);
  const double density_sqrt = NoteToFrequency(
      60.0 + parameters.timbre * parameters.timbre * 72.0);
  const double density = density_sqrt * density_sqrt * (1.0 / kNumParticles);
  const double gain = 1.0 / density;
  const double q_sqrt = SemitonesToRatio(parameters.morph >= 0.5
      ? (parameters.morph - 0.5) * 120.0
      : 0.0);
  const double q = 0.5 + q_sqrt * q_sqrt;
  const double spread = 48.0 * parameters.harmonics * parameters.harmonics;
  const double raw_diffusion_sqrt = 2.0 * fabs(parameters.morph - 0.5);
  const double raw_diffusion = raw_diffusion_sqrt * raw_diffusion_sqrt;
  const double diffusion = parameters.morph < 0.5
      ? raw_diffusion
      : 0.0;
  const bool sync = parameters.trigger & TRIGGER_RISING_EDGE;
  
  fill(&out[0], &out[size], 0.0);
  fill(&aux[0], &aux[size], 0.0);
  
  for (int i = 0; i < kNumParticles; ++i) {
    particle_[i].Render(
        sync,
        density,
        gain,
        f0,
        spread,
        q,
        out,
        aux,
        size);
  }
  
  post_filter_.set_f_q<FREQUENCY_DIRTY>(min(f0, 0.49), 0.5);
  post_filter_.Process<FILTER_MODE_LOW_PASS>(out, out, size);
  
  diffuser_.Process(
      0.8 * diffusion * diffusion,
      0.5 * diffusion + 0.25,
      out,
      size);
}

}  // namespace plaits
