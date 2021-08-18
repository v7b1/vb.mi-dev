//
//  read_Inputs.cpp
//  vb.mi-dev
//
//  Created by vb on 10.11.18.
//
//

#include "read_inputs.hpp"

#include <algorithm>

#include "stmlib/dsp/dsp.h"


namespace marbles {
    
    using namespace std;
    using namespace stmlib;
    
    /* static */
    const CvReaderChannel::Settings ReadInputs::channel_settings_[] = {
        // cv_lp | pot_scale | pot_offset | pot_lp | min | max | hysteresis
        // ADC_CHANNEL_T_RATE,
        { 0.2f, 120.0f, -60.0f, 0.05f, -120.0f, 120.0f, 0.001f },       // pot_lp: 0.01
        //{ 0.2f, 1.0f, 0.0f, 0.05f, 10.0f, 500.0f, 0.001f },
        // ADC_CHANNEL_T_BIAS,
        { 0.05f, 1.05f, -0.025f, 0.01f, 0.0f, 1.0f, 0.00f },
        // ADC_CHANNEL_T_JITTER,
        { 0.05f, 1.0f, 0.0f, 0.01f, 0.0f, 1.0f, 0.00f },
        // ADC_CHANNEL_DEJA_VU_AMOUNT,
        { 0.05f, 1.0f, 0.0f, 0.01f, 0.0f, 1.0f, 0.00f },
        // ADC_CHANNEL_X_SPREAD_2 / ADC_CHANNEL_DEJA_VU_LENGTH,
        { 0.05f, 1.0f, 0.0f, 0.01f, 0.0f, 1.0f, 0.00f },
        // ADC_CHANNEL_X_SPREAD,
        { 0.1f, 1.0f, 0.0f, 0.01f, 0.0f, 1.0f, 0.01f },
        // ADC_CHANNEL_X_BIAS,
        { 0.1f, 1.0f, 0.0f, 0.01f, 0.0f, 1.0f, 0.02f },
        // ADC_CHANNEL_X_STEPS,
        { 0.05f, 1.0f, 0.0f, 0.01f, 0.0f, 1.0f, 0.02f },
    };
    
    void ReadInputs::Init(CalibrationData* calibration_data) {
        calibration_data_ = calibration_data;
        
        for (int i = 0; i < ADC_CHANNEL_LAST; ++i) {
            channel_[i].Init(
                             &calibration_data_->adc_scale[i],
                             &calibration_data_->adc_offset[i],
                             channel_settings_[i]);
        }
        
        fill(&attenuverter_[0], &attenuverter_[ADC_CHANNEL_LAST], 1.0f);
        
        // Set virtual attenuverter to 12 o'clock to ignore the non-existing
        // CV input for DEJA VU length.
        //attenuverter_[ADC_CHANNEL_DEJA_VU_LENGTH] = 0.5f;     // skip this for now, vb
        
        // Set virtual attenuverter to a little more than 100% to
        // compensate for op-amp clipping and get full parameter swing.
        // don't need this anymore, vb
//        attenuverter_[ADC_CHANNEL_DEJA_VU_AMOUNT] = 1.01f;
//        attenuverter_[ADC_CHANNEL_T_BIAS] = 1.01f;
//        attenuverter_[ADC_CHANNEL_T_JITTER] = 1.01f;
//        attenuverter_[ADC_CHANNEL_X_SPREAD] = 1.01f;
//        attenuverter_[ADC_CHANNEL_X_BIAS] = 1.01f;
//        attenuverter_[ADC_CHANNEL_X_STEPS] = 1.01f;
    }

    
    void ReadInputs::Process(const float* raw_values, float* output) {
        // T_RATE param
        output[0] = channel_[0].Process_full(
                                           raw_values[ADC_GROUP_POT],
                                           raw_values[ADC_GROUP_CV]);
        // and the rest
        for (int i = 1; i < ADC_CHANNEL_LAST; ++i) {
            output[i] = channel_[i].Process_vb(
                                    raw_values[ADC_GROUP_POT + i],
                                    raw_values[ADC_GROUP_CV + i]);
            /*output[i] = channel_[i].Process(
                                               raw_values[ADC_GROUP_POT + i],
                                               raw_values[ADC_GROUP_CV + i],
                                               attenuverter_[i]);*/
        }
        //std::cout << "x_spread: " << raw_values[ADC_CHANNEL_X_SPREAD] << " + " << raw_values[ADC_CHANNEL_X_SPREAD + ADC_CHANNEL_LAST] << "attenu: " << attenuverter_[ADC_CHANNEL_X_SPREAD] << "\n";
        //std::cout << "dejavu length: " << raw_values[ADC_CHANNEL_DEJA_VU_LENGTH] << " + " << raw_values[ADC_CHANNEL_DEJA_VU_LENGTH + ADC_CHANNEL_LAST] << " attenu: " << attenuverter_[ADC_CHANNEL_DEJA_VU_LENGTH] << "\n";
        //std::cout << "t_rate: " << output[ADC_CHANNEL_T_RATE] << "\n";
    }
    
    // TODO: hier stimmt etwas nicht! --------------------
    /* external clock input */
    void ReadInputs::ReadClocks(Block *block, size_t size, uint8_t* inClocks) {
        for (int i = 0; i < CLOCK_INPUT_LAST; ++i) {
            previous_flags_[i] = ExtractGateFlags(
                                    previous_flags_[i],
                                    inClocks[i]);
            block->input[i][0] = previous_flags_[i];
        }
        
        // Extend gate input data to the next samples.
        // TODO: do we need this at all?  vb
        // yes, at least for the scale-recorder
        
        for (size_t j = 1; j < size; ++j) {
            for (int i = 0; i < CLOCK_INPUT_LAST; ++i) {
                block->input[i][j] = \
                previous_flags_[i] & GATE_FLAG_HIGH;
            }
        }
    }

    
}  // namespace marbles
