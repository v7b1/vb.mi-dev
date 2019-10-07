//
//  read_Inputs.cpp
//  vb.mi-dev
//
//  Created by vb on 10.11.18.
//
//

#include "read_inputs.hpp"

#include <algorithm>

//#include "stmlib/dsp/dsp.h"

#include "elements/dsp/part.h"
#include "elements/dsp/patch.h"

namespace elements {
    
    using namespace std;
    using namespace stmlib;
    
    
    Law ReadInputs::law_[POT_LAST] = {
        LAW_LINEAR,  // POT_EXCITER_ENVELOPE_SHAPE,
        LAW_LINEAR,  // POT_EXCITER_BOW_LEVEL,
        LAW_LINEAR,  // POT_EXCITER_BOW_TIMBRE,
        LAW_QUADRATIC_BIPOLAR,  // POT_EXCITER_BOW_TIMBRE_ATTENUVERTER,
        LAW_LINEAR,  // POT_EXCITER_BLOW_LEVEL,
        LAW_LINEAR,  // POT_EXCITER_BLOW_META,
        LAW_QUADRATIC_BIPOLAR,  // POT_EXCITER_BLOW_META_ATTENUVERTER,
        LAW_LINEAR,  // POT_EXCITER_BLOW_TIMBRE,
        LAW_QUADRATIC_BIPOLAR,  // POT_EXCITER_BLOW_TIMBRE_ATTENUVERTER,
        LAW_LINEAR,  // POT_EXCITER_STRIKE_LEVEL,
        LAW_LINEAR,  // POT_EXCITER_STRIKE_META,
        LAW_QUADRATIC_BIPOLAR,  // POT_EXCITER_STRIKE_META_ATTENUVERTER,
        LAW_LINEAR,  // POT_EXCITER_STRIKE_TIMBRE,
        LAW_QUADRATIC_BIPOLAR,  // POT_EXCITER_STRIKE_TIMBRE_ATTENUVERTER,
        LAW_LINEAR, // POT_RESONATOR_COARSE, <<- direct midi note input, no quantisation needed anymore, vb
        LAW_QUADRATIC_BIPOLAR,  // POT_RESONATOR_FINE,
        LAW_QUARTIC_BIPOLAR,  // POT_RESONATOR_FM_ATTENUVERTER,
        LAW_LINEAR,  // POT_RESONATOR_GEOMETRY,
        LAW_QUADRATIC_BIPOLAR,  // POT_RESONATOR_GEOMETRY_ATTENUVERTER,
        LAW_LINEAR,  // POT_RESONATOR_BRIGHTNESS,
        LAW_QUADRATIC_BIPOLAR,  // POT_RESONATOR_BRIGHTNESS_ATTENUVERTER,
        LAW_LINEAR,  // POT_RESONATOR_DAMPING,
        LAW_QUADRATIC_BIPOLAR,  // POT_RESONATOR_DAMPING_ATTENUVERTER,
        LAW_LINEAR,  // POT_RESONATOR_POSITION,
        LAW_QUADRATIC_BIPOLAR,  // POT_RESONATOR_POSITION_ATTENUVERTER,
        LAW_LINEAR,  // POT_SPACE,
        LAW_QUADRATIC_BIPOLAR  // POT_SPACE_ATTENUVERTER
    };
    
    
    //Storage<1> storage;
    
    void ReadInputs::Init() {

        calibration_settings_.pitch_offset = 66.67;
        calibration_settings_.pitch_scale = -84.26;
        for (size_t i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
            calibration_settings_.offset[i] = 10.0 * 20.0 / 120.0 / 3.3;
            cv_floats[i] = 0.0;
        }
        calibration_settings_.boot_in_easter_egg_mode = false;
        calibration_settings_.resonator_model = 0;

        
        //CONSTRAIN(calibration_settings_.resonator_model, 0, 2);       // vb ??
        
        note_ = 0.0;
        modulation_ = 0.0;
        fill(&pot_raw_[0], &pot_raw_[POT_LAST], 0.0);
        fill(&pot_lp_[0], &pot_lp_[POT_LAST], 0.0);
        fill(&pot_quantized_[0], &pot_quantized_[POT_LAST], 0.0);
    }
    
    
    void ReadInputs::ReadPanelPot(uint8_t index, double value) {
        // put one value into pot_raw
        
        switch (law_[index]) {
            case LAW_LINEAR:
                pot_raw_[index] = value;    // 0. -- +1.;
                break;
                
            case LAW_QUADRATIC_BIPOLAR:
            {
                double x = value;   // -1. -- +1.
                double x2 = x * x * 3.3;
                pot_raw_[index] = x < 0.0 ? -x2 : x2;
            }
                break;
                
            case LAW_QUARTIC_BIPOLAR:
            {
                double x = value;   // -1. -- +1.
                double x2 = x * x;
                double x4 = x2 * x2 * 3.3;
                pot_raw_[index] = x < 0.0 ? -x4 : x4;
            }
                break;
                
            case LAW_QUANTIZED_NOTE:
            {
                // 5 octaves.
                double note = 60.0 * value;   // 0. -- +1.;
                pot_raw_[index] += 0.5 * (note - pot_raw_[index]);
                double n = pot_raw_[index];
                double hysteresis = n - pot_quantized_[index] > 0.0 ? -0.3 : +0.3;
                pot_quantized_[index] = static_cast<int32_t>(n + hysteresis + 0.5);
            }
                break;
        }

    }

    
    
#define BIND(destination, NAME, lp_coefficient, min, max) \
{ \
double pot = pot_lp_[POT_ ## NAME]; \
double cv = cv_floats[CV_ADC_ ## NAME] * 0.5; \
double value = pot + pot_lp_[POT_ ## NAME ## _ATTENUVERTER] * cv; \
CONSTRAIN(value, min, max); \
if (lp_coefficient != 1.0) { \
destination += (value - destination) * lp_coefficient; \
} else { \
destination = value; \
} \
}
//const double* offset = calibration_settings_.offset; \
    //double cv = offset[CV_ADC_ ## NAME] - cv_floats[CV_ADC_ ## NAME];
    
    void ReadInputs::Read(
                        Patch* patch,
                        PerformanceState* state)
    {
        // Low-pass filter all front-panel pots.
        for (size_t i = 0; i < POT_LAST; ++i) {
            pot_lp_[i] += 0.01 * (pot_raw_[i] - pot_lp_[i]);
        }
        
        patch->exciter_envelope_shape = pot_lp_[POT_EXCITER_ENVELOPE_SHAPE];
        patch->exciter_bow_level = pot_lp_[POT_EXCITER_BOW_LEVEL];
        patch->exciter_blow_level = pot_lp_[POT_EXCITER_BLOW_LEVEL];
        patch->exciter_strike_level = pot_lp_[POT_EXCITER_STRIKE_LEVEL];
        
        // combine cv and pot inputs
        BIND(patch->exciter_bow_timbre, EXCITER_BOW_TIMBRE, 1.0, 0.0, 0.9995);
        BIND(patch->exciter_blow_meta, EXCITER_BLOW_META, 0.05, 0.0, 0.9995);
        BIND(patch->exciter_blow_timbre, EXCITER_BLOW_TIMBRE, 1.0, 0.0, 0.9995);
        BIND(patch->exciter_strike_meta, EXCITER_STRIKE_META, 0.05, 0.0, 0.9995);
        BIND(patch->exciter_strike_timbre, EXCITER_STRIKE_TIMBRE, 1.0, 0.0, 0.995);
        
        BIND(patch->resonator_geometry, RESONATOR_GEOMETRY, 0.05, 0.0, 0.9995);
        BIND(patch->resonator_brightness, RESONATOR_BRIGHTNESS, 1.0, 0.0, 0.9995);
        BIND(patch->resonator_damping, RESONATOR_DAMPING, 1.0, 0.0, 0.9995);
        BIND(patch->resonator_position, RESONATOR_POSITION, 0.01, 0.0, 0.9995);
        BIND(patch->space, SPACE, 0.01, 0.0, 2.0);

        //double note = calibration_settings_.pitch_offset;
        // inverse cv v/oct value
        //note += (1.0 - cv_floats[CV_ADC_V_OCT]) * calibration_settings_.pitch_scale;
        // use v/oct values directly (as midi pitches)
        double note = cv_floats[CV_ADC_V_OCT];
        double interval = note - note_;
        // When a pitch difference of more than 1 quartertone is observed, do
        // not attempt to slew-limit and jump straight to the right pitch.
        if (interval < -0.4 || interval > 0.4) {
            note_ = note;
        } else {
            note_ += 0.1 * interval;
        }
        /*
        double modulation = pot_lp_[POT_RESONATOR_FM_ATTENUVERTER] * 49.5 *
        (calibration_settings_.offset[CV_ADC_FM] - cv_floats[CV_ADC_FM]);*/
        double modulation = pot_lp_[POT_RESONATOR_FM_ATTENUVERTER] * 49.5 * cv_floats[CV_ADC_FM];
        
        modulation_ += 0.5 * (modulation - modulation_);
        state->modulation = modulation_;
        
        state->note = note_;
        //state->note += pot_quantized_[POT_RESONATOR_COARSE] + 19.0;
        // don't use quantized input anymore, use direct midi note input
        state->note += pot_raw_[POT_RESONATOR_COARSE];
        state->note += pot_lp_[POT_RESONATOR_FINE] * (2.0 / 3.3);
        
        //state->strength = 1.0 - cv_floats[CV_ADC_PRESSURE];
        // scale -1..+1 input to 0..+1.0
        state->strength = cv_floats[CV_ADC_PRESSURE];   // * 0.5 + 0.5;
        
        CONSTRAIN(state->strength, 0.0, 1.0);
        CONSTRAIN(state->modulation, -60.0, 60.0);
        
        /*
        // Scan the gate input 1ms after the other CVs to prevent lag.
        state->gate |= previous_gate_;
        previous_gate_ = gate_;
        gate_ = cv_floats[CV_ADC_CHANNEL_LAST] != 0.;
         */
    }
    
}  // namespace elements
