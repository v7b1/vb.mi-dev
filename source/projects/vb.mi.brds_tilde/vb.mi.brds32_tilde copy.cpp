#include "c74_msp.h"

#include "stmlib/utils/dsp.h"

#include "braids/envelope.h"
#include "braids/macro_oscillator.h"
#include "braids/quantizer.h"
#include "braids/signature_waveshaper.h"
#include "braids/quantizer_scales.h"

#include "Accelerate/Accelerate.h"

const uint32_t kSampleRate = 96000;
const uint16_t kAudioBlockSize = 16;

const uint16_t decimation_factors[] = { 24, 12, 6, 4, 3, 2, 1 };
const uint16_t bit_reduction_masks[] = {
    0xc000,
    0xe000,
    0xf000,
    0xf800,
    0xff00,
    0xfff0,
    0xffff };


using namespace c74::max;

static t_class* this_class = nullptr;


struct t_myObj {
	t_pxobject	obj;
    
    braids::MacroOscillator *osc;
    braids::SignatureWaveshaper *ws;
    int16_t     buffer[kAudioBlockSize];
    uint8_t     sync_buffer[kAudioBlockSize];
    int16_t     timbre, color;
    uint8_t     shape;
    bool        trigger_flag, auto_trig;
    
    // settings
    uint16_t     signature;
    uint8_t     resolution;
    uint8_t     sample_rate;
    
    double      sr;
    long        sigvs;
    short       blockCount;
};


void* myObj_new(void) {
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 6);
        outlet_new(self, "signal");

        self->sr = sys_getsr();
        self->timbre = 0;
        self->color = 0;

        // Init and seed the random parameters and generators with the serial number.
        self->osc = new braids::MacroOscillator;
        //memset(self->part, 0, sizeof(*t_myObj::part));
        self->osc->Init(self->sr);
        self->osc->set_pitch((60 << 7));
        self->osc->set_shape(braids::MACRO_OSC_SHAPE_VOWEL_FOF);
        
        self->ws = new braids::SignatureWaveshaper;
        self->ws->Init(1234);   // TODO: check out GetUniqueId(1)
        
        memset(self->sync_buffer, 0, sizeof(self->sync_buffer));
        
        self->auto_trig = true;
        self->trigger_flag = false;
        
        self->blockCount = 0;
        
    }
    else {
        object_free(self);
        self = NULL;
    }
    
    
	return self;
}


#pragma mark ----- sound shaping pots -----

// bow timbre
void myObj_timbre(t_myObj* self, double m) {
    self->timbre = clamp(m, 0., 1.) * 65535;
}

// blow timbre
void myObj_modulation(t_myObj* self, double m) {
    //self->p->exciter_blow_timbre = clamp(m, 0., 0.9995);
    //self->osc->set
}

// strike timbre
void myObj_color(t_myObj* self, double m) {
    self->color = clamp(m, 0., 1.) * 65535;
}

#pragma mark ----- general pots -----

// contour (env_shape)
void myObj_fine(t_myObj* self, double m) {
    //self->p->exciter_envelope_shape = clamp(m, 0., 0.9995);
}

// bow level
void myObj_coarse(t_myObj* self, double m) {
    //self->p->exciter_bow_level = clamp(m, 0., 0.9995);
}

//blow level
void myObj_fm(t_myObj* self, double m) {
    //self->p->exciter_blow_level = clamp(m, 0., 0.9995);
}

// this directly sets the pitch via a midi note
void myObj_note(t_myObj* self, double n) {
    int pit = n;
    self->osc->set_pitch((pit << 7));
    if(self->auto_trig)
        self->trigger_flag = true;
}

void myObj_bang(t_myObj* self) {
    self->trigger_flag = true;
}

void myObj_shape(t_myObj *self, long n) {
    CONSTRAIN(n, 0, braids::MACRO_OSC_SHAPE_LAST - 1);     // 47
    self->shape = n;
}


void myObj_easter(t_myObj* self, long n) {
    //self->part->set_easter_egg(n != 0);
}

#pragma mark -------- settings ----------

void myObj_set_sampleRate(t_myObj *self, int input) {
    self->sample_rate = clamp(input, 0, 6);
}

void myObj_set_resolution(t_myObj *self, int input) {
    self->resolution = clamp(input, 0, 6);
}

void myObj_signature(t_myObj *self, double input) {
    self->signature = clamp(input, 0.0, 1.0) * 65535.0;
}

#pragma mark -------- DSP Loop ----------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double *in0 = ins[0];       // red input
    double *in1 = ins[1];     // green input
    double *in2 = ins[2];     // green input
    double *in3 = ins[3];     // green input
    double *in4 = ins[4];     // green input
    double *in5 = ins[5];     // green input
    double *outL = outs[0];
    
    long vs = sampleframes;
    size_t size = kAudioBlockSize;
    
    if (self->obj.z_disabled)
        return;
    
    int16_t     *buffer = self->buffer;
    uint8_t     *sync_buffer = self->sync_buffer;
    int16_t     timber, color;
    uint8_t     shape_offset = self->shape;
    uint8_t     shape;

    braids::MacroOscillator *osc = self->osc;
    braids::SignatureWaveshaper *ws = self->ws;
    
    int16_t sample = 0;
    size_t decimation_factor = decimation_factors[self->sample_rate];
    uint16_t bit_mask = bit_reduction_masks[self->resolution];
    //int32_t gain = settings.GetValue(SETTING_AD_VCA) ? ad_value : 65535;
    uint16_t signature = self->signature;
    //signature *= signature * 4095;
    
    
    int kend = vs / size;
    
    for(size_t k = 0; k < kend; ++k) {
        int offset = k * size;
        
        timber = clamp(in3[offset], 0.0, 1.0) * 32756.0;
        color = clamp(in4[offset], 0.0, 1.0) * 32756.0;
        
        shape = (int)(in5[offset] + shape_offset) & 63;
        /*
        if (shape >= braids::MACRO_OSC_SHAPE_LAST_ACCESSIBLE_FROM_META) {
            shape -= braids::MACRO_OSC_SHAPE_LAST_ACCESSIBLE_FROM_META;
        }*/
        if (shape >= braids::MACRO_OSC_SHAPE_LAST)
            shape -= braids::MACRO_OSC_SHAPE_LAST;
        //object_post(NULL, "shape: %d", shape);
        osc->set_shape(static_cast<braids::MacroOscillatorShape>(shape));
        osc->set_parameters(timber, color);
        if(self->trigger_flag) {
            osc->Strike();
            self->trigger_flag = false;
        }
        osc->Render(sync_buffer, buffer,kAudioBlockSize);
        
        for (size_t i = 0; i < size; ++i) {
            int index = i + offset;
            
            if((i % decimation_factor) == 0) {
                sample = buffer[i] & bit_mask;
            }
            int16_t warped = ws->Transform(sample);
            buffer[i] = stmlib::Mix(sample, warped, signature);
            outL[index] = (buffer[i] / 32756.0);
        }
        
    }
    
}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags) {
    /*
    if(maxvectorsize < elements::kMaxBlockSize) {
        object_error((t_object*)self, "vector size can't be smaller than 16 samples, sorry!");
        return;
    }
    */
    //object_post(NULL, "before sr: %f", elements::Dsp::getSr());
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->osc->Init(self->sr);
        //self->osc->set_pitch((60 << 7));
    }
    
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);
    delete self->osc;
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
	if (io == ASSIST_INLET) {
		switch (index) {
			case 0: 
				strncpy(string_dest,"(signal) Phase (0-1)", ASSIST_STRING_MAXSIZE); 
				break;
		}
	}
	else if (io == ASSIST_OUTLET) {
		switch (index) {
            case 0:
                strncpy(string_dest,"(signal) OUT", ASSIST_STRING_MAXSIZE);
                break;
		}
	}
}


void ext_main(void* r) {
	this_class = class_new("vb.mi.brds32~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);

    // timbre pots
    class_addmethod(this_class, (method)myObj_timbre,    "timbre",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_color,     "color",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_modulation,   "modulation", A_FLOAT, 0);
    
    // general pots
    class_addmethod(this_class, (method)myObj_coarse,   "coarse",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_fine,     "fine",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_fm,       "fm",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_note,     "note",     A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_shape,    "shape",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_bang,     "bang",  0);
    //class_addmethod(this_class, (method)myObj_easter,   "easteregg",  A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_set_sampleRate,    "sr",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_set_resolution,    "resolution", A_LONG, 0);
    class_addmethod(this_class, (method)myObj_signature,    "signature",      A_FLOAT, 0);
	
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
}
