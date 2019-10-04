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
// Group of voices.

#include "elements/dsp/part.h"

#include "elements/resources.h"

namespace elements {

using namespace std;
using namespace stmlib;

void Part::Init(uint16_t* reverb_buffer) {
  patch_.exciter_envelope_shape = 1.0;
  patch_.exciter_bow_level = 0.0;
  patch_.exciter_bow_timbre = 0.5;
  patch_.exciter_blow_level = 0.0;
  patch_.exciter_blow_meta = 0.5;
  patch_.exciter_blow_timbre = 0.5;
  patch_.exciter_strike_level = 0.8;
  patch_.exciter_strike_meta = 0.5;
  patch_.exciter_strike_timbre = 0.5;
  patch_.exciter_signature = 0.0;
  patch_.resonator_geometry = 0.2;
  patch_.resonator_brightness = 0.5;
  patch_.resonator_damping = 0.25;
  patch_.resonator_position = 0.3;
  patch_.resonator_modulation_frequency = 0.5 / Dsp::getSr();
  patch_.resonator_modulation_offset = 0.1;
  patch_.reverb_diffusion = 0.625;
  patch_.reverb_lp = 0.7;
  patch_.space = 0.5;
  previous_gate_ = false;
  active_voice_ = 0;
  
  fill(&silence_[0], &silence_[kMaxBlockSize], 0.0);
  fill(&note_[0], &note_[kNumVoices], 69.0);
  
  for (size_t i = 0; i < kNumVoices; ++i) {
    voice_[i].Init();
    ominous_voice_[i].Init();
  }
  
  reverb_.Init(reverb_buffer);
  
  scaled_exciter_level_ = 0.0;
  scaled_resonator_level_ = 0.0;
  resonator_level_ = 0.0;
  
  bypass_ = false;
  
  resonator_model_ = RESONATOR_MODEL_MODAL;
    
    // vb init buffers
    fill(&sides_buffer_[0], &sides_buffer_[kMaxBlockSize], 0.0);
    fill(&center_buffer_[0], &center_buffer_[kMaxBlockSize], 0.0);
    fill(&raw_buffer_[0], &raw_buffer_[kMaxBlockSize], 0.0);
    
}

void Part::Seed(uint32_t* seed, size_t size) {
  // Scramble all bits from the serial number.
  uint32_t signature = 0xf0cacc1a;
  for (size_t i = 0; i < size; ++i) {
      signature ^= seed[i];
      signature = signature * 1664525L + 1013904223L;
  }
  double x;

  x = static_cast<double>(signature & 7) / 8.0;
  signature >>= 3;
  patch_.resonator_modulation_frequency = (0.4 + 0.8 * x) / Dsp::getSr();
  
  x = static_cast<double>(signature & 7) / 8.0;
  signature >>= 3;
  patch_.resonator_modulation_offset = 0.05 + 0.1 * x;

  x = static_cast<double>(signature & 7) / 8.0;
  signature >>= 3;
  patch_.reverb_diffusion = 0.55 + 0.15 * x;

  x = static_cast<double>(signature & 7) / 8.0;
  signature >>= 3;
  patch_.reverb_lp = 0.7 + 0.2 * x;

  x = static_cast<double>(signature & 7) / 8.0;
  signature >>= 3;
  patch_.exciter_signature = x;
}

void Part::Process(
    const PerformanceState& performance_state,
    const double* blow_in,
    const double* strike_in,
    double* main,
    double* aux,
    size_t size) {

  // Copy inputs to outputs when bypass mode is enabled.
  if (bypass_ || panic_) {
    if (panic_) {
      // If the resonator is blowing up (this has been observed once before
      // corrective action was taken), reset the state of the filters to 0
      // to prevent the module to freeze with resonators' state blocked at NaN.
      for (size_t i = 0; i < kNumVoices; ++i) {
        voice_[i].Panic();
      }
      resonator_level_ = 0.0;
      panic_ = false;
    }
    copy(&blow_in[0], &blow_in[size], &aux[0]);
    copy(&strike_in[0], &strike_in[size], &main[0]);
    return;
  }

  // When a new note is played, cycle to the next voice.
  if (performance_state.gate && !previous_gate_) {
    ++active_voice_;
    if (active_voice_ >= kNumVoices) {
      active_voice_ = 0;
    }
  }
  
  previous_gate_ = performance_state.gate;
  note_[active_voice_] = performance_state.note;
  fill(&main[0], &main[size], 0.0);
  fill(&aux[0], &aux[size], 0.0);
  
  // Compute the raw signal gain, stereo spread, and reverb parameters from
  // the "space" metaparameter.
  double space = patch_.space >= 1.0 ? 1.0 : patch_.space;
  double raw_gain = space <= 0.05 ? 1.0 : 
    (space <= 0.1 ? 2.0 - space * 20.0 : 0.0);
  space = space >= 0.1 ? space - 0.1 : 0.0;
  double spread = space <= 0.7 ? space : 0.7;
  double reverb_amount = space >= 0.5 ? 1.0 * (space - 0.5) : 0.0;
  double reverb_time = 0.35 + 1.2 * reverb_amount;
  
    
  // Render each voice.
    /*
  for (size_t i = 0; i < kNumVoices; ++i) {
    double midi_pitch = note_[i] + performance_state.modulation;
    if (easter_egg_) {
      ominous_voice_[i].Process(
          patch_,
          midi_pitch,
          performance_state.strength,
          i == active_voice_ && performance_state.gate,
          (i == active_voice_) ? blow_in : silence_,
          (i == active_voice_) ? strike_in : silence_,
          raw_buffer_,
          center_buffer_,
          sides_buffer_,
          size);
    } else {
      // Convert the MIDI pitch to a frequency.
      int32_t pitch = static_cast<int32_t>((midi_pitch + 48.0) * 256.0);
      if (pitch < 0) {
        pitch = 0;
      } else if (pitch >= 65535) {
        pitch = 65535;
      }
      voice_[i].set_resonator_model(resonator_model_);
      // Render the voice signal.
        double freq = lut_midi_to_f_high[pitch >> 8] * lut_midi_to_f_low[pitch & 0xff];
        freq *= Dsp::getSrFactor();
        //std::cout << "freq: " << freq << "\n";
        
      voice_[i].Process(
          patch_,
          freq,
          performance_state.strength,
          i == active_voice_ && performance_state.gate,
          (i == active_voice_) ? blow_in : silence_,
          (i == active_voice_) ? strike_in : silence_,
          raw_buffer_,
          center_buffer_,
          sides_buffer_,
          size);
    }*/
    

        double midi_pitch = note_[0] + performance_state.modulation;
        if (easter_egg_) {
            ominous_voice_[0].Process(
                                      patch_,
                                      midi_pitch,
                                      performance_state.strength,
                                      performance_state.gate,
                                      blow_in,
                                      strike_in,
                                      raw_buffer_,
                                      center_buffer_,
                                      sides_buffer_,
                                      size);
        } else {
            // Convert the MIDI pitch to a frequency.
            int32_t pitch = static_cast<int32_t>((midi_pitch + 48.0) * 256.0);
            if (pitch < 0) {
                pitch = 0;
            } else if (pitch >= 65535) {
                pitch = 65535;
            }
            voice_[0].set_resonator_model(resonator_model_);
            // Render the voice signal.
            double freq = lut_midi_to_f_high[pitch >> 8] * lut_midi_to_f_low[pitch & 0xff];
            freq *= Dsp::getSrFactor();

            voice_[0].Process(
                              patch_,
                              freq,
                              performance_state.strength,
                              performance_state.gate,
                              blow_in,
                              strike_in,
                              raw_buffer_,
                              center_buffer_,
                              sides_buffer_,
                              size);
        }
    
    // Mixdown.
    for (size_t j = 0; j < size; ++j) {
      double side = sides_buffer_[j] * spread;
      double r = center_buffer_[j] - side;
      double l = center_buffer_[j] + side;;
      main[j] += r;
      aux[j] += l + (raw_buffer_[j] - l) * raw_gain;
    }
  //}

    
  // Pre-clipping
  if (!easter_egg_) {
    for (size_t i = 0; i < size; ++i) {
      main[i] = SoftLimit(main[i]);
      aux[i] = SoftLimit(aux[i]);
    }
  }
 
  // Metering.
//TODO: clean metering code
    /*
  double exciter_level = voice_[active_voice_].exciter_level();
  double resonator_level = resonator_level_;
  for (size_t i = 0; i < size; ++i) {
    double error = main[i] * main[i] - resonator_level;
    resonator_level += error * (error > 0.0 ? 0.05 : 0.0005);
  }
  resonator_level_ = resonator_level;
  if (resonator_level >= 200.0) {
    panic_ = true;
  }
    

  if (easter_egg_) {
    double l = (patch_.exciter_blow_level + patch_.exciter_strike_level) * 0.5;
    scaled_exciter_level_ = l * (2.0 - l);
  } else {
    exciter_level *= 16.0;
    scaled_exciter_level_ = exciter_level > 0.1 ? 1.0 : exciter_level;
  }

  resonator_level *= 16.0;
  scaled_resonator_level_ = resonator_level < 1.0 ? resonator_level : 1.0;
  */
    
  // Apply reverb.
  reverb_.set_amount(reverb_amount);
  reverb_.set_diffusion(patch_.reverb_diffusion);
    bool freeze = patch_.space >= 1.75;       // TODO: freeze level a little too low, no? vb
  if (freeze) {
    reverb_.set_time(1.0);
    reverb_.set_input_gain(0.0);
    reverb_.set_lp(1.0);
  } else {
    reverb_.set_time(reverb_time);
    reverb_.set_input_gain(0.2);
    reverb_.set_lp(patch_.reverb_lp);
  }
  reverb_.Process(main, aux, size);
}

}  // namespace elements
