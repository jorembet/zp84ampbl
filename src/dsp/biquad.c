#include "biquad.h"

#include <math.h>
#include <complex.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double zp_clampd(double v, double lo, double hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void pre(double fs, double f0, double *w0, double *cs, double *sn)
{
    double w = 2.0 * M_PI * (f0 / fs);
    *w0 = w;
    *cs = cos(w);
    *sn = sin(w);
}

static zp_biquad make(double b0, double b1, double b2, double a0, double a1,
                     double a2)
{
    zp_biquad r;
    if (!isfinite(a0) || a0 == 0.0)
        return (zp_biquad){NAN,NAN,NAN,NAN,NAN};
    r.b0 = b0 / a0;
    r.b1 = b1 / a0;
    r.b2 = b2 / a0;
    r.a1 = a1 / a0;
    r.a2 = a2 / a0;
    return r;
}

zp_biquad zp_biquad_peaking(double fs, double f0, double q, double gain_db)
{
    double w0, cs, sn, a, al;
    pre(fs, f0, &w0, &cs, &sn);
    a = pow(10.0, gain_db / 40.0);
    al = sn / (2.0 * q);
    return make(1.0 + al * a, -2.0 * cs, 1.0 - al * a, 1.0 + al / a, -2.0 * cs,
                1.0 - al / a);
}

zp_biquad zp_biquad_lowshelf(double fs, double f0, double q, double gain_db)
{
    double w0, cs, sn, a, al, sa;
    pre(fs, f0, &w0, &cs, &sn);
    a = pow(10.0, gain_db / 40.0);
    al = sn / (2.0 * q);
    sa = 2.0 * sqrt(a) * al;
    return make(a * ((a + 1.0) - (a - 1.0) * cs + sa),
                2.0 * a * ((a - 1.0) - (a + 1.0) * cs),
                a * ((a + 1.0) - (a - 1.0) * cs - sa),
                (a + 1.0) + (a - 1.0) * cs + sa,
                -2.0 * ((a - 1.0) + (a + 1.0) * cs),
                (a + 1.0) + (a - 1.0) * cs - sa);
}

zp_biquad zp_biquad_highshelf(double fs, double f0, double q, double gain_db)
{
    double w0, cs, sn, a, al, sa;
    pre(fs, f0, &w0, &cs, &sn);
    a = pow(10.0, gain_db / 40.0);
    al = sn / (2.0 * q);
    sa = 2.0 * sqrt(a) * al;
    return make(a * ((a + 1.0) + (a - 1.0) * cs + sa),
                -2.0 * a * ((a - 1.0) + (a + 1.0) * cs),
                a * ((a + 1.0) + (a - 1.0) * cs - sa),
                (a + 1.0) - (a - 1.0) * cs + sa,
                2.0 * ((a - 1.0) - (a + 1.0) * cs),
                (a + 1.0) - (a - 1.0) * cs - sa);
}

zp_biquad zp_biquad_lowpass(double fs, double f0, double q)
{
    double w0, cs, sn, al;
    pre(fs, f0, &w0, &cs, &sn);
    al = sn / (2.0 * q);
    return make((1.0 - cs) / 2.0, 1.0 - cs, (1.0 - cs) / 2.0, 1.0 + al, -2.0 * cs,
                1.0 - al);
}

zp_biquad zp_biquad_highpass(double fs, double f0, double q)
{
    double w0, cs, sn, al;
    pre(fs, f0, &w0, &cs, &sn);
    al = sn / (2.0 * q);
    return make((1.0 + cs) / 2.0, -(1.0 + cs), (1.0 + cs) / 2.0, 1.0 + al,
                -2.0 * cs, 1.0 - al);
}

zp_biquad zp_biquad_bandpass(double fs, double f0, double q)
{
    double w0, cs, sn, al;
    pre(fs, f0, &w0, &cs, &sn);
    al = sn / (2.0 * q);
    return make(al, 0.0, -al, 1.0 + al, -2.0 * cs, 1.0 - al);
}

zp_biquad zp_biquad_notch(double fs, double f0, double q)
{
    double w0, cs, sn, al;
    pre(fs, f0, &w0, &cs, &sn);
    al = sn / (2.0 * q);
    return make(1.0, -2.0 * cs, 1.0, 1.0 + al, -2.0 * cs, 1.0 - al);
}

zp_biquad zp_biquad_allpass(double fs, double f0, double q)
{
    double w0, cs, sn, al;
    pre(fs, f0, &w0, &cs, &sn);
    al = sn / (2.0 * q);
    return make(1.0 - al, -2.0 * cs, 1.0 + al, 1.0 + al, -2.0 * cs, 1.0 - al);
}

zp_biquad zp_biquad_gain(double gain_db)
{
    return make(pow(10.0, gain_db / 20.0), 0.0, 0.0, 1.0, 0.0, 0.0);
}

void zp_cascade_init(zp_cascade *c)
{
    int i;
    for (i = 0; i < ZP_MAX_STAGES; i++)
        c->stage[i] = zp_biquad_gain(0.0);
    c->gain_db = 0;
    c->delay_ms = 0;
    c->polarity = 0;
    c->count = 0;
}

void zp_cascade_push(zp_cascade *c, zp_biquad s)
{
    if (c->count >= ZP_MAX_STAGES)
        return;
    c->stage[c->count++] = s;
}

static int crossover_valid(const zp_cascade *c,double fs,double f0,int slope)
{
    return c&&c->count>=0&&isfinite(fs)&&fs>0&&isfinite(f0)&&f0>0&&f0<fs/2
        &&slope>=6&&slope<=48&&slope%6==0;
}

static void push_butterworth_order(zp_cascade *c,double fs,double f0,int order,int hp)
{
    /* Real pole of an odd-order Butterworth, transformed with prewarped BLT. */
    if(order%2) {
        double k=tan(M_PI*f0/fs),norm=1+k;
        zp_cascade_push(c,make(hp?1:k,hp?-1:k,0,norm,k-1,0));
    }
    /* Conjugate Butterworth poles: distinct Q per SOS, low Q first. */
    for(int k=order/2-1;k>=0;k--) {
        double q=1/(2*sin(M_PI*(2*k+1)/(2*order)));
        zp_cascade_push(c,hp?zp_biquad_highpass(fs,f0,q):zp_biquad_lowpass(fs,f0,q));
    }
}

int zp_cascade_push_butterworth(zp_cascade *c,double fs,double f0,int slope,int hp)
{
    if(!crossover_valid(c,fs,f0,slope)||c->count+(slope/6+1)/2>ZP_MAX_STAGES)return -1;
    push_butterworth_order(c,fs,f0,slope/6,hp);
    return 0;
}

int zp_cascade_push_lr(zp_cascade *c,double fs,double f0,int slope,int hp)
{
    /* LR 2n = two identical Butterworth order-n filters; LR order 6 (36 dB/oct) uses BW3 twice.
       Non-multiples of 12 dB/oct in the vendor menu have no standard LR model. */
    if(!crossover_valid(c,fs,f0,slope)||slope%12||c->count+2*((slope/12+1)/2)>ZP_MAX_STAGES)return -1;
    push_butterworth_order(c,fs,f0,slope/12,hp);
    push_butterworth_order(c,fs,f0,slope/12,hp);
    return 0;
}

void zp_cascade_push_linkwitz(zp_cascade *c,double fs,double f0,int slope,int hp)
{
    (void)zp_cascade_push_lr(c,fs,f0,slope,hp);
}

double zp_linkwitz_makeup_db(double fs, double hp_freq, int hp_order,
                             double lp_freq, int lp_order, int has_hp,
                             int has_lp)
{
    const double nyq = fs * 0.5;
    const double lo0 = 20.0;
    const double hi0 = 20000.0;
    double lo = lo0, hi = hi0;
    double loss = 0;

    if (has_hp && hp_freq > lo)
        lo = hp_freq;
    if (has_lp && lp_freq < hi)
        hi = lp_freq;
    if (lo >= hi)
        lo = hi = sqrt(lo * hi);
    if (lo < lo0)
        lo = lo0;
    if (hi > nyq)
        hi = nyq;

    if (has_hp) {
        double probe = sqrt(lo * hi);
        zp_cascade c;
        if (probe <= hp_freq)
            probe = hp_freq * 1.5;
        zp_cascade_init(&c);
        zp_cascade_push_linkwitz(&c, fs, hp_freq, hp_order, 1);
        loss += -zp_cascade_response_db(&c, fs, probe);
    }
    if (has_lp) {
        double probe = sqrt(lo * hi);
        zp_cascade c;
        if (probe >= lp_freq)
            probe = lp_freq * 0.5;
        zp_cascade_init(&c);
        zp_cascade_push_linkwitz(&c, fs, lp_freq, lp_order, 0);
        loss += -zp_cascade_response_db(&c, fs, probe);
    }
    return loss;
}

static int response_frequency_valid(double fs,double freq)
{
    return isfinite(fs)&&fs>0&&isfinite(freq)&&freq>=0&&freq<=fs/2;
}

zp_complex_response zp_biquad_response(const zp_biquad *b,double fs,double freq)
{
    if(!b||!response_frequency_valid(fs,freq))return (zp_complex_response){NAN,NAN};
    double omega=2*M_PI*freq/fs;
    double complex z1=cexp(-I*omega),z2=z1*z1;
    double complex numerator=b->b0+b->b1*z1+b->b2*z2;
    double complex denominator=1+b->a1*z1+b->a2*z2;
    if(cabs(denominator)==0)return (zp_complex_response){NAN,NAN};
    double complex h=numerator/denominator;
    return (zp_complex_response){creal(h),cimag(h)};
}

zp_complex_response zp_cascade_response(const zp_cascade *c,double fs,double freq)
{
    if(!c||!response_frequency_valid(fs,freq)||c->count<0||c->count>ZP_MAX_STAGES)
        return (zp_complex_response){NAN,NAN};
    double complex h=pow(10,c->gain_db/20.0);
    for(int i=0;i<c->count;i++) {
        zp_complex_response section=zp_biquad_response(&c->stage[i],fs,freq);
        h*=section.re+I*section.im;
    }
    h*=cexp(-I*2*M_PI*freq*c->delay_ms/1000.0);
    if(c->polarity)h=-h;
    return (zp_complex_response){creal(h),cimag(h)};
}

double zp_cascade_response_db(const zp_cascade *c,double fs,double freq)
{
    zp_complex_response h=zp_cascade_response(c,fs,freq);
    double magnitude=hypot(h.re,h.im);
    if(!isfinite(magnitude))return NAN;
    return 20*log10(fmax(magnitude,1e-12));
}

static double cascade_extreme(const zp_cascade *c, double fs, double f_lo,
                              double f_hi, double step, int want_max)
{
    if(!response_frequency_valid(fs,f_lo)||!isfinite(f_hi)||!isfinite(step)||step<=0||f_hi<f_lo)return NAN;
    f_hi=fmin(f_hi,fs/2);
    double best = want_max?-INFINITY:INFINITY;
    int n = (int)ceil((f_hi - f_lo) / step);
    int i;
    for (i = 0; i <= n; i++) {
        double f = fmin(f_hi,f_lo + i * step);
        double v = zp_cascade_response_db(c, fs, f);
        if(!isfinite(v))return NAN;
        if (want_max ? (v > best) : (v < best))
            best = v;
    }
    return best;
}

double zp_cascade_min_db(const zp_cascade *c, double fs, double f_lo,
                         double f_hi, double step)
{
    return cascade_extreme(c, fs, f_lo, f_hi, step, 0);
}

double zp_cascade_max_db(const zp_cascade *c, double fs, double f_lo,
                         double f_hi, double step)
{
    return cascade_extreme(c, fs, f_lo, f_hi, step, 1);
}

void zp_cascade_to_fixed(const zp_cascade *c, double scale, int32_t *out)
{
    int i;
    for (i = 0; i < c->count; i++) {
        const zp_biquad *b = &c->stage[i];
        out[i * 5 + 0] = (int32_t)llround(b->b0 * scale);
        out[i * 5 + 1] = (int32_t)llround(b->b1 * scale);
        out[i * 5 + 2] = (int32_t)llround(b->b2 * scale);
        out[i * 5 + 3] = (int32_t)llround(b->a1 * scale);
        out[i * 5 + 4] = (int32_t)llround(b->a2 * scale);
    }
}

void zp_biquad_reset(zp_biquad_state *s)
{
    s->z1 = 0;
    s->z2 = 0;
}

double zp_biquad_tick(const zp_biquad *b, zp_biquad_state *s, double in)
{
    double out = b->b0 * in + s->z1;
    s->z1 = b->b1 * in - b->a1 * out + s->z2;
    s->z2 = b->b2 * in - b->a2 * out;
    return out;
}
