//
// Copyright 2021 Volker Böhm.
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


// a clone of mutable instruments' 'Grids' module for maxmsp
// by volker böhm, may 2021, https://vboehm.net


// Original code by Émilie Gillet, https://mutable-instruments.net/



// TODO: make pattern length user definable

#include "c74_msp.h"


#include "avrlib/op.h"
#include "grids/clock.h"
#include "grids/pattern_generator.h"

#include <cstdio>



using namespace c74::max;

#define COUNTMAX 4

static t_class* this_class = nullptr;


struct t_myObj {
	t_pxobject	obj;
    
    grids::Clock clock;
    grids::PatternGenerator pattern_generator;
    uint8_t     swing_amount;
    uint32_t    tap_duration;
    uint8_t     clock_resolution;
    uint8_t     count;
    
    double      sr;
    double      c;      // factor for bpm calculation
    long        sigvs;
    
    uint8_t     mode, swing, config, gate_mode;
    uint8_t     previous_tick;
    
    uint8_t     ext_clock;
    
};


uint8_t ticks_granularity[] = { 6, 3, 1 };


void* myObj_new(t_symbol *s, long argc, t_atom *argv)
{
	t_myObj* self = (t_myObj*)object_alloc(this_class);
	
    if(self)
    {
        dsp_setup((t_pxobject*)self, 7);
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        outlet_new(self, "signal");
        
        self->sigvs = sys_getblksize();
        

        self->sr = sys_getsr();
        self->c = ((1L<<32) * 8) / (120 * self->sr / COUNTMAX );
        
        self->clock.Init(self->c);
        self->pattern_generator.Init();
        
        self->mode = 0;     // drum mode
        self->swing = 0;
        self->config = 0;
        self->swing_amount = 0;
        self->clock_resolution = grids::CLOCK_RESOLUTION_24_PPQN;
        self->count = 0;
        self->previous_tick = 0;
        
        // process attributes
        attr_args_process(self, argc, argv);
        
    }
    else {
        object_free(self);
        self = NULL;
    }
    
	return self;
}



void myObj_bang(t_myObj* self) {
    
    self->pattern_generator.Reset();    // stay in time
    
    if(self->clock.locked())
        self->clock.Reset();            // if externally clocked, reset immediately!
    
}

void myObj_hard_reset(t_myObj* self) {
    
    self->pattern_generator.Reset();
    self->clock.Reset();
    
}


void myObj_int(t_myObj* self, long m) {
    
    long innum = proxy_getinlet((t_object *)self);
    
    if (innum == 0) {
        double bpm = clamp(m, 20L, 511L);
        
        if (bpm != self->clock.bpm() && !self->clock.locked()) {
            self->clock.Update_f(bpm, self->c);
        }
    }
    else {
        grids::PatternGeneratorSettings* settings = self->pattern_generator.mutable_settings();
        uint8_t mm = m;
        if (innum == 1)
            settings->options.drums.x = mm;
        else if (innum == 2)
            settings->options.drums.y = mm;
        else if (innum == 3)
            settings->options.drums.randomness = mm;
        else {
            settings->density[innum - 4] = mm;
        }
        
    }
}


void myObj_float(t_myObj* self, double m) {
    
    long innum = proxy_getinlet((t_object *)self);

    if (innum == 0) {
//        double bpm = m + 0.5;
        double bpm = clamp(m, 20.0, 511.0);

        if (bpm != self->clock.bpm() && !self->clock.locked()) {
            self->clock.Update_f(bpm, self->c);
        }
    }
    else {
        grids::PatternGeneratorSettings* settings = self->pattern_generator.mutable_settings();
        uint8_t mm = m * 255.0;
        if (innum == 1)
            settings->options.drums.x = mm;
        else if (innum == 2)
            settings->options.drums.y = mm;
        else if (innum == 3)
            settings->options.drums.randomness = mm;
        else {
            settings->density[innum - 4] = mm;
        }
        
    }
    
}


void myObj_print(t_myObj *self) {
    grids::Clock *clock = &self->clock;
    grids::PatternGenerator *pg = &self->pattern_generator;
    grids::PatternGeneratorSettings* settings = pg->mutable_settings();
    
    object_post((t_object*)self, "----- INFO -----");
    object_post((t_object*)self, "clock bpm: %f", clock->bpm());
    object_post((t_object*)self, "clock locked: %d", clock->locked());
    object_post((t_object*)self, "pg swing amount: %d", pg->swing_amount());
    object_post((t_object*)self, "pg output mode: %d", pg->output_mode());
    object_post((t_object*)self, "pg clock reso: %d", pg->clock_resolution());
    object_post((t_object*)self, "pg config: %d", pg->output_clock());
    object_post((t_object*)self, "pg gate mode: %d", pg->gate_mode());
    object_post((t_object*)self, "pg step: %d", pg->step());
    
}



#pragma mark --------- attr setters ---------

t_max_err mode_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    // let's have 0: normal, 1: euklid
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->mode = ( m != 0 );
        self->pattern_generator.set_output_mode(!self->mode);
    }

    return MAX_ERR_NONE;
}


t_max_err swing_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->swing = m != 0;
        self->pattern_generator.set_swing(self->swing);
    }
    
    return MAX_ERR_NONE;
}

t_max_err config_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->config = m != 0;
        self->pattern_generator.set_output_clock(self->config);
    }
    
    return MAX_ERR_NONE;
}


//CLOCK_RESOLUTION_4_PPQN,
//CLOCK_RESOLUTION_8_PPQN,
//CLOCK_RESOLUTION_24_PPQN
t_max_err resolution_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        long m = atom_getlong(av);
        clamp(m, 0L, 2L);
        self->clock_resolution = m;
        self->pattern_generator.set_clock_resolution(m);
     
        self->clock.Update_f(self->clock.bpm(), self->c);
        self->pattern_generator.Reset();
//        self->clock.Reset();
    }

    return MAX_ERR_NONE;
}

t_max_err gate_setter(t_myObj *self, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        t_atom_long m = atom_getlong(av);
        self->gate_mode = m != 0;
        self->pattern_generator.set_gate_mode(self->gate_mode);
    }
    
    return MAX_ERR_NONE;
}

#pragma mark -------- DSP Loop ----------

void myObj_perform64(t_myObj* self, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    
    double      *clock_input = ins[0];
    // use overflow to limit data range to uint8
//    uint8_t     map_x = ins[1][0] * 255.0;
//    uint8_t     map_y = ins[2][0] * 255.0;
//    uint8_t     randomness = ins[3][0] * 255.0;
//    uint8_t     bd_density = ins[4][0] * 255.0;
//    uint8_t     sd_density = ins[5][0] * 255.0;
//    uint8_t     hh_density = ins[6][0] * 255.0;
    
    
    if (self->obj.z_disabled)
        return;
    
    
    grids::Clock *clock = &self->clock;
    grids::PatternGenerator *pattern_generator = &self->pattern_generator;
    grids::PatternGeneratorSettings* settings = pattern_generator->mutable_settings();
    
    
//    settings->options.drums.x = self->map_x;
//    settings->options.drums.y = self->map_y;
//    settings->options.drums.randomness = self->randomness;
//    settings->density[0] = self->bd_density;
//    settings->density[1] = self->sd_density;
//    settings->density[2] = self->hh_density;
    
    uint8_t state = pattern_generator->state();
    uint8_t count = self->count;
    uint8_t previous_tick = self->previous_tick;
    uint8_t ext_clock = self->ext_clock;
    
    uint8_t increment = ticks_granularity[pattern_generator->clock_resolution()];
    uint8_t sum = 0;
    
    for(int i=0; i<sampleframes; ++i) {
        
        sum += (clock_input[i] > 0.1);
        
        if(count >= COUNTMAX) {
            count = 0;
            
            uint8_t num_ticks = 0;
            
            // external clock input
            if(ext_clock) {
                uint8_t tick = (sum != 0);
                if (tick && !(previous_tick)) {
                    num_ticks = increment;
                }
                if (!tick && previous_tick) {
                    pattern_generator->ClockFallingEdge();
                    sum = 0;
                }
                previous_tick = tick;
            }
            else {
                clock->Tick();
                clock->Wrap(self->swing_amount);
                if (clock->raising_edge()) {
                    num_ticks = increment;
                }
                if (clock->past_falling_edge()) {
                    pattern_generator->ClockFallingEdge();
                }
            }
            
            
            if (num_ticks) {
                self->swing_amount = pattern_generator->swing_amount();
                pattern_generator->TickClock(num_ticks);
            }
            
            // TODO: add reset here?
            
            state = pattern_generator->state();
            pattern_generator->IncrementPulseCounter();
            
            
        }
        count++;
        
        
        // decode 'state' into output triggers/gates
        
        for(int k=0; k<8; k++) {
            outs[k][i] = (state >> k) & 1;
        }
        
    }
    
    self->count = count;
    self->previous_tick = previous_tick;

}



void myObj_dsp64(t_myObj* self, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    if(samplerate != self->sr) {
        self->sr = samplerate;
        self->c = ((1L<<32) * 8) / (120 * self->sr / COUNTMAX );
        if ( !self->clock.locked() ) {
            self->clock.Update_f(self->clock.bpm(), self->c);
        }
    }
    
        object_method_direct(void, (t_object*, t_object*, t_perfroutine64, long, void*),
                         dsp64, gensym("dsp_add64"), (t_object*)self, (t_perfroutine64)myObj_perform64, 0, NULL);
    
}




void myObj_assist(t_myObj* self, void* unused, t_assist_function io, long index, char* string_dest)
{
	if (io == ASSIST_INLET) {
		switch (index) {
			case 0:
                strncpy(string_dest,"(signal) ext. clock (float/int) BPM", ASSIST_STRING_MAXSIZE); break;
            case 1:
                strncpy(string_dest,"(float) MAP_X", ASSIST_STRING_MAXSIZE); break;
            case 2:
                strncpy(string_dest,"(float) MAP_Y", ASSIST_STRING_MAXSIZE); break;
            case 3:
                strncpy(string_dest,"(float) CHOAS / SWING", ASSIST_STRING_MAXSIZE); break;
            case 4:
                strncpy(string_dest,"(float) Density 1", ASSIST_STRING_MAXSIZE); break;
            case 5:
                strncpy(string_dest,"(float) Density 2", ASSIST_STRING_MAXSIZE); break;
            case 6:
                strncpy(string_dest,"(float) Density 3", ASSIST_STRING_MAXSIZE); break;
		}
	}
	else if (io == ASSIST_OUTLET) {
		switch (index) {
            case 0:
                strncpy(string_dest,"(signal) BD trigger", ASSIST_STRING_MAXSIZE); break;
            case 1:
                strncpy(string_dest,"(signal) SD trigger", ASSIST_STRING_MAXSIZE); break;
            case 2:
                strncpy(string_dest,"(signal) HH trigger", ASSIST_STRING_MAXSIZE); break;
            case 3:
                strncpy(string_dest,"(signal) BD accent / all accents", ASSIST_STRING_MAXSIZE); break;
            case 4:
                strncpy(string_dest,"(signal) SD accent / clock out", ASSIST_STRING_MAXSIZE); break;
            case 5:
                strncpy(string_dest,"(signal) HH accent / reset signal", ASSIST_STRING_MAXSIZE); break;
            case 6:
                strncpy(string_dest,"(signal) 16th pulses", ASSIST_STRING_MAXSIZE); break;
            case 7:
                strncpy(string_dest,"(signal) random 32nd pulses", ASSIST_STRING_MAXSIZE); break;
        }
	}
}


void ext_main(void* r) {
	this_class = class_new("vb.mi.grds~", (method)myObj_new, (method)dsp_free, sizeof(t_myObj), 0, A_GIMME, 0);

	class_addmethod(this_class, (method)myObj_assist,	"assist",	A_CANT,		0);
	class_addmethod(this_class, (method)myObj_dsp64,	"dsp64",	A_CANT,		0);
    class_addmethod(this_class, (method)myObj_int,      "int",      A_LONG, 0);
    class_addmethod(this_class, (method)myObj_float,    "float",    A_FLOAT, 0);
    class_addmethod(this_class, (method)myObj_hard_reset,     "reset",     0L);
    class_addmethod(this_class, (method)myObj_bang,     "bang",     0L);
    class_addmethod(this_class, (method)myObj_print,     "info",     0L);
    
	class_dspinit(this_class);
	class_register(CLASS_BOX, this_class);
    
    // ATTRIBUTES ..............

    // mode selection
    CLASS_ATTR_CHAR(this_class, "mode", 0, t_myObj, mode);
    CLASS_ATTR_ENUMINDEX(this_class, "mode", 0, "Normal Euclid");
    CLASS_ATTR_LABEL(this_class, "mode", 0, "drumming mode");
    CLASS_ATTR_FILTER_CLIP(this_class, "mode", 0, 1);
    CLASS_ATTR_ACCESSORS(this_class, "mode", NULL, (method)mode_setter);
    CLASS_ATTR_SAVE(this_class, "mode", 0);

    // swing
    CLASS_ATTR_CHAR(this_class, "swing", 0, t_myObj, swing);
    CLASS_ATTR_STYLE_LABEL(this_class, "swing", 0, "onoff", "swing on/off");
    CLASS_ATTR_ACCESSORS(this_class, "swing", NULL, (method)swing_setter);
    CLASS_ATTR_SAVE(this_class, "swing", 0);
    
    // ext clock
    CLASS_ATTR_CHAR(this_class, "ext_clock", 0, t_myObj, ext_clock);
    CLASS_ATTR_STYLE_LABEL(this_class, "ext_clock", 0, "onoff", "swing on/off");
    CLASS_ATTR_SAVE(this_class, "ext_clock", 0);
    
    // output config selection
    CLASS_ATTR_CHAR(this_class, "config", 0, t_myObj, config);
    CLASS_ATTR_ENUMINDEX(this_class, "config", 0, "Accents Clock_Info");
    CLASS_ATTR_LABEL(this_class, "config", 0, "output configuration");
    CLASS_ATTR_FILTER_CLIP(this_class, "config", 0, 1);
    CLASS_ATTR_ACCESSORS(this_class, "config", NULL, (method)config_setter);
    CLASS_ATTR_SAVE(this_class, "config", 0);
    
    // set clock resolution
    CLASS_ATTR_CHAR(this_class, "resolution", 0, t_myObj, clock_resolution);
    CLASS_ATTR_ENUMINDEX(this_class, "resolution", 0, "4_PPQN 8_PPQN 24_PPQN");
    CLASS_ATTR_LABEL(this_class, "resolution", 0, "output configuration");
    CLASS_ATTR_FILTER_CLIP(this_class, "resolution", 0, 2);
    CLASS_ATTR_ACCESSORS(this_class, "resolution", NULL, (method)resolution_setter);
    CLASS_ATTR_SAVE(this_class, "resolution", 0);
    
    // trigger/gate mode
    CLASS_ATTR_CHAR(this_class, "gate_mode", 0, t_myObj, gate_mode);
    CLASS_ATTR_STYLE_LABEL(this_class, "gate_mode", 0, "onoff", "gate mode on/off");
    CLASS_ATTR_ACCESSORS(this_class, "gate_mode", NULL, (method)gate_setter);
    CLASS_ATTR_SAVE(this_class, "gate_mode", 0);

    
    object_post(NULL, "vb.mi.grds~ by Volker Böhm -- https://vboehm.net");
    object_post(NULL, "based on mutable instruments' 'Grids' module");
}
