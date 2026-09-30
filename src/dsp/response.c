#include "response.h"
#include <math.h>

const char *zp_response_mode_name(int mode)
{
    static const char *names[]={"HPF + EQ + LPF","HPF only","LPF only","EQ only","HPF + LPF"};
    return mode>=0&&mode<ZP_RESPONSE_MODES?names[mode]:"Invalid mode";
}
static void log_section(FILE *f,const char *type,double freq,double gain,double q,int slope,int index,const zp_biquad *b)
{
    if(!f)return;
    fprintf(f,"%s section=%d frequency=%.17g gain=%.17g Q=%.17g slope=%d\n"
              "  normalized b0=%.17g b1=%.17g b2=%.17g a0=1 a1=%.17g a2=%.17g\n",
              type,index,freq,gain,q,slope,b->b0,b->b1,b->b2,b->a1,b->a2);
}
static int crossover(const zp_response_filter *filter,double fs,int hp,zp_cascade *out,FILE *log)
{
    if(!filter->known)return ZP_RESPONSE_UNSUPPORTED;
    if(!filter->slope){if(log)fprintf(log,"%s BYPASS (OFF)\n",hp?"HPF":"LPF");return ZP_RESPONSE_OK;}
    if(filter->family!=0&&filter->family!=2)return ZP_RESPONSE_UNSUPPORTED;
    if(filter->family==2&&filter->slope%12)return ZP_RESPONSE_UNSUPPORTED;
    int start=out->count;
    int result=filter->family==0?zp_cascade_push_butterworth(out,fs,filter->frequency,filter->slope,hp)
                              :zp_cascade_push_lr(out,fs,filter->frequency,filter->slope,hp);
    if(result)return ZP_RESPONSE_INVALID;
    /* Log the prototype Q for every generated SOS/first-order section. */
    int order=filter->slope/(filter->family==2?12:6),index=start;
    for(int repeat=0;repeat<(filter->family==2?2:1);repeat++) {
        char type[40];snprintf(type,sizeof(type),"%s %s",hp?"HPF":"LPF",filter->family==2?"Linkwitz-Riley":"Butterworth");
        if(order%2){log_section(log,type,filter->frequency,0,0,filter->slope,index-start+1,&out->stage[index]);index++;}
        for(int k=order/2-1;k>=0;k--) {
            double q=1/(2*sin(3.14159265358979323846*(2*k+1)/(2*order)));
            log_section(log,type,filter->frequency,0,q,filter->slope,index-start+1,&out->stage[index]);index++;
        }
    }
    return ZP_RESPONSE_OK;
}
int zp_response_build(const zp_response_config *cfg,int mode,zp_cascade *out,FILE *log)
{
    if(!out)return ZP_RESPONSE_INVALID;
    zp_cascade_init(out);
    if(!cfg||!isfinite(cfg->sample_rate)||cfg->sample_rate<=0||mode<0||mode>=ZP_RESPONSE_MODES)return ZP_RESPONSE_INVALID;
    double fs=cfg->sample_rate;int status=ZP_RESPONSE_OK;
    if(log)fprintf(log,"Mode: %s Fs=%.17g Nyquist=%.17g\n",zp_response_mode_name(mode),fs,fs/2);
    if(mode==ZP_RESPONSE_FULL||mode==ZP_RESPONSE_HPF||mode==ZP_RESPONSE_CROSSOVER)
        status=crossover(&cfg->hp,fs,1,out,log);
    if(status)goto failed;
    if(mode==ZP_RESPONSE_FULL||mode==ZP_RESPONSE_EQ) {
        if(!cfg->eq_enabled){if(log)fprintf(log,"EQ BYPASS (channel enable = 0)\n");}
        else for(int band=0;band<31;band++) {
            const zp_response_eq *eq=&cfg->eq[band];
            if(!eq->enabled){if(log)fprintf(log,"EQ band=%d BYPASS\n",band+1);continue;}
            if(!isfinite(eq->frequency)||eq->frequency<=0||eq->frequency>=fs/2||!isfinite(eq->q)||eq->q<=0||
               !isfinite(eq->gain)||eq->gain < -30||eq->gain > 30){status=ZP_RESPONSE_INVALID;goto failed;}
            zp_biquad section;const char *type;double q=eq->q;
            if(eq->type==7){section=zp_biquad_peaking(fs,eq->frequency,q,eq->gain);type="PEQ";}
            /* APK chart/c.java uses min(Q,2) for its shelf model. */
            else if(eq->type==5){q=fmin(q,2);section=zp_biquad_highshelf(fs,eq->frequency,q,eq->gain);type="High shelf";}
            else if(eq->type==6){q=fmin(q,2);section=zp_biquad_lowshelf(fs,eq->frequency,q,eq->gain);type="Low shelf";}
            else {status=ZP_RESPONSE_UNSUPPORTED;goto failed;}
            zp_cascade_push(out,section);
            log_section(log,type,eq->frequency,eq->gain,q,0,band+1,&section);
        }
    }
    if(mode==ZP_RESPONSE_FULL||mode==ZP_RESPONSE_LPF||mode==ZP_RESPONSE_CROSSOVER)
        status=crossover(&cfg->lp,fs,0,out,log);
    if(status)goto failed;
    return ZP_RESPONSE_OK;
failed:
    if(log)fprintf(log,"Response unavailable: %s; partial cascade not plotted.\n",status==ZP_RESPONSE_UNSUPPORTED?"unsupported filter":"invalid parameter");
    zp_cascade_init(out);
    return status;
}
double zp_response_frequency(int index,int count,double fs)
{
    if(count<2||index<0||index>=count||!isfinite(fs)||fs/2<=ZP_RESPONSE_MIN)return NAN;
    double high=fmin(ZP_RESPONSE_MAX,nextafter(fs/2,0));
    if(index==count-1)return high;
    return fmin(high,ZP_RESPONSE_MIN*pow(high/ZP_RESPONSE_MIN,(double)index/(count-1)));
}
