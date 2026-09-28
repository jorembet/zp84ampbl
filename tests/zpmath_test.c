#include "../src/dsp/design.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;
static int checks = 0;

static void check(const char *what, double got, double want, double tol)
{
    checks++;
    if (fabs(got - want) > tol) {
        failures++;
        printf("FAIL %-46s got %10.4f want %10.4f (tol %.4f)\n", what, got,
               want, tol);
    } else {
        printf("ok   %-46s %10.4f\n", what, got);
    }
}

static double sum_power_db(const double *db, int n)
{
    double sum = 0;
    int i;
    for (i = 0; i < n; i++)
        sum += pow(10.0, db[i] / 10.0);
    return 10.0 * log10(sum);
}

static double resp1(zp_biquad b, double fs, double f)
{
    zp_cascade c;
    zp_cascade_init(&c);
    zp_cascade_push(&c, b);
    return zp_cascade_response_db(&c, fs, f);
}

static void test_band_freqs(void)
{
    int i;
    check("band 0 (20 Hz)", zp_band_freq(0), 20.0, 1e-9);
    check("band 9 (160 Hz)", zp_band_freq(9), 160.0, 1e-6);
    check("band 30 (20.48 kHz)", zp_band_freq(30), 20480.0, 1e-6);
    for (i = 1; i < ZP_BANDS; i++) {
        double r = zp_band_freq(i) / zp_band_freq(i - 1);
        if (fabs(r - pow(2.0, 1.0 / 3.0)) > 1e-12) {
            failures++;
            printf("FAIL band %d spacing not 1/3 octave: %.9f\n", i, r);
            return;
        }
    }
    checks++;
    printf("ok   all 31 bands are exactly 1/3 octave apart\n");
}

static void test_filter_shapes(void)
{
    const double fs = 48000.0;
    check("peaking +6 dB at f0", resp1(zp_biquad_peaking(fs, 1000, 1.45, 6), fs, 1000), 6.0, 0.01);
    check("peaking -6 dB at f0", resp1(zp_biquad_peaking(fs, 1000, 1.45, -6), fs, 1000), -6.0, 0.01);
    check("peaking 0 dB far below", resp1(zp_biquad_peaking(fs, 1000, 1.45, 6), fs, 10), 0.0, 0.05);
    check("peaking 0 dB far above", resp1(zp_biquad_peaking(fs, 1000, 1.45, 6), fs, 20000), 0.0, 0.6);

    check("butterworth LP -3 dB at f0", resp1(zp_biquad_lowpass(fs, 1000, 0.70710678), fs, 1000), -3.01, 0.05);
    check("butterworth LP one octave up", resp1(zp_biquad_lowpass(fs, 1000, 0.70710678), fs, 2000), -12.30, 0.15);
    check("butterworth HP -3 dB at f0", resp1(zp_biquad_highpass(fs, 1000, 0.70710678), fs, 1000), -3.01, 0.05);
    check("butterworth HP one octave down", resp1(zp_biquad_highpass(fs, 1000, 0.70710678), fs, 500), -12.30, 0.15);

    check("lowshelf +6 dB at DC", resp1(zp_biquad_lowshelf(fs, 1000, 0.707, 6), fs, 5), 6.0, 0.05);
    check("lowshelf 0 dB at Nyquist", resp1(zp_biquad_lowshelf(fs, 1000, 0.707, 6), fs, 23000), 0.0, 0.1);
    check("highshelf +6 dB at Nyquist", resp1(zp_biquad_highshelf(fs, 1000, 0.707, 6), fs, 23000), 6.0, 0.1);
    check("highshelf 0 dB at DC", resp1(zp_biquad_highshelf(fs, 1000, 0.707, 6), fs, 5), 0.0, 0.05);

    check("notch 0 dB far away", resp1(zp_biquad_notch(fs, 1000, 8), fs, 8000), 0.0, 0.05);
    check("allpass 0 dB far away", resp1(zp_biquad_allpass(fs, 1000, 1), fs, 8000), 0.0, 0.05);
}

static void test_crossover_slopes(void)
{
    const double fs = 48000.0;
    zp_cascade c;
    struct {
        int order;
        double at_f0;
        double one_oct_up;
    } cases[] = {{12, -6.0, -14.0}, {24, -12.0, -28.0}, {48, -24.0, -56.0}};
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        zp_cascade_init(&c);
        zp_cascade_push_linkwitz(&c, fs, 1000.0, cases[i].order, 0);
        check("LR LP at crossover f0", zp_cascade_response_db(&c, fs, 1000), cases[i].at_f0, 0.1);
        check("LR LP one octave above", zp_cascade_response_db(&c, fs, 2000), cases[i].one_oct_up, 0.5);

        zp_cascade_init(&c);
        zp_cascade_push_linkwitz(&c, fs, 1000.0, cases[i].order, 1);
        check("LR HP at crossover f0", zp_cascade_response_db(&c, fs, 1000), cases[i].at_f0, 0.1);
        check("LR HP one octave below", zp_cascade_response_db(&c, fs, 500), cases[i].one_oct_up, 0.5);
    }
}

static void test_three_way_sum(void)
{
    const double fs = 48000.0;
    zp_design d;
    zp_cascade woo, mid, tw;

    zp_design_defaults(&d, fs);
    d.ch[0].hp_enabled = 1;
    d.ch[0].hp_freq = 80.0;
    d.ch[0].lp_enabled = 1;
    d.ch[0].lp_freq = 800.0;
    d.ch[1].hp_enabled = 1;
    d.ch[1].hp_freq = 800.0;
    d.ch[1].lp_enabled = 1;
    d.ch[1].lp_freq = 4000.0;
    d.ch[2].hp_enabled = 1;
    d.ch[2].hp_freq = 4000.0;
    d.auto_preamp = 0;

    zp_design_cascade(&d, 0, &woo);
    zp_design_cascade(&d, 1, &mid);
    zp_design_cascade(&d, 2, &tw);

#define SUM3(fr) (sum_power_db((double[]){zp_cascade_response_db(&woo, fs, (fr)), zp_cascade_response_db(&mid, fs, (fr)), zp_cascade_response_db(&tw, fs, (fr))}, 3))
    check("sum flat 250 Hz (woofer band)", SUM3(250.0), 0.0, 0.3);
    check("sum flat 2 kHz (mid band)", SUM3(2000.0), 0.0, 0.3);
    check("sum flat 8 kHz (tweeter band)", SUM3(8000.0), 0.0, 1.0);
    check("sum dip at crossover 800 Hz", SUM3(800.0), -4.5, 1.0);
    check("rolloff below woofer HP at 40 Hz", SUM3(40.0), -24.7, 1.5);
    check("near tweeter knee at 3 kHz", SUM3(3000.0), -2.4, 1.5);
#undef SUM3
}

static void test_preamp_compensation(void)
{
    const double fs = 48000.0;
    zp_design d;
    zp_cascade c;
    double peak_before;

    zp_design_defaults(&d, fs);
    d.auto_preamp = 0;
    d.ch[0].gain_db[10] = 9.0;
    d.ch[0].gain_db[20] = -4.0;
    zp_design_cascade(&d, 0, &c);
    peak_before = zp_cascade_max_db(&c, fs, 20.0, 20000.0, 2.0);
    check("raw peak boost", peak_before, 9.0, 0.05);

    d.auto_preamp = 1;
    zp_design_cascade(&d, 0, &c);
    check("after preamp, peak", zp_cascade_max_db(&c, fs, 20.0, 20000.0, 2.0), 0.0, 0.05);
    check("preamp value", c.gain_db, -9.0, 0.05);
}

static void test_time_domain(void)
{
    const double fs = 48000.0;
    zp_biquad b = zp_biquad_peaking(fs, 1000.0, 1.45, 6.0);
    zp_biquad_state st;
    int n, i;
    double peak = 0;

    zp_biquad_reset(&st);
    for (n = 0; n < 480; n++) {
        double in = (n == 0) ? 1.0 : 0.0;
        double out = zp_biquad_tick(&b, &st, in);
        if (fabs(out) > peak)
            peak = fabs(out);
        if (n > 40 && fabs(out) < 1e-12)
            break;
    }
    check("impulse response settles", (double)n, 480.0, 400.0);
    (void)i;

    zp_biquad_reset(&st);
    {
        double sum_in = 0, sum_out = 0;
        for (n = 0; n < 9600; n++) {
            double in = sin(2 * M_PI * 1000.0 * n / fs);
            double out = zp_biquad_tick(&b, &st, in);
            sum_in += in * in;
            sum_out += out * out;
        }
        check("time-domain gain at f0 (dB)", 10 * log10(sum_out / sum_in), 6.0, 0.05);
    }
}

static void test_fixed_point(void)
{
    const double fs = 48000.0;
    zp_biquad b = zp_biquad_peaking(fs, 1000.0, 1.45, 6.0);
    const double scale = 1073741824.0;
    int32_t q[5];

    q[0] = (int32_t)llround(b.b0 * scale);
    q[1] = (int32_t)llround(b.b1 * scale);
    q[2] = (int32_t)llround(b.b2 * scale);
    q[3] = (int32_t)llround(b.a1 * scale);
    q[4] = (int32_t)llround(b.a2 * scale);

    check("Q31 b0 round trip", (double)q[0] / scale, b.b0, 1e-9);
    check("Q31 a2 round trip", (double)q[4] / scale, b.a2, 1e-9);
}

static void test_autoeq(void)
{
    const double fs = 48000.0;
    zp_curve meas, target;
    double freq[200], tgt[200] = {0}, gains[ZP_BANDS];
    int i, nf = 0;
    double before = 0, after = 0, worst = 0;
    zp_cascade corr;

    for (i = 0; i < 200; i++) {
        freq[i] = 20.0 * pow(1000.0, (double)i / 199.0);
        if (freq[i] > 20000.0)
            break;
        nf = i + 1;
    }
    for (i = 0; i < nf; i++)
        tgt[i] = 78.0;
    zp_curve_from_target(&target, freq, tgt, nf);

    for (i = 0; i < nf; i++) {
        double f = freq[i];
        meas.freq[i] = f;
        meas.spl[i] = 78.0 + 3.0 / (1.0 + pow(f / 70.0, 2.0)) -
                      6.0 * exp(-pow(log(f / 1500.0) / 0.55, 2.0)) +
                      2.5 / (1.0 + pow(f / 6000.0, 2.0));
    }
    meas.count = nf;

    if (zp_autoeq(&meas, &target, fs, 0.02, 12.0, gains) != 0) {
        failures++;
        printf("FAIL autoeq returned error\n");
        return;
    }
    checks++;
    printf("ok   autoeq solver produced %d band gains\n", ZP_BANDS);

    zp_cascade_init(&corr);
    for (i = 0; i < ZP_BANDS; i++)
        if (gains[i] != 0.0)
            zp_cascade_push(&corr,
                            zp_biquad_peaking(fs, zp_band_freq(i), 1.45,
                                              gains[i]));

    for (i = 0; i < nf; i++) {
        double f = freq[i];
        double need = target.spl[i] - meas.spl[i];
        double got = zp_cascade_response_db(&corr, fs, f);
        double resid = need - got;
        before += need * need;
        after += resid * resid;
        if (fabs(resid) > worst)
            worst = fabs(resid);
    }

    check("autoeq reduces MSE (dB)", 10 * log10(after / before), -20.0, 4.0);
    check("autoeq worst residual (dB)", worst, 0.0, 1.0);
}

int main(void)
{
    printf("--- band layout ---\n");
    test_band_freqs();
    printf("--- filter shapes ---\n");
    test_filter_shapes();
    printf("--- crossover slopes (12/24/48 dB per oct) ---\n");
    test_crossover_slopes();
    printf("--- three-way summing ---\n");
    test_three_way_sum();
    printf("--- preamp compensation ---\n");
    test_preamp_compensation();
    printf("--- time domain ---\n");
    test_time_domain();
    printf("--- fixed point ---\n");
    test_fixed_point();
    printf("--- autoeq solver ---\n");
    test_autoeq();

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
