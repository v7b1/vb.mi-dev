//
//  read_inputs.cpp
//  vb.mi-dev
//
//  Created by vb on 30.10.18.
//
//

#include "read_inputs.h"


#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"

#include <iostream>

namespace rings {
    
    using namespace std;
    using namespace stmlib;
    
    /*

     Law law;
     bool remove_offset;
     double lp_coefficient;
*/
    
    /* static */
    ChannelSettings ReadInputs::channel_settings_[ADC_CHANNEL_LAST] = {
        { LAW_LINEAR, true, 1.0 },  // ADC_CHANNEL_CV_FREQUENCY
        { LAW_LINEAR, true, 0.1 },  // ADC_CHANNEL_CV_STRUCTURE
        { LAW_LINEAR, true, 0.1 },  // ADC_CHANNEL_CV_BRIGHTNESS
        { LAW_LINEAR, true, 0.05 },  // ADC_CHANNEL_CV_DAMPING
        { LAW_LINEAR, true, 0.01 },  // ADC_CHANNEL_CV_POSITION
        { LAW_LINEAR, false, 1.00 },  // ADC_CHANNEL_CV_V_OCT
        { LAW_LINEAR, false, 0.5},  // ADC_CHANNEL_POT_FREQUENCY // coef: 0.01
        { LAW_LINEAR, false, 0.01 },  // ADC_CHANNEL_POT_STRUCTURE
        { LAW_LINEAR, false, 0.01 },  // ADC_CHANNEL_POT_BRIGHTNESS
        { LAW_LINEAR, false, 0.01 },  // ADC_CHANNEL_POT_DAMPING
        { LAW_LINEAR, false, 0.01 },  // ADC_CHANNEL_POT_POSITION
        { LAW_QUARTIC_BIPOLAR, false, 0.01 },  // ADC_CHANNEL_ATTENUVERTER_FREQUENCY, was 0.005
        { LAW_QUADRATIC_BIPOLAR, false, 0.01 },  //ADC_CHANNEL_ATTENUVERTER_STRUCTURE,
        { LAW_QUADRATIC_BIPOLAR, false, 0.01 },  // ADC_CHANNEL_ATTENUVERTER_BRIGHTNESS,
        { LAW_QUADRATIC_BIPOLAR, false, 0.01 },  // ADC_CHANNEL_ATTENUVERTER_DAMPING,
        { LAW_QUADRATIC_BIPOLAR, false, 0.01 },  // ADC_CHANNEL_ATTENUVERTER_POSITION,
    };
    //void ReadInputs::Init(CalibrationData* calibration_data) {
    void ReadInputs::Init() {
        //calibration_data_ = calibration_data;
        
        transpose_ = 0.0;
        
        fill(&adc_lp_[0], &adc_lp_[ADC_CHANNEL_LAST], 0.0);
        inhibit_strum_ = 0;
        fm_cv_ = 0.0;
    }
    
    
#define ATTENUVERT(destination, NAME, min, max) \
{ \
double value = adc_lp_[ADC_CHANNEL_CV_ ## NAME]; \
value *= adc_lp_[ADC_CHANNEL_ATTENUVERTER_ ## NAME]; \
value += adc_lp_[ADC_CHANNEL_POT_ ## NAME]; \
CONSTRAIN(value, min, max) \
destination = value; \
}
    
    void ReadInputs::Read(Patch* patch, PerformanceState* performance_state, double* inputs)
    {
        // Process all CVs / pots.
        for (size_t i = 0; i < ADC_CHANNEL_LAST; ++i) {
            const ChannelSettings& settings = channel_settings_[i];
            double value = inputs[i];
            if (settings.remove_offset) {
                // vb: originally cv inputs are inverted in hardware - so, no need to re-invert them here...
                // also, our 'cv' data is bipolar (-1.--+1.)
                // so, no need for offset, either
                //value = calibration_data_->offset[i] - value;
                value *= 0.5;
            }
            switch (settings.law) {
                case LAW_QUADRATIC_BIPOLAR:
                {
                    value = value - 0.5;
                    double value2 = value * value * 4.0 * 3.3;
                    value = value < 0.0 ? -value2 : value2;
                }
                    break;
                    
                case LAW_QUARTIC_BIPOLAR:
                {
                    value = value - 0.5;
                    double value2 = value * value * 4.0;
                    double value4 = value2 * value2 * 3.3;
                    value = value < 0.0 ? -value4 : value4;
                }
                    break;
                    
                default:
                    break;
            }
            adc_lp_[i] += settings.lp_coefficient * (value - adc_lp_[i]);
        }
        
        ATTENUVERT(patch->structure, STRUCTURE, 0.0, 0.9995);
        ATTENUVERT(patch->brightness, BRIGHTNESS, 0.0, 1.0);
        ATTENUVERT(patch->damping, DAMPING, 0.0, 1.0);
        ATTENUVERT(patch->position, POSITION, 0.0, 1.0);
        
        double fm = adc_lp_[ADC_CHANNEL_CV_FREQUENCY] * 48.0;
        double error = fm - fm_cv_;
        if (fabs(error) >= 0.8) {
            fm_cv_ = fm;
        } else {
            fm_cv_ += 0.02 * error;
        }
        performance_state->fm = fm_cv_ * adc_lp_[ADC_CHANNEL_ATTENUVERTER_FREQUENCY];
        CONSTRAIN(performance_state->fm, -48.0, 48.0);

        double transpose = 60.0 * adc_lp_[ADC_CHANNEL_POT_FREQUENCY];
        double hysteresis = transpose - transpose_ > 0.0 ? -0.3 : +0.3;
        // transpose as int!
        transpose_ = static_cast<int32_t>(transpose + hysteresis + 0.5);
        
        // TODO: change that
        /*
        double note = calibration_data_->pitch_offset;  //66.67
        note += adc_lp_[ADC_CHANNEL_CV_V_OCT] * calibration_data_->pitch_scale; //-84.26
        */
        // try without any scaling
        double note = adc_lp_[ADC_CHANNEL_CV_V_OCT];
        
        // TODO: what's the difference between 'note' and 'tonic'?
        performance_state->note = note;
        performance_state->tonic = 12.0 + transpose_;
        
        performance_state->strum = trigger_input_.rising_edge();
        //if(performance_state->strum) std::cout << "strum!\n";

        
        if (performance_state->internal_note) {
            // Remove quantization when nothing is plugged in the V/OCT input.
            performance_state->note = 0.0;
            performance_state->tonic = 12.0 + transpose;
        }
        
        // Hysteresis on chord.
        //double chord = calibration_data_->offset[ADC_CHANNEL_CV_STRUCTURE] - \
        //inputs[ADC_CHANNEL_CV_STRUCTURE];
        
        // no need to invert 'cv' input, just scale between -0.5 + 0.5, vb
        double chord = inputs[ADC_CHANNEL_CV_STRUCTURE] * 0.5;
        chord *= adc_lp_[ADC_CHANNEL_ATTENUVERTER_STRUCTURE];
        chord += adc_lp_[ADC_CHANNEL_POT_STRUCTURE];
        chord *= static_cast<double>(kNumChords - 1);       // 11 - 1
        hysteresis = chord - chord_ > 0.0 ? -0.1 : +0.1;
        chord_ = static_cast<int32_t>(chord + hysteresis + 0.5);
        CONSTRAIN(chord_, 0, kNumChords - 1);
        performance_state->chord = chord_;
        trigger_input_.Read(inputs[ADC_CHANNEL_LAST]);
    }
    
}  // namespace rings
