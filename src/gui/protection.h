#ifndef ZP_PROTECTION_H
#define ZP_PROTECTION_H
/* Local edit guard, NOT a hardware limiter or a claim of speaker safety.
   Explicitly approved current settings become the maximum editing envelope. */
typedef struct {
    int enabled, role;
    uint16_t hp_type,hp_freq,lp_type,lp_freq,level,master;
    uint16_t eq[124],eq_enable;
} speaker_guard;
static speaker_guard guards[8];
static int protection_confirm;
static int protection_file_invalid;
static char protection_path[1024];
static const char *protection_filename(void)
{
    if(!protection_path[0]) {
        const char *home=getenv("HOME");
        if(!home||snprintf(protection_path,sizeof(protection_path),"%s/.zp84-protection.conf",home)>=(int)sizeof(protection_path))return NULL;
    }
    return protection_path;
}
static int protection_error(int ch,const char *reason)
{
    snprintf(A.console_notice,sizeof(A.console_notice),"Proteksi CH%d: %s",ch+1,reason);return 0;
}
static int protection_filter(uint16_t type,uint16_t freq,uint16_t bound_type,uint16_t bound_freq,int hp)
{
    console_filter f=decode_filter(type,hp),b=decode_filter(bound_type,hp);
    double hz=dsp_frequency(freq),limit=dsp_frequency(bound_freq);
    return f.known&&b.known&&f.slope>=b.slope&&f.slope>0&&f.family==b.family&&
        isfinite(hz)&&hz>=10&&hz<=23000&&limit>=10&&limit<=23000&&(hp?hz>=limit:hz<=limit);
}
static int protection_channel_valid(int ch,const uint16_t *v)
{
    speaker_guard *g=&guards[ch];if(!g->enabled)return 1;
    int b=136*ch;
    if(!protection_filter(v[b+138],v[b+139],g->hp_type,g->hp_freq,1))return protection_error(ch,"HPF OFF / cutoff atau slope di bawah batas / jenis berubah.");
    if(g->role==2&&!protection_filter(v[b+142],v[b+143],g->lp_type,g->lp_freq,0))return protection_error(ch,"LPF OFF / cutoff atau slope di luar batas / jenis berubah.");
    if(g->role==2&&dsp_frequency(v[b+139])>=dsp_frequency(v[b+143]))return protection_error(ch,"HPF harus lebih rendah daripada LPF.");
    if(dsp_channel_level(v[26+ch])>dsp_channel_level(g->level)||dsp_channel_level(v[12+ch])>dsp_channel_level(g->master))return protection_error(ch,"level melebihi batas tersimpan.");
    if(v[65+ch]!=g->eq_enable)return protection_error(ch,"status bypass EQ terkunci.");
    for(int i=0;i<124;i++) {
        int id=b+146+i;
        if(i%4==2){if(v[id]>g->eq[i])return protection_error(ch,"gain EQ melebihi batas band tersimpan.");}
        else if(v[id]!=g->eq[i])return protection_error(ch,"tipe, frekuensi dan Q EQ terkunci.");
    }
    return 1;
}
static int protection_check(int id,uint16_t value)
{
    if(id<0||id>=ZP_PARAM_COUNT)return 0;
    if(id>=2&&id<=9&&value==1)return 1;
    if(protection_file_invalid)return protection_error(0,"file proteksi rusak; pulihkan ~/.zp84-protection.conf.");
    if(value==A.dsp_values[id]&&!(id>=2&&id<=9&&value==0))return 1;
    uint16_t v[ZP_PARAM_COUNT];memcpy(v,A.dsp_values,sizeof(v));v[id]=value;
    for(int ch=0;ch<8;ch++)if(guards[ch].enabled) {
        int b=136*ch;
        if(id==2+ch&&value!=0)continue; /* Mute is always available. */
        if((id==2+ch&&value==0)||id==26+ch||id==12+ch||id==65+ch||(id>=b+138&&id<=b+269)) {
            if(!protection_channel_valid(ch,v))return 0;
        }
        /* Routing/input changes may increase signal before the output guard. */
        if((id>=1226&&id<=1554)||id==1900||id==1908)
            return protection_error(ch,"input / mixer terkunci selama proteksi aktif.");
    }
    return 1;
}
static int protection_save(void)
{
    const char *dest=protection_filename();char path[1100];
    if(!dest)return 0;
    snprintf(path,sizeof(path),"%s.XXXXXX",dest);int fd=mkstemp(path);FILE *f=fd>=0?fdopen(fd,"w"):NULL;
    if(!f){if(fd>=0){close(fd);unlink(path);}return 0;}
    fprintf(f,"ZP84_PROTECTION 1\n");
    for(int c=0;c<8;c++) {
        speaker_guard *g=&guards[c];
        fprintf(f,"%d %d %u %u %u %u %u %u %u\n",g->enabled,g->role,g->hp_type,g->hp_freq,g->lp_type,g->lp_freq,g->level,g->master,g->eq_enable);
        for(int i=0;i<124;i++)fprintf(f,"%u%c",g->eq[i],i==123?'\n':' ');
    }
    int fail=ferror(f);if(fclose(f))fail=1;
    if(fail||rename(path,dest)){unlink(path);return 0;}return 1;
}
static void protection_load(void)
{
    const char *path=protection_filename();if(!path){protection_file_invalid=1;return;}
    FILE *f=fopen(path,"r");
    if(!f){if(errno!=ENOENT)protection_file_invalid=1;return;}
    speaker_guard tmp[8]={0};char header[80];int valid=fgets(header,sizeof(header),f)&&!strcmp(header,"ZP84_PROTECTION 1\n");
    for(int c=0;c<8&&valid;c++) {
        unsigned n[133];for(int i=0;i<133&&valid;i++)valid=fscanf(f,"%u",&n[i])==1&&n[i]<=65535;
        if(!valid)break;
        valid=n[0]<=1&&n[1]<=2&&(!n[0]||n[1]);
        tmp[c]=(speaker_guard){.enabled=n[0],.role=n[1],.hp_type=n[2],.hp_freq=n[3],.lp_type=n[4],.lp_freq=n[5],.level=n[6],.master=n[7],.eq_enable=n[8]};
        for(int i=0;i<124;i++)tmp[c].eq[i]=n[9+i];
        if(n[0])valid=valid&&protection_filter(n[2],n[3],n[2],n[3],1)&&(n[1]!=2||protection_filter(n[4],n[5],n[4],n[5],0));
    }
    char extra;if(valid&&fscanf(f," %c",&extra)==1)valid=0;if(ferror(f))valid=0;fclose(f);
    if(valid)memcpy(guards,tmp,sizeof(guards));else protection_file_invalid=1;
}
static int protection_capture(int ch,int role)
{
    if(ch<0||ch>7||(role!=1&&role!=2))return 0;
    if(!A.connected||!A.dsp_valid||A.dsp_reading)return protection_error(ch,"hubungkan dan Baca DSP terlebih dahulu.");
    if(!protection_confirm)return protection_error(ch,"konfirmasikan batas sesuai spesifikasi speaker terlebih dahulu.");
    int b=136*ch;speaker_guard old=guards[ch];
    guards[ch]=(speaker_guard){.enabled=1,.role=role,.hp_type=A.dsp_values[b+138],.hp_freq=A.dsp_values[b+139],.lp_type=A.dsp_values[b+142],.lp_freq=A.dsp_values[b+143],.level=A.dsp_values[26+ch],.master=A.dsp_values[12+ch],.eq_enable=A.dsp_values[65+ch]};
    memcpy(guards[ch].eq,&A.dsp_values[b+146],sizeof(guards[ch].eq));
    if(!protection_channel_valid(ch,A.dsp_values)||!protection_save()){guards[ch]=old;return protection_error(ch,"HPF/LPF wajib valid dan aktif; periksa batas atau izin file.");}
    protection_confirm=0;return 1;
}
#endif
