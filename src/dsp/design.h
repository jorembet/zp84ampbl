#ifndef ZP_DSP_DESIGN_H
#define ZP_DSP_DESIGN_H

#include "biquad.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZP_BANDS 31
#define ZP_CHANNELS 8
#define ZP_BAND_F0 20.0
#define ZP_MAX_GAIN 12.0
#define ZP_MAX_POINTS 1024

typedef struct {
    double gain_db[ZP_BANDS];
    int hp_enabled;
    double hp_freq;
    int hp_order;
    int lp_enabled;
    double lp_freq;
    int lp_order;
    double q;
    double delay_ms;
    int polarity;
    double tone_db;
    int tone_enabled;
    int limiter_enabled;
    double ceiling_db;
    int xover_makeup;
} zp_channel_cfg;

typedef struct {
    zp_channel_cfg ch[ZP_CHANNELS];
    double sample_rate;
    int auto_preamp;
} zp_design;

typedef struct {
    int count;
    double freq[ZP_MAX_POINTS];
    double spl[ZP_MAX_POINTS];
} zp_curve;

void zp_design_defaults(zp_design *d, double fs);
double zp_band_freq(int i);
void zp_channel_defaults(zp_channel_cfg *c);

void zp_design_cascade(const zp_design *d, int ch, zp_cascade *out);
double zp_preamp_for(const zp_design *d, int ch, double f_lo, double f_hi);
double zp_design_total_gain(const zp_design *d, int ch);

int zp_curve_load_rew(zp_curve *c, const char *path);
int zp_curve_from_target(zp_curve *c, const double *freq, const double *spl,
                         int n);
int zp_autoeq(const zp_curve *measured, const zp_curve *target, double fs,
              double ridge, double max_gain_db, double out_gain[ZP_BANDS]);
void zp_design_apply_autoeq(zp_channel_cfg *c, const double *gains);

#ifdef __cplusplus
}
#endif

#endif
