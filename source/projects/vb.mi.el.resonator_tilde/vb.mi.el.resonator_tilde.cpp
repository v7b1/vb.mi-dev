#include "c74_msp.h"

#include "dsp.h"
#include "resonator.h"

#include "Accelerate/Accelerate.h"

#include <iostream>


#define NUMFILT 64

using namespace c74::max;


static t_class* this_class = nullptr;


struct t_myObj {
    t_pxobject  x_obj;
    double      sr;
    bool       bypass;
    g_svf       **bfilters;
    
    double      *freqs;
    double      *qs;
    double      *gains;
    
    double      position;
    double      prev_position;
    g_cosOsc    *lfo;
    
    elements::Resonator   resonator;
    double      spread;
    double      *center;
    double      *side;
};



void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 1);            // one signal inlet
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        
        self->sr = sys_getsr();
        if(self->sr <= 0.0)
            self->sr = 44100.;
        
        
        // alloc mem
        self->freqs = (double*)sysmem_newptr(NUMFILT*sizeof(double));
        self->qs = (double*)sysmem_newptr(NUMFILT*sizeof(double));
        self->gains = (double*)sysmem_newptr(NUMFILT*sizeof(double));
        
        // create filters
        self->bfilters = (g_svf **)calloc(NUMFILT, sizeof(g_svf));
        for(int i=0; i<NUMFILT; i++) {
            double f = 0.001 * (i+1);
            double g = (NUMFILT-i)*0.01;
            self->bfilters[i] = svf_make(f, 500, g);
            
            self->freqs[i] = f * self->sr;
            self->qs[i] = 500;
            self->gains[i] = g;
        }
        
        self->bypass = false;
        self->position = 0.5;
        self->prev_position = 0.5;
        self->lfo = cosOsc_make(self->position);
        self->spread = 0.5;
        
        self->center = (double*)sysmem_newptrclear(4096*sizeof(double));
        self->side = (double*)sysmem_newptrclear(4096*sizeof(double));
        
        self->resonator.Init();
        
        self->x_obj.z_misc = Z_NO_INPLACE;     // TODO: check out Z_NO_INPLACE
        
        //attr_args_process(x, argc, argv);            // process attributes
    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}



void myObj_int(t_myObj *self, long input) {
    //x->myint = input;
    //object_post((t_object *)x, "myint: %d", x->myint);
}

void myObj_float(t_myObj *self, double input) {
    //x->myfloat = input;
    //object_post((t_object *)x, "myfloat: %f", x->myfloat);
}

#pragma mark --------- resonator params ------------
void myObj_bypass(t_myObj *self, long input) {

    self->bypass = ( input != 0);
}


void myObj_position(t_myObj *self, double input) {
    self->position = input;
    self->resonator.set_position(input);
}

void myObj_frequency(t_myObj *self, double input) {
    self->resonator.set_frequency(input / self->sr);
    self->resonator.ComputeFilters();
}

void myObj_geometry(t_myObj *self, double input) {
    self->resonator.set_geometry(input);
    self->resonator.ComputeFilters();
}

void myObj_brightness(t_myObj *self, double input) {
    self->resonator.set_brightness(input);
    self->resonator.ComputeFilters();
}

void myObj_damping(t_myObj *self, double input) {
    self->resonator.set_damping(input);
    self->resonator.ComputeFilters();
}

void myObj_spread(t_myObj *self, double input) {
    self->spread = clamp(input, 0., 1.);
}

void update_filters(t_myObj *self) {
    /*
    int i;
    for(i=0; i<NUMFILT; i++) {
        double freq = self->freqs[i] / self->sr;
        //post("freq %f -- q %f -- gain %f", freq, x->qs[i], x->gains[i]);
        svf_set_f_q_g(self->bfilters[i], freq, self->qs[i], self->gains[i]);
    }*/
    self->resonator.setFilters(self->freqs, self->qs, self->gains);
}

void myObj_mode(t_myObj *self, long a) {
    //object_post(NULL, "mode: %ld", a);
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


int atom_getdouble_array(long ac, t_atom *av, long count, double *vals) {
    int err = 0;
    
    for(int i=0; i<count; ++i) {
        *vals++ = atom_getfloat(av+i);
    }
    
    return err;
}


void myObj_setFreqs(t_myObj *self, t_symbol *mess, short argc, t_atom *argv)
{
    if(argc>NUMFILT) argc = NUMFILT;
    atom_getdouble_array(argc, argv, argc, self->freqs);
    
    update_filters(self);
}

void myObj_setQs(t_myObj *self, t_symbol *mess, short argc, t_atom *argv)
{
    if(argc>NUMFILT) argc = NUMFILT;
    atom_getdouble_array(argc, argv, argc, self->qs);
    
    update_filters(self);
}

void myObj_setGains(t_myObj *self, t_symbol *mess, short argc, t_atom *argv)
{
    if(argc>NUMFILT) argc = NUMFILT;
    atom_getdouble_array(argc, argv, argc, self->gains);
    
    update_filters(self);
}



#pragma mark ----- dsp loop -----

// 64 bit signal input version
void myObj_perform64(t_myObj *self, t_object *dsp64, double **ins, long numins,
                     double **outs, long numouts, long sampleframes, long flags, void *userparam){
    
    double *in = ins[0];
    double *outL = outs[0];
    double *outR = outs[1];
    long vs = sampleframes;

    //g_cosOsc *lfo = self->lfo;
    double  spread = self->spread;
    double  *center = self->center;
    double  *side = self->side;
    
    if (self->x_obj.z_disabled)
        return;
    if(self->bypass)
        goto byp;
    
    
    //cos_Osc_setFreq(lfo, self->position);
    
    /*

    // calculate filters
    
    for(int i=0; i<NUMFILT; i++) {
        //svf_do_block(self->bfilters[i], in, out, vs);
        svf_do_block_amp(self->bfilters[i], in, out, cosOsc_next(lfo), vs);
    }*/
    
    memset(center, 0, vs*sizeof(double));
    memset(side, 0, vs*sizeof(double));
    
    self->resonator.Process(NULL, in, center, side, sampleframes);
    
    for(int i=0; i<sampleframes; i++) {
        double s = side[i] * spread;
        outL[i] = center[i] + s;
        outR[i] = center[i] - s;
    }
    
    return;
    
byp:
    while (vs--)
        *outL++ = *outR++ = (*in++);
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
    
    for(int i=0; i<NUMFILT; i++)
        svf_free(self->bfilters[i]);
    
    if(self->freqs) sysmem_freeptr(self->freqs);
    if(self->qs) sysmem_freeptr(self->qs);
    if(self->gains) sysmem_freeptr(self->gains);
    if(self->lfo) cosOsc_free(self->lfo);
    
    if(self->center) sysmem_freeptr(self->center);
    if(self->side) sysmem_freeptr(self->side);
}




void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) audio in 'blow' (red)", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) audio in 'strike' (green)", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) V/OCT, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) FM", ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal) STRENGTH, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal) BOW TIMBRE, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 6:
                strncpy(string_dest,"(signal) FLOW, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 7:
                strncpy(string_dest,"(signal) BLOW TIMBRE, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 8:
                strncpy(string_dest,"(signal) MALLET, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 9:
                strncpy(string_dest,"(signal) STRIKE TIMBRE, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 10:
                strncpy(string_dest,"(signal) DAMPING, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 11:
                strncpy(string_dest,"(signal) GEOMETRY, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 12:
                strncpy(string_dest,"(signal) POSITION, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 13:
                strncpy(string_dest,"(signal) BRIGHTNESS, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 14:
                strncpy(string_dest,"(signal) SPACE, (float) attenuvert", ASSIST_STRING_MAXSIZE);
                break;
            case 15:
                strncpy(string_dest,"(signal) GATE", ASSIST_STRING_MAXSIZE);
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
            case 2:
                strncpy(string_dest,"info outlet", ASSIST_STRING_MAXSIZE);
                break;
        }
    }
}


void ext_main(void* r) {
    this_class = class_new("vb.mi.el.resonator~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(this_class, (method)myObj_float, "float", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_int, "int", A_LONG, 0);
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
    
    class_addmethod(this_class, (method)myObj_assist, "assist", A_CANT,0);
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.el.resonator~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "resonator of the 'elements' module by mutable instruments");
}
