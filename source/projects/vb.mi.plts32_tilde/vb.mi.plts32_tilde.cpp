#include "c74_msp.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"

#include "Accelerate/Accelerate.h"


// TODO: check this out! (see: ui.cc)
//#define ENABLE_LFO_MODE


float kSampleRate = 48000.0;
float a0 = (440.0f / 8.0f) / kSampleRate;

using namespace c74::max;


static t_class* this_class = nullptr;

struct t_myObj {
    t_pxobject	obj;
    
    plaits::Voice       *voice_;
    plaits::Modulations modulations;
    plaits::Patch       patch;
    float               transposition_;
    float               octave_;
    short               trigger_connected;
    short               trigger_toggle;
    
    char                *shared_buffer;
    void                *info_out;
    
    float               sr;
    int                 sigvs;
};




void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 8);
        
        self->info_out = outlet_new((t_object *)self, NULL);
        outlet_new(self, "signal"); // 'out' output
        outlet_new(self, "signal"); // 'aux' output
        
        //plaits::kSampleRate = sys_getsr();
        self->sr = sys_getsr();
        if(self->sr <= 0)
            self->sr = 44100.0;
        
        kSampleRate = self->sr;
        a0 = (440.0f / 8.0f) / kSampleRate;
        //plaits::Dsp::setSr(self->sr);
        //plaits::Dsp::setBlockSize(self->sigvs);
        
        // init some params
        self->transposition_ = 0.;
        self->octave_ = 0.5;
        self->patch.note = 48.0;
        self->patch.harmonics = 0.1;
        
        
        // allocate memory
        self->shared_buffer = sysmem_newptrclear(65536);
        
        if(self->shared_buffer == NULL) {
            object_post((t_object*)self, "mem alloc failed!");
            object_free(self);
            self = NULL;
            return self;
        }
        stmlib::BufferAllocator allocator(self->shared_buffer, 65536);
        
        self->voice_ = new plaits::Voice;
        self->voice_->Init(&allocator);

    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}


void myObj_info(t_myObj *self)
{
    plaits::Patch p = self->patch;
    plaits::Modulations m = self->modulations;
    
    object_post((t_object*)self, "Patch ----------------------->");
    object_post((t_object*)self, "note: %f", p.note);
    object_post((t_object*)self, "harmonics: %f", p.harmonics);
    object_post((t_object*)self, "timbre: %f", p.timbre);
    object_post((t_object*)self, "morph: %f", p.morph);
    object_post((t_object*)self, "freq_mod_amount: %f", p.frequency_modulation_amount);
    object_post((t_object*)self, "timbre_mod_amount: %f", p.timbre_modulation_amount);
    object_post((t_object*)self, "morph_mod_amount: %f", p.morph_modulation_amount);
    
    object_post((t_object*)self, "engine: %d", p.engine);
    object_post((t_object*)self, "decay: %f", p.decay);
    object_post((t_object*)self, "lpg_colour: %f", p.lpg_colour);
    
    object_post((t_object*)self, "Modulations ------------>");
    object_post((t_object*)self, "engine: %f", m.engine);
    object_post((t_object*)self, "note: %f", m.note);
    object_post((t_object*)self, "frequency: %f", m.frequency);
    object_post((t_object*)self, "harmonics: %f", m.harmonics);
    object_post((t_object*)self, "timbre: %f", m.timbre);
    object_post((t_object*)self, "morph: %f", m.morph);
    object_post((t_object*)self, "trigger: %f", m.trigger);
    object_post((t_object*)self, "level: %f", m.level);
    object_post((t_object*)self, "freq_patched: %d", m.frequency_patched);
    object_post((t_object*)self, "timbre_patched: %d", m.timbre_patched);
    object_post((t_object*)self, "morph_patched: %d", m.morph_patched);
    object_post((t_object*)self, "trigger_patched: %d", m.trigger_patched);
    object_post((t_object*)self, "level_patched: %d", m.level_patched);
    object_post((t_object*)self, "-----");
    
    
}


// plug / unplug patch chords...

void myObj_int(t_myObj *self, long value)
{
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 1:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 2:
            self->modulations.frequency_patched = value != 0;
            break;
        case 3:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 4:
            self->modulations.timbre_patched = value != 0;
            break;
        case 5:
            self->modulations.morph_patched = value != 0;
            break;
        case 6:
            self->trigger_toggle = value != 0;
            self->modulations.trigger_patched = self->trigger_toggle && self->trigger_connected;
            break;
        case 7:
            self->modulations.level_patched = value != 0;
            break;
        default:
            break;
    }
}


void calc_note(t_myObj* self)
{
#ifdef ENABLE_LFO_MODE
    int octave = static_cast<int>(self->octave_ * 10.0f);
    if (octave == 0) {
        self->patch.note = -48.37f + self->transposition_ * 60.0f;
    } else if (octave == 9) {
        self->patch.note = 60.0f + self->transposition_ * 48.0f;
    } else {
        const float fine = self->transposition_ * 7.0f;
        self->patch.note = fine + static_cast<double>(octave) * 12.0f;
    }
#else
    int octave = static_cast<int>(self->octave_ * 9.0f);
    if (octave < 8) {
        const float fine = self->transposition_ * 7.0f;
        self->patch.note = fine + static_cast<float>(octave) * 12.0f + 12.0f;
    } else {
        self->patch.note = 60.0f + self->transposition_ * 48.0f;
    }
#endif  // ENABLE_LFO_MODE
}




void myObj_choose_engine(t_myObj* self, long e) {
    self->patch.engine = e;
}

void myObj_get_engine(t_myObj* self) {
    
    //object_post((t_object*)self, "active engine: %d", self->voice_->active_engine());
    
    t_atom argv;
    atom_setlong(&argv, self->voice_->active_engine());
    outlet_anything(self->info_out, gensym("active_engine"), 1, &argv);
    
}


#pragma mark ----- main pots -----
// main pots

void myObj_frequency(t_myObj* self, double m) {
    self->transposition_ = clamp(m, -1., 1.);
    calc_note(self);
}

void myObj_harmonics(t_myObj* self, double h) {
    self->patch.harmonics = clamp(h, 0., 1.);
}

void myObj_timbre(t_myObj* self, double t) {
    self->patch.timbre = clamp(t, 0., 1.);
}

void myObj_morph(t_myObj* self, double m) {
    self->patch.morph = clamp(m, 0., 1.);
}

// smaller pots
void myObj_timbre_mod_amount(t_myObj* self, double m) {
    self->patch.timbre_modulation_amount = clamp(m, -1., 1.);
}

void myObj_freq_mod_amount(t_myObj* self, double m) {
    self->patch.frequency_modulation_amount = clamp(m, -1., 1.);
}

void myObj_morph_mod_amount(t_myObj* self, double m) {
    self->patch.morph_modulation_amount = clamp(m, -1., 1.);
}


#pragma mark ----- hidden parameter -----

// hidden parameters

void myObj_decay(t_myObj* self, double m) {
    self->patch.decay = clamp(m, 0., 1.);
}
void myObj_lpg_colour(t_myObj* self, double m) {
    self->patch.lpg_colour = clamp(m, 0., 1.);
}

void myObj_octave(t_myObj* self, double m) {
    self->octave_ = clamp(m, 0., 1.);
    calc_note(self);
}


// this directly sets the pitch via a midi note
void myObj_note(t_myObj* self, double n) {
    self->patch.note = n;
}



#pragma mark ----- dsp loop -----

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double *out = outs[0];
    double *aux = outs[1];
    double *trig_input = ins[6];
    
    long        vs = sampleframes;
    size_t      size = plaits::kBlockSize;
    plaits::Voice::Frame   output[size];
    
    float       pitch_lp_ = 0.;
    uint16_t    count = 0;
    
    if (self->obj.z_disabled)
        return;
    
    
    float* destination = &self->modulations.engine;
    
    for(count = 0; count < vs; count += size) {
        
        for(auto i=0; i<8; i++) {
            destination[i] = ins[i][count];
        }
        
        if(self->modulations.trigger_patched) {
            // calc sum of trigger input
            double vectorsum = 0.0;
            
            //for(int i=0; i<vs; ++i)
              //  vectorsum += trig[i];
            vDSP_sveD(trig_input+count, 1, &vectorsum, count);
            self->modulations.trigger = vectorsum;
        }
        
        // smooth out pitch changes
        ONE_POLE(pitch_lp_, self->modulations.note, 0.7);
        
        self->modulations.note = pitch_lp_;

        self->voice_->Render(self->patch, self->modulations, output, size);
        
        for(auto i=0; i<size; ++i) {
            out[count+i] = output[i].out / 32768.f;
            aux[count+i] = output[i].aux / 32768.f;
        }
    }
    
}



void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    self->trigger_connected = count[6];
    self->modulations.trigger_patched = self->trigger_toggle && self->trigger_connected;
    
    if(maxvectorsize > plaits::kMaxBlockSize) {
        object_error((t_object*)self, "vector size can't be larger than 1024 samples, sorry!");
        return;
    }
    if(samplerate != self->sr) {
        self->sr = samplerate;
        kSampleRate = self->sr;
        a0 = (440.0f / 8.0f) / kSampleRate;
    }
    object_post(NULL, "new sr: %f", kSampleRate);
/*
    if(maxvectorsize != self->sigvs)
        plaits::Dsp::setBlockSize(maxvectorsize);
*/
    
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);
    self->voice_->plaits::Voice::~Voice();
    delete self->voice_;
    if(self->shared_buffer)
        sysmem_freeptr(self->shared_buffer);
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) MODEL", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) V/OCT", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) FM, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) HARMO", ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal) TIMBRE, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal) MORPH, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 6:
                strncpy(string_dest,"(signal) TRIGGER, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 7:
                strncpy(string_dest,"(signal) LEVEL, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
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
    this_class = class_new("vb.mi.plts32~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);
    
    // main pots
    class_addmethod(this_class, (method)myObj_frequency,        "frequency",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_harmonics,	"harmonics",	A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_timbre,       "timbre",       A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_morph,        "morph",        A_FLOAT, 0);
    
    // small pots
    class_addmethod(this_class, (method)myObj_morph_mod_amount,        "morph_mod",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_timbre_mod_amount,        "timbre_mod",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_freq_mod_amount,        "freq_mod",        A_FLOAT, 0);
    
    // hidden parameters
    class_addmethod(this_class, (method)myObj_octave,        "octave",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_lpg_colour,   "lpg_colour",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_decay,        "decay",        A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_note,	"note",	A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_choose_engine,      "engine",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_get_engine,      "get_engine", 0);
    class_addmethod(this_class, (method)myObj_int,  "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_info,	"info", 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.plts32~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'plaits' module");
}
