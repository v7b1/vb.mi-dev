#include "c74_msp.h"

#include "elements/dsp/dsp.h"
#include "elements/dsp/part.h"

#include "Accelerate/Accelerate.h"

float elements::Dsp::kSampleRate = 32000.0f; //44100.0f; //48000.0f;
float elements::Dsp::kSrFactor = 32000.0f / kSampleRate;
float elements::Dsp::kIntervalCorrection = logf(kSrFactor)/logf(2.0f)*12.0f;

using namespace c74::max;

static t_class* this_class = nullptr;

struct t_myObj {
	t_pxobject	obj;
    
    elements::Part              *part;
    elements::PerformanceState  ps;
    elements::Patch             *p;

    float               blow_in[elements::kMaxBlockSize];
    float               strike_in[elements::kMaxBlockSize];
    
    uint16_t            *reverb_buffer;
    void                *info_out;
    float              *out, *aux;     // output buffers
    double              sr;
    long                sigvs;
    
    short               blockCount;
};


void* myObj_new(void) {
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 2);
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        
        //self->strike_in_level = 0.0;
        //self->blow_in_level = 0.0;
        
        // allocate memory
        self->reverb_buffer = (t_uint16*)sysmem_newptrclear(32768*sizeof(t_uint16)); // 65536
        
        if(self->reverb_buffer == NULL) {
            object_post((t_object*)self, "mem alloc failed!");
            object_free(self);
            self = NULL;
            return self;
        }
        
        self->out = (float *)sysmem_newptrclear(4096*sizeof(float));
        self->aux = (float *)sysmem_newptrclear(4096*sizeof(float));
        
        // Init and seed the random parameters and generators with the serial number.
        self->part = new elements::Part;
        memset(self->part, 0, sizeof(*t_myObj::part));
        self->part->Init(self->reverb_buffer);
        //self->part->Seed((uint32_t*)(0x1fff7a10), 3);
        uint32_t mySeed = 0x1fff7a10;
        self->part->Seed(&mySeed, 3);
        
        self->part->set_easter_egg(false);
        self->p = self->part->mutable_patch();
        
        self->blockCount = 0;
        
    }
    else {
        object_free(self);
        self = NULL;
    }
    
    
	return self;
}


#pragma mark ----- exciter pots -----

// contour (env_shape)
void myObj_contour(t_myObj* self, double m) {
    self->p->exciter_envelope_shape = clamp(m, 0., 0.9995);
}

// bow level
void myObj_bow(t_myObj* self, double m) {
    self->p->exciter_bow_level = clamp(m, 0., 0.9995);
}

//blow level
void myObj_blow(t_myObj* self, double m) {
    self->p->exciter_blow_level = clamp(m, 0., 0.9995);
}

// strike level
void myObj_strike(t_myObj* self, double m) {
    self->p->exciter_strike_level = clamp(m, 0., 0.9995);
}

// bigger exciter pots ---------------------------
// blow meta
void myObj_flow(t_myObj* self, double m) {
    self->p->exciter_blow_meta = clamp(m, 0., 0.9995);
}

// strike meta
void myObj_mallet(t_myObj* self, double m) {
    self->p->exciter_strike_meta = clamp(m, 0., 0.9995);
}


// bow timbre
void myObj_bow_timbre(t_myObj* self, double m) {
    self->p->exciter_bow_timbre = clamp(m, 0., 0.9995);
}

// blow timbre
void myObj_blow_timbre(t_myObj* self, double m) {
    self->p->exciter_blow_timbre = clamp(m, 0., 0.9995);
}

// strike timbre
void myObj_strike_timbre(t_myObj* self, double m) {
    self->p->exciter_strike_timbre = clamp(m, 0., 0.9995);
}

#pragma mark ----- resonator pots -----

// geometry
void myObj_geometry(t_myObj* self, double m) {
    self->p->resonator_geometry = clamp(m, 0., 0.9995);
}

void myObj_brightness(t_myObj* self, double m) {
    self->p->resonator_brightness = clamp(m, 0., 0.9995);
}

void myObj_damping(t_myObj* self, double m) {
    self->p->resonator_damping = clamp(m, 0., 0.9995);
}

void myObj_position(t_myObj* self, double m) {
    self->p->resonator_position = clamp(m, 0., 0.9995);
}

void myObj_space(t_myObj* self, double m) {
    self->p->space = clamp(m, 0., 1.) * 2.0;
}


// coarse freq --
void myObj_coarse(t_myObj* self, double m) {
    //m = clamp(m, 0., 1.);
    //self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_COARSE, m*60.0 + 39.0);//120.0);
    // TODO: need correct scaling
    self->ps.note = clamp(m, 0., 1.) * 60.0 + 39.0;
}

// fine freq
void myObj_fine(t_myObj* self, double m) {
    //m = clamp(m, -1., 1.);
    //self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_FINE, m);
}

// fm (big pot, but acts as attenuverter)
void myObj_fm(t_myObj* self, double m) {
    //m = clamp(m, -1., 1.);
    //self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_FM_ATTENUVERTER, m);
}

void myObj_strength(t_myObj *self, double m) {
    self->ps.strength = clamp(m, 0., 1.);
}


void myObj_button(t_myObj *self, long b) {
    //self->uigate = b != 0;
    self->ps.gate = b != 0;
}

// this directly sets the pitch via a midi note
void myObj_note(t_myObj* self, double n) {
    self->ps.note = n;
    //self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_COARSE, n); // write midi pitch directly into COARSE (set 'law' to 'linear'!)
}


/*
 enum ResonatorModel {
 RESONATOR_MODEL_MODAL,
 RESONATOR_MODEL_STRING,
 RESONATOR_MODEL_STRINGS,
 };*/

void myObj_model(t_myObj *self, long n) {
    CONSTRAIN(n, 0, 2);
    self->part->set_resonator_model(static_cast<elements::ResonatorModel>(n));

    /*
     TODO: have a look at 'easter_egg' mode (in ui.cc)
     self->part_->set_easter_egg(!part_->easter_egg());
     self->read_inputs->set_boot_in_easter_egg_mode(part_->easter_egg());
     */
}


void myObj_bypass(t_myObj* self, long n) {
    self->part->set_bypass(n != 0);
}

void myObj_easter(t_myObj* self, long n) {
    self->part->set_easter_egg(n != 0);
}



void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double *in0 = ins[0];       // red input
    double *in1 = ins[1];     // green input
    double *outL = outs[0];
    double *outR = outs[1];
    
    long vs = sampleframes;
    size_t size = elements::kMaxBlockSize;
    
    if (self->obj.z_disabled)
        return;
    
    float   *blow_in = self->blow_in;
    float   *strike_in = self->strike_in;
    float  *out = self->out;
    float  *aux = self->aux;

    elements::PerformanceState ps = self->ps;
    
    
    int kend = vs / size;
    
    for(size_t k = 0; k < kend; ++k) {
        int offset = k * size;
        
        for (size_t i = 0; i < size; ++i) {
            int index = i + offset;
            blow_in[i] = in0[index];
            strike_in[i] = in1[index];
        }
        
        self->part->Process(ps, blow_in, strike_in, out, aux, size);
        
        for (size_t i = 0; i < size; ++i) {
            int index = i + offset;
            outL[index] = stmlib::SoftLimit(out[i]*0.5);        //out[i];   //
            outR[index] = stmlib::SoftLimit(aux[i]*0.5);        //aux[i];   //
        }
        
    }
    
}


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags) {
    
    if(maxvectorsize < elements::kMaxBlockSize) {
        object_error((t_object*)self, "vector size can't be smaller than 16 samples, sorry!");
        return;
    }
    
    object_post(NULL, "before sr: %f", elements::Dsp::getSr());
    if(samplerate != elements::Dsp::getSr()) {
        elements::Dsp::setSr(samplerate);
        self->sr = samplerate;
        
        self->part->Init(self->reverb_buffer);
    }
    object_post(NULL, "after sr: %f", elements::Dsp::getSr());
    object_post(NULL, "srFactor: %f", elements::Dsp::getSrFactor());
    
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);
    
    delete self->part;
    
    if(self->reverb_buffer)
        sysmem_freeptr(self->reverb_buffer);
    /*
    if(self->strike_in)
        sysmem_freeptr(self->strike_in);
    if(self->blow_in)
        sysmem_freeptr(self->blow_in);*/
    if(self->out)
        sysmem_freeptr(self->out);
    if(self->aux)
        sysmem_freeptr(self->aux);
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
            case 1:
                strncpy(string_dest,"(signal) AUX", ASSIST_STRING_MAXSIZE);
                break;
		}
	}
}


void ext_main(void* r) {
	this_class = class_new("vb.mi.elmnts32~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);
    
    // exciter pots
    class_addmethod(this_class, (method)myObj_contour, "contour",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_bow,    "bow",         A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_blow, "blow",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_strike, "strike",    A_FLOAT, 0);
    
    // bigger pots
    class_addmethod(this_class, (method)myObj_flow, "flow",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_mallet, "mallet",    A_FLOAT, 0);
    
    // timbre pots
    class_addmethod(this_class, (method)myObj_bow_timbre,      "bow_timbre",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_blow_timbre,     "blow_timbre",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_strike_timbre,   "strike_timbre",        A_FLOAT, 0);
    
    // resonator pots
    class_addmethod(this_class, (method)myObj_coarse, "coarse",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_fine,    "fine",         A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_fm, "fm",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_geometry, "geometry",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_brightness, "brightness",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_damping, "damping",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_position, "position",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_space, "space",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_strength,  "strength",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_note,    "note",    A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_button,  "play",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_model,  "model",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_bypass,  "bypass",    A_LONG, 0);
    class_addmethod(this_class, (method)myObj_easter,  "easteregg",          A_LONG, 0);

    
	
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
}
