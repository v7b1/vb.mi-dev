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
#include "stmlib/utils/gate_flags.h"

#include "marbles/cv_reader_channel.h"
#include "marbles/settings.h"
#include "dsp.h"


namespace marbles {
    
    enum AdcGroup {
        ADC_GROUP_POT = 0,
        ADC_GROUP_CV = ADC_CHANNEL_LAST
    };
    
    enum ClockInput {
        CLOCK_INPUT_T,
        CLOCK_INPUT_X,
        CLOCK_INPUT_LAST
    };
    
    struct CalibrationSettings {
        
    };
    
    
    enum Law {
        LAW_LINEAR,
        LAW_QUADRATIC_BIPOLAR,
        LAW_QUARTIC_BIPOLAR,
        LAW_QUANTIZED_NOTE
    };
    
    struct Block {
        float adc_value[kNumParameters * 2];
        bool input_patched[kNumInputs];
        
        stmlib::GateFlags input[kNumInputs][kBlockSize];
        float cv_output[kNumCvOutputs][kBlockSize];
        bool gate_output[kNumGateOutputs][kBlockSize + 2];
    };
    
    class ReadInputs {
    public:
        ReadInputs() { }
        ~ReadInputs() { }
        
        //void Init();
        void Init(CalibrationData* calibration_data);
        //void Process(const uint16_t* raw_values, float* output);
        void Process(const float* raw_values, float* output);
        
        inline const CvReaderChannel& channel(size_t index) {
            return channel_[index];
        }
        inline CvReaderChannel* mutable_channel(size_t index) {
            return &channel_[index];
        }
        inline void set_attenuverter(int index, float value) {
            attenuverter_[index] = value;
        }
        
        //void ReadClocks(const IOBuffer::Slice& slice, size_t size, uint8_t* inClocks);
        void ReadClocks(Block *block, size_t size, uint8_t* inClocks);
        
    private:
        CalibrationData* calibration_data_;
        bool gate_;
        bool previous_gate_;
        CvReaderChannel channel_[ADC_CHANNEL_LAST];
        float attenuverter_[ADC_CHANNEL_LAST];
        static const CvReaderChannel::Settings channel_settings_[ADC_CHANNEL_LAST];
        stmlib::GateFlags previous_flags_[CLOCK_INPUT_LAST];

        
        DISALLOW_COPY_AND_ASSIGN(ReadInputs);
    };
    
}  // namespace marbles


#endif /* read_inputs_hpp */
