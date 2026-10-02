#ifndef ZP_CONSOLE_H
#define ZP_CONSOLE_H

/* Display verified USB values; edits use acknowledged writes and readback. */
static void ui_font(XFontStruct *font)
{
    A.active_font = font ? font : A.font_struct;
}

static void ui_box(int x, int y, int w, int h, const char *label, int state)
{
    unsigned long fill = state == 1 ? A.accent : state == 2 ? 0x19232e : 0x293847;
    frect(x,y,w,h,fill);
    fframe(x,y,w,h,state==1?A.accent:0x46586b);
    ctext(x,y+h/2+5,w,state==1?0x10232c:state==2?A.dim:A.text,label);
}

static void ui_text_fit(int x,int y,int width,unsigned long color,const char *text)
{
    char clipped[256];snprintf(clipped,sizeof(clipped),"%s",text);
    size_t length=strlen(clipped);
    if(text_width(clipped)>width) {
        while(length>3) {
            length--;clipped[length]=0;
            clipped[length-3]='.';clipped[length-2]='.';clipped[length-1]='.';
            if(text_width(clipped)<=width)break;
        }
    }
    dtext(x,y,color,clipped);
}

static void ui_value(int x, int y, int w, const char *text)
{
    frect(x, y, w, 21, 0x101a25);
    line(x, y, x+w, y, 0x101720);
    line(x, y+20, x+w, y+20, 0x476077);
    ctext(x, y+15, w, A.text, text);
}

static void ui_dot(int x, int y, int r, unsigned long color)
{
    XSetForeground(A.dpy, A.gc, 0x101720);
    XFillArc(A.dpy, A.buf, A.gc, ui_px(x-r-2), ui_px(y-r-2), (unsigned)ui_px(2*r+4), (unsigned)ui_px(2*r+4), 0, 360*64);
    XSetForeground(A.dpy, A.gc, color);
    XFillArc(A.dpy, A.buf, A.gc, ui_px(x-r), ui_px(y-r), (unsigned)ui_px(2*r), (unsigned)ui_px(2*r), 0, 360*64);
}

static void ui_fader(int x, int top, int height, double fraction, int known)
{
    frect(x-2, top, 4, height, 0x181916);
    line(x+2, top, x+2, top+height, 0x44453f);
    if (!known) return;
    int y = top + (int)((1-zp_clampd(fraction, 0, 1))*height);
    frect(x-1,y,3,top+height-y,A.accent);
    for (int j = -11; j <= 11; j++) {
        int g = j < 0 ? 218+j*3 : 218-j*2;
        unsigned long c = (unsigned long)g * 0x010101;
        frect(x-6, y+j, 13, 1, c);
    }
    fframe(x-6, y-11, 13, 23, 0x797b73);
    line(x-5, y, x+5, y, 0x5a5c54);
    line(x-5, y+1, x+5, y+1, 0xf5f5ed);
}

#include "output_marks.h"

/* Clickable installation marks, independent of DSP mute state. */
static void ui_output_hardware(int x,int channel)
{
    int y=650;
    int amp=(output_marks[channel]&1)!=0,rca=(output_marks[channel]&2)!=0;
    frect(x+46,y,62,23,0x14212d);
    if(channel<4)frect(x+46,y,62,11,amp?0x315446:0x14212d);
    frect(x+46,channel<4?y+12:y,62,channel<4?11:23,rca?0x264c55:0x14212d);
    ui_font(A.font_small);
    if(channel<4) {
        unsigned long color=amp?0xf4ba63:0x657786;
        fframe(x+49,y+1,16,9,color);
        line(x+51,y+4,x+57,y+4,color);line(x+51,y+7,x+57,y+7,color);
        frect(x+61,y+4,2,3,color);
        dtext(x+77,y+9,color,"AMP");
        if(amp){line(x+100,y+5,x+102,y+8,A.accent);line(x+102,y+8,x+106,y+2,A.accent);}
        else fframe(x+100,y+2,7,7,0x657786);
    }
    int row=channel<4?y+13:y+7;
    for(int i=0;i<2;i++) {
        unsigned long color=rca?(i?0xe98779:0xd9e3ec):0x657786;
        XSetForeground(A.dpy,A.gc,color);
        XDrawArc(A.dpy,A.buf,A.gc,ui_px(x+49+i*11),ui_px(row),ui_px(8),ui_px(8),0,360*64);
        frect(x+52+i*11,row+3,2,2,color);
    }
    dtext(x+77,row+8,rca?A.text:0x657786,"RCA");
    if(rca){line(x+100,row+3,x+102,row+6,A.accent);line(x+102,row+6,x+106,row,A.accent);}
    else fframe(x+100,row,7,7,0x657786);
    ui_font(A.font_struct);
}

static void ui_speaker(int x, int y, int known)
{
    ui_box(x, y, 34, 20, "", 2);
    unsigned long c = known ? 0xbec1b7 : 0x70736b;
    frect(x+8, y+7, 4, 7, c);
    line(x+12, y+7, x+17, y+3, c);
    line(x+17, y+3, x+17, y+17, c);
    line(x+17, y+17, x+12, y+13, c);
    line(x+21, y+6, x+23, y+9, c);
    line(x+23, y+9, x+21, y+13, c);
    if (!known) dtext(x+25, y+15, 0x85897c, "?");
}

/* Vendor APK g/a.c,d + resources arrays filter_type and filter_rate.
   Unknown codes remain unknown; rate index 8 is OFF, not 54 dB/oct. */
typedef struct { int family, slope, known; } console_filter;
static console_filter decode_filter(uint16_t code, int highpass)
{
    static const unsigned char hp[] = {40,8,14,20,46,52,58,26,64,42,10,16,22,48,54,60,28,66,44,12,18,24,50,56,62,30,68};
    static const unsigned char lp[] = {41,9,15,21,47,53,59,27,65,43,11,17,23,49,55,61,29,67,45,13,19,25,51,57,63,31,69};
    for (int i = 0; i < 27; i++) {
        if (code == (highpass ? hp[i] : lp[i])) {
            console_filter f = {i/9, i%9 == 8 ? 0 : (i%9+1)*6, 1};
            return f;
        }
    }
    console_filter f = {-1, 0, 0};
    return f;
}

static zp_response_config console_response_config(int channel)
{
    zp_response_config cfg={0};
    int base=136*channel;
    console_filter hp=decode_filter(A.dsp_values[base+138],1);
    console_filter lp=decode_filter(A.dsp_values[base+142],0);
    cfg.sample_rate=ZP_RESPONSE_FS;
    cfg.hp=(zp_response_filter){hp.family,hp.slope,hp.known,dsp_frequency(A.dsp_values[base+139])};
    cfg.lp=(zp_response_filter){lp.family,lp.slope,lp.known,dsp_frequency(A.dsp_values[base+143])};
    /* Vendor BTService.s uses address - 1; EQ enable is vendor address 66+ch. */
    cfg.eq_enabled=A.dsp_values[65+channel]!=0;
    for(int band=0;band<31;band++) {
        int id=base+147+4*band;
        cfg.eq[band]=(zp_response_eq){A.dsp_values[id-1],1,dsp_frequency(A.dsp_values[id]),
            ((double)A.dsp_values[id+1]-500)/10,A.dsp_values[id+2]*19.0/600};
        /* Vendor MainActivity sets Q display scale=19/6. chart/c.java divides
           displayed Q by that scale for PEQ alpha; shelves use displayed Q. */
        if(cfg.eq[band].type==7)cfg.eq[band].q=A.dsp_values[id+2]/100.0;
    }
    return cfg;
}

static void console_response_log(void)
{
    if(!A.dsp_valid){snprintf(A.dsp_status,sizeof(A.dsp_status),"Baca DSP sebelum ekspor response.");return;}
    char path[]="/tmp/zp84-response-XXXXXX";
    int fd=mkstemp(path);
    if(fd<0){snprintf(A.dsp_status,sizeof(A.dsp_status),"Gagal membuat log response: %s",strerror(errno));return;}
    FILE *fp=fdopen(fd,"w");
    if(!fp){close(fd);unlink(path);snprintf(A.dsp_status,sizeof(A.dsp_status),"Gagal membuka log response.");return;}
    zp_response_config cfg=console_response_config(A.cur_ch);
    fprintf(fp,"CH%d parameter model; Fs=%.0f; not measured hardware coefficients\n",A.cur_ch+1,cfg.sample_rate);
    for(int mode=0;mode<ZP_RESPONSE_MODES;mode++) {
        zp_cascade cascade;
        fprintf(fp,"\nMODE %s\n",zp_response_mode_name(mode));
        if(zp_response_build(&cfg,mode,&cascade,fp)!=ZP_RESPONSE_OK)continue;
        fprintf(fp,"frequency_hz,response_db\n");
        for(int i=0;i<ZP_RESPONSE_POINTS;i++) {
            double f=zp_response_frequency(i,ZP_RESPONSE_POINTS,cfg.sample_rate);
            fprintf(fp,"%.12g,%.12g\n",f,zp_cascade_response_db(&cascade,cfg.sample_rate,f));
        }
    }
    int failed=ferror(fp);
    if(fclose(fp)!=0)failed=1;
    if(failed){unlink(path);snprintf(A.dsp_status,sizeof(A.dsp_status),"Gagal menulis log response.");}
    else snprintf(A.dsp_status,sizeof(A.dsp_status),"Log response: %s",path);
}

static const unsigned long response_colors[8]={0xffb454,0xa997ff,0x9aa7bb,0xff879a,0x92c96a,0x7bbcff,0xde9fdb,0xe6df89};

#include "protection.h"
#include "speaker_names.h"
#include "controls.h"
#include "layout.h"
#include "presets.h"
#include "car_asset.h"
#include "logo_asset.h"

/* Cache the scaled sprite in the display's native pixel format. The asset is
   embedded so installed binaries work regardless of the working directory. */
static unsigned long car_component(unsigned int value, unsigned long mask)
{
    if (!mask) return 0;
    unsigned int shift = 0;
    while (!(mask & 1)) { mask >>= 1; shift++; }
    return ((value * mask + 127) / 255) << shift;
}

static void draw_logo_background(int x, int y)
{
    if (!A.logo_image_attempted) {
        A.logo_image_attempted = 1;
        int width = ui_px(210), height = ui_px(172);
        XImage *im = XCreateImage(A.dpy, DefaultVisual(A.dpy, DefaultScreen(A.dpy)),
                                 (unsigned)A.depth, ZPixmap, 0, NULL,
                                 (unsigned)width, (unsigned)height, 32, 0);
        if (!im) { logline("Cannot create logo background"); return; }
        im->data = calloc((size_t)im->bytes_per_line, (size_t)height);
        if (!im->data) {
            XDestroyImage(im);
            logline("Cannot allocate logo background");
            return;
        }
        unsigned long palette[256];
        for (int i = 0; i < 256; i++) {
            unsigned long rgba = logo_palette[i];
            unsigned int alpha = ((rgba >> 24) & 255) * 45 / 100;
            unsigned int rgb[3];
            for (int c = 0; c < 3; c++) {
                int shift = 16 - c * 8;
                rgb[c] = (((rgba >> shift) & 255) * alpha +
                          ((A.panel >> shift) & 255) * (255 - alpha) + 127) / 255;
            }
            palette[i] = car_component(rgb[0], im->red_mask) |
                         car_component(rgb[1], im->green_mask) |
                         car_component(rgb[2], im->blue_mask);
        }
        for (int yy = 0; yy < height; yy++)
            for (int xx = 0; xx < width; xx++)
                XPutPixel(im, xx, yy, palette[logo_pixels[
                    (yy * LOGO_HEIGHT / height) * LOGO_WIDTH + xx * LOGO_WIDTH / width]]);
        A.logo_image = im;
    }
    if (A.logo_image)
        XPutImage(A.dpy, A.buf, A.gc, A.logo_image, 0, 0, ui_px(x), ui_px(y),
                  (unsigned)A.logo_image->width, (unsigned)A.logo_image->height);
}

static void prepare_car_image(void)
{
    if (A.car_image_attempted) return;
    A.car_image_attempted = 1;
    int width = ui_px(100), height = ui_px(200);
    XImage *im = XCreateImage(A.dpy, DefaultVisual(A.dpy, DefaultScreen(A.dpy)),
                             (unsigned)A.depth, ZPixmap, 0, NULL,
                             (unsigned)width, (unsigned)height, 32, 0);
    if (!im) { logline("Cannot create car preview image"); return; }
    im->data = calloc((size_t)im->bytes_per_line, (size_t)height);
    if (!im->data) {
        XDestroyImage(im);
        logline("Cannot allocate car preview pixels");
        return;
    }
    unsigned long palette[256];
    for (int i = 0; i < 256; i++) {
        unsigned long rgb = car_palette[i];
        palette[i] = car_component((rgb >> 16) & 255, im->red_mask) |
                     car_component((rgb >> 8) & 255, im->green_mask) |
                     car_component(rgb & 255, im->blue_mask);
    }
    for (int y = 0; y < height; y++) {
        int row = y * CAR_HEIGHT / height;
        for (int x = 0; x < width; x++) {
            int col = x * CAR_WIDTH / width;
            XPutPixel(im, x, y, palette[car_pixels[row * CAR_WIDTH + col]]);
        }
    }
    A.car_image = im;
}

static void console_car(void)
{
    layout_init();
    ui_font(A.font_small);
    ui_box(10,313,60,23,"Mobil",layout.mode==0);
    ui_box(74,313,60,23,"Ruangan",layout.mode==1);
    ui_box(138,313,46,23,"Reset",0);
    ui_font(A.font_struct);
    if(layout.mode==0) {
        prepare_car_image();
        if (A.car_image)
            XPutImage(A.dpy, A.buf, A.gc, A.car_image, 0, 0, ui_px(47), ui_px(338),
                  (unsigned)A.car_image->width, (unsigned)A.car_image->height);
        else ctext(42,430,110,A.dim,"Car preview");
    } else {
        /* Top-down listening room, with front screen and listening seat. */
        frect(18,344,158,190,0x15202b);
        fframe(18,344,158,190,0x66798a);
        fframe(22,348,150,182,0x344658);
        frect(59,349,76,5,A.accent);
        for(int yy=375;yy<520;yy+=20)line(24,yy,170,yy,0x203040);
        frect(53,398,88,82,0x263848);
        frect(65,446,65,28,0x526577);
        fframe(65,446,65,28,0x7890a3);
        line(97,448,97,470,0x344658);
        frect(61,443,7,34,0x344658);frect(127,443,7,34,0x344658);
        ui_font(A.font_small);
        ctext(30,369,135,A.dim,"DEPAN");
        ctext(40,494,115,A.dim,"Posisi dengar");
        ui_font(A.font_struct);
    }
    int lx=layout.x[layout.mode][8],ly=layout.y[layout.mode][8];
    if(layout_channel_visible(A.cur_ch))line(lx,ly,layout.x[layout.mode][A.cur_ch],layout.y[layout.mode][A.cur_ch],0x698599);
    ui_dot(lx,ly,8,0xd9e6f0);ctext(lx-9,ly+4,18,0x10232c,"P");
    for (int c = 0; c < 8; c++) {
        if (!layout_channel_visible(c)) continue;
        int x = layout.x[layout.mode][c], y = layout.y[layout.mode][c];
        ui_dot(x, y, 9, c == A.cur_ch ? 0xffbb18 : 0x1c9d9d);
        char n[4]; snprintf(n,sizeof(n),"%d",c+1);
        ctext(x-10,y+4,20,0x151916,n);
    }
}

static void console_export(void)
{
    if (!A.dsp_valid) {
        snprintf(A.console_notice,sizeof(A.console_notice),"Belum ada data. Klik Baca DSP terlebih dahulu.");
        return;
    }
    char path[] = "zevox-snapshot-XXXXXX";
    int fd = mkstemp(path);
    FILE *f = fd >= 0 ? fdopen(fd,"w") : NULL;
    if (!f) {
        if (fd >= 0) { close(fd); unlink(path); }
        snprintf(A.console_notice,sizeof(A.console_notice),"Snapshot gagal disimpan: %s",strerror(errno));
        return;
    }
    fprintf(f,"# Snapshot USB %s\n# id value\n",A.dsp_time);
    for (int i=0;i<ZP_PARAM_COUNT;i++) fprintf(f,"%04x %04x\n",i,A.dsp_values[i]);
    int failed=ferror(f);
    if (fclose(f)) failed=1;
    if (failed) { unlink(path); snprintf(A.console_notice,sizeof(A.console_notice),"Gagal menulis snapshot."); }
    else snprintf(A.console_notice,sizeof(A.console_notice),"Tersimpan: %s (register mentah, bukan preset restore)",path);
}

static void draw_mixer(void)
{
    int top=mixer_top(),rows=mixer_rows(),height=132+65*rows;
    int source=A.dsp_values[1554],known=A.dsp_valid&&(source==1||source==2||source==3);
    const unsigned long panel=A.panel,border=0x46586b,ink=A.text;
    frect(88,top-4,1048,height+8,0x101720);
    frect(92,top,1040,height,panel);fframe(92,top,1040,height,border);
    frect(93,top+1,1038,26,0x293847);
    ui_font(A.font_struct);dtext(101,top+19,ink,"Mixer");
    if(output_locked)dtext(1008,top+19,A.warn,"Output terkunci");
    line(1105,top+7,1117,top+19,A.dim);line(1117,top+7,1105,top+19,A.dim);
    if(!known) {
        dtext(120,top+65,ink,"Pilih AUX, Bluetooth atau High level setelah Baca DSP.");
    } else {
        for(int i=0;i<rows;i++) {
            int y=mixer_cell_y(i);char label[32];
            if(source==3)snprintf(label,sizeof(label),"Hi Level%d",i+1);
            else snprintf(label,sizeof(label),"%s-%c",source==1?"AUX":"BT",i?'R':'L');
            dtext(102,y+28,ink,label);
            line(179,y+24,198,y+24,ink);line(193,y+20,198,y+24,ink);line(193,y+28,198,y+24,ink);
            for(int c=0;c<8;c++) {
                int x=mixer_cell_x(c),id=mixer_id(source,c,i);
                unsigned value=A.dsp_values[id],gain=value/256;
                if(fader_kind==5&&fader_index==id)gain=mixer_drag_gain();
                frect(x,y,100,56,0x192633);
                fframe(x,y,100,56,border);
                unsigned long power=(value&1)?A.accent:0x657786;
                XSetForeground(A.dpy,A.gc,power);
                XSetLineAttributes(A.dpy,A.gc,(unsigned)(ui_px(2)>0?ui_px(2):1),LineSolid,CapRound,JoinRound);
                XDrawArc(A.dpy,A.buf,A.gc,ui_px(x+16),ui_px(y+8),ui_px(12),ui_px(12),130*64,280*64);
                line(x+22,y+5,x+22,y+13,power);
                XSetLineAttributes(A.dpy,A.gc,0,LineSolid,CapButt,JoinMiter);
                char text[16];snprintf(text,sizeof(text),"%u",gain);
                ui_font(A.font_small);ctext(x+50,y+17,43,ink,text);ui_font(A.font_struct);
                line(x+10,y+39,x+90,y+39,0x52687b);line(x+10,y+40,x+90,y+40,0x101720);
                int thumb=x+10+(int)lround(80*zp_clampd(gain/100.0,0,1));
                if(value&1)line(x+10,y+39,thumb,y+39,A.accent);
                for(int k=-6;k<=6;k++) {
                    int shade=235-abs(k)*12;
                    frect(thumb+k,y+33,1,14,(unsigned long)shade*0x010101);
                }
                line(thumb-2,y+34,thumb-2,y+45,0xf3f3f3);
                line(thumb+2,y+34,thumb+2,y+45,0xf3f3f3);
                fframe(thumb-6,y+33,13,14,0x858585);
            }
        }
    }
    int bottom=top+40+rows*65;
    for(int c=0;c<8;c++) {
        int x=mixer_cell_x(c)+50;char label[16];
        line(x,bottom,x,bottom+15,ink);line(x-4,bottom+10,x,bottom+15,ink);line(x+4,bottom+10,x,bottom+15,ink);
        snprintf(label,sizeof(label),"OutCh%d",c+1);ui_font(A.font_heading);ctext(x-50,bottom+38,100,ink,label);ui_font(A.font_struct);
    }
    ui_font(A.font_small);
    ui_text_fit(108,top+height-12,1005,A.dim,A.console_notice[0]?A.console_notice:output_locked?"Mixer terkunci. Tutup panel lalu klik Unlock output untuk mengubah routing atau level.":"Klik power: routing | Geser slider: gain | Klik angka: edit | Esc: batal / tutup");
    ui_font(A.font_struct);
}
static void mixer_click(int x,int y)
{
    int top=mixer_top();
    if(hit(1094,top,37,27,x,y)||!hit(92,top,1040,132+65*mixer_rows(),x,y)) {
        fader_kind=0;A.console_modal=0;return;
    }
    int source=A.dsp_values[1554];
    if(!A.dsp_valid||(source!=1&&source!=2&&source!=3))return;
    if(output_locked) {
        fader_kind=0;
        snprintf(A.console_notice,sizeof(A.console_notice),"Output terkunci. Tutup panel lalu klik Unlock output untuk mengubah mixer.");
        return;
    }
    for(int c=0;c<8;c++)for(int i=0;i<mixer_rows();i++) {
        int xx=mixer_cell_x(c),yy=mixer_cell_y(i),id=mixer_id(source,c,i);
        if(hit(xx+6,yy+2,34,25,x,y)){control_write(id,A.dsp_values[id]^1);return;}
        if(hit(xx+50,yy+2,45,25,x,y)){control_edit(id,6,"Mixer level %",A.dsp_values[id]/256,0,100);return;}
        if(hit(xx+3,yy+29,94,23,x,y)&&control_ready()) {
            fader_kind=5;fader_index=id;fader_channel=c;fader_x=x;return;
        }
    }
}

static void draw_memory(void)
{
    frect(236,176,768,448,0x101720);frect(240,180,760,440,0x1d2936);
    fframe(240,180,760,440,0x52687b);
    ui_font(A.font_heading);dtext(266,214,A.text,"Memory");ui_font(A.font_struct);
    ui_box(944,192,34,26,"X",0);
    dtext(266,238,A.dim,"Preset lokal komputer | beri nama, simpan, lalu Load ke DSP");
    ui_font(A.font_small);
    dtext(277,265,A.dim,"SLOT");dtext(327,265,A.dim,"NAMA PRESET");
    dtext(685,265,A.dim,"SIMPAN DATA DSP");dtext(806,265,A.dim,"MUAT PRESET");
    ui_font(A.font_struct);
    int ready=A.connected&&A.dsp_valid&&!A.dsp_reading;
    for(int i=0;i<8;i++) {
        int y=278+i*31,exists=preset_exists(i);char name[40],number[8];
        preset_name(i,name,sizeof(name));snprintf(number,sizeof(number),"%02d",i+1);
        frect(266,y,708,29,i%2?0x233241:0x192633);
        dtext(278,y+20,A.dim,number);
        dtext(327,y+20,exists?A.text:A.dim,name);
        ui_box(680,y+2,105,25,exists?"Simpan ulang":"Simpan",ready?0:2);
        ui_box(802,y+2,150,25,"Load",exists&&ready&&!output_locked?1:2);
    }
    ui_box(266,548,180,30,"Ekspor snapshot",A.dsp_valid?0:2);
    ui_font(A.font_small);
    dtext(465,566,A.dim,"CH1-4: AMP + RCA | CH5-8: RCA");
    ui_text_fit(266,602,708,A.text,A.console_notice[0]?A.console_notice:"Preset tersimpan tetap tersedia setelah aplikasi ditutup.");
    ui_font(A.font_struct);
}
static void memory_click(int x,int y)
{
    if(hit(944,192,34,26,x,y)||!hit(240,180,760,440,x,y)){A.console_modal=0;return;}
    if(hit(266,548,180,30,x,y)){console_export();return;}
    for(int i=0;i<8;i++) {
        int yy=280+i*31;
        if(hit(680,yy,105,25,x,y)){preset_name_edit(i);return;}
        if(hit(802,yy,150,25,x,y)){preset_restore(i);return;}
    }
}

/* First-run root password prompt. The password is written to sudo's stdin
   only; it is never embedded in a command line or stored. A correct password
   installs the udev rule so later runs can open /dev/hidraw* unprivileged. */
#define ZP_UDEV_RULE \
    "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"4084\", ATTRS{idProduct}==\"4357\", MODE=\"0660\", GROUP=\"plugdev\", TAG+=\"uaccess\", SYMLINK+=\"zp84amp\"\n" \
    "\n" \
    "KERNEL==\"hidraw*\", ATTRS{idVendor}==\"4084\", ATTRS{idProduct}==\"4357\", ACTION==\"add\", RUN+=\"/bin/chmod 0660 /dev/%k\"\n"

static char password_text[64];

static void password_submit(void)
{
    char tmpl[] = "/tmp/zp84-udev-XXXXXX";
    char cmd[512];
    int fd = mkstemp(tmpl);
    FILE *out;
    FILE *p;
    if (!password_text[0]) {
        snprintf(A.console_notice,sizeof(A.console_notice),"Masukkan password root terlebih dahulu.");
        return;
    }
    if (fd < 0) {
        snprintf(A.console_notice,sizeof(A.console_notice),"Gagal membuat file sementara.");
        return;
    }
    out = fdopen(fd, "w");
    if (!out) {
        close(fd);
        unlink(tmpl);
        snprintf(A.console_notice,sizeof(A.console_notice),"Gagal membuat file sementara.");
        return;
    }
    fputs(ZP_UDEV_RULE, out);
    if (fclose(out)) {
        unlink(tmpl);
        snprintf(A.console_notice,sizeof(A.console_notice),"Gagal menulis aturan udev.");
        return;
    }
    snprintf(cmd, sizeof(cmd),
        "sudo -S -k sh -c 'install -m 0644 %s /etc/udev/rules.d/99-zp84.rules"
        " && udevadm control --reload-rules"
        " && udevadm trigger --subsystem-match=hidraw'",
        tmpl);
    p = popen(cmd, "w");
    if (!p) {
        unlink(tmpl);
        snprintf(A.console_notice,sizeof(A.console_notice),"Gagal menjalankan sudo.");
        return;
    }
    fprintf(p, "%s\n", password_text);
    int ok = pclose(p) == 0;
    unlink(tmpl);
    password_text[0] = 0;
    if (ok) {
        first_run_mark();
        A.console_modal = 0;
        snprintf(A.console_notice,sizeof(A.console_notice),"Aturan udev terpasang. Klik Connect untuk mengakses amplifier.");
        logline("udev rule installed via sudo");
    } else {
        snprintf(A.console_notice,sizeof(A.console_notice),"Password root salah atau instalasi gagal. Coba lagi atau Lewati.");
        logline("sudo/udev install failed");
    }
}

static void password_key(XKeyEvent *event, KeySym ks)
{
    size_t n = strlen(password_text);
    if (ks == XK_Escape) {
        A.console_modal = 0;
        first_run_mark();
        snprintf(A.console_notice,sizeof(A.console_notice),"Dilewati. Jalankan sudo install-udev.sh untuk memasang aturan USB.");
        return;
    }
    if (ks == XK_Return || ks == XK_KP_Enter) {
        password_submit();
        return;
    }
    if (ks == XK_BackSpace) {
        if (n) password_text[n-1] = 0;
        return;
    }
    if ((event->state & ControlMask) && (ks == XK_a || ks == XK_A)) {
        password_text[0] = 0;
        return;
    }
    char chars[16]; KeySym key;
    int count = XLookupString(event, chars, sizeof(chars), &key, NULL);
    for (int i = 0; i < count && n + 1 < sizeof(password_text); i++)
        if (chars[i] >= 32 && chars[i] <= 126)
            password_text[n++] = chars[i];
    password_text[n] = 0;
}

static void password_click(int x, int y)
{
    if (hit(343, 452, 170, 32, x, y)) {
        password_submit();
        return;
    }
    if (hit(533, 452, 100, 32, x, y)) {
        first_run_mark();
        A.console_modal = 0;
        snprintf(A.console_notice,sizeof(A.console_notice),"Dilewati. Jalankan sudo install-udev.sh untuk memasang aturan USB.");
    }
}

static void draw_password(void)
{
    char masked[sizeof(password_text)];
    size_t n = strlen(password_text);
    for (size_t i = 0; i < n; i++) masked[i] = '*';
    masked[n] = 0;
    frect(313,235,614,320,0x181a17);
    frect(318,240,604,310,0x1d2936);
    fframe(318,240,604,310,0x52687b);
    ui_font(A.font_heading);
    dtext(343,276,A.text,"Password root");
    ui_font(A.font_struct);
    dtext(343,312,A.text,"Aplikasi memerlukan hak root untuk memasang");
    dtext(343,336,A.dim,"aturan udev agar amplifier USB dapat diakses");
    dtext(343,360,A.dim,"tanpa menjalankan aplikasi sebagai root.");
    dtext(343,392,A.dim,"Password root");
    frect(343,406,400,26,0x101a25);
    fframe(343,406,400,26,A.accent);
    dtext(351,424,A.text,masked);
    ui_box(343,452,170,32,"OK",1);
    ui_box(533,452,100,32,"Lewati",0);
    ui_font(A.font_small);
    ui_text_fit(343,507,555,A.text,A.console_notice);
    ui_font(A.font_struct);
}

static void draw_protection(void)
{
    char text[200];int c=A.cur_ch;speaker_guard *g=&guards[c];
    frect(265,190,710,430,A.panel);fframe(265,190,710,430,A.accent);
    ui_font(A.font_heading);snprintf(text,sizeof(text),"Proteksi speaker - CH%d",c+1);dtext(285,225,A.text,text);
    ui_font(A.font_struct);ui_box(920,200,36,26,"X",0);
    for(int i=0;i<8;i++){snprintf(text,sizeof(text),"CH%d",i+1);ui_box(285+i*82,242,73,28,text,i==c);}
    int compliant=!g->enabled||!A.dsp_valid||protection_channel_valid(c,A.dsp_values);
    snprintf(text,sizeof(text),"Status: %s",protection_file_invalid?"FILE RUSAK - penulisan DSP diblokir":!compliant?"DSP DI LUAR BATAS - mute dan periksa":g->enabled?(A.dsp_valid?"BATAS AKTIF":"BATAS TERSIMPAN - Baca DSP"):"BELUM AKTIF");dtext(285,298,A.accent,text);
    dtext(285,326,A.text,"Atur crossover dan level sesuai spesifikasi, lalu kunci batas saat ini.");
    dtext(285,351,A.dim,"Tweeter: HPF wajib. Subwoofer ported: HPF/subsonic dan LPF wajib.");
    dtext(285,376,A.dim,"Batas mencakup slope, cutoff, volume master/output dan gain tiap EQ.");
    if(g->enabled) {
        snprintf(text,sizeof(text),"%s | HPF >= %g Hz / %d dB/oct | LPF %s",g->role==1?"Tweeter":"Subwoofer",dsp_frequency(g->hp_freq),decode_filter(g->hp_type,1).slope,g->role==2?"dibatasi":"tidak dikunci");
        dtext(285,405,A.text,text);
        if(g->role==2){snprintf(text,sizeof(text),"LPF <= %g Hz / >= %d dB/oct",dsp_frequency(g->lp_freq),decode_filter(g->lp_type,0).slope);dtext(285,428,A.text,text);}
    } else dtext(285,405,A.dim,"Tidak ada angka universal yang dijamin aman. Model speaker diperlukan.");
    ui_box(285,443,660,29,protection_confirm?"[X] Saya sudah memeriksa batas terhadap spesifikasi speaker":"[ ] Saya sudah memeriksa batas terhadap spesifikasi speaker",protection_confirm);
    if(!g->enabled){ui_box(285,488,205,32,"Kunci sebagai tweeter",0);ui_box(505,488,220,32,"Kunci sebagai subwoofer",0);}
    else ui_box(285,488,300,32,"Lepas batas (perlu konfirmasi)",0);
    ui_font(A.font_small);
    dtext(285,548,A.dim,"Proteksi aplikasi, bukan limiter daya DSP. Tidak mendeteksi clipping / suhu / excursion.");
    dtext(285,568,A.dim,"Setelan perangkat lain tetap dapat berubah. Periksa status setelah Baca DSP.");
    ui_text_fit(285,595,660,A.text,A.console_notice);ui_font(A.font_struct);
}
static void protection_click(int x,int y)
{
    if(hit(920,200,36,26,x,y)){A.console_modal=0;return;}
    for(int c=0;c<8;c++)if(hit(285+c*82,242,73,28,x,y)){A.cur_ch=c;protection_confirm=0;return;}
    if(hit(285,443,660,29,x,y)){protection_confirm=!protection_confirm;return;}
    if(!guards[A.cur_ch].enabled){
        int role=hit(285,488,205,32,x,y)?1:hit(505,488,220,32,x,y)?2:0;
        if(role&& !protection_file_invalid && protection_capture(A.cur_ch,role))snprintf(A.console_notice,sizeof(A.console_notice),"Batas CH%d tersimpan. Tidak ada parameter DSP diubah.",A.cur_ch+1);
    } else if(hit(285,488,300,32,x,y)) {
        if(!protection_confirm){protection_error(A.cur_ch,"centang konfirmasi untuk melepas batas.");return;}
        speaker_guard old=guards[A.cur_ch];guards[A.cur_ch].enabled=0;
        if(!protection_save()){guards[A.cur_ch]=old;protection_error(A.cur_ch,"gagal menyimpan; batas tetap aktif.");}
        else snprintf(A.console_notice,sizeof(A.console_notice),"Batas CH%d dilepas.",A.cur_ch+1);
        protection_confirm=0;
    }
}

static void draw_console_modal(void)
{
    if (!A.console_modal) return;
    if(A.console_modal==12){draw_protection();return;}
    if(A.console_modal==4){draw_mixer();return;}
    if(A.console_modal==1){draw_memory();return;}
    if(A.console_modal==11){draw_password();return;}
    frect(313,235,614,320,0x181a17);
    frect(318,240,604,310,0x1d2936);
    fframe(318,240,604,310,0x52687b);
    static const char *titles[]={"","Memory","Option","Info","Mixer","Kontrol DSP","Edit parameter DSP","Pilih input","Jarak / Delay","Input volume","Noise Gate"};
    ui_font(A.font_heading);
    dtext(343,276,A.text,titles[A.console_modal]);
    ui_font(A.font_struct);
    ui_box(870,250,38,25,"X",0);
    if (A.console_modal==6) {
        dtext(343,315,A.text,edit_label);
        ui_value(343,338,400,edit_text);
        fframe(343,338,400,21,A.accent);
        if(edit_selected)line(350,357,735,357,A.accent);
        dtext(343,385,A.dim,"Ketik nilai baru | Ctrl+A: pilih semua | Enter: terapkan");
        ui_box(343,420,170,32,edit_kind==11?"Simpan nama":edit_kind==10?"Simpan preset":edit_kind==7?"Simpan skala":"Terapkan ke DSP",1);
        ui_box(533,420,100,32,"Batal",0);
    } else if (A.console_modal==8) {
        layout_init();
        int m=layout.mode;char text[120];
        snprintf(text,sizeof(text),"Lebar denah: %.2f m %s",layout.width_m[m],layout.calibrated[m]?"":"(perkiraan)");
        dtext(343,308,A.text,text);
        ui_box(690,286,150,28,"Atur lebar (m)",0);
        ui_font(A.font_small);
        dtext(343,332,A.dim,"CHANNEL");dtext(465,332,A.dim,"JARAK 2D");
        dtext(585,332,A.dim,"PREVIEW ms");dtext(720,332,A.dim,"DSP ms");
        for(int c=0;c<8;c++) {
            int y=350+c*17;
            snprintf(text,sizeof(text),"CH%d%s",c+1,layout_channel_visible(c)?"":" (mute)");dtext(343,y,A.text,text);
            snprintf(text,sizeof(text),"%.2f m",layout_distance(c));dtext(465,y,A.text,text);
            if(layout_channel_visible(c))snprintf(text,sizeof(text),"%.3f%s",layout_delay(c),layout_delay(c)>20?" >20!":"");else strcpy(text,"--");
            dtext(585,y,layout_delay(c)>20?A.warn:A.accent,text);
            if(A.dsp_valid)snprintf(text,sizeof(text),"%.3f",dsp_delay_ms(A.dsp_values[73+c]));else strcpy(text,"--");
            dtext(720,y,A.dim,text);
        }
        ui_font(A.font_struct);
        ui_box(343,480,160,27,"Terapkan Delay",1);
        ui_font(A.font_small);dtext(523,498,A.dim,"2D / 346 m/s / acuan: speaker terjauh");ui_font(A.font_struct);
    } else if (A.console_modal==2) {
        dtext(343,315,A.text,"Pilih tampilan dan periksa data perangkat.");
        ui_box(343,350,175,32,"Register USB",0);
        ui_box(538,350,175,32,"Simulasi lokal",0);
        ui_box(343,395,175,32,"Input volume",0);
        ui_box(538,395,175,32,"Noise Gate",0);
        dtext(343,458,A.dim,"R: baca ulang | 1-8: channel | Esc: tutup panel");
    } else if (A.console_modal==3) {
        dtext(343,315,A.text,"ZP 8.4 AMP - kontrol DSP melalui USB.");
        dtext(343,345,A.dim,"Input, mixer, EQ, crossover, level, mute, phase, delay.");
    } else if (A.console_modal==7) {
        dtext(343,310,A.dim,"Pilih sumber audio amplifier");
        static const int sources[]={1,2,3,7};
        for(int i=0;i<4;i++)ui_box(343+(i%2)*260,335+(i/2)*50,240,36,source_name(sources[i]),A.dsp_valid&&A.dsp_values[1554]==sources[i]?1:0);
    } else if (A.console_modal==9) {
        dtext(343,310,A.dim,"Level sumber 0-100 | klik angka untuk mengubah");
        for(int i=0;i<2;i++) {
            int id=i?1900:1908,yy=345+i*65;char text[32];
            dtext(343,yy+20,A.text,i?"BT_VOL":"USB_VOL");
            if(A.dsp_valid)snprintf(text,sizeof(text),"%d",A.dsp_values[id]>=500?A.dsp_values[id]-500:A.dsp_values[id]);
            else strcpy(text,"--");
            ui_box(515,yy,65,30,text,A.dsp_valid?0:2);
            line(610,yy+15,840,yy+15,A.dim);
            if(A.dsp_valid) {
                double v=A.dsp_values[id]>=500?A.dsp_values[id]-500:A.dsp_values[id];
                if(fader_kind==4&&fader_index==id)v=100*zp_clampd((fader_x-610)/230.0,0,1);
                int px=610+(int)lround(230*zp_clampd(v/100,0,1));
                frect(px-4,yy+3,9,24,A.accent);
            }
        }
    } else if (A.console_modal==10) {
        char text[40];int index=noise_index(A.dsp_values[1565]);
        dtext(343,320,A.dim,"Noise gate: tingkat vendor 0-20 (0 = OFF)");
        if(A.dsp_valid&&index>=0)snprintf(text,sizeof(text),"Tingkat %d",index);else strcpy(text,"--");
        ui_box(343,355,210,32,text,A.dsp_valid?0:2);
    } else {
        dtext(343,315,A.text,"Kontrol ini belum mengirim perubahan ke amplifier.");
        dtext(343,345,A.dim,"Panel saat ini menampilkan snapshot USB dalam mode baca saja.");
        dtext(343,375,A.dim,"Gunakan Option > Simulasi lokal untuk mencoba pengaturan lokal.");
    }
    ui_font(A.font_small);
    ui_text_fit(343,A.console_modal==8?535:507,555,A.text,A.console_notice);
    ui_font(A.font_struct);
}

static void draw_console(void)
{
    const int gx=232, gy=88, gw=931, gh=192;
    const unsigned long yellow=0xf4ba63, purple=0x59d8cd;
    char b[160];
    int base=136*A.cur_ch;
    frect(0,0,WIN_W,WIN_H,0x101720);
    ui_font(A.font_heading);
    dtext(10,22,A.text,"ZP 8.4 AMP");
    ui_font(A.font_struct);
    dtext(152,22,A.dim,"Memory"); dtext(231,22,A.dim,"Option"); dtext(302,22,A.dim,"Info");
    ui_font(A.font_small);
    dtext(450,21,0xb0b5a9,"DSP STUDIO  /  8 OUTPUT  /  31 BAND EQ");
    ui_font(A.font_struct);
    frect(0,31,WIN_W,36,A.panel);
    ui_box(964,4,130,23,A.maximized?"Pulihkan":"Maksimalkan",0);
    ui_box(1100,4,126,23,A.fullscreen?"Keluar penuh":"Layar penuh",0);
    dtext(14,54,A.dim,"Input"); ui_box(65,37,111,24,A.dsp_valid?source_name(A.dsp_values[1554]):"Pilih input",0);
    ui_box(183,38,52,22,"Mixer",0);
    ui_box(244,37,124,24,"Jarak / Delay",0);
    ui_box(377,37,103,24,"Input volume",0);
    ui_box(489,37,99,24,"Noise Gate",0);
    ui_box(602,37,114,24,"Proteksi speaker",0);
    ui_box(725,37,115,24,bluetooth_selected?"BLE Mango3.0":"Koneksi: USB",bluetooth_selected);
    ui_box(850,37,97,24,"Baca DSP",A.dsp_reading?1:0);
    ui_box(954,37,108,24,bluetooth_pending?"Batal koneksi":A.connected?"Disconnect":"Connect",0);
    frect(1116,39,110,22,A.connected?0x38b909:0x53564e);
    ctext(1116,55,110,A.connected?0xffffff:0xc4c7be,bluetooth_pending?"connecting...":A.connected?"connected":"disconnected");
    frect(4,73,186,230,A.panel);
    static const char *families[]={"Butter-W","Bessel","Link_R"};
    for(int f=0;f<2;f++) {
        int x=18+91*f;
        line(x+10,f?87:107,x+30,87,yellow); line(x+30,87,x+56,f?107:87,yellow);
        ctext(x,119,72,A.text,f?"LPF":"HPF");
        ctext(x,155,72,A.dim,"Freq:");
        if(A.dsp_valid) snprintf(b,sizeof(b),"%g",dsp_frequency(A.dsp_values[base+139+4*f])); else strcpy(b,"--");
        ui_value(x,169,72,b);
        ctext(x,211,72,A.dim,"Type:");
        console_filter filter=decode_filter(A.dsp_values[base+138+4*f],!f);
        ui_value(x,220,72,A.dsp_valid&&filter.known?families[filter.family]:"--");
        ctext(x,260,72,A.dim,"Oct:");
        if(A.dsp_valid&&filter.known) { if(filter.slope) snprintf(b,sizeof(b),"%ddB/Oct",filter.slope); else strcpy(b,"OFF"); } else strcpy(b,"--");
        ui_value(x,270,72,b);
    }
    frect(196,73,1038,230,A.panel);
    draw_logo_background(gx + (gw - 210) / 2, gy + (gh - 172) / 2);
    for(int i=0;i<=30;i++) {
        int x=gx+i*gw/30;
        line(x,gy,x,gy+gh,0x2d3b4a);
    }
    for(int d=-20;d<=20;d+=5) {
        int y=gy+(20-d)*gh/40;
        line(gx,y,gx+gw,y,d?0x2d3b4a:0x536579);
        if(d%10==0) { snprintf(b,sizeof(b),"%+d",d); dtext(203,y+4,A.dim,b); }
    }
    const double ticks[]={20,50,100,200,500,1000,2000,5000,10000,20000};
    for(int i=0;i<10;i++) {
        int x=gx+(int)(log(ticks[i]/20)/log(1000)*gw);
        if(ticks[i]>=1000)snprintf(b,sizeof(b),"%gK",ticks[i]/1000);else snprintf(b,sizeof(b),"%g",ticks[i]);
        ctext(x-18,296,36,A.dim,b);
    }
    if(A.dsp_valid) {
        for(int pass=0;pass<9;pass++) {
            int c=pass==8?A.cur_ch:pass;
            if(pass<8&&(c==A.cur_ch||A.response_mode!=ZP_RESPONSE_FULL||!(A.overlay_mask&(1u<<c))))continue;
            zp_response_config cfg=console_response_config(c);
            zp_cascade cascade;
            if(zp_response_build(&cfg,A.response_mode,&cascade,NULL)!=ZP_RESPONSE_OK)continue;
            double prev_db=0; int prev_x=0;
            for(int i=0;i<ZP_RESPONSE_POINTS;i++) {
                double freq=zp_response_frequency(i,ZP_RESPONSE_POINTS,cfg.sample_rate);
                double db=zp_cascade_response_db(&cascade,cfg.sample_rate,freq);
                int x=gx+(int)lround(log(freq/20)/log(1000)*gw);
                if(i&&isfinite(db)&&isfinite(prev_db)&&!(db>20&&prev_db>20)&&!(db< -20&&prev_db< -20)) {
                    int y=gy+(int)lround((20-zp_clampd(db,-20,20))*gh/40);
                    int py=gy+(int)lround((20-zp_clampd(prev_db,-20,20))*gh/40);
                    line(prev_x,py,x,y,c==A.cur_ch?purple:response_colors[c]);
                }
                prev_db=db;prev_x=x;
            }
        }
        int id=base+147+4*A.console_band;
        snprintf(b,sizeof(b),"F:%g Hz   Q:%.2f   G:%.1f dB",dsp_frequency(A.dsp_values[id]),A.dsp_values[id+2]*(19.0/6)/100,((double)A.dsp_values[id+1]-500)/10);
        dtext(gx+580,228,A.text,b);
        for(int f=0;f<2;f++) {
            double hz=dsp_frequency(A.dsp_values[base+139+4*f]);
            if(hz>=20&&hz<=20000) {int x=gx+(int)(log(hz/20)/log(1000)*gw);dtext(x,250,yellow,f?"L":"H");}
        }
    }
    ui_font(A.font_small);
    zp_response_config cfg=console_response_config(A.cur_ch);
    zp_cascade check_response;
    int response_status=zp_response_build(&cfg,A.response_mode,&check_response,NULL);
    snprintf(b,sizeof(b),"CH%d %s | %s | F2 mode / F3 log | Model 48 kHz",A.cur_ch+1,zp_response_mode_name(A.response_mode),response_status==ZP_RESPONSE_OK?"": "filter tidak didukung / data invalid");
    ctext(gx,276,gw,A.dim,A.dsp_valid?b:"Belum ada data USB - klik Baca DSP");
    ui_font(A.font_struct);
    for(int c=0;c<8;c++) {
        int y=82+c*27;
        frect(1180,y,44,20,A.panel);
        unsigned long color=c==A.cur_ch?purple:A.overlay_mask&(1u<<c)?response_colors[c]:A.dim;
        fframe(1180,y,44,20,color);snprintf(b,sizeof(b),"CH%d",c+1);ctext(1180,y+15,44,color,b);
    }
    frect(4,309,186,233,A.panel);console_car();
    frect(196,309,1038,233,A.panel);
    dtext(202,328,A.dim,"ID"); dtext(199,353,A.dim,"Freq");dtext(206,378,A.dim,"Q");dtext(198,403,A.dim,"Gain");
    for(int band=0;band<31;band++) {
        int x=232+band*32,id=base+147+band*4;
        ui_font(A.font_struct);snprintf(b,sizeof(b),"%d",band+1);ctext(x,328,30,band==A.console_band?yellow:A.dim,b);
        ui_font(A.font_small);
        for(int r=0;r<3;r++) {
            if(!A.dsp_valid)strcpy(b,"--");
            else if(r==0)snprintf(b,sizeof(b),"%.0f",dsp_frequency(A.dsp_values[id]));
            else if(r==1)snprintf(b,sizeof(b),"%.2f",A.dsp_values[id+2]*(19.0/6)/100);
            else snprintf(b,sizeof(b),"%.1f",((double)A.dsp_values[id+1]-500)/10);
            frect(x,337+r*25,30,20,0x121e2b);fframe(x,337+r*25,30,20,0x34485b);
            ctext(x,352+r*25,30,A.text,b);
        }
        double gain=fader_preview(1,band,((double)A.dsp_values[id+1]-500)/10+12,428,69,24)-12;
        ui_fader(x+15,428,69,(gain+12)/24,A.dsp_valid);
        ui_dot(x+15,525,4,band==A.console_band?yellow:0x242620);
    }
    ui_font(A.font_struct);
    frect(4,548,96,216,A.panel);frect(102,548,88,216,A.panel);
    ctext(4,573,96,A.text,"Volume");
    if(A.dsp_valid)snprintf(b,sizeof(b),"%.0f dB*",fader_preview(3,0,dsp_channel_level(A.dsp_values[12]),628,99,60));else strcpy(b,"--");
    ui_value(21,588,61,b);
    ui_fader(55,628,99,fader_preview(3,0,dsp_channel_level(A.dsp_values[12]),628,99,60)/60,A.dsp_valid);
    ui_font(A.font_small);
    for(int n=0;n<=60;n+=10){snprintf(b,sizeof(b),"%d",n);dtext(26,731-n*99/60,A.dim,b);line(42,727-n*99/60,49,727-n*99/60,A.dim);}
    ctext(5,620,92,A.dim,"Master");ui_font(A.font_struct);ui_speaker(37,739,A.dsp_valid);
    if(A.dsp_valid){int all=1;for(int c=0;c<8;c++)if(!A.dsp_values[2+c])all=0;if(all)line(42,742,66,756,A.accent);}
    ctext(102,573,88,A.text,"Delay unit");
    static const char *units[]={"Ms","Cm","Inch"};
    for(int i=0;i<3;i++){ui_dot(119,620+57*i,5,A.delay_unit==i?0xe45624:0x898d81);dtext(132,625+57*i,A.text,units[i]);}
    for(int c=0;c<8;c++) {
        int x=196+c*114;
        frect(x,548,112,216,A.panel);
        snprintf(b,sizeof(b),channel_linked(c)?"CH%d =":"CH%d",c+1);ui_box(x+9,559,94,26,b,c==A.cur_ch?1:0);
        ui_font(A.font_small);ui_value(x+8,595,96,"");ui_text_fit(x+11,610,90,A.text,CH_ROLE[c]);ui_font(A.font_struct);
        double level=A.dsp_valid?dsp_channel_level(A.dsp_values[26+c]):0;
        level=fader_preview(2,c,level,634,79,60);
        ui_fader(x+32,634,79,level/60,A.dsp_valid);
        ui_font(A.font_small);
        for(int n=0;n<=60;n+=20){snprintf(b,sizeof(b),"%d",n);dtext(x+8,717-n*79/60,A.dim,b);line(x+23,713-n*79/60,x+27,713-n*79/60,A.dim);}
        ui_font(A.font_struct);
        if(A.dsp_valid)snprintf(b,sizeof(b),"%g dB*",level);else strcpy(b,"--");ctext(x+47,645,61,A.text,b);
        ui_output_hardware(x,c);
        int phase=A.dsp_valid&&A.dsp_values[26+c]<5000;
        ui_box(x+62,675,38,19,A.dsp_valid?(phase?"180'":"0'"):"--",phase?1:2);
        ui_speaker(x+64,704,A.dsp_valid);
        if(A.dsp_valid&&A.dsp_values[2+c])line(x+69,707,x+94,721,0xffbb18);
        double ms=A.dsp_valid?dsp_delay_ms(A.dsp_values[73+c]):0;
        if(A.dsp_valid)snprintf(b,sizeof(b),A.delay_unit==0?"%.3f": "%.2f",A.delay_unit==0?ms:A.delay_unit==1?ms*34.6:ms*34.6/2.54);else strcpy(b,"--");
        ui_font(A.font_small);dtext(x+7,750,A.dim,"Delay:");dtext(x+46,750,A.text,b);ui_font(A.font_struct);
    }
    frect(1114,548,120,216,A.panel);
    const char *actions[]={"Edit EQ","Reset EQ","Restore EQ","Reset Output",channel_linked(A.cur_ch)?"Unlink pairs":"Link pairs",output_locked?"Unlock output":"Lock output"};
    for(int i=0;i<6;i++)ui_box(1123,559+i*32,102,23,actions[i],i==4?channel_linked(A.cur_ch):i==5?output_locked:0);
    ui_font(A.font_small);ctext(1114,759,120,A.dim,"Klik nilai untuk edit");
    frect(0,770,1240,30,0x152331);
    snprintf(b,sizeof(b),"CH%d  %s  |  %s",A.cur_ch+1,CH_ROLE[A.cur_ch],A.dsp_valid?A.dsp_time:"Belum ada snapshot");
    dtext(12,789,A.text,b);
    ui_text_fit(386,789,840,A.dim,A.dsp_reading?A.dsp_status:A.console_notice[0]?A.console_notice:!A.connected?"Hubungkan USB lalu Baca DSP untuk mulai mengedit":"USB aktif | *dB: skala vendor | Geser fader / klik angka");
    ui_font(A.font_struct);
    draw_console_modal();
}

static void console_click(int x,int y)
{
    if(A.console_modal==12){protection_click(x,y);return;}
    if(A.console_modal==4){mixer_click(x,y);return;}
    if(A.console_modal==1){memory_click(x,y);return;}
    if(A.console_modal==11){password_click(x,y);return;}
    if(A.console_modal) {
        if(hit(870,250,38,25,x,y)||!hit(318,240,604,310,x,y)){A.console_modal=0;return;}
        if(A.console_modal==6&&hit(343,420,170,32,x,y)){control_apply();return;}
        if(A.console_modal==6&&hit(533,420,100,32,x,y)){A.console_modal=edit_return;return;}
        if(A.console_modal==7){static const int sources[]={1,2,3,7};for(int i=0;i<4;i++)
            if(hit(343+(i%2)*260,335+(i/2)*50,240,36,x,y)&&control_write(1554,sources[i]))A.console_modal=0;}
        if(A.console_modal==9)for(int i=0;i<2;i++) {
            int id=i?1900:1908;
            if(hit(600,345+i*65,250,30,x,y)&&control_ready()){fader_kind=4;fader_index=id;fader_x=x;}
            if(hit(515,345+i*65,65,30,x,y))control_edit(id,8,i?"BT volume":"USB volume",A.dsp_values[id]>=500?A.dsp_values[id]-500:A.dsp_values[id],0,100);
        }
        if(A.console_modal==10&&hit(343,355,210,32,x,y))control_edit(1565,9,"Noise gate level",fmax(0,noise_index(A.dsp_values[1565])),0,20);
        if(A.console_modal==2&&hit(343,395,175,32,x,y))A.console_modal=9;
        if(A.console_modal==2&&hit(538,395,175,32,x,y))A.console_modal=10;
        if(A.console_modal==8&&hit(690,286,150,28,x,y))control_edit(0,7,"Lebar denah (meter)",layout.width_m[layout.mode],0.5,30);
        if(A.console_modal==8&&hit(343,480,160,27,x,y))layout_apply_delays();
        if(A.console_modal==2&&hit(343,350,175,32,x,y)){A.dsp_view=1;A.console_modal=0;}
        if(A.console_modal==2&&hit(538,350,175,32,x,y)){A.dsp_view=0;A.console_modal=0;}
        return;
    }
    if(hit(964,4,130,23,x,y)){window_toggle(0);return;}
    if(hit(1100,4,126,23,x,y)){window_toggle(1);return;}
    if(layout_press(x,y))return;
    if(output_marks_click(x,y))return;
    if(console_control_click(x,y))return;
    if(hit(143,0,76,30,x,y))A.console_modal=1;
    else if(hit(221,0,71,30,x,y))A.console_modal=2;
    else if(hit(292,0,78,30,x,y))A.console_modal=3;
    else if(hit(65,37,111,24,x,y))A.console_modal=7;
    else if(hit(183,38,52,22,x,y))A.console_modal=4;
    else if(hit(244,37,124,24,x,y)){layout_init();A.console_modal=8;}
    else if(hit(377,37,103,24,x,y))A.console_modal=9;
    else if(hit(489,37,99,24,x,y))A.console_modal=10;
    else if(hit(602,37,114,24,x,y)){protection_confirm=0;A.console_modal=12;}
    else if(hit(725,37,115,24,x,y)) {
        if(A.connected||bluetooth_pending)snprintf(A.console_notice,sizeof(A.console_notice),"Disconnect / batalkan dahulu sebelum mengganti USB / Bluetooth.");
        else {bluetooth_selected=!bluetooth_selected;A.dsp_valid=0;snprintf(A.console_notice,sizeof(A.console_notice),"%s dipilih. Klik Connect (scan BLE sekitar 6 detik).",bluetooth_selected?"Bluetooth Mango3.0":"USB");}
    }
    else if(hit(850,37,97,24,x,y))do_readids();
    else if(hit(954,37,108,24,x,y))do_connect();
    else if(hit(102,600,88,155,x,y)) {int unit=(y-600)/57;if(unit<3)A.delay_unit=unit;}
    else if(hit(232,309,992,233,x,y)){A.console_band=(x-232)/32;}
    else {
        for(int c=0;c<8;c++) {
            if(hit(196+c*114,548,112,216,x,y)) {
                A.cur_ch=c;A.overlay_mask=1u<<c;return;
            }
            if(hit(1180,82+c*27,44,20,x,y)){A.overlay_mask^=1u<<c;return;}
        }
    }
    if(A.console_modal)A.console_notice[0]='\0';
}
#endif
