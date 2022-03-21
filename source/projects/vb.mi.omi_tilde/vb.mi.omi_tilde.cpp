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


// based on 'ominous': a dark 2x2-op FM synth by mutable instruments
// hidden as 'easter egg' inside the Elements eurorack module.
// stripped down to a bare synth voice and slightly optimized
// by volker böhm, may 2020, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/




#include "c74_msp.h"
#include "omi/dsp/part.h"
#ifdef __APPLE__
#include "Accelerate/Accelerate.h"
#endif


using namespace c74::max;


const int kMaxBlockSize = omi::kMaxBlockSize;
//const int kBlockSize = 16;

static t_class* this_class = nullptr;


struct t_myObj {
    t_pxobject	obj;
    
    omi::Part      *part;
    omi::PerformanceState ps;
    bool                uigate;
    
    short               gate_connected;
    double              sr;
};


void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 2);
        
        outlet_new(self, "signal"); // 'raw' output
        outlet_new(self, "signal"); // 'out L' output
        outlet_new(self, "signal"); // 'out R' output
        
        self->sr = sys_getsr();
        if(self->sr <= 0.)
            self->sr = 44100.0;
        
        // init some params
        self->uigate = false;
        self->ps.note = 48;
        self->ps.strength = 0.5;
        self->ps.modulation = 0.0;
        self->ps.gate = 0;
        
        
        // Init and seed the random parameters and generators with the serial number.
        self->part = new omi::Part;
        self->part->Init(self->sr);

        uint32_t mySeed = 0x1fff7a10;
        self->part->Seed(&mySeed, 3);
        

    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}


void myObj_int(t_myObj *self, long value)
{
    self->uigate = (value != 0);
}


void myObj_button(t_myObj *self, long b) {
    self->uigate = (b != 0);
}


#pragma mark ----- exciter pots -----

// contour (env_shape)
void myObj_contour(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_envelope_shape = m;
}

// detune
void myObj_detune(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_bow_level = m;
}

// osc1 level
void myObj_osc1(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_blow_level = m;
}

// osc2 level
void myObj_osc2(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_strike_level = m;
}

// ratio1
void myObj_ratio1(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_blow_meta = m;
}

// ratio2
void myObj_ratio2(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_strike_meta = m;
}


void myObj_fb(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_bow_timbre = m;
}

void myObj_fm1(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_blow_timbre = m;
}

void myObj_fm2(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->exciter_strike_timbre = m;
}


#pragma mark ----- multimode filter and space -----

void myObj_filter_mode(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->resonator_geometry = m;
}

void myObj_cutoff(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->resonator_brightness = m;
}

void myObj_filter_env(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->resonator_damping = m;
}

void myObj_rotation(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->resonator_position = m;
}

void myObj_space(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->space = m;
}

// vb: add some scalable resonance to the multimode filter
void myObj_reson(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->resonance = m * 10.0;
}

// vb: add some scalable resonance to the multimode filter
void myObj_xfb(t_myObj* self, double m) {
    m = CLAMP(m, 0., 1.);
    self->part->mutable_patch()->cross_fb = m;
}


void myObj_strength(t_myObj* self, double m) {
    m = CLAMP(m, 0.0, 1.0);
    self->ps.strength = m;
}


// this directly sets the pitch via a midi note
void myObj_note(t_myObj* self, double n) {
    self->ps.note = n;
}


inline void SoftLimit_block(t_myObj *self, double *inout, size_t size)
{
    while(size--) {
        double x = *inout * 0.5;
        double x2 = x * x;
        *inout = x * (27.0 + x2) / (27.0 + 9.0 * x2);
        inout++;
    }
}




#pragma mark ----- dsp loop -----

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double *audio_in = ins[0];
    double *gate = ins[1];
    double *outL = outs[0];
    double *outR = outs[1];
    double *raw = outs[2];
    
    long vs = sampleframes;
    size_t size = kMaxBlockSize;
    omi::PerformanceState *ps = &self->ps;
    
    if (self->obj.z_disabled)
        return;
    
    ps->gate = self->uigate;          // ui button pressed?

    for(int count=0; count<vs; count+=size) {
        
        if(self->gate_connected) {
            double trigger = 0.0;
#ifdef __APPLE__
            vDSP_sveD(gate+count, 1, &trigger, size);   // calc sum of input vector
#else
            for(int i=0; i<size; ++i)
                trigger += gate[i+count];
#endif
            ps->gate |= trigger > 0.0;
        }
        
        self->part->Process(*ps, audio_in+count, outL+count, outR+count, raw+count, size);
    }

    SoftLimit_block(self, outL, vs);
    SoftLimit_block(self, outR, vs);

}




void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    if(maxvectorsize < kMaxBlockSize) {
        object_error((t_object*)self, "vector size can't be smaller than %d samples, sorry!", kMaxBlockSize);
        return;
    }
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->part->Init(self->sr);
    }

    self->gate_connected = count[2];
    
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);
    delete self->part;
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) fm audio in", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) GATE", ASSIST_STRING_MAXSIZE);
                break;
        }
    }
    else if (io == ASSIST_OUTLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) OUT L", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) OUT R", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) Raw Osc output", ASSIST_STRING_MAXSIZE);
                break;
        }
    }
}


void ext_main(void* r) {
    this_class = class_new("vb.mi.omi~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);
    
    // exciter pots
    class_addmethod(this_class, (method)myObj_contour,  "contour",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_detune,	"detune",         A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_osc1,     "osc1",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_osc2,     "osc2",    A_FLOAT, 0);
    
    // bigger pots
    class_addmethod(this_class, (method)myObj_ratio1,   "ratio1",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_ratio2,   "ratio2",    A_FLOAT, 0);
    
    // timbre pots
    class_addmethod(this_class, (method)myObj_fb,       "fb",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_fm1,      "fm1",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_fm2,      "fm2",        A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_strength,      "strength",    A_FLOAT, 0);
    
    // filter pots
    class_addmethod(this_class, (method)myObj_filter_mode, "filter_mode",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_cutoff, "cutoff",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_filter_env, "filter_env",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_rotation, "rotation",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_space, "space",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_reson, "reson",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_xfb, "xfb",    A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_note,	"note",	A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_button,  "play",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_int,  "int",          A_LONG, 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.omi~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'ominous synth'");
}
