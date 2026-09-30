#ifndef ZP_SPEAKER_NAMES_H
#define ZP_SPEAKER_NAMES_H
/* Installation labels only; renaming never changes routing or protection. */
static int speaker_names_path(char *path,size_t size)
{
    const char *home=getenv("HOME");
    return home&&snprintf(path,size,"%s/.zp84-speaker-names.conf",home)<(int)size;
}
static int speaker_name_valid(const char *name)
{
    size_t len=strlen(name);int nonspace=0;
    if(!len||len>24)return 0;
    for(size_t i=0;i<len;i++) {
        unsigned char c=(unsigned char)name[i];if(c<32||c>126)return 0;
        if(c!=' ')nonspace=1;
    }
    return nonspace;
}
static void speaker_names_load(void)
{
    char path[1024],line[128],names[8][25];
    if(!speaker_names_path(path,sizeof(path)))return;
    FILE *f=fopen(path,"r");if(!f)return;
    int valid=fgets(line,sizeof(line),f)&&!strcmp(line,"ZP84_SPEAKER_NAMES 1\n");
    for(int i=0;i<8&&valid;i++) {
        if(!fgets(line,sizeof(line),f)){valid=0;break;}
        line[strcspn(line,"\r\n")]=0;valid=speaker_name_valid(line);
        if(valid)memcpy(names[i],line,strlen(line)+1);
    }
    if(fgetc(f)!=EOF||ferror(f))valid=0;
    fclose(f);if(valid)memcpy(CH_ROLE,names,sizeof(names));
}
static int speaker_name_save(int channel,const char *name)
{
    if(channel<0||channel>7||!speaker_name_valid(name)) {
        snprintf(A.console_notice,sizeof(A.console_notice),"Nama wajib diisi, maksimal 24 karakter (huruf, angka, tanda baca).");return 0;
    }
    char dest[1024],tmp[1100];
    if(!speaker_names_path(dest,sizeof(dest)))goto failed;
    snprintf(tmp,sizeof(tmp),"%s.XXXXXX",dest);
    int fd=mkstemp(tmp);if(fd<0)goto failed;
    FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(tmp);goto failed;}
    fprintf(f,"ZP84_SPEAKER_NAMES 1\n");
    for(int c=0;c<8;c++)fprintf(f,"%s\n",c==channel?name:CH_ROLE[c]);
    int err=ferror(f);if(fclose(f))err=1;
    if(err||rename(tmp,dest)){unlink(tmp);goto failed;}
    snprintf(CH_ROLE[channel],sizeof(CH_ROLE[channel]),"%s",name);
    snprintf(A.console_notice,sizeof(A.console_notice),"Nama speaker CH%d diperbarui: %s",channel+1,name);return 1;
failed:
    snprintf(A.console_notice,sizeof(A.console_notice),"Gagal menyimpan nama speaker; nama sebelumnya dipertahankan.");return 0;
}
#endif
