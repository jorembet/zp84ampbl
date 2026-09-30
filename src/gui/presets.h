#ifndef ZP_PRESETS_H
#define ZP_PRESETS_H

/* Local scenes contain only mapped controls, never command/password registers.
   No raw snapshot or vendor scene is replayed as an arbitrary register dump. */
static int preset_value_valid(int id, unsigned value)
{
    if(id>=2&&id<=9)return value<=1;
    if((id>=12&&id<=19)||(id>=26&&id<=33)) {
        unsigned n=value>=5000?value-5000:value;
        return n==0||(n>=410&&n<=1000&&n%10==0);
    }
    if(id>=73&&id<=80)return value<=20000;
    if(id==1554)return value==1||value==2||value==3||value==7;
    if(id==1565)return noise_index(value)>=0;
    if(id==1900||id==1908)return value>=500&&value<=600;
    for(int c=0;c<8;c++) {
        int offset=id-136*c;
        if(offset==138||offset==142)return decode_filter(value,offset==138).known;
        if(offset==139||offset==143)return dsp_frequency(value)>=10&&dsp_frequency(value)<=23000;
        if(offset>=147&&offset<=269) {
            int r=(offset-147)%4;
            if(r==0)return dsp_frequency(value)>=20&&dsp_frequency(value)<=20000;
            if(r==1)return value>=380&&value<=620;
            if(r==2)return value>=3&&value<=947;
        }
        for(int source=1;source<=3;source++)for(int i=0;i<(source==3?4:2);i++)
            if(id==mixer_id(source,c,i))return value/256<=100&&(value&255)<=1;
    }
    return 0;
}
static int preset_mapped(int id)
{
    if((id>=2&&id<=9)||(id>=12&&id<=19)||(id>=26&&id<=33)||(id>=73&&id<=80)||id==1554||id==1565||id==1900||id==1908)return 1;
    for(int c=0;c<8;c++) {
        int offset=id-136*c;
        if(offset==138||offset==139||offset==142||offset==143)return 1;
        if(offset>=147&&offset<=269&&(offset-147)%4!=3)return 1;
        for(int source=1;source<=3;source++)for(int i=0;i<(source==3?4:2);i++)if(id==mixer_id(source,c,i))return 1;
    }
    return 0;
}
static void preset_path(int slot,char *path,size_t size)
{
    snprintf(path,size,"zp84-preset-%d.scene",slot+1);
}
static int preset_exists(int slot)
{
    char path[64];preset_path(slot,path,sizeof(path));return access(path,F_OK)==0;
}
static int preset_name_valid(const char *name)
{
    size_t n=strlen(name);int visible=0;
    if(n==0||n>24)return 0;
    for(size_t i=0;i<n;i++){if(name[i]<32||name[i]>126)return 0;if(name[i]!=' ')visible=1;}
    return visible;
}
static int preset_read_header(FILE *f,char *name,size_t size)
{
    char line[96];
    if(!fgets(line,sizeof(line),f))return 0;
    if(!strcmp(line,"ZP84_SCENE 1\n")){snprintf(name,size,"Preset lama");return 1;}
    if(strcmp(line,"ZP84_SCENE 2\n")||!fgets(line,sizeof(line),f)||strncmp(line,"NAME ",5))return 0;
    size_t n=strlen(line);
    if(!n||line[n-1]!='\n')return 0;
    line[n-1]=0;
    if(!preset_name_valid(line+5))return 0;
    snprintf(name,size,"%s",line+5);return 1;
}
static void preset_name(int slot,char *name,size_t size)
{
    char path[64];preset_path(slot,path,sizeof(path));FILE *f=fopen(path,"r");
    snprintf(name,size,"Kosong");
    if(f){if(!preset_read_header(f,name,size))snprintf(name,size,"File tidak valid");fclose(f);}
}
static void preset_name_edit(int slot)
{
    if(!control_ready())return;
    control_edit(slot,10,"Nama preset",0,0,24);
    snprintf(edit_label,sizeof(edit_label),"Nama preset %d (maksimal 24 karakter)",slot+1);
    if(preset_exists(slot))preset_name(slot,edit_text,sizeof(edit_text));
    else snprintf(edit_text,sizeof(edit_text),"Preset %d",slot+1);
}
static int preset_save_named(int slot,const char *name)
{
    if(!preset_name_valid(name)){snprintf(A.console_notice,sizeof(A.console_notice),"Isi nama 1-24 karakter; nama tidak boleh hanya spasi.");return 0;}
    if(!control_ready())return 0;
    for(int id=0;id<ZP_PARAM_COUNT;id++)if(preset_mapped(id)&&!preset_value_valid(id,A.dsp_values[id])) {
        snprintf(A.console_notice,sizeof(A.console_notice),"Preset gagal: ID %04X = %04X belum didukung.",id,A.dsp_values[id]);return 0;
    }
    char path[64],temp[]="zp84-preset-XXXXXX";preset_path(slot,path,sizeof(path));
    int fd=mkstemp(temp);FILE *f=fd>=0?fdopen(fd,"w"):NULL;
    if(!f){if(fd>=0){close(fd);unlink(temp);}goto failed;}
    fprintf(f,"ZP84_SCENE 2\nNAME %s\n",name);
    for(int id=0;id<ZP_PARAM_COUNT;id++)if(preset_mapped(id))fprintf(f,"%d %u\n",id,A.dsp_values[id]);
    int error=ferror(f);if(fclose(f))error=1;
    if(error||rename(temp,path)){unlink(temp);goto failed;}
    snprintf(A.console_notice,sizeof(A.console_notice),"Preset %d tersimpan: %s",slot+1,name);return 1;
failed:
    snprintf(A.console_notice,sizeof(A.console_notice),"Preset gagal disimpan: %s",strerror(errno));
    return 0;
}
static void preset_restore(int slot)
{
    if(output_locked){snprintf(A.console_notice,sizeof(A.console_notice),"Unlock output sebelum memuat preset.");return;}
    if(!control_ready())return;
    char path[64],linebuf[96];preset_path(slot,path,sizeof(path));FILE *f=fopen(path,"r");
    if(!f){snprintf(A.console_notice,sizeof(A.console_notice),"Preset %d tidak dapat dibuka.",slot+1);return;}
    uint16_t values[ZP_PARAM_COUNT]={0};unsigned char seen[ZP_PARAM_COUNT]={0};
    char name[40];int valid=preset_read_header(f,name,sizeof(name));
    while(valid&&fgets(linebuf,sizeof(linebuf),f)) {
        unsigned id,value;char extra;
        valid=sscanf(linebuf,"%u %u %c",&id,&value,&extra)==2&&id<ZP_PARAM_COUNT&&value<=65535;
        if(valid)valid=preset_mapped(id)&&!seen[id]&&preset_value_valid(id,value);
        if(valid){seen[id]=1;values[id]=(uint16_t)value;}
    }
    if(ferror(f))valid=0;
    fclose(f);
    for(int id=0;id<ZP_PARAM_COUNT;id++)if(preset_mapped(id)&&!seen[id])valid=0;
    if(!valid){snprintf(A.console_notice,sizeof(A.console_notice),"Preset rusak/tidak didukung. Tidak ada perubahan dikirim.");return;}
    uint16_t candidate[ZP_PARAM_COUNT];memcpy(candidate,A.dsp_values,sizeof(candidate));
    for(int id=0;id<ZP_PARAM_COUNT;id++)if(seen[id])candidate[id]=values[id];
    for(int ch=0;ch<8;ch++)if(!protection_channel_valid(ch,candidate))return;
    for(int id=0;id<ZP_PARAM_COUNT;id++)if(seen[id]&&!protection_check(id,values[id]))return;
    int changed=0;
    for(int id=0;id<ZP_PARAM_COUNT;id++)if(seen[id]&&values[id]!=A.dsp_values[id])changed++;
    if(!changed){snprintf(A.console_notice,sizeof(A.console_notice),"Preset %d sudah aktif.",slot+1);return;}
    link_output=0; /* A scene may intentionally contain different L/R settings. */
    /* Mute first, restore desired mute state only after every setting succeeds. */
    memset(eq_undo_valid,0,sizeof(eq_undo_valid));
    int done=0;
    for(int c=0;c<8;c++)if(!A.dsp_values[2+c]){if(!control_write(2+c,1))goto failed;done++;}
    for(int id=10;id<ZP_PARAM_COUNT;id++)if(seen[id]&&values[id]!=A.dsp_values[id]) {
        if(!control_write(id,values[id]))goto failed;
        done++;
    }
    for(int c=0;c<8;c++)if(values[2+c]!=A.dsp_values[2+c]){if(!control_write(2+c,values[2+c]))goto failed;done++;}
    memset(eq_undo_valid,0,sizeof(eq_undo_valid));
    snprintf(A.console_notice,sizeof(A.console_notice),"Preset lokal %d diterapkan dan diverifikasi (%d write).",slot+1,done);return;
failed:
    snprintf(A.console_notice,sizeof(A.console_notice),"Preset terhenti setelah %d write; kondisi parsial. Baca DSP ulang.",done);
}
#endif
