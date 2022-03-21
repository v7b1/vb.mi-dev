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


// based on resonator code from the 'elements' eurorack module
// by volker böhm, okt 2019, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/


#include "c74_msp.h"

#include "dsp.h"
#include "resonator.h"

//#include "Accelerate/Accelerate.h"


// TODO: make kNumModes settable



using namespace c74::max;


static t_class* this_class = nullptr;
t_symbol *ps_freqs;
t_symbol *ps_qs;

struct t_myObj {
    t_pxobject  x_obj;
    double      sr;
    bool        bypass;
    
    double      *freqs;
    double      *qs;
    double      *gains;
    t_atom      *atoms;
    
    double      position;
    double      prev_position;
    g_cosOsc    *lfo;
    
    Resonator   resonator;
    double      spread;
    double      *center;
    double      *side;
    
    // last coeffs calculation method
    bool        filter_calc_elements;
    void        *info_out;
};



void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 1);            // one signal inlet
        self->info_out = outlet_new((t_object *)self, NULL);
        outlet_new(self, "signal");
        outlet_new(self, "signal");;
        
        self->sr = sys_getsr();
        if(self->sr <= 0.0)
            self->sr = 44100.;
        
        
        // alloc mem
        self->freqs = (double*)sysmem_newptr(kMaxModes*sizeof(double));
        self->qs = (double*)sysmem_newptr(kMaxModes*sizeof(double));
        self->gains = (double*)sysmem_newptr(kMaxModes*sizeof(double));
        self->atoms = (t_atom*)sysmem_newptr(kMaxModes*sizeof(t_atom));
        
        // create filters
        for(int i=0; i<kMaxModes; i++) {
            double f = 0.001 * (i+1);
            
            self->freqs[i] = f * self->sr;
            self->qs[i] = 500;
            self->gains[i] = 0.25;
        }
        
        
        self->bypass = false;
        self->position = 0.5;
        self->prev_position = 0.5;
        self->lfo = cosOsc_make(self->position);
        self->spread = 0.5;
        
        self->center = (double*)sysmem_newptrclear(4096*sizeof(double));
        self->side = (double*)sysmem_newptrclear(4096*sizeof(double));
        
        self->resonator.Init(self->sr);
        
        self->resonator.set_frequency(100.0);
        self->resonator.ComputeFilters(self->freqs, self->qs, self->gains);
        self->filter_calc_elements = true;

    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}



// output freq and q arrays
void myObj_getInfo(t_myObj *self) {
    t_atom *a = self->atoms;
    
    for(int i=0; i<kMaxModes; ++i)
        atom_setfloat(&a[i], self->qs[i]);
    outlet_anything(self->info_out, ps_qs, kMaxModes, a);
    
    for(int i=0; i<kMaxModes; ++i)
        atom_setfloat(&a[i], self->freqs[i]);
    outlet_anything(self->info_out, ps_freqs, kMaxModes, a);
}


void update_elements_filters(t_myObj *self) {
    self->resonator.ComputeFilters(self->freqs, self->qs, self->gains);
    self->filter_calc_elements = true;
    myObj_getInfo(self);
}

void update_filters(t_myObj *self) {
    self->filter_calc_elements = false;
    self->resonator.setFilters(self->freqs, self->qs, self->gains);
}



#pragma mark --------- elements resonator params ------------

void myObj_frequency(t_myObj *self, double input) {
    CONSTRAIN(input, 8.0, 15000.0);
    self->resonator.set_frequency(input);
    update_elements_filters(self);
}

void myObj_geometry(t_myObj *self, double input) {
    CONSTRAIN(input, 0.0, 1.0);
    self->resonator.set_geometry(input);
    update_elements_filters(self);
}

void myObj_brightness(t_myObj *self, double input) {
    CONSTRAIN(input, 0.0, 1.0);
    self->resonator.set_brightness(input);
    update_elements_filters(self);
}

void myObj_damping(t_myObj *self, double input) {
    CONSTRAIN(input, 0.0, 1.0);
    self->resonator.set_damping(input);
    update_elements_filters(self);
}


void myObj_position(t_myObj *self, double input) {
    CONSTRAIN(input, 0.0, 1.0);
    self->position = input;
    self->resonator.set_position(input);
}

void myObj_spread(t_myObj *self, double input) {
    self->spread = CLAMP(input, 0., 1.);
}

void myObj_bypass(t_myObj *self, long input) {
    
    self->bypass = ( input != 0);
}


void myObj_mode(t_myObj *self, long a) {
    switch(a) {
        case 0:
            self->resonator.calc_res = true;
            self->resonator.calc_wg = true;
            break;
        case 1:
            self->resonator.calc_res = true;
            self->resonator.calc_wg = false;
            break;
        case 2:
            self->resonator.calc_res = false;
            self->resonator.calc_wg = true;
            break;
        default:
            self->resonator.calc_res = true;
            self->resonator.calc_wg = true;
    }
    
}


int atom_getdouble_array(long ac, t_atom *av, long count, double *vals, double min, double max) {
    int err = 0;
    
    for(int i=0; i<count; ++i) {
        *vals++ = CLAMP( atom_getfloat(av+i), min, max );
    }
    
    return err;
}


void myObj_setFreqs(t_myObj *self, t_symbol *mess, short argc, t_atom *argv)
{
    if(argc>kMaxModes) argc = kMaxModes;
    atom_getdouble_array(argc, argv, argc, self->freqs, 8.0, 20000.0);
    
    update_filters(self);
}

void myObj_setQs(t_myObj *self, t_symbol *mess, short argc, t_atom *argv)
{
    if(argc>kMaxModes) argc = kMaxModes;
    atom_getdouble_array(argc, argv, argc, self->qs, 1.0, 99999.0);
    
    update_filters(self);
}

void myObj_setGains(t_myObj *self, t_symbol *mess, short argc, t_atom *argv)
{
    if(argc>kMaxModes) argc = kMaxModes;
    atom_getdouble_array(argc, argv, argc, self->gains, 0.0, 1.0);
    
    if(self->filter_calc_elements)
        update_elements_filters(self);
    else
        update_filters(self);
}

void myObj_xf_resonators(t_myObj *self, double f)
{
    CONSTRAIN(f, 0.0, 1.0);
    self->resonator.xf_resonators(f);
}

inline void SoftLimit_block(double *inout, size_t size) {
    while(size--) {
        double x2 = (*inout)*(*inout);
        *inout *= (27.0 + x2) / (27.0 + 9.0 * x2);
        inout++;
    }
}


#pragma mark ----- dsp loop -----

// 64 bit signal input version
void myObj_perform64(t_myObj *self, t_object *dsp64, double **ins, long numins,
                     double **outs, long numouts, long sampleframes, long flags, void *userparam){
    
    double *in = ins[0];
    double *outL = outs[0];
    double *outR = outs[1];
    long vs = sampleframes;

    double  spread = self->spread;
    double  *center = self->center;
    double  *side = self->side;
    
    if (self->x_obj.z_disabled)
        return;
    
    if(self->bypass) {
        while (vs--)
            *outL++ = *outR++ = (*in++);
        return;
    }
    
    memset(center, 0, vs*sizeof(double));
    memset(side, 0, vs*sizeof(double));
    
    self->resonator.Process(in, center, side, sampleframes);
    
    for(int i=0; i<sampleframes; i++) {
        double s = side[i] * spread;
        outL[i] = center[i] + s;
        outR[i] = center[i] - s;
    }
    
    SoftLimit_block(outL, sampleframes);
    SoftLimit_block(outR, sampleframes);
}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    self->sr = samplerate;
    if(self->sr<=0) self->sr = 44100.0;

    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self)
{
    dsp_free((t_pxobject*)self);
    
    if(self->freqs) sysmem_freeptr(self->freqs);
    if(self->qs) sysmem_freeptr(self->qs);
    if(self->gains) sysmem_freeptr(self->gains);
    if(self->lfo) cosOsc_free(self->lfo);
    if(self->atoms) sysmem_freeptr(self->atoms);
    
    if(self->center) sysmem_freeptr(self->center);
    if(self->side) sysmem_freeptr(self->side);
}




void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) audio in", ASSIST_STRING_MAXSIZE);
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
        }
    }
}


void ext_main(void* r) {
    this_class = class_new("vb.mi.reson~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(this_class, (method)myObj_bypass, "bypass", A_LONG, 0);
    class_addmethod(this_class, (method)myObj_position, "position", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_setFreqs, "setFreqs", A_GIMME, 0);
    class_addmethod(this_class, (method)myObj_setQs, "setQs", A_GIMME, 0);
    class_addmethod(this_class, (method)myObj_setGains, "setGains", A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_frequency, "frequency", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_geometry, "geometry", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_brightness, "brightness", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_damping, "damping", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_spread, "spread", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_mode, "mode", A_LONG, 0);
    class_addmethod(this_class, (method)myObj_xf_resonators, "xfade", A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_assist, "assist", A_CANT,0);
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    ps_freqs = gensym("freqs");
    ps_qs = gensym("qs");
    
    object_post(NULL, "vb.mi.reson~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "resonator of the 'elements' module by mutable instruments");
}
