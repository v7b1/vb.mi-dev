// Copyright 2016 Olivier Gillet.
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
// Utility DSP routines.

#ifndef PLAITS_DSP_DSP_H_
#define PLAITS_DSP_DSP_H_

#include "stmlib/stmlib.h"

extern double kSampleRate;
extern double a0;

namespace plaits {
    
    //const double kSampleRate = 48000.0;      //48000.0;
    //static double kSampleRate = 96000.0;

// There is no proper PLL for I2S, only a divider on the system clock to derive
// the bit clock.
// The division ratio is set to 47 (23 EVEN, 1 ODD) by the ST libraries.
//
// Bit clock = 72000000 / 47 = 1531.91 kHz
// Frame clock = Bit clock / 32 = 47872.34 Hz
//
// That's only 4.6 cts of error, but we care!

    
//static const double kCorrectedSampleRate = 47872.34;
//const double a0 = (440.0 / 8.0) / kCorrectedSampleRate;

    const size_t kMaxBlockSize = 1024;   // was 24;
    const size_t kBlockSize = 16;       // 12
    
    /*
    class Dsp {
    private:
        static double kSampleRate;
        //const double a0 = (440.0 / 8.0) / kSampleRate;
        static double a0;
        static size_t kBlockSize;
        
    public:
        static double getSr() {return kSampleRate;}
        static double getA0() {return a0;}
        static void setSr(double newsr) {
            kSampleRate = newsr;
            a0 = (440.0 / 8.0) / kSampleRate;
        }
        static size_t getBlockSize() {return kBlockSize;}
        static void setBlockSize(int newBlockSize) {
            kBlockSize = static_cast<size_t>(newBlockSize);
        }
    };*/
    

}  // namespace plaits

#endif  // PLAITS_DSP_DSP_H_
