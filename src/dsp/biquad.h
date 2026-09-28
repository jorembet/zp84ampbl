#ifndef ZP_DSP_BIQUAD_H
#define ZP_DSP_BIQUAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZP_MAX_STAGES 64

typedef struct {
    double b0, b1, b2;
    double a1, a2;
} zp_biquad;

typedef struct {
    zp_biquad stage[ZP_MAX_STAGES];
    double gain_db;
    double delay_ms;
    int polarity;
    int count;
} zp_cascade;

double zp_clampd(double v, double lo, double hi);

zp_biquad zp_biquad_peaking(double fs, double f0, double q, double gain_db);
zp_biquad zp_biquad_lowshelf(double fs, double f0, double q, double gain_db);
zp_biquad zp_biquad_highshelf(double fs, double f0, double q, double gain_db);
zp_biquad zp_biquad_lowpass(double fs, double f0, double q);
zp_biquad zp_biquad_highpass(double fs, double f0, double q);
zp_biquad zp_biquad_bandpass(double fs, double f0, double q);
zp_biquad zp_biquad_notch(double fs, double f0, double q);
zp_biquad zp_biquad_allpass(double fs, double f0, double q);
zp_biquad zp_biquad_gain(double gain_db);

void zp_cascade_init(zp_cascade *c);
void zp_cascade_push(zp_cascade *c, zp_biquad s);
void zp_cascade_push_linkwitz(zp_cascade *c, double fs, double f0,
                              int order_is_octaves, int highpass);
double zp_linkwitz_makeup_db(double fs, double hp_freq, int hp_order,
                             double lp_freq, int lp_order, int has_hp,
                             int has_lp);
double zp_cascade_response_db(const zp_cascade *c, double fs, double freq);
double zp_cascade_min_db(const zp_cascade *c, double fs, double f_lo,
                         double f_hi, double step);
double zp_cascade_max_db(const zp_cascade *c, double fs, double f_lo,
                         double f_hi, double step);
void zp_cascade_to_fixed(const zp_cascade *c, double scale, int32_t *out);

typedef struct {
    double z1, z2;
} zp_biquad_state;

void zp_biquad_reset(zp_biquad_state *s);
double zp_biquad_tick(const zp_biquad *b, zp_biquad_state *s, double in);

#ifdef __cplusplus
}
#endif

#endif
