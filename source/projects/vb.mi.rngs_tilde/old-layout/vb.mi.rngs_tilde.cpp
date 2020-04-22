
// Copyright 2015 Olivier Gillet.
//
// Original Author: Olivier Gillet (ol.gillet@gmail.com)
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
// by volker böhm, jan 2019


#include "c74_msp.h"

#include "read_inputs.h"

#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"
#include "rings/dsp/string_synth_part.h"
//#include "rings/settings.h"
#include "rings/dsp/dsp.h"

#include "Accelerate/Accelerate.h"


using namespace c74::max;


static t_class* this_class = nullptr;


double rings::Dsp::sr = 48000.0;
double rings::Dsp::a3 = 440.0 / 48000.0;
size_t rings::Dsp::maxBlockSize = 64;

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
    void                    *info_out;
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
        
        //self->info_out = outlet_new((t_object *)self, NULL);
        outlet_new(self, "signal"); // 'out' output
        outlet_new(self, "signal"); // 'aux' output
        
        
        self->sr = sys_getsr();
        if(self->sr <= 0.0)
            self->sr = 48000.0;
        
        self->sigvs = sys_getblksize();
        
        // set actual Sampling Rate and block size
        rings::Dsp::setBlockSize(self->sigvs);
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
        

        self->strummer.Init(0.01, rings::Dsp::getSr() / rings::Dsp::getBlockSize());        
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
    //rings::Part *pa = &self->part;
    //rings::State *st = self->settings->mutable_state();
    
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
    /*
    object_post((t_object*)self, "Part ----------------------->");
    object_post((t_object*)self, "polyphony: %d", pa->polyphony());
    object_post((t_object*)self, "model: %d", pa->model());
    
    object_post((t_object*)self, "mySR: %f", rings::Dsp::getSr());
    object_post((t_object*)self, "a3: %f", rings::Dsp::getA3());
    */
    object_post((t_object*)self, "-----");
    
}

void infoCV(t_myObj *self) {
    
    double *cvinfo = self->cvinputs;
    for(int i=0; i<=rings::ADC_CHANNEL_LAST; i++) {
        object_post((t_object*)self, "cvinput[%d]: %f", cvinfo[i]);
    }
    object_post(NULL, "----------------------");
    /*ADC_CHANNEL_CV_FREQUENCY,   // 0
     ADC_CHANNEL_CV_STRUCTURE,   // 1
     ADC_CHANNEL_CV_BRIGHTNESS,  // 2
     ADC_CHANNEL_CV_DAMPING,     // 3
     ADC_CHANNEL_CV_POSITION,    // 4
     ADC_CHANNEL_CV_V_OCT,       // 5
     ADC_CHANNEL_POT_FREQUENCY,  // 6
     ADC_CHANNEL_POT_STRUCTURE,  // 7
     ADC_CHANNEL_POT_BRIGHTNESS, // 8
     ADC_CHANNEL_POT_DAMPING,    // 9
     ADC_CHANNEL_POT_POSITION,   // 10
     ADC_CHANNEL_ATTENUVERTER_FREQUENCY, // 11
     ADC_CHANNEL_ATTENUVERTER_STRUCTURE, // 12
     ADC_CHANNEL_ATTENUVERTER_BRIGHTNESS,// 13
     ADC_CHANNEL_ATTENUVERTER_DAMPING,   // 14
     ADC_CHANNEL_ATTENUVERTER_POSITION,  // 15
     ADC_CHANNEL_LAST,       // use this for strum? yes
     */
}


// plug / unplug patch chords...

void myObj_int(t_myObj *self, long value)
{
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            self->performance_state.internal_exciter = (value == 0);
            break;
        case 1:
            self->fm_patched = (value != 0);
            break;
        case 2:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 3:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 4:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 5:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
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


#pragma mark --------- Attenuverters ----------------
//accept input from -1. to 1., but scale it to 0. to 1. for internal use

void myObj_freq_mod_amount(t_myObj* self, double m) {
    m = clamp(m, -1., 1.);
    self->cvinputs[rings::ADC_CHANNEL_ATTENUVERTER_FREQUENCY] = (m+1.)*0.5;
}

void myObj_struct_mod_amount(t_myObj* self, double m) {
    m = clamp(m, -1., 1.);
    self->cvinputs[rings::ADC_CHANNEL_ATTENUVERTER_STRUCTURE] = (m+1.)*0.5;
}

void myObj_bright_mod_amount(t_myObj* self, double m) {
    m = clamp(m, -1., 1.);
    self->cvinputs[rings::ADC_CHANNEL_ATTENUVERTER_BRIGHTNESS] = (m+1.)*0.5;
}

void myObj_damp_mod_amount(t_myObj* self, double m) {
    m = clamp(m, -1., 1.);
    self->cvinputs[rings::ADC_CHANNEL_ATTENUVERTER_DAMPING] = (m+1.)*0.5;
}

void myObj_pos_mod_amount(t_myObj* self, double m) {
    m = clamp(m, -1., 1.);
    self->cvinputs[rings::ADC_CHANNEL_ATTENUVERTER_POSITION] = (m+1.)*0.5;
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
void reinit(t_myObj* self, double newSR, size_t newBlockSize)
{
    rings::Dsp::setSr(newSR);
    rings::Dsp::setBlockSize(newBlockSize);
    
    //delete self->strummer;
    //self->strummer = new rings::Strummer;
    self->strummer.Init(0.01, rings::Dsp::getSr() / rings::Dsp::getBlockSize());
    
    self->part.Init(self->reverb_buffer);
    self->string_synth.Init(self->reverb_buffer);
}


// panic: simply reinit...
void myObj_reset(t_myObj *self) {
    reinit(self, self->sr, self->sigvs);
}





#pragma mark ----- dsp loop -----

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double *in = ins[0];
    double *out = outs[0];
    double *out2 = outs[1];
    double *cvinputs = self->cvinputs;
    
    long vs = sampleframes;
    size_t size = vs;
    
    if (self->obj.z_disabled)
        return;

    
    // read 'cv' input signals, store first value of a sig vector
    for(int i=0; i<5; i++) {
        // cv inputs are expected in -1. to 1. range
        cvinputs[i] = clamp(ins[i+1][0], -1., 1.);  // leave out first inlet (which is audio in)
    }
    // if fm_cv is not patched, set it to a halftone constant (?)
    // so fm_attenuverter can be used as pitch fine control
    if(!self->fm_patched)
        cvinputs[0] = 0.0125;
    
    // v/oct input, no limits on range
    cvinputs[5] = ins[6][0];
    
    // 8 signal inlets, last one is strum input
    double *strum = ins[7];
    double trigger = 0.;
    
    // will not be used, if internal exciter is off
    // TODO: should we check for intern.exciter?
    if(self->strum_connected && !self->performance_state.internal_strum)
        vDSP_sveD(strum, 1, &trigger, vs);  // calc sum of trigger input
    
    cvinputs[16] = trigger;         // cvinputs[16] => ADC_CHANNEL_LAST,
    
    self->read_inputs.Read(&self->patch, &self->performance_state, cvinputs);
    
    if(self->easter_egg) {
        self->strummer.Process(NULL, size, &self->performance_state);  // TODO: check this out
        self->string_synth.Process(self->performance_state, self->patch, in, out, out2, size);
    }
    else {
        self->strummer.Process(in, size, &self->performance_state);
        self->part.Process(self->performance_state, self->patch, in, out, out2, size);
    }

}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    self->strum_connected = count[7];

    if(samplerate != self->sr || maxvectorsize != self->sigvs) {
        self->sr = samplerate;
        self->sigvs = maxvectorsize;
        
        reinit(self, samplerate, maxvectorsize);
    }
    
    if(self->sigvs > 1024)
        object_warn((t_object*)self, "signal vector size can't be higher than 1024 samples, sorry!");
    else {
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    }
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);
    //delete self->strummer;
    //delete self->string_synth;
    //delete self->part;
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
                strncpy(string_dest,"(signal) FREQUENCY (-1..+1), (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) STRUCTURE (-1..+1)" , ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) BRIGHTNESS (-1..+1)", ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal) DAMPING (-1..+1)", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal) POSITION (-1..+1)", ASSIST_STRING_MAXSIZE);
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
    
    
    // attenuverters
    class_addmethod(this_class, (method)myObj_freq_mod_amount,      "freq_mod",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_struct_mod_amount,    "struct_mod",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_bright_mod_amount,    "bright_mod",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_damp_mod_amount,      "damp_mod",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_pos_mod_amount,       "pos_mod",         A_FLOAT, 0);

    // other params
    class_addmethod(this_class, (method)myObj_note,     "note",     A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_bypass,   "bypass",   A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_reset,    "reset", 0);
    class_addmethod(this_class, (method)myObj_int,      "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_info,     "info", 0);
    //class_addmethod(this_class, (method)infoCV,     "cvinfo", 0);
    
    class_addmethod(this_class, (method)myObj_easter,     "easter", A_LONG, 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.rngs~ by volker böhm --> vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'rings' module");
}
