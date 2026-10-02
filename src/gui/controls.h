#ifndef ZP_CONTROLS_H
#define ZP_CONTROLS_H
/* Edits are committed only after a device acknowledgement AND an independent
   readback. A failed transaction invalidates the snapshot, including partial
   multi-register operations; the user must read again before further edits. */
static void layout_set_width(double value);
static int preset_save_named(int slot,const char *name);

static int edit_id, edit_kind, output_locked, link_output;
static char edit_text[40], edit_label[80];
static double edit_min, edit_max;
static int fader_kind, fader_index, fader_channel, fader_y, fader_x;
static int edit_selected, edit_return;
static const int noise_codes[]={0,1,2,3,4,5,6,7,8,9,10,11,13,14,16,18,23,29,41,65,103};
static int noise_index(unsigned raw)
{
    for(int i=0;i<21;i++)if(raw==(unsigned)noise_codes[i])return i;
    return -1;
}
static uint16_t eq_undo[8][93];
static int eq_undo_valid[8];

/* Shared geometry for the eight-column Remix dialog and its drag controls. */
static int mixer_rows(void){return A.dsp_values[1554]==3?4:2;}
static int mixer_top(void){return (800-(132+65*mixer_rows()))/2;}
static int mixer_cell_x(int channel){return 210+112*channel;}
static int mixer_cell_y(int input){return mixer_top()+40+65*input;}
static unsigned mixer_drag_gain(void)
{return (unsigned)lround(100*zp_clampd((fader_x-(mixer_cell_x(fader_channel)+10))/80.0,0,1));}

static int control_ready(void)
{
    if (A.connected && A.dsp_valid && !A.dsp_reading) return 1;
    snprintf(A.console_notice,sizeof(A.console_notice),"Hubungkan USB lalu klik Baca DSP: data USB lengkap diperlukan.");
    return 0;
}
static int control_write(int id, uint16_t value)
{
    if (!control_ready() || id < 0 || id >= ZP_PARAM_COUNT) return 0;
    if(!protection_check(id,value))return 0;
    int err=zp_id_write(&A.conn,(uint16_t)id,value);
    if (err) {
        A.dsp_valid=0;link_output=0;
        memset(eq_undo_valid,0,sizeof(eq_undo_valid));
        snprintf(A.console_notice,sizeof(A.console_notice),"Gagal verifikasi ID %04X (%s). Baca DSP sebelum mencoba lagi.",id,zp_strerror(err));
        return 0;
    }
    A.dsp_values[id]=value;
    snprintf(A.console_notice,sizeof(A.console_notice),"ID %04X = %u: tersimpan dan terverifikasi melalui USB.",id,value);
    return 1;
}
/* Each bit links only one stereo pair: 1/2, 3/4, 5/6 or 7/8.
   Copy only mapped channel processing, never reserved registers or input routing. */
static int channel_linked(int ch){return (link_output&(1<<(ch/2)))!=0;}
static int channel_setting_id(int ch,int index)
{
    if(index<93)return 147+136*ch+4*(index/3)+index%3;
    static const int ids[]={138,139,142,143,73,26,2};
    return ids[index-93]+(index<97?136*ch:ch);
}
static int control_set(int id,uint16_t value)
{
    int partner=-1;
    for(int ch=0;ch<8&&partner<0;ch++)if(channel_linked(ch))
        for(int i=0;i<100;i++)if(channel_setting_id(ch,i)==id){partner=channel_setting_id(ch^1,i);break;}
    if(!protection_check(id,value)||(partner>=0&&!protection_check(partner,value)))return 0;
    if(!control_write(id,value))return 0;
    if(partner>=0&&!control_write(partner,value)) {
        snprintf(A.console_notice,sizeof(A.console_notice),"Link gagal pada ID %04X; hasil parsial. Link dilepas, Baca DSP ulang.",partner);
        return 0;
    }
    return 1;
}
static void control_link_toggle(void)
{
    int ch=A.cur_ch,other=ch^1,bit=1<<(ch/2);
    if(link_output&bit) {
        link_output&=~bit;
        snprintf(A.console_notice,sizeof(A.console_notice),"Link CH%d/CH%d dilepas; pengaturan tetap.",(ch/2)*2+1,(ch/2)*2+2);
        return;
    }
    if(output_locked){snprintf(A.console_notice,sizeof(A.console_notice),"Unlock output sebelum menyalin pengaturan pasangan.");return;}
    if(!control_ready())return;
    for(int i=0;i<100;i++)if(!protection_check(channel_setting_id(other,i),A.dsp_values[channel_setting_id(ch,i)]))return;
    eq_undo_valid[ch]=eq_undo_valid[other]=0;
    int done=0;
    for(int i=0;i<100;i++) {
        int from=channel_setting_id(ch,i),to=channel_setting_id(other,i);
        if(A.dsp_values[from]==A.dsp_values[to])continue;
        if(!control_write(to,A.dsp_values[from])) {
            snprintf(A.console_notice,sizeof(A.console_notice),"Copy CH%d > CH%d gagal setelah %d write. Link OFF; Baca DSP ulang.",ch+1,other+1,done);
            return;
        }
        done++;
    }
    link_output|=bit;
    snprintf(A.console_notice,sizeof(A.console_notice),"Link ON: CH%d > CH%d. Volume, EQ, crossover, phase, mute dan delay sama.",ch+1,other+1);
}

static uint16_t level_raw(double level, int phase)
{
    int n=(int)lround(level);
    return (uint16_t)((phase?0:5000)+(n? n+40:0)*10);
}
static void control_level(int ch, double level)
{
    if (!control_ready()) return;
    if (output_locked) {snprintf(A.console_notice,sizeof(A.console_notice),"Output terkunci. Klik Unlock output untuk mengedit.");return;}
    if(ch<0) {
        for(int c=0;c<8;c++)if(!protection_check(12+c,level_raw(level,0)))return;
        for(int c=0;c<8;c++) if(!control_write(12+c,level_raw(level,0)))break;
    } else {
        control_set(26+ch,level_raw(level,A.dsp_values[26+ch]<5000));
    }
}
/* kind: 0 integer, 1 Hz, 2 dB, 3 Q, 4 delay, 5 channel level, 6 mixer %,
   7 layout width, 8 input volume, 9 noise gate index. */
static void control_edit(int id,int kind,const char *label,double value,double low,double high)
{
    if(kind!=7&&!control_ready())return;
    edit_selected=1;edit_return=kind==7?8:A.console_modal;
    edit_id=id;edit_kind=kind;edit_min=low;edit_max=high;
    snprintf(edit_label,sizeof(edit_label),"%s (%.2f ... %.2f)",label,low,high);
    snprintf(edit_text,sizeof(edit_text),"%.3f",value);
    A.console_modal=6;
}
static void control_apply(void)
{
    if(edit_kind==11) {
        if(speaker_name_save(edit_id,edit_text))A.console_modal=edit_return;
        return;
    }
    if(edit_kind==10) {
        if(preset_save_named(edit_id,edit_text))A.console_modal=1;
        return;
    }
    char *end;errno=0;double v=strtod(edit_text,&end);
    if(errno||end==edit_text||*end||!isfinite(v)||v<edit_min||v>edit_max) {
        snprintf(A.console_notice,sizeof(A.console_notice),"Nilai tidak valid. Rentang %.2f sampai %.2f.",edit_min,edit_max);return;
    }
    if(edit_kind==7){layout_set_width(v);A.console_modal=8;return;}
    if ((edit_kind==4||edit_kind==5||edit_kind==6)&&output_locked) {snprintf(A.console_notice,sizeof(A.console_notice),"Output terkunci. Buka Lock output sebelum mengubah mixer atau output.");return;}
    if (edit_kind==5) {if(!control_ready()||output_locked)return;control_level(edit_id,v);if(A.dsp_valid)A.console_modal=0;return;}
    uint16_t raw=(uint16_t)lround(v);
    if(edit_kind==1) raw=v<100?(uint16_t)((int)lround(v*10)|0x8000):(uint16_t)lround(v);
    if(edit_kind==2) raw=(uint16_t)lround(v*10+500);
    if(edit_kind==3) raw=(uint16_t)lround(v*600/19);
    if(edit_kind==4) {double ms=A.delay_unit==0?v:A.delay_unit==1?v/34.6:v*2.54/34.6;raw=(uint16_t)lround(round(ms*48)*1000/48);}
    if(edit_kind==8)raw=(uint16_t)(500+lround(v));
    if(edit_kind==9)raw=(uint16_t)noise_codes[(int)lround(v)];
    if(edit_kind==6)raw=(uint16_t)((int)lround(v)*256+(A.dsp_values[edit_id]&1));
    if(control_set(edit_id,raw)) A.console_modal=edit_return;
}
static void control_key(XKeyEvent *event,KeySym ks)
{
    size_t n=strlen(edit_text);
    if(ks==XK_Escape){A.console_modal=edit_return;return;}
    if((event->state & ControlMask)&&(ks==XK_a||ks==XK_A)){edit_selected=1;return;}
    if(ks==XK_Return||ks==XK_KP_Enter){control_apply();return;}
    if(ks==XK_BackSpace){if(edit_selected)edit_text[0]=0;else if(n)edit_text[n-1]=0;edit_selected=0;return;}
    if(ks==XK_Delete){edit_text[0]=0;return;}
    char chars[16];KeySym key;int count=XLookupString(event,chars,sizeof(chars),&key,NULL);
    for(int i=0;i<count&&(edit_selected||n+1<sizeof(edit_text));i++)if(((edit_kind==10||edit_kind==11)&&chars[i]>=32&&chars[i]<=126)||(edit_kind!=10&&edit_kind!=11&&((chars[i]>='0'&&chars[i]<='9')||chars[i]=='.'||chars[i]==','||chars[i]=='-'))) {
        if(edit_selected){n=0;edit_selected=0;}
        edit_text[n++]=edit_kind!=10&&edit_kind!=11&&chars[i]==','?'.':chars[i];
    }
    edit_text[n]=0;
}
static void control_release(int y)
{
    int kind=fader_kind,index=fader_index;fader_kind=0;
    if(!kind||!control_ready())return;
    if(kind==5&&output_locked){snprintf(A.console_notice,sizeof(A.console_notice),"Output terkunci. Buka Lock output sebelum mengubah mixer.");return;}
    if(kind==1)control_set(148+136*fader_channel+4*index,(uint16_t)lround(500+120*(1-2*zp_clampd((y-428)/69.0,0,1))));
    if(kind==2)control_level(index,60*(1-zp_clampd((y-634)/79.0,0,1)));
    if(kind==5)control_set(index,(uint16_t)(256*mixer_drag_gain()+(A.dsp_values[index]&1)));
    if(kind==4)control_set(index,(uint16_t)(500+lround(100*zp_clampd((fader_x-610)/230.0,0,1))));
    if(kind==3)control_level(-1,60*(1-zp_clampd((y-628)/99.0,0,1)));
}
static void control_eq_reset(int restore)
{
    if(!control_ready())return;
    int ch=A.cur_ch;
    if(restore&&!eq_undo_valid[ch]){snprintf(A.console_notice,sizeof(A.console_notice),"Belum ada Reset EQ yang dapat dikembalikan pada channel ini.");return;}
    if(!restore) {
        for(int b=0;b<31;b++)for(int r=0;r<3;r++)eq_undo[ch][3*b+r]=A.dsp_values[147+136*ch+4*b+r];
        eq_undo_valid[ch]=1;
        if(channel_linked(ch)){memcpy(eq_undo[ch^1],eq_undo[ch],sizeof(eq_undo[ch]));eq_undo_valid[ch^1]=1;}
    }
    for(int b=0;b<31;b++) {
        if(restore) {for(int r=0;r<3;r++)if(!control_set(147+136*ch+4*b+r,eq_undo[ch][3*b+r]))return;}
        else if(!control_set(148+136*ch+4*b,500))return;
    }
    if(restore){eq_undo_valid[ch]=0;if(channel_linked(ch))eq_undo_valid[ch^1]=0;}
}
static int console_control_click(int x,int y)
{
    int base=136*A.cur_ch;
    for(int f=0;f<2;f++) {
        int xx=18+91*f;
        if(hit(xx,169,72,21,x,y)) {control_edit(base+139+4*f,1,f?"LPF Hz":"HPF Hz",dsp_frequency(A.dsp_values[base+139+4*f]),10,23000);return 1;}
        if(hit(xx,220,72,21,x,y)||hit(xx,270,72,21,x,y)) {
            if(!control_ready())return 1;
            console_filter v=decode_filter(A.dsp_values[base+138+4*f],!f);
            if(!v.known){v.family=0;v.slope=0;}
            int family=v.family,rate=v.slope?v.slope/6-1:8;
            if(y<250)family=(family+1)%3;else rate=(rate+1)%9;
            static const int codes[]={40,8,14,20,46,52,58,26,64,42,10,16,22,48,54,60,28,66,44,12,18,24,50,56,62,30,68};
            control_set(base+138+4*f,(uint16_t)(codes[family*9+rate]+f));
            return 1;
        }
    }
    if(hit(232,337,992,161,x,y)) {
        int band=(x-232)/32,id=base+147+4*band;A.console_band=band;
        if(y<357)control_edit(id,1,"EQ frequency Hz",dsp_frequency(A.dsp_values[id]),20,20000);
        else if(y>=362&&y<382)control_edit(id+2,3,"EQ Q",A.dsp_values[id+2]*19.0/600,0.1,30);
        else if(y>=387&&y<407)control_edit(id+1,2,"EQ gain dB",((double)A.dsp_values[id+1]-500)/10,-12,12);
        else if(y>=417){if(control_ready()){fader_kind=1;fader_index=band;fader_channel=A.cur_ch;fader_y=y;}}
        return 1;
    }
    if(hit(21,588,61,21,x,y)){if(!output_locked)control_edit(-1,5,"Master level",dsp_channel_level(A.dsp_values[12]),0,60);return 1;}
    if(hit(35,625,42,110,x,y)){if(!output_locked&&control_ready()){fader_kind=3;fader_y=y;}return 1;}
    if(hit(37,739,34,20,x,y)) {
        if(!output_locked&&control_ready()){int mute=0;for(int c=0;c<8;c++)if(!A.dsp_values[2+c])mute=1;
            for(int c=0;c<8;c++)if(!control_set(2+c,(uint16_t)mute))break;}
        return 1;
    }
    for(int c=0;c<8;c++) {
        int xx=196+c*114;
        if(hit(xx+8,595,96,21,x,y)) {
            edit_id=c;edit_kind=11;edit_selected=1;edit_return=0;
            snprintf(edit_label,sizeof(edit_label),"Nama speaker CH%d (maksimal 24 karakter)",c+1);
            snprintf(edit_text,sizeof(edit_text),"%s",CH_ROLE[c]);
            A.console_modal=6;return 1;
        }
        if(hit(xx+47,625,61,32,x,y)){if(!output_locked)control_edit(c,5,"Output level",dsp_channel_level(A.dsp_values[26+c]),0,60);return 1;}
        if(hit(xx+17,623,31,103,x,y)){if(!output_locked&&control_ready()){fader_kind=2;fader_index=c;fader_y=y;}return 1;}
        if(hit(xx+62,675,38,19,x,y)) {
            if(control_ready()&&!output_locked)control_set(26+c,(uint16_t)(A.dsp_values[26+c]<5000?A.dsp_values[26+c]+5000:A.dsp_values[26+c]-5000));
            return 1;
        }
        if(hit(xx+64,704,34,20,x,y)) {
            if(control_ready()&&!output_locked)control_set(2+c,!A.dsp_values[2+c]);
            return 1;
        }
        if(hit(xx+7,734,100,27,x,y)) {
            double factor=A.delay_unit==0?1:A.delay_unit==1?34.6:34.6/2.54;
            if(!output_locked)control_edit(73+c,4,"Delay",dsp_delay_ms(A.dsp_values[73+c])*factor,0,20*factor);
            return 1;
        }
    }
    if(hit(1123,559,102,185,x,y)) {
        int action=(y-559)/32;
        if((y-559)%32>=23)return 1;
        if(action==0)control_edit(base+148+4*A.console_band,2,"EQ gain dB",((double)A.dsp_values[base+148+4*A.console_band]-500)/10,-12,12);
        if(action==1)control_eq_reset(0);
        if(action==2)control_eq_reset(1);
        if(action==3&&!output_locked&&control_ready()) {
            if(control_set(26+A.cur_ch,5000))control_set(73+A.cur_ch,0);
        }
        if(action==4)control_link_toggle();
        if(action==5)output_locked=!output_locked;
        return 1;
    }
    return 0;
}
/* APK d/u.java and f/a.java: mixer gain in high byte, route enable bit 0.
   BTService uses one-based IDs; USB uses zero-based IDs. */
static int mixer_id(int source,int channel,int input)
{
    return (source==3?1226:source==1?1482:1360)+channel*8+input;
}
static const char *source_name(int source)
{
    switch(source){case 1:return "AUX";case 2:return "Bluetooth";case 3:return "High level";case 7:return "USB media";default:return "Unknown";}
}
static double fader_preview(int kind,int index,double value,int top,int height,double max)
{
    if(fader_kind==kind&&(kind==3||fader_index==index))return max*(1-zp_clampd((fader_y-top)/(double)height,0,1));
    return value;
}
#endif
