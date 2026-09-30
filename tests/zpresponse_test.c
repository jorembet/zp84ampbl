#include "../src/dsp/response.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void near(double got,double want,double tol){assert(isfinite(got));assert(fabs(got-want)<tol);}
static zp_response_config fixture(void)
{
    static const double freq[31]={20,25,32,40,50,63,80,100,125,160,200,250,315,400,500,630,800,1000,1250,1600,2000,2500,3150,4000,5000,6300,8000,10000,12500,16000,20000};
    zp_response_config cfg={.sample_rate=48000,.hp={0,24,1,100},.lp={2,36,1,6000},.eq_enabled=1};
    for(int i=0;i<31;i++)cfg.eq[i]=(zp_response_eq){7,1,freq[i],0,7.57/(19.0/6)};
    cfg.eq[7].gain=1.9;cfg.eq[8].gain=3.3;cfg.eq[9].gain=3;cfg.eq[10].gain=1.9;
    return cfg;
}
int main(int argc,char **argv)
{
    zp_response_config cfg=fixture();zp_cascade modes[ZP_RESPONSE_MODES];
    for(int m=0;m<ZP_RESPONSE_MODES;m++)assert(zp_response_build(&cfg,m,&modes[m],NULL)==0);
    near(zp_cascade_response_db(&modes[ZP_RESPONSE_HPF],48000,100),-3.01029995664,1e-7);
    near(zp_cascade_response_db(&modes[ZP_RESPONSE_LPF],48000,6000),-6.02059991328,1e-7);
    FILE *csv=argc>1?fopen(argv[1],"w"):NULL;
    if(argc>1)assert(csv);
    if(csv)fprintf(csv,"frequency,full,hpf,lpf,eq,crossover\n");
    double lastlp=1,lasthp=-1e9,lastfull=1e9;
    for(int i=0;i<2048;i++) {
        double f=zp_response_frequency(i,2048,48000),db[5];
        assert(f>=20&&f<=20000&&f<24000);
        for(int m=0;m<5;m++){db[m]=zp_cascade_response_db(&modes[m],48000,f);assert(isfinite(db[m]));}
        assert(db[1]>=lasthp-1e-8);assert(db[2]<=lastlp+1e-8);
        if(f>=6000)assert(db[0]<lastfull);
        if(f<50||f>10000)assert(db[3]<0.5); /* No EQ peaks away from enabled boosts. */
        near(db[0],db[1]+db[2]+db[3],1e-8);
        lastlp=db[2];lasthp=db[1];lastfull=db[0];
        if(csv)fprintf(csv,"%.12g,%.12g,%.12g,%.12g,%.12g,%.12g\n",f,db[0],db[1],db[2],db[3],db[4]);
    }
    if(csv)assert(fclose(csv)==0);
    assert(isnan(zp_cascade_response_db(&modes[0],48000,24001)));
    /* Independent impulse DFT verifies phase, denominator signs, and normalization. */
    zp_biquad b=zp_biquad_peaking(48000,125,7.57,3.3);zp_biquad_state state;
    zp_biquad_reset(&state);double re=0,im=0,w=2*acos(-1)*125/48000;
    for(int n=0;n<48000;n++){double y=zp_biquad_tick(&b,&state,n==0);re+=y*cos(w*n);im-=y*sin(w*n);}
    zp_complex_response h=zp_biquad_response(&b,48000,125);
    near(h.re,re,1e-8);near(h.im,im,1e-8);near(20*log10(hypot(h.re,h.im)),3.3,1e-8);
    /* Butterworth orders 1..8, LR orders 2/4/6/8, independently checked BLT formula. */
    for(int slope=6;slope<=48;slope+=6)for(int hp=0;hp<2;hp++) {
        zp_cascade c;zp_cascade_init(&c);
        assert(zp_cascade_push_butterworth(&c,48000,1000,slope,hp)==0);
        double ratio=tan(acos(-1)*2000/48000)/tan(acos(-1)*1000/48000);if(hp)ratio=1/ratio;
        near(zp_cascade_response_db(&c,48000,2000),-10*log10(1+pow(ratio,2*slope/6)),1e-8);
        if(slope%12==0){zp_cascade_init(&c);assert(zp_cascade_push_lr(&c,48000,1000,slope,hp)==0);
            near(zp_cascade_response_db(&c,48000,2000),-20*log10(1+pow(ratio,slope/6)),1e-8);}
    }
    for(int i=0;i<31;i++)cfg.eq[i].gain=0;
    zp_cascade c;assert(zp_response_build(&cfg,ZP_RESPONSE_EQ,&c,NULL)==0&&c.count==31);
    near(zp_cascade_response_db(&c,48000,50),0,1e-8);
    cfg.eq_enabled=0;cfg.eq[0].type=999;
    assert(zp_response_build(&cfg,ZP_RESPONSE_EQ,&c,NULL)==0&&c.count==0);
    cfg.eq_enabled=1;assert(zp_response_build(&cfg,ZP_RESPONSE_EQ,&c,NULL)==ZP_RESPONSE_UNSUPPORTED&&c.count==0);
    cfg=fixture();cfg.hp.family=1;assert(zp_response_build(&cfg,ZP_RESPONSE_FULL,&c,NULL)==ZP_RESPONSE_UNSUPPORTED);
    cfg.hp.slope=0;cfg.lp.slope=0;cfg.eq_enabled=0;
    assert(zp_response_build(&cfg,ZP_RESPONSE_FULL,&c,NULL)==0&&c.count==0);
    for(int rate=32000;rate<=96000;rate+=16000)assert(zp_response_frequency(2047,2048,rate)<rate/2.0);
    /* Genuine narrow EQ peaks at both extremes must remain visible. */
    cfg=fixture();cfg.eq[0].gain=6;cfg.eq[30].gain=6;
    assert(zp_response_build(&cfg,ZP_RESPONSE_EQ,&c,NULL)==0);
    assert(zp_cascade_response_db(&c,48000,20)>5.99);
    assert(zp_cascade_response_db(&c,48000,20000)>5.99);
    cfg=fixture();cfg.eq[0].frequency=24000;assert(zp_response_build(&cfg,ZP_RESPONSE_EQ,&c,NULL)==ZP_RESPONSE_INVALID);
    puts("Frequency response: 2048 samples, isolated filters, full cascade, impulse DFT, bypass and Nyquist PASS");
    return 0;
}
