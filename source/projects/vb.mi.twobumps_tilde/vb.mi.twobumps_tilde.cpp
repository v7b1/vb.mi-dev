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
// running the alternative firmware "parasite" by Matthias Puech
// https://github.com/mqtthiqs/parasites
// in TWO BUMPS mode


// Original code by Émilie Gillet, https://mutable-instruments.net/




#include "c74_msp.h"


#include "tides/generator.h"



const size_t kAudioBlockSize = 16;       // sig vs can't be smaller than this!
const double kSampleRate = 48000.0;     // SR of the original module

using namespace c74::max;


static t_class* this_class = nullptr;


struct t_myObj {
	t_pxobject	obj;
    
    tides::Generator        generator;

    tides::GeneratorMode    output_mode;
    tides::GeneratorRange   quality;
    uint8_t     previous_state_;
    
    double      pitch, slope, shape, smooth;
    bool        trig_connected;
    bool        clock_connected;
    bool        use_trigger;
    bool        use_clock;
    uint8_t     quantize;
    
    double      sr;
    uint16_t    sr_pitch_correction;
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
        self->sr_pitch_correction = log2(kSampleRate / self->sr) * 12.0 * 128.0;
        memset(&self->generator, 0, sizeof(self->generator));
        self->generator.Init();
        self->generator.set_range(tides::GENERATOR_RANGE_HIGH);
        self->generator.set_mode(tides::GENERATOR_MODE_LOOPING);
        self->generator.set_sync(false);
        
        // 'two bumps' is feature_mode HARMONIC
        self->generator.feature_mode_ = tides::Generator::FEAT_MODE_HARMONIC;

        
        self->previous_state_ = 0;
        self->use_trigger = false;
        self->use_clock = false;
        self->quantize = 0;
        
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




#pragma mark --------- attr setters ---------

t_max_err output_mode_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->output_mode = tides::GeneratorMode(m);
        self->generator.set_mode(self->output_mode);
    }
    
    return MAX_ERR_NONE;
}



t_max_err quality_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->quality = tides::GeneratorRange(m);
        self->generator.set_range(self->quality);
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
    double  pitch_ = self->pitch;
    double  shape_ = self->shape;
    double  slope_ = self->slope;
    double  smooth_ = self->smooth;
    uint8_t quant = self->quantize;
    uint16_t pitch_correction = self->sr_pitch_correction;
    long    vs = sampleframes;
    
    
    for(int count = 0; count < vs; count += kAudioBlockSize) {
        
        double pitchf = pitch_ + freq_in[count];
        CONSTRAIN(pitchf, -128.0, 128.0);
        
        int16_t pitch;
        
        if(quant) {
            int16_t semi = (int16_t)(pitchf + 0.5);
            uint16_t octaves = semi / 12;
            semi -= octaves * 12;
            pitch = octaves * kOctave + quantize_lut[quant - 1][semi] + pitch_correction;
        }
        else {
            pitch = (pitchf * 128.0) + pitch_correction;
        }
        
//        generator->set_pitch(pitch, 0);
        generator->set_pitch_high_range(pitch, 0);
        
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
            outs[1][index] = (double)sample.unipolar / 32768.0 - 1.0;   // vb, make this also bipolar
            outs[2][index] = sample.flags & tides::FLAG_END_OF_ATTACK;
            outs[3][index] = ( sample.flags & tides::FLAG_END_OF_RELEASE ) >> 1;
        }
        
        
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
        self->sr_pitch_correction = log2(kSampleRate / self->sr) * 12.0 * 128.0;
    }
    
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    
}




void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest)
{
	if (io == ASSIST_INLET) {
		switch (index) {
			case 0:
                strncpy(string_dest,"(signal/float) pitch", ASSIST_STRING_MAXSIZE); break;
            case 1:
                strncpy(string_dest,"(signal/float) BMP1 centre freqs", ASSIST_STRING_MAXSIZE); break;
            case 2:
                strncpy(string_dest,"(signal/float) BMP2 centre freqs", ASSIST_STRING_MAXSIZE); break;
            case 3:
                strncpy(string_dest,"(signal/float) bumps <> potholes", ASSIST_STRING_MAXSIZE); break;
            case 4:
                strncpy(string_dest,"(signal) randomize freqs on second outlet", ASSIST_STRING_MAXSIZE); break;
            case 5:
                strncpy(string_dest,"(signal) randomize phases", ASSIST_STRING_MAXSIZE); break;
            case 6:
                strncpy(string_dest,"(signal) randomize harm dist and decim settings", ASSIST_STRING_MAXSIZE); break;
		}
	}
	else if (io == ASSIST_OUTLET) {
		switch (index) {
            case 0:
                strncpy(string_dest,"(signal) MAIN OUT", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) permuted harmonic OUT", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) lofi 1-bit OUT", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) square sub osc", ASSIST_STRING_MAXSIZE);
                break;
		}
	}
}


void ext_main(void* r) {
	this_class = class_new("vb.mi.twobumps~", (method)myObj_new, (method)dsp_free, sizeof(t_myObj), 0, A_GIMME, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);

    class_addmethod(this_class, (method)myObj_int,      "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,    "float",    A_FLOAT, 0);

    
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
    
    // ATTRIBUTES ..............    
    // quantization on/off
    CLASS_ATTR_CHAR(this_class, "quant", 0, t_myObj, quantize);
    CLASS_ATTR_ENUMINDEX(this_class, "quant", 0, "OFF chromatic major minor whole_tones penta_minor poor_penta fifths");
    CLASS_ATTR_LABEL(this_class, "quant", 0, "quantization");
    CLASS_ATTR_FILTER_CLIP(this_class, "quant", 0, 7);
    CLASS_ATTR_SAVE(this_class, "quant", 0);
    

    // output mode
    CLASS_ATTR_CHAR(this_class, "output_mode", 0, t_myObj, output_mode);
    CLASS_ATTR_ENUMINDEX(this_class, "output_mode", 0, "ODD_HARM ALL_HARM OCTAVES");
    CLASS_ATTR_LABEL(this_class, "output_mode", 0, "output mode");
    CLASS_ATTR_FILTER_CLIP(this_class, "output_mode", 0, 2);
    CLASS_ATTR_ACCESSORS(this_class, "output_mode", NULL, (method)output_mode_setter);
    CLASS_ATTR_SAVE(this_class, "output_mode", 0);

    // range
    CLASS_ATTR_CHAR(this_class, "quality", 0, t_myObj, quality);
    CLASS_ATTR_ENUMINDEX(this_class, "quality", 0, "HIGH MEDIUM LOW");
    CLASS_ATTR_LABEL(this_class, "quality", 0, "quality selector");
    CLASS_ATTR_FILTER_CLIP(this_class, "quality", 0, 2);
    CLASS_ATTR_ACCESSORS(this_class, "quality", NULL, (method)quality_setter);
    CLASS_ATTR_SAVE(this_class, "quality", 0);

    
    
    object_post(NULL, "vb.mi.twobumps~ by Volker Böhm -- https://vboehm.net");
    object_post(NULL, "based on mutable instruments' 'tides(parasite)' module");
}
