#include "biquad.h"

#include <math.h>

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
    if (a0 == 0.0)
        a0 = 1.0;
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

void zp_cascade_push_linkwitz(zp_cascade *c, double fs, double f0,
                              int order_is_octaves, int highpass)
{
    const double lr_q = 0.5;
    int sections = order_is_octaves / 12;
    int i;

    if (sections < 1)
        sections = 1;
    if (sections > 12)
        sections = 12;
    for (i = 0; i < sections; i++) {
        zp_biquad b = highpass ? zp_biquad_highpass(fs, f0, lr_q)
                               : zp_biquad_lowpass(fs, f0, lr_q);
        zp_cascade_push(c, b);
    }
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

static double stage_response_db(const zp_biquad *b, double fs, double freq)
{
    double w = 2.0 * M_PI * (freq / fs);
    double c1 = cos(w), s1 = sin(w);
    double c2 = cos(2 * w), s2 = sin(2 * w);
    double nr, ni, dr, di;
    double mag;

    nr = b->b0 + b->b1 * c1 + b->b2 * c2;
    ni = -(b->b1 * s1 + b->b2 * s2);
    dr = 1.0 + b->a1 * c1 + b->a2 * c2;
    di = -(b->a1 * s1 + b->a2 * s2);

    if (dr == 0.0 && di == 0.0)
        return 0.0;
    mag = sqrt(nr * nr + ni * ni) / sqrt(dr * dr + di * di);
    if (mag < 1e-12)
        mag = 1e-12;
    return 20.0 * log10(mag);
}

double zp_cascade_response_db(const zp_cascade *c, double fs, double freq)
{
    double total = c->gain_db;
    int i;
    for (i = 0; i < c->count; i++)
        total += stage_response_db(&c->stage[i], fs, freq);
    return total;
}

static double cascade_extreme(const zp_cascade *c, double fs, double f_lo,
                              double f_hi, double step, int want_max)
{
    double best = 0;
    int n = (int)((f_hi - f_lo) / step) + 1;
    int i;
    for (i = 0; i <= n; i++) {
        double f = f_lo + i * step;
        double v = zp_cascade_response_db(c, fs, f);
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
