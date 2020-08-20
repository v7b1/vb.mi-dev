//
// Copyright 2020 Volker Böhm.
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


// the oscillator of mutable instruments' 'Tides(2)' module
// by volker böhm, july 2020, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/



#include "c74_msp.h"


#include "tides2/poly_slope_generator.h"


const size_t kAudioBlockSize = 8;       // sig vs can't be smaller than this!
const size_t kNumOutputs = 1;

using namespace c74::max;


static t_class* this_class = nullptr;


struct t_myObj {
	t_pxobject	obj;
    
    tides::PolySlopeGenerator poly_slope_generator;
    
    tides::PolySlopeGenerator::OutputSample out[kAudioBlockSize];
    stmlib::GateFlags   no_gate[kAudioBlockSize];
    
    tides::Range        range;
    
    float       frequency, freq_lp;
    float       shape, shape_lp;
    float       slope, slope_lp;
    float       smoothness, smooth_lp;
//    float       shift, shift_lp;
    
    float       sr;
    float       r_sr;
    long        sigvs;
    
};





void* myObj_new(t_symbol *s, long argc, t_atom *argv)
{
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 4);
        outlet_new(self, "signal");
        
        self->sigvs = sys_getblksize();
        
        if(self->sigvs < kAudioBlockSize) {
            object_error((t_object*)self,
                         "sigvs can't be smaller than %d samples\n", kAudioBlockSize);
            object_free(self);
            self = NULL;
            return self;
        }

        self->sr = sys_getsr();
        self->r_sr =  1.f / self->sr;
        

        self->poly_slope_generator.Init();
        self->range = tides::RANGE_CONTROL;
        
        
        self->frequency = 100.f;
        self->shape = 0.5f;
        self->slope = 0.5f;
        self->smoothness = 0.5f;
//        self->shift = 0.3f;
        
        
        self->freq_lp = self->shape_lp = self->slope_lp = self->smooth_lp;// = self->shift_lp = 0.f;
        
        
        // process attributes
        attr_args_process(self, argc, argv);
        
    }
    else {
        object_free(self);
        self = NULL;
    }
    
	return self;
}



void myObj_float(t_myObj* self, double m) {
    
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            self->frequency = m;
            break;
        case 1:
            self->shape = m;
            break;
        case 2:
            self->slope = m;
            break;
        case 3:
            self->smoothness = m;
            break;
//        case 4:
//            self->shift = m;
//            break;
    }
    
}


#pragma mark -------- general pots -----------

void myObj_freq(t_myObj* self, double m) {
    self->frequency = m;
}

void myObj_shape(t_myObj* self, double m) {
    self->shape = m;
}

void myObj_slope(t_myObj* self, double m) {
    self->slope = m;
}

void myObj_smooth(t_myObj* self, double m) {
    self->smoothness = m;
}

void myObj_shift(t_myObj* self, double m) {
//    self->shift = m;
}




t_max_err range_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        if( m != 0 ) self->range = tides::RANGE_AUDIO;
        else self->range = tides::RANGE_CONTROL;
    }
    
    return MAX_ERR_NONE;
}


#pragma mark -------- DSP Loop ----------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double  *freq_in = ins[0];
    double  *shape_in = ins[1];
    double  *slope_in = ins[2];
    double  *smooth_in = ins[3];
//    double  *shift_in = ins[4];
    
    long vs = sampleframes;
    
    if (self->obj.z_disabled)
        return;

    
    tides::PolySlopeGenerator::OutputSample *out = self->out;
    stmlib::GateFlags *gate_flags = self->no_gate;
    tides::Range        range = self->range;
    
    float   frequency, shape, slope, smoothness; //shift,
    float   freq_knob = self->frequency;
    float   shape_knob = self->shape;
    float   slope_knob = self->slope;
//    float   shift_knob = self->shift;
    float   smoothness_knob = self->smoothness;
    
    float   freq_lp = self->freq_lp;
    float   shape_lp = self->shape_lp;
    float   slope_lp = self->slope_lp;
//    float   shift_lp = self->shift_lp;
    float   smooth_lp = self->smooth_lp;
    
    float   r_sr = self->r_sr;
    
    
    for(int count = 0; count < vs; count += kAudioBlockSize) {
        
        frequency = (freq_in[count] + freq_knob) * r_sr;
        CONSTRAIN(frequency, 0.f, 0.4f);
        // no filtering for now
//         ONE_POLE(freq_lp, frequency, 0.3f);
//         frequency = freq_lp;

        
        
        // parameter inputs
        shape = shape_knob + (float)shape_in[count];
        CONSTRAIN(shape, 0.f, 1.f);
        ONE_POLE(shape_lp, shape, 0.1f);
        slope = slope_knob + (float)slope_in[count];
        CONSTRAIN(slope, 0.f, 1.f);
        ONE_POLE(slope_lp, slope, 0.1f);
        smoothness = smoothness_knob + (float)smooth_in[count];
        CONSTRAIN(smoothness, 0.f, 1.f);
        ONE_POLE(smooth_lp, smoothness, 0.1f);
//        shift = shift_knob + shift_in[count];
//        CONSTRAIN(shift, 0.f, 1.f);
//        ONE_POLE(shift_lp, shift, 0.1f);
        
        
        self->poly_slope_generator.Render(tides::RAMP_MODE_LOOPING,
                                          tides::OUTPUT_MODE_SLOPE_PHASE,
                                          range,
                                          frequency, slope_lp, shape_lp, smooth_lp, 0.f, //shift_lp,
                                          gate_flags,
                                          NULL,
                                          out, kAudioBlockSize);


        for(int i=0; i<kAudioBlockSize; ++i) {
            for(int j=0; j<kNumOutputs; ++j) {
                outs[j][i + count] = out[i].channel[j] * 0.2f;
            }
        }
        
    }
    
    self->freq_lp = freq_lp;
    self->shape_lp = shape_lp;
//    self->shift_lp = shift_lp;
    self->slope_lp = slope_lp;
    self->smooth_lp = smooth_lp;

}



void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    
    if(maxvectorsize < kAudioBlockSize) {
        object_error((t_object*)self, "sigvs can't be smaller than %d samples, sorry!", kAudioBlockSize);
        return;
    }
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->r_sr = 1.f / self->sr;

    }
    
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    
}




void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest)
{
	if (io == ASSIST_INLET) {
		switch (index) {
			case 0:
                strncpy(string_dest,"(signal/float) FREQ", ASSIST_STRING_MAXSIZE); break;
            case 1:
                strncpy(string_dest,"(signal/float) SHAPE", ASSIST_STRING_MAXSIZE); break;
            case 2:
                strncpy(string_dest,"(signal/float) SLOPE", ASSIST_STRING_MAXSIZE); break;
            case 3:
                strncpy(string_dest,"(signal/float) SMOOTHNESS", ASSIST_STRING_MAXSIZE); break;
//            case 4:
//                strncpy(string_dest,"(signal/float) SHIFT/LEVEL", ASSIST_STRING_MAXSIZE); break;
		}
	}
	else if (io == ASSIST_OUTLET) {
		switch (index) {
            case 0:
                strncpy(string_dest,"(signal) OUT 1", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) OUT 2", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) OUT 3", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) OUT 4", ASSIST_STRING_MAXSIZE);
                break;
		}
	}
}


void ext_main(void* r) {
	this_class = class_new("vb.mi.tds.osc~", (method)myObj_new, (method)dsp_free, sizeof(t_myObj), 0, A_GIMME, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);

//    class_addmethod(this_class, (method)myObj_int,      "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,    "float",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_freq,     "freq",     A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_shape,    "shape",    A_FLOAT, 0);
//    class_addmethod(this_class, (method)myObj_shift,    "shift",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_slope,    "slope",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_smooth,   "smooth",   A_FLOAT, 0);
    
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
    
    // ATTRIBUTES ..............
    // output mode
//    CLASS_ATTR_CHAR(this_class, "output_mode", 0, t_myObj, output_mode);
//    CLASS_ATTR_ENUMINDEX(this_class, "output_mode", 0, "GATE AMPLITUDE PHASE FREQUENCY");
//    CLASS_ATTR_LABEL(this_class, "output_mode", 0, "output mode");
//    CLASS_ATTR_FILTER_CLIP(this_class, "output_mode", 0, 3);
//    CLASS_ATTR_ACCESSORS(this_class, "output_mode", NULL, (method)output_mode_setter);
//    CLASS_ATTR_SAVE(this_class, "output_mode", 0);
//
//    // ramp mode
//    CLASS_ATTR_CHAR(this_class, "ramp_mode", 0, t_myObj, ramp_mode);
//    CLASS_ATTR_ENUMINDEX(this_class, "ramp_mode", 0, "AD LOOPING AR");
//    CLASS_ATTR_LABEL(this_class, "ramp_mode", 0, "ramp mode");
//    CLASS_ATTR_FILTER_CLIP(this_class, "ramp_mode", 0, 2);
//    CLASS_ATTR_ACCESSORS(this_class, "ramp_mode", NULL, (method)ramp_mode_setter);
//    CLASS_ATTR_SAVE(this_class, "ramp_mode", 0);
    
    // range
    CLASS_ATTR_CHAR(this_class, "range", 0, t_myObj, range);
    CLASS_ATTR_ENUMINDEX(this_class, "range", 0, "CONTROL AUDIO");
    CLASS_ATTR_LABEL(this_class, "range", 0, "range selector");
    CLASS_ATTR_FILTER_CLIP(this_class, "range", 0, 1);
    CLASS_ATTR_ACCESSORS(this_class, "range", NULL, (method)range_setter);
    CLASS_ATTR_SAVE(this_class, "range", 0);
    
    
    object_post(NULL, "vb.mi.tds.osc~ by Volker Böhm -- https://vboehm.net");
    object_post(NULL, "an oscillator based on mutable instruments' 'tides' module");
}
