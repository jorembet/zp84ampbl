/* Exercise the USB snapshot state machine without hardware or an X server. */
#define main gui_main
#define zp_id_query mock_id_query
#define zp_id_write mock_id_write
#include "../src/gui/zp84gui.c"
#undef zp_id_query
#undef zp_id_write
#undef main
#include <assert.h>

static int response_mode, write_error, write_count, last_id, last_value, fail_write_at;
int mock_id_write(zp_conn *c,uint16_t id,uint16_t value)
{
    (void)c;write_count++;last_id=id;last_value=value;return write_count==fail_write_at?ZP_ERR_TIMEOUT:write_error;
}
static void control_tests(void)
{
    memset(&A,0,sizeof(A));A.connected=1;A.dsp_valid=1;A.dsp_view=2;
    console_click(100,48);assert(A.console_modal==7);
    console_click(620,350);assert(last_id==1554&&last_value==2&&A.console_modal==0);
    A.cur_ch=7;console_click(200,48);assert(A.console_modal==4);
    console_click(1016,mixer_cell_y(0)+12);assert(last_id==1416&&last_value==1);
    console_click(1060,mixer_cell_y(0)+12);assert(A.console_modal==6&&edit_id==1416);
    strcpy(edit_text,"75");control_apply();assert(last_value==19201&&A.console_modal==4);
    A.console_modal=0;A.cur_ch=5;A.dsp_values[0x333]=0x8064;
    console_click(30,180);assert(edit_min==10&&edit_id==0x333);
    strcpy(edit_text,"10");control_apply();assert(last_id==0x333&&last_value==0x8064);
    A.console_modal=0;A.cur_ch=0;
    control_edit(147,1,"Hz",20,20,20000);strcpy(edit_text,"25.5");control_apply();assert(last_value==0x80ff);
    control_edit(148,2,"Gain",0,-12,12);strcpy(edit_text,"-3.5");control_apply();assert(last_value==465);
    int count=write_count;control_edit(148,2,"Gain",0,-12,12);strcpy(edit_text,"nan");control_apply();assert(write_count==count&&A.console_modal==6);
    strcpy(edit_text,"13");control_apply();assert(write_count==count);
    A.console_modal=0;A.dsp_values[26]=5500;
    console_click(250,640);assert(edit_kind==5&&edit_id==0);strcpy(edit_text,"35");control_apply();assert(last_id==26&&last_value==5750);
    A.delay_unit=1;control_edit(73,4,"Delay",0,0,692);strcpy(edit_text,"34.6");control_apply();assert(last_value==1000);
    console_click(250,450);assert(fader_kind==1);
    XEvent motion={0};motion.type=MotionNotify;motion.xmotion.y=428;handle_event(&motion);
    assert(fader_preview(1,0,0,428,69,24)==24);
    A.cur_ch=1;control_release(428);assert(last_id==148&&last_value==620);
    output_locked=1;count=write_count;control_level(0,60);assert(write_count==count);output_locked=0;
    link_output=1;A.dsp_values[27]=5500;control_level(0,20);assert(A.dsp_values[26]==5600&&A.dsp_values[27]==5600);link_output=0;
    A.cur_ch=0;control_eq_reset(0);assert(A.dsp_values[148]==500);control_eq_reset(1);assert(A.dsp_values[148]==620);
    write_error=ZP_ERR_TIMEOUT;count=write_count;assert(!control_write(148,510)&&!A.dsp_valid);assert(A.dsp_values[148]==620);
    assert(!control_write(148,520)&&write_count==count+1);write_error=0;
    assert(mixer_id(1,7,1)==1539&&mixer_id(3,7,3)==1285);
}

static void scene_tests(void)
{
    char *cwd=getcwd(NULL,0),dir[]="/tmp/zp84-scene-test-XXXXXX";
    assert(cwd&&mkdtemp(dir)&&chdir(dir)==0);
    memset(&A,0,sizeof(A));A.connected=1;A.dsp_valid=1;output_locked=0;
    for(int id=0;id<ZP_PARAM_COUNT;id++)if(preset_mapped(id)) {
        unsigned value=0;
        while(value<=65535&&!preset_value_valid(id,value))value++;
        assert(value<=65535);A.dsp_values[id]=(uint16_t)value;
    }
    assert(!preset_mapped(1566)&&!preset_value_valid(1566,42406));
    /* Hardware baseline: CH5/CH6 HPF stores 10 Hz as 0x8064. */
    for(int c=0;c<8;c++)for(int f=0;f<2;f++) {
        int id=139+136*c+4*f;
        assert(preset_value_valid(id,0x8064));
        assert(preset_value_valid(id,10));
        assert(!preset_value_valid(id,0x8063));
        assert(!preset_value_valid(id,23001));
    }
    assert(!preset_value_valid(147,0x8064)); /* EQ still starts at 20 Hz. */
    A.dsp_values[0x333]=0x8064;
    assert(preset_save_named(0,"Harian, Bass"));assert(preset_exists(0));
    char name[40];preset_name(0,name,sizeof(name));assert(!strcmp(name,"Harian, Bass"));
    assert(!preset_save_named(0,"1234567890123456789012345"));
    assert(!preset_save_named(0,"Nama\nPalsu"));
    assert(!preset_save_named(0,"   "));
    preset_name(0,name,sizeof(name));assert(!strcmp(name,"Harian, Bass"));
    A.console_modal=1;console_click(710,290);assert(edit_kind==10&&A.console_modal==6);
    strcpy(edit_text,"Vokal");control_apply();assert(A.console_modal==1);
    preset_name(0,name,sizeof(name));assert(!strcmp(name,"Vokal"));
    A.console_modal=0;
    int count=write_count;preset_restore(0);assert(write_count==count);
    A.dsp_values[0x333]=20;
    A.dsp_values[148]=500;A.console_modal=1;console_click(840,290);A.console_modal=0;
    assert(A.dsp_values[0x333]==0x8064);
    assert(A.dsp_values[148]==380&&A.dsp_values[2]==0&&A.dsp_valid);
    A.dsp_values[148]=500;count=write_count;output_locked=1;preset_restore(0);
    assert(write_count==count);output_locked=0;
    fail_write_at=write_count+9;preset_restore(0);
    assert(!A.dsp_valid&&A.dsp_values[148]==500&&strstr(A.console_notice,"parsial"));
    for(int c=0;c<8;c++)assert(A.dsp_values[2+c]==1);
    fail_write_at=0;A.dsp_valid=1;
    FILE *f=fopen("zp84-preset-1.scene","a");assert(f);fputs("1566 42406\n",f);fclose(f);
    count=write_count;preset_restore(0);assert(write_count==count&&strstr(A.console_notice,"rusak"));
    f=fopen("zp84-preset-1.scene","w");assert(f);fputs("ZP84_SCENE 1\n2 0\n",f);fclose(f);
    preset_restore(0);assert(write_count==count);
    unlink("zp84-preset-1.scene");assert(chdir(cwd)==0);rmdir(dir);free(cwd);
}
static void output_marks_tests(void)
{
    char *cwd=getcwd(NULL,0),dir[]="/tmp/zp84-marks-test-XXXXXX";
    assert(cwd&&mkdtemp(dir)&&chdir(dir)==0);
    memset(&A,0,sizeof(A));output_marks_load();int count=write_count;
    assert(output_marks[0]==0);
    console_click(250,655);assert(output_marks[0]==1);
    console_click(250,667);assert(output_marks[0]==3);
    console_click(250,655);assert(output_marks[0]==2);
    console_click(196+4*114+54,655);assert(output_marks[4]==2);
    memset(output_marks,0,sizeof(output_marks));output_marks_load();
    assert(output_marks[0]==2&&output_marks[4]==2&&write_count==count);
    FILE *f=fopen("zp84-outputs.conf","w");assert(f);
    fputs("ZP84_OUTPUT_MARKS 1\n0 0 0 0 1 0 0 0\n",f);fclose(f);
    output_marks_load();for(int c=0;c<8;c++)assert(output_marks[c]==0);
    assert(write_count==count);
    unlink("zp84-outputs.conf");assert(chdir(cwd)==0);rmdir(dir);free(cwd);
}
static void option_tests(void)
{
    memset(&A,0,sizeof(A));A.connected=1;A.dsp_valid=1;
    int before=write_count;
    console_click(420,48);assert(A.console_modal==9&&write_count==before);
    A.console_modal=0;console_click(530,48);assert(A.console_modal==10&&write_count==before);
    A.console_modal=0;
    console_click(250,15);console_click(400,410);assert(A.console_modal==9);
    console_click(540,360);assert(edit_id==1908&&edit_kind==8);
    strcpy(edit_text,"100");control_apply();assert(last_id==1908&&last_value==600&&A.console_modal==9);
    console_click(540,425);strcpy(edit_text,"0");control_apply();
    assert(last_id==1900&&last_value==500&&A.console_modal==9);
    console_click(540,360);console_click(550,430);assert(A.console_modal==9);
    console_click(725,355);assert(fader_kind==4);control_release(355);assert(last_id==1908&&last_value==550);
    A.console_modal=2;console_click(600,410);assert(A.console_modal==10);
    console_click(400,370);strcpy(edit_text,"20");control_apply();
    assert(last_id==1565&&last_value==103&&A.console_modal==10);
    assert(noise_index(65)==19&&noise_index(999)==-1);
    A.console_modal=4;A.dsp_values[1554]=3;
    console_click(1016,mixer_cell_y(3)+12);assert(last_id==1285&&last_value==1);
    console_click(1060,mixer_cell_y(3)+12);assert(edit_id==1285);
    strcpy(edit_text,"25");control_apply();assert(last_value==6401&&A.console_modal==4);
    int count=write_count;
    console_click(1044,mixer_cell_y(3)+39);assert(fader_kind==5&&write_count==count);
    control_release(0);assert(last_id==1285&&last_value==12801);
    console_click(220,mixer_cell_y(0)+39);assert(fader_kind==5);
    fader_x=9999;control_release(0);assert(last_id==1226&&last_value==25600);
    console_click(220,mixer_cell_y(0)+39);
    /* Cancel pending drag just as the Escape handler does: no write. */
    count=write_count;fader_kind=0;control_release(0);assert(write_count==count);
    mixer_click(1108,mixer_top()+13);assert(A.console_modal==0);
}


int mock_id_query(zp_conn *c, const uint16_t *ids, size_t count,
                  uint8_t *out, size_t cap, size_t *len)
{
    (void)c;
    assert(count <= DSP_READ_BATCH && cap >= count * 4);
    *len = count * 4;
    for (size_t i = 0; i < count; i++) {
        out[i * 4] = (uint8_t)(ids[i] >> 8);
        out[i * 4 + 1] = (uint8_t)ids[i];
        out[i * 4 + 2] = 0x12;
        out[i * 4 + 3] = 0x34;
    }
    if (response_mode == 1) out[1] ^= 1;
    if (response_mode == 2) (*len)--;
    return response_mode == 3 ? ZP_ERR_TIMEOUT : ZP_OK;
}

static void layout_tests(void)
{
    char *cwd=getcwd(NULL,0), dir[]="/tmp/zp84-layout-test-XXXXXX";
    assert(cwd&&mkdtemp(dir)&&chdir(dir)==0);
    memset(&A,0,sizeof(A));memset(&layout,0,sizeof(layout));
    A.dsp_view=2;A.ui_scale=1.5;
    layout_init();
    A.dsp_valid=1;
    for(int mode=0;mode<2;mode++) {
        layout.mode=mode;
        int x=layout.x[mode][0],y=layout.y[mode][0];
        A.dsp_values[2]=1;
        assert(!layout_channel_visible(0)&&layout_channel_visible(1));
        assert(!layout_press(x,y)&&layout.dragging==-1);
        A.dsp_values[2]=0;
        assert(layout_channel_visible(0)&&layout_press(x,y));
        assert(layout.dragging==0&&layout.x[mode][0]==x&&layout.y[mode][0]==y);
        layout_cancel();
    }
    layout.mode=0;
    A.dsp_valid=0;A.dsp_values[2]=1;
    assert(layout_channel_visible(0)); /* Unknown mute state must not hide channels. */
    A.dsp_values[2]=0;
    int writes=write_count;
    XEvent ev={0};ev.type=ButtonPress;ev.xbutton.button=Button1;
    ev.xbutton.x=ui_px(27);ev.xbutton.y=ui_px(358);handle_event(&ev);
    assert(layout.dragging==0&&A.cur_ch==0);
    ev.type=MotionNotify;ev.xmotion.x=ui_px(90);ev.xmotion.y=ui_px(400);handle_event(&ev);
    assert(layout.x[0][0]==90&&layout.y[0][0]==400);
    ev.type=ButtonRelease;ev.xbutton.button=Button1;
    ev.xbutton.x=ui_px(90);ev.xbutton.y=ui_px(400);handle_event(&ev);
    assert(layout.dragging==-1&&write_count==writes);
    console_click(90,320);assert(layout.mode==1&&layout.x[1][0]==27);
    console_click(27,358);layout_motion(-100,1000);
    assert(layout.x[1][0]==18&&layout.y[1][0]==528);
    layout_cancel();assert(layout.x[1][0]==27&&layout.y[1][0]==358);
    console_click(27,358);layout_release(161,358);
    assert(layout.x[1][0]==27); /* Overlapping target is rejected. */
    console_click(27,358);layout_release(80,410);
    assert(layout.x[1][0]==80);
    memset(&layout,0,sizeof(layout));layout_load();
    assert(layout.mode==1&&layout.x[0][0]==90&&layout.x[1][0]==80);
    console_click(160,320);assert(layout.x[1][0]==27&&layout.x[0][0]==90);
    FILE *f=fopen("zp84-layout.conf","w");assert(f);fputs("ZP84_LAYOUT 1 7\n",f);fclose(f);
    memset(&layout,0,sizeof(layout));layout_load();assert(layout.mode==0&&layout.x[0][0]==27);
    assert(write_count==writes);
    unlink("zp84-layout.conf");assert(chdir(cwd)==0);free(cwd);assert(rmdir(dir)==0);
    memset(&layout,0,sizeof(layout));
}

static void distance_tests(void)
{
    char *cwd=getcwd(NULL,0),dir[]="/tmp/zp84-distance-test-XXXXXX";
    assert(cwd&&mkdtemp(dir)&&chdir(dir)==0);
    memset(&A,0,sizeof(A));memset(&layout,0,sizeof(layout));layout_init();
    A.dsp_view=2;A.connected=1;A.dsp_valid=1;
    int writes=write_count;
    layout_apply_delays();assert(write_count==writes); /* Calibration required. */
    A.connected=0;console_click(270,48);assert(A.console_modal==8);
    console_click(710,300);assert(A.console_modal==6&&edit_kind==7);
    strcpy(edit_text,"1.58");control_apply();
    assert(A.console_modal==8&&layout.calibrated[0]&&layout.width_m[0]==1.58);
    A.console_modal=0;
    assert(layout_press(97,430)&&layout.dragging==8);
    layout_release(97,410);assert(layout.y[0][8]==410&&write_count==writes);
    memset(&layout,0,sizeof(layout));layout_load();
    assert(layout.y[0][8]==410&&layout.calibrated[0]&&layout.width_m[0]==1.58);
    assert(layout.width_m[1]==5&&!layout.calibrated[1]);
    layout.x[0][8]=18;layout.y[0][8]=350;
    layout.x[0][0]=48;layout.y[0][0]=390;
    layout.x[0][1]=118;layout.y[0][1]=350;
    for(int c=2;c<8;c++){A.dsp_values[2+c]=1;A.dsp_values[73+c]=321;}
    assert(fabs(layout_distance(0)-0.5)<1e-9&&fabs(layout_distance(1)-1)<1e-9);
    assert(fabs(layout_delay(0)-0.5/0.346)<1e-9&&layout_delay(1)==0);
    layout_apply_delays();assert(write_count==writes); /* Disconnected. */
    A.connected=1;output_locked=1;layout_apply_delays();assert(write_count==writes);output_locked=0;
    layout_apply_delays();assert(write_count==writes+2&&A.dsp_values[73]==1438&&A.dsp_values[74]==0);
    for(int c=2;c<8;c++)assert(A.dsp_values[73+c]==321);
    A.dsp_values[3]=1;assert(layout_delay(0)==0); /* Muted farthest channel excluded. */
    A.dsp_values[2]=1;writes=write_count;layout_apply_delays();assert(write_count==writes);
    A.dsp_values[2]=A.dsp_values[3]=0;layout.width_m[0]=30;
    layout_apply_delays();assert(write_count==writes&&strstr(A.console_notice,"20 ms"));
    layout.width_m[0]=1.58;fail_write_at=write_count+2;
    layout_apply_delays();assert(write_count==writes+2&&!A.dsp_valid&&strstr(A.console_notice,"1 channel"));
    fail_write_at=0;
    FILE *f=fopen("zp84-layout.conf","w");assert(f);fputs("ZP84_LAYOUT 1 0\n",f);
    for(int m=0;m<2;m++)for(int c=0;c<8;c++)fprintf(f,"%d %d\n",c%2?161:27,358+c/2*53);
    fclose(f);memset(&layout,0,sizeof(layout));layout_load();
    assert(layout.x[0][8]==97&&layout.y[0][8]==430&&!layout.calibrated[0]);
    unlink("zp84-layout.conf");assert(chdir(cwd)==0);free(cwd);assert(rmdir(dir)==0);
    memset(&layout,0,sizeof(layout));
}

int main(void)
{
    layout_tests();
    control_tests();
    option_tests();
    output_marks_tests();
    scene_tests();
    distance_tests();
    assert(dsp_frequency(0x80c8) == 20.0);
    assert(dsp_frequency(5000) == 5000.0);
    assert(dsp_channel_level(5990) == 59);
    assert(dsp_channel_level(980) == 58);
    assert(dsp_channel_level(6000) == 60);
    assert(dsp_delay_ms(208) == 0.208);
    assert(dsp_delay_ms(0) == 0.0);
    console_filter hp = decode_filter(56, 1), lp = decode_filter(25, 0);
    assert(hp.known && hp.family == 2 && hp.slope == 36);
    assert(lp.known && lp.family == 2 && lp.slope == 24);
    assert(decode_filter(69, 0).slope == 0);
    assert(!decode_filter(65535, 0).known);
    assert(fabs(console_filter_db(hp, 100, 100, 1) + 6.020599913) < 0.00001);
    assert(console_filter_db(decode_filter(69, 0), 5000, 1000, 0) == 0);
    memset(&A, 0, sizeof(A));
    A.dsp_view = 2;
    XEvent click = {0};
    click.type = ButtonPress;
    click.xbutton.button = Button1;
    click.xbutton.x = 196 + 3 * 114 + 20;
    click.xbutton.y = 570;
    handle_event(&click);
    assert(A.cur_ch == 3 && A.dsp_view == 2);
    click.xbutton.x = 240;
    click.xbutton.y = 15;
    handle_event(&click);
    assert(A.console_modal == 2);
    click.xbutton.x = 360;
    click.xbutton.y = 365;
    handle_event(&click);
    assert(A.dsp_view == 1);
    click.xbutton.x = 20;
    click.xbutton.y = 55;
    handle_event(&click);
    assert(A.dsp_view == 2);
    console_click(120, 680);
    assert(A.delay_unit == 1);
    console_click(232 + 30 * 32 + 2, 400);
    assert(A.console_band == 30);
    console_click(1190, 83);
    assert(A.overlay_mask & 1u);
    console_click(1140, 600);
    assert(A.console_modal == 0);
    assert(strstr(A.console_notice,"USB"));
    assert(!A.dsp_reading);
    console_click(880, 260);
    assert(A.console_modal == 0);
    A.ui_scale = 1.5;
    click.xbutton.x = ui_px(196 + 6 * 114 + 10);
    click.xbutton.y = ui_px(565);
    handle_event(&click);
    assert(A.cur_ch == 6);
    A.offset_x=120;A.offset_y=30;
    click.xbutton.x=A.offset_x+ui_px(196+3*114+10);
    click.xbutton.y=A.offset_y+ui_px(565);
    handle_event(&click);assert(A.cur_ch==3);
    A.offset_x=0;A.offset_y=0;
    A.ui_scale = 0;
    A.conn.fd = -1;
    A.connected = 1;
    do_readids();
    read_dsp_batch();
    assert(A.dsp_next == 8 && !A.dsp_valid && A.dsp_reading);
    assert(A.dsp_values[0] == 0); /* No partial snapshot is published. */
    while (A.dsp_reading) read_dsp_batch();
    assert(A.dsp_valid && A.dsp_next == ZP_PARAM_COUNT);
    for (int i = 0; i < ZP_PARAM_COUNT; i++) assert(A.dsp_values[i] == 0x1234);
    for (response_mode = 1; response_mode <= 3; response_mode++) {
        A.connected = 1;
        do_readids();
        read_dsp_batch();
        assert(!A.dsp_reading && !A.connected && A.dsp_valid);
        assert(A.dsp_values[0] == 0x1234);
        assert(strstr(A.dsp_status, "Gagal"));
    }
    dsp_page_move(-1);
    assert(A.dsp_page == 0);
    for (int i = 0; i < 100; i++) dsp_page_move(1);
    assert(A.dsp_page == (ZP_PARAM_COUNT - 1) / DSP_PAGE_SIZE);
    puts("GUI snapshot checks passed: complete, partial, wrong ID, short, timeout, paging");
    return 0;
}
