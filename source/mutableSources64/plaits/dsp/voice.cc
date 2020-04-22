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
// Main synthesis voice.

#include "plaits/dsp/voice.h"

namespace plaits {

using namespace std;
using namespace stmlib;


void Voice::Init(BufferAllocator* allocator) {
  engines_.Init();

  engines_.RegisterInstance(&virtual_analog_engine_, false, 0.8, 0.8);

  engines_.RegisterInstance(&waveshaping_engine_, false, 0.7, 0.6);
  engines_.RegisterInstance(&fm_engine_, false, 0.6, 0.6);
  engines_.RegisterInstance(&grain_engine_, false, 0.7, 0.6);
 
  engines_.RegisterInstance(&additive_engine_, false, 0.8, 0.8);
 
  engines_.RegisterInstance(&wavetable_engine_, false, 0.6, 0.6);
  engines_.RegisterInstance(&chord_engine_, false, 0.8, 0.8);
  engines_.RegisterInstance(&speech_engine_, false, -0.7, 0.8);
  engines_.RegisterInstance(&swarm_engine_, false, -3.0, 1.0);
  engines_.RegisterInstance(&noise_engine_, false, -1.0, -1.0);
  engines_.RegisterInstance(&particle_engine_, false, -2.0, 1.0);
  engines_.RegisterInstance(&string_engine_, true, -1.0, 0.8);
  engines_.RegisterInstance(&modal_engine_, true, -1.0, 0.8);
  engines_.RegisterInstance(&bass_drum_engine_, true, 0.8, 0.8);
  engines_.RegisterInstance(&snare_drum_engine_, true, 0.8, 0.8);
  engines_.RegisterInstance(&hi_hat_engine_, true, 0.8, 0.8);

  for (int i = 0; i < engines_.size(); ++i) {
    // All engines will share the same RAM space.
    allocator->Free();
    engines_.get(i)->Init(allocator);
  }
  
  engine_quantizer_.Init();
  previous_engine_index_ = -1;
  engine_cv_ = 0.0;
  
  out_post_processor_.Init();
  aux_post_processor_.Init();

  decay_envelope_.Init();
  lpg_envelope_.Init();
  
  trigger_state_ = false;
  previous_note_ = 0.0;
  
  trigger_delay_.Init(trigger_delay_line_);
}
    
    
    // changed out and aux buffers, vb
    
    void Voice::Render(
                       const Patch& patch,
                       const Modulations& modulations,
                       double* out,
                       double* aux,
                       size_t size) {
        // Trigger, LPG, internal envelope.
        
        // Delay trigger by 1ms to deal with sequencers or MIDI interfaces whose
        // CV out lags behind the GATE out.
        /* don't need the trigger delay for now, vb.
        trigger_delay_.Write(modulations.trigger);
        double trigger_value = trigger_delay_.Read(kTriggerDelay);
        */
        double trigger_value = modulations.trigger;
        
        bool previous_trigger_state = trigger_state_;
        if (!previous_trigger_state) {
            if (trigger_value > 0.3) {
                trigger_state_ = true;
                if (!modulations.level_patched) {
                    lpg_envelope_.Trigger();
                }
                decay_envelope_.Trigger(); // TODO: check out!
                //std::cout << "trigger!\n";
                engine_cv_ = modulations.engine;
            }
        } else {
            if (trigger_value < 0.1) {
                trigger_state_ = false;
            }
        }
        if (!modulations.trigger_patched) {
            engine_cv_ = modulations.engine;
        }
        
        // Engine selection.
        int engine_index = engine_quantizer_.Process(
                                                     patch.engine,
                                                     engine_cv_,
                                                     engines_.size(),
                                                     0.25);
        
        Engine* e = engines_.get(engine_index);
        
        if (engine_index != previous_engine_index_) {
            e->Reset();
            out_post_processor_.Reset();
            previous_engine_index_ = engine_index;
        }
        EngineParameters p;
        
        bool rising_edge = trigger_state_ && !previous_trigger_state;
        double note = (modulations.note + previous_note_) * 0.5;
        previous_note_ = modulations.note;
        const PostProcessingSettings& pp_s = e->post_processing_settings;
        
        if (modulations.trigger_patched) {
            p.trigger = rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW;
        } else {
            p.trigger = TRIGGER_UNPATCHED;
        }
        
        const double short_decay = (200.0 * kBlockSize) / kSampleRate *
        SemitonesToRatio(-96.0 * patch.decay);
        
        decay_envelope_.Process(short_decay * 2.0);
        
        const double compressed_level = max(
                                            1.3 * modulations.level / (0.3 + fabs(modulations.level)),
                                            0.0);
        p.accent = modulations.level_patched ? compressed_level : 0.8;
        
        bool use_internal_envelope = modulations.trigger_patched;
        
        // Actual synthesis parameters.
        
        p.harmonics = patch.harmonics + modulations.harmonics;
        CONSTRAIN(p.harmonics, 0.0, 1.0);
        
        double internal_envelope_amplitude = 1.0;
        
        if (engine_index == 7) {
            internal_envelope_amplitude = 2.0 - p.harmonics * 6.0;
            CONSTRAIN(internal_envelope_amplitude, 0.0, 1.0);
            speech_engine_.set_prosody_amount(
                                              !modulations.trigger_patched || modulations.frequency_patched ?
                                              0.0 : patch.frequency_modulation_amount);
            speech_engine_.set_speed(
                                     !modulations.trigger_patched || modulations.morph_patched ?
                                     0.0 : patch.morph_modulation_amount);
        }
        
        p.note = ApplyModulations(
                                  patch.note + note,
                                  patch.frequency_modulation_amount,
                                  modulations.frequency_patched,
                                  modulations.frequency,
                                  use_internal_envelope,
                                  internal_envelope_amplitude * \
                                  decay_envelope_.value() * decay_envelope_.value() * 48.0,
                                  1.0,
                                  -119.0,
                                  120.0);
        
        p.timbre = ApplyModulations(
                                    patch.timbre,
                                    patch.timbre_modulation_amount,
                                    modulations.timbre_patched,
                                    modulations.timbre,
                                    use_internal_envelope,
                                    decay_envelope_.value(),
                                    0.0,
                                    0.0,
                                    1.0);
        
        p.morph = ApplyModulations(
                                   patch.morph,
                                   patch.morph_modulation_amount,
                                   modulations.morph_patched,
                                   modulations.morph,
                                   use_internal_envelope,
                                   internal_envelope_amplitude * decay_envelope_.value(),
                                   0.0,
                                   0.0,
                                   1.0);
        
        bool already_enveloped = pp_s.already_enveloped;

        e->Render(p, out, aux, size, &already_enveloped);
        
        // TODO: look at that --> lpg_bypass
        bool lpg_bypass = already_enveloped || \
        (!modulations.level_patched && !modulations.trigger_patched);
        
        // Compute LPG parameters.
        if (!lpg_bypass) {
            const double hf = patch.lpg_colour;
            const double decay_tail = (20.0 * kBlockSize) / kSampleRate *
            SemitonesToRatio(-72.0 * patch.decay + 12.0 * hf) - short_decay;
            
            if (modulations.level_patched) {
                lpg_envelope_.ProcessLP(compressed_level, short_decay, decay_tail, hf);
            } else {
                const double attack = NoteToFrequency(p.note) * double(kBlockSize) * 2.0;
                lpg_envelope_.ProcessPing(attack, short_decay, decay_tail, hf);
            }
        }
        
        
        // changed buffer handling of post processors a little, vb
        // use in/out buffer and skip conversion to 16bit int
        out_post_processor_.Process(
                                    pp_s.out_gain,
                                    lpg_bypass,
                                    lpg_envelope_.gain(),
                                    lpg_envelope_.frequency(),
                                    lpg_envelope_.hf_bleed(),
                                    out,        // in_out
                                    size);
        
        aux_post_processor_.Process(
                                    pp_s.aux_gain,
                                    lpg_bypass,
                                    lpg_envelope_.gain(),
                                    lpg_envelope_.frequency(),
                                    lpg_envelope_.hf_bleed(),
                                    aux,        // in_out
                                    size);
    }
    
    
    
    
    // old render function
    /*
    void Voice::RenderOld(
                          const Patch& patch,
                          const Modulations& modulations,
                          Frame* frames,
                          size_t size) {
        // Trigger, LPG, internal envelope.
        
        // Delay trigger by 1ms to deal with sequencers or MIDI interfaces whose
        // CV out lags behind the GATE out.
        trigger_delay_.Write(modulations.trigger);
        double trigger_value = trigger_delay_.Read(kTriggerDelay);
        
        bool previous_trigger_state = trigger_state_;
        if (!previous_trigger_state) {
            if (trigger_value > 0.3) {
                trigger_state_ = true;
                if (!modulations.level_patched) {
                    lpg_envelope_.Trigger();
                }
                decay_envelope_.Trigger();
                engine_cv_ = modulations.engine;
            }
        } else {
            if (trigger_value < 0.1) {
                trigger_state_ = false;
            }
        }
        if (!modulations.trigger_patched) {
            engine_cv_ = modulations.engine;
        }
        
        // Engine selection.
        int engine_index = engine_quantizer_.Process(
                                                     patch.engine,
                                                     engine_cv_,
                                                     engines_.size(),
                                                     0.25);
        
        Engine* e = engines_.get(engine_index);
        
        if (engine_index != previous_engine_index_) {
            e->Reset();
            out_post_processor_.Reset();
            previous_engine_index_ = engine_index;
        }
        EngineParameters p;
        
        bool rising_edge = trigger_state_ && !previous_trigger_state;
        double note = (modulations.note + previous_note_) * 0.5;
        previous_note_ = modulations.note;
        const PostProcessingSettings& pp_s = e->post_processing_settings;
        
        if (modulations.trigger_patched) {
            p.trigger = rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW;
        } else {
            p.trigger = TRIGGER_UNPATCHED;
        }
        
        int blockSize = plaits::Dsp::getBlockSize();
        const double sr = Dsp::getSr();
        const double short_decay = (200.0 * blockSize) / sr *
        SemitonesToRatio(-96.0 * patch.decay);
        
        decay_envelope_.Process(short_decay * 2.0);
        
        const double compressed_level = max(
                                            1.3 * modulations.level / (0.3 + fabs(modulations.level)),
                                            0.0);
        p.accent = modulations.level_patched ? compressed_level : 0.8;
        
        bool use_internal_envelope = modulations.trigger_patched;
        
        // Actual synthesis parameters.
        
        p.harmonics = patch.harmonics + modulations.harmonics;
        CONSTRAIN(p.harmonics, 0.0, 1.0);
        
        double internal_envelope_amplitude = 1.0;
        
        if (engine_index == 7) {
            internal_envelope_amplitude = 2.0 - p.harmonics * 6.0;
            CONSTRAIN(internal_envelope_amplitude, 0.0, 1.0);
            speech_engine_.set_prosody_amount(
                                              !modulations.trigger_patched || modulations.frequency_patched ?
                                              0.0 : patch.frequency_modulation_amount);
            speech_engine_.set_speed(
                                     !modulations.trigger_patched || modulations.morph_patched ?
                                     0.0 : patch.morph_modulation_amount);
        }
        
        p.note = ApplyModulations(
                                  patch.note + note,
                                  patch.frequency_modulation_amount,
                                  modulations.frequency_patched,
                                  modulations.frequency,
                                  use_internal_envelope,
                                  internal_envelope_amplitude * \
                                  decay_envelope_.value() * decay_envelope_.value() * 48.0,
                                  1.0,
                                  -119.0,
                                  120.0);
        
        p.timbre = ApplyModulations(
                                    patch.timbre,
                                    patch.timbre_modulation_amount,
                                    modulations.timbre_patched,
                                    modulations.timbre,
                                    use_internal_envelope,
                                    decay_envelope_.value(),
                                    0.0,
                                    0.0,
                                    1.0);
        
        p.morph = ApplyModulations(
                                   patch.morph,
                                   patch.morph_modulation_amount,
                                   modulations.morph_patched,
                                   modulations.morph,
                                   use_internal_envelope,
                                   internal_envelope_amplitude * decay_envelope_.value(),
                                   0.0,
                                   0.0,
                                   1.0);
        
        bool already_enveloped = pp_s.already_enveloped;
        
        e->Render(p, out_buffer_, aux_buffer_, size, &already_enveloped);
        
        bool lpg_bypass = already_enveloped || \
        (!modulations.level_patched && !modulations.trigger_patched);
        
        // Compute LPG parameters.
        if (!lpg_bypass) {
            const double hf = patch.lpg_colour;
            const double decay_tail = (20.0 * blockSize) / sr *
            SemitonesToRatio(-72.0 * patch.decay + 12.0 * hf) - short_decay;
            
            if (modulations.level_patched) {
                lpg_envelope_.ProcessLP(compressed_level, short_decay, decay_tail, hf);
            } else {
                const double attack = NoteToFrequency(p.note) * double(blockSize) * 2.0;
                lpg_envelope_.ProcessPing(attack, short_decay, decay_tail, hf);
            }
        }
        
        
        out_post_processor_.Process(
                                    pp_s.out_gain,
                                    lpg_bypass,
                                    lpg_envelope_.gain(),
                                    lpg_envelope_.frequency(),
                                    lpg_envelope_.hf_bleed(),
                                    out_buffer_,
                                    &frames->out,
                                    size,
                                    2);

        aux_post_processor_.Process(
                                    pp_s.aux_gain,
                                    lpg_bypass,
                                    lpg_envelope_.gain(),
                                    lpg_envelope_.frequency(),
                                    lpg_envelope_.hf_bleed(),
                                    aux_buffer_,
                                    &frames->aux,
                                    size,
                                    2);
    }*/
  
}  // namespace plaits
