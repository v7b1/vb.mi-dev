//
//  read_inputs.cpp
//  vb.mi-dev
//
//  Created by vb on 30.10.18.
//

// Copyright 2015 Émilie Gillet.
//
// Author: Émilie Gillet
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



#include "read_inputs.h"


#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"


namespace rings {
    
    using namespace std;
    using namespace stmlib;
    
    
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
    
    void ReadInputs::Init() {
        transpose_ = 0.0;
        
        fill(&adc_lp_[0], &adc_lp_[ADC_CHANNEL_LAST], 0.0);
        inhibit_strum_ = 0;
        fm_cv_ = 0.0;
    }
 /*
#define ATTENUVERT(destination, NAME, min, max) \
{ \
double value = adc_lp_[ADC_CHANNEL_CV_ ## NAME]; \
value *= adc_lp_[ADC_CHANNEL_ATTENUVERTER_ ## NAME]; \
value += adc_lp_[ADC_CHANNEL_POT_ ## NAME]; \
CONSTRAIN(value, min, max) \
destination = value; \
}
  */
    
//#define ATTENUVERT(destination, NAME, min, max) \
//{ \
//double value = adc_lp_[ADC_CHANNEL_CV_ ## NAME]; \
//value += adc_lp_[ADC_CHANNEL_POT_ ## NAME]; \
//CONSTRAIN(value, min, max) \
//destination = value; \
//}
    
    void ReadInputs::Read(Patch* patch, PerformanceState* performance_state, double* inputs)
    {
        for (size_t i = 0; i < ADC_CHANNEL_ATTENUVERTER_FREQUENCY; ++i) {
            double value = inputs[i];
            const ChannelSettings& settings = channel_settings_[i];
            adc_lp_[i] += settings.lp_coefficient * (value - adc_lp_[i]);
        }
        
//        ATTENUVERT(patch->structure, STRUCTURE, 0.0, 0.9995);
//        ATTENUVERT(patch->brightness, BRIGHTNESS, 0.0, 1.0);
//        ATTENUVERT(patch->damping, DAMPING, 0.0, 1.0);
//        ATTENUVERT(patch->position, POSITION, 0.0, 1.0);
        
        double value;
        value = adc_lp_[ADC_CHANNEL_CV_STRUCTURE] + adc_lp_[ADC_CHANNEL_POT_STRUCTURE];
        CONSTRAIN(value, 0.0, 0.9995);
        patch->structure = value;
        
        value = adc_lp_[ADC_CHANNEL_CV_BRIGHTNESS] + adc_lp_[ADC_CHANNEL_POT_BRIGHTNESS];
        CONSTRAIN(value, 0.0, 1.0);
        patch->brightness = value;
        
        value = adc_lp_[ADC_CHANNEL_CV_DAMPING] + adc_lp_[ADC_CHANNEL_POT_DAMPING];
        CONSTRAIN(value, 0.0, 1.0);
        patch->damping = value;
        
        value = adc_lp_[ADC_CHANNEL_CV_POSITION] + adc_lp_[ADC_CHANNEL_POT_POSITION];
        CONSTRAIN(value, 0.0, 1.0);
        patch->position = value;
        
        
        performance_state->fm = adc_lp_[ADC_CHANNEL_CV_FREQUENCY];// * 48.0;
        
        double transpose = 60.0 * adc_lp_[ADC_CHANNEL_POT_FREQUENCY];
        // vb, we don't care about the quantization...
        //double hysteresis = transpose - transpose_ > 0.0 ? -0.3 : +0.3;
        //transpose_ = static_cast<int32_t>(transpose + hysteresis + 0.5);

        performance_state->note = adc_lp_[ADC_CHANNEL_CV_V_OCT];
        performance_state->tonic = 12.0 + transpose;
        
        if(performance_state->internal_note) {
            performance_state->note = 0.0;
        }
        
        // TODO: isn't this from the previous block and therefore late?
        performance_state->strum = trigger_input_.rising_edge();

        
        performance_state->chord = roundf(patch->structure * (kNumChords-1));
        
        trigger_input_.Read(inputs[ADC_CHANNEL_LAST]);
    }
    
}  // namespace rings
