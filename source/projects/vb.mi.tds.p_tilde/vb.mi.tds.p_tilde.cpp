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


// a clone of mutable instruments' 'Tides(1)' module for maxmsp
// by volker böhm, august 2020, https://vboehm.net
// running the alternative firmware "parasite" by Matthias Puech https://github.com/mqtthiqs/parasites


// Original code by Émilie Gillet, https://mutable-instruments.net/




#include "c74_msp.h"


#include "tides/generator.h"
#include "tides/plotter.h"



const size_t kAudioBlockSize = 16;       // sig vs can't be smaller than this!
const double kSampleRate = 48000.0;     // SR of the original module

using namespace c74::max;


static t_class* this_class = nullptr;


struct t_myObj {
	t_pxobject	obj;
    
    tides::Generator    generator;
//    tides::Plotter      plotter;

    tides::Generator::FeatureMode feat_mode;
    tides::GeneratorMode     ramp_mode;
    tides::GeneratorRange        range;
    uint8_t     previous_state_;
    
    double      pitch, slope, shape, smooth;
    bool        trig_connected;
    bool        clock_connected;
    bool        use_trigger;
    bool        use_clock;
    bool        sheep;
    uint8_t     quantize;
    
    double      sr;
    double      sr_pitch_correction;
    long        sigvs;
    
};


// quantization stuff

const int16_t kOctave = 12 * 128;

#define SE * 128

const uint16_t quantize_lut[7][12] = {
    /* semitones */
    {0, 1 SE, 2 SE, 3 SE, 4 SE, 5 SE, 6 SE, 7 SE, 8 SE, 9 SE, 10 SE, 11 SE},
    /* ionian */
    {0, 0, 2 SE, 2 SE, 4 SE, 5 SE, 5 SE, 7 SE, 7 SE, 9 SE, 9 SE, 11 SE},
    /* aeolian */
    {0, 0, 2 SE, 3 SE, 3 SE, 5 SE, 5 SE, 7 SE, 8 SE, 8 SE, 10 SE, 10 SE},
    /* whole tones */
    {0, 0, 2 SE, 2 SE, 4 SE, 4 SE, 6 SE, 6 SE, 8 SE, 8 SE, 10 SE, 10 SE},
    /* pentatonic minor */
    {0, 0, 3 SE, 3 SE, 3 SE, 5 SE, 5 SE, 7 SE, 7 SE, 10 SE, 10 SE, 10 SE},
    /* pent-3 */
    {0, 0, 0, 0, 7 SE, 7 SE, 7 SE, 7 SE, 10 SE, 10 SE, 10 SE, 10 SE},
    /* fifths */
    {0, 0, 0, 0, 0, 0, 7 SE, 7 SE, 7 SE, 7 SE, 7 SE, 7 SE},
};



void* myObj_new(t_symbol *s, long argc, t_atom *argv)
{
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 7);
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
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
        self->sr_pitch_correction = log2(kSampleRate / self->sr) * 12.0;
        object_post(NULL, "size of generator: %d -- %d", sizeof(self->generator), sizeof(tides::Generator));
        memset(&self->generator, 0, sizeof(self->generator));
        self->generator.Init();
        self->generator.set_range(tides::GENERATOR_RANGE_HIGH);
        self->generator.set_mode(tides::GENERATOR_MODE_LOOPING);
        self->generator.feature_mode_ = tides::Generator::FEAT_MODE_FUNCTION;
        self->generator.set_sync(false);

        
        self->previous_state_ = 0;
        self->use_trigger = false;
        self->use_clock = false;
        self->quantize = 0;
        self->sheep = false;
        
        self->pitch = 60.0;
        self->shape = 0.0;
        self->slope = 0.0;
        self->smooth = 0.0;
        
        
        // process attributes
        attr_args_process(self, argc, argv);
        
    }
    else {
        object_free(self);
        self = NULL;
    }
    
	return self;
}



void myObj_int(t_myObj* self, long m) {
    
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 5:
            self->use_trigger = (m != 0);
            break;
        case 6:
            bool a = m != 0;
            self->use_clock = a;
            self->generator.set_sync(a);
            break;
    }
}


void myObj_float(t_myObj* self, double m) {
    
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            CONSTRAIN(m, -128.0, 128.0);
            self->pitch = m;
            break;
        case 1:
            self->shape = m;
            break;
        case 2:
            self->slope = m;
            break;
        case 3:
            self->smooth = m;
            break;

    }
    
}


#pragma mark -------- general pots -----------

void myObj_freq(t_myObj* self, double m) {
    CONSTRAIN(m, -128.0, 128.0);
    self->pitch = m;
}

void myObj_shape(t_myObj* self, double m) {
    self->shape = m;
}

void myObj_slope(t_myObj* self, double m) {
    self->slope = m;
}

void myObj_smooth(t_myObj* self, double m) {
    self->smooth = m;
}

void myObj_pw(t_myObj* self, double m) {
    CONSTRAIN(m, 0.0, 1.0);
    uint16_t val = m * 32767.0;
    self->generator.set_pulse_width(val);
}


void myObj_feat_mode(t_myObj *self, long m) {
    
}

t_max_err sheep_mode_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
//        t_atom_long m = atom_getlong(av);
//        self->sheep = ( m != 0 );
//        self->generator.set_sheep( self->sheep );
//        if (!self->sheep) // switch to last selected ramp_mode, when sheep goes off
//            self->generator.set_mode(self->ramp_mode);
        
    }
    
    return MAX_ERR_NONE;
}



t_max_err ramp_mode_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->ramp_mode = tides::GeneratorMode(m);
        self->generator.set_mode(self->ramp_mode);
    }
    
    return MAX_ERR_NONE;
}

t_max_err feat_mode_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->feat_mode = tides::Generator::FeatureMode(m);
        self->generator.feature_mode_ = self->feat_mode;
    }
    
    return MAX_ERR_NONE;
}


t_max_err range_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->range = tides::GeneratorRange(m);
        self->generator.set_range(self->range);
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
    double  *freeze_in = ins[4];
    double  *trig_in = ins[5];      // trigger signal input
    double  *clock_in = ins[6];     // clock signal input
    
    if (self->obj.z_disabled)
        return;

    
    tides::Generator *generator = &self->generator;
    uint8_t prev_state = self->previous_state_;
    double  pitch_ = self->pitch + self->sr_pitch_correction;
    double  shape_ = self->shape;
    double  slope_ = self->slope;
    double  smooth_ = self->smooth;
    uint8_t quant = self->quantize;
    long    vs = sampleframes;
    
    
    for(int count = 0; count < vs; count += kAudioBlockSize) {
        
        double pitchf = pitch_;
        pitchf += freq_in[count];
        CONSTRAIN(pitchf, -128.0, 128.0);
        int16_t pitch = (pitchf - 12.0) * 128.0;   // need to go an octave lower, why?
        if(quant) {
            uint16_t semi = pitch >> 7;
            uint16_t octaves = semi / 12 ;
            semi -= octaves * 12;
            pitch = octaves * kOctave + quantize_lut[quant - 1][semi];
        }
        // set pitch below...
        
        double shape = shape_;
        shape += shape_in[count];
//        CONSTRAIN(shape, -1.0, 1.0);      // save some cycles and use overflow
        shape *= 32767.0;
        self->generator.set_shape((int16_t)shape);
        
        double slope = slope_;
        slope += slope_in[count];
//        CONSTRAIN(slope, -1.0, 1.0);
        slope *= 32767.0;
        self->generator.set_slope((int16_t)slope);
        
        double smooth = smooth_;
        smooth += smooth_in[count];
//        CONSTRAIN(smooth, -1.0, 1.0);
        smooth *= 32767.0;
        self->generator.set_smoothness((int16_t)smooth);
        

        for(int i=0; i<kAudioBlockSize; ++i) {
            
            int index = i + count;
            
            uint8_t state = 0;

            if (freeze_in[index] >= 0.1)
                state |= tides::CONTROL_FREEZE;
            if (trig_in[index] >= 0.1)
                state |= tides::CONTROL_GATE;
            if (clock_in[index] >= 0.1)
                state |= tides::CONTROL_CLOCK;
            if (!(prev_state & tides::CONTROL_CLOCK) && (state & tides::CONTROL_CLOCK))
                state |= tides::CONTROL_CLOCK_RISING;
            if (!(prev_state & tides::CONTROL_GATE) && (state & tides::CONTROL_GATE))
                state |= tides::CONTROL_GATE_RISING;
            if ((prev_state & tides::CONTROL_GATE) && !(state & tides::CONTROL_GATE))
                state |= tides::CONTROL_GATE_FALLING;
            
            prev_state = state;
            
            const tides::GeneratorSample& sample = generator->Process(state);

            
            outs[0][index] = (double)sample.bipolar / 32768.0;
            outs[1][index] = (double)sample.unipolar / 65536.0;
            outs[2][index] = sample.flags & tides::FLAG_END_OF_ATTACK;
            outs[3][index] = ( sample.flags & tides::FLAG_END_OF_RELEASE ) >> 1;
        }
        
        if (generator->feature_mode_ == tides::Generator::FEAT_MODE_HARMONIC)
            generator->set_pitch_high_range(pitch, 0);
        else
            generator->set_pitch(pitch, 0);
        
        generator->FillBuffer();
    }
    
    self->previous_state_ = prev_state;
    
}



void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    // is a signal connected to the trigger/clock input?
    self->trig_connected = count[5];
    self->clock_connected = count[6];
    
    
    if(maxvectorsize < kAudioBlockSize) {
        object_error((t_object*)self, "sigvs can't be smaller than %d samples, sorry!", kAudioBlockSize);
        return;
    }
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->sr_pitch_correction = log2(kSampleRate / self->sr) * 12.0;
//        object_post(NULL, "sr_scale: %f", self->sr_pitch_correction);
    }
    
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    
}




void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest)
{
	if (io == ASSIST_INLET) {
		switch (index) {
			case 0:
                strncpy(string_dest,"(signal) PITCH", ASSIST_STRING_MAXSIZE); break;
            case 1:
                strncpy(string_dest,"(signal) SHAPE", ASSIST_STRING_MAXSIZE); break;
            case 2:
                strncpy(string_dest,"(signal) SLOPE", ASSIST_STRING_MAXSIZE); break;
            case 3:
                strncpy(string_dest,"(signal) SMOOTHNESS", ASSIST_STRING_MAXSIZE); break;
            case 4:
                strncpy(string_dest,"(signal) FREEZE IN", ASSIST_STRING_MAXSIZE); break;
            case 5:
                strncpy(string_dest,"(signal) TRIG IN", ASSIST_STRING_MAXSIZE); break;
            case 6:
                strncpy(string_dest,"(signal) CLOCK IN", ASSIST_STRING_MAXSIZE); break;
		}
	}
	else if (io == ASSIST_OUTLET) {
		switch (index) {
            case 0:
                strncpy(string_dest,"(signal) bipolar out", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) unipolar out", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) high tide", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) low tide", ASSIST_STRING_MAXSIZE);
                break;
		}
	}
}


void ext_main(void* r) {
	this_class = class_new("vb.mi.tds.p~", (method)myObj_new, (method)dsp_free, sizeof(t_myObj), 0, A_GIMME, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);

    class_addmethod(this_class, (method)myObj_int,      "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,    "float",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_freq,     "freq",     A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_shape,    "shape",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_slope,    "slope",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_smooth,   "smooth",   A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_pw,   "pw",   A_FLOAT, 0);

    
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
    
    // ATTRIBUTES ..............
    // sheep mode
//    CLASS_ATTR_CHAR(this_class, "sheep", 0, t_myObj, sheep);
//    CLASS_ATTR_ENUMINDEX(this_class, "sheep", 0, "OFF ON");
//    CLASS_ATTR_STYLE_LABEL(this_class, "sheep", 0, "onoff", "sheep mode");
//    CLASS_ATTR_FILTER_CLIP(this_class, "sheep", 0, 1);
//    CLASS_ATTR_ACCESSORS(this_class, "sheep", NULL, (method)sheep_mode_setter);
//    CLASS_ATTR_SAVE(this_class, "sheep", 0);
    
    // quantization on/off
    CLASS_ATTR_CHAR(this_class, "quant", 0, t_myObj, quantize);
    CLASS_ATTR_ENUMINDEX(this_class, "quant", 0, "OFF chromatic major minor whole_tones penta_minor poor_penta fifths");
    CLASS_ATTR_LABEL(this_class, "quant", 0, "quantization");
    CLASS_ATTR_FILTER_CLIP(this_class, "quant", 0, 7);
//    CLASS_ATTR_ACCESSORS(this_class, "quant", NULL, (method)quant_mode_setter);
    CLASS_ATTR_SAVE(this_class, "quant", 0);
    
    // feature mode
    CLASS_ATTR_CHAR(this_class, "feat_mode", 0, t_myObj, feat_mode);
    CLASS_ATTR_ENUMINDEX(this_class, "feat_mode", 0, "FEATURE HARMONIC RANDOM");
    CLASS_ATTR_LABEL(this_class, "feat_mode", 0, "feature mode");
    CLASS_ATTR_FILTER_CLIP(this_class, "feat_mode", 0, 2);
    CLASS_ATTR_ACCESSORS(this_class, "feat_mode", NULL, (method)feat_mode_setter);
    CLASS_ATTR_SAVE(this_class, "feat_mode", 0);

    // ramp mode
    CLASS_ATTR_CHAR(this_class, "ramp_mode", 0, t_myObj, ramp_mode);
    CLASS_ATTR_ENUMINDEX(this_class, "ramp_mode", 0, "AD LOOPING AR");
    CLASS_ATTR_LABEL(this_class, "ramp_mode", 0, "ramp mode");
    CLASS_ATTR_FILTER_CLIP(this_class, "ramp_mode", 0, 2);
    CLASS_ATTR_ACCESSORS(this_class, "ramp_mode", NULL, (method)ramp_mode_setter);
    CLASS_ATTR_SAVE(this_class, "ramp_mode", 0);

    // range
    CLASS_ATTR_CHAR(this_class, "range", 0, t_myObj, range);
    CLASS_ATTR_ENUMINDEX(this_class, "range", 0, "HIGH MEDIUM LOW");
    CLASS_ATTR_LABEL(this_class, "range", 0, "range selector");
    CLASS_ATTR_FILTER_CLIP(this_class, "range", 0, 2);
    CLASS_ATTR_ACCESSORS(this_class, "range", NULL, (method)range_setter);
    CLASS_ATTR_SAVE(this_class, "range", 0);

    
    
    object_post(NULL, "vb.mi.tds.p~ by volker boehm -- https://vboehm.net");
    object_post(NULL, "based on mutable instruments' 'tides(parasite)' module");
}
