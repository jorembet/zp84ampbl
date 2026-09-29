#ifndef ZP_OUTPUT_MARKS_H
#define ZP_OUTPUT_MARKS_H

/* User installation notes, not independently measured or switched DSP outputs.
   Bit 0 = AMP (CH1-4 only), bit 1 = RCA (CH1-8). Start unmarked. */
static unsigned output_marks[8];
static void output_marks_load(void)
{
    memset(output_marks,0,sizeof(output_marks));
    FILE *f=fopen("zp84-outputs.conf","r");if(!f)return;
    char header[64];unsigned values[8];int valid=fgets(header,sizeof(header),f)&&!strcmp(header,"ZP84_OUTPUT_MARKS 1\n");
    for(int c=0;c<8&&valid;c++)valid=fscanf(f,"%u",&values[c])==1&&values[c]<=3&&(c<4||!(values[c]&1));
    char extra;if(valid&&fscanf(f," %c",&extra)==1)valid=0;
    if(ferror(f))valid=0;
    fclose(f);
    if(valid)memcpy(output_marks,values,sizeof(values));
    else snprintf(A.console_notice,sizeof(A.console_notice),"File penanda output tidak valid; pilih ulang AMP/RCA yang digunakan.");
}
static int output_marks_save(void)
{
    char path[]="zp84-outputs-XXXXXX";int fd=mkstemp(path);
    FILE *f=fd>=0?fdopen(fd,"w"):NULL;
    if(!f){if(fd>=0){close(fd);unlink(path);}return 0;}
    fprintf(f,"ZP84_OUTPUT_MARKS 1\n");
    for(int c=0;c<8;c++)fprintf(f,"%u\n",output_marks[c]);
    int failed=ferror(f);if(fclose(f))failed=1;
    if(failed||rename(path,"zp84-outputs.conf")){unlink(path);return 0;}
    return 1;
}
static int output_marks_click(int x,int y)
{
    for(int c=0;c<8;c++) {
        int xx=196+c*114+46;
        if(!hit(xx,650,62,23,x,y))continue;
        unsigned bit=c<4&&y<662?1:2;
        output_marks[c]^=bit;
        int saved=output_marks_save();
        snprintf(A.console_notice,sizeof(A.console_notice),"CH%d %s %s | penanda penggunaan, bukan sakelar DSP%s",c+1,
                 bit==1?"AMP":"RCA",output_marks[c]&bit?"dipilih":"dilepas",saved?"":" | gagal simpan file");
        return 1;
    }
    return 0;
}
#endif
