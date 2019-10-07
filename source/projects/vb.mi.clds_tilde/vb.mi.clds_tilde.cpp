#include "c74_msp.h"

//#include "stmlib/utils/dsp.h"

#include "clouds/dsp/granular_processor.h"
#include "clouds/resources.h"

//#include "Accelerate/Accelerate.h"


//#define    BUFFER_LEN            1024
#define     SAMPLERATE          96000.0
#define     BLOCK_SIZE          16
#define     SAMP_SCALE          (float)(1.0 / 32767.0)

//const uint32_t kSampleRate = 96000;
const uint16_t kAudioBlockSize = 32;

using namespace c74::max;

static t_class* this_class = nullptr;


struct t_myObj {
	t_pxobject	obj;
    
    clouds::GranularProcessor   *processor;
    
    // buffers
    uint8_t     *large_buffer;
    uint8_t     *small_buffer;
    
    // parameters
    float       position;
    float       size;
    float       pitch;
    float       density;
    float       texture;
    float   dry_wet;
    float   stereo_spread;
    float   feedback;
    float   reverb;
    
    bool        freeze;
    bool        trigger;
    bool        gate;
    
    uint8_t     mode;
    
    clouds::ShortFrame  input[kAudioBlockSize];
    clouds::ShortFrame  output[kAudioBlockSize];
    
    double          sr;
    long            sigvs;

    
};

void* myObj_new(t_symbol *s, long argc, t_atom *argv) {
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 2);
        outlet_new(self, "signal");
        outlet_new(self, "signal");

        self->sr = sys_getsr();
        
        self->large_buffer = (uint8_t*)sysmem_newptr(118784 * sizeof(uint8_t));
        self->small_buffer = (uint8_t*)sysmem_newptr((65536-128) * sizeof(uint8_t));
        
        self->processor = new clouds::GranularProcessor;
        memset(self->processor, 0, sizeof(*t_myObj::processor));
        self->processor->Init(self->large_buffer, 118784, self->small_buffer, (65536-128));
        self->processor->set_num_channels(2);
        self->processor->set_low_fidelity(false);
        self->processor->set_playback_mode(clouds::PLAYBACK_MODE_LOOPING_DELAY);
        

        /*
        self->midi_pitch = 69 << 7;
        self->root = 0;
        self->timbre = 0;
        self->color = 0;
        self->timbre_pot = 0.0;
        self->color_pot = 0.0;
        //self->modulation = 0.0;
        self->drift = 0;
        self->auto_trig = true;
        self->trigger_flag = false;
        self->resamp = true;
        self->last_in = 0.0;
        */
        
        // process attributes
        attr_args_process(self, argc, argv);
        
    }
    else {
        object_free(self);
        self = NULL;
    }
    
	return self;
}


#pragma mark ----- sound parameter pots -----

void myObj_freeze(t_myObj* self, long m) {

    self->freeze = m != 0;
    self->processor->mutable_parameters()->freeze = m != 0;
}

void myObj_position(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->position = m;
    self->processor->mutable_parameters()->position = m;
}

void myObj_size(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->size = m;
    self->processor->mutable_parameters()->size = m;
}

void myObj_pitch(t_myObj* self, double m) {
    //m = clamp(m, 0., 1.);
    self->pitch = m;
    self->processor->mutable_parameters()->pitch = m;
}



#pragma mark ----- general pots -----

void myObj_in_gain(t_myObj* self, double m) {
    
}


void myObj_density(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->density = m;
    self->processor->mutable_parameters()->density = m;
}


void myObj_texture(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->texture = m;
    self->processor->mutable_parameters()->texture = m;
}


#pragma mark ---------- blend modes -------------

void myObj_feedback(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    self->feedback = m;
    self->processor->mutable_parameters()->feedback = m;
}

void myObj_reverb(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    self->reverb = m;
    self->processor->mutable_parameters()->reverb = m;
}

void myObj_spread(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    self->stereo_spread = m;
    self->processor->mutable_parameters()->stereo_spread = m;
}

void myObj_drywet(t_myObj *self, double m) {
    m = clamp(m, 0., 1.);
    self->dry_wet = m;
    self->processor->mutable_parameters()->dry_wet = m;
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
    self->mode = n;
    self->processor->set_playback_mode(static_cast<clouds::PlaybackMode>(n));
}

void myObj_lofi(t_myObj *self, long m) {
    self->processor->set_low_fidelity(m != 0);
}

void myObj_bypass(t_myObj *self, long n) {
    //self->mode = n != 0;
    self->processor->set_bypass(n != 0);
}


/*
#pragma mark -------- settings ----------

void myObj_set_sampleRate(t_myObj *self, int input) {
    uint8_t sample_rate = clamp(input, 0, 6);
    self->pd.decimation_factor = decimation_factors[sample_rate];
}

void myObj_set_resolution(t_myObj *self, int input) {
    uint8_t resolution = clamp(input, 0, 6);
    self->pd.bit_mask = bit_reduction_masks[resolution];
}

void myObj_signature(t_myObj *self, int input) {
    self->pd.signature = clamp(input, 0, 4) * 4095;
}

*/



#pragma mark -------- DSP Loop ----------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double  *inL = ins[0];     // V/OCT
    double  *inR = ins[1];    // timber CV
    double  *outL = outs[0];
    double  *outR = outs[1];

    long vs = sampleframes;
    
    clouds::ShortFrame  *input = self->input;
    clouds::ShortFrame  *output = self->output;
    uint16_t    count = 0;
    /*
    float       *samples, *output;
    int16_t     timbre, color, count;
    double      ratio, timbre_pot, color_pot;
    uint8_t     shape_offset, shape, root, drift;
    int32_t     midi_pitch, pitch = 0;
    bool        trig_connected, trigger_flag;
    */
    if (self->obj.z_disabled)
        return;
    /*
    ratio = self->ratio;
    timbre_pot = self->timbre_pot;
    color_pot = self->color_pot;
    //double modulation = self->modulation;
    shape_offset = self->shape;
    midi_pitch = self->midi_pitch;
    root = self->root;
    drift = self->drift;
    samples = self->samples;
    trig_connected = self->trig_connected;
    trigger_flag = self->trigger_flag;
    */
    clouds::GranularProcessor   *gp = self->processor;
    //clouds::Parameters   *p = gp->mutable_parameters();
    
    for(count = 0; count < vs; count += kAudioBlockSize) {
        
        for(auto i=0; i<kAudioBlockSize; ++i) {
            input[i].l = inL[i + count] * 32767.0;
            input[i].r = inR[i + count] * 32767.0;
        }
        
        gp->Process(input, output, kAudioBlockSize);
        gp->Prepare();
        
        for(auto i=0; i<kAudioBlockSize; ++i) {
            outL[i + count] = output[i].l / 32767.0;
            outR[i + count] = output[i].r / 32767.0;
        }
    }
    
    
}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    /*
    // is a signal connected to the trigger input?
    self->trig_connected = count[0];
    
    if(maxvectorsize < kAudioBlockSize) {
        object_error((t_object*)self, "vector size can't be smaller than %d samples, sorry!", kAudioBlockSize);
        return;
    }
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        // ratio: output_sample_rate / input_sample_rate.
        self->ratio = self->sr / SAMPLERATE;
        
    }
    */

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
                strncpy(string_dest,"(signal) V/OCT", ASSIST_STRING_MAXSIZE); break;
            case 1:
                strncpy(string_dest,"(signal) Timbre", ASSIST_STRING_MAXSIZE); break;
            case 2:
                strncpy(string_dest,"(signal) Color", ASSIST_STRING_MAXSIZE); break;
            case 3:
                strncpy(string_dest,"(signal) Model", ASSIST_STRING_MAXSIZE); break;
            case 4:
                strncpy(string_dest,"(signal/bang) tigger input", ASSIST_STRING_MAXSIZE); break;
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
	this_class = class_new("vb.mi.clds~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);

    // timbre pots
    class_addmethod(this_class, (method)myObj_position,    "position", A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_size,     "size",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_pitch,   "pitch", A_FLOAT, 0);
    
    // general pots
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
	
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
    
    /*
    // attributes ====
    CLASS_ATTR_CHAR(this_class,"drift", 0, t_myObj, drift);
    CLASS_ATTR_SAVE(this_class, "drift", 0);
    CLASS_ATTR_FILTER_CLIP(this_class, "drift", 0, 15);
    
    CLASS_ATTR_CHAR(this_class,"auto_trig", 0, t_myObj, auto_trig);
    CLASS_ATTR_SAVE(this_class, "auto_trig", 0);
    CLASS_ATTR_STYLE(this_class, "auto_trig", 0, "onoff");
    //CLASS_ATTR_FILTER_CLIP(this_class, "auto_trig", 0, 1);
    
    CLASS_ATTR_CHAR(this_class,"root", 0, t_myObj, root);
    CLASS_ATTR_SAVE(this_class, "root", 0);
    CLASS_ATTR_FILTER_CLIP(this_class, "root", 0, 11);
    
    CLASS_ATTR_CHAR(this_class,"resamp", 0, t_myObj, resamp);
    CLASS_ATTR_SAVE(this_class, "resamp", 0);
    CLASS_ATTR_STYLE(this_class, "resamp", 0, "onoff");
    */
    
    object_post(NULL, "vb.mi.clds~ by volker boehm -- https://vboehm.net");
    object_post(NULL, "based on mutable instruments' 'clouds' module");
}
