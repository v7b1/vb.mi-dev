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

#ifndef PLAITS_DSP_ENGINE_SWARM_ENGINE_H_
#define PLAITS_DSP_ENGINE_SWARM_ENGINE_H_

#include "stmlib/dsp/polyblep.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/dsp/oscillator/string_synth_oscillator.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/resources.h"


namespace plaits {

const int kNumSwarmVoices = 8;

class GrainEnvelope {
 public:
  GrainEnvelope() { }
  ~GrainEnvelope() { }
  
  void Init() {
    from_ = 0.0;
    interval_ = 1.0;
    phase_ = 1.0;
    fm_ = 0.0;
    amplitude_ = 0.5;
    previous_size_ratio_ = 0.0;
      filter_coefficient_ = 0.5; // vb, add initialization
  }
  
  inline void Step(double rate, bool burst_mode, bool start_burst) {
    bool randomize = false;
    if (start_burst) {
      phase_ = 0.5;
      fm_ = 16.0;
      randomize = true;
    } else {
      phase_ += rate * fm_;
      if (phase_ >= 1.0) {
        phase_ -= static_cast<double>(static_cast<int>(phase_));
        randomize = true;
      }
    }
    
    if (randomize) {
      from_ += interval_;
      interval_ = stmlib::Random::GetDouble() - from_;
      // Randomize the duration of the grain.
      if (burst_mode) {
        fm_ *= 0.8 + 0.2 * stmlib::Random::GetDouble();
      } else {
        fm_ = 0.5 + 1.5 * stmlib::Random::GetDouble();
      }
    }
  }
  
  inline double frequency(double size_ratio) const {
    // We approximate two overlapping grains of frequencies f1 and f2
    // By a continuous tone ramping from f1 to f2. This allows a continuous
    // transition between the "grain cloud" and "swarm of glissandi" textures.
    if (size_ratio < 1.0f) {
      return 2.0f * (from_ + interval_ * phase_) - 1.0f;
    } else {
      return from_;
    }
  }
  
  inline double amplitude(double size_ratio) {
    double target_amplitude = 1.0;
    if (size_ratio >= 1.0) {
      double phase = (phase_ - 0.5) * size_ratio;
      CONSTRAIN(phase, -1.0, 1.0);
      double e = stmlib::InterpolateWrap(
          lut_sine, 0.5 * phase + 1.25, 1024.0);
      target_amplitude = 0.5 * (e + 1.0);
    }
    
    if ((size_ratio >= 1.0) ^ (previous_size_ratio_ >= 1.0)) {
      filter_coefficient_ = 0.5;
    }
    filter_coefficient_ *= 0.95;
    
    previous_size_ratio_ = size_ratio;
    ONE_POLE(amplitude_, target_amplitude, 0.5 - filter_coefficient_);
    return amplitude_;
  }
  
 private:
  double from_;
  double interval_;
  double phase_;
  double fm_;
  double amplitude_;
  double previous_size_ratio_;
  double filter_coefficient_;
  
  DISALLOW_COPY_AND_ASSIGN(GrainEnvelope);
};

class AdditiveSawOscillator {
 public:
  AdditiveSawOscillator() { }
  ~AdditiveSawOscillator() { }

  inline void Init() {
    phase_ = 0.0;
    next_sample_ = 0.0;
    frequency_ = 0.01;
    gain_ = 0.0;
  }

  inline void Render(
      double frequency,
      double level,
      double* out,
      size_t size) {
    if (frequency >= kMaxFrequency) {
      frequency = kMaxFrequency;
    }
    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator gain(&gain_, level, size);

    double next_sample = next_sample_;
    double phase = phase_;

    while (size--) {
      double this_sample = next_sample;
      next_sample = 0.0;

      const double frequency = fm.Next();

      phase += frequency;
  
      if (phase >= 1.0) {
        phase -= 1.0;
        double t = phase / frequency;
        this_sample -= stmlib::ThisBlepSample(t);
        next_sample -= stmlib::NextBlepSample(t);
      }

      next_sample += phase;
      *out++ += (2.0 * this_sample - 1.0) * gain.Next();
    }
    phase_ = phase;
    next_sample_ = next_sample;
  }

 private:
  // Oscillator state.
  double phase_;
  double next_sample_;

  // For interpolation of parameters.
  double frequency_;
  double gain_;

  DISALLOW_COPY_AND_ASSIGN(AdditiveSawOscillator);
};

class SwarmVoice {
 public:
  SwarmVoice() { }
  ~SwarmVoice() { }
  
  void Init(double rank) {
    rank_ = rank;
    envelope_.Init();
    saw_.Init();
    sine_.Init();
  }
  
  void Render(
      double f0,
      double density,
      bool burst_mode,
      bool start_burst,
      double spread,
      double size_ratio,
      double* saw,
      double* sine,
      size_t size) {
    envelope_.Step(density, burst_mode, start_burst);
    
    const double scale = 1.0 / kNumSwarmVoices;
    const double amplitude = envelope_.amplitude(size_ratio) * scale;

    const double expo_amount = envelope_.frequency(size_ratio);
    f0 *= stmlib::SemitonesToRatio(48.0 * expo_amount * spread * rank_);
    
    const double linear_amount = rank_ * (rank_ + 0.01) * spread * 0.25;
    f0 *= 1.0 + linear_amount;

    saw_.Render(f0, amplitude, saw, size);
    sine_.Render(f0, amplitude, sine, size);
  };
  
 private:
  double rank_;

  GrainEnvelope envelope_;
  AdditiveSawOscillator saw_;
  FastSineOscillator sine_;
};

class SwarmEngine : public Engine {
 public:
  SwarmEngine() { }
  ~SwarmEngine() { }
  
  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void Render(const EngineParameters& parameters,
      double* out,
      double* aux,
      size_t size,
      bool* already_enveloped);
  
 private:
  SwarmVoice swarm_voice_[kNumSwarmVoices];
  
  DISALLOW_COPY_AND_ASSIGN(SwarmEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_SWARM_ENGINE_H_
