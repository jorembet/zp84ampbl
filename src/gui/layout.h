#ifndef ZP_LAYOUT_H
#define ZP_LAYOUT_H

/* Dragging edits the local diagram. Only Apply Delay writes DSP registers. */
static struct {
    int ready, mode, dragging, old_x, old_y, offset_x, offset_y;
    int x[2][9], y[2][9]; /* Slot 8 is the listening position. */
    double width_m[2];
    int calibrated[2];
} layout;

static int layout_channel_visible(int channel)
{
    return !A.dsp_valid || !A.dsp_values[2+channel];
}

static void layout_defaults(int mode)
{
    for (int c=0;c<8;c++) {
        layout.x[mode][c]=c%2?161:27;
        layout.y[mode][c]=358+(c/2)*53;
    }
    layout.x[mode][8]=97;layout.y[mode][8]=mode?465:430;
}
static void layout_init(void)
{
    if(layout.ready)return;
    layout.ready=1;layout.dragging=-1;
    layout_defaults(0);layout_defaults(1);
    layout.width_m[0]=2;layout.width_m[1]=5;
}
static void layout_save(void)
{
    char path[]="zp84-layout-XXXXXX";
    int fd=mkstemp(path);
    FILE *f=fd>=0?fdopen(fd,"w"):NULL;
    if(!f){if(fd>=0){close(fd);unlink(path);}goto failed;}
    fprintf(f,"ZP84_LAYOUT 2 %d\n",layout.mode);
    for(int m=0;m<2;m++)for(int c=0;c<9;c++)fprintf(f,"%d %d\n",layout.x[m][c],layout.y[m][c]);
    for(int m=0;m<2;m++)fprintf(f,"%.9g %d\n",layout.width_m[m],layout.calibrated[m]);
    int error=ferror(f);
    if(fclose(f))error=1;
    if(error||rename(path,"zp84-layout.conf")){unlink(path);goto failed;}
    return;
failed:
    snprintf(A.console_notice,sizeof(A.console_notice),"Posisi aktif, tetapi gagal disimpan ke zp84-layout.conf.");
}
static void layout_load(void)
{
    layout_init();
    FILE *f=fopen("zp84-layout.conf","r");
    if(!f)return;
    int version=0,mode=0,x[2][9],y[2][9],calibrated[2]={0,0};
    double width[2]={2,5};
    memcpy(x,layout.x,sizeof(x));memcpy(y,layout.y,sizeof(y));
    int valid=fscanf(f,"ZP84_LAYOUT %d %d",&version,&mode)==2&&(version==1||version==2)&&mode>=0&&mode<=1;
    for(int m=0;m<2&&valid;m++)for(int c=0;c<(version==2?9:8)&&valid;c++) {
        valid=fscanf(f,"%d %d",&x[m][c],&y[m][c])==2;
        if(valid)valid=x[m][c]>=18&&x[m][c]<=176&&y[m][c]>=350&&y[m][c]<=528;
    }
    if(version==2&&valid)for(int m=0;m<2&&valid;m++) {
        valid=fscanf(f,"%lf %d",&width[m],&calibrated[m])==2;
        if(valid)valid=isfinite(width[m])&&width[m]>=0.5&&width[m]<=30&&(calibrated[m]==0||calibrated[m]==1);
    }
    fclose(f);
    if(valid){layout.mode=mode;memcpy(layout.x,x,sizeof(x));memcpy(layout.y,y,sizeof(y));memcpy(layout.width_m,width,sizeof(width));memcpy(layout.calibrated,calibrated,sizeof(calibrated));}
    else snprintf(A.console_notice,sizeof(A.console_notice),"File posisi tidak valid; memakai posisi awal.");
}
static void layout_set_width(double value)
{
    layout_init();
    layout.width_m[layout.mode]=value;layout.calibrated[layout.mode]=1;
    snprintf(A.console_notice,sizeof(A.console_notice),"Skala tersimpan. Geser P / channel; buka Jarak / Delay untuk menerapkan.");
    layout_save();
}
static double layout_distance(int c)
{
    int m=layout.mode;
    return hypot(layout.x[m][c]-layout.x[m][8],layout.y[m][c]-layout.y[m][8])*layout.width_m[m]/158.0;
}
static double layout_delay(int c)
{
    double farthest=0;
    for(int i=0;i<8;i++)if(layout_channel_visible(i))farthest=fmax(farthest,layout_distance(i));
    return fmax(0,(farthest-layout_distance(c))/0.346);
}
static void layout_feedback(void)
{
    snprintf(A.console_notice,sizeof(A.console_notice),"%sCH%d: %.2f m | preview delay %.3f ms | P = posisi dengar | Jarak / Delay",
             layout.calibrated[layout.mode]?"":"Skala perkiraan! ",A.cur_ch+1,layout_distance(A.cur_ch),layout_delay(A.cur_ch));
}
static void layout_apply_delays(void)
{
    layout_init();
    if(!layout.calibrated[layout.mode]){snprintf(A.console_notice,sizeof(A.console_notice),"Atur lebar denah dalam meter terlebih dahulu.");return;}
    if(output_locked){snprintf(A.console_notice,sizeof(A.console_notice),"Output terkunci. Klik Unlock output terlebih dahulu.");return;}
    if(link_output){snprintf(A.console_notice,sizeof(A.console_notice),"Lepas Link pairs sebelum menerapkan delay berbeda dari posisi speaker.");return;}
    if(!control_ready())return;
    uint16_t values[8];int count=0,done=0;
    for(int c=0;c<8;c++) {
        values[c]=0;
        if(!layout_channel_visible(c))continue;
        double ms=layout_delay(c);
        if(!isfinite(ms)||ms>20){snprintf(A.console_notice,sizeof(A.console_notice),"CH%d melebihi batas delay 20 ms. Tidak ada delay dikirim.",c+1);return;}
        values[c]=(uint16_t)lround(round(ms*48)*1000/48);count++;
    }
    if(!count){snprintf(A.console_notice,sizeof(A.console_notice),"Semua channel mute. Tidak ada delay dikirim.");return;}
    for(int c=0;c<8;c++)if(layout_channel_visible(c)) {
        if(!control_write(73+c,values[c])) {
            snprintf(A.console_notice,sizeof(A.console_notice),"Gagal pada CH%d; %d channel sudah diterapkan. Baca DSP sebelum mencoba lagi.",c+1,done);
            return;
        }
        done++;
    }
    snprintf(A.console_notice,sizeof(A.console_notice),"Delay %d channel aktif terverifikasi. Channel mute tidak diubah.",done);
}
static void layout_cancel(void)
{
    if(!layout.ready||layout.dragging<0)return;
    layout.x[layout.mode][layout.dragging]=layout.old_x;
    layout.y[layout.mode][layout.dragging]=layout.old_y;
    layout.dragging=-1;
}
static int layout_press(int x,int y)
{
    layout_init();
    if(hit(10,313,60,23,x,y)||hit(74,313,60,23,x,y)) {
        layout.mode=x<74?0:1;layout_save();return 1;
    }
    if(hit(138,313,46,23,x,y)) {
        layout_defaults(layout.mode);layout_save();return 1;
    }
    /* Selected channel has priority when loading an overlapping old layout. */
    for(int n=0;n<8;n++) {
        int c=(A.cur_ch+n)%8;
        if(!layout_channel_visible(c))continue;
        if(hit(layout.x[layout.mode][c]-12,layout.y[layout.mode][c]-12,24,24,x,y)) {
            A.cur_ch=c;A.overlay_mask=1u<<c;
            layout.dragging=c;layout.old_x=layout.x[layout.mode][c];layout.old_y=layout.y[layout.mode][c];
            layout.offset_x=x-layout.old_x;layout.offset_y=y-layout.old_y;
            snprintf(A.console_notice,sizeof(A.console_notice),"Geser CH%d sesuai posisi speaker. Esc: batal. Reset: posisi awal.",c+1);
            return 1;
        }
    }
    if(hit(layout.x[layout.mode][8]-10,layout.y[layout.mode][8]-10,20,20,x,y)) {
        layout.dragging=8;layout.old_x=layout.x[layout.mode][8];layout.old_y=layout.y[layout.mode][8];
        layout.offset_x=x-layout.old_x;layout.offset_y=y-layout.old_y;
        layout_feedback();return 1;
    }
    return 0;
}
static void layout_motion(int x,int y)
{
    if(!layout.ready||layout.dragging<0)return;
    layout.x[layout.mode][layout.dragging]=(int)zp_clampd(x-layout.offset_x,18,176);
    layout.y[layout.mode][layout.dragging]=(int)zp_clampd(y-layout.offset_y,350,528);
    layout_feedback();
}
static void layout_release(int x,int y)
{
    if(!layout.ready||layout.dragging<0)return;
    layout_motion(x,y);
    int c=layout.dragging,m=layout.mode;
    for(int i=0;i<9;i++)if(i!=c) {
        int dx=layout.x[m][c]-layout.x[m][i],dy=layout.y[m][c]-layout.y[m][i];
        if(dx*dx+dy*dy<22*22) {
            layout_cancel();
            snprintf(A.console_notice,sizeof(A.console_notice),"Posisi terlalu dekat dengan CH%d. Geser ke tempat yang kosong.",i+1);
            return;
        }
    }
    int moved=layout.old_x!=layout.x[m][c]||layout.old_y!=layout.y[m][c];
    layout.dragging=-1;
    if(moved) {
        layout_feedback();
        layout_save();
    }
}
#endif
