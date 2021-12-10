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


// a clone of mutable instruments' 'rings' module for maxmsp
// by volker böhm, jan 2019, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/


#include "c74_msp.h"

#include "read_inputs.h"

#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/dsp.h"

#ifdef __APPLE__
#include "Accelerate/Accelerate.h"
#endif

using std::clamp;

using namespace c74::max;


static t_class* this_class = nullptr;


double rings::Dsp::sr = 48000.0;
double rings::Dsp::a3 = 440.0 / 48000.0;

const int kBlockSize = rings::kMaxBlockSize;


struct t_myObj {
    t_pxobject	obj;
    
    rings::Part             part;
    rings::StringSynthPart  string_synth;
    rings::Strummer         strummer;
    rings::ReadInputs       read_inputs;

    double                  in_level;
    
    uint16_t                *reverb_buffer;
    
    rings::PerformanceState performance_state;
    rings::Patch            patch;
    
    double                  cvinputs[rings::ADC_CHANNEL_LAST+1];
    double                  sr;
    int                     sigvs;
    short                   strum_connected;
    short                   fm_patched;
    bool                    easter_egg;
};



void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 8);        // 8 signal inlets
        
        outlet_new(self, "signal"); // 'out' output
        outlet_new(self, "signal"); // 'aux' output
        
        self->sigvs = sys_getblksize();
        
        if(self->sigvs < kBlockSize) {
            object_error((t_object*)self,
                         "sigvs can't be smaller than %d samples\n", kBlockSize);
            object_free(self);
            self = NULL;
            return self;
        }
        
        
        self->sr = sys_getsr();
        if(self->sr <= 0.0)
            self->sr = 48000.0;
        
        
        // set actual Sampling Rate
        rings::Dsp::setSr(self->sr);
        

        self->in_level = 0.0;
        
        self->performance_state.internal_exciter = true;
        self->performance_state.internal_strum = true;
        self->performance_state.internal_note = true;
        
        for(int i=0; i<rings::ADC_CHANNEL_LAST; i++)
            self->cvinputs[i] = 0.0;

        self->cvinputs[rings::ADC_CHANNEL_POT_FREQUENCY] = 0.33;
        self->cvinputs[rings::ADC_CHANNEL_POT_STRUCTURE] = 0.25;
        self->cvinputs[rings::ADC_CHANNEL_POT_BRIGHTNESS] = 0.5;
        self->cvinputs[rings::ADC_CHANNEL_POT_DAMPING] = 0.75;
        self->cvinputs[rings::ADC_CHANNEL_POT_POSITION] = 0.25;
        
        // init attenuverters
        for(int i=11; i<16; ++i)
            self->cvinputs[i] = 0.5;
        
        // allocate memory
        self->reverb_buffer = (t_uint16*)sysmem_newptrclear(65536*sizeof(t_uint16));    //32768
        
        
        memset(&self->strummer, 0, sizeof(self->strummer));
        memset(&self->part, 0, sizeof(self->part));
        memset(&self->string_synth, 0, sizeof(self->string_synth));
        

        self->strummer.Init(0.01, rings::Dsp::getSr() / kBlockSize);
        self->part.Init(self->reverb_buffer);
        self->string_synth.Init(self->reverb_buffer);
        
        self->read_inputs.Init();

        self->part.set_polyphony(1);
        self->part.set_model(rings::RESONATOR_MODEL_MODAL);
        self->string_synth.set_polyphony(1);
        self->string_synth.set_fx(rings::FX_FORMANT);
        
        self->easter_egg = false;
        
        // seems like we need this...
        self->obj.z_misc = Z_NO_INPLACE;
    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}



void myObj_easter(t_myObj *self, long m) {
    self->easter_egg = m != 0;
}

void myObj_info(t_myObj *self)
{
    
    rings::Patch p = self->patch;
    rings::PerformanceState ps = self->performance_state;

    
    object_post((t_object*)self, "Patch ----------------------->");
    object_post((t_object*)self, "structure: %f", p.structure);
    object_post((t_object*)self, "brightness: %f", p.brightness);
    object_post((t_object*)self, "damping: %f", p.damping);
    object_post((t_object*)self, "position: %f", p.position);
    
    object_post((t_object*)self, "Performance_state ----------->");
    object_post((t_object*)self, "strum: %d", ps.strum);
    object_post((t_object*)self, "internal_exciter: %d", ps.internal_exciter);
    object_post((t_object*)self, "internal_strum: %d", ps.internal_strum);
    object_post((t_object*)self, "internal_note: %d", ps.internal_note);
    object_post((t_object*)self, "tonic: %f", ps.tonic);
    object_post((t_object*)self, "note: %f", ps.note);
    object_post((t_object*)self, "fm: %f", ps.fm);
    object_post((t_object*)self, "chord: %d", ps.chord);

    object_post((t_object*)self, "-----");
    
}



// plug / unplug patch chords...

void myObj_int(t_myObj *self, long value)
{
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            self->performance_state.internal_exciter = (value == 0);
            break;
        case 6:
            self->performance_state.internal_note = (value == 0);
            break;
        case 7:
            self->performance_state.internal_strum = (value == 0);
            break;
        default:
            break;
    }
}


void myObj_float(t_myObj *self, double m)
{
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 1:
            self->cvinputs[rings::ADC_CHANNEL_POT_FREQUENCY] = clamp(m, 0., 1.);
            break;
        case 2:
            self->cvinputs[rings::ADC_CHANNEL_POT_STRUCTURE] = clamp(m, 0., 1.);
            break;
        case 3:
            self->cvinputs[rings::ADC_CHANNEL_POT_BRIGHTNESS] = clamp(m, 0., 1.);
            break;
        case 4:
            self->cvinputs[rings::ADC_CHANNEL_POT_DAMPING] = clamp(m, 0., 1.);
            break;
        case 5:
            self->cvinputs[rings::ADC_CHANNEL_POT_POSITION] = clamp(m, 0., 1.);
            break;
        default:
            break;
    }
}


#pragma mark ----- main pots -----

void myObj_frequency(t_myObj* self, double m) {
    self->cvinputs[rings::ADC_CHANNEL_POT_FREQUENCY] = clamp(m, 0., 1.);
}

void myObj_structure(t_myObj* self, double m) {
    self->cvinputs[rings::ADC_CHANNEL_POT_STRUCTURE] = clamp(m, 0., 1.);
}

void myObj_brightness(t_myObj* self, double m) {
    self->cvinputs[rings::ADC_CHANNEL_POT_BRIGHTNESS] = clamp(m, 0., 1.);
}

void myObj_damping(t_myObj* self, double m) {
    self->cvinputs[rings::ADC_CHANNEL_POT_DAMPING] = clamp(m, 0., 1.);
}

void myObj_position(t_myObj* self, double m) {
    self->cvinputs[rings::ADC_CHANNEL_POT_POSITION] = clamp(m, 0., 1.);
}



#pragma mark ----- other parameters -----


// this directly sets the pitch via midi note
void myObj_note(t_myObj* self, double n) {
    self->cvinputs[rings::ADC_CHANNEL_POT_FREQUENCY] = (n-12.0) / 60.0;
}

// set polyphony count
void myObj_polyphony(t_myObj* self, long n) {
    if(n>4) n = 4;  // was 3...
    else if(n<1) n = 1;
    self->part.set_polyphony(n);
    self->string_synth.set_polyphony(n);
}

// set resonator model
void myObj_model(t_myObj* self, long n) {
    if(n>5) n = 5;
    else if(n<0) n = 0;
    self->part.set_model(static_cast<rings::ResonatorModel>(n));
    self->string_synth.set_fx(static_cast<rings::FxType>(n));
}

void myObj_bypass(t_myObj* self, long n) {
    self->part.set_bypass(n != 0);
}



// change the sample rate and or blockSize and reinit
void reinit(t_myObj* self, double newSR)
{
    rings::Dsp::setSr(newSR);
    
    self->strummer.Init(0.01, rings::Dsp::getSr() / kBlockSize);
    
    self->part.Init(self->reverb_buffer);
    self->string_synth.Init(self->reverb_buffer);
}


// panic: simply reinit...
void myObj_reset(t_myObj *self) {
    reinit(self, self->sr);
}





#pragma mark ----- dsp loop -----

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double *in = ins[0];
    double *out = outs[0];
    double *out2 = outs[1];
    double *cvinputs = self->cvinputs;
    
    long vs = sampleframes;
    size_t size = kBlockSize;
    
    if (self->obj.z_disabled)
        return;

    // FM input
    cvinputs[0] = clamp(ins[1][0], -48., 48.);
    
    // read 'cv' input signals, store first value of a sig vector
    for(int i=1; i<5; i++) {
        // cv inputs are expected in -1. to 1. range
        cvinputs[i] = clamp(ins[i+1][0], -1., 1.);  // leave out first inlet (which is audio in)
    }
    // if fm_cv is not patched, set it to a halftone constant (?)
    // so fm_attenuverter can be used as pitch fine control
    //if(!self->fm_patched)
      //  cvinputs[0] = 0.0125;
    
    // v/oct input, no limits on range
    cvinputs[5] = ins[6][0];
    
    // 8 signal inlets, last one is strum input
    double *strum = ins[7];
    double trigger = 0.;
    
    // will not be used, if internal exciter is off
    // TODO: should we check for internal_exciter?
    if(self->strum_connected && !self->performance_state.internal_strum) {
#ifdef __APPLE__
        vDSP_sveD(strum, 1, &trigger, vs);  // calc sum of trigger input
#else
        for(int i=0; i<vs; ++i)
            trigger += strum[i];
#endif
    }
    
    cvinputs[16] = trigger;         // cvinputs[16] => ADC_CHANNEL_LAST,
    
    
        
    if(self->easter_egg) {
        for(int count=0; count<vs; count+=size) {
            
            self->read_inputs.Read(&self->patch, &self->performance_state, cvinputs);
            
            self->strummer.Process(NULL, size, &self->performance_state);
            self->string_synth.Process(self->performance_state, self->patch, in+count, out+count, out2+count, size);
        }
    }
    else {
        for(int count=0; count<vs; count+=size) {
            
            self->read_inputs.Read(&self->patch, &self->performance_state, cvinputs);
            
            self->strummer.Process(in+count, size, &self->performance_state);
            self->part.Process(self->performance_state, self->patch, in+count, out+count, out2+count, size);
        }
    }
    
}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    self->strum_connected = count[7];

    if(samplerate != self->sr || maxvectorsize != self->sigvs) {
        self->sr = samplerate;
        self->sigvs = maxvectorsize;
        
        reinit(self, samplerate);
    }
    
    if(self->sigvs < kBlockSize)
        object_warn((t_object*)self, "sigvs can't be smaller than %d samples!", kBlockSize);
    else {
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    }
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);
    if(self->reverb_buffer)
        sysmem_freeptr(self->reverb_buffer);
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) audio IN", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) FREQUENCY_CV (-1..+1), (float) FREQUENCY_POT", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) STRUCTURE_CV (-1..+1), (float) STRUCTURE_POT" , ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) BRIGHTNESS_CV (-1..+1), (float) BRIGHTNESS_POT", ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal) DAMPING_CV (-1..+1), (float) DAMPING_POT", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal) POSITION_CV (-1..+1), (float) POSITION_POT", ASSIST_STRING_MAXSIZE);
                break;
            case 6:
                strncpy(string_dest,"(signal) V/OCT, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 7:
                strncpy(string_dest,"(signal) STRUM, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
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


void ext_main(void* r) {
    this_class = class_new("vb.mi.rngs~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);
    
    // 2 buttons on the top
    class_addmethod(this_class, (method)myObj_polyphony,    "polyphony",    A_LONG, 0);
    class_addmethod(this_class, (method)myObj_model,        "model",        A_LONG, 0);
    
    // main pots
    class_addmethod(this_class, (method)myObj_frequency,    "frequency",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_structure,	"structure",	A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_brightness,   "brightness",   A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_damping,      "damping",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_position,     "position",     A_FLOAT, 0);
    

    // other params
    class_addmethod(this_class, (method)myObj_note,     "note",     A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_bypass,   "bypass",   A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_reset,    "reset", 0);
    class_addmethod(this_class, (method)myObj_int,      "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,    "float",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_info,     "info", 0);
    
    class_addmethod(this_class, (method)myObj_easter,     "easter", A_LONG, 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.rngs~ by volker böhm --> vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'Rings' module");
}
