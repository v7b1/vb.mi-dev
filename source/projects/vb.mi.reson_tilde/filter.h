//
//  filter.h
//  vb.mi.el.resonator_tilde
//
//  Created by vb on 17.05.19.
//

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
// Zero-delay-feedback filters (one pole and SVF).
// Naive SVF.

#ifndef STMLIB_DSP_FILTER_H_
#define STMLIB_DSP_FILTER_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/delay_line.h"

#include <cmath>
#include <algorithm>

    
enum FilterMode {
    FILTER_MODE_LOW_PASS,
    FILTER_MODE_BAND_PASS,
    FILTER_MODE_BAND_PASS_NORMALIZED,
    FILTER_MODE_HIGH_PASS
};
    
enum FrequencyApproximation {
    FREQUENCY_EXACT,
    FREQUENCY_ACCURATE,
    FREQUENCY_FAST,
    FREQUENCY_DIRTY
};


#define M_PI_POW_2 M_PI * M_PI
#define M_PI_POW_3 M_PI_POW_2 * M_PI
#define M_PI_POW_5 M_PI_POW_3 * M_PI_POW_2
#define M_PI_POW_7 M_PI_POW_5 * M_PI_POW_2
#define M_PI_POW_9 M_PI_POW_7 * M_PI_POW_2
#define M_PI_POW_11 M_PI_POW_9 * M_PI_POW_2
    
    class DCBlocker {
    public:
        DCBlocker() { }
        ~DCBlocker() { }
        
        void Init(double pole) {
            x_ = 0.0;
            y_ = 0.0;
            pole_ = pole;
        }
        
        inline void Process(double* in_out, size_t size) {
            double x = x_;
            double y = y_;
            const double pole = pole_;
            while (size--) {
                double old_x = x;
                x = *in_out;
                *in_out++ = y = y * pole + x - old_x;
            }
            x_ = x;
            y_ = y;
        }
        
    private:
        double pole_;
        double x_;
        double y_;
    };
    
    class OnePole {
    public:
        OnePole() { }
        ~OnePole() { }
        
        void Init() {
            set_f<FREQUENCY_DIRTY>(0.01);
            Reset();
        }
        
        void Reset() {
            state_ = 0.0;
        }
        
        template<FrequencyApproximation approximation>
        static inline double tangens(double f) {
            if (approximation == FREQUENCY_EXACT) {
                // Clip coefficient to about 100.
                f = f < 0.497 ? f : 0.497;
                return tan(M_PI * f);
            } else if (approximation == FREQUENCY_DIRTY) {
                // Optimized for frequencies below 8kHz.
                const double a = 3.736e-01 * M_PI_POW_3;
                return f * (M_PI + a * f * f);
            } else if (approximation == FREQUENCY_FAST) {
                // The usual tangensent approximation uses 3.1755e-01 and 2.033e-01, but
                // the coefficients used here are optimized to minimize error for the
                // 16Hz to 16kHz range, with a sample rate of 48kHz.
                const double a = 3.260e-01 * M_PI_POW_3;
                const double b = 1.823e-01 * M_PI_POW_5;
                double f2 = f * f;
                return f * (M_PI + f2 * (a + b * f2));
            } else if (approximation == FREQUENCY_ACCURATE) {
                // These coefficients don't need to be tweaked for the audio range.
                const double a = 3.333314036e-01 * M_PI_POW_3;
                const double b = 1.333923995e-01 * M_PI_POW_5;
                const double c = 5.33740603e-02 * M_PI_POW_7;
                const double d = 2.900525e-03 * M_PI_POW_9;
                const double e = 9.5168091e-03 * M_PI_POW_11;
                double f2 = f * f;
                return f * (M_PI + f2 * (a + f2 * (b + f2 * (c + f2 * (d + f2 * e)))));
            }
        }
        
        // Set frequency and resonance from true units. Various approximations
        // are available to avoid the cost of tanf.
        template<FrequencyApproximation approximation>
        inline void set_f(double f) {
            g_ = tangens<approximation>(f);
            gi_ = 1.0 / (1.0 + g_);
        }
        
        template<FilterMode mode>
        inline double Process(double in) {
            double lp;
            lp = (g_ * in + state_) * gi_;
            state_ = g_ * (in - lp) + lp;
            
            if (mode == FILTER_MODE_LOW_PASS) {
                return lp;
            } else if (mode == FILTER_MODE_HIGH_PASS) {
                return in - lp;
            } else {
                return 0.0;
            }
        }
        
        template<FilterMode mode>
        inline void Process(double* in_out, size_t size) {
            while (size--) {
                *in_out = Process<mode>(*in_out);
                ++in_out;
            }
        }
        
    private:
        double g_;
        double gi_;
        double state_;
        
        DISALLOW_COPY_AND_ASSIGN(OnePole);
    };
    
    
    
    class Svf {
    public:
        Svf() { }
        ~Svf() { }
        
        void Init() {
            set_f_q<FREQUENCY_DIRTY>(0.01, 100.0);
            Reset();
        }
        
        void Reset() {
            state_1_ = state_2_ = 0.0;
            gain_ = 1.0;
        }
        
        // Copy settings from another filter.
        inline void set(const Svf& f) {
            g_ = f.g();
            r_ = f.r();
            h_ = f.h();
        }
        
        // Set all parameters from LUT.
        inline void set_g_r_h(double g, double r, double h) {
            g_ = g;
            r_ = r;
            h_ = h;
        }
        
        // Set frequency and resonance coefficients from LUT, adjust remaining
        // parameter.
        inline void set_g_r(double g, double r) {
            g_ = g;
            r_ = r;
            h_ = 1.0 / (1.0 + r_ * g_ + g_ * g_);
        }
        
        // Set frequency from LUT, resonance in true units, adjust the rest.
        inline void set_g_q(double g, double resonance) {
            g_ = g;
            r_ = 1.0 / resonance;
            h_ = 1.0 / (1.0 + r_ * g_ + g_ * g_);
        }
        
        // Set frequency and resonance from true units. Various approximations
        // are available to avoid the cost of tanf.
        template<FrequencyApproximation approximation>
        inline void set_f_q(double f, double resonance) {
            g_ = OnePole::tangens<approximation>(f);
            r_ = 1.0 / resonance;
            h_ = 1.0 / (1.0 + r_ * g_ + g_ * g_);
        }
        
        // Set frequency and resonance from true units. Various approximations
        // are available to avoid the cost of tanf.
        // vb
        inline void set_g_q_g(double g, double resonance, double gain) {
            g_ = g;
            r_ = 1.0 / resonance;
            h_ = 1.0 / (1.0 + r_ * g_ + g_ * g_);
            gain_ = gain;
        }
        
        // Set frequency and resonance from true units. Various approximations
        // are available to avoid the cost of tanf.
        // vb, add gain parameter
        template<FrequencyApproximation approximation>
        inline void set_f_q_g(double f, double resonance, double gain) {
            g_ = OnePole::tangens<approximation>(f);
            r_ = 1.0 / resonance;
            h_ = 1.0 / (1.0 + r_ * g_ + g_ * g_);
            gain_ = gain;
        }
        
        template<FilterMode mode>
        inline double Process(double in) {
            double hp, bp, lp;
            hp = (in - r_ * state_1_ - g_ * state_1_ - state_2_) * h_;
            bp = g_ * hp + state_1_;
            state_1_ = g_ * hp + bp;
            lp = g_ * bp + state_2_;
            state_2_ = g_ * bp + lp;
            
            if (mode == FILTER_MODE_LOW_PASS) {
                return lp;
            } else if (mode == FILTER_MODE_BAND_PASS) {
                return bp;
            } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                return bp * r_;
            } else if (mode == FILTER_MODE_HIGH_PASS) {
                return hp;
            }
        }
        
        template<FilterMode mode_1, FilterMode mode_2>
        inline void Process(double in, double* out_1, double* out_2) {
            double hp, bp, lp;
            hp = (in - r_ * state_1_ - g_ * state_1_ - state_2_) * h_;
            bp = g_ * hp + state_1_;
            state_1_ = g_ * hp + bp;
            lp = g_ * bp + state_2_;
            state_2_ = g_ * bp + lp;
            
            if (mode_1 == FILTER_MODE_LOW_PASS) {
                *out_1 = lp;
            } else if (mode_1 == FILTER_MODE_BAND_PASS) {
                *out_1 = bp;
            } else if (mode_1 == FILTER_MODE_BAND_PASS_NORMALIZED) {
                *out_1 = bp * r_;
            } else if (mode_1 == FILTER_MODE_HIGH_PASS) {
                *out_1 = hp;
            }
            
            if (mode_2 == FILTER_MODE_LOW_PASS) {
                *out_2 = lp;
            } else if (mode_2 == FILTER_MODE_BAND_PASS) {
                *out_2 = bp;
            } else if (mode_2 == FILTER_MODE_BAND_PASS_NORMALIZED) {
                *out_2 = bp * r_;
            } else if (mode_2 == FILTER_MODE_HIGH_PASS) {
                *out_2 = hp;
            }
        }
        
        template<FilterMode mode>
        inline void Process(const double* in, double* out, size_t size) {
            double hp, bp, lp;
            double state_1 = state_1_;
            double state_2 = state_2_;
            
            while (size--) {
                hp = (*in - r_ * state_1 - g_ * state_1 - state_2) * h_;
                bp = g_ * hp + state_1;
                state_1 = g_ * hp + bp;
                lp = g_ * bp + state_2;
                state_2 = g_ * bp + lp;
                
                double value;
                if (mode == FILTER_MODE_LOW_PASS) {
                    value = lp;
                } else if (mode == FILTER_MODE_BAND_PASS) {
                    value = bp;
                } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    value = bp * r_;
                } else if (mode == FILTER_MODE_HIGH_PASS) {
                    value = hp;
                }
                
                *out = value;
                ++out;
                ++in;
            }
            state_1_ = state_1;
            state_2_ = state_2;
        }
        
        template<FilterMode mode>
        inline void ProcessAdd(const double* in, double* out, size_t size, double gain) {
            double hp, bp, lp;
            double state_1 = state_1_;
            double state_2 = state_2_;
            
            while (size--) {
                hp = (*in - r_ * state_1 - g_ * state_1 - state_2) * h_;
                bp = g_ * hp + state_1;
                state_1 = g_ * hp + bp;
                lp = g_ * bp + state_2;
                state_2 = g_ * bp + lp;
                
                double value;
                if (mode == FILTER_MODE_LOW_PASS) {
                    value = lp;
                } else if (mode == FILTER_MODE_BAND_PASS) {
                    value = bp;
                } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    value = bp * r_;
                } else if (mode == FILTER_MODE_HIGH_PASS) {
                    value = hp;
                }
                
                *out += gain * value;
                ++out;
                ++in;
            }
            state_1_ = state_1;
            state_2_ = state_2;
        }
        
        template<FilterMode mode>
        inline void Process(const double* in, double* out, size_t size, size_t stride) {
            double hp, bp, lp;
            double state_1 = state_1_;
            double state_2 = state_2_;
            
            while (size--) {
                hp = (*in - r_ * state_1 - g_ * state_1 - state_2) * h_;
                bp = g_ * hp + state_1;
                state_1 = g_ * hp + bp;
                lp = g_ * bp + state_2;
                state_2 = g_ * bp + lp;
                
                double value;
                if (mode == FILTER_MODE_LOW_PASS) {
                    value = lp;
                } else if (mode == FILTER_MODE_BAND_PASS) {
                    value = bp;
                } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    value = bp * r_;
                } else if (mode == FILTER_MODE_HIGH_PASS) {
                    value = hp;
                }
                
                *out = value;
                out += stride;
                in += stride;
            }
            state_1_ = state_1;
            state_2_ = state_2;
        }
        
        inline void ProcessMultimode(
                                     const double* in,
                                     double* out,
                                     size_t size,
                                     double mode) {
            double hp, bp, lp;
            double state_1 = state_1_;
            double state_2 = state_2_;
            double hp_gain = mode < 0.5 ? -mode * 2.0 : -2.0 + mode * 2.0;
            double lp_gain = mode < 0.5 ? 1.0 - mode * 2.0 : 0.0;
            double bp_gain = mode < 0.5 ? 0.0 : mode * 2.0 - 1.0;
            while (size--) {
                hp = (*in - r_ * state_1 - g_ * state_1 - state_2) * h_;
                bp = g_ * hp + state_1;
                state_1 = g_ * hp + bp;
                lp = g_ * bp + state_2;
                state_2 = g_ * bp + lp;
                *out = hp_gain * hp + bp_gain * bp + lp_gain * lp;
                ++in;
                ++out;
            }
            state_1_ = state_1;
            state_2_ = state_2;
        }
        
        inline void ProcessMultimodeLPtoHP(
                                           const double* in,
                                           double* out,
                                           size_t size,
                                           double mode) {
            double hp, bp, lp;
            double state_1 = state_1_;
            double state_2 = state_2_;
            double hp_gain = std::min(-mode * 2.0 + 1.0, 0.0);
            double bp_gain = 1.0 - 2.0 * fabs(mode - 0.5);
            double lp_gain = std::max(1.0 - mode * 2.0, 0.0);
            while (size--) {
                hp = (*in - r_ * state_1 - g_ * state_1 - state_2) * h_;
                bp = g_ * hp + state_1;
                state_1 = g_ * hp + bp;
                lp = g_ * bp + state_2;
                state_2 = g_ * bp + lp;
                *out = hp_gain * hp + bp_gain * bp + lp_gain * lp;
                ++in;
                ++out;
            }
            state_1_ = state_1;
            state_2_ = state_2;
        }
        
        template<FilterMode mode>
        inline void Process(
                            const double* in, double* out_1, double* out_2, size_t size,
                            double gain_1, double gain_2) {
            double hp, bp, lp;
            double state_1 = state_1_;
            double state_2 = state_2_;
            
            while (size--) {
                hp = (*in - r_ * state_1 - g_ * state_1 - state_2) * h_;
                bp = g_ * hp + state_1;
                state_1 = g_ * hp + bp;
                lp = g_ * bp + state_2;
                state_2 = g_ * bp + lp;
                
                double value;
                if (mode == FILTER_MODE_LOW_PASS) {
                    value = lp;
                } else if (mode == FILTER_MODE_BAND_PASS) {
                    value = bp;
                } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    value = bp * r_;
                } else if (mode == FILTER_MODE_HIGH_PASS) {
                    value = hp;
                }
                
                *out_1 += value * gain_1;
                *out_2 += value * gain_2;
                ++out_1;
                ++out_2;
                ++in;
            }
            state_1_ = state_1;
            state_2_ = state_2;
        }
        
        
        
        inline void Process_bp_block(const double* in, double* center, double* sides, size_t size, double gain1, double gain2) {
            double hp, bp, lp;
            double state_1 = state_1_;
            double state_2 = state_2_;
            
            while (size--) {
                hp = (*in - r_ * state_1 - g_ * state_1 - state_2) * h_;
                bp = g_ * hp + state_1;
                state_1 = g_ * hp + bp;
                lp = g_ * bp + state_2;
                state_2 = g_ * bp + lp;
                
                bp *= gain_;
                
                *center += gain1 * bp;
                *sides += gain2 * bp;
                ++center;
                ++sides;
                ++in;
            }
            state_1_ = state_1;
            state_2_ = state_2;
        }
        
        inline void Process_bp_norm_block(const double* in, double* out, size_t size,
                                          stmlib::DelayLine<double, 1024> *d, double gain) {
            double hp, bp, lp;
            double state_1 = state_1_;
            double state_2 = state_2_;
            
            while (size--) {
                double input = 0.99 * d->Read() + *in;
                hp = (input - r_ * state_1 - g_ * state_1 - state_2) * h_;
                bp = g_ * hp + state_1;
                state_1 = g_ * hp + bp;
                lp = g_ * bp + state_2;
                state_2 = g_ * bp + lp;
                double s = bp * r_;
                d->Write(s);
                
                *out += s * gain * gain_;     // vb, apply gain;
                ++out;
                ++in;
            }
            state_1_ = state_1;
            state_2_ = state_2;
        }
        
        
        
        inline double g() const { return g_; }
        inline double r() const { return r_; }
        inline double h() const { return h_; }
        inline double gain() const { return gain_; }
        
    private:
        double g_;
        double r_;
        double h_;
        double gain_;
        
        double state_1_;
        double state_2_;
        
        DISALLOW_COPY_AND_ASSIGN(Svf);
    };
    
    
    
    // Naive Chamberlin SVF.
    class NaiveSvf {
    public:
        NaiveSvf() { }
        ~NaiveSvf() { }
        
        void Init() {
            set_f_q<FREQUENCY_DIRTY>(0.01, 100.0);
            Reset();
        }
        
        void Reset() {
            lp_ = bp_ = 0.0;
        }
        
        // Set frequency and resonance from true units. Various approximations
        // are available to avoid the cost of sinf.
        template<FrequencyApproximation approximation>
        inline void set_f_q(double f, double resonance) {
            f = f < 0.49 ? f : 0.497;
            if (approximation == FREQUENCY_EXACT) {
                f_ = 2.0 * sin(M_PI * f);
            } else {
                f_ = 2.0 * M_PI * f;
            }
            damp_ = 1.0 / resonance;
        }
        
        template<FilterMode mode>
        inline double Process(double in) {
            double hp, notch, bp_normalized;
            bp_normalized = bp_ * damp_;
            notch = in - bp_normalized;
            lp_ += f_ * bp_;
            hp = notch - lp_;
            bp_ += f_ * hp;
            
            if (mode == FILTER_MODE_LOW_PASS) {
                return lp_;
            } else if (mode == FILTER_MODE_BAND_PASS) {
                return bp_;
            } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                return bp_normalized;
            } else if (mode == FILTER_MODE_HIGH_PASS) {
                return hp;
            }
        }
        
        inline double lp() const { return lp_; }
        inline double bp() const { return bp_; }
        
        template<FilterMode mode>
        inline void Process(const double* in, double* out, size_t size) {
            double hp, notch, bp_normalized;
            double lp = lp_;
            double bp = bp_;
            while (size--) {
                bp_normalized = bp * damp_;
                notch = *in++ - bp_normalized;
                lp += f_ * bp;
                hp = notch - lp;
                bp += f_ * hp;
                
                if (mode == FILTER_MODE_LOW_PASS) {
                    *out++ = lp;
                } else if (mode == FILTER_MODE_BAND_PASS) {
                    *out++ = bp;
                } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    *out++ = bp_normalized;
                } else if (mode == FILTER_MODE_HIGH_PASS) {
                    *out++ = hp;
                }
            }
            lp_ = lp;
            bp_ = bp;
        }
        
        inline void Split(const double* in, double* low, double* high, size_t size) {
            double hp, notch, bp_normalized;
            double lp = lp_;
            double bp = bp_;
            while (size--) {
                bp_normalized = bp * damp_;
                notch = *in++ - bp_normalized;
                lp += f_ * bp;
                hp = notch - lp;
                bp += f_ * hp;
                *low++ = lp;
                *high++ = hp;
            }
            lp_ = lp;
            bp_ = bp;
        }
        
        template<FilterMode mode>
        inline void Process(const double* in, double* out, size_t size, size_t decimate) {
            double hp, notch, bp_normalized;
            double lp = lp_;
            double bp = bp_;
            size_t n = decimate - 1;
            while (size--) {
                bp_normalized = bp * damp_;
                notch = *in++ - bp_normalized;
                lp += f_ * bp;
                hp = notch - lp;
                bp += f_ * hp;
                
                ++n;
                if (n == decimate) {
                    if (mode == FILTER_MODE_LOW_PASS) {
                        *out++ = lp;
                    } else if (mode == FILTER_MODE_BAND_PASS) {
                        *out++ = bp;
                    } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                        *out++ = bp_normalized;
                    } else if (mode == FILTER_MODE_HIGH_PASS) {
                        *out++ = hp;
                    }
                    n = 0;
                }
            }
            lp_ = lp;
            bp_ = bp;
        }
        
    private:
        double f_;
        double damp_;
        double lp_;
        double bp_;
        
        DISALLOW_COPY_AND_ASSIGN(NaiveSvf);
    };
    
    
    
    // Modified Chamberlin SVF (Duane K. Wise)
    // http://www.dafx.ca/proceedings/papers/p_053.pdf
    class ModifiedSvf {
    public:
        ModifiedSvf() { }
        ~ModifiedSvf() { }
        
        void Init() {
            Reset();
        }
        
        void Reset() {
            lp_ = bp_ = 0.0;
        }
        
        inline void set_f_fq(double f, double fq) {
            f_ = f;
            fq_ = fq;
            x_ = 0.0;
        }
        
        template<FilterMode mode>
        inline void Process(const double* in, double* out, size_t size) {
            double lp = lp_;
            double bp = bp_;
            double x = x_;
            const double fq = fq_;
            const double f = f_;
            while (size--) {
                lp += f * bp;
                bp += -fq * bp -f * lp + *in;
                if (mode == FILTER_MODE_BAND_PASS ||
                    mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    bp += x;
                }
                x = *in++;
                
                if (mode == FILTER_MODE_LOW_PASS) {
                    *out++ = lp * f;
                } else if (mode == FILTER_MODE_BAND_PASS) {
                    *out++ = bp * f;
                } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    *out++ = bp * fq;
                } else if (mode == FILTER_MODE_HIGH_PASS) {
                    *out++ = x - lp * f - bp * fq;
                }
            }
            lp_ = lp;
            bp_ = bp;
            x_ = x;
        }
        
    private:
        double f_;
        double fq_;
        double x_;
        double lp_;
        double bp_;
        
        DISALLOW_COPY_AND_ASSIGN(ModifiedSvf);
    };
    
    
    
    // Two passes of modified Chamberlin SVF with the same coefficients -
    // to implement Linkwitz–Riley (Butterworth squared) crossover filters.
    class CrossoverSvf {
    public:
        CrossoverSvf() { }
        ~CrossoverSvf() { }
        
        void Init() {
            Reset();
        }
        
        void Reset() {
            lp_[0] = bp_[0] = lp_[1] = bp_[1] = 0.0;
            x_[0] = 0.0;
            x_[1] = 0.0;
        }
        
        inline void set_f_fq(double f, double fq) {
            f_ = f;
            fq_ = fq;
        }
        
        template<FilterMode mode>
        inline void Process(const double* in, double* out, size_t size) {
            double lp_1 = lp_[0];
            double bp_1 = bp_[0];
            double lp_2 = lp_[1];
            double bp_2 = bp_[1];
            double x_1 = x_[0];
            double x_2 = x_[1];
            const double fq = fq_;
            const double f = f_;
            while (size--) {
                lp_1 += f * bp_1;
                bp_1 += -fq * bp_1 -f * lp_1 + *in;
                if (mode == FILTER_MODE_BAND_PASS ||
                    mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    bp_1 += x_1;
                }
                x_1 = *in++;
                
                double y;
                if (mode == FILTER_MODE_LOW_PASS) {
                    y = lp_1 * f;
                } else if (mode == FILTER_MODE_BAND_PASS) {
                    y = bp_1 * f;
                } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    y = bp_1 * fq;
                } else if (mode == FILTER_MODE_HIGH_PASS) {
                    y = x_1 - lp_1 * f - bp_1 * fq;
                }
                
                lp_2 += f * bp_2;
                bp_2 += -fq * bp_2 -f * lp_2 + y;
                if (mode == FILTER_MODE_BAND_PASS ||
                    mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    bp_2 += x_2;
                }
                x_2 = y;
                
                if (mode == FILTER_MODE_LOW_PASS) {
                    *out++ = lp_2 * f;
                } else if (mode == FILTER_MODE_BAND_PASS) {
                    *out++ = bp_2 * f;
                } else if (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
                    *out++ = bp_2 * fq;
                } else if (mode == FILTER_MODE_HIGH_PASS) {
                    *out++ = x_2 - lp_2 * f - bp_2 * fq;
                }
            }
            lp_[0] = lp_1;
            bp_[0] = bp_1;
            lp_[1] = lp_2;
            bp_[1] = bp_2;
            x_[0] = x_1;
            x_[1] = x_2;
        }
        
    private:
        double f_;
        double fq_;
        double x_[2];
        double lp_[2];
        double bp_[2];
        
        DISALLOW_COPY_AND_ASSIGN(CrossoverSvf);
    };


#endif  // STMLIB_DSP_FILTER_H_

