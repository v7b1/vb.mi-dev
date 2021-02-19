//
//  read_inputs.cpp
//  vb.mi.wrps_tilde
//
//  Created by vb on 23.03.19.
//

#include "read_inputs.hpp"

#include <algorithm>
#include <cmath>
//#include <iostream>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

#include "warps/dsp/parameters.h"
#include "warps/resources.h"

namespace warps {
    
    using namespace std;
    using namespace stmlib;
    
    void ReadInputs::Init() {
        note_cv_ = 0.0;
        note_pot_ = 0.0;
        fill(&lp_state_[0], &lp_state_[ADC_LAST], 0.0);
    }
    
    double ReadInputs::UnwrapPot(double x) const {
        return Interpolate(lut_pot_curve, x, 512.0);        
    }
 /*
#define BIND(destination, NAME, unwrap, scale, lp_coefficient, attenuate) \
{ \
    lp_state_[ADC_ ## NAME ## _POT] += 0.33 * lp_coefficient * (adc_inputs[ADC_ ## NAME ## _POT] - lp_state_[ADC_ ## NAME ## _POT]); \
    lp_state_[ADC_ ## NAME ## _CV] += lp_coefficient * (adc_inputs[ADC_ ## NAME ## _CV] - lp_state_[ADC_ ## NAME ## _CV]); \
    double pot = lp_state_[ADC_ ## NAME ## _POT]; \
    if (unwrap) pot = UnwrapPot(pot); \
    double cv = calibration_data_->offset[ADC_ ## NAME ## _CV] - lp_state_[ADC_ ## NAME ## _CV]; \
    double value = attenuate ? (pot * pot * cv * scale) : (pot + cv * scale); \
    CONSTRAIN(value, 0.0, 1.0); \
    destination = value; \
}
   */
#define BIND(destination, NAME, unwrap, scale, lp_coefficient, attenuate) \
{ \
    lp_state_[ADC_ ## NAME ## _POT] += 0.33 * lp_coefficient * (adc_inputs[ADC_ ## NAME ## _POT] - lp_state_[ADC_ ## NAME ## _POT]); \
    lp_state_[ADC_ ## NAME ## _CV] += lp_coefficient * (adc_inputs[ADC_ ## NAME ## _CV] - lp_state_[ADC_ ## NAME ## _CV]); \
    double pot = lp_state_[ADC_ ## NAME ## _POT]; \
    double cv = lp_state_[ADC_ ## NAME ## _CV]; \
    double value = attenuate ? (pot * pot * cv * scale) : (pot + cv * scale); \
    CONSTRAIN(value, 0.0, 1.0); \
    destination = value; \
}
    
    void ReadInputs::Read(Parameters* p, double *adc_inputs, short* patched) {
        // Modulation parameters.
//        BIND(p->channel_drive[0], LEVEL_1, false, 1.6, 0.08, true);     // unipolar input?
//        BIND(p->channel_drive[1], LEVEL_2, false, 1.6, 0.08, true);     // unipolar input?
//        BIND(p->modulation_algorithm, ALGORITHM, false, 1.0, 0.08, false);  // unwrap was true, vb
//        BIND(p->modulation_parameter, PARAMETER, false, 1.0, 0.08, false);
        
        // 2x level --> channel drive
        for (int32_t i = 0; i < 2; ++i) {
            int index_pot = ADC_LEVEL_1_POT + i;
            int index_cv = ADC_LEVEL_1_CV + i;
            lp_state_[index_pot] += 0.33 * 0.08 * (adc_inputs[index_pot] - lp_state_[index_pot]);
            lp_state_[index_cv] += 0.08 * (adc_inputs[index_cv] - lp_state_[index_cv]);
            
            double pot = lp_state_[index_pot];
            if (!patched[i]) {
               p->channel_drive[i] = pot * pot;
            }
            else {
                double cv = lp_state_[index_cv];
                double value = (pot * pot * cv * 1.6);
                CONSTRAIN(value, 0.0, 1.0);
                p->channel_drive[i] = value;
            }
        }
        
        
        // algorithm
        lp_state_[ADC_ALGORITHM_POT] += 0.33 * 0.08 * (adc_inputs[ADC_ALGORITHM_POT] - lp_state_[ADC_ALGORITHM_POT]);
        lp_state_[ADC_ALGORITHM_CV] += 0.08 * (adc_inputs[ADC_ALGORITHM_CV] - lp_state_[ADC_ALGORITHM_CV]);
        double pot = lp_state_[ADC_ALGORITHM_POT];
        //pot = UnwrapPot(pot);
        double cv = lp_state_[ADC_ALGORITHM_CV];
        double value = (pot + cv);
        CONSTRAIN(value, 0.0, 1.0);
        p->modulation_algorithm = value;
        
        // timbre/parameter
        lp_state_[ADC_PARAMETER_POT] += 0.33 * 0.08 * (adc_inputs[ADC_PARAMETER_POT] - lp_state_[ADC_PARAMETER_POT]);
        lp_state_[ADC_PARAMETER_CV] += 0.08 * (adc_inputs[ADC_PARAMETER_CV] - lp_state_[ADC_PARAMETER_CV]);
        pot = lp_state_[ADC_PARAMETER_POT];
        cv = lp_state_[ADC_PARAMETER_CV];
        value = (pot + cv);
        CONSTRAIN(value, 0.0, 1.0);
        p->modulation_parameter = value;
        
        
        // Easter egg parameter mappings.
        p->frequency_shift_pot = lp_state_[ADC_ALGORITHM_POT];
        float frequency_shift_cv = lp_state_[ADC_ALGORITHM_CV];
        p->frequency_shift_cv = frequency_shift_cv * 0.5f;  // vb, range is too wide!
        CONSTRAIN(p->frequency_shift_cv, -1.0, 1.0);
        
        double phase_shift = lp_state_[ADC_ALGORITHM_POT] + frequency_shift_cv;
        CONSTRAIN(phase_shift, 0.0, 1.0);
        p->phase_shift = phase_shift;
        
        
        // Internal oscillator parameters.
//        double note;
//        note = adc_inputs[ADC_LEVEL_1_CV] * 60.f;
//        double interval = note - note_cv_;
//        if (interval < -0.4 || interval > 0.4) {
//            note_cv_ = note;
//        } else {
//            note_cv_ += 0.1 * interval;
//        }
//
//        note = 60.0 * adc_inputs[ADC_LEVEL_1_POT] + 12.0;
//        note_pot_ += 0.1 * (note - note_pot_);
//        p->note = note_pot_ + note_cv_;
//
        // if not patched! this overrides the 'BIND' above!
//        for (int32_t i = 0; i < 2; ++i) {
//            if (!patched[i]) {
//                double pot = lp_state_[ADC_LEVEL_1_POT + i];
//                p->channel_drive[i] = pot * pot;
//            }
//        }
//        if (!patched[0]) {
//            p->note = note_pot_ + 24.0;
//        }
    }
    
}
