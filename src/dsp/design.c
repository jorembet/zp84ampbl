#include "design.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double zp_band_freq(int i)
{
    if (i < 0)
        i = 0;
    if (i > ZP_BANDS - 1)
        i = ZP_BANDS - 1;
    return ZP_BAND_F0 * pow(2.0, (double)i / 3.0);
}

void zp_channel_defaults(zp_channel_cfg *c)
{
    memset(c, 0, sizeof(*c));
    c->q = 1.45;
    c->xover_makeup = 1;
    c->hp_order = 24;
    c->lp_order = 24;
    c->ceiling_db = -0.5;
}

void zp_design_defaults(zp_design *d, double fs)
{
    int i;
    memset(d, 0, sizeof(*d));
    d->sample_rate = fs;
    d->auto_preamp = 1;
    for (i = 0; i < ZP_CHANNELS; i++)
        zp_channel_defaults(&d->ch[i]);
}

void zp_design_cascade(const zp_design *d, int ch, zp_cascade *out)
{
    const zp_channel_cfg *c;
    int i;

    zp_cascade_init(out);
    if (ch < 0 || ch >= ZP_CHANNELS)
        return;
    c = &d->ch[ch];

    if (c->hp_enabled && c->hp_freq > 1.0)
        zp_cascade_push_linkwitz(out, d->sample_rate, c->hp_freq, c->hp_order,
                                 1);
    if (c->lp_enabled && c->lp_freq > 1.0)
        zp_cascade_push_linkwitz(out, d->sample_rate, c->lp_freq, c->lp_order,
                                 0);
    if (c->xover_makeup)
        out->gain_db += zp_linkwitz_makeup_db(d->sample_rate, c->hp_freq,
                                              c->hp_order, c->lp_freq,
                                              c->lp_order, c->hp_enabled,
                                              c->lp_enabled);
    if (c->tone_enabled && c->tone_db != 0.0)
        zp_cascade_push(out, zp_biquad_peaking(d->sample_rate, 1000.0, 0.707,
                                                c->tone_db));
    for (i = 0; i < ZP_BANDS; i++) {
        if (c->gain_db[i] == 0.0)
            continue;
        zp_cascade_push(out, zp_biquad_peaking(d->sample_rate,
                                                zp_band_freq(i),
                                                c->q, c->gain_db[i]));
    }
    out->delay_ms = c->delay_ms;
    out->polarity = c->polarity;
    out->gain_db = 0.0;
    if (c->xover_makeup)
        out->gain_db += zp_linkwitz_makeup_db(d->sample_rate, c->hp_freq,
                                              c->hp_order, c->lp_freq,
                                              c->lp_order, c->hp_enabled,
                                              c->lp_enabled);
    if (d->auto_preamp)
        out->gain_db += zp_design_total_gain(d, ch);
}

double zp_preamp_for(const zp_design *d, int ch, double f_lo, double f_hi)
{
    zp_cascade c;
    double worst;
    zp_design_cascade(d, ch, &c);
    worst = zp_cascade_max_db(&c, d->sample_rate, f_lo, f_hi, 2.0);
    if (worst > 0.0)
        return -worst;
    return 0.0;
}

double zp_design_total_gain(const zp_design *d, int ch)
{
    const zp_channel_cfg *c;
    double peak = 0;
    int i;

    if (ch < 0 || ch >= ZP_CHANNELS)
        return 0;
    c = &d->ch[ch];
    for (i = 0; i < ZP_BANDS; i++) {
        double a = c->gain_db[i];
        if (a > peak)
            peak = a;
    }
    if (c->tone_enabled && c->tone_db > peak)
        peak = c->tone_db;
    return -peak;
}

int zp_curve_load_rew(zp_curve *c, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int count = 0;

    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f)) {
        double fr, spl;
        if (sscanf(line, " %lf %lf", &fr, &spl) != 2)
            continue;
        if (fr <= 0.0)
            continue;
        if (count >= ZP_MAX_POINTS)
            break;
        c->freq[count] = fr;
        c->spl[count] = spl;
        count++;
    }
    fclose(f);
    c->count = count;
    return count > 0 ? 0 : -1;
}

int zp_curve_from_target(zp_curve *c, const double *freq, const double *spl,
                         int n)
{
    int i;
    if (n <= 0 || n > ZP_MAX_POINTS)
        return -1;
    for (i = 0; i < n; i++) {
        c->freq[i] = freq[i];
        c->spl[i] = spl[i];
    }
    c->count = n;
    return 0;
}

static double interp(const zp_curve *c, double f)
{
    int i;
    if (c->count < 2)
        return c->count ? c->spl[0] : 0.0;
    if (f <= c->freq[0])
        return c->spl[0];
    if (f >= c->freq[c->count - 1])
        return c->spl[c->count - 1];
    for (i = 1; i < c->count; i++) {
        if (f <= c->freq[i]) {
            double f0 = c->freq[i - 1], f1 = c->freq[i];
            double s0 = c->spl[i - 1], s1 = c->spl[i];
            double t = (f1 == f0) ? 0.0 : (f - f0) / (f1 - f0);
            return s0 + t * (s1 - s0);
        }
    }
    return c->spl[c->count - 1];
}

static double zp_biquad_peaking_db(double fs, double f0, double q,
                                  double gain_db, double f)
{
    zp_cascade c;
    zp_cascade_init(&c);
    zp_cascade_push(&c, zp_biquad_peaking(fs, f0, q, gain_db));
    return zp_cascade_response_db(&c, fs, f);
}



int zp_autoeq(const zp_curve *measured, const zp_curve *target, double fs,
              double ridge, double max_gain_db, double out_gain[ZP_BANDS])
{
    const int max_pts = 512;
    static double freq[512];
    static double logt[512];
    static double basis[512][ZP_BANDS];
    static double ata_reg[ZP_BANDS][ZP_BANDS];
    static double lhs[ZP_BANDS][ZP_BANDS + 1];
    const double q = 1.45;
    const double step = 1.0 / 12.0;
    const double probe_gain = 6.0;
    int i, j, k, m, it, nf = 0;
    double f;

    if (!measured || !target || measured->count < 2 || target->count < 2)
        return -1;

    for (f = zp_band_freq(0); f <= zp_band_freq(ZP_BANDS - 1) * 1.02;
         f *= pow(2.0, step)) {
        double lin;
        if (nf >= max_pts)
            break;
        freq[nf] = f;
        lin = pow(10.0, (interp(target, f) - interp(measured, f)) / 20.0);
        logt[nf] = log(lin < 1e-9 ? 1e-9 : lin);
        nf++;
    }
    if (nf < ZP_BANDS)
        return -1;

    for (i = 0; i < ZP_BANDS; i++)
        out_gain[i] = 0.0;

    for (it = 0; it < 20; it++) {
        double trace = 0;
        double cur[512];

        for (k = 0; k < nf; k++) {
            double mag = 1.0;
            for (i = 0; i < ZP_BANDS; i++)
                mag *= pow(10.0, zp_biquad_peaking_db(fs, zp_band_freq(i), q,
                                                      out_gain[i], freq[k]) /
                                  20.0);
            cur[k] = log(mag < 1e-9 ? 1e-9 : mag);
        }

        for (k = 0; k < nf; k++)
            for (i = 0; i < ZP_BANDS; i++) {
                zp_cascade c;
                zp_cascade_init(&c);
                zp_cascade_push(&c, zp_biquad_peaking(fs, zp_band_freq(i), q,
                                                      probe_gain));
                basis[k][i] =
                    zp_cascade_response_db(&c, fs, freq[k]) / probe_gain;
            }

        for (i = 0; i < ZP_BANDS; i++)
            for (j = 0; j < ZP_BANDS; j++) {
                double sum = 0;
                for (k = 0; k < nf; k++)
                    sum += basis[k][i] * basis[k][j];
                ata_reg[i][j] = sum;
            }
        for (i = 0; i < ZP_BANDS; i++)
            trace += ata_reg[i][i];
        trace /= (double)ZP_BANDS;
        for (i = 0; i < ZP_BANDS; i++)
            ata_reg[i][i] += ridge * (trace > 0 ? trace : 1.0);

        for (i = 0; i < ZP_BANDS; i++) {
            double rhs = 0;
            for (k = 0; k < nf; k++)
                rhs += basis[k][i] * (logt[k] - cur[k]);
            for (j = 0; j < ZP_BANDS; j++)
                lhs[i][j] = ata_reg[i][j];
            lhs[i][ZP_BANDS] = rhs;
        }

        for (m = 0; m < ZP_BANDS; m++) {
            double piv = lhs[m][m];
            if (fabs(piv) < 1e-12)
                return -1;
            for (j = m; j <= ZP_BANDS; j++)
                lhs[m][j] /= piv;
            for (i = 0; i < ZP_BANDS; i++) {
                double fct;
                if (i == m)
                    continue;
                fct = lhs[i][m];
                if (fct == 0.0)
                    continue;
                for (j = m; j <= ZP_BANDS; j++)
                    lhs[i][j] -= fct * lhs[m][j];
            }
        }

        {
            double moved = 0;
            for (i = 0; i < ZP_BANDS; i++) {
                double g = zp_clampd(out_gain[i] + zp_clampd(lhs[i][ZP_BANDS],
                                                             -4.0, 4.0),
                                     -max_gain_db, max_gain_db);
                if (fabs(g - out_gain[i]) > moved)
                    moved = fabs(g - out_gain[i]);
                out_gain[i] = g;
            }
            if (moved < 1e-4)
                break;
        }
    }
    return 0;
}

void zp_design_apply_autoeq(zp_channel_cfg *c, const double *gains)
{
    int i;
    for (i = 0; i < ZP_BANDS; i++)
        c->gain_db[i] = zp_clampd(gains[i], -ZP_MAX_GAIN, ZP_MAX_GAIN);
}
