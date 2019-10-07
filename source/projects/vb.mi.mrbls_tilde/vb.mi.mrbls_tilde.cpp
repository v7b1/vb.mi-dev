#include "c74_msp.h"

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

#include "Accelerate/Accelerate.h"

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

int loop_length[] = {
    1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8,
    10, 10, 10,
    12, 12, 12, 12, 12, 12, 12,
    14,
    16
};



struct t_myObj {
    t_pxobject	obj;
    
    ReadInputs          read_inputs;
    Block               block;
    ScaleRecorder       scale_recorder;
    stmlib::HysteresisQuantizer deja_vu_length_quantizer;
    Settings            settings;
    RandomGenerator     random_generator;
    RandomStream        random_stream;
    TGenerator          t_generator;
    XYGenerator         xy_generator;
    
    float               *voltages;
    float               *ramp_buffer;
    bool                *gates;
    
    bool                set_scale;
    bool                record_scale;
    short               gate_connected;
    short               clock_connected[2];     // observe if clock inputs receive a signal
    
    float               parameters[kNumParameters];
    float               dejavu_length;      // the only one which has no cv input
    
    float               sr;
    int                 sigvs;      // signal vector size
    short               blockCounter;
};

unsigned long long rdtsc() {
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((unsigned long long)hi << 32) | lo;
}

void Init(t_myObj *self)
{
    self->settings.Init();
    
    self->deja_vu_length_quantizer.Init();
    self->read_inputs.Init(self->settings.mutable_calibration_data());
    self->scale_recorder.Init();

   
    self->random_generator.Init( rdtsc() );  // use time stamp counter as seed, good?
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
    
}



#pragma mark --------- NEW -----------

void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 10);
        
        outlet_new(self, "signal");
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
        
        self->set_scale = false;    // don't need this one
        self->record_scale = false;
        
        for(int i=0; i<kNumParameters; ++i)
            self->parameters[i] = 0.0f;
        

        Init(self);
        

    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}


#pragma mark ---------------------------------



// plug / unplug patch chords...

void myObj_int(t_myObj *self, long value)
{
    long innum = proxy_getinlet((t_object *)self);
    
    switch (innum) {
        case 0:
            self->block.input_patched[0] = (value != 0);        // use external clock
            //object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 1:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 2:
            //self->modulations.frequency_patched = value != 0;
            break;
        case 3:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 9:
            self->block.input_patched[1] = (value != 0);        // use external clock
            break;
        default:
            break;
    }
}


// read float inputs at corresponding inlet as attenuverter levels
void myObj_float(t_myObj *self, double f)
{
    long innum = proxy_getinlet((t_object *)self);
    //object_post(NULL, "innum: %ld", innum);
    f = clamp(f, -1., 1.);
    
    switch (innum) {

        default:
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
    }

}


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
            object_post((t_object *)self, "set scale success!");
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
        object_post(NULL, "note_input: %f", note_input);
        float u = 0.5f * (note_input + 1.0f);
        float voltage = (u - 0.5f) * 10.0f;
        self->scale_recorder.NewNote(voltage);
        self->scale_recorder.AcceptNote();
    }
    
    bool success = self->scale_recorder.ExtractScale(
                            self->settings.mutable_scale(scale_index));
    if (success) {
        self->settings.set_dirty_scale_index(scale_index);
        object_post((t_object *)self, "set scale success!");
    }
}



void myObj_bang(t_myObj *self) {
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
    state->t_deja_vu = (b != 0);
}

void myObj_x(t_myObj *self, long b) {
    State* state = self->settings.mutable_state();
    state->x_deja_vu = (b != 0);
}


void myObj_t_model(t_myObj *self, long b) {
    //b = clamp(b, 0, 2);
    State* state = self->settings.mutable_state();
    if(b>2) b = 2;
    else if(b<0) b = 0;
    state->t_model = b;
}


void myObj_t_range(t_myObj *self, long b) {
    State* state = self->settings.mutable_state();
    if(b>2) b = 2;
    else if(b<0) b = 0;
    state->t_range = b;
}


void myObj_x_range(t_myObj *self, long b) {
    State* state = self->settings.mutable_state();
    if(b>2) b = 2;
    else if(b<0) b = 0;
    state->x_range = b;
}

void myObj_x_mode(t_myObj *self, long b) {
    State* state = self->settings.mutable_state();
    if(b>2) b = 2;
    else if(b<0) b = 0;
    state->x_control_mode = b;
}

void myObj_x_scale(t_myObj *self, long b) {
    State* state = self->settings.mutable_state();
    int max = kNumScales-1;
    if(b>max) b = max;
    else if(b<0) b = 0;
    state->x_scale = b;
}


void myObj_x_ext(t_myObj *self, long b) {
    State* state = self->settings.mutable_state();
    state->x_register_mode = (b != 0);
}

void myObj_info(t_myObj *self)
{
    float *p = self->parameters;
    object_post((t_object*)self, "PARAMETERS:::::::");
    object_post((t_object*)self, "dejavu amount: \t%f", p[ADC_CHANNEL_DEJA_VU_AMOUNT]);
    object_post((t_object*)self, "dejavu length: \t%f", self->dejavu_length);
    object_post((t_object*)self, "x_ext_input: \t%f", p[ADC_CHANNEL_X_SPREAD_2]);
    object_post((t_object*)self, "t_rate: \t%f", p[ADC_CHANNEL_T_RATE]);
    float bpm = powf(2.0f, p[ADC_CHANNEL_T_RATE] / 12.0f) * 120.f;
    object_post((t_object*)self, "bpm: \t%f", bpm);
    object_post((t_object*)self, "t_bias: \t%f", p[ADC_CHANNEL_T_BIAS]);
    object_post((t_object*)self, "t_jitter: \t%f", p[ADC_CHANNEL_T_JITTER]);
    object_post((t_object*)self, "x_spread: \t%f", p[ADC_CHANNEL_X_SPREAD]);
    object_post((t_object*)self, "x_bias: \t%f", p[ADC_CHANNEL_X_BIAS]);
    object_post((t_object*)self, "x_steps: \t%f", p[ADC_CHANNEL_X_STEPS]);
    
    
    object_post((t_object*)self, ".......");
    
}




#pragma mark -------- time pots --------

void myObj_rate(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->block.adc_value[ADC_CHANNEL_T_RATE] = m;
}

void myObj_bpm(t_myObj* self, double m) {
    float bpm = clamp(m, 10., 800.);
    bpm *= 0.008333f;   // / 120.0
    float rate = log10f(bpm) * 39.863137f;    // 12.0/log10(2)
    //object_post(NULL, "t_rate: %f", rate);
    
    rate += 60.0f;
    rate *= 0.008333f;       // / 120.0
    //object_post(NULL, "rate input: %f", rate);
    
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


#pragma mark -------- seq pots --------

void myObj_dejavu(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    self->block.adc_value[ADC_CHANNEL_DEJA_VU_AMOUNT] = m;
}

void myObj_length(t_myObj* self, double m) {
    m = clamp(m, 0., 1.);
    //block.adc_value[ADC_CHANNEL_DEJA_VU_LENGTH] = m;
    self->dejavu_length = m;
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





#pragma mark ----- dsp loop -----



void dsp_loop(t_myObj* self, double** ins, double** outs, long blockSize, long offsetCount)
{
    
    size_t size = blockSize;
    
    float   *parameters = self->parameters;
    float   *ramp_buffer = self->ramp_buffer;
    Block   *block = &self->block;
    ScaleRecorder   *scale_recorder = &self->scale_recorder;
    Ramps           ramps;
    GroupSettings   x, y;
    TGenerator      *t_generator = &self->t_generator;
    
    int offset = offsetCount * blockSize;

    
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
    uint8_t inClocks[2] = {0,0};
    double vectorsum, *clock_input;
    
    if(self->clock_connected[0] && block->input_patched[0]) {
        vectorsum = 0.0;
        clock_input = ins[0]+offset;
        vDSP_sveD(clock_input, 1, &vectorsum, size);
        if(vectorsum > 0.5) inClocks[0] = 255;
    }
    if(self->clock_connected[1] && block->input_patched[1]) {
        xy_clock_source = CLOCK_SOURCE_EXTERNAL;
        vectorsum = 0.0;
        clock_input = ins[ADC_CHANNEL_LAST+1]+offset;
        vDSP_sveD(clock_input, 1, &vectorsum, size);
        if(vectorsum > 0.5) inClocks[1] = 255;
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
    int deja_vu_length = self->deja_vu_length_quantizer.Lookup(
                                                         loop_length,
                                                         self->dejavu_length,
                                                         sizeof(loop_length) / sizeof(int));
    
    t_generator->set_model(TGeneratorModel(state.t_model));
    t_generator->set_range(TGeneratorRange(state.t_range));
    t_generator->set_rate(parameters[ADC_CHANNEL_T_RATE]);
    t_generator->set_bias(parameters[ADC_CHANNEL_T_BIAS]);
    t_generator->set_jitter(parameters[ADC_CHANNEL_T_JITTER]);
    t_generator->set_deja_vu(state.t_deja_vu ? deja_vu : 0.0f);
    t_generator->set_length(deja_vu_length);
    t_generator->set_pulse_width_mean(float(state.t_pulse_width_mean) / 256.0f);
    t_generator->set_pulse_width_std(float(state.t_pulse_width_std) / 256.0f);
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
                object_post(NULL, "new note: %f", voltage);
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
        x.control_mode = ControlMode(state.x_control_mode);
        x.voltage_range = VoltageRange(state.x_range % 3);
        x.register_mode = state.x_register_mode;
        x.register_value = u;
        
        x.spread = parameters[ADC_CHANNEL_X_SPREAD];
        x.bias = parameters[ADC_CHANNEL_X_BIAS];
        x.steps = parameters[ADC_CHANNEL_X_STEPS];
        x.deja_vu = state.x_deja_vu ? deja_vu : 0.0f;
        x.length = deja_vu_length;
        x.ratio.p = 1;
        x.ratio.q = 1;
        
        y.control_mode = CONTROL_MODE_IDENTICAL;
        y.voltage_range = VoltageRange(state.y_range);
        y.register_mode = false;
        y.register_value = 0.0f;
        y.spread = float(state.y_spread) / 256.0f;
        y.bias = float(state.y_bias) / 256.0f;
        y.steps = float(state.y_steps) / 256.0f;
        y.deja_vu = 0.0f;
        y.length = 1;
        y.ratio = y_divider_ratios[
                                   static_cast<uint16_t>(state.y_divider) * 12 >> 8];
        
        if (self->settings.dirty_scale_index() != -1) {
            int i = self->settings.dirty_scale_index();
            object_post(NULL, "dirtyscale! reload scale: %d", i);
            self->xy_generator.LoadScale(i, self->settings.persistent_data().scale[i]);
            self->settings.set_dirty_scale_index(-1);
        }
        
        y.scale_index = x.scale_index = state.x_scale;
        
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

    int count = self->blockCounter;

    for(int i=0; i<count; ++i)
        dsp_loop(self, ins, outs, kBlockSize, i);
    

}



void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    self->clock_connected[0] = count[0];
    self->clock_connected[1] = count[ADC_CHANNEL_LAST+1];
    
    if(maxvectorsize < kBlockSize) {
        object_error((t_object*)self, "vector size can't be smaller than %d samples, sorry!", kBlockSize);
        return;
    }
    self->sigvs = maxvectorsize;
    self->blockCounter = maxvectorsize / kBlockSize;
    object_post(NULL, "vector size div: %d", self->blockCounter);
    
    
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
    //self->part->elements::Part::~Part();
    //delete self->part;
    //self->read_inputs->elements::ReadInputs::~ReadInputs();
    //delete self->read_inputs;
    //if(self->block)
      //  sysmem_freeptr(self->block);
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
                strncpy(string_dest,"(signal) external clock", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) DEJA_VU_AMOUNT", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) DEJA_VU_LENGTH / X_SPREAD_2", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) T_RATE", ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal) T_BIAS", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal) T_JITTER", ASSIST_STRING_MAXSIZE);
                break;
            case 6:
                strncpy(string_dest,"(signal) X_SPREAD", ASSIST_STRING_MAXSIZE);
                break;
            case 7:
                strncpy(string_dest,"(signal) X_BIAS", ASSIST_STRING_MAXSIZE);
                break;
            case 8:
                strncpy(string_dest,"(signal) X_STEPS", ASSIST_STRING_MAXSIZE);
                break;
            case 9:
                strncpy(string_dest,"(signal) MALLET, (float) attenuvert", ASSIST_STRING_MAXSIZE);
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
    class_addmethod(this_class, (method)myObj_length,     "length",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_jitter,     "jitter",    A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_spread,   "spread",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_xbias,   "xbias",    A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_steps,   "steps",        A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_dejavu,  "dejavu",  A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_t_model,  "model",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_t,  "t",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_x,  "x",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_t_range,  "t_range",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_x_range,  "x_range",  A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_x_mode,  "x_mode",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_x_scale,  "x_scale",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_x_ext,  "x_ext",  A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_int,  "int",          A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,  "float",      A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_set_scale,  "set_scale",      A_GIMME, 0);
    class_addmethod(this_class, (method)myObj_record_scale,  "record_scale",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_info,	"info", 0);
    class_addmethod(this_class, (method)myObj_bang,    "bang", 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.mrbls~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'marbles' module");
}
