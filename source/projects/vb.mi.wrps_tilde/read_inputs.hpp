//
//  read_inputs.hpp
//  vb.mi.wrps_tilde
//
//  Created by vb on 23.03.19.
//

#ifndef read_inputs_hpp
#define read_inputs_hpp


#include "stmlib/stmlib.h"

namespace warps {
    
    enum AdcChannel {
        ADC_LEVEL_1_CV,
        ADC_LEVEL_2_CV,
        ADC_ALGORITHM_CV,
        ADC_PARAMETER_CV,
        ADC_LEVEL_1_POT,
        ADC_LEVEL_2_POT,
        ADC_ALGORITHM_POT,
        ADC_PARAMETER_POT,
        ADC_LAST
    };
    
    class Parameters;
    
    
    class ReadInputs {
    public:
        ReadInputs() { }
        ~ReadInputs() { }
        
        void Init();
        void Read(Parameters* parameters, double *adc_inputs, short* patched);
        
        inline uint8_t easter_egg_digit() const {
            if (lp_state_[ADC_LEVEL_1_POT] < 0.05 && \
                lp_state_[ADC_LEVEL_2_POT] < 0.05 && \
                lp_state_[ADC_PARAMETER_POT] < 0.05) {
                return static_cast<uint8_t>(
                                            UnwrapPot(lp_state_[ADC_ALGORITHM_POT]) * 8.0 + 0.5);
            } else {
                return 0;
            }
        }
      
        double UnwrapPot(double x) const;
        
    private:
        void DetectNormalization();
        

//        CalibrationData* calibration_data_;
        
        double lp_state_[ADC_LAST];
        double note_cv_;
        double note_pot_;
        double note_pot_quantized_;
        double cv_c1_;
        double cv_low_[2];
        
        
        DISALLOW_COPY_AND_ASSIGN(ReadInputs);
    };
    
}


#endif /* read_inputs_hpp */
