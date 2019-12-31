#include "c74_msp.h"

#include "clouds/dsp/granular_processor.h"
#include "clouds/resources.h"

#include "Accelerate/Accelerate.h"

// original sample rate is 32 kHz

// Dezember 2019
// author: volker böhm, https://vboehm.net
//
// TODO: save and load audio data to buffer, make size of internal memory settable, make lofi bit depth settable

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
    
    clouds::GranularProcessor   *processor;
    
    // buffers
    uint8_t     *large_buffer;
    uint8_t     *small_buffer;
    
    // parameters
    float       in_gain;
    bool        freeze;
    bool        trigger, previous_trig;
    bool        gate;
    
    float       pot_value_[PARAM_CHANNEL_LAST];
    float       smoothed_value_[PARAM_CHANNEL_LAST];
    float       coef;      // smoothing coefficient for parameter changes
    
    clouds::FloatFrame  input[kAudioBlockSize];
    clouds::FloatFrame  output[kAudioBlockSize];
    
    double      sr;
    long        sigvs;
    
    bool        gate_connected;
    bool        trig_connected;
    
};

//void* myObj_new(t_symbol *s, long argc, t_atom *argv) {
void* myObj_new(long size_in_ms) {
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 10);
        outlet_new(self, "signal");
        outlet_new(self, "signal");

        self->sr = sys_getsr();
        
        // check channel arg
        /*
        if(chns <= 0)
            chns = 2;
        else if(chns >= 2)
            chns = 2;
        else
            chns = 1;
        */
        
        int largeBufSize = 118784;
        int smallBufSize = 65536-128;
        
        // check size arg
        if(size_in_ms > 1000) {
            if(size_in_ms > 4000) size_in_ms = 4000;
            smallBufSize = size_in_ms * self->sr * 0.002 + 128;
            largeBufSize = smallBufSize + 65536;
        }
        self->large_buffer = (uint8_t*)sysmem_newptrclear(largeBufSize * sizeof(uint8_t));
        self->small_buffer = (uint8_t*)sysmem_newptrclear(smallBufSize * sizeof(uint8_t));
        
        self->processor = new clouds::GranularProcessor;
        memset(self->processor, 0, sizeof(*t_myObj::processor));
        self->processor->Init(self->large_buffer, largeBufSize, self->small_buffer, smallBufSize);
        self->processor->set_sample_rate(self->sr);
        self->processor->set_num_channels(2);       // always use stereo setup (?)
        self->processor->set_low_fidelity(false);
        self->processor->set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
        
        // init values
        self->pot_value_[PARAM_PITCH] = self->smoothed_value_[PARAM_PITCH] = 0.f;
        self->pot_value_[PARAM_POSITION] = self->smoothed_value_[PARAM_POSITION] = 0.f;
        self->pot_value_[PARAM_SIZE] = self->smoothed_value_[PARAM_SIZE] = 0.5f;
        self->pot_value_[PARAM_DENSITY] = self->smoothed_value_[PARAM_DENSITY] = 0.1f;
        self->pot_value_[PARAM_TEXTURE] = self->smoothed_value_[PARAM_TEXTURE] = 0.5f;
        
        self->pot_value_[PARAM_DRYWET] = self->smoothed_value_[PARAM_DRYWET] = 1.f;
        self->processor->mutable_parameters()->stereo_spread = 0.5f;
        self->processor->mutable_parameters()->reverb = 0.f;
        self->processor->mutable_parameters()->feedback = 0.f;
        
        self->in_gain = 1.0f;
        self->coef = 0.1f;
        self->previous_trig = false;

        
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
    self->freeze = m != 0;
}

void myObj_bang(t_myObj* self) {
    self->processor->mutable_parameters()->trigger = true;
}

void myObj_position(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    //self->position = m;
    //self->processor->mutable_parameters()->position = m;
    self->pot_value_[PARAM_POSITION] = m;
}

void myObj_size(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    //self->size = m;
    //self->processor->mutable_parameters()->size = m;
    self->pot_value_[PARAM_SIZE] = m;
}

void myObj_pitch(t_myObj* self, double m) {
    //m = clamp(m, 0., 1.);
    //self->pitch = m;
    //self->processor->mutable_parameters()->pitch = m;  // ???
    self->pot_value_[PARAM_PITCH] = m;
}

void myObj_density(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    //self->density = m;
    //self->processor->mutable_parameters()->density = m;
    self->pot_value_[PARAM_DENSITY] = m;
}


void myObj_texture(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    //self->texture = m;
    self->pot_value_[PARAM_TEXTURE] = m;
}


#pragma mark ---------- blend parameters -------------

void myObj_drywet(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    //self->dry_wet = m;
    //self->processor->mutable_parameters()->dry_wet = m;
    self->pot_value_[PARAM_DRYWET] = m;
}

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


#pragma mark ----- general pots -----

void myObj_in_gain(t_myObj* self, double m) {
    m = clamp(m, -18.0, 18.0);
    self->in_gain = pow(10.0, (m/20.0));
}

/*
 PlaybackModes:
 PLAYBACK_MODE_GRANULAR,
 PLAYBACK_MODE_STRETCH,
 PLAYBACK_MODE_LOOPING_DELAY,
 PLAYBACK_MODE_SPECTRAL,
 PLAYBACK_MODE_LAST
 */
void myObj_mode(t_myObj *self, long n) {
    CONSTRAIN(n, 0, clouds::PLAYBACK_MODE_LAST - 1);
    //self->mode = n;
    self->processor->set_playback_mode(static_cast<clouds::PlaybackMode>(n));
}

void myObj_lofi(t_myObj *self, long m) {
    self->processor->set_low_fidelity(m != 0);
}

void myObj_bypass(t_myObj *self, long n) {
    self->processor->set_bypass(n != 0);
}


void myObj_coef(t_myObj *self, double m) {
    m = 1.f - clamp(m, 0., 1.0) * 0.9;
    self->coef = m*m*m*m;
}

void myObj_info(t_myObj *self) {
    clouds::Parameters *p = self->processor->mutable_parameters();
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
    object_post((t_object*)self, "coef: %f", self->coef);
    object_post(NULL, "---------------------");
}


#pragma mark -------- DSP Loop ----------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double  *inL = ins[0];      // audio in L
    double  *inR = ins[1];      // audio in R
    double  *gate_in = ins[8];  // freeze input (inlet 8)
    double  *trig_in = ins[9];  // trigger input (inlet 9)
    
    double  *outL = outs[0];
    double  *outR = outs[1];

    long vs = sampleframes;
    
    if (self->obj.z_disabled)
        return;
    
    
    clouds::FloatFrame  *input = self->input;
    clouds::FloatFrame  *output = self->output;
    float       *pot_value = self->pot_value_;
    float       *smoothed_value = self->smoothed_value_;
    uint16_t    count = 0;
    float       value;
    float       coef = self->coef;
    float       in_gain = self->in_gain;

    bool        gate_connected = self->gate_connected;
    bool        trig_connected = self->trig_connected;
    
    clouds::GranularProcessor   *gp = self->processor;
    clouds::Parameters   *p = gp->mutable_parameters();
    
    
    for(count = 0; count < vs; count += kAudioBlockSize) {
        
        for(auto i=0; i<kAudioBlockSize; ++i) {
            input[i].l = inL[i + count] * in_gain;
            input[i].r = inR[i + count] * in_gain;
        }
        
        // combine pot and cv inputs
        // pitch
        value = pot_value[PARAM_PITCH] + ins[2][count];
        CONSTRAIN(value, -48.0f, 48.0f);
        smoothed_value[PARAM_PITCH] += coef * (value - smoothed_value[PARAM_PITCH]);
        p->pitch = smoothed_value[PARAM_PITCH];
        
        // rest
        for(auto i=1; i<PARAM_CHANNEL_LAST; ++i) {
            value = pot_value[i] + ins[i+2][count];
            CONSTRAIN(value, 0.0f, 1.0f);
            smoothed_value[i] += coef * (value - smoothed_value[i]);
        }
        
        p->position = smoothed_value[PARAM_POSITION];
        p->size = smoothed_value[PARAM_SIZE];
        p->density = smoothed_value[PARAM_DENSITY];
        p->texture = smoothed_value[PARAM_TEXTURE];
        p->dry_wet = smoothed_value[PARAM_DRYWET];
        
        // gate & trigger
        if(gate_connected) {
            double gate_sum = 0.0;
            vDSP_sveD(gate_in+count, 1, &gate_sum, kAudioBlockSize);
            p->freeze = (gate_sum != 0.0) || self->freeze;
        }
        else {
            p->freeze = self->freeze;
        }
        
        if(trig_connected) {
            double trig_sum = 0.0;
            vDSP_sveD(trig_in+count, 1, &trig_sum, kAudioBlockSize);
            bool trigger = trig_sum != 0.0;
            p->trigger = (trigger && !self->previous_trig);
            self->previous_trig = trigger;
        }
        
        
        gp->Process(input, output, kAudioBlockSize);
        gp->Prepare();      // muss immer hier sein?
        
        if(p->trigger)
            p->trigger = false;
        
        for(auto i=0; i<kAudioBlockSize; ++i) {
            outL[i + count] = output[i].l;
            outR[i + count] = output[i].r;
        }
    }
    
}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    
    // is a signal connected to the trigger input?
    self->gate_connected = count[8];
    self->trig_connected = count[9];
    
    if(maxvectorsize < kAudioBlockSize) {
        object_error((t_object*)self, "vector size can't be smaller than %d samples, sorry!", kAudioBlockSize);
        return;
    }
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->processor->set_sample_rate(samplerate);
    }
    
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    
}



void myObj_free(t_myObj* self)
{
    dsp_free((t_pxobject*)self);
    delete self->processor;
    
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
	this_class = class_new("vb.mi.clds~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_DEFLONG, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);

    class_addmethod(this_class, (method)myObj_bang,    "bang", 0);
    class_addmethod(this_class, (method)myObj_position,    "position", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_size,     "size",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_pitch,   "pitch", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_in_gain,   "in_gain",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_density,     "density",  A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_texture,  "texture",     A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_drywet,    "drywet",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_spread,    "spread",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_reverb,    "reverb",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_feedback,    "feedback",  A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_mode,     "mode",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_freeze,   "freeze",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_lofi,   "lofi",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_bypass,   "bypass",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_info,   "info", 0);
    class_addmethod(this_class, (method)myObj_coef,    "smooth",  A_FLOAT, 0);
	
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
    
    /*
    // attributes ====
    CLASS_ATTR_CHAR(this_class,"drift", 0, t_myObj, drift);
    CLASS_ATTR_SAVE(this_class, "drift", 0);
    CLASS_ATTR_FILTER_CLIP(this_class, "drift", 0, 15);
    */
    
    object_post(NULL, "vb.mi.clds~ by volker boehm -- https://vboehm.net");
    object_post(NULL, "based on mutable instruments' 'clouds' module");
}
