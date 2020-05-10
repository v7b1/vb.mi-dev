//
//  dsp.h
//  vb.elements-resonator~
//
//  Created by vb on 29/04/17.
//
//

#ifndef dsp_h
#define dsp_h




// state variable filter ////////////////////


typedef struct {
    double g;
    double r;
    double h;
    double state1;
    double state2;
    double gain;
} g_svf;


// prototypes
g_svf *svf_make(double f, double resonance, double gain);
void svf_set_f_q(g_svf *p, double f, double resonance);
void svf_set_f_q_g(g_svf *p, double f, double resonance, double gain);
void svf_do_block(g_svf *p, double *input, double *output, long vecsize);
void svf_free(g_svf *p);


g_svf *svf_make(double f, double resonance, double gain)
{
    g_svf *p;
    
    p = (g_svf *)malloc(sizeof(g_svf));
    //svf_set_f_q(p, f, resonance);
    double g = tan(f*M_PI);
    p->r = 1.0 / resonance;
    p->h = 1.0 / (1.0 + p->r * g + g * g);
    p->g = g;
    p->state1 = 0.0;
    p->state2 = 0.0;
    p->gain = gain;
    
    return(p);
}


void svf_free(g_svf *p)
{
    free(p);
}


inline void svf_set_f_q(g_svf *p, double f, double resonance) {
    double g = tan(f*M_PI);
    p->r = 1.0 / resonance;
    p->h = 1.0 / (1.0 + p->r * g + g * g);
    p->g = g;
}


inline void svf_set_f_q_g(g_svf *p, double f, double resonance, double gain) {
    double g = tan(f*M_PI);
    p->r = 1.0 / resonance;
    p->h = 1.0 / (1.0 + p->r * g + g * g);
    p->g = g;
    p->gain = gain;
}



void svf_do_block(g_svf *p, double *input, double *output, long vecsize)
{
    int i;
    double hp, bp, lp;
    double g, h, r;
    double state1, state2;
    double gain = p->gain;
    g = p->g;
    h = p->h;
    r = p->r;
    state1 = p->state1;
    state2 = p->state2;
    
    for(i=0; i<vecsize; i++)
    {
        hp = (input[i] - r * state1 - g * state1 - state2) * h;
        bp = g * hp + state1;
        state1 = g * hp + bp;
        lp = g * bp + state2;
        state2 = g * bp + lp;

        output[i] += (bp*gain);
    }
    
    p->state1 = state1;
    p->state2 = state2;
    
}


void svf_do_block_amp(g_svf *p, double *input, double *output, double amp, long vecsize)
{
    int i;
    double hp, bp, lp;
    double g, h, r;
    double state1, state2;
    double gain = p->gain * amp;
    g = p->g;
    h = p->h;
    r = p->r;
    state1 = p->state1;
    state2 = p->state2;
    
    for(i=0; i<vecsize; i++)
    {
        hp = (input[i] - r * state1 - g * state1 - state2) * h;
        bp = g * hp + state1;
        state1 = g * hp + bp;
        lp = g * bp + state2;
        state2 = g * bp + lp;
        
        output[i] += (bp * gain);
    }
    
    p->state1 = state1;
    p->state2 = state2;
    
}


// CosineOscillator ////////////////////

typedef struct {
    double y1;
    double y0;
    double iir_coef;
    double init_amp;
} g_cosOsc;


g_cosOsc* cosOsc_make(double frequency);
void cosOsc_free(g_cosOsc *p);
void cos_Osc_setFreq(g_cosOsc *p, double frequency);
void cosOsc_start(g_cosOsc *p);
double cosOsc_next(g_cosOsc *p);

g_cosOsc* cosOsc_make(double frequency)
{
    g_cosOsc *p;
    
    p = (g_cosOsc *)malloc(sizeof(g_cosOsc));
    p->iir_coef = 2.0 * cos(2.0 * M_PI * frequency);
    p->init_amp = p->iir_coef * 0.25;

    cosOsc_start(p);
    return p;
}


void cosOsc_free(g_cosOsc *p)
{
    free(p);
}


void cos_Osc_setFreq(g_cosOsc *p, double frequency) {
    p->iir_coef = 2.0 * cos(2.0 * M_PI * frequency);
    p->init_amp = p->iir_coef * 0.25;
    p->y1 = p->init_amp;
    p->y0 = 0.5;
}

void cosOsc_start(g_cosOsc *p) {
    p->y1 = p->init_amp;
    p->y0 = 0.5;
}


double cosOsc_next(g_cosOsc *p) {
    double temp = p->y0;
    p->y0 = p->iir_coef * temp - p->y1;
    p->y1 = temp;
    return temp + 0.5;
}


#endif /* dsp_h */
