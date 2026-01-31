//
// Copyright 2019 Volker Böhm.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.


// a clone of mutable instruments' 'plaits' module for maxmsp
// by volker böhm, jan 2019, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/



#include "c74_msp.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"
#ifdef __APPLE__
#include "Accelerate/Accelerate.h"
#endif


//#define ENABLE_LFO_MODE
#pragma warning (disable : 4068 )


using namespace c74::max;


const size_t kBlockSize = plaits::kBlockSize;

double kSampleRate = 48000.0;
double a0 = (440.0 / 8.0) / kSampleRate;

static t_class* this_class = nullptr;

struct t_myObj {
    t_pxobject	obj;

    plaits::Voice       *voice_;
    plaits::Modulations modulations;
    plaits::Patch       patch;
    double              transposition_;
    double              octave_;
    double              morph_pot;
    double              harm_pot;
    double              timb_pot;
    long                engine;
    short               trigger_connected;
    short               trigger_toggle;

    char                *shared_buffer;
    void                *info_out;

    double              sr;
    int                 sigvs;
};



void* myObj_new(t_symbol *s, long argc, t_atom *argv) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);

    if(self)
    {
        dsp_setup((t_pxobject*)self, 8);

        self->info_out = outlet_new((t_object *)self, NULL);
        outlet_new(self, "signal"); // 'out' output
        outlet_new(self, "signal"); // 'aux' output

        self->sigvs = sys_getblksize();

        if(self->sigvs < kBlockSize) {
            object_error((t_object*)self,
                         "sigvs can't be smaller than %d samples\n", kBlockSize);
            object_free(self);
            self = NULL;
            return self;
        }

        self->sr = sys_getsr();
        if(self->sr <= 0)
            self->sr = 44100.0;


        kSampleRate = self->sr;
        a0 = (440.0f / 8.0f) / kSampleRate;

        // init some params
        self->transposition_ = 0.;
        self->octave_ = 0.5;
        self->patch.note = 48.0;
        self->patch.harmonics = 0.1;
        self->patch.decay = 0.333;
        self->patch.morph = 0.0;
        self->patch.timbre = 0.0;
        self->patch.lpg_colour = 0.5;
        self->patch.frequency_modulation_amount = 0.0;
        self->patch.timbre_modulation_amount = 0.0;
        self->patch.morph_modulation_amount = 0.0;
        
        self->morph_pot = 0.0;
        self->harm_pot = 0.0;
        self->timb_pot = 0.0;


        // allocate memory
        self->shared_buffer = sysmem_newptrclear(32768);

        if(self->shared_buffer == NULL) {
            object_post((t_object*)self, "mem alloc failed!");
            object_free(self);
            self = NULL;
            return self;
        }
        stmlib::BufferAllocator allocator(self->shared_buffer, 32768);

        self->voice_ = new plaits::Voice;
        self->voice_->Init(&allocator);

        // process attributes
        attr_args_process(self, argc, argv);

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

void calc_note(t_myObj* self)
{
#ifdef ENABLE_LFO_MODE
    int octave = static_cast<int>(self->octave_ * 10.0);
    if (octave == 0) {
        self->patch.note = -48.37 + self->transposition_ * 60.0;
    } else if (octave == 9) {
        self->patch.note = 60.0 + self->transposition_ * 48.0;
    } else {
        const double fine = self->transposition_ * 7.0;
        self->patch.note = fine + static_cast<double>(octave) * 12.0;
    }
#else
    int octave = static_cast<int>(self->octave_ * 9.0);
    if (octave < 8) {
        const double fine = self->transposition_ * 7.0;
        self->patch.note = fine + static_cast<float>(octave) * 12.0 + 12.0;
    } else {
        self->patch.note = 60.0 + self->transposition_ * 48.0;
    }
#endif  // ENABLE_LFO_MODE
}



// plug / unplug patch chords...

void myObj_int(t_myObj *self, long value)
{
    long innum = proxy_getinlet((t_object *)self);

    switch (innum) {
        case 0:
            // TODO: this doesn't seem to set the 'engine' attribute correctly
            self->patch.engine = self->engine = CLAMP(value, 0L, 23L);
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

void myObj_float(t_myObj *self, double value)
{
    long innum = proxy_getinlet((t_object *)self);

    switch (innum) {
        case 1:
            self->transposition_ = CLAMP(value, -1., 1.);
            calc_note(self);
            break;
        case 3:
            self->harm_pot = CLAMP(value, 0., 1.);
            break;
        case 4:
            self->timb_pot = CLAMP(value, 0., 1.);
            break;
        case 5:
            self->morph_pot = CLAMP(value, 0., 1.);
            break;
        default:
            break;
    }
}


//void myObj_choose_engine(t_myObj* self, long e) {
//    self->patch.engine = e;
//}

t_max_err engine_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->patch.engine = self->engine = m;
    }

    return MAX_ERR_NONE;
}

void myObj_get_engine(t_myObj* self) {

    t_atom argv;
    atom_setlong(&argv, self->voice_->active_engine());
    outlet_anything(self->info_out, gensym("active_engine"), 1, &argv);

}


#pragma mark ----- main pots -----
// main pots

void myObj_frequency(t_myObj* self, double m) {
    self->transposition_ = CLAMP(m, -1., 1.);
    calc_note(self);
}

void myObj_harmonics(t_myObj* self, double h) {
    self->harm_pot = CLAMP(h, 0., 1.);
}

void myObj_timbre(t_myObj* self, double t) {
    self->timb_pot = CLAMP(t, 0., 1.);
}

void myObj_morph(t_myObj* self, double m) {
    self->morph_pot = CLAMP(m, 0., 1.);
}

// smaller pots
void myObj_timbre_mod_amount(t_myObj* self, double m) {
    self->patch.timbre_modulation_amount = CLAMP(m, -1., 1.);
}

void myObj_freq_mod_amount(t_myObj* self, double m) {
    self->patch.frequency_modulation_amount = CLAMP(m, -1., 1.);
}

void myObj_morph_mod_amount(t_myObj* self, double m) {
    self->patch.morph_modulation_amount = CLAMP(m, -1., 1.);
}


#pragma mark ----- hidden parameter -----

// hidden parameters

void myObj_decay(t_myObj* self, double m) {
    self->patch.decay = CLAMP(m, 0., 1.);
}
void myObj_lpg_colour(t_myObj* self, double m) {
    self->patch.lpg_colour = CLAMP(m, 0., 1.);
}

void myObj_octave(t_myObj* self, double m) {
    self->octave_ = CLAMP(m, 0., 1.);
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

    long    vs = sampleframes;
    size_t  size = plaits::kBlockSize;
    plaits::Patch *p = &self->patch;
    double morph_pot = self->morph_pot;
    double harm_pot = self->harm_pot;
    double timb_pot = self->timb_pot;
    uint16_t count = 0;

    if (self->obj.z_disabled)
        return;


    // copy first value of signal inlets into corresponding params
    double* destination = &self->modulations.engine;

    for(count=0; count < vs; count += size) {
        
        // parameter smoothing
        ONE_POLE(p->morph, morph_pot, 0.012);
        ONE_POLE(p->harmonics, harm_pot, 0.012);
        ONE_POLE(p->timbre, timb_pot, 0.012);

        for(int i=0; i<8; i++) {
            destination[i] = ins[i][count];
        }

        if(self->modulations.trigger_patched) {
            // calc sum of trigger input
            double vectorsum = 0.0;
#ifdef __APPLE__
            vDSP_sveD(trig_input+count, 1, &vectorsum, size);
#else
            for(int i=0; i<size; ++i)
                vectorsum += trig_input[i+count];
#endif
            self->modulations.trigger = vectorsum;
        }

        self->voice_->Render(*p, self->modulations, out+count, aux+count, size);
    }

}




void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    self->trigger_connected = count[6];
    self->modulations.trigger_patched = self->trigger_toggle && self->trigger_connected;

    if(maxvectorsize < kBlockSize) {
        object_error((t_object*)self, "sigvs can't be smaller than %d samples, sorry!", kBlockSize);
        return;
    }

    if(samplerate != self->sr) {
        self->sr = samplerate;
        kSampleRate = self->sr;
        a0 = (440.0f / 8.0f) / kSampleRate;
    }

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
    this_class = class_new("vb.mi.plts~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);

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

//    class_addmethod(this_class, (method)myObj_choose_engine,      "engine",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_get_engine,      "get_engine", 0);
    class_addmethod(this_class, (method)myObj_int,  "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,  "float",      A_FLOAT, 0);
//    class_addmethod(this_class, (method)myObj_info,    "info", 0);

    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);

    // attributes ====
    CLASS_ATTR_CHAR(this_class,"engine", 0, t_myObj, engine);
    CLASS_ATTR_ENUMINDEX(this_class, "engine", 0,
        "virtual_analog_with_filter"
        " phase_distortion_synthesis"
        " 6-op_FM_bank1"
        " 6-op_FM_bank2"
        " 6-op_FM_bank3"
        " wave_terrain_synthesis"
        " string_machine_emulation"
        " chiptune_engine"
        " virtual_analog_synthesis"
        " waveshaping_oscillator"
        " 2-op_FM"
        " granular_formant_oscillator"
        " harmonic_oscillator"
        " wavetable_oscillator"
        " chord_engine"
        " speech_synthesis"
        " swarm_engine"
        " filtered_noise"
        " particle_noise"
        " inharmonic_string"
        " modal_resonator"
        " bass_drum_model"
        " snare_drum_model"
        " hi_hat_model");
    CLASS_ATTR_LABEL(this_class, "engine", 0, "synthesis engine");
    CLASS_ATTR_FILTER_CLIP(this_class, "engine", 0, 23);
    CLASS_ATTR_ACCESSORS(this_class, "engine", NULL, (method)engine_setter);
    CLASS_ATTR_SAVE(this_class, "engine", 0);

//    CLASS_ATTR_CHAR(this_class,"timbre_patched", 0, t_myObj, modulations.timbre_patched);
//    CLASS_ATTR_ENUMINDEX(this_class, "timbre_patched", 0, "false true");
//    CLASS_ATTR_LABEL(this_class, "timbre_patched", 0, "timbre modulation patched");
//    CLASS_ATTR_FILTER_CLIP(this_class, "timbre_patched", 0, 1);
//    CLASS_ATTR_SAVE(this_class, "timbre_patched", 0);

    object_post(NULL, "vb.mi.plts~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'plaits' module");
}
