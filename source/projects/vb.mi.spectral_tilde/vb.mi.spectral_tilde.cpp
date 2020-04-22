#include "c74_msp.h"

#include "clouds/dsp/granular_processor.h"
#include "clouds/resources.h"
#include "clouds/dsp/audio_buffer.h"
#include "clouds/dsp/mu_law.h"
#include "clouds/dsp/sample_rate_converter.h"

#include <math.h>

#include "Accelerate/Accelerate.h"


// January 2020
// author: volker böhm, https://vboehm.net
//
// explore the phaseVocoder mode of the 'clouds' module

const uint16_t kAudioBlockSize = 32;        // sig vs can't be smaller than this!



using namespace c74::max;

enum ModParams {
    PARAM_PITCH,
    PARAM_POSITION,
    PARAM_SIZE,
    PARAM_DENSITY,
    PARAM_TEXTURE,
    PARAM_DRYWET,
    PARAM_CHANNEL_LAST
};

static t_class* this_class = nullptr;


struct t_myObj {
	t_pxobject	obj;
    
    //clouds::GranularProcessor   *processor;
    clouds::PhaseVocoder        phase_vocoder_;
    clouds::Parameters          parameters_;
    
    clouds::AudioBuffer<clouds::RESOLUTION_8_BIT_MU_LAW> buffer_8_[2];
    clouds::AudioBuffer<clouds::RESOLUTION_16_BIT> buffer_16_[2];
    
    // buffers
    uint8_t     *large_buffer;
    uint8_t     *small_buffer;
    
    // parameters
    float       in_gain;
    bool        freeze;
    bool        trigger, previous_trig;
    bool        gate;
    
    //float       pot_value_[PARAM_CHANNEL_LAST];
    //float       smoothed_value_[PARAM_CHANNEL_LAST];
    //float       coef;      // smoothing coefficient for parameter changes
    
    clouds::FloatFrame  input[kAudioBlockSize];
    clouds::FloatFrame  output[kAudioBlockSize];
    
    double      sr;
    long        sigvs;
    
    bool        gate_connected;
    bool        trig_connected;

    
    clouds::SampleRateConverter<-clouds::kDownsamplingFactor, 45, clouds::src_filter_1x_2_45> src_down_;
    clouds::SampleRateConverter<+clouds::kDownsamplingFactor, 45, clouds::src_filter_1x_2_45> src_up_;
    
};

void* myObj_new(t_symbol *s, long argc, t_atom *argv) {
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 2);
        outlet_new(self, "signal");
        outlet_new(self, "signal");

        self->sr = sys_getsr();
        
        int largeBufSize = 118784;
        int smallBufSize = 65536-128;
        
        self->large_buffer = (uint8_t*)sysmem_newptrclear(largeBufSize * sizeof(uint8_t));
        self->small_buffer = (uint8_t*)sysmem_newptrclear(smallBufSize * sizeof(uint8_t));
        

        
        // init pvoc
        
        int num_channels_ = 2;
        int32_t resolution = 16;
        void* buffer[2];
        size_t buffer_size[2];
        
        buffer_size[0] = buffer_size[1] = smallBufSize;
        buffer[0] = self->large_buffer;
        buffer[1] = self->small_buffer;
        
        self->phase_vocoder_.Init(
                                  buffer, buffer_size,
                                  clouds::lut_sine_window_4096, 4096,
                                  num_channels_, resolution, self->sr);
        
        // init values
        self->parameters_.pitch = 0.f;
        self->parameters_.position = 0.f;
        self->parameters_.size = 0.5f;
        self->parameters_.density = 0.1f;
        self->parameters_.texture = 0.5f;
        
        self->parameters_.dry_wet = 1.f;
        self->parameters_.stereo_spread = 0.5f;
        self->parameters_.reverb = 0.f;
        self->parameters_.feedback = 0.f;
        
        self->in_gain = 1.0f;

        self->src_down_.Init();
        self->src_up_.Init();
        
        // process attributes
        //attr_args_process(self, argc, argv);
        
    }
    else {
        object_free(self);
        self = NULL;
    }
    
	return self;
}


#pragma mark ----- sound parameter pots ------

void myObj_freeze(t_myObj* self, long m) {
    self->parameters_.freeze = m != 0;
}

void myObj_glitch(t_myObj* self, long m) {
    self->parameters_.gate = m != 0;
}


void myObj_position(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->parameters_.position = m;
}

void myObj_size(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->parameters_.size = m;
}

void myObj_pitch(t_myObj* self, double m) {
    m = clamp(m, -48.0, 48.0);
    self->parameters_.pitch = m;
}

void myObj_density(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->parameters_.density = m;
}


void myObj_texture(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->parameters_.texture = m;
}


#pragma mark ---------- blend parameters -------------

void myObj_drywet(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    self->parameters_.dry_wet = m;
}

/*
void myObj_spread(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    //self->stereo_spread = m;
    self->processor->mutable_parameters()->stereo_spread = m;
    //self->pot_value_[POT_SPREAD] = m;
}
void myObj_reverb(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    //self->reverb = m;
    self->processor->mutable_parameters()->reverb = m;
    //self->pot_value_[POT_REVERB] = m;
}

void myObj_feedback(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    //self->feedback = m;
    self->processor->mutable_parameters()->feedback = m;
    //self->pot_value_[POT_FEEDBACK] = m;
}
*/


#pragma mark -------- general pots -----------

void myObj_in_gain(t_myObj* self, double m) {
    m = clamp(m, -18.0, 18.0);
    self->in_gain = pow(10.0, (m/20.0));
}


/*
void myObj_lofi(t_myObj *self, long m) {
    self->processor->set_low_fidelity(m != 0);
}

void myObj_bypass(t_myObj *self, long n) {
    self->processor->set_bypass(n != 0);
}
*/



void myObj_info(t_myObj *self) {
    clouds::Parameters *p = &self->parameters_;
    object_post((t_object*)self, "position: %f", p->position);
    object_post((t_object*)self, "size: %f", p->size);
    object_post((t_object*)self, "pitch: %f", p->pitch);
    object_post((t_object*)self, "density: %f", p->density);
    object_post((t_object*)self, "texture: %f", p->texture);
    object_post((t_object*)self, "drywet: %f", p->dry_wet);
    object_post((t_object*)self, "spread: %f", p->stereo_spread);
    object_post((t_object*)self, "reverb: %f", p->reverb);
    object_post((t_object*)self, "feedback: %f", p->feedback);
    
    object_post((t_object*)self, "freeze: %d", p->freeze);
    object_post((t_object*)self, "trigger: %d", p->trigger);
    object_post((t_object*)self, "gate: %d", p->gate);
    object_post((t_object*)self, "in_gain: %f", self->in_gain);

    object_post(NULL, "---------------------");
}


#pragma mark -------- DSP Loop ----------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double  *inL = ins[0];      // audio in L
    double  *inR = ins[1];      // audio in R
    
    double  *outL = outs[0];
    double  *outR = outs[1];

    long vs = sampleframes;
    
    if (self->obj.z_disabled)
        return;
    
    
    clouds::FloatFrame  *input = self->input;
    clouds::FloatFrame  *output = self->output;

    uint16_t    count = 0;
    float       in_gain = self->in_gain;


    clouds::Parameters   *p = &self->parameters_;
    clouds::PhaseVocoder *pv = &self->phase_vocoder_;
    
    
    for(count = 0; count < vs; count += kAudioBlockSize) {
        
        for(auto i=0; i<kAudioBlockSize; ++i) {
            input[i].l = inL[i + count] * in_gain;
            input[i].r = inR[i + count] * in_gain;
        }
        
        /*
        // combine pot and cv inputs
        // pitch
        value = pot_value[PARAM_PITCH];
        CONSTRAIN(value, -48.0f, 48.0f);
        smoothed_value[PARAM_PITCH] += coef * (value - smoothed_value[PARAM_PITCH]);
        p->pitch = smoothed_value[PARAM_PITCH];
        
        // rest
        for(auto i=1; i<PARAM_CHANNEL_LAST; ++i) {
            value = pot_value[i];
            CONSTRAIN(value, 0.0f, 1.0f);
            smoothed_value[i] += coef * (value - smoothed_value[i]);
        }
        
        p->position = smoothed_value[PARAM_POSITION];
        p->size = smoothed_value[PARAM_SIZE];
        p->density = smoothed_value[PARAM_DENSITY];
        p->texture = smoothed_value[PARAM_TEXTURE];
        p->dry_wet = smoothed_value[PARAM_DRYWET];
        
        p->freeze = self->freeze;
         */
        
        p->spectral.quantization = p->texture;
        p->spectral.refresh_rate = 0.01f + 0.99f * p->density;
        float warp = p->size - 0.5f;
        p->spectral.warp = 4.0f * warp * warp * warp + 0.5f;
        
        float randomization = p->density - 0.5f;
        randomization *= randomization * 4.2f;
        randomization -= 0.05f;
        CONSTRAIN(randomization, 0.0f, 1.0f);
        p->spectral.phase_randomization = randomization;
        pv->Process(*p, input, output, kAudioBlockSize);
        pv->Buffer();
        
        
        for(auto i=0; i<kAudioBlockSize; ++i) {
            outL[i + count] = output[i].l;
            outR[i + count] = output[i].r;
        }
    }
    
}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    
    if(maxvectorsize < kAudioBlockSize) {
        object_error((t_object*)self, "vector size can't be smaller than %d samples, sorry!", kAudioBlockSize);
        return;
    }
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        //self->processor->set_sample_rate(samplerate);
    }
    
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    
}



void myObj_free(t_myObj* self)
{
    dsp_free((t_pxobject*)self);
    
    if(self->large_buffer)
        sysmem_freeptr(self->large_buffer);
    
    if(self->small_buffer)
        sysmem_freeptr(self->small_buffer);
    
    
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
	if (io == ASSIST_INLET) {
		switch (index) {
            case 0:
                strncpy(string_dest,"(signal) IN L", ASSIST_STRING_MAXSIZE); break;
            case 1:
                strncpy(string_dest,"(signal) IN R", ASSIST_STRING_MAXSIZE); break;
			case 2:
                strncpy(string_dest,"(signal) V/OCT", ASSIST_STRING_MAXSIZE); break;
            case 3:
                strncpy(string_dest,"(signal) POSITION", ASSIST_STRING_MAXSIZE); break;
            case 4:
                strncpy(string_dest,"(signal) SIZE", ASSIST_STRING_MAXSIZE); break;
            case 5:
                strncpy(string_dest,"(signal) DENSITY", ASSIST_STRING_MAXSIZE); break;
            case 6:
                strncpy(string_dest,"(signal) TEXTURE", ASSIST_STRING_MAXSIZE); break;
            case 7:
                strncpy(string_dest,"(signal) DRY_WET", ASSIST_STRING_MAXSIZE); break;
            case 8:
                strncpy(string_dest,"(signal) FREEZE", ASSIST_STRING_MAXSIZE); break;
            case 9:
                strncpy(string_dest,"(signal) TRIGGER", ASSIST_STRING_MAXSIZE); break;
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
	this_class = class_new("vb.mi.spectral~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_DEFLONG, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);

    //class_addmethod(this_class, (method)myObj_bang,    "bang", 0);
    class_addmethod(this_class, (method)myObj_position,    "position", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_size,     "size",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_pitch,   "pitch", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_in_gain,   "in_gain",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_density,     "density",  A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_texture,  "texture",     A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_drywet,    "drywet",      A_FLOAT, 0);
    //class_addmethod(this_class, (method)myObj_spread,    "spread",      A_FLOAT, 0);
    //class_addmethod(this_class, (method)myObj_reverb,    "reverb",      A_FLOAT, 0);
    //class_addmethod(this_class, (method)myObj_feedback,    "feedback",  A_FLOAT, 0);
    //class_addmethod(this_class, (method)myObj_mode,     "mode",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_freeze,   "freeze",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_glitch,   "glitch",  A_LONG, 0);
    //class_addmethod(this_class, (method)myObj_lofi,   "lofi",  A_LONG, 0);
    //class_addmethod(this_class, (method)myObj_bypass,   "bypass",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_info,   "info", 0);

	
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
    
    /*
    // attributes ====
    CLASS_ATTR_CHAR(this_class,"drift", 0, t_myObj, drift);
    CLASS_ATTR_SAVE(this_class, "drift", 0);
    CLASS_ATTR_FILTER_CLIP(this_class, "drift", 0, 15);
    */
    
    object_post(NULL, "vb.mi.spectral~ by volker boehm -- https://vboehm.net");
    object_post(NULL, "based on mutable instruments' 'clouds' module");
}
