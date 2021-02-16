//
// Copyright 2019 Volker Böhm.
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


// a clone of mutable instruments' 'warps' module for maxmsp
// by volker böhm, okt 2019, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/



#include "c74_max.h"
#include "c74_msp.h"

#include "warps/dsp/modulator.h"
#include "warps/dsp/oscillator.h"
#include "read_inputs.hpp"



using namespace c74::max;


// TODO: work on block size and SR, use libsamplerate for downsampling?
// original SR: 96 kHz, block size: 60

const size_t kBlockSize = 96;       // has to stay like that TODO: why?

static t_class* this_class = nullptr;

struct t_myObj {
    t_pxobject	obj;
    
    warps::Modulator    *modulator;
    warps::ReadInputs   *read_inputs;

    double              adc_inputs[warps::ADC_LAST];
    short               patched[2];
    short               easterEgg;
    uint8_t             carrier_shape;
    double              pre_gain;
    
    warps::FloatFrame   *input;
    warps::FloatFrame   *output;
    
    long                count;
    double              sr;
    int                 sigvs;
};



void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 6);    // six audio inputs
        
        outlet_new(self, "signal");         // 'out' output
        outlet_new(self, "signal");         // 'aux' output
        
        self->sr = sys_getsr();
        if(self->sr <= 0)
            self->sr = 44100.0;

        
        // init some params
        self->count = 0;
        self->easterEgg = 0;
        self->patched[0] = self->patched[1] = 0;
        self->carrier_shape = 1;
        
        self->modulator = new warps::Modulator;
        memset(self->modulator, 0, sizeof(*t_myObj::modulator));
        self->modulator->Init(self->sr);
        
        self->modulator->mutable_parameters()->note = 110.0f; // (Hz)
        
        self->read_inputs = new warps::ReadInputs;
        self->read_inputs->Init();

        
        for(int i=0; i<warps::ADC_LAST; i++)
            self->adc_inputs[i] = 0.0;


        self->input = (warps::FloatFrame*)sysmem_newptr(kBlockSize*sizeof(warps::FloatFrame));
        self->output = (warps::FloatFrame*)sysmem_newptrclear(kBlockSize*sizeof(warps::FloatFrame));
    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}


void myObj_info(t_myObj *self)
{
    warps::Parameters *p = self->modulator->mutable_parameters();

    object_post((t_object*)self, "Modulator Parameters ----------->");
    object_post((t_object*)self, "drive[0]: %f", p->channel_drive[0]);
    object_post((t_object*)self, "drive[1]: %f", p->channel_drive[1]);
    object_post((t_object*)self, "mod_algo: %f", p->modulation_algorithm);
    object_post((t_object*)self, "mod param: %f", p->modulation_parameter);
    object_post((t_object*)self, "car_shape: %d", p->carrier_shape);
    
    object_post((t_object*)self, "freqShift_pot: %f", p->frequency_shift_pot);
    object_post((t_object*)self, "freqShift_cv: %f", p->frequency_shift_cv);
    
    object_post((t_object*)self, "phaseShift: %f", p->phase_shift);
    object_post((t_object*)self, "note: %f", p->note);

}


// plug / unplug patch chords...

void myObj_int(t_myObj *self, long value)
{
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 2: // ADC_LEVEL_1_POT
            self->patched[0] = value != 0;
            break;
        case 3:
            // ADC_LEVEL_2_POT,
            self->patched[1] = value != 0;
            break;
        default:
            break;
    }
}


void myObj_float(t_myObj *self, double value)
{    
    long innum = proxy_getinlet((t_object *)self);
    value = clamp(value, 0., 1.);
    
    switch (innum) {
        case 2:
            self->adc_inputs[warps::ADC_LEVEL_1_POT] = value;
            break;
        case 3:
            self->adc_inputs[warps::ADC_LEVEL_2_POT] = value;
            break;
        case 4:
            self->adc_inputs[warps::ADC_ALGORITHM_POT] = value;
            break;
        case 5:
            self->adc_inputs[warps::ADC_PARAMETER_POT] = value;
            break;
            
    }
}


#pragma mark ----- main pots -----

void myObj_modulation_algo(t_myObj* self, double m) {
    // Selects which signal processing operation is performed on the carrier and modulator.
    self->adc_inputs[warps::ADC_ALGORITHM_POT] = clamp(m, 0., 1.);
}

void myObj_modulation_timbre(t_myObj* self, double m) {
    // Controls the intensity of the high harmonics created by cross-modulation
    // (or provides another dimension of tone control for some algorithms).
    self->adc_inputs[warps::ADC_PARAMETER_POT] = clamp(m, 0., 1.);
}


// oscillator button

void myObj_int_osc_shape(t_myObj* self, long t) {
    // Enables the internal oscillator and selects its waveform.
    self->carrier_shape = clamp((int)t, 0, 3);
    
    if (!self->easterEgg)
        self->modulator->mutable_parameters()->carrier_shape = self->carrier_shape;

}


#pragma mark --------- small pots ----------

void myObj_level1(t_myObj* self, double m) {
    // External carrier amplitude or internal oscillator frequency.
    // When the internal oscillator is switched off, this knob controls the amplitude of the carrier, or the amount of amplitude modulation from the channel 1 LEVEL CV input (1). When the internal oscillator is active, this knob controls its frequency.
    self->adc_inputs[warps::ADC_LEVEL_1_POT] = clamp(m, 0., 1.);
}

void myObj_level2(t_myObj* self, double m) {
    // This knob controls the amplitude of the modulator, or the amount of amplitude modulation from the channel 2 LEVEL CV input (2). Past a certain amount of gain, the signal soft clips.
    self->adc_inputs[warps::ADC_LEVEL_2_POT] = clamp(m, 0., 1.);
}


#pragma mark -------- other messages ----------

void myObj_freq(t_myObj* self, double n) {
    // set freq (in Hz) of internal oscillator
    self->modulator->mutable_parameters()->note = clamp(n, 0., 15000.);
}


void myObj_bypass(t_myObj* self,long t) {
    self->modulator->set_bypass(t != 0);
}

void myObj_easter_egg(t_myObj* self, long t) {
    
    if(t != 0) {
        self->easterEgg = true;
        self->modulator->mutable_parameters()->carrier_shape = 1;
    }
    else {
        self->easterEgg = false;
        self->modulator->mutable_parameters()->carrier_shape = self->carrier_shape;
    }
    self->modulator->set_easter_egg(self->easterEgg);

}


t_max_err gain_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_float f = atom_getlong(av);
        self->pre_gain = f;
        self->modulator->vocoder_.limiter_pre_gain_ = f * 1.4;
    }
}


#pragma mark ----- dsp loop ------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    warps::FloatFrame  *input = self->input;
    warps::FloatFrame  *output = self->output;

    long    vs = sampleframes;
    long    count = self->count;
    double  *adc_inputs = self->adc_inputs;
    
    if (self->obj.z_disabled)
        return;
    
    
    // read 'cv' input signals, store first value of a sig vector
    for(int k=0; k<4; k++) {
        // cv inputs are expected in -1. to 1. range
        //adc_inputs[k] = clamp((1.-ins[k+2][0]), 0., 1.);  // leave out first two inlets (which are the audio inputs)
        adc_inputs[k] = ins[k+2][0];
    }
    self->read_inputs->Read(self->modulator->mutable_parameters(), adc_inputs, self->patched);
    
    
    for (int i=0; i<vs; ++i) {

        input[count].l = ins[0][i];
        input[count].r = ins[1][i];
        
        count++;
        if(count >= kBlockSize) {
            self->modulator->Processf(input, output, kBlockSize);
            count = 0;
        }
        
        outs[0][i] = output[count].l;
        outs[1][i] = output[count].r;
    }
    
    self->count = count;
    
}



void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->modulator->Init(self->sr);
    }
    
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);
    delete self->modulator;

    sysmem_freeptr(self->input);
    sysmem_freeptr(self->output);
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) IN carrier", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) IN modulator", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal/float) LEVEL1 controls carrier amplitude or int osc freq, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal/float) LEVEL2 controls modulator amplitude, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal/float) ALGO - choose x-fade algorithm", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal/float) TIMBRE", ASSIST_STRING_MAXSIZE);
                break;
        }
    }
    else if (io == ASSIST_OUTLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) OUT", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) AUX", ASSIST_STRING_MAXSIZE);
                break;
        }
    }
}


void ext_main(void* r)
{
    this_class = class_new("vb.mi.wrps~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_assist,	            "assist",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_dsp64,	            "dsp64",	A_CANT,		0);
    
    // main pots
    class_addmethod(this_class, (method)myObj_modulation_algo,      "algo",     A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_modulation_timbre,	"timbre",	A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_int_osc_shape,        "osc_shape",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_level1,               "level1",   A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_level2,               "level2",   A_FLOAT, 0);

    class_addmethod(this_class, (method)myObj_freq,                 "freq",     A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_bypass,               "bypass",   A_LONG, 0);
    class_addmethod(this_class, (method)myObj_easter_egg,           "easteregg",A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_int,                  "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,                "float",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_info,	                "info", 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    
    CLASS_ATTR_DOUBLE(this_class, "pre_gain", 0, t_myObj, pre_gain);
    CLASS_ATTR_LABEL(this_class, "pre_gain", 0, "vocoder pre_gain");
    CLASS_ATTR_FILTER_CLIP(this_class, "pre_gain", 1.0, 10.0);
    CLASS_ATTR_ACCESSORS(this_class, "pre_gain", NULL, (method)gain_setter);
    CLASS_ATTR_SAVE(this_class, "pre_gain", 0);
    
    object_post(NULL, "vb.mi.wrps~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'warps' module");
}
