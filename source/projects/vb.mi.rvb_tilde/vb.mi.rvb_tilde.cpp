#include "c74_msp.h"

#include "reverb.h"

//#include "Accelerate/Accelerate.h"

//#include <iostream>


using namespace c74::max;


static t_class* this_class = nullptr;


struct t_myObj {
    t_pxobject  x_obj;
    double      sr;
    bool        bypass;
    
    Reverb      *reverb_;
    uint16_t    *reverb_buffer;
    double      reverb_amount;
    double      reverb_time;
    double      reverb_diffusion;
    double      reverb_lp;
    double      space;
    double      input_gain;
    bool        freeze;
};



void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 2);
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        
        self->sr = sys_getsr();
        if(self->sr <= 0.0)
            self->sr = 44100.;
        
        
        // alloc mem
        self->reverb_buffer = (t_uint16*)sysmem_newptrclear(32768*sizeof(t_uint16)); // 65536
        
        
        if(self->reverb_buffer == NULL) {
            object_post((t_object*)self, "mem alloc failed!");
            object_free(self);
            self = NULL;
            return self;
        }
        
        self->bypass = false;
        
        self->reverb_ = new Reverb;
        self->reverb_->Init(self->reverb_buffer);
        
        self->reverb_amount = 0.5;
        self->reverb_time = 0.5;
        self->reverb_diffusion = 0.625;
        self->reverb_lp = 0.7;
        self->input_gain = 0.2;
        
        self->reverb_->set_time(self->reverb_time);
        self->reverb_->set_input_gain(self->input_gain);
        self->reverb_->set_lp(self->reverb_lp);
        self->reverb_->set_amount(self->reverb_amount);
        self->reverb_->set_diffusion(self->reverb_diffusion);
        self->reverb_->set_hp(0.995);
        
        self->freeze = false;
        
        //attr_args_process(x, argc, argv);            // process attributes
    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}



void myObj_float(t_myObj *self, double input) {
    //x->myfloat = input;
    //object_post((t_object *)x, "myfloat: %f", x->myfloat);
}


void myObj_bypass(t_myObj *self, long input) {
    self->bypass = ( input != 0);
}

#pragma mark ----------- reverb parameters -------------

void myObj_input_gain(t_myObj *self, double input) {
    self->input_gain = clamp(input, 0.0, 1.0);
    self->reverb_->set_input_gain(self->input_gain);
}

void myObj_time(t_myObj *self, double input) {
    self->reverb_time = clamp(input, 0., 1.5);      // everything above 1.0 grows and distorts!
    self->reverb_->set_time(self->reverb_time);
}

void myObj_diffusion(t_myObj *self, double input) {
    self->reverb_diffusion = clamp(input, 0.0, 1.0);
    self->reverb_->set_diffusion(self->reverb_diffusion);
}

void myObj_amount(t_myObj *self, double input) {
    self->reverb_amount = clamp(input, 0.0, 1.0);
    self->reverb_->set_amount(self->reverb_amount);
}

void myObj_lp(t_myObj *self, double input) {
    self->reverb_lp = clamp(input, 0.0, 1.0);
    self->reverb_->set_lp(self->reverb_lp);
}

void myObj_hp(t_myObj *self, double input) {
    self->reverb_->set_hp(clamp(input, 0.0, 1.0));
    //(self->reverb_lp);
}


void myObj_space(t_myObj *self, double input) {
    self->space = clamp(input, 0., 1.); // * 2.0;
    
    // Compute reverb parameters from the "space" metaparameter.
    
    //double space = self->space >= 1.0 ? 1.0 : self->space;
    //space = space >= 0.1 ? space - 0.1 : 0.0;
    //self->reverb_amount = space >= 0.5 ? 1.0 * (space - 0.5) : 0.0;
    self->reverb_amount = self->space*0.7;
    self->reverb_time = 0.3 + self->reverb_amount;
    
    bool freeze = self->space >= 0.98;   //1.75;
    if (freeze) {
        self->reverb_->set_time(1.0);
        self->reverb_->set_input_gain(0.0);
        self->reverb_->set_lp(1.0);
    } else {
        self->reverb_->set_time(self->reverb_time);
        self->reverb_->set_input_gain(self->input_gain);    // was: 0.2 - why that?
        self->reverb_->set_lp(self->reverb_lp);
    }
    
    self->reverb_->set_amount(self->reverb_amount);
}


void myObj_freeze(t_myObj *self, long input) {
    if(input != 0) {
        self->reverb_->set_time(1.0);
        self->reverb_->set_input_gain(0.0);
        self->reverb_->set_lp(1.0);
        self->freeze = true;
    } else {
        self->reverb_->set_time(self->reverb_time);
        self->reverb_->set_input_gain(self->input_gain);    // was: 0.2 - why that?
        self->reverb_->set_lp(self->reverb_lp);
        self->freeze = false;
    }
}


void myObj_setLFO(t_myObj *self, double f1, double f2) {
    self->reverb_->set_lfo_1(f1);
    self->reverb_->set_lfo_2(f2);
}

void myObj_info(t_myObj *self)
{
    object_post((t_object*)self, "info - reverb ----------------------->");
    object_post((t_object*)self, "space: %f", self->space);
    object_post((t_object*)self, "reverb time: %f", self->reverb_time);
    object_post((t_object*)self, "reverb amount: %f", self->reverb_amount);
    object_post((t_object*)self, "reverb_diffusion: %f", self->reverb_diffusion);
    object_post((t_object*)self, "reverb_lp: %f", self->reverb_lp);
    object_post((t_object*)self, "reverb_gain: %f", self->input_gain);
    object_post((t_object*)self, "<-----------------------");
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
    
    //double *inL = ins[0];
    //double *inR = ins[1];
    double *outL = outs[0];
    double *outR = outs[1];
    long vs = sampleframes;
    size_t size = (int)vs;
    
    Reverb *reverb_ = self->reverb_;
    
    if (self->x_obj.z_disabled)
        return;
    
    // TODO: can we rely on the fact that input & output vectors are the same?
    //std::copy(&inL[0], &inL[size], &outL[0]);
    //std::copy(&inR[0], &inR[size], &outR[0]);
    
    if(self->bypass)
        return;
    
    // if 'freeze' is on, we want no direct signal
    if(self->freeze) {
        memset(outL, 0, size*sizeof(double));
        memset(outR, 0, size*sizeof(double));
    }
    
    reverb_->Process(outL, outR, size);
    
    SoftLimit_block(outL, size);
    SoftLimit_block(outR, size);

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
    if(self->reverb_) delete self->reverb_;
    if(self->reverb_buffer) sysmem_freeptr(self->reverb_buffer);
    
}




void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) audio inL", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) audio inR", ASSIST_STRING_MAXSIZE);
                break;
            
        }
    }
    else if (io == ASSIST_OUTLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) OUTL", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) OUTR", ASSIST_STRING_MAXSIZE);
                break;
        }
    }
}


void ext_main(void* r) {
    this_class = class_new("vb.mi.rvb~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_dsp64, "dsp64", A_CANT, 0);
    //class_addmethod(this_class, (method)myObj_float, "float", A_FLOAT, 0);
    //class_addmethod(this_class, (method)myObj_int, "int", A_LONG, 0);
    class_addmethod(this_class, (method)myObj_bypass, "bypass", A_LONG, 0);
    class_addmethod(this_class, (method)myObj_space, "space", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_time, "time", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_input_gain, "gain", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_diffusion, "diffusion", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_amount, "amount", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_lp, "lp", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_hp, "hp", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_freeze, "freeze", A_LONG, 0);
    class_addmethod(this_class, (method)myObj_setLFO, "lfo", A_FLOAT, A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_info, "info", 0);
    
    class_addmethod(this_class, (method)myObj_assist, "assist", A_CANT,0);
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.rvb~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "reverb generator of the 'elements' module by mutable instruments");
}
