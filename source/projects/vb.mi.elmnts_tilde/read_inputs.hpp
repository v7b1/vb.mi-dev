//
//  read_Inputs.hpp
//  vb.mi-dev
//
//  Created by vb on 10.11.18.
//
//


//
// modified version of CV scaler.

#ifndef read_inputs_hpp
#define read_inputs_hpp


#include "stmlib/stmlib.h"

//#include "elements/drivers/gate_input.h"

namespace elements {
    
    /* static */
    
    enum Potentiometer {
        POT_EXCITER_ENVELOPE_SHAPE,
        POT_EXCITER_BOW_LEVEL,
        POT_EXCITER_BOW_TIMBRE,
        POT_EXCITER_BOW_TIMBRE_ATTENUVERTER,
        POT_EXCITER_BLOW_LEVEL,
        POT_EXCITER_BLOW_META,
        POT_EXCITER_BLOW_META_ATTENUVERTER,
        POT_EXCITER_BLOW_TIMBRE,
        POT_EXCITER_BLOW_TIMBRE_ATTENUVERTER,
        POT_EXCITER_STRIKE_LEVEL,
        POT_EXCITER_STRIKE_META,
        POT_EXCITER_STRIKE_META_ATTENUVERTER,
        POT_EXCITER_STRIKE_TIMBRE,
        POT_EXCITER_STRIKE_TIMBRE_ATTENUVERTER,
        POT_RESONATOR_COARSE,
        POT_RESONATOR_FINE,
        POT_RESONATOR_FM_ATTENUVERTER,
        POT_RESONATOR_GEOMETRY,
        POT_RESONATOR_GEOMETRY_ATTENUVERTER,
        POT_RESONATOR_BRIGHTNESS,
        POT_RESONATOR_BRIGHTNESS_ATTENUVERTER,
        POT_RESONATOR_DAMPING,
        POT_RESONATOR_DAMPING_ATTENUVERTER,
        POT_RESONATOR_POSITION,
        POT_RESONATOR_POSITION_ATTENUVERTER,
        POT_SPACE,
        POT_SPACE_ATTENUVERTER,
        POT_LAST                // 27
    };
    
    enum CvAdcChannel {
        CV_ADC_V_OCT,
        CV_ADC_FM,
        CV_ADC_PRESSURE,
        CV_ADC_EXCITER_BOW_TIMBRE,
        CV_ADC_EXCITER_BLOW_META,
        CV_ADC_EXCITER_BLOW_TIMBRE,
        CV_ADC_EXCITER_STRIKE_META,
        CV_ADC_EXCITER_STRIKE_TIMBRE,
        CV_ADC_RESONATOR_DAMPING,
        CV_ADC_RESONATOR_GEOMETRY,
        CV_ADC_RESONATOR_POSITION,
        CV_ADC_RESONATOR_BRIGHTNESS,
        CV_ADC_SPACE,
        CV_ADC_CHANNEL_LAST     // 13
    };
    

    
    struct CalibrationSettings {
        double pitch_offset;
        double pitch_scale;
        double offset[CV_ADC_CHANNEL_LAST];
        uint8_t boot_in_easter_egg_mode;
        uint8_t resonator_model;
        uint8_t padding[62];
    };
    

    class Patch;
    class PerformanceState;
    
    enum Law {
        LAW_LINEAR,
        LAW_QUADRATIC_BIPOLAR,
        LAW_QUARTIC_BIPOLAR,
        LAW_QUANTIZED_NOTE
    };
    
    class ReadInputs {
    public:
        ReadInputs() { }
        ~ReadInputs() { }
        
        void Init();
        void Read(Patch* patch, PerformanceState* state);
        void ReadPanelPot(uint8_t index, double value);

        
        bool exciter_low() {
            return pot_lp_[POT_EXCITER_BOW_TIMBRE_ATTENUVERTER] < -3.0 &&
            pot_lp_[POT_EXCITER_BLOW_META_ATTENUVERTER] < -3.0 &&
            pot_lp_[POT_EXCITER_BLOW_TIMBRE_ATTENUVERTER] < -3.0 &&
            pot_lp_[POT_EXCITER_STRIKE_META_ATTENUVERTER] < -3.0 &&
            pot_lp_[POT_EXCITER_STRIKE_TIMBRE_ATTENUVERTER] < -3.0 &&
            pot_lp_[POT_EXCITER_BLOW_TIMBRE] < 0.2 &&
            pot_lp_[POT_EXCITER_BOW_TIMBRE] < 0.2 &&
            pot_lp_[POT_EXCITER_STRIKE_TIMBRE] < 0.2;
        }
        
        bool resonator_high() {
            return pot_lp_[POT_RESONATOR_GEOMETRY_ATTENUVERTER] > 3.0 &&
            pot_lp_[POT_RESONATOR_BRIGHTNESS_ATTENUVERTER] > 3.0 &&
            pot_lp_[POT_RESONATOR_DAMPING_ATTENUVERTER] > 3.0 &&
            pot_lp_[POT_RESONATOR_POSITION_ATTENUVERTER] > 3.0 &&
            pot_lp_[POT_SPACE_ATTENUVERTER] > 3.0 &&
            pot_lp_[POT_RESONATOR_DAMPING] > 0.8 &&
            pot_lp_[POT_RESONATOR_POSITION] > 0.8 &&
            pot_lp_[POT_SPACE] > 0.8;
        }
        
        
        inline bool gate() const { return gate_; }
        
        inline bool boot_in_easter_egg_mode() const {
            return calibration_settings_.boot_in_easter_egg_mode;
        }
        
        inline uint8_t resonator_model() const {
            return calibration_settings_.resonator_model;
        }
        
        inline void set_boot_in_easter_egg_mode(bool boot_in_easter_egg_mode) {
            calibration_settings_.boot_in_easter_egg_mode = boot_in_easter_egg_mode;
        }
        
        inline void set_resonator_model(uint8_t resonator_model) {
            calibration_settings_.resonator_model = resonator_model;
        }
        
        inline bool freshly_baked() const {
            return freshly_baked_;
        }
        
        
        double cv_floats[CV_ADC_CHANNEL_LAST+1];
        
    private:
        CalibrationSettings calibration_settings_;
        //GateInput gate_input_;
        
        bool freshly_baked_;
        bool gate_;
        bool previous_gate_;
        double pot_raw_[POT_LAST];
        double pot_lp_[POT_LAST];
        double pot_quantized_[POT_LAST];
        double note_;
        double modulation_;
        double cv_c1_;
        
        
        static Law law_[POT_LAST];
        
        DISALLOW_COPY_AND_ASSIGN(ReadInputs);
    };
    
}  // namespace elements


#endif /* read_inputs_hpp */
