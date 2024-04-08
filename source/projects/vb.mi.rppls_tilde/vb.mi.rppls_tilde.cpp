//
//    Copyright 2020 Volker Böhm.
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful, but
//    WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program. If not, see http://www.gnu.org/licenses/ .


// A clone of mutable instruments' 'Ripples' Eurorack module
// by volker böhm, july 2020, https://vboehm.net
// based on the virtual analog model provided by
// Alright Devices https://www.alrightdevices.com/
// done for VCVrack https://vcvrack.com/


// Original module conceived by Émilie Gillet, https://mutable-instruments.net/


#include "c74_msp.h"

#include "ripples.hpp"



using namespace c74::max;


static t_class* this_class = nullptr;



struct t_myObj {
    t_pxobject	obj;
    
    ripples::RipplesEngine  engine;
    ripples::RipplesEngine::Frame frame;
    float                   sr;
    int                     sigvs;
    double                  drive;
};



void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 3);        // 3 signal inlets
        
        outlet_new(self, "signal"); // 'BP2' output
        outlet_new(self, "signal"); // 'LP2' output
        outlet_new(self, "signal"); // 'LP4' output
        //outlet_new(self, "signal"); // 'LPVCA' output
        
        self->sigvs = sys_getblksize();
        
        
        self->sr = sys_getsr();
        if(self->sr <= 0.0)
            self->sr = 48000.0;
        
        
        // set actual Sampling Rate
        self->engine.setSampleRate(self->sr);
        self->drive = 1.0;

    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}



void myObj_float(t_myObj *self, double m)
{
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            self->drive = fmax(m, 1.0);
            break;
        case 1:
            self->frame.freq_knob = CLAMP(m, 0., 1.);
            break;
        case 2:
            self->frame.res_knob = CLAMP(m, 0., 1.);
            break;
        case 3:
//            self->frame.fm_knob = clamp(m, -1., 1.);
            break;
        default:
            break;
    }
}


#pragma mark ----- main pots -----

void myObj_frequency(t_myObj* self, double m) {
    self->frame.freq_knob = CLAMP(m, 0., 1.);
}

void myObj_resonance(t_myObj* self, double m) {
    self->frame.res_knob = CLAMP(m, 0., 1.);
}


inline void SoftLimit_block(double *inout, double drive, size_t size) {
    while(size--) {
        *inout *= drive;
        double x2 = (*inout)*(*inout);
        *inout *= (27.0 + x2) / (27.0 + 9.0 * x2);
        inout++;
    }
}




#pragma mark ----- dsp loop -----

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double *in = ins[0];
    double *freq_in = ins[1];
    double *res_in = ins[2];
    
    double *out1 = outs[0];
    double *out2 = outs[1];
    double *out3 = outs[2];
    //double *out4 = outs[3];


    size_t vs = sampleframes;
    
    if (self->obj.z_disabled)
        return;
    
    ripples::RipplesEngine  *engine = &self->engine;
    ripples::RipplesEngine::Frame *frame = &self->frame;

    
    for(int i=0; i<sampleframes; ++i) {
        frame->input = (float)(in[i] * 10.0);   // vb, scale up to -10 .. +10 input range
        frame->freq_cv = (float)freq_in[i];
        frame->res_cv = (float)res_in[i];
        
        engine->process(*frame);
        
        out1[i] = frame->bp2;
        out2[i] = frame->lp2;
        out3[i] = frame->lp4;
        //out4[i] = frame->lp4vca;
    }
    
//    engine->process_block(in, out1, *frame, vs);
    
    if(self->drive > 1.0)
        SoftLimit_block(out3, self->drive, vs);

    
}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->engine.setSampleRate(self->sr);
    }
    self->sigvs = maxvectorsize;
    

    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);

}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);

}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) audio IN", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal/float) cutoff/center frequency", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal/float) resonance" , ASSIST_STRING_MAXSIZE);
                break;
        }
    }
    else if (io == ASSIST_OUTLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) BP2", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) LP2", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) LP4", ASSIST_STRING_MAXSIZE);
                break;
//            case 3:
//                strncpy(string_dest,"(signal) LP4_VCA", ASSIST_STRING_MAXSIZE);
//                break;
        }
    }
}


void ext_main(void* r) {
    this_class = class_new("vb.mi.rppls~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);
    
    class_addmethod(this_class, (method)myObj_frequency,    "freq",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_resonance,	"res",	A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_float,    "float",    A_FLOAT, 0);

    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.rppls~ by volker böhm --> vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'Ripples' module");
    object_post(NULL, "based on a virtual analog model by Alright Devices");
}
