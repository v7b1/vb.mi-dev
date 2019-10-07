//
//  read_inputs.hpp
//  vb.mi-dev
//
//  Created by vb on 30.10.18.
//
//


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
        
        //void Init(CalibrationData* calibration_data);
        void Init();
        void Read(Patch* patch, PerformanceState* performance_state, double* inputs);
        
        /*
         void DetectAudioNormalization(double* in, size_t size);
        
        inline bool ready_for_calibration() const {
            return true;
        }*/
        
        inline bool easter_egg() const {
            return adc_lp_[ADC_CHANNEL_POT_FREQUENCY] < 0.1 &&
            adc_lp_[ADC_CHANNEL_POT_STRUCTURE] > 0.9 &&
            adc_lp_[ADC_CHANNEL_POT_BRIGHTNESS] < 0.1 &&
            adc_lp_[ADC_CHANNEL_POT_POSITION] > 0.9 &&
            adc_lp_[ADC_CHANNEL_POT_DAMPING] > 0.4 &&
            adc_lp_[ADC_CHANNEL_POT_DAMPING] < 0.6 &&
            adc_lp_[ADC_CHANNEL_ATTENUVERTER_BRIGHTNESS] < -1.00 &&
            adc_lp_[ADC_CHANNEL_ATTENUVERTER_FREQUENCY] > 1.00 &&
            adc_lp_[ADC_CHANNEL_ATTENUVERTER_DAMPING] < -1.00 &&
            adc_lp_[ADC_CHANNEL_ATTENUVERTER_STRUCTURE] > 1.00 &&
            adc_lp_[ADC_CHANNEL_ATTENUVERTER_POSITION] < -1.00;
        }
        
        
        /*
        inline uint8_t adc_value(size_t index) const {
            return adc_.value(index) >> 8;
        }
        
        inline bool gate_value() const {
            return trigger_input_.value();
        }
        
        inline uint8_t normalization(size_t index) const {
            switch (index) {
                case 0:
                    return fm_cv_ * 3.3 > 0.8 ? 255 : 0;
                    break;
                    
                case 1:
                    return normalization_detector_trigger_.normalized() ? 255 : 0;
                    break;
                    
                case 2:
                    return normalization_detector_v_oct_.normalized() ? 255 : 0;
                    break;
                    
                default:
                    return normalization_detector_exciter_.normalized() ? 255 : 0;
                    break;
            }
        }*/
        
    private:
        //void DetectNormalization(double* inputs);
        
        //Adc adc_;
        CalibrationData* calibration_data_;
        TriggerInp trigger_input_;
        
        //NormalizationProbe normalization_probe_;
        
        /*
        NormalizationDetector normalization_detector_trigger_;  // strum
        NormalizationDetector normalization_detector_v_oct_;    // v/oct
        NormalizationDetector normalization_detector_exciter_;  // audio in
        
        bool normalization_probe_value_[2];
        */
        int32_t inhibit_strum_;
        
        double adc_lp_[ADC_CHANNEL_LAST];
        double transpose_;
        double fm_cv_;
        double cv_c1_;
        double cv_low_;
        int32_t chord_;
        
        //bool normalization_probe_enabled_;
        //bool normalization_probe_forced_state_;
        
        static ChannelSettings channel_settings_[ADC_CHANNEL_LAST];
        
        DISALLOW_COPY_AND_ASSIGN(ReadInputs);
    };

}  // namespace rings

#endif /* read_inputs_h */
