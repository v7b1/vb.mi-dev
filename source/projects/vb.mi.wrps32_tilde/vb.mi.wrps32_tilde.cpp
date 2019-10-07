#include "c74_max.h"
#include "c74_msp.h"

#include "warps/dsp/modulator.h"
#include "warps/settings.h"
#include "read_inputs.hpp"


//#include "Accelerate/Accelerate.h"



using namespace c74::max;

const size_t kBlockSize = 96;

static t_class* this_class = nullptr;

struct t_myObj {
    t_pxobject	obj;
    
    warps::Modulator    *modulator;
    warps::ReadInputs   *read_inputs;
    warps::Settings     *settings;
    double              adc_inputs[warps::ADC_LAST];
    short               patched[2];
    short               easterEgg;
    uint8_t             carrier_shape;
    
    //float              **inputs;
    //float              **outputs;
    warps::FloatFrame          *input;
    warps::FloatFrame          *output;
    
    long                count;
    double              sr;
    int                 sigvs;
};



void* myObj_new(void) {
    t_myObj* self = (t_myObj*)object_alloc(this_class);
    
    if(self)
    {
        dsp_setup((t_pxobject*)self, 6);    // six audio inputs
        
        //self->info_out = outlet_new((t_object *)self, NULL);
        outlet_new(self, "signal"); // 'out' output
        outlet_new(self, "signal"); // 'aux' output
        
        self->sr = sys_getsr();
        if(self->sr <= 0)
            self->sr = 44100.0;

        
        // init some params
        self->count = 0;
        self->easterEgg = 0;
        self->patched[0] = self->patched[1] = 0;
        self->carrier_shape = 1;
        
        self->modulator = new warps::Modulator;
        memset(self->modulator, 0, sizeof(*t_myObj::modulator));
        self->modulator->Init(self->sr);
        
        self->settings = new warps::Settings;
        self->settings->Init();
        
        self->read_inputs = new warps::ReadInputs;
        self->read_inputs->Init(self->settings->mutable_calibration_data());

        
        for(int i=0; i<warps::ADC_LAST; i++)
            self->adc_inputs[i] = 0.0;
        /*
        self->inputs = (float**)sysmem_newptr(2*sizeof(float*));
        self->outputs = (float**)sysmem_newptr(2*sizeof(float*));
        self->inputs[0] = (float*)sysmem_newptrclear(kBlockSize*sizeof(float));
        self->inputs[1] = (float*)sysmem_newptrclear(kBlockSize*sizeof(float));
        self->outputs[0] = (float*)sysmem_newptrclear(kBlockSize*sizeof(float));
        self->outputs[1] = (float*)sysmem_newptrclear(kBlockSize*sizeof(float));
        */
         /*
        self->inputs = (double**)sysmem_newptr(2*sizeof(double*));
        self->outputs = (double**)sysmem_newptr(2*sizeof(double*));
        self->inputs[0] = (double*)sysmem_newptrclear(kBlockSize*sizeof(double));
        self->inputs[1] = (double*)sysmem_newptrclear(kBlockSize*sizeof(double));
        self->outputs[0] = (double*)sysmem_newptrclear(kBlockSize*sizeof(double));
        self->outputs[1] = (double*)sysmem_newptrclear(kBlockSize*sizeof(double));
        */
        /*
        self->inputshort = (warps::ShortFrame*)sysmem_newptr(kBlockSize*sizeof(warps::ShortFrame));
        self->outputshort = (warps::ShortFrame*)sysmem_newptr(kBlockSize*sizeof(warps::ShortFrame));
        */
        self->input = (warps::FloatFrame*)sysmem_newptr(kBlockSize*sizeof(warps::FloatFrame));
        self->output = (warps::FloatFrame*)sysmem_newptr(kBlockSize*sizeof(warps::FloatFrame));
    }
    else {
        object_free(self);
        self = NULL;
    }
    
    return self;
}


void myObj_info(t_myObj *self)
{
    /*
     double channel_drive[2];
     double modulation_algorithm;
     double modulation_parameter;
     
     // Easter egg parameters.
     double frequency_shift_pot;
     double frequency_shift_cv;
     double phase_shift;
     double note;
     
     double frequency_shift_pot;
     double frequency_shift_cv;
     double phase_shift;
     double note;
     
     int32_t carrier_shape;  // 0 = external
     */
    //warps::Modulator *m = self->modulator;
    warps::Parameters *p = self->modulator->mutable_parameters();

    object_post((t_object*)self, "Modulator Parameters ----------->");
    object_post((t_object*)self, "drive[0]: %f", p->channel_drive[0]);
    object_post((t_object*)self, "drive[1]: %f", p->channel_drive[1]);
    object_post((t_object*)self, "mod_algo: %f", p->modulation_algorithm);
    object_post((t_object*)self, "mod param: %f", p->modulation_parameter);
    object_post((t_object*)self, "car_shape: %d", p->carrier_shape);
    
    object_post((t_object*)self, "freqShift_pot: %f", p->frequency_shift_pot);
    object_post((t_object*)self, "freqShift_cv: %f", p->frequency_shift_cv);
    
    object_post((t_object*)self, "phaseShift: %d", p->phase_shift);
    object_post((t_object*)self, "note: %f", p->note);

    
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
        case 2: // ADC_LEVEL_1_POT
            self->patched[0] = value != 0;
            break;
        case 3:
            // ADC_LEVEL_2_POT,
            self->patched[1] = value != 0;
            break;
        case 4:
            //self->patched[0] = value != 0;
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        case 5:
            //self->patched[1] = value != 0;
            object_post((t_object*)self, "inlet %ld: nothing to do...", innum);
            break;
        default:
            break;
    }
}



#pragma mark ----- main pots -----

void myObj_modulation_algo(t_myObj* self, double m) {
    // Selects which signal process- ing operation is performed on the carrier and modulator.
    self->adc_inputs[warps::ADC_ALGORITHM_POT] = clamp(m, 0., 1.);
    //object_post(NULL, "algo: %f", self->adc_inputs[warps::ADC_ALGORITHM_POT]);
    
    self->modulator->mutable_parameters()->modulation_algorithm = clamp(m, 0., 1.);
}

void myObj_modulation_timbre(t_myObj* self, double m) {
    // Controls the intensity of the high C harmonics created by cross-modulation
    // (or provides another dimension of tone control for some algorithms).
    self->adc_inputs[warps::ADC_PARAMETER_POT] = clamp(m, 0., 1.);
    
    self->modulator->mutable_parameters()->modulation_parameter = clamp(m, 0., 1.);
}


// oscillator button

void myObj_int_osc_shape(t_myObj* self, long t) {
    // Enables the internal oscillator and selects its waveform.
    int shape = clamp((int)t, 0, 3);
    
    
    if (!self->easterEgg) {
        self->carrier_shape = shape;
        //object_post((t_object *)self, "carrier_shape: %d", self->carrier_shape_);
    } else {
        bool easter = !self->modulator->easter_egg();
        self->modulator->set_easter_egg(easter);
        self->settings->mutable_state()->boot_in_easter_egg_mode = easter;
        self->carrier_shape = 1;
    }
    //UpdateCarrierShape();
    self->modulator->mutable_parameters()->carrier_shape = self->carrier_shape;
    self->settings->mutable_state()->carrier_shape = self->carrier_shape;
    object_post(NULL, "shape: %d", self->carrier_shape);
}


#pragma mark --------- small pots ----------

void myObj_level1(t_myObj* self, double m) {
    // External carrier amplitude or internal oscillator frequency.
    // When the internal oscillator is switched off, this knob controls the amplitude of the carrier, or the amount of amplitude modulation from the channel 1 LEVEL CV input (1). When the internal oscillator is active, this knob controls its frequency.
    
    self->adc_inputs[warps::ADC_LEVEL_1_POT] = clamp(m, 0., 1.);
    
    self->modulator->mutable_parameters()->channel_drive[0] = clamp(m, 0., 1.);
}

void myObj_level2(t_myObj* self, double m) {
    // This knob controls the amplitude of the modulator, or the amount of amplitude modulation from the channel 2 LEVEL CV input (2). Past a certain amount of gain, the signal soft clips.
    self->adc_inputs[warps::ADC_LEVEL_2_POT] = clamp(m, 0., 1.);
    
    self->modulator->mutable_parameters()->channel_drive[1] = clamp(m, 0., 1.);
}


#pragma mark -------- other messages ----------

void myObj_note(t_myObj* self, double n) {
    //double note = 60.0 * self->adc_inputs[warps::ADC_LEVEL_1_POT] + 12.0;
    if(self->carrier_shape != 0) {
        double note = (n-12.0)/60.0;
        self->adc_inputs[warps::ADC_LEVEL_1_POT] = note;
        //object_post(NULL, "note: %f", note);
    }
    self->modulator->mutable_parameters()->note = n;
}

void myObj_bypass(t_myObj* self,long t) {
    self->modulator->set_bypass(t != 0);
}

void myObj_easter_egg(t_myObj* self, long t) {
    self->easterEgg = (t != 0);
    self->modulator->set_easter_egg(self->easterEgg);
}



#pragma mark ----- dsp loop ------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    //float  **inputs = self->inputs;
    //float  **outputs = self->outputs;
    //warps::ShortFrame *inputshort = self->inputshort;
    //warps::ShortFrame *outputshort = self->outputshort;
    warps::FloatFrame  *input = self->input;
    warps::FloatFrame  *output = self->output;

    long    vs = sampleframes;
    long    count = self->count;
    //double  *adc_inputs = self->adc_inputs;
    
    if (self->obj.z_disabled)
        return;
    
    /*
    // read 'cv' input signals, store first value of a sig vector
    for(auto k=0; k<4; k++) {
        // cv inputs are expected in -1. to 1. range
        adc_inputs[k] = clamp((1.-ins[k+2][0]), 0., 1.);  // leave out first two inlets (which are the audio inputs)
    }
    self->read_inputs->Read(self->modulator->mutable_parameters(), adc_inputs, self->patched);
    */
    
    for (auto i=0; i<vs; ++i) {
        //inputs[0][count] = ins[0][i];
        //inputs[1][count] = ins[1][i];
        //inputshort[count].l = ins[0][i] * 32767.0;
        //inputshort[count].r = ins[1][i] * 32767.0;
        input[count].l = ins[0][i];
        input[count].r = ins[1][i];
        
        //outs[0][i] = outputs[0][count];
        //outs[1][i] = outputs[1][count];
        //outs[0][i] = outputshort[count].l / 32767.0;
        //outs[1][i] = outputshort[count].r / 32767.0;
        outs[0][i] = output[count].l;
        outs[1][i] = output[count].r;
        
        count++;
        if(count >= kBlockSize) {
            
            //self->modulator->Processfff(inputs, outputs, kBlockSize);
            //self->modulator->Process(inputshort, outputshort, kBlockSize);
            self->modulator->Processf(input, output, kBlockSize);
            count = 0;
        }
    }
    
    self->count = count;
    
}



void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    /*
    self->trigger_connected = count[6];
    self->modulations.trigger_patched = self->trigger_toggle && self->trigger_connected;*/
    /*
    if(maxvectorsize > plaits::kMaxBlockSize) {
        object_error((t_object*)self, "vector size can't be larger than 1024 samples, sorry!");
        return;
    }
    */
    
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->modulator->Init(self->sr);
    }
    
    object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
}



#pragma mark ---- free function ----

void myObj_free(t_myObj* self) {
    dsp_free((t_pxobject*)self);
    delete self->modulator;

    //sysmem_freeptr(self->inputs[0]);
    //sysmem_freeptr(self->inputs[1]);
    //sysmem_freeptr(self->outputs[0]);
    //sysmem_freeptr(self->outputs[1]);
    sysmem_freeptr(self->input);
    sysmem_freeptr(self->output);
    //sysmem_freeptr(self->inputshort);
    //sysmem_freeptr(self->outputshort);
}


void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest) {
    if (io == ASSIST_INLET) {
        switch (index) {
            case 0:
                strncpy(string_dest,"(signal) IN carrier", ASSIST_STRING_MAXSIZE);
                break;
            case 1:
                strncpy(string_dest,"(signal) IN modulator", ASSIST_STRING_MAXSIZE);
                break;
            case 2:
                strncpy(string_dest,"(signal) LEVEL1 controls carrier amplitude or int osc freq, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 3:
                strncpy(string_dest,"(signal) LEVEL2 controls modulator amplitude, (int) patch/unpatch", ASSIST_STRING_MAXSIZE);
                break;
            case 4:
                strncpy(string_dest,"(signal) ALGO - choose x-fade algorithm", ASSIST_STRING_MAXSIZE);
                break;
            case 5:
                strncpy(string_dest,"(signal) TIMBRE", ASSIST_STRING_MAXSIZE);
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
    this_class = class_new("vb.mi.wrps32~", (method)myObj_new, (method)myObj_free, sizeof(t_myObj), 0, A_GIMME, 0);
    
    class_addmethod(this_class, (method)myObj_assist,	            "assist",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_dsp64,	            "dsp64",	A_CANT,		0);
    
    // main pots
    class_addmethod(this_class, (method)myObj_modulation_algo,      "algo",     A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_modulation_timbre,	"timbre",	A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_int_osc_shape,        "osc_shape",  A_LONG, 0);
    class_addmethod(this_class, (method)myObj_level1,               "level1",   A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_level2,               "level2",   A_FLOAT, 0);

    class_addmethod(this_class, (method)myObj_note,                 "note",     A_FLOAT, 0);
    
    class_addmethod(this_class, (method)myObj_bypass,               "bypass",   A_LONG, 0);
    class_addmethod(this_class, (method)myObj_easter_egg,           "easterEgg",A_LONG, 0);
    
    class_addmethod(this_class, (method)myObj_int,                  "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_info,	                "info", 0);
    
    class_dspinit(this_class);
    class_register(CLASS_BOX, this_class);
    
    object_post(NULL, "vb.mi.wrps32~ by volker böhm --> https://vboehm.net");
    object_post(NULL, "a clone of mutable instruments' 'warps' module");
}
