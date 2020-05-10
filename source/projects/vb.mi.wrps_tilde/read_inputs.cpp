//
//  read_inputs.cpp
//  vb.mi.wrps_tilde
//
//  Created by vb on 23.03.19.
//

#include "read_inputs.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "stmlib/dsp/dsp.h"
//#include "stmlib/system/storage.h"
#include "stmlib/utils/random.h"

#include "warps/dsp/parameters.h"
#include "warps/resources.h"

namespace warps {
    
    using namespace std;
    using namespace stmlib;
    
    void ReadInputs::Init(CalibrationData* calibration_data) {
        calibration_data_ = calibration_data;
        
        note_cv_ = 0.0;
        note_pot_ = 0.0;
        fill(&lp_state_[0], &lp_state_[ADC_LAST], 1.0);     // 0.0
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
        BIND(p->channel_drive[0], LEVEL_1, false, 1.6, 0.08, true);     // unipolar input?
        BIND(p->channel_drive[1], LEVEL_2, false, 1.6, 0.08, true);     // unipolar input?
        BIND(p->modulation_algorithm, ALGORITHM, false, 1.0, 0.08, false);  // unwrap was true, vb
        BIND(p->modulation_parameter, PARAMETER, false, 1.0, 0.08, false);
        
        /*
        std::cout << "readInputs pot: " << lp_state_[ADC_LEVEL_1_POT] << "\n";
        //std::cout << "readInputs algoCalib: " << calibration_data_->offset[ADC_ALGORITHM_CV] << "\n";
        std::cout << "readInputs cv: " << lp_state_[ADC_LEVEL_1_CV] << "\n";
        
        std::cout << "readInputs value: " << p->channel_drive[0] << "\n";
        */
        // Prevent wavefolder bleed caused by a slight offset in the pot or ADC.
        /*
        if (p->modulation_algorithm <= 0.125) {
            p->modulation_algorithm = p->modulation_algorithm * 1.08 - 0.01;
            CONSTRAIN(p->modulation_algorithm, 0.0, 1.0);
        }*/
        
        // Easter egg parameter mappings.
        p->frequency_shift_pot = lp_state_[ADC_ALGORITHM_POT];
        float frequency_shift_cv = lp_state_[ADC_ALGORITHM_CV];
        //float frequency_shift_cv = -lp_state_[ADC_ALGORITHM_CV];
        //frequency_shift_cv += calibration_data_->offset[ADC_ALGORITHM_CV];      // 0.495f;
        
        p->frequency_shift_cv = frequency_shift_cv; // * 2.0;
        CONSTRAIN(p->frequency_shift_cv, -1.0, 1.0);
        
        double phase_shift = lp_state_[ADC_ALGORITHM_POT] + frequency_shift_cv; // * 2.0;
        CONSTRAIN(phase_shift, 0.0, 1.0);
        p->phase_shift = phase_shift;
        
        // Internal oscillator parameters.  // TODO: check internal oscillator note!
        double note;
        //pitch_offset = 66.67;         // 100.00f;
        //pitch_scale = -84.26;         //-110.00f;
        //note = calibration_data_->pitch_offset;
        //note += adc_inputs[ADC_LEVEL_1_CV] * calibration_data_->pitch_scale;
        note = adc_inputs[ADC_LEVEL_1_CV] * 60.f;
        double interval = note - note_cv_;
        if (interval < -0.4 || interval > 0.4) {
            note_cv_ = note;
        } else {
            note_cv_ += 0.1 * interval;
        }
        
        note = 60.0 * adc_inputs[ADC_LEVEL_1_POT] + 12.0;
        note_pot_ += 0.1 * (note - note_pot_);
        p->note = note_pot_ + note_cv_;
        
        // if not patched!
        for (int32_t i = 0; i < 2; ++i) {
            if (!patched[i]) {
                double pot = lp_state_[ADC_LEVEL_1_POT + i];
                p->channel_drive[i] = pot * pot;
            }
        }
        if (!patched[0]) {
            p->note = note_pot_ + 24.0;
        }
    }
    
}
