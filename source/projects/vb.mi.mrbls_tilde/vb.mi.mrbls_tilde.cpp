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


// a clone of mutable instruments' 'marbles' module for maxmsp
// by volker böhm, nov 2019, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/




#include "c74_msp.h"
#include <time.h>

#include "dsp.h"
#include "read_inputs.hpp"
#include "marbles/ramp/ramp_extractor.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"


//#include "marbles/io_buffer.h"      // still in here, because of the globals, vb
#include "marbles/resources.h"
#include "marbles/scale_recorder.h"
#include "marbles/settings.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/hysteresis_quantizer.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/gate_flags.h"

#ifdef __APPLE__
#include "Accelerate/Accelerate.h"
#endif

//#include <iostream>

#define MAX_NOTE_SIZE 256


using namespace c74::max;
using namespace marbles;


static t_class* this_class = nullptr;


marbles::Ratio y_divider_ratios[] = {
    { 1, 64 },
    { 1, 48 },
    { 1, 32 },
    { 1, 24 },
    { 1, 16 },
    { 1, 12 },
    { 1, 8 },
    { 1, 6 },
    { 1, 4 },
    { 1, 3 },
    { 1, 2 },
    { 1, 1 },
};


struct t_myObj {
    t_pxobject	obj;
    
    ReadInputs          read_inputs;
    Block               block;
    ScaleRecorder       scale_recorder;
    Settings            settings;
    RandomGenerator     random_generator;
    RandomStream        random_stream;
    TGenerator          t_generator;
    XYGenerator         xy_generator;
    GroupSettings       x, y;
    
    float               *voltages;
    float               *ramp_buffer;
    bool                *gates;
    
    bool                set_scale;
    bool                record_scale;
    short               gate_connected;
    short               clock_connected[2];     // observe if clock inputs receive a signal
    
    float               parameters[kNumParameters];
    int                 dejavu_length;      // the only one which has no cv input
    
    float               sr;
    float               y_divider;
    int                 sigvs;      // signal vector size
    void                *info_out;
};



void Init(t_myObj *self)
{
    self->settings.Init();
    
    //self->deja_vu_length_quantizer.Init();
    self->read_inputs.Init(self->settings.mutable_calibration_data());
    self->scale_recorder.Init();

   
//    self->random_generator.Init( rdtsc() );  // use time stamp counter as seed, good?
    self->random_generator.Init( (unsigned int)time(NULL) );
    self->random_stream.Init(&self->random_generator);
    self->t_generator.Init(&self->random_stream, self->sr);
    self->xy_generator.Init(&self->random_stream, self->sr);
    
    for (size_t i = 0; i < kNumScales; ++i) {
        self->xy_generator.LoadScale(i, self->settings.persistent_data().scale[i]);
    }
    
    // allocate memory
    self->voltages = (float*)sysmem_newptrclear(kBlockSize * 4 * sizeof(float));
    self->ramp_buffer = (float*)sysmem_newptrclear(kBlockSize * 4 * sizeof(float));
    self->gates = (bool*)sysmem_newptrclear(kBlockSize * 2 * sizeof(bool));
    
    // init paramters
    self->block.adc_value[ADC_CHANNEL_T_RATE] = 0.5f;
    self->block.adc_value[ADC_CHANNEL_T_BIAS] = 0.5f;
    self->block.adc_value[ADC_CHANNEL_T_JITTER] = 0.0f;
    self->block.adc_value[ADC_CHANNEL_DEJA_VU_AMOUNT] = 0.0f;
    self->block.adc_value[ADC_CHANNEL_DEJA_VU_LENGTH] = 0.0f;    // X_SPREAD_2
    self->block.adc_value[ADC_CHANNEL_X_SPREAD] = 0.5f;
    self->block.adc_value[ADC_CHANNEL_X_BIAS] = 0.5f;
    self->block.adc_value[ADC_CHANNEL_X_STEPS] = 0.5f;
    
    // x
    self->x.length = 5;
    self->x.ratio.p = 1;
    self->x.ratio.q = 1;
    
    // y generator init values
    self->y.bias = 0.5f;
    self->y.spread = 0.5f;
    self->y.steps = 0.0f;
    self->y.ratio = y_divider_ratios[6];
    self->y.control_mode = CONTROL_MODE_IDENTICAL;
    self->y.deja_vu = 0.f;
    self->y.length = 1;
    self->y.register_mode = false;
    self->y.register_value = 0.0f;
    self->y.voltage_range = VOLTAGE_RANGE_FULL;
    
}



#pragma mark --------- NEW -----------

void* myObj_new(t_symbol *s, long argc, t_atom *argv) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 10);
        
        self->info_out = outlet_new(self, NULL);
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        
        // init some params
        
        self->sr = sys_getsr();
        if(self->sr <= 0)
            self->sr = 44100.0f;
        self->sigvs = sys_getblksize();
        
        if(self->sigvs < kBlockSize) {
            object_error((t_object*)self,
                         "sigvs can't be smaller than %d samples\n", kBlockSize);
            object_free(self);
            self = NULL;
            return self;
        }
        
        
        self->set_scale = false;    // don't need this one
        self->record_scale = false;
        
        for(int i=0; i<kNumParameters; ++i)
            self->parameters[i] = 0.0f;
        

        Init(self);
        
        
        attr_args_process(self, argc, argv);            // process attributes

    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}


#pragma mark ---------------------------------



// plug / unplug patch chords...

void myObj_int(t_myObj *self, long m)
{
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            self->block.input_patched[0] = (m != 0);        // use external clock
            break;
        case 5:
            // set deja_vu sequence length
            m = clamp((int)m, 1, 16);
            self->dejavu_length = m;
            self->t_generator.set_length(m);
            self->x.length = m;
            break;
        case 9:
            self->block.input_patched[1] = (m != 0);        // use external clock
            break;
        default:
            break;
    }
}


// read float inputs at corresponding inlet as pot values
void myObj_float(t_myObj *self, double m)
{
    long innum = proxy_getinlet((t_object *)self);
    float *adc_value = self->block.adc_value;
    
    switch (innum) {
        case 1:
            // set rate
            adc_value[ADC_CHANNEL_T_RATE] = clamp(m, 0., 1.);
            break;
        case 2:
            // set bias
            adc_value[ADC_CHANNEL_T_BIAS] = clamp(m, 0., 1.);
            break;
        case 3:
            // jitter amount
            adc_value[ADC_CHANNEL_T_JITTER] = clamp(m, 0., 1.);
        case 4:
            // deja_vu amount
            adc_value[ADC_CHANNEL_DEJA_VU_AMOUNT] = clamp(m, 0., 1.);
            break;
        case 5:
            // deja_vu sequence length (round float to int)
//            int len = clamp(m, 1.0, 16.0) + 0.5;
//            self->dejavu_length = clamp(m, 1.0, 16.0) + 0.5;
//            myObj_length(self, (long)m);
            break;
        case 6:
            // set x_spread
            adc_value[ADC_CHANNEL_X_SPREAD] = clamp(m, 0., 1.);
            break;
        case 7:
            // set x_bias
            adc_value[ADC_CHANNEL_X_BIAS] = clamp(m, 0., 1.);
            break;
        case 8:
            // set x_steps
            adc_value[ADC_CHANNEL_X_STEPS] = clamp(m, 0., 1.);
            break;
        default:
            break;
    }

}

void myObj_info(t_myObj *self)
{
    float *p = self->parameters;
    object_post((t_object*)self, "PARAMETERS:::::::");
    object_post((t_object*)self, "dejavu amount: \t%f", p[ADC_CHANNEL_DEJA_VU_AMOUNT]);
    object_post((t_object*)self, "dejavu length: \t%d", self->dejavu_length);
    object_post((t_object*)self, "x_ext_input: \t%f", p[ADC_CHANNEL_X_SPREAD_2]);
    object_post((t_object*)self, "t_rate: \t%f", (p[ADC_CHANNEL_T_RATE]+60.f)/120.0f);
    float bpm = powf(2.0f, p[ADC_CHANNEL_T_RATE] / 12.0f) * 120.f;
    object_post((t_object*)self, "bpm: \t%f", bpm);
    object_post((t_object*)self, "t_bias: \t%f", p[ADC_CHANNEL_T_BIAS]);
    object_post((t_object*)self, "t_jitter: \t%f", p[ADC_CHANNEL_T_JITTER]);
    object_post((t_object*)self, "x_spread: \t%f", p[ADC_CHANNEL_X_SPREAD]);
    object_post((t_object*)self, "x_bias: \t%f", p[ADC_CHANNEL_X_BIAS]);
    object_post((t_object*)self, "x_steps: \t%f", p[ADC_CHANNEL_X_STEPS]);
    
    object_post((t_object*)self, ".......");

}


#pragma mark ----------- scales --------------


void myObj_record_scale(t_myObj *self, long a)
{
    if(a != 0) {
        self->record_scale = true;
        self->scale_recorder.Clear();
    }
    else {
        int scale_index = self->settings.state().x_scale;
        self->record_scale = false;
        bool success = self->scale_recorder.ExtractScale(
                                                   self->settings.mutable_scale(scale_index));
        if (success) {
            self->settings.set_dirty_scale_index(scale_index);
            object_post((t_object *)self, "extracting scale done! scale index: %d", scale_index);
        }
        else {
            object_warn((t_object *)self, "extracting scale failed!");
        }
    }

}


void myObj_set_scale(t_myObj *self, t_symbol *mess, short argc, t_atom *argv) {
    
    float noteList[MAX_NOTE_SIZE];
    int scale_index = self->settings.state().x_scale;

    if(argc > MAX_NOTE_SIZE) argc = MAX_NOTE_SIZE;
    else if(argc == 0) {
        self->settings.ResetScale(scale_index);
        object_post(NULL, "resetting scale: %d", scale_index);
        self->settings.set_dirty_scale_index(scale_index);
        return;
    }
    // copy input list
    for(int i=0; i<argc; i++, argv++) {
        if(atom_gettype(argv) == A_LONG)
            noteList[i] = static_cast<float>(atom_getlong(argv));
        else if(atom_gettype(argv) == A_FLOAT)
            noteList[i] = atom_getfloat(argv);
        else
            object_error((t_object *)self, "only lists of ints or floats are accepted");
    }
    
    self->scale_recorder.Clear();
    
    // .. do the work here
    float note_input;
    for(int i=0; i<argc; ++i) {
        note_input = noteList[i];
        //object_post(NULL, "note_input: %f", note_input);
        //float u = 0.5f * (note_input + 1.0f);
        //float voltage = (u - 0.5f) * 10.0f;
        //object_post(NULL, "note_input: %f -- voltage: %f", note_input, voltage);
        float voltage = note_input / 12.0f;
        self->scale_recorder.NewNote(voltage);
        self->scale_recorder.UpdateVoltage(voltage);
        self->scale_recorder.AcceptNote();
    }
    
    bool success = self->scale_recorder.ExtractScale(
                            self->settings.mutable_scale(scale_index));
    if (success) {
        self->settings.set_dirty_scale_index(scale_index);
        object_post((t_object *)self, "set scale success!");
    }
}



void myObj_scale_info(t_myObj *self) {
    int scale_index = self->settings.state().x_scale;
    Scale *scale = self->settings.mutable_scale(scale_index);
    int numDeg = scale->num_degrees;
    
    object_post((t_object*)self, "scale_index: %d",scale_index);
    for(int i=0; i<numDeg; ++i) {
        Degree d = scale->degree[i];
        object_post((t_object*)self, "v: %f \tw: %d", d.voltage*12.0f, d.weight);
    }
    object_post((t_object*)self, "----------");
}


#pragma mark ------- button switches ----------

void myObj_t(t_myObj *self, long b) {
    State* state = self->settings.mutable_state();
//    state->t_deja_vu = (b != 0);
    switch (b) {
        case 0:
            state->t_deja_vu = DEJA_VU_OFF;
            break;
        case 1:
            state->t_deja_vu = DEJA_VU_ON;
            break;
        case 2:
            state->t_deja_vu = DEJA_VU_LOCKED;
            break;
        default:
            state->t_deja_vu = DEJA_VU_OFF;
    }
}

void myObj_x(t_myObj *self, long b) {
    State* state = self->settings.mutable_state();
//    state->x_deja_vu = (b != 0);
    switch (b) {
        case 0:
            state->x_deja_vu = DEJA_VU_OFF;
            break;
        case 1:
            state->x_deja_vu = DEJA_VU_ON;
            break;
        case 2:
            state->x_deja_vu = DEJA_VU_LOCKED;
            break;
        default:
            state->x_deja_vu = DEJA_VU_OFF;
    }
}


//void myObj_t_model(t_myObj *self, long b) {
//    CONSTRAIN(b, 0, 2);
//    self->settings.mutable_state()->t_model = b;
//    self->t_generator.set_model(TGeneratorModel(b));
//
//}

t_max_err t_model_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getcharfix(av);
        self->settings.mutable_state()->t_model = m;
        self->t_generator.set_model(TGeneratorModel(m));
    }
    
    return MAX_ERR_NONE;
}


//void myObj_t_range(t_myObj *self, long b) {
//    CONSTRAIN(b, 0, 2);
//    self->settings.mutable_state()->t_range = b;
//    self->t_generator.set_range(TGeneratorRange(b));
//}

t_max_err t_range_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getcharfix(av);
        self->settings.mutable_state()->t_range = m;
        self->t_generator.set_range(TGeneratorRange(m));
    }
    
    return MAX_ERR_NONE;
}


//void myObj_x_range(t_myObj *self, long b) {
//    CONSTRAIN(b, 0, 2);
//    self->settings.mutable_state()->x_range = b;
//
//    for (size_t i = 0; i < kNumXChannels; ++i) {
//        OutputChannel& channel = self->xy_generator.output_channel_[i];
//
//        // TODO: add more ranges?
//        switch (b) {
//            case VOLTAGE_RANGE_NARROW:
//                channel.set_scale_offset(ScaleOffset(2.0f, 0.0f));
//                break;
//
//            case VOLTAGE_RANGE_POSITIVE:
//                channel.set_scale_offset(ScaleOffset(5.0f, 0.0f));
//                break;
//
//            case VOLTAGE_RANGE_FULL:
//                channel.set_scale_offset(ScaleOffset(10.0f, -5.0f));
//                break;
//
//            default:
//                break;
//        }
//    }
//}

t_max_err x_range_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getcharfix(av);
        self->settings.mutable_state()->x_range = m;
        
        for (size_t i = 0; i < kNumXChannels; ++i) {
            OutputChannel& channel = self->xy_generator.output_channel_[i];
            
            // TODO: add more ranges?
            switch (m) {
                case VOLTAGE_RANGE_NARROW:
                    channel.set_scale_offset(ScaleOffset(2.0f, 0.0f));
                    break;
                    
                case VOLTAGE_RANGE_POSITIVE:
                    channel.set_scale_offset(ScaleOffset(5.0f, 0.0f));
                    break;
                    
                case VOLTAGE_RANGE_FULL:
                    channel.set_scale_offset(ScaleOffset(10.0f, -5.0f));
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    return MAX_ERR_NONE;
}

// controls how the outputs x1, x1 and x3 react to the pots of the X section
//void myObj_x_mode(t_myObj *self, long b) {
//    // 0: All channels follow the settings on the control panel.
//    // 1: X2 follows the control panel, while X1 and X3 take opposite values
//    // 2: X3 follows the control panel, X1 reacts in the opposite direction, and X2 always stays in the middle (steppy, unbiased, bell-curve)
//
//    CONSTRAIN(b, 0, 2);
//    self->settings.mutable_state()->x_control_mode = b;
//    self->x.control_mode = ControlMode(b);
//}

t_max_err x_mode_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    // 0: All channels follow the settings on the control panel.
    // 1: X2 follows the control panel, while X1 and X3 take opposite values
    // 2: X3 follows the control panel, X1 reacts in the opposite direction, and X2 always stays in the middle (steppy, unbiased, bell-curve)
    if (ac && av) {
        t_atom_long m = atom_getcharfix(av);
        self->settings.mutable_state()->x_control_mode = m;
        self->x.control_mode = ControlMode(m);
    }
    
    return MAX_ERR_NONE;
}


// choose a scale
//void myObj_x_scale(t_myObj *self, long b) {
//    int max = kNumScales-1;
//    CONSTRAIN(b, 0, max);
//    self->settings.mutable_state()->x_scale = b;
//    self->x.scale_index = self->y.scale_index = b;
//}

t_max_err x_scale_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getcharfix(av);
        self->settings.mutable_state()->x_scale = m;
        self->x.scale_index = self->y.scale_index = m;
    }
    
    return MAX_ERR_NONE;
}



// enable sampling of external cv input
void myObj_x_ext(t_myObj *self, long b) {
    bool a = (b != 0);
    self->settings.mutable_state()->x_register_mode = a;
    self->x.register_mode = a;
}




#pragma mark -------- time pots --------

void myObj_rate(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->block.adc_value[ADC_CHANNEL_T_RATE] = m;
}

void myObj_bpm(t_myObj* self, double m) {
    double bpm = clamp(m, 10., 800.);
    bpm *= 0.008333;   // / 120.0
    float rate = (log2(bpm) * 12.0 + 60) * 0.008333;
//    object_post(NULL, "t_rate: %f", rate);
    
    self->block.adc_value[ADC_CHANNEL_T_RATE] = rate;
}


void myObj_t_bias(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->block.adc_value[ADC_CHANNEL_T_BIAS] = m;
}

void myObj_jitter(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->block.adc_value[ADC_CHANNEL_T_JITTER] = m;
}


// set pulse width of t1 and t3 gates

void myObj_pulse_width_mean(t_myObj *self, double m) {
    m = clamp(m, 0.0, 1.0);
    self->t_generator.set_pulse_width_mean(m);
}

void myObj_pulse_width_std(t_myObj *self, double m) {
    m = clamp(m, 0.0, 1.0);
    self->t_generator.set_pulse_width_std(m);
}



#pragma mark -------- seq pots --------

void myObj_dejavu(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->block.adc_value[ADC_CHANNEL_DEJA_VU_AMOUNT] = m;
}
/*
void myObj_length(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    //block.adc_value[ADC_CHANNEL_DEJA_VU_LENGTH] = m;
    self->dejavu_length = m;
}
*/
void myObj_length(t_myObj* self, long m) {
    m = clamp((int)m, 1, 16);
    self->dejavu_length = m;
    
    self->t_generator.set_length(m);
    self->x.length = m;
}


#pragma mark -------- X pots --------

void myObj_spread(t_myObj* self, double m) {
    m = clamp(m, 0., 1.0);
    self->block.adc_value[ADC_CHANNEL_X_SPREAD] = m;
}


void myObj_steps(t_myObj* self, double m) {
    m = clamp(m, 0., 1.0);
    self->block.adc_value[ADC_CHANNEL_X_STEPS] = m;
}


void myObj_xbias(t_myObj* self, double m) {
    m = clamp(m, 0., 1.0);
    self->block.adc_value[ADC_CHANNEL_X_BIAS] = m;
}


#pragma mark -------- Y functions --------

//void myObj_y_divider(t_myObj* self, double m) {
//    m = clamp(m, 0., 1.);
//    int div = int(m*255.0 + 0.5);
//    self->settings.mutable_state()->y_divider = div;
//}


t_max_err y_div_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_float m = atom_getfloat(av);
        self->y_divider = m;
        self->y.ratio =
            y_divider_ratios[static_cast<uint16_t>((m * 11.0) + 0.5)];
        
    }
    
    return MAX_ERR_NONE;
}


#pragma mark ----- dsp loop -----


void dsp_loop(t_myObj* self, double** ins, double** outs, long blockSize, long offset)
{
    
    size_t size = blockSize;
    
    float   *parameters = self->parameters;
    float   *ramp_buffer = self->ramp_buffer;
    Block   *block = &self->block;
    ScaleRecorder   *scale_recorder = &self->scale_recorder;
    Ramps           ramps;
    GroupSettings   x = self->x;
    GroupSettings   y = self->y;
    TGenerator      *t_generator = &self->t_generator;
    
    
    // 7 cv inputs (inlets 1 - 7)
    // 2 clock/audio inputs (inlets 0 and 8)
    
    stmlib::GateFlags* t_clock = block->input[0];
    stmlib::GateFlags* xy_clock = block->input[1];
    
    // just copy first value from cv inputs
    for(int i=0; i<ADC_CHANNEL_LAST; ++i)
        block->adc_value[i+ADC_GROUP_CV] = ins[i+1][offset];
    
    // apply scaling for cv rate input
    block->adc_value[ADC_CHANNEL_T_RATE + ADC_GROUP_CV] *= 60.0f;
    
    
    // clock input ----------------
    // Determine the clock source for the XY section
    ClockSource xy_clock_source = CLOCK_SOURCE_INTERNAL_T1_T2_T3;
    //ClockSource xy_clock_source = CLOCK_SOURCE_INTERNAL_T1;
    uint8_t inClocks[2] = {0,0};
    double vectorsum, *clock_input;
    
    if(self->clock_connected[0] && block->input_patched[0]) {
        vectorsum = 0.0;
        clock_input = ins[0]+offset;
#ifdef __APPLE__
        vDSP_sveD(clock_input, 1, &vectorsum, size);
#else
        for(int i=0; i<size; ++i)
            vectorsum += clock_input[i];
#endif
        if(vectorsum > 0.5) inClocks[0] = 255;
    }
    if(self->clock_connected[1] && block->input_patched[1]) {
        xy_clock_source = CLOCK_SOURCE_EXTERNAL;
        vectorsum = 0.0;
        clock_input = ins[ADC_CHANNEL_LAST+1]+offset;
#ifdef __APPLE__
        vDSP_sveD(clock_input, 1, &vectorsum, size);
#else
        for(int i=0; i<size; ++i)
            vectorsum += clock_input[i];
#endif
        if(vectorsum > 0.5) {
            inClocks[1] = 255;
            //object_post(NULL, "ping! - clockSrc: %d", xy_clock_source);
        }
    }
    
    self->read_inputs.ReadClocks(block, size, inClocks);
    
    // Filter CV values (3.5%)
    // get 8 pots and 8 cv inputs and process them into parameters
    self->read_inputs.Process(&block->adc_value[0], parameters);
    
    float deja_vu = parameters[ADC_CHANNEL_DEJA_VU_AMOUNT];
    
    //  Deadband near 12 o'clock for the deja vu parameter.
    if (deja_vu < 0.47f) {
        deja_vu *= 1.06382978723f;
    } else if (deja_vu > 0.53f) {
        deja_vu = 0.5f + (deja_vu - 0.53f) * 1.06382978723f;
    } else {
        deja_vu = 0.5f;
    }

    
    
    // Generate gates for T-section (16%).
    ramps.master = &ramp_buffer[0];
    ramps.external = &ramp_buffer[kBlockSize];
    ramps.slave[0] = &ramp_buffer[kBlockSize * 2];
    ramps.slave[1] = &ramp_buffer[kBlockSize * 3];
    
    const State& state = self->settings.state();

    
    // TODO: move this out of dsp loop
//    int deja_vu_length = self->dejavu_length;
    
//    t_generator->set_model(TGeneratorModel(state.t_model));
//    t_generator->set_range(TGeneratorRange(state.t_range));
    t_generator->set_rate(parameters[ADC_CHANNEL_T_RATE]);
    t_generator->set_bias(parameters[ADC_CHANNEL_T_BIAS]);
    t_generator->set_jitter(parameters[ADC_CHANNEL_T_JITTER]);
    t_generator->set_deja_vu(state.t_deja_vu == DEJA_VU_LOCKED
                             ? 0.5f
                             : (state.t_deja_vu == DEJA_VU_ON ? deja_vu : 0.0f));
//    t_generator->set_length(deja_vu_length);
    
    t_generator->Process(
                        block->input_patched[0],
                        t_clock,
                        ramps,
                        self->gates,
                        size);

    
    // only take external voltages from x_spread_2 input, vb
    float note_cv = self->read_inputs.channel(ADC_CHANNEL_X_SPREAD_2).scaled_raw_cv();
    float u = 0.5f * (note_cv + 1.0f);
    
    //object_post(NULL, "cv: %f -- u: %f", note_cv, u);
    

    if (self->record_scale) {
        float voltage = (u - 0.5f) * 10.0f;
        for (size_t i = 0; i < size; ++i) {
            stmlib::GateFlags gate = block->input_patched[1]
            ? block->input[1][i]
            : stmlib::GATE_FLAG_LOW;
            if (gate & stmlib::GATE_FLAG_RISING) {
                scale_recorder->NewNote(voltage);
                object_post((t_object*)self, "new note: %f", voltage);
            }
            if (gate & stmlib::GATE_FLAG_HIGH) {
                scale_recorder->UpdateVoltage(voltage);
            }
            if (gate & stmlib::GATE_FLAG_FALLING) {
                scale_recorder->AcceptNote();
            }
        }
        std::fill(&self->voltages[0], &self->voltages[4 * size], voltage);
    } else {
//        x.control_mode = ControlMode(state.x_control_mode);
        //x.voltage_range = VoltageRange(state.x_range % 3);
//        x.register_mode = state.x_register_mode;
        x.register_value = u;
        
        x.spread = parameters[ADC_CHANNEL_X_SPREAD];
        x.bias = parameters[ADC_CHANNEL_X_BIAS];
        x.steps = parameters[ADC_CHANNEL_X_STEPS];
        x.deja_vu = state.x_deja_vu == DEJA_VU_LOCKED
            ? 0.5f
            : (state.x_deja_vu == DEJA_VU_ON ? deja_vu : 0.0f);
//        x.length = deja_vu_length;
//        x.ratio.p = 1;
//        x.ratio.q = 1;
        
//        y.control_mode = CONTROL_MODE_IDENTICAL;
//        y.voltage_range = VoltageRange(state.y_range);      // TODO: we never set this manually ?
//        y.register_mode = false;
//        y.register_value = 0.0f;
////        y.spread = float(state.y_spread) / 256.0f;
////        y.bias = float(state.y_bias) / 256.0f;
////        y.steps = float(state.y_steps) / 256.0f;
//        y.spread = state.y_spread;
//        y.bias = state.y_bias;
//        y.steps = state.y_steps;
//        y.deja_vu = 0.0f;
//        y.length = 1;
////        y.ratio = y_divider_ratios[
////                                   static_cast<uint16_t>(state.y_divider) * 12 >> 8];
//        y.ratio = y_divider_ratios[
//                                   static_cast<uint16_t>((state.y_divider * 11.0) + 0.5)];
        
        if (self->settings.dirty_scale_index() != -1) {
            int i = self->settings.dirty_scale_index();
            object_post(NULL, "dirtyscale! reload scale: %d", i);
            self->xy_generator.LoadScale(i, self->settings.persistent_data().scale[i]);
            self->settings.set_dirty_scale_index(-1);
        }
        
//        y.scale_index = x.scale_index = state.x_scale;
        
        self->xy_generator.Process(
                             xy_clock_source,
                             x,
                             y,
                             xy_clock,
                             ramps,
                             self->voltages,
                             size);
    }
    
    const float* v = self->voltages;
    const bool* g = self->gates;

    for (size_t i = 0; i < size; ++i) {
        int idx = i + offset;
        outs[0][idx] = *g++;  // t1
        outs[1][idx] = ramps.master[i] < 0.5f;    // t2
        outs[2][idx] = *g++;  // t3
        outs[4][idx] = *v++;  // X1
        outs[5][idx] = *v++;  // X2
        outs[6][idx] = *v++;  // X3
        outs[3][idx] = *v++;  //  y
    }

}

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    if (self->obj.z_disabled)
        return;

    for(int count = 0; count < sampleframes; count += kBlockSize)
        dsp_loop(self, ins, outs, kBlockSize, count);
    

}



void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    self->clock_connected[0] = count[0];
    self->clock_connected[1] = count[ADC_CHANNEL_LAST+1];
    
    if(maxvectorsize < kBlockSize) {
        object_error((t_object*)self, "sgivs can't be smaller than %d samples, sorry!", kBlockSize);
        return;
    }
    self->sigvs = maxvectorsize;
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        //delete self->t_generator;
        self->t_generator.Init(&self->random_stream, self->sr);
        self->xy_generator.Init(&self->random_stream, self->sr);
    }

    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);

    if(self->voltages)
        sysmem_freeptr(self->voltages);
    if(self->ramp_buffer)
        sysmem_freeptr(self->ramp_buffer);
    if(self->gates)
        sysmem_freeptr(self->gates);
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal/float) external clock(1)", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal/float) T_RATE", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal/float) T_BIAS", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal/float) T_JITTER",
                 ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal/float) DEJA_VU_AMOUNT", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal) external sampling source, (int) deja_vu length (1-16)", ASSIST_STRING_MAXSIZE);
                break;
            case 6:
                strncpy(string_dest,"(signal/float) X_SPREAD", ASSIST_STRING_MAXSIZE);
                break;
            case 7:
                strncpy(string_dest,"(signal/float) X_BIAS", ASSIST_STRING_MAXSIZE);
                break;
            case 8:
                strncpy(string_dest,"(signal/float) X_STEPS", ASSIST_STRING_MAXSIZE);
                break;
            case 9:
                strncpy(string_dest,"(signal) external clock(2) - for XY outputs", ASSIST_STRING_MAXSIZE);
                break;
        }
    }
    else if (io == ASSIST_OUTLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) t1", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) t2", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) t3", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) y", ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal) X1", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal) X2", ASSIST_STRING_MAXSIZE);
                break;
            case 6:
                strncpy(string_dest,"(signal) X3", ASSIST_STRING_MAXSIZE);
                break;
            case 7:
                strncpy(string_dest,"info outlet", ASSIST_STRING_MAXSIZE);
                break;
        }
    }
}


void ext_main(void* r) {
    this_class = class_new("vb.mi.mrbls~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);
    
    class_addmethod(this_class, (method)myObj_rate,  "rate",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_bpm,  "bpm",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_t_bias,	"t_bias",         A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_length,     "length",        A_LONG, 0);
    class_addmethod(this_class, (method)myObj_jitter,     "jitter",    A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_spread,   "spread",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_xbias,   "xbias",    A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_steps,   "steps",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_dejavu,  "dejavu",  A_FLOAT, 0);
    
//    class_addmethod(this_class, (method)myObj_t_model,  "model",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_t,  "t",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_x,  "x",  A_LONG, 0);
//    class_addmethod(this_class, (method)myObj_t_range,  "t_range",  A_LONG, 0);
//    class_addmethod(this_class, (method)myObj_x_range,  "x_range",  A_LONG, 0);
//    class_addmethod(this_class, (method)myObj_pulse_width_mean,  "pw_mean",    A_FLOAT, 0);
//    class_addmethod(this_class, (method)myObj_pulse_width_std,  "pw_std",      A_FLOAT, 0);
    
//    class_addmethod(this_class, (method)myObj_x_mode,  "x_mode",  A_LONG, 0);
//    class_addmethod(this_class, (method)myObj_x_scale,  "x_scale",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_x_ext,  "x_ext",  A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_int,  "int",          A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,  "float",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_set_scale,  "set_scale",      A_GIMME, 0);
    class_addmethod(this_class, (method)myObj_record_scale,  "record_scale",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_info,	"info", 0);
    class_addmethod(this_class, (method)myObj_scale_info,    "scale_info", 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    
    // attributes
    CLASS_ATTR_CHAR(this_class, "t_model", 0, t_myObj, settings.mutable_state()->t_model);
    CLASS_ATTR_ENUMINDEX(this_class, "t_model", 0, "coin divide pattern");
    CLASS_ATTR_FILTER_CLIP(this_class, "t_model", 0, 2);
    CLASS_ATTR_LABEL(this_class, "t_model", 0, "randomness model");
    CLASS_ATTR_ACCESSORS(this_class, "t_model", NULL, (method)t_model_setter);
    CLASS_ATTR_SAVE(this_class, "t_model", 0);
    
    CLASS_ATTR_CHAR(this_class, "t_range", 0, t_myObj, settings.mutable_state()->t_range);
    CLASS_ATTR_ENUMINDEX(this_class, "t_range", 0, "/4 x1 x4");
    CLASS_ATTR_FILTER_CLIP(this_class, "t_range", 0, 2);
    CLASS_ATTR_LABEL(this_class, "t_range", 0, "clock division");
    CLASS_ATTR_ACCESSORS(this_class, "t_range", NULL, (method)t_range_setter);
    CLASS_ATTR_SAVE(this_class, "t_range", 0);
    
    
    CLASS_ATTR_CHAR(this_class, "x_mode", 0, t_myObj, settings.mutable_state()->x_control_mode);
    CLASS_ATTR_ENUMINDEX(this_class, "x_mode", 0, "identical bump tilt");
    CLASS_ATTR_FILTER_CLIP(this_class, "x_mode", 0, 2);
    CLASS_ATTR_LABEL(this_class, "x_mode", 0, "x section behavior");
    CLASS_ATTR_ACCESSORS(this_class, "x_mode", NULL, (method)x_mode_setter);
    CLASS_ATTR_SAVE(this_class, "x_mode", 0);
    
    CLASS_ATTR_CHAR(this_class, "x_range", 0, t_myObj, settings.mutable_state()->x_range);
    CLASS_ATTR_ENUMINDEX(this_class, "x_range", 0, "+2 +5 ±5");
    CLASS_ATTR_FILTER_CLIP(this_class, "x_range", 0, 2);
    CLASS_ATTR_LABEL(this_class, "x_range", 0, "x output range");
    CLASS_ATTR_ACCESSORS(this_class, "x_range", NULL, (method)x_range_setter);
    CLASS_ATTR_SAVE(this_class, "x_range", 0);
    
    CLASS_ATTR_CHAR(this_class, "x_scale", 0, t_myObj, settings.mutable_state()->x_scale);
    CLASS_ATTR_ENUMINDEX(this_class, "x_scale", 0, "major minor penta gamelan bhairav shree");
    CLASS_ATTR_FILTER_CLIP(this_class, "x_scale", 0, 5);
    CLASS_ATTR_LABEL(this_class, "x_scale", 0, "x output range");
    CLASS_ATTR_ACCESSORS(this_class, "x_scale", NULL, (method)x_scale_setter);
    CLASS_ATTR_SAVE(this_class, "x_scale", 0);
    

    CLASS_ATTR_FLOAT(this_class, "y_div", 0, t_myObj, y_divider);
    CLASS_ATTR_FILTER_CLIP(this_class, "y_div", 0.0, 1.0);
    CLASS_ATTR_ACCESSORS(this_class, "y_div", NULL, (method)y_div_setter);
    CLASS_ATTR_SAVE(this_class, "y_div", 0);
    
    CLASS_ATTR_FLOAT(this_class, "y_spread", 0, t_myObj, y.spread);
    CLASS_ATTR_FILTER_CLIP(this_class, "y_spread", 0.0, 1.0);
    CLASS_ATTR_SAVE(this_class, "y_spread", 0);
    
    CLASS_ATTR_FLOAT(this_class, "y_bias", 0, t_myObj, y.bias);
    CLASS_ATTR_FILTER_CLIP(this_class, "y_bias", 0.0, 1.0);
    CLASS_ATTR_SAVE(this_class, "y_bias", 0);
    
    CLASS_ATTR_FLOAT(this_class, "y_steps", 0, t_myObj, y.steps);
    CLASS_ATTR_FILTER_CLIP(this_class, "y_steps", 0.0, 1.0);
    CLASS_ATTR_SAVE(this_class, "y_steps", 0);
    
    object_post(NULL, "vb.mi.mrbls~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'marbles' module");
}
