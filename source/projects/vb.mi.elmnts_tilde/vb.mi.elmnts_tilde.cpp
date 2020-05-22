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


// a clone of mutable instruments' 'elements' module for maxmsp
// by volker böhm, april 2019, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/




#include "c74_msp.h"


#include "elements/dsp/dsp.h"
#include "elements/dsp/part.h"
#include "read_inputs.hpp"

#include "Accelerate/Accelerate.h"


using namespace c74::max;

const size_t kBlockSize = elements::kMaxBlockSize;

double elements::Dsp::kSampleRate = 32000.0;
double elements::Dsp::kSrFactor = 32000.0 / kSampleRate;
double elements::Dsp::kIntervalCorrection = log(kSrFactor)/log(2.0)*12.0;

static t_class* this_class = nullptr;


struct t_myObj {
    t_pxobject	obj;
    
    elements::Part              *part;
    elements::PerformanceState  ps;
    elements::ReadInputs        read_inputs;
    double              strike_in_level;
    double              blow_in_level;
    bool                uigate;
    
    uint16_t            *reverb_buffer;
    void                *info_out;
    //double              *out, *aux;     // output buffers
    double              sr;
    long                sigvs;
    bool                gate_connected;
    short               blockCount;
};


void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 16);
        
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
        
        // init some params
        
        self->strike_in_level = 0.0;
        self->blow_in_level = 0.0;
        self->uigate = false;
        
        // allocate memory
        self->reverb_buffer = (t_uint16*)sysmem_newptrclear(32768*sizeof(t_uint16));
        
        // TODO: make sure 4096 is enough memory
//        self->out = (double *)sysmem_newptrclear(4096*sizeof(double));
//        self->aux = (double *)sysmem_newptrclear(4096*sizeof(double));
        
        if(self->reverb_buffer == NULL) {
            object_post((t_object*)self, "mem alloc failed!");
            object_free(self);
            self = NULL;
            return self;
        }
        

        self->read_inputs.Init();
        
        
        // Init and seed the random parameters and generators with the serial number.
        self->part = new elements::Part;
        memset(self->part, 0, sizeof(*t_myObj::part));
        self->part->Init(self->reverb_buffer);
        //self->part->Seed((uint32_t*)(0x1fff7a10), 3);
        uint32_t mySeed = 0x1fff7a10;
        self->part->Seed(&mySeed, 3);
        
        self->part->set_easter_egg(false);
        
        self->blockCount = 0;

    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}


void myObj_button(t_myObj *self, long b) {
    self->uigate = b != 0;
}

void myObj_info(t_myObj *self)
{
    elements::PerformanceState *ps = &self->ps;
    elements::Patch p = *self->part->mutable_patch();
    
    object_post((t_object*)self, "Performance State ------------>");
    object_post((t_object*)self, "gate: %d", ps->gate);
    object_post((t_object*)self, "note: %f", ps->note);
    object_post((t_object*)self, "modulation: %f", ps->modulation);
    object_post((t_object*)self, "strength: %f", ps->strength);
    
    object_post((t_object*)self, "Patch - exciter ----------------------->");
    object_post((t_object*)self, "envelope_shape: %f", p.exciter_envelope_shape);
    object_post((t_object*)self, "bow_level: %f", p.exciter_bow_level);
    object_post((t_object*)self, "bow_timbre: %f", p.exciter_bow_timbre);
    object_post((t_object*)self, "blow_level: %f", p.exciter_blow_level);
    object_post((t_object*)self, "blow_meta: %f", p.exciter_blow_meta);
    object_post((t_object*)self, "blow_timbre: %f", p.exciter_blow_timbre);
    object_post((t_object*)self, "strike_level: %f", p.exciter_strike_level);
    object_post((t_object*)self, "strike_meta: %f", p.exciter_strike_meta);
    object_post((t_object*)self, "strike_timbre: %f", p.exciter_strike_timbre);
    object_post((t_object*)self, "signature: %f", p.exciter_signature);
    
    object_post((t_object*)self, "Patch - resonator ----------------------->");
    object_post((t_object*)self, "geometry: %f", p.resonator_geometry);
    object_post((t_object*)self, "brightness: %f", p.resonator_brightness);
    object_post((t_object*)self, "damping: %f", p.resonator_damping);
    object_post((t_object*)self, "position: %f", p.resonator_position);
    object_post((t_object*)self, "modulation_frequency: %f", p.resonator_modulation_frequency);
    object_post((t_object*)self, "modulation_offset: %f", p.resonator_modulation_offset);
    
    object_post((t_object*)self, "Patch - reverb ----------------------->");
    object_post((t_object*)self, "reverb_diffusion: %f", p.reverb_diffusion);
    object_post((t_object*)self, "reverb_lp: %f", p.reverb_lp);
    object_post((t_object*)self, "space: %f", p.space);
    object_post((t_object*)self, "<-----------------------");
    
    
}


void myObj_bang(t_myObj *self) {
    /*
    int size = elements::kMaxModes;
    double *f = self->part->voice_[0].getF();
    t_atom freqs[size];
    for(int i=0; i<size; ++i)
        atom_setfloat(&freqs[i], f[i]);
    outlet_list(self->info_out, 0L, size, freqs);
    */
        //object_post((t_object*)self, "freq[%d]: %f", i, freqs[i]);
}



// plug / unplug patch chords...

void myObj_int(t_myObj *self, long value)
{
    //long innum = proxy_getinlet((t_object *)self);
    /*
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
            self->modulations.trigger_patched = value != 0;
            break;
        case 7:
            self->modulations.level_patched = value != 0;
            break;
        default:
            break;
    }*/
}


// read float inputs at corresponding inlet as attenuverter levels
void myObj_float(t_myObj *self, double f)
{
    long innum = proxy_getinlet((t_object *)self);
    //object_post(NULL, "innum: %ld", innum);
    f = clamp(f, -1., 1.);
    
    /* inlets 0 - 5 are for
        0: ext in 1
        1: ext in 2
        2: v/oct
        3: fm
        [4: gate     ?????]
        4: strength
     */
    switch (innum) {
        case 5:
            self->read_inputs.ReadPanelPot(elements::POT_EXCITER_BOW_TIMBRE_ATTENUVERTER, f);
            break;
        case 6:
            self->read_inputs.ReadPanelPot(elements::POT_EXCITER_BLOW_META_ATTENUVERTER, f);
            break;
        case 7:
            self->read_inputs.ReadPanelPot(elements::POT_EXCITER_BLOW_TIMBRE_ATTENUVERTER, f);
            break;
        case 8:
            self->read_inputs.ReadPanelPot(elements::POT_EXCITER_STRIKE_META_ATTENUVERTER, f);
            break;
        case 9:
            self->read_inputs.ReadPanelPot(elements::POT_EXCITER_STRIKE_TIMBRE_ATTENUVERTER, f);
            break;
        case 10:
            self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_DAMPING_ATTENUVERTER, f);
            break;
        case 11:
            self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_GEOMETRY_ATTENUVERTER, f);
            break;
        case 12:
            self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_POSITION_ATTENUVERTER, f);
            break;
        case 13:
            self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_BRIGHTNESS_ATTENUVERTER, f);
            break;
        case 14:
            self->read_inputs.ReadPanelPot(elements::POT_SPACE_ATTENUVERTER, f);
            break;
        default:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
    }
}



#pragma mark ----- exciter pots -----

// contour (env_shape)
void myObj_contour(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_ENVELOPE_SHAPE, m);
}

// bow level
void myObj_bow(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_BOW_LEVEL, m);
}

//blow level
void myObj_blow(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_BLOW_LEVEL, m);
}

// strike level
void myObj_strike(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_STRIKE_LEVEL, m);
}

// bigger exciter pots ---------------------------
// blow meta
void myObj_flow(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_BLOW_META, m);
}

// strike meta
void myObj_mallet(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_STRIKE_META, m);
}


// bow timbre
void myObj_bow_timbre(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_BOW_TIMBRE, m);
}

// blow timbre
void myObj_blow_timbre(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_BLOW_TIMBRE, m);
}

// strike timbre
void myObj_strike_timbre(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_EXCITER_STRIKE_TIMBRE, m);
}

#pragma mark ----- resonator pots -----

// coarse freq -- 
void myObj_coarse(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_COARSE, m*60.0 + 39.0);//120.0);
    // TODO: need correct scaling
}

// fine freq
void myObj_fine(t_myObj* self, double m) {
    m = clamp(m, -1., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_FINE, m);
}

// fm (big pot, but acts as attenuverter)
void myObj_fm(t_myObj* self, double m) {
    m = clamp(m, -1., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_FM_ATTENUVERTER, m);
}

// geometry
void myObj_geometry(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_GEOMETRY, m);
}

void myObj_brightness(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_BRIGHTNESS, m);
}

void myObj_damping(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_DAMPING, m);
}

void myObj_position(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_POSITION, m);
}

void myObj_space(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->read_inputs.ReadPanelPot(elements::POT_SPACE, m * 1.11);
    // vb, use the last bit to trigger rev freeze
}



// this directly sets the pitch via a midi note
void myObj_note(t_myObj* self, double n) {
    //self->ps.note = n;
    self->read_inputs.ReadPanelPot(elements::POT_RESONATOR_COARSE, n); // write midi pitch directly into COARSE (set 'law' to 'linear'!)
}



void myObj_model(t_myObj *self, long n) {

    CONSTRAIN(n, 0, 2);
    self->part->set_resonator_model(static_cast<elements::ResonatorModel>(n));
    self->read_inputs.set_resonator_model(self->part->resonator_model());
    
    /*
     TODO: have a look at 'easter_egg' mode (in ui.cc)
     */
}


void myObj_bypass(t_myObj* self, long n) {
    self->part->set_bypass(n != 0);
}

void myObj_easter(t_myObj* self, long n) {
    self->part->set_easter_egg(n != 0);
}



inline void SoftLimit_block(t_myObj *self, double *inout, size_t size)
{
    while(size--) {
        double x = *inout * 0.5;
        double x2 = x * x;
        *inout = x * (27.0 + x2) / (27.0 + 9.0 * x2);
        inout++;
    }
}


#pragma mark ----- dsp loop -----

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double *blow_in = ins[0];       // red input
    double *strike_in = ins[1];     // green input
    double *outL = outs[0];
    double *outR = outs[1];
    double *gate_in = ins[15];
    
    
    long vs = sampleframes;
    size_t size = kBlockSize;
    
    if (self->obj.z_disabled)
        return;
    

    double *cvinputs = self->read_inputs.cv_floats;    //self->cvinputs;
    int     numcvs = elements::CV_ADC_CHANNEL_LAST;     // 13
    elements::PerformanceState *ps = &self->ps;
    bool    gate_connected = self->gate_connected;
    
    ps->gate = self->uigate;            // ui button pressed?
    
    /*
    for (size_t i = 0; i < vs; ++i)
    {
        double blow_in_sample = blow_in[i];
        double strike_in_sample = strike_in[i];
        double error, gain;
        
        error = strike_in_sample * strike_in_sample - strike_in_level;
        strike_in_level += error * (error > 0.0 ? 0.1 : 0.0001);
        gain = strike_in_level <= kNoiseGateThreshold
        ? (1.0 / kNoiseGateThreshold) * strike_in_level : 1.0;
        strike_in[i] = gain * strike_in_sample;
        
        error = blow_in_sample * blow_in_sample - blow_in_level;
        blow_in_level += error * (error > 0.0 ? 0.1 : 0.0001);
        gain = blow_in_level <= kNoiseGateThreshold
        ? (1.0 / kNoiseGateThreshold) * blow_in_level : 1.0;
        blow_in[i] = gain * blow_in_sample;
    }*/
//    int kend = vs / size;
//
//    for(size_t k = 0; k < kend; ++k) {
//        int offset = k * size;
//
//        // read 'cv' input signals, store only first value of a block
//        for(int j=0; j<numcvs; j++) {
//            cvinputs[j] = ins[j+2][offset];  // leave out first two inlets (audio inputs)
//        }
//
//        if(gate_connected) {        // check if gate signal is connected
//            double trigger = 0.0;
//            vDSP_sveD(gate_in+offset, 1, &trigger, size);   // calc sum of input block
//            //cvinputs[numcvs] = trigger;         //put it into last cv channel
//            ps->gate |= trigger != 0.0;
//        }
//        //else
//          //  cvinputs[numcvs] = 0.0;
//
//
//        self->read_inputs.Read(self->part->mutable_patch(), ps);
//
//        self->part->Process(*ps, blow_in+offset, strike_in+offset, out, aux, size);
//
//        for (size_t i = 0; i < size; ++i) {
//            int index = i + offset;
//            outL[index] = out[i];
//            outR[index] = aux[i];
//        }
//
//    }
    
    
    for(size_t count = 0; count < vs; count += size) {
        
        // read 'cv' input signals, store only first value of a block
        for(int j=0; j<numcvs; j++) {
            cvinputs[j] = ins[j+2][count];  // leave out first two inlets (audio inputs)
        }
        
        if(gate_connected) {        // check if gate signal is connected
            double trigger = 0.0;
            vDSP_sveD(gate_in+count, 1, &trigger, size);   // calc sum of input block
            ps->gate |= trigger != 0.0;
        }

        
        self->read_inputs.Read(self->part->mutable_patch(), ps);
        
        self->part->Process(*ps, blow_in+count, strike_in+count, outL+count, outR+count, size);
        
    }
    
    SoftLimit_block(self, outL, vs);
    SoftLimit_block(self, outR, vs);
}




void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    self->gate_connected = count[15];       // check if last signal inlet (gate in) is connected
    
    if(maxvectorsize < elements::kMaxBlockSize) {
        object_error((t_object*)self, "sigvs can't be smaller than %d samples, sorry!", kBlockSize);
        return;
    }
    if(samplerate != elements::Dsp::getSr()) {
        elements::Dsp::setSr(samplerate);
        self->sr = samplerate;
        
        self->part->Init(self->reverb_buffer);
        object_post((t_object *)self, "Re-Init() after change of SR: %f", elements::Dsp::getSr());
    }
    
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self)
{
    dsp_free((t_pxobject*)self);
    
    delete self->part;

    if(self->reverb_buffer)
        sysmem_freeptr(self->reverb_buffer);
//    if(self->out)
//        sysmem_freeptr(self->out);
//    if(self->aux)
//        sysmem_freeptr(self->aux);
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
    this_class = class_new("vb.mi.elmnts~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);
    
    // exciter pots
    class_addmethod(this_class, (method)myObj_contour, "contour",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_bow,	"bow",         A_FLOAT, 0);
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
    class_addmethod(this_class, (method)myObj_fine,	"fine",         A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_fm, "fm",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_geometry, "geometry",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_brightness, "brightness",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_damping, "damping",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_position, "position",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_space, "space",    A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_note,	"note",	A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_button,  "play",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_model,  "model",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_bypass,  "bypass",    A_LONG, 0);
    class_addmethod(this_class, (method)myObj_int,  "int",          A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,  "float",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_easter,  "easteregg",          A_LONG, 0);
    class_addmethod(this_class, (method)myObj_info,	"info", 0);
    class_addmethod(this_class, (method)myObj_bang,    "bang", 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.elmnts~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'elements' module");
}
