// Copyright 2021 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// Various conversion routines for DX7 patch data.

#include "plaits/dsp/fm/dx_units.h"

namespace plaits {

namespace fm {

/* extern */
const double lut_coarse[32] = {
 -12.000000,
   0.000000,
  12.000000,
  19.019550,
  24.000000,
  27.863137,
  31.019550,
  33.688259,
  36.000000,
  38.039100,
  39.863137,
  41.513180,
  43.019550,
  44.405276,
  45.688259,
  46.882687,
  48.000000,
  49.049554,
  50.039100,
  50.975130,
  51.863137,
  52.707809,
  53.513180,
  54.282743,
  55.019550,
  55.726274,
  56.405276,
  57.058650,
  57.688259,
  58.295772,
  58.882687,
  59.450356
};

/* extern */
const double lut_amp_mod_sensitivity[4] = {
  0.0,
  0.2588,
  0.4274,
  1.0
};

/* extern */
const double lut_pitch_mod_sensitivity[8] = {
  0.0,
  0.0781250,
  0.1562500,
  0.2578125,
  0.4296875,
  0.7187500,
  1.1953125,
  2.0
};

/* extern */
const double lut_cube_root[17] = {
  0.0,
  0.39685062976,
  0.50000000000,
  0.57235744065,
  0.62996081605,
  0.67860466725,
  0.72112502092,
  0.75914745216,
  0.79370070937,
  0.82548197054,
  0.85498810729,
  0.88258719406,
  0.90856038354,
  0.93312785379,
  0.95646563396,
  0.97871693135,
  1.0
};


}  // namespace fm

}  // namespace plaits
