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
// Swarm of sawtooths and sines.

#include "plaits/dsp/engine/swarm_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

void SwarmEngine::Init(BufferAllocator* allocator) {
  const double n = (kNumSwarmVoices - 1) / 2;
  for (int i = 0; i < kNumSwarmVoices; ++i) {
    double rank = (static_cast<double>(i) - n) / n;
    swarm_voice_[i].Init(rank);
  }
}

void SwarmEngine::Reset() { }

void SwarmEngine::Render(
    const EngineParameters& parameters,
    double* out,
    double* aux,
    size_t size,
    bool* already_enveloped) {
  const double f0 = NoteToFrequency(parameters.note);
  const double control_rate = static_cast<double>(size);
  const double density = NoteToFrequency(parameters.timbre * 120.0) * \
      0.025 * control_rate;
  const double spread = parameters.harmonics * parameters.harmonics * \
      parameters.harmonics;
  double size_ratio = 0.25 * SemitonesToRatio(
      (1.0 - parameters.morph) * 84.0);
  
  const bool burst_mode = !(parameters.trigger & TRIGGER_UNPATCHED);
  const bool start_burst = parameters.trigger & TRIGGER_RISING_EDGE;

  fill(&out[0], &out[size], 0.0);
  fill(&aux[0], &aux[size], 0.0);
  
  for (int i = 0; i < kNumSwarmVoices; ++i) {
    swarm_voice_[i].Render(
        f0,
        density,
        burst_mode,
        start_burst,
        spread,
        size_ratio,
        out,
        aux,
        size);
    size_ratio *= 0.97;
  }
}

}  // namespace plaits
