//
//  read_inputs.h
//  vb.mi-dev
//
//  Created by vb on 30.10.18.
//
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



#ifndef read_inputs_h
#define read_inputs_h


#include "stmlib/stmlib.h"
#include "rings/settings.h"

namespace rings {
    
    enum AdcChannel {
        ADC_CHANNEL_CV_FREQUENCY,   // 0
        ADC_CHANNEL_CV_STRUCTURE,   // 1
        ADC_CHANNEL_CV_BRIGHTNESS,  // 2
        ADC_CHANNEL_CV_DAMPING,     // 3
        ADC_CHANNEL_CV_POSITION,    // 4
        ADC_CHANNEL_CV_V_OCT,       // 5
        ADC_CHANNEL_POT_FREQUENCY,  // 6
        ADC_CHANNEL_POT_STRUCTURE,  // 7
        ADC_CHANNEL_POT_BRIGHTNESS, // 8
        ADC_CHANNEL_POT_DAMPING,    // 9
        ADC_CHANNEL_POT_POSITION,   // 10
        ADC_CHANNEL_ATTENUVERTER_FREQUENCY, // 11
        ADC_CHANNEL_ATTENUVERTER_STRUCTURE, // 12
        ADC_CHANNEL_ATTENUVERTER_BRIGHTNESS,// 13
        ADC_CHANNEL_ATTENUVERTER_DAMPING,   // 14
        ADC_CHANNEL_ATTENUVERTER_POSITION,  // 15
        ADC_CHANNEL_LAST,       // use this for strum? yes
        
        ADC_CHANNEL_LAST_DIRECT = ADC_CHANNEL_POT_STRUCTURE,
        ADC_CHANNEL_FIRST_MUXED = ADC_CHANNEL_POT_BRIGHTNESS,
        ADC_CHANNEL_NUM_DIRECT = ADC_CHANNEL_FIRST_MUXED,
        ADC_CHANNEL_NUM_MUXED = ADC_CHANNEL_LAST - ADC_CHANNEL_FIRST_MUXED,
        ADC_CHANNEL_NUM_OFFSETS = ADC_CHANNEL_CV_POSITION + 1
    };
    
    enum Law {
        LAW_LINEAR,
        LAW_QUADRATIC_BIPOLAR,
        LAW_QUARTIC_BIPOLAR
    };
    
    struct ChannelSettings {
        Law law;
        bool remove_offset;
        double lp_coefficient;
    };
    
    struct Patch;
    struct PerformanceState;

    class TriggerInp {
    public:
        TriggerInp() { }
        ~TriggerInp() { }
        
        void Init() {
            trigger_ = false;
            previous_trigger_ = false;
        }
        
        void Read(double strum) {
            previous_trigger_ = trigger_;
            trigger_ = strum > 0.0 ;
        }
        
        inline bool rising_edge() const {
            return trigger_ && !previous_trigger_;
        }
        
        inline bool value() const {
            return trigger_;
        }
        
        inline bool DummyRead(double strum) const {
            return strum > 0.0 ;
            //return !GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2);
        }
        
    private:
        bool previous_trigger_;
        bool trigger_;
        
        DISALLOW_COPY_AND_ASSIGN(TriggerInp);
    };

    
    class NormalizationDetector {
    public:
        NormalizationDetector() { }
        ~NormalizationDetector() { }
        
        void Init(double lp_coefficient, double threshold) {
            score_ = 0.0;
            state_ = false;
            threshold_ = threshold;                 
            lp_coefficient_ = lp_coefficient;
        }
        
        void Process(double x, double y) {
            // x is supposed to be centered!
            score_ += lp_coefficient_ * (x * y - score_);
            
            double hysteresis = state_ ? -0.05 * threshold_ : 0.0;
            state_ = score_ >= (threshold_ + hysteresis);
        }
        
        inline bool normalized() const { return state_; }
        inline double score() const { return score_; }
        
    private:
        double score_;
        double lp_coefficient_;
        double threshold_;
        
        bool state_;
        
        DISALLOW_COPY_AND_ASSIGN(NormalizationDetector);
    };

    
    class ReadInputs {
    public:
        ReadInputs() { }
        ~ReadInputs() { }
        
        void Init();
        void Read(Patch* patch, PerformanceState* performance_state, double* inputs);
        
        
    private:
        
        TriggerInp trigger_input_;
        
        int32_t inhibit_strum_;
        
        double adc_lp_[ADC_CHANNEL_LAST];
        double transpose_;
        double fm_cv_;
        double cv_c1_;
        double cv_low_;
        int32_t chord_;
        
        static ChannelSettings channel_settings_[ADC_CHANNEL_LAST];
        
        DISALLOW_COPY_AND_ASSIGN(ReadInputs);
    };

}  // namespace rings

#endif /* read_inputs_h */
