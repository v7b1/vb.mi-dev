// Copyright 2015 Olivier Gillet.
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
// Group of voices.

#include "rings/dsp/part.h"

#include "stmlib/dsp/units.h"

#include "rings/resources.h"
#include "rings/dsp/dsp.h"

//extern double gSampleRate;
//extern double gA3;

namespace rings {

using namespace std;
using namespace stmlib;
    
    

void Part::Init(uint16_t* reverb_buffer) {
  active_voice_ = 0;
  
  fill(&note_[0], &note_[kMaxPolyphony], 0.0);
  
  bypass_ = false;
  polyphony_ = 1;
  model_ = RESONATOR_MODEL_MODAL;
  dirty_ = true;
  
  for (int32_t i = 0; i < kMaxPolyphony; ++i) {
    excitation_filter_[i].Init();
    plucker_[i].Init();
      dc_blocker_[i].Init(1.0 - 10.0 / Dsp::getSr());
  }
  
  reverb_.Init(reverb_buffer);
  limiter_.Init();

  note_filter_.Init(
      Dsp::getSr() / Dsp::getBlockSize(),
      0.001,  // Lag time with a sharp edge on the V/Oct input or trigger.
      0.010,  // Lag time after the trigger has been received.
      0.050,  // Time to transition from reactive to filtered.
      0.004); // Prevent a sharp edge to partly leak on the previous voice.
}

void Part::ConfigureResonators() {
  if (!dirty_) {
    return;
  }
  
  switch (model_) {
    case RESONATOR_MODEL_MODAL:
      {
        int32_t resolution = 64 / polyphony_ - 4;
        for (int32_t i = 0; i < polyphony_; ++i) {
          resonator_[i].Init();
          resonator_[i].set_resolution(resolution);
        }
      }
      break;
    
    case RESONATOR_MODEL_SYMPATHETIC_STRING:
    case RESONATOR_MODEL_STRING:
    case RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED:
    case RESONATOR_MODEL_STRING_AND_REVERB:
      {
        double lfo_frequencies[kNumStrings] = {
          0.5, 0.4, 0.35, 0.23, 0.211, 0.2, 0.171
        };
        for (int32_t i = 0; i < kNumStrings; ++i) {
          bool has_dispersion = model_ == RESONATOR_MODEL_STRING || \
              model_ == RESONATOR_MODEL_STRING_AND_REVERB;
          string_[i].Init(has_dispersion);

          double f_lfo = double(Dsp::getBlockSize()) / double(Dsp::getSr());
          f_lfo *= lfo_frequencies[i];
          lfo_[i].Init<COSINE_OSCILLATOR_APPROXIMATE>(f_lfo);
        }
        for (int32_t i = 0; i < polyphony_; ++i) {
          plucker_[i].Init();
        }
      }
      break;
    
    case RESONATOR_MODEL_FM_VOICE:
      {
        for (int32_t i = 0; i < polyphony_; ++i) {
          fm_voice_[i].Init();
        }
      }
      break;
    
    default:
      break;
  }

  if (active_voice_ >= polyphony_) {
    active_voice_ = 0;
  }
  dirty_ = false;
}

#ifdef BRYAN_CHORDS

// Chord table by Bryan Noll:
double chords[kMaxPolyphony][11][8] = {
  {
    { -12.0, -0.01, 0.0,  0.01, 0.02, 11.98, 11.99, 12.0 }, // OCT
    { -12.0, -5.0,  0.0,  6.99, 7.0,  11.99, 12.0,  19.0 }, // 5
    { -12.0, -5.0,  0.0,  5.0,  7.0,  11.99, 12.0,  17.0 }, // sus4
    { -12.0, -5.0,  0.0,  3.0,  7.0,   3.01, 12.0,  19.0 }, // m
    { -12.0, -5.0,  0.0,  3.0,  7.0,   3.01, 10.0,  19.0 }, // m7
    { -12.0, -5.0,  0.0,  3.0, 14.0,   3.01, 10.0,  19.0 }, // m9
    { -12.0, -5.0,  0.0,  3.0,  7.0,   3.01, 10.0,  17.0 }, // m11
    { -12.0, -5.0,  0.0,  2.0,  7.0,   9.0,  16.0,  19.0 }, // 69
    { -12.0, -5.0,  0.0,  4.0,  7.0,  11.0,  14.0,  19.0 }, // M9
    { -12.0, -5.0,  0.0,  4.0,  7.0,  11.0,  10.99, 19.0 }, // M7
    { -12.0, -5.0,  0.0,  4.0,  7.0,  11.99, 12.0,  19.0 } // M
  },
  { 
    { -12.0, 0.0,  0.01, 12.0 }, // OCT
    { -12.0, 6.99, 7.0,  12.0 }, // 5
    { -12.0, 5.0,  7.0,  12.0 }, // sus4
    { -12.0, 3.0, 11.99, 12.0 }, // m
    { -12.0, 3.0, 10.0,  12.0 }, // m7
    { -12.0, 3.0, 10.0,  14.0 }, // m9
    { -12.0, 3.0, 10.0,  17.0 }, // m11
    { -12.0, 2.0,  9.0,  16.0 }, // 69
    { -12.0, 4.0, 11.0,  14.0 }, // M9
    { -12.0, 4.0,  7.0,  11.0 }, // M7
    { -12.0, 4.0,  7.0,  12.0 }, // M
  },
  {
    { 0.0, -12.0 },
    { 0.0, 2.0 },
    { 0.0, 3.0 },
    { 0.0, 4.0 },
    { 0.0, 5.0 },
    { 0.0, 7.0 },
    { 0.0, 9.0 },
    { 0.0, 10.0 },
    { 0.0, 11.0 },
    { 0.0, 12.0 },
    { -12.0, 12.0 }
  },
  {
    { 0.0, -12.0 },
    { 0.0, 2.0 },
    { 0.0, 3.0 },
    { 0.0, 4.0 },
    { 0.0, 5.0 },
    { 0.0, 7.0 },
    { 0.0, 9.0 },
    { 0.0, 10.0 },
    { 0.0, 11.0 },
    { 0.0, 12.0 },
    { -12.0, 12.0 }
  }
};

#else

// Original chord table
double chords[kMaxPolyphony][11][8] = {
  {
    { -12.0, 0.0, 0.01, 0.02, 0.03, 11.98, 11.99, 12.0 },
    { -12.0, 0.0, 3.0,  3.01, 7.0,  9.99,  10.0,  19.0 },
    { -12.0, 0.0, 3.0,  3.01, 7.0,  11.99, 12.0,  19.0 },
    { -12.0, 0.0, 3.0,  3.01, 7.0,  13.99, 14.0,  19.0 },
    { -12.0, 0.0, 3.0,  3.01, 7.0,  16.99, 17.0,  19.0 },
    { -12.0, 0.0, 6.98, 6.99, 7.0,  12.00, 18.99, 19.0 },
    { -12.0, 0.0, 3.99, 4.0,  7.0,  16.99, 17.0,  19.0 },
    { -12.0, 0.0, 3.99, 4.0,  7.0,  13.99, 14.0,  19.0 },
    { -12.0, 0.0, 3.99, 4.0,  7.0,  11.99, 12.0,  19.0 },
    { -12.0, 0.0, 3.99, 4.0,  7.0,  10.99, 11.0,  19.0 },
    { -12.0, 0.0, 4.99, 5.0,  7.0,  11.99, 12.0,  17.0 }
  },
  { 
    { -12.0, 0.0, 0.01, 12.0 },
    { -12.0, 3.0, 7.0,  10.0 },
    { -12.0, 3.0, 7.0,  12.0 },
    { -12.0, 3.0, 7.0,  14.0 },
    { -12.0, 3.0, 7.0,  17.0 },
    { -12.0, 7.0, 12.0, 19.0 },
    { -12.0, 4.0, 7.0,  17.0 },
    { -12.0, 4.0, 7.0,  14.0 },
    { -12.0, 4.0, 7.0,  12.0 },
    { -12.0, 4.0, 7.0,  11.0 },
    { -12.0, 5.0, 7.0,  12.0 },
  },
  {
    { 0.0, -12.0 },
    { 0.0, 0.01 },
    { 0.0, 2.0 },
    { 0.0, 3.0 },
    { 0.0, 4.0 },
    { 0.0, 5.0 },
    { 0.0, 7.0 },
    { 0.0, 10.0 },
    { 0.0, 11.0 },
    { 0.0, 12.0 },
    { -12.0, 12.0 }
  },
  {
    { 0.0, -12.0 },
    { 0.0, 0.01 },
    { 0.0, 2.0 },
    { 0.0, 3.0 },
    { 0.0, 4.0 },
    { 0.0, 5.0 },
    { 0.0, 7.0 },
    { 0.0, 10.0 },
    { 0.0, 11.0 },
    { 0.0, 12.0 },
    { -12.0, 12.0 }
  }
};

#endif  // BRYAN_CHORDS

void Part::ComputeSympatheticStringsNotes(
    double tonic,
    double note,
    double parameter,
    double* destination,
    size_t num_strings) {
    
  double notes[9] = {
      tonic,
      note - 12.0,
      note - 7.01955,
      note,
      note + 7.01955,
      note + 12.0,
      note + 19.01955,
      note + 24.0,
      note + 24.0 };
  const double detunings[4] = {
      0.013,
      0.011,
      0.007,
      0.017
  };
  
  if (parameter >= 2.0) {
    // Quantized chords
    int32_t chord_index = parameter - 2.0;
    const double* chord = chords[polyphony_ - 1][chord_index];
    for (size_t i = 0; i < num_strings; ++i) {
      destination[i] = chord[i] + note;
    }
    return;
  }

  size_t num_detuned_strings = (num_strings - 1) >> 1;
  size_t first_detuned_string = num_strings - num_detuned_strings;
  
  for (size_t i = 0; i < first_detuned_string; ++i) {
    double note = 3.0;
    if (i != 0) {
      note = parameter * 7.0;
      parameter += (1.0 - parameter) * 0.2;
    }
    
    MAKE_INTEGRAL_FRACTIONAL(note);
    note_fractional = Squash(note_fractional);

    double a = notes[note_integral];
    double b = notes[note_integral + 1];
    
    note = a + (b - a) * note_fractional;
    destination[i] = note;
    if (i + first_detuned_string < num_strings) {
      destination[i + first_detuned_string] = destination[i] + detunings[i & 3];
    }
  }
}

void Part::RenderModalVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    double frequency,
    double filter_cutoff,
    size_t size) {
  // Internal exciter is a pulse, pre-filter.
  if (performance_state.internal_exciter &&
      voice == active_voice_ &&
      performance_state.strum) {
    resonator_input_[0] += 0.25 * SemitonesToRatio(
        filter_cutoff * filter_cutoff * 24.0) / filter_cutoff;
  }
  
  // Process through filter.
  excitation_filter_[voice].Process<FILTER_MODE_LOW_PASS>(
      resonator_input_, resonator_input_, size);

  Resonator& r = resonator_[voice];
  r.set_frequency(frequency);
  r.set_structure(patch.structure);
  r.set_brightness(patch.brightness * patch.brightness);
  r.set_position(patch.position);
  r.set_damping(patch.damping);
  r.Process(resonator_input_, out_buffer_, aux_buffer_, size);
/*
    for(int i=0; i<kMaxBlockSize; ++i) {
        out_buffer_[i] = aux_buffer_[i] = resonator_input_[i];
    }
     */   
}

void Part::RenderFMVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    double frequency,
    double filter_cutoff,
    size_t size) {
  FMVoice& v = fm_voice_[voice];
  if (performance_state.internal_exciter &&
      voice == active_voice_ &&
      performance_state.strum) {
    v.TriggerInternalEnvelope();
  }

  v.set_frequency(frequency);
  v.set_ratio(patch.structure);
  v.set_brightness(patch.brightness);
  v.set_feedback_amount(patch.position);
  v.set_position(/*patch.position*/ 0.0);
  v.set_damping(patch.damping);
  v.Process(resonator_input_, out_buffer_, aux_buffer_, size);
}

void Part::RenderStringVoice(
    int32_t voice,
    const PerformanceState& performance_state,
    const Patch& patch,
    double frequency,
    double filter_cutoff,
    size_t size) {
  // Compute number of strings and frequency.
  int32_t num_strings = 1;
  double frequencies[kNumStrings];

  if (model_ == RESONATOR_MODEL_SYMPATHETIC_STRING ||
      model_ == RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED) {
    num_strings = 2 * kMaxPolyphony / polyphony_;
    double parameter = model_ == RESONATOR_MODEL_SYMPATHETIC_STRING
        ? patch.structure
        : 2.0 + performance_state.chord;
    ComputeSympatheticStringsNotes(
        performance_state.tonic + performance_state.fm,
        performance_state.tonic + note_[voice] + performance_state.fm,
        parameter,
        frequencies,
        num_strings);
    for (int32_t i = 0; i < num_strings; ++i) {
      frequencies[i] = SemitonesToRatio(frequencies[i] - 69.0) * Dsp::getA3();
    }
  } else {
    frequencies[0] = frequency;
  }

  if (voice == active_voice_) {
    const double gain = 1.0 / Sqrt(static_cast<double>(num_strings) * 2.0);
    for (size_t i = 0; i < size; ++i) {
      resonator_input_[i] *= gain;
    }
  }

  // Process external input.
  excitation_filter_[voice].Process<FILTER_MODE_LOW_PASS>(
      resonator_input_, resonator_input_, size);

  // Add noise burst.
  if (performance_state.internal_exciter) {
    if (voice == active_voice_ && performance_state.strum) {
      plucker_[voice].Trigger(frequency, filter_cutoff * 8.0, patch.position);
    }
    plucker_[voice].Process(noise_burst_buffer_, size);
    for (size_t i = 0; i < size; ++i) {
      resonator_input_[i] += noise_burst_buffer_[i];
    }
  }
  dc_blocker_[voice].Process(resonator_input_, size);
  
  fill(&out_buffer_[0], &out_buffer_[size], 0.0);
  fill(&aux_buffer_[0], &aux_buffer_[size], 0.0);
  
  double structure = patch.structure;
  double dispersion = structure < 0.24
      ? (structure - 0.24) * 4.166
      : (structure > 0.26 ? (structure - 0.26) * 1.35135 : 0.0);
  
  for (int32_t string = 0; string < num_strings; ++string) {
    int32_t i = voice + string * polyphony_;
    String& s = string_[i];
    double lfo_value = lfo_[i].Next();
    
    double brightness = patch.brightness;
    double damping = patch.damping;
    double position = patch.position;
    double glide = 1.0;
    double string_index = static_cast<double>(string) / static_cast<double>(num_strings);
    const double* input = resonator_input_;
    
    if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
      damping *= (2.0 - damping);
    }
    
    // When the internal exciter is used, string 0 is the main
    // source, the other strings are vibrating by sympathetic resonance.
    // When the internal exciter is not used, all strings are vibrating
    // by sympathetic resonance.
    if (string > 0 && performance_state.internal_exciter) {
      brightness *= (2.0 - brightness);
      brightness *= (2.0 - brightness);
      damping = 0.7 + patch.damping * 0.27;
      double amount = (0.5 - fabs(0.5 - patch.position)) * 0.9;
      position = patch.position + lfo_value * amount;
      glide = SemitonesToRatio((brightness - 1.0) * 36.0);
      input = sympathetic_resonator_input_;
    }
    
    s.set_dispersion(dispersion);
    s.set_frequency(frequencies[string], glide);
    s.set_brightness(brightness);
    s.set_position(position);
    s.set_damping(damping + string_index * (0.95 - damping));
    s.Process(input, out_buffer_, aux_buffer_, size);
    
    if (string == 0) {
      // Was 0.1, Ben Wilson -> 0.2
      double gain = 0.2 / static_cast<double>(num_strings);
      for (size_t i = 0; i < size; ++i) {
        double sum = out_buffer_[i] - aux_buffer_[i];
        sympathetic_resonator_input_[i] = gain * sum;
      }
    }
  }
}

const int32_t kPingPattern[] = {
  1, 0, 2, 1, 0, 2, 1, 0
};

void Part::Process(
    const PerformanceState& performance_state,
    const Patch& patch,
    const double* in,
    double* out,
    double* aux,
    size_t size) {

  // Copy inputs to outputs when bypass mode is enabled.
  if (bypass_) {
    copy(&in[0], &in[size], &out[0]);
    copy(&in[0], &in[size], &aux[0]);
    return;
  }
    
  ConfigureResonators();
  
  note_filter_.Process(
      performance_state.note,
      performance_state.strum);
    

  if (performance_state.strum) {
    note_[active_voice_] = note_filter_.stable_note();
    if (polyphony_ > 1 && polyphony_ & 1) {
      active_voice_ = kPingPattern[step_counter_ % 8];
      step_counter_ = (step_counter_ + 1) % 8;
    } else {
      active_voice_ = (active_voice_ + 1) % polyphony_;
    }
  }
  
  note_[active_voice_] = note_filter_.note();

  
  fill(&out[0], &out[size], 0.0);
  fill(&aux[0], &aux[size], 0.0);
    
  for (int32_t voice = 0; voice < polyphony_; ++voice) {
    // Compute MIDI note value, frequency, and cutoff frequency for excitation
    // filter.
    double cutoff = patch.brightness * (2.0 - patch.brightness);
    double note = note_[voice] + performance_state.tonic + performance_state.fm;
      /*
      std::cout << "part -->> performance_state: voice: " << voice << "\n";
      std::cout << "note_: " << note_[voice] << "\n";
      std::cout << "fm: " << performance_state.fm << "\n";
      std::cout << "note: " << performance_state.note << "\n";
      std::cout << "tonic: " << performance_state.tonic << "\n";
      std::cout << "---------\n";
      std::cout << "note: " << note << "\n";
      */
      // TODO: check what's happening here
    double frequency = SemitonesToRatio(note - 69.0) * Dsp::getA3();
    double filter_cutoff_range = performance_state.internal_exciter
      ? frequency * SemitonesToRatio((cutoff - 0.5) * 96.0)
      : 0.4 * SemitonesToRatio((cutoff - 1.0) * 108.0);
    double filter_cutoff = min(voice == active_voice_
      ? filter_cutoff_range
      : (10.0 / Dsp::getSr()), 0.499);
    double filter_q = performance_state.internal_exciter ? 1.5 : 0.8;
      /*
      std::cout << "filter: ---------------\n";
      std::cout << "frequency: " << frequency << "\n";
      std::cout << "cutoff_range: " << filter_cutoff_range << "\n";
      std::cout << "cutoff: " << filter_cutoff << "\n";
      std::cout << "filter q: " << filter_q << "\n";
       */
      
    // Process input with excitation filter. Inactive voices receive silence.
    excitation_filter_[voice].set_f_q<FREQUENCY_DIRTY>(filter_cutoff, filter_q);
    if (voice == active_voice_) {
      copy(&in[0], &in[size], &resonator_input_[0]);
    } else {
      fill(&resonator_input_[0], &resonator_input_[size], 0.0);
    }
    
    if (model_ == RESONATOR_MODEL_MODAL) {
      RenderModalVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    } else if (model_ == RESONATOR_MODEL_FM_VOICE) {
      RenderFMVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    } else {
      RenderStringVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    }
      
    
    if (polyphony_ == 1) {
      // Send the two sets of harmonics / pickups to individual outputs.
      for (size_t i = 0; i < size; ++i) {
        out[i] += out_buffer_[i];
        aux[i] += aux_buffer_[i];
      }
    } else {
      // Dispatch odd/even voices to individual outputs.
      double* destination = voice & 1 ? aux : out;
      for (size_t i = 0; i < size; ++i) {
        destination[i] += out_buffer_[i] - aux_buffer_[i];
      }
    }
  }
  
  if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
    for (size_t i = 0; i < size; ++i) {
      double l = out[i];
      double r = aux[i];
      out[i] = l * patch.position + (1.0 - patch.position) * r;
      aux[i] = r * patch.position + (1.0 - patch.position) * l;
    }
    reverb_.set_amount(0.1 + patch.damping * 0.5);
    reverb_.set_diffusion(0.625);
    reverb_.set_time(0.35 + 0.63 * patch.damping);
    reverb_.set_input_gain(0.2);
    reverb_.set_lp(0.3 + patch.brightness * 0.6);
    reverb_.Process(out, aux, size);
    for (size_t i = 0; i < size; ++i) {
      aux[i] = -aux[i];
    }
  }
  
  // Apply limiter to string output.
  limiter_.Process(out, aux, size, model_gains_[model_]);
}

/* static */
double Part::model_gains_[] = {
  1.4,  // RESONATOR_MODEL_MODAL
  1.0,  // RESONATOR_MODEL_SYMPATHETIC_STRING
  1.4,  // RESONATOR_MODEL_STRING
  0.7,  // RESONATOR_MODEL_FM_VOICE,
  1.0,  // RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED
  1.4,  // RESONATOR_MODEL_STRING_AND_REVERB
};

}  // namespace rings
