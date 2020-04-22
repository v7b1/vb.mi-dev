#include "c74_msp.h"

//#include "reverb.h"

//#include "Accelerate/Accelerate.h"


using namespace c74::max;


static t_class* this_class = nullptr;


struct t_myObj {
    t_pxobject  x_obj;
    double      gain;
    bool        bypass;
    
};



void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 1);
        outlet_new(self, "signal");

        self->gain = 1.0;
        self->bypass = false;
        
        
        
        //attr_args_process(x, argc, argv);            // process attributes
    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}



// TODO: implement bypass
void myObj_bypass(t_myObj *self, long input) {
    self->bypass = ( input != 0);
}




inline unsigned char Lin2MuLaw(int16_t pcm_val) {
    int16_t mask;
    int16_t seg;
    uint8_t uval;
    pcm_val = pcm_val >> 2;
    if (pcm_val < 0) {
        pcm_val = -pcm_val;
        mask = 0x7f;
    } else {
        mask = 0xff;
    }
    if (pcm_val > 8159) pcm_val = 8159;
    pcm_val += (0x84 >> 2);
    
    if (pcm_val <= 0x3f) seg = 0;
    else if (pcm_val <= 0x7f) seg = 1;
    else if (pcm_val <= 0xff) seg = 2;
    else if (pcm_val <= 0x1ff) seg = 3;
    else if (pcm_val <= 0x3ff) seg = 4;
    else if (pcm_val <= 0x7ff) seg = 5;
    else if (pcm_val <= 0xfff) seg = 6;
    else if (pcm_val <= 0x1fff) seg = 7;
    else seg = 8;
    if (seg >= 8)
        return static_cast<uint8_t>(0x7f ^ mask);
    else {
        uval = static_cast<uint8_t>((seg << 4) | ((pcm_val >> (seg + 1)) & 0x0f));
        return (uval ^ mask);
    }
}

inline short MuLaw2Lin(uint8_t u_val) {
    int16_t t;
    u_val = ~u_val;
    t = ((u_val & 0xf) << 3) + 0x84;
    t <<= ((unsigned)u_val & 0x70) >> 4;
    return ((u_val & 0x80) ? (0x84 - t) : (t - 0x84));
}


inline double SoftLimit(double x) {
    return x * (27.0 + x * x) / (27.0 + 9.0 * x * x);
}

inline double SoftClip(double x) {
    if (x < -3.0) {
        return -1.0;
    } else if (x > 3.0) {
        return 1.0;
    } else {
        return SoftLimit(x);
    }
}

/*
inline void SoftLimit_block(double *inout, size_t size) {
    while(size--) {
        double x2 = (*inout)*(*inout);
        *inout *= (27.0 + x2) / (27.0 + 9.0 * x2);
        inout++;
    }
}*/


#pragma mark ----- dsp loop -----

// 64 bit signal input version
void myObj_perform64(t_myObj *self, t_object *dsp64, double **ins, long numins,
                     double **outs, long numouts, long sampleframes, long flags, void *userparam){
    
    double *in = ins[0];
    double *out = outs[0];
    double gain = self->gain;
    
    long vs = sampleframes;
    
    if (self->x_obj.z_disabled)
        return;

    
    if(self->bypass)
        return;
    
    
    for (size_t i = 0; i < vs; ++i) {
        double input = SoftClip(in[i] * gain);
        int16_t pcm_in = input * 32767.0;
        uint8_t compressed = Lin2MuLaw(pcm_in);
        
        int16_t pcm_out = MuLaw2Lin(compressed);
        out[i] = pcm_out / 32767.0;
        
    }

}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
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
    this_class = class_new("vb.mi.mu~", (method)myObj_new, (method)dsp_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(this_class, (method)myObj_bypass, "bypass", A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_assist, "assist", A_CANT,0);
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    // attributes ====
    CLASS_ATTR_DOUBLE(this_class,"gain", 0, t_myObj, gain);
    CLASS_ATTR_SAVE(this_class, "gain", 0);

    
    object_post(NULL, "vb.mi.mu~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "mµ law compression from 'clouds' module by mutable instruments");
}
