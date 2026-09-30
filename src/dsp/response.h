#ifndef ZP_DSP_RESPONSE_H
#define ZP_DSP_RESPONSE_H
#include "biquad.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZP_RESPONSE_POINTS 2048
#define ZP_RESPONSE_FS 48000.0
#define ZP_RESPONSE_MIN 20.0
#define ZP_RESPONSE_MAX 20000.0

enum { ZP_RESPONSE_FULL, ZP_RESPONSE_HPF, ZP_RESPONSE_LPF,
       ZP_RESPONSE_EQ, ZP_RESPONSE_CROSSOVER, ZP_RESPONSE_MODES };
enum { ZP_RESPONSE_OK, ZP_RESPONSE_INVALID, ZP_RESPONSE_UNSUPPORTED };
typedef struct { int family, slope, known; double frequency; } zp_response_filter;
typedef struct { int type, enabled; double frequency, gain, q; } zp_response_eq;
typedef struct {
    double sample_rate;
    zp_response_filter hp, lp;
    int eq_enabled;
    zp_response_eq eq[31];
} zp_response_config;

const char *zp_response_mode_name(int mode);
/* All coefficients are synthesized from decoded parameters, never USB words. */
int zp_response_build(const zp_response_config *config,int mode,zp_cascade *out,FILE *debug);
double zp_response_frequency(int index,int count,double fs);
#ifdef __cplusplus
}
#endif
#endif
