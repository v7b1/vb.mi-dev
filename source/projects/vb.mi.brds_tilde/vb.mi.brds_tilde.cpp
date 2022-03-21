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


// a clone of mutable instruments' 'braids' module for maxmsp
// by volker böhm, okt 2019, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/




#include "c74_msp.h"

#include "stmlib/utils/dsp.h"

#include "braids/envelope.h"
#include "braids/macro_oscillator.h"
#include "braids/quantizer.h"
#include "braids/signature_waveshaper.h"
#include "braids/quantizer_scales.h"
#include "braids/vco_jitter_source.h"

#ifdef __APPLE__
#include "Accelerate/Accelerate.h"
#endif
#include "samplerate.h"


// TODO: work on decimation and sr reduction + audioBlockSize, auto-trigger


const uint32_t  kSampleRate = 96000;        // original sampling rate
const uint16_t  kAudioBlockSize = 32;       // 24 
const float     kSampScale = (float)(1.0 / 32767.0);


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


typedef struct
{
    braids::MacroOscillator *osc;
    braids::SignatureWaveshaper *ws;

    uint16_t    signature, bit_mask;
    size_t      decimation_factor;
    
    float       samps[kAudioBlockSize] ;
    int16_t     buffer[kAudioBlockSize];
    uint8_t     sync_buffer[kAudioBlockSize];
    
} PROCESS_CB_DATA ;


struct t_myObj {
	t_pxobject	obj;
    
    braids::VcoJitterSource jitter_source;
    braids::Quantizer   *quantizer;
    
    // settings
    int16_t         timbre, color;
    double          timbre_pot, color_pot;
    uint8_t         shape, drift, root, scale;
    bool            trigger_flag, auto_trig;
    bool            last_trig, trig_connected;
    bool            resamp;
    int32_t         midi_pitch;
    int32_t         previous_pitch;
    
    double          sr;
    long            sigvs;
    double          last_in;
    
    // resampler
    SRC_STATE       *src_state;
    PROCESS_CB_DATA pd;
    float           *samples;
    double          ratio;
    
};


static long src_input_callback(void *cb_data, float **audio);


void* myObj_new(t_symbol *s, long argc, t_atom *argv) {
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 5);
        outlet_new(self, "signal");
        
        self->sigvs = sys_getblksize();
        
        if(self->sigvs < kAudioBlockSize) {
            object_error((t_object*)self,
                         "sigvs can't be smaller than %d samples\n", kAudioBlockSize);
            object_free(self);
            self = NULL;
            return self;
        }

        self->sr = sys_getsr();
        self->ratio = self->sr / kSampleRate;
        
        self->pd.osc = new braids::MacroOscillator;
        self->pd.osc->Init(kSampleRate);
        self->pd.osc->set_pitch((48 << 7));
        self->shape = 0;
        self->pd.osc->set_shape(braids::MACRO_OSC_SHAPE_CSAW);
        
        self->pd.signature = 0;
        self->pd.decimation_factor = 1;
        self->pd.bit_mask = 0xffff;
        
        self->pd.ws = new braids::SignatureWaveshaper;
        self->pd.ws->Init(123784);   // TODO: check out GetUniqueId(1)
        
        self->quantizer = new braids::Quantizer;
        self->quantizer->Init();
        self->quantizer->Configure(braids::scales[0]);
        self->scale = 0;
        
        self->jitter_source.Init();
        
        memset(self->pd.sync_buffer, 0, sizeof(self->pd.sync_buffer));
        
        self->midi_pitch = 60 << 7;     // 69?
        self->root = 0;
        self->timbre = 0;
        self->color = 0;
        self->timbre_pot = 0.0;
        self->color_pot = 0.0;
        self->drift = 0;
        self->auto_trig = true;
        self->trigger_flag = false;
        self->resamp = true;
        self->last_in = 0.0;
        self->previous_pitch = 0;
        
        // setup SRC ----------------
        int error;
        int converter = SRC_SINC_FASTEST;       //SRC_SINC_MEDIUM_QUALITY;
        
        
        /* Initialize the sample rate converter. */
        if ((self->src_state = src_callback_new(src_input_callback, converter, 1, &error, &self->pd)) == NULL)
        {
            object_post(NULL, "\n\nError : src_new() failed : %s.\n\n", src_strerror (error)) ;
            //exit (1) ;
        }
        
        self->samples = (float *)sysmem_newptrclear(1024 * sizeof(float));
        
        
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

// timbre
void myObj_timbre(t_myObj* self, double m) {
    self->timbre_pot = CLAMP(m, 0., 1.);
}

void myObj_color(t_myObj* self, double m) {
    self->color_pot = CLAMP(m, 0., 1.);
}


#pragma mark ----- general pots -----

void myObj_coarse(t_myObj* self, double m) {
    m = CLAMP(m, -4., 4.) * 12.0 + 60.0;   // +/-4 octaves around middle C
    int16_t pit = (int)m;
    double frac = m - pit;
    self->midi_pitch = (pit << 7) + (int)(frac * 128.0);
}

// this directly sets the pitch via a midi note
void myObj_note(t_myObj* self, double n) {
    n = CLAMP(n, 0., 127.);
    int pit = (int)n;
    double frac = n - pit;
    self->midi_pitch = (pit << 7) + (int)(frac * 128.0);
    
    if(self->auto_trig)
        self->trigger_flag = true;
}



void myObj_float(t_myObj *self, double m) {
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            myObj_note(self, m);        // TODO: good idea?
            break;
        case 1:
            self->timbre_pot = CLAMP(m, 0., 1.);
            break;
        case 2:
            self->color_pot = CLAMP(m, 0., 1.);
            break;
//        case 3:
//            CONSTRAIN(m, 0., 1.);
//            int shape = m * (braids::MACRO_OSC_SHAPE_LAST - 1);
//            self->shape = shape;
//            break;
        default:
            break;
    }
}



void myObj_bang(t_myObj* self) {
    self->trigger_flag = true;
}

void myObj_shape(t_myObj *self, long n) {
    CONSTRAIN(n, 0, braids::MACRO_OSC_SHAPE_LAST - 1);     // 47
    self->shape = n;
}

//void myObj_set_scale(t_myObj *self, long n) {
//    uint8_t scale = clamp((int)n, 0, 48);
//    self->quantizer->Configure(braids::scales[scale]);
//}

t_max_err scale_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        self->scale = atom_getlong(av);
        self->quantizer->Configure(braids::scales[self->scale]);
    }
    
    return MAX_ERR_NONE;
}


#pragma mark -------- settings ----------

void myObj_set_sampleRate(t_myObj *self, int input) {
    uint8_t sample_rate = CLAMP(input, 0, 6);
    self->pd.decimation_factor = decimation_factors[sample_rate];
}

void myObj_set_resolution(t_myObj *self, int input) {
    uint8_t resolution = CLAMP(input, 0, 6);
    self->pd.bit_mask = bit_reduction_masks[resolution];
}

void myObj_signature(t_myObj *self, int input) {
    self->pd.signature = CLAMP(input, 0, 4) * 4095;
}



#pragma mark ---------- callback function -----------

static long
src_input_callback(void *cb_data, float **audio)
{
    PROCESS_CB_DATA *data = (PROCESS_CB_DATA *) cb_data;
    const int input_frames = kAudioBlockSize;
    
    int16_t     *buffer = data->buffer;
    uint8_t     *sync_buffer = data->sync_buffer;
    float       *samps = data->samps;
    /*
    uint16_t    bit_mask = data->bit_mask;
    size_t      decimation_factor = data->decimation_factor;
    uint16_t    signature = data->signature;
    int16_t     sample = 0;

    braids::SignatureWaveshaper *ws = data->ws;
    */
    data->osc->Render(sync_buffer, buffer, input_frames);
    
    for (size_t i = 0; i < input_frames; ++i) {
        /*
        if((i % decimation_factor) == 0) {
            sample = buffer[i] & bit_mask;
        }
        int16_t warped = ws->Transform(sample);
        buffer[i] = stmlib::Mix(sample, warped, signature);
        */
        samps[i] = (buffer[i] * kSampScale);
    }
    
    *audio = &(samps [0]);
    
    return input_frames;
}




#pragma mark -------- DSP Loop ----------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double  *pitch_cv = ins[0];     // V/OCT
    double  *timbre_cv = ins[1];    // timber CV
    double  *color_cv = ins[2];     // color CV
    double  *model_cv = ins[3];     // model selection
    double  *trigger_cv = ins[4];   // trig input

    long    vs = sampleframes;
    
    float       *samples, *output;
    int16_t     timbre, color, count;
    double      ratio, timbre_pot, color_pot;
    uint8_t     shape_offset, shape, root, drift;
    int32_t     midi_pitch, pitch = 0;
    bool        trig_connected, trigger_flag;
    
    if (self->obj.z_disabled)
        return;
    
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
    
    braids::MacroOscillator *osc = self->pd.osc;
    braids::Quantizer *quantizer = self->quantizer;
    braids::VcoJitterSource *jitter_source = &self->jitter_source;
    SRC_STATE   *src_state = self->src_state;
    
    for(count = 0; count < vs; count += kAudioBlockSize) {
        output = samples + count;
        
        // set parameters
        timbre = CLAMP(timbre_pot + timbre_cv[count], 0.0, 1.0) * 32767.0;
        color = CLAMP(color_pot + color_cv[count], 0.0, 1.0) * 32767.0;
        osc->set_parameters(timbre, color);
        
        // set shape/model
        shape = ((int)(model_cv[count] * braids::MACRO_OSC_SHAPE_LAST) + shape_offset) & 63;
        if (shape >= braids::MACRO_OSC_SHAPE_LAST)
            shape -= braids::MACRO_OSC_SHAPE_LAST;
        osc->set_shape(static_cast<braids::MacroOscillatorShape>(shape));
        
        // set pitch
        pitch = midi_pitch;
        pitch += (int)(pitch_cv[count] * 128.0 * 12.0);    // V/OCT add pitch in half tone steps
        // quantize
        pitch = quantizer->Process(pitch, (60 + root) << 7);
        pitch += jitter_source->Render(drift);

        // add FM mod
        //pitch += (int)(in2[count] * 5.0 * 7680) >> 12;
        osc->set_pitch( CLAMP(pitch, 0, 16383) );
        
        // detect trigger
        if(trig_connected) {
            double sum = 0.0;
#ifdef __APPLE__
            vDSP_sveD(trigger_cv+count, 1, &sum, kAudioBlockSize);
#else
            for(int i=0; i<kAudioBlockSize; ++i)
                sum += trigger_cv[i+count];
#endif
            bool trigger = sum != 0.0;
            trigger_flag |= (trigger && (!self->last_trig));
            self->last_trig = trigger;
        }
        if(trigger_flag) {
            osc->Strike();
            trigger_flag = false;
        }
        
        // render
        src_callback_read(src_state, ratio, kAudioBlockSize, output);
    }
    
    // copy and type cast output samples from 'float' to 'double'
#ifdef __APPLE__
    vDSP_vspdp(samples, 1, outs[0], 1, vs);
#else
    for(int i=0; i<vs; ++i) {
        outs[0][i] = (double)samples[i];
    }
#endif
    
    self->trigger_flag = trigger_flag;
    
}


void myObj_perform64_no_resamp(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    double  *pitch_cv = ins[0];     // V/OCT
    double  *timbre_cv = ins[1];    // timber CV
    double  *color_cv = ins[2];     // color CV
    double  *model_cv = ins[3];     // model selection
    double  *trigger_cv = ins[4];   // trig input
    double  *out = outs[0];
    
    long vs = sampleframes;
    size_t size = kAudioBlockSize;
    
    if (self->obj.z_disabled)
        return;
    
    float       *samples;
    int16_t     timbre, color, count;
    double      ratio, timbre_pot, color_pot, last_in;
    uint8_t     shape_offset, shape, root, drift;
    int32_t     midi_pitch, pitch = 0;
    bool        trig_connected, trigger_flag;
    
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
    //last_in = self->last_in;
    
    int16_t     *buffer = self->pd.buffer;
    uint8_t     *sync_buffer = self->pd.sync_buffer;
    
    
    braids::MacroOscillator *osc = self->pd.osc;
    braids::Quantizer *quantizer = self->quantizer;
    braids::VcoJitterSource *jitter_source = &self->jitter_source;
    
    
    for(count = 0; count < vs; count += kAudioBlockSize) {
        
        // set parameters
        timbre = CLAMP(timbre_pot + timbre_cv[count], 0.0, 1.0) * 32767.0;
        color = CLAMP(color_pot + color_cv[count], 0.0, 1.0) * 32767.0;
        osc->set_parameters(timbre, color);
        
        // set shape/model
        shape = ((int)(model_cv[count] * braids::MACRO_OSC_SHAPE_LAST) + shape_offset) & 63;
        if (shape >= braids::MACRO_OSC_SHAPE_LAST)
            shape -= braids::MACRO_OSC_SHAPE_LAST;
        osc->set_shape(static_cast<braids::MacroOscillatorShape>(shape));
        
        // set pitch
        pitch = midi_pitch;
        pitch += (int)(pitch_cv[count] * 128.0 * 12.0);    // V/OCT add pitch in half tone steps
        // quantize
        pitch = quantizer->Process(pitch, (60 + root) << 7);
        /*
        bool trigger_detected_flag = false;
        int32_t pitch_delta = pitch - self->previous_pitch;
        if(self->auto_trig && (pitch_delta >= 0x40 || -pitch_delta >= 0x40)) {
            trigger_detected_flag = true;
        }
        self->previous_pitch = pitch;
        */
        pitch += jitter_source->Render(drift);
        
        // add FM mod
        //pitch += (int)(in2[count] * 5.0 * 7680) >> 12;
        osc->set_pitch( CLAMP(pitch, 0, 16383) );
        
        // detect trigger
        if(trig_connected) {
            double sum = 0.0;
#ifdef __APPLE__
            vDSP_sveD(trigger_cv+count, 1, &sum, kAudioBlockSize);
#else
            for(int i=0; i<kAudioBlockSize; ++i)
                sum += trigger_cv[i+count];
#endif
            bool trigger = sum != 0.0;
            trigger_flag |= (trigger && (!self->last_trig));
            self->last_trig = trigger;
        }
        if(trigger_flag) { // || trigger_detected_flag) {
            osc->Strike();
            trigger_flag = false;
        }
        
        osc->Render(sync_buffer, buffer, kAudioBlockSize);
        
        for (size_t i = 0; i < size; ++i) {
            double input = buffer[i] / 32756.0;
            //double filtered = (input + last_in) * 0.5;
            //last_in = input;
            out[count + i] = input;
        }
    }
    //self->last_in = last_in;
}

/*
void myObj_perform64OLD(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
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
    int16_t     timbre, color;
    uint8_t     shape_offset = self->shape;
    uint8_t     shape;


    braids::MacroOscillator *osc = self->osc;
    
    braids::SignatureWaveshaper *ws = self->ws;
    
    int16_t sample = 0;
    size_t decimation_factor = decimation_factors[self->sample_rate];
    uint16_t bit_mask = bit_reduction_masks[self->resolution];
    uint16_t signature = self->signature;
    //signature *= signature * 4095;
    
    
    int kend = vs / size;
    for(size_t k = 0; k < kend; ++k) {
        int offset = k * size;
        
        timbre = clamp(in3[offset], 0.0, 1.0) * 32756.0;
        color = clamp(in4[offset], 0.0, 1.0) * 32756.0;
        
        shape = (int)(in5[offset] + shape_offset) & 63;

        if (shape >= braids::MACRO_OSC_SHAPE_LAST)
            shape -= braids::MACRO_OSC_SHAPE_LAST;
        //object_post(NULL, "shape: %d", shape);
        osc->set_shape(static_cast<braids::MacroOscillatorShape>(shape));
        osc->set_parameters(timbre, color);
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

}*/


void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    // is a signal connected to the trigger input?
    self->trig_connected = count[0];
    
    if(maxvectorsize < kAudioBlockSize) {
        object_error((t_object*)self, "sigvs can't be smaller than %d samples, sorry!", kAudioBlockSize);
        return;
    }
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->ratio = self->sr / kSampleRate;
        
    }
    
    if(self->resamp) {
        self->pd.osc->Init(kSampleRate);
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    }
    else {
        self->pd.osc->Init(self->sr);
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                             dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64_no_resamp, 0, NULL);
    }
}



void myObj_free(t_myObj* self)
{
    dsp_free((t_pxobject*)self);
    delete self->pd.osc;
    delete self->quantizer;
    
    if(self->samples)
        sysmem_freeptr(self->samples);
    
    if(self->src_state)
        src_delete(self->src_state);
    
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
	if (io == ASSIST_INLET) {
		switch (index) {
			case 0:
                strncpy(string_dest,"(signal) V/OCT_CV, (bang) trigger, (float) MIDI NOTE", ASSIST_STRING_MAXSIZE); break; // TODO: midi note, good idea?
            case 1:
                strncpy(string_dest,"(signal) Timbre_CV, (float) Timbre_POT", ASSIST_STRING_MAXSIZE); break;
            case 2:
                strncpy(string_dest,"(signal) Color_CV, (float) Color_POT", ASSIST_STRING_MAXSIZE); break;
            case 3:
                strncpy(string_dest,"(signal) Model_CV, (float) Model", ASSIST_STRING_MAXSIZE); break;
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
	this_class = class_new("vb.mi.brds~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);

    // timbre pots
    class_addmethod(this_class, (method)myObj_timbre,    "timbre",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_color,     "color",        A_FLOAT, 0);

    // general pots
    class_addmethod(this_class, (method)myObj_coarse,   "coarse",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_note,     "note",       A_FLOAT, 0);
//    class_addmethod(this_class, (method)myObj_set_scale,"scale",      A_LONG, 0);
//    class_addmethod(this_class, (method)myObj_shape,    "model",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_bang,     "bang",  0);
    class_addmethod(this_class, (method)myObj_float,    "float",    A_FLOAT, 0);

    
//    class_addmethod(this_class, (method)myObj_set_sampleRate,    "sr",      A_LONG, 0);
//    class_addmethod(this_class, (method)myObj_set_resolution,    "resolution", A_LONG, 0);
//    class_addmethod(this_class, (method)myObj_signature,    "signature",      A_LONG, 0);
	
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
    
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
    
    
    CLASS_ATTR_CHAR(this_class,"model", 0, t_myObj, shape);
    CLASS_ATTR_ENUMINDEX(this_class, "model", 0, "CSAW MORPH SAW_SQUARE SINE_TRIANGLE BUZZ SQUARE_SUB SAW_SUB SQUARE_SYNC SAW_SYNC TRIPLE_SAW TRIPLE_SQUARE TRIPLE_TRIANGLE TRIPLE_SINE TRIPLE_RING_MOD SAW_SWARM SAW_COMB TOY DIGITAL_FILTER_LP DIGITAL_FILTER_PK DIGITAL_FILTER_BP DIGITAL_FILTER_HP VOSIM VOWEL VOWEL_FOF HARMONICS FM FEEDBACK_FM CHAOTIC_FEEDBACK_FM PLUCKED BOWED BLOWN FLUTED STRUCK_BELL STRUCK_DRUM KICK CYMBAL SNARE WAVETABLES WAVE_MAP WAVE_LINE WAVE_PARAPHONIC FILTERED_NOISE TWIN_PEAKS_NOISE CLOCKED_NOISE GRANULAR_CLOUD PARTICLE_NOISE DIGITAL_MODULATION QUESTION_MARK");
    CLASS_ATTR_LABEL(this_class, "model", 0, "synthesis model");
    CLASS_ATTR_FILTER_CLIP(this_class, "model", 0, 47);
    CLASS_ATTR_SAVE(this_class, "model", 0);
    
    
    CLASS_ATTR_CHAR(this_class,"scale", 0, t_myObj, scale);
    CLASS_ATTR_ENUMINDEX(this_class, "scale", 0, "OFF SEMI IONI DORI PHRY LYDI MIXO AEOL LOCR BLU+ BLU- PEN+ PEN- FOLK JAPA GAME GYPS ARAB FLAM WHOL PYTH EB/4 E_/4 EA/4 BHAI GUNA MARW SHRI PURV BILA YAMA KAFI BHIM DARB RAGE KHAM MIMA PARA RANG GANG KAME PAKA NATB KAUN BAIR BTOD CHAN KTOD JOGE");
    CLASS_ATTR_LABEL(this_class, "scale", 0, "set scale");
    CLASS_ATTR_FILTER_CLIP(this_class, "scale", 0, 48);
    CLASS_ATTR_ACCESSORS(this_class, "scale", NULL, (method)scale_setter);
    CLASS_ATTR_SAVE(this_class, "scale", 0);

    
    object_post(NULL, "vb.mi.brds~ by volker böhm -- https://vboehm.net");
    object_post(NULL, "based on mutable instruments' 'braids' module");
}
