// Copyright 2014 Olivier Gillet.
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
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
//
// Modulator.

#include "warps/dsp/modulator.h"

#include <algorithm>

#include "stmlib/dsp/units.h"

//#include "warps/drivers/debug_pin.h"
#include "warps/resources.h"


namespace warps {

using namespace std;
using namespace stmlib;

void Modulator::Init(double sample_rate) {
  bypass_ = false;
  easter_egg_ = false;
  
  for (int32_t i = 0; i < 2; ++i) {
    amplifier_[i].Init();
    src_up_[i].Init();
    quadrature_transform_[i].Init(lut_ap_poles, LUT_AP_POLES_SIZE);
  }
  src_down_.Init();
  
  xmod_oscillator_.Init(sample_rate);
  vocoder_oscillator_.Init(sample_rate);
  quadrature_oscillator_.Init(sample_rate);
  vocoder_.Init(sample_rate);
    
    parameters_.channel_drive[0] = 0.5;
    parameters_.channel_drive[1] = 0.5;
    parameters_.modulation_algorithm = 0.0;
    parameters_.modulation_parameter = 0.0;
    
    // Easter egg parameters.
    parameters_.frequency_shift_pot = 0.0;
    parameters_.frequency_shift_cv = 0.0;
    parameters_.phase_shift = 0.0;
    parameters_.note = 48;
    
    parameters_.carrier_shape = 2;
  
  previous_parameters_.carrier_shape = 0;
  previous_parameters_.channel_drive[0] = 0.0;
  previous_parameters_.channel_drive[1] = 0.0;
  previous_parameters_.modulation_algorithm = 0.0;
  previous_parameters_.modulation_parameter = 0.0;
  previous_parameters_.note = 48.0;

  feedback_sample_ = 0.0;
    
    //vb
    /*
    fill(&buffer_[0][0], &buffer_[0][kMaxBlockSize], 0.0);
    fill(&buffer_[1][0], &buffer_[1][kMaxBlockSize], 0.0);
    fill(&buffer_[2][0], &buffer_[2][kMaxBlockSize], 0.0);*/
}

    void Modulator::ProcessEasterEgg(
                                     double** input, double** output,
                                     size_t size) {
        double* carrier = buffer_[0];
        double* carrier_i = &src_buffer_[0][0];
        double* carrier_q = &src_buffer_[0][size];
        
        // Generate the I/Q components.
        if (parameters_.carrier_shape) {
            double d = parameters_.frequency_shift_pot - 0.5;
            double linear_modulation_amount = 1.0 - 14.0 * d * d;
            if (linear_modulation_amount < 0.0) {
                linear_modulation_amount = 0.0;
            }
            double frequency = parameters_.frequency_shift_pot;
            frequency += linear_modulation_amount * parameters_.frequency_shift_cv;
            
            double direction = frequency >= 0.5 ? 1.0 : -1.0;
            frequency = 2.0 * fabs(frequency - 0.5);
            frequency = frequency <= 0.4
            ? frequency * frequency * frequency * 62.5
            : 4.0 * SemitonesToRatio(180.0 * (frequency - 0.4));
            frequency *= SemitonesToRatio(
                                          parameters_.frequency_shift_cv * 60.0 * \
                                          (1.0 - linear_modulation_amount) * direction);
            frequency *= direction;
            
            double shape = static_cast<double>(parameters_.carrier_shape - 1) * 0.5;
            quadrature_oscillator_.Render(shape, frequency, carrier_i, carrier_q, size);
        } else {
            for (size_t i = 0; i < size; ++i) {
                carrier[i] = input[0][i];            // TODO: avoid copying? vb
            }
            quadrature_transform_[0].Process(carrier, carrier_i, carrier_q, size);
            
            ParameterInterpolator phase_shift(
                                              &previous_parameters_.phase_shift,
                                              parameters_.phase_shift,
                                              size);
            
            for (size_t i = 0; i < size; ++i) {
                double x_i = carrier_i[i];
                double x_q = carrier_q[i];
                double angle = phase_shift.Next();
                double r_sin = Interpolate(lut_sin, angle, 1024.0);
                double r_cos = Interpolate(lut_sin + 256, angle, 1024.0);
                carrier_i[i] = r_sin * x_i + r_cos * x_q;
                carrier_q[i] = r_sin * x_q - r_cos * x_i;
            }
        }
        
        // Setup parameter interpolation.
        ParameterInterpolator mix(
                                  &previous_parameters_.modulation_parameter,
                                  parameters_.modulation_parameter,
                                  size);
        ParameterInterpolator feedback_amount(
                                              &previous_parameters_.channel_drive[0],
                                              parameters_.channel_drive[0],
                                              size);
        ParameterInterpolator dry_wet(
                                      &previous_parameters_.channel_drive[1],
                                      parameters_.channel_drive[1],
                                      size);
        
        double feedback_sample = feedback_sample_;
        for (size_t i = 0; i < size; ++i) {
            double timbre = mix.Next();
            double modulator_i, modulator_q;
            
            // Start from the signal from input 2, with non-linear gain.
            //double in = static_cast<double>(input->r) / 32768.0;
            double in = input[1][i];
            
            if (parameters_.carrier_shape) {
                //in += static_cast<double>(input->l) / 32768.0;
                in += input[0][i];
            }
            
            double modulator = in;
            
            // Apply feedback if necessary, and soft limit.
            double amount = feedback_amount.Next();
            amount *= (2.0 - amount);
            amount *= (2.0 - amount);
            
            // mic.w feedback amount tweak.
            double max_fb = 1.0 + 2.0 * (timbre - 0.5) * (timbre - 0.5);
            modulator += amount * (
                                   SoftClip(modulator + max_fb * feedback_sample * amount) - modulator);
            
            quadrature_transform_[1].Process(modulator, &modulator_i, &modulator_q);
            
            // Modulate!
            double a = *carrier_i++ * modulator_i;
            double b = *carrier_q++ * modulator_q;
            double up = a - b;
            double down = a + b;
            double lut_index = timbre;
            double fade_in = Interpolate(lut_xfade_in, lut_index, 256.0);
            double fade_out = Interpolate(lut_xfade_out, lut_index, 256.0);
            double main = up * fade_in + down * fade_out;
            double aux = down * fade_in + up * fade_out;
            
            // Simple LP to prevent feedback of high-frequencies.
            ONE_POLE(feedback_sample, main, 0.2);
            
            double wet_dry = 1.0 - dry_wet.Next();
            main += wet_dry * (in - main);
            aux += wet_dry * (in - aux);
            
            //output->l = Clip16(static_cast<int32_t>(main * 32768.0));
            //output->r = Clip16(static_cast<int32_t>(aux * 32768.0));
            output[0][i] = main;        // TODO: do we need to clamp values?
            output[1][i] = aux;
            //++output;
            //++input;
        }
        feedback_sample_ = feedback_sample;
        previous_parameters_ = parameters_;
    }

    
    void Modulator::Process(double** input, double** output, size_t size) {
        
        if (bypass_) {
            copy(&input[0][0], &input[0][size], &output[0][0]);
            copy(&input[1][0], &input[1][size], &output[1][0]);
            return;
        } else if (easter_egg_) {
            ProcessEasterEgg(input, output, size);
            return;
        }
        double* carrier = buffer_[0];
        double* modulator = buffer_[1];
        double* main_output = buffer_[0];
        double* aux_output = buffer_[2];
        double* oversampled_carrier = src_buffer_[0];
        double* oversampled_modulator = src_buffer_[1];
        double* oversampled_output = src_buffer_[0];
        
        // 0.0: use cross-modulation algorithms. 1.0: use vocoder.
        double vocoder_amount = (
                                 parameters_.modulation_algorithm - 0.7) * 20.0 + 0.5;
        CONSTRAIN(vocoder_amount, 0.0, 1.0);
        //std::cout << "voc amount: " << vocoder_amount << std::endl;
        
        if (!parameters_.carrier_shape) {
            fill(&aux_output[0], &aux_output[size], 0.0);
        }
        
        // Convert audio inputs to double and apply VCA/saturation (5.8% per channel)
        //short* input_samples = &input->l;
        for (int32_t i = parameters_.carrier_shape ? 1 : 0; i < 2; ++i) {
            amplifier_[i].Process(
                                  parameters_.channel_drive[i],
                                  1.0 - vocoder_amount,
                                  input[i],   // + i,
                                  buffer_[i],
                                  aux_output,
                                  1,    // 2
                                  size);
        }
        
        // If necessary, render carrier. Otherwise, sum signals 1 and 2 for aux out.
        if (parameters_.carrier_shape) {
            // Scale phase-modulation input.
            for (size_t i = 0; i < size; ++i) {
                internal_modulation_[i] = input[0][i];  //static_cast<double>(input[i].l) / 32768.0;  // TODO: avoid copy?
            }
            // Xmod: sine, triangle saw.
            // Vocoder: saw, pulse, noise.
            OscillatorShape xmod_shape = static_cast<OscillatorShape>(
                                                                      parameters_.carrier_shape - 1);
            OscillatorShape vocoder_shape = static_cast<OscillatorShape>(
                                                                         parameters_.carrier_shape + 1);
            
            const double kXmodCarrierGain = 0.5;
            
            // Outside of the transition zone between the cross-modulation and vocoding
            // algorithm, we need to render only one of the two oscillators.
            if (vocoder_amount == 0.0) {
                xmod_oscillator_.Render(
                                        xmod_shape,
                                        parameters_.note,
                                        internal_modulation_,
                                        aux_output,
                                        size);
                for (size_t i = 0; i < size; ++i) {
                    carrier[i] = aux_output[i] * kXmodCarrierGain;
                }
            } else if (vocoder_amount >= 0.5) {
                //std::cout << "voc_amount >= 0.5 \n";
                double carrier_gain = vocoder_oscillator_.Render(
                                                                 vocoder_shape,
                                                                 parameters_.note,
                                                                 internal_modulation_,
                                                                 aux_output,
                                                                 size);
                for (size_t i = 0; i < size; ++i) {
                    carrier[i] = aux_output[i] * carrier_gain;
                }
            } else {
                double balance = vocoder_amount * 2.0;
                xmod_oscillator_.Render(
                                        xmod_shape,
                                        parameters_.note,
                                        internal_modulation_,
                                        carrier,
                                        size);
                double carrier_gain = vocoder_oscillator_.Render(
                                                                 vocoder_shape,
                                                                 parameters_.note,
                                                                 internal_modulation_,
                                                                 aux_output,
                                                                 size);
                for (size_t i = 0; i < size; ++i) {
                    double a = carrier[i];
                    double b = aux_output[i];
                    aux_output[i] = a + (b - a) * balance;
                    a *= kXmodCarrierGain;
                    b *= carrier_gain;
                    carrier[i] = a + (b - a) * balance;
                }
            }
        }
        
        if (vocoder_amount < 0.5) {
            src_up_[0].Process(carrier, oversampled_carrier, size);
            src_up_[1].Process(modulator, oversampled_modulator, size);
            
            double algorithm = min(parameters_.modulation_algorithm * 8.0, 5.999);
            double previous_algorithm = min(
                                            previous_parameters_.modulation_algorithm * 8.0, 5.999);
            
            MAKE_INTEGRAL_FRACTIONAL(algorithm);
            MAKE_INTEGRAL_FRACTIONAL(previous_algorithm);
            
            if (algorithm_integral != previous_algorithm_integral) {
                previous_algorithm_fractional = algorithm_fractional;
            }
            
            (this->*xmod_table_[algorithm_integral])(
                                                     previous_algorithm_fractional,
                                                     algorithm_fractional,
                                                     previous_parameters_.skewed_modulation_parameter(),
                                                     parameters_.skewed_modulation_parameter(),
                                                     oversampled_modulator,
                                                     oversampled_carrier,
                                                     oversampled_output,
                                                     size * kOversampling);
            
            src_down_.Process(oversampled_output, main_output, size * kOversampling);
        } else {
            //std::cout << "voc_shape >= 0.5 ++ \n";
            double release_time = 4.0 * (parameters_.modulation_algorithm - 0.75);
            CONSTRAIN(release_time, 0.0, 1.0);
            
            vocoder_.set_release_time(release_time * (2.0 - release_time));
            vocoder_.set_formant_shift(parameters_.modulation_parameter);
            vocoder_.Process(modulator, carrier, main_output, size);
        }
        
        // Cross-fade to raw modulator for the transition between cross-modulation
        // algorithms and vocoding algorithms.
        double transition_gain = 2.0 * (vocoder_amount < 0.5
                                        ? vocoder_amount
                                        : 1.0 - vocoder_amount);
        if (transition_gain != 0.0) {
            for (size_t i = 0; i < size; ++i) {
                main_output[i] += transition_gain * (modulator[i] - main_output[i]);
            }
        }
        
        /*
         // Convert back to integer and clip.
         while (size--) {
         output->l = Clip16(static_cast<int32_t>(*main_output * 32768.0));
         output->r = Clip16(static_cast<int32_t>(*aux_output * 16384.0));
         ++main_output;
         ++aux_output;
         ++output;
         }*/

        for (int i=0; i<size; i++) {
            double main = main_output[i];
            double aux = aux_output[i];
            CONSTRAIN(main, -1.0, 1.0);
            CONSTRAIN(aux, -1.0, 1.0);
            output[0][i] = main;
            output[1][i] = aux;
        }
        previous_parameters_ = parameters_;
    }
    


/* static */
inline double Modulator::Diode(double x) {
  // Approximation of diode non-linearity from:
  // Julian Parker - "A simple Digital model of the diode-based ring-modulator."
  // Proc. DAFx-11
  double sign = x > 0.0 ? 1.0 : -1.0;
  double dead_zone = fabs(x) - 0.667;
  dead_zone += fabs(dead_zone);
  dead_zone *= dead_zone;
  return 0.04324765822726063 * dead_zone * sign;
}

/* static */
template<>
inline double Modulator::Xmod<ALGORITHM_XFADE>(
    double x_1, double x_2, double parameter) {
  double fade_in = Interpolate(lut_xfade_in, parameter, 256.0);
  double fade_out = Interpolate(lut_xfade_out, parameter, 256.0);
  return x_1 * fade_in + x_2 * fade_out;
}

/* static */
template<>
inline double Modulator::Xmod<ALGORITHM_FOLD>(
    double x_1, double x_2, double parameter) {
  double sum = 0.0;
  sum += x_1;
  sum += x_2;
  sum += x_1 * x_2 * 0.25;
  sum *= 0.02 + parameter;
  const double kScale = 2048.0 / ((1.0 + 1.0 + 0.25) * 1.02);
  return Interpolate(lut_bipolar_fold + 2048, sum, kScale);
}

/* static */
template<>
inline double Modulator::Xmod<ALGORITHM_ANALOG_RING_MODULATION>(
    double modulator, double carrier, double parameter) {
  carrier *= 2.0;
  double ring = Diode(modulator + carrier) + Diode(modulator - carrier);
  ring *= (4.0 + parameter * 24.0);
  return SoftLimit(ring);
}

/* static */
template<>
inline double Modulator::Xmod<ALGORITHM_DIGITAL_RING_MODULATION>(
    double x_1, double x_2, double parameter) {
  double ring = 4.0 * x_1 * x_2 * (1.0 + parameter * 8.0);
  return ring / (1.0 + fabs(ring));
}

/* static */
template<>
inline double Modulator::Xmod<ALGORITHM_XOR>(
    double x_1, double x_2, double parameter) {
  short x_1_short = Clip16(static_cast<int32_t>(x_1 * 32768.0));
  short x_2_short = Clip16(static_cast<int32_t>(x_2 * 32768.0));
  double mod = static_cast<double>(x_1_short ^ x_2_short) / 32768.0;
  double sum = (x_1 + x_2) * 0.7;
  return sum + (mod - sum) * parameter;
}

/* static */
template<>
inline double Modulator::Xmod<ALGORITHM_COMPARATOR>(
    double modulator, double carrier, double parameter) {
  double x = parameter * 2.995;
  MAKE_INTEGRAL_FRACTIONAL(x)

  double direct = modulator < carrier ? modulator : carrier;
  double window = fabs(modulator) > fabs(carrier) ? modulator : carrier;
  double window_2 = fabs(modulator) > fabs(carrier)
      ? fabs(modulator)
      : -fabs(carrier);
  double threshold = carrier > 0.05 ? carrier : modulator;
  
  double sequence[4] = { direct, threshold, window, window_2 };
  double a = sequence[x_integral];
  double b = sequence[x_integral + 1];
  
  return a + (b - a) * x_fractional;
}

/* static */
template<>
inline double Modulator::Xmod<ALGORITHM_NOP>(
    double modulator, double carrier, double parameter) {
  return modulator;
}

/* static */
Modulator::XmodFn Modulator::xmod_table_[] = {
  &Modulator::ProcessXmod<ALGORITHM_XFADE, ALGORITHM_FOLD>,
  &Modulator::ProcessXmod<ALGORITHM_FOLD, ALGORITHM_ANALOG_RING_MODULATION>,
  &Modulator::ProcessXmod<
      ALGORITHM_ANALOG_RING_MODULATION, ALGORITHM_DIGITAL_RING_MODULATION>,
  &Modulator::ProcessXmod<ALGORITHM_DIGITAL_RING_MODULATION, ALGORITHM_XOR>,
  &Modulator::ProcessXmod<ALGORITHM_XOR, ALGORITHM_COMPARATOR>,
  &Modulator::ProcessXmod<ALGORITHM_COMPARATOR, ALGORITHM_NOP>,
};

}  // namespace warps
