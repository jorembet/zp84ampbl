#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../dsp/design.h"
#include "../hid/zp_hid.h"

#define WIN_W 1240
#define WIN_H 800
#define PAD 12
#define PLOT_X 190
#define PLOT_Y 92
#define PLOT_W 700
#define PLOT_H 430
#define SIDE_X 916
#define SIDE_W 300
#define LOG_H 40

#define F_MIN 20.0
#define F_MAX 20000.0
#define DB_TOP 15.0
#define DB_BOT -15.0

typedef struct {
    int x, y, w, h;
    const char *label;
    int id;
} button;

enum {
    BT_CH1 = 1, BT_CH2, BT_CH3, BT_CH4, BT_CH5, BT_CH6, BT_CH7, BT_CH8,
    BT_HPF, BT_LPFT, BT_POL, BT_HDOWN, BT_HUP, BT_LDOWN, BT_LUP,
    BT_SLOPE, BT_DELAY_D, BT_DELAY_U, BT_CONNECT, BT_READIDS, BT_AB,
    BT_PREAMP, BT_RESET
};

typedef struct {
    Display *dpy;
    Window win;
    GC gc;
    Pixmap buf;
    XImage *shot;
    Font font;
    XFontStruct *font_struct;
    int depth;
    unsigned long px;
    unsigned long bg, panel, panel2, text, dim, grid, curve, xover, warn, good,
        accent, sel;
    zp_design design;
    int cur_ch;
    int dragging;
    int hover_band;
    int hpf_on, lpf_on, pol;
    double hpf_f, lpf_f, delay_ms;
    int slope;
    zp_conn conn;
    int connected;
    char log[6][160];
    int nlog;
    int ab;
} app;

static app A;

static const char *CH_ROLE[8] = {"Woofer", "Woofer", "Midrange", "Midrange",
                                 "Tweeter", "Tweeter", "Subwoofer", "Subwoofer"};

static const int SLOPES[] = {12, 24, 48};
#define NSLOPE 3

static const char *SLOPE_NAME[NSLOPE] = {"12 dB/oct", "24 dB/oct", "48 dB/oct"};

static void logline(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (A.nlog >= 6) {
        int i;
        for (i = 0; i < 5; i++)
            memcpy(A.log[i], A.log[i + 1], sizeof(A.log[0]));
        A.nlog = 5;
    }
    vsnprintf(A.log[A.nlog], sizeof(A.log[0]), fmt, ap);
    A.nlog++;
    va_end(ap);
}

static double x_of_f(double f)
{
    double t = (log10(f) - log10(F_MIN)) / (log10(F_MAX) - log10(F_MIN));
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return PLOT_X + t * PLOT_W;
}

static double f_of_x(double x)
{
    double t = (x - PLOT_X) / (double)PLOT_W;
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return F_MIN * pow(F_MAX / F_MIN, t);
}

static double y_of_db(double db)
{
    double t = (DB_TOP - db) / (DB_TOP - DB_BOT);
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return PLOT_Y + t * PLOT_H;
}

static double db_of_y(double y)
{
    double t = (y - PLOT_Y) / (double)PLOT_H;
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return DB_TOP - t * (DB_TOP - DB_BOT);
}

static void frect(int x, int y, int w, int h, unsigned long c)
{
    XSetForeground(A.dpy, A.gc, c);
    XFillRectangle(A.dpy, A.buf, A.gc, x, y, (unsigned)w, (unsigned)h);
}

static void fframe(int x, int y, int w, int h, unsigned long c)
{
    XSetForeground(A.dpy, A.gc, c);
    XDrawRectangle(A.dpy, A.buf, A.gc, x, y, (unsigned)(w - 1), (unsigned)(h - 1));
}

static void line(int x0, int y0, int x1, int y1, unsigned long c)
{
    XSetForeground(A.dpy, A.gc, c);
    XDrawLine(A.dpy, A.buf, A.gc, x0, y0, x1, y1);
}

static void dashed_v(int x, int y0, int y1, unsigned long c)
{
    static char dash[2] = {2, 4};
    XSetForeground(A.dpy, A.gc, c);
    XSetLineAttributes(A.dpy, A.gc, 1, LineOnOffDash, CapButt, JoinMiter);
    XSetDashes(A.dpy, A.gc, 0, dash, 2);
    XDrawLine(A.dpy, A.buf, A.gc, x, y0, x, y1);
    XSetLineAttributes(A.dpy, A.gc, 1, LineSolid, CapButt, JoinMiter);
}

static void dtext(int x, int y, unsigned long c, const char *s)
{
    XSetForeground(A.dpy, A.gc, c);
    XDrawString(A.dpy, A.buf, A.gc, x, y, s, (int)strlen(s));
}

static int text_width(const char *s)
{
    return XTextWidth(A.font_struct, s, (int)strlen(s));
}

static void ctext(int x, int y, int w, unsigned long c, const char *s)
{
    dtext(x + (w - text_width(s)) / 2, y, c, s);
}

static int hit(int bx, int by, int bw, int bh, int mx, int my)
{
    return mx >= bx && mx < bx + bw && my >= by && my < by + bh;
}

static int band_at(int mx, int my)
{
    int i, best = -1;
    double bd = 14.0;
    if (my < PLOT_Y - 8 || my > PLOT_Y + PLOT_H + 8)
        return -1;
    for (i = 0; i < ZP_BANDS; i++) {
        double fx = x_of_f(zp_band_freq(i));
        double fy = y_of_db(A.design.ch[A.cur_ch].gain_db[i]);
        double d = hypot(fx - mx, fy - my);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static void draw_button(int x, int y, int w, int h, const char *label,
                        int on, int accent_on)
{
    unsigned long bg = on ? (accent_on ? A.accent : A.sel) : A.panel2;
    unsigned long fg = on ? A.bg : A.text;
    frect(x, y, w, h, bg);
    fframe(x, y, w, h, accent_on ? A.accent : A.grid);
    ctext(x, y + h / 2 + 4, w, fg, label);
}

static void draw(void)
{
    int i, k;
    char buf[64];
    zp_cascade casc;

    frect(0, 0, WIN_W, WIN_H, A.bg);

    frect(0, 0, WIN_W, 34, A.panel);
    dtext(PAD, 23, A.text, "ZP 8.4 AMP  -  DSP Editor");
    snprintf(buf, sizeof(buf), "48 kHz  |  CH%d %s", A.cur_ch + 1,
             CH_ROLE[A.cur_ch]);
    dtext(300, 23, A.dim, buf);
    {
        const char *st = A.connected ? "device: connected"
                                     : "device: not connected";
        int w = (int)strlen(st) * 7 + 20;
        draw_button(WIN_W - w - 12, 5, w, 24, st, A.connected, A.connected);
    }

    frect(PLOT_X - 6, PLOT_Y - 6, PLOT_W + 12, PLOT_H + 12, A.panel);

    {
        static const double ticks[] = {20, 50, 100, 200, 500, 1000,
                                       2000, 5000, 10000, 20000};
        for (i = 0; i < 10; i++) {
            double ff = ticks[i];
            int x = (int)x_of_f(ff);
            line(x, PLOT_Y, x, PLOT_Y + PLOT_H, A.grid);
            if (ff >= 1000)
                snprintf(buf, sizeof(buf), "%gk", ff / 1000);
            else
                snprintf(buf, sizeof(buf), "%g", ff);
            ctext(x - 20, PLOT_Y + PLOT_H + 16, 40, A.dim, buf);
        }
    }
    dtext(PLOT_X + PLOT_W - 34, PLOT_Y + PLOT_H + 30, A.dim, "Hz");

    for (i = -3; i <= 3; i++) {
        double db = i * 5.0;
        int y = (int)y_of_db(db);
        line(PLOT_X, y, PLOT_X + PLOT_W, y, i == 0 ? A.dim : A.grid);
        snprintf(buf, sizeof(buf), "%+.0f", db);
        dtext(PLOT_X - 42, y + 4, A.dim, buf);
    }
    dtext(10, PLOT_Y - 12, A.dim, "dB");

    if (A.hpf_on)
        dashed_v((int)x_of_f(A.hpf_f), PLOT_Y, PLOT_Y + PLOT_H, A.xover);
    if (A.lpf_on)
        dashed_v((int)x_of_f(A.lpf_f), PLOT_Y, PLOT_Y + PLOT_H, A.xover);

    zp_design_cascade(&A.design, A.cur_ch, &casc);
    {
        int px = PLOT_X, py = (int)y_of_db(zp_cascade_response_db(&casc, 48000.0, F_MIN));
        for (i = 1; i <= PLOT_W; i++) {
            double ff = f_of_x(PLOT_X + i);
            int y = (int)y_of_db(zp_cascade_response_db(&casc, 48000.0, ff));
            line(px, py, PLOT_X + i, y, A.curve);
            px = PLOT_X + i;
            py = y;
        }
    }

    for (i = 0; i < ZP_BANDS; i++) {
        double g = A.design.ch[A.cur_ch].gain_db[i];
        int x = (int)x_of_f(zp_band_freq(i));
        int y = (int)y_of_db(g);
        int s = (i == A.hover_band) ? 6 : 4;
        frect(x - s, y - s, s * 2, s * 2, A.curve);
    }

    for (i = 0; i < 8; i++) {
        char lbl[64];
        int by = PLOT_Y + 4 + i * 46;
        snprintf(lbl, sizeof(lbl), "CH%d  %s", i + 1, CH_ROLE[i]);
        draw_button(PAD, by, 166, 40, lbl, i == A.cur_ch, 0);
    }

    frect(SIDE_X, PLOT_Y - 6, SIDE_W, PLOT_H + 12, A.panel);
    dtext(SIDE_X + 14, PLOT_Y + 14, A.text, "Crossover");

    snprintf(buf, sizeof(buf), "HPF %s", A.hpf_on ? "ON" : "OFF");
    draw_button(SIDE_X + 14, PLOT_Y + 26, 78, 26, buf, A.hpf_on, 0);
    draw_button(SIDE_X + 98, PLOT_Y + 26, 40, 26, "-", 0, 0);
    snprintf(buf, sizeof(buf), "%6.0f Hz", A.hpf_f);
    draw_button(SIDE_X + 144, PLOT_Y + 26, 140, 26, buf, 0, 0);
    draw_button(SIDE_X + 144, PLOT_Y + 58, 40, 26, "-", 0, 0);
    draw_button(SIDE_X + 190, PLOT_Y + 58, 40, 26, "+", 0, 0);
    {
        int si = 0;
        while (si < NSLOPE - 1 && SLOPES[si] < A.slope)
            si++;
        draw_button(SIDE_X + 236, PLOT_Y + 58, 48, 26, SLOPE_NAME[si], 0, 0);
    }

    snprintf(buf, sizeof(buf), "LPF %s", A.lpf_on ? "ON" : "OFF");
    draw_button(SIDE_X + 14, PLOT_Y + 96, 78, 26, buf, A.lpf_on, 0);
    draw_button(SIDE_X + 98, PLOT_Y + 96, 40, 26, "-", 0, 0);
    snprintf(buf, sizeof(buf), "%4.0f Hz", A.lpf_f);
    draw_button(SIDE_X + 144, PLOT_Y + 96, 140, 26, buf, 0, 0);
    draw_button(SIDE_X + 144, PLOT_Y + 128, 40, 26, "-", 0, 0);
    draw_button(SIDE_X + 190, PLOT_Y + 128, 40, 26, "+", 0, 0);

    dtext(SIDE_X + 14, PLOT_Y + 182, A.text, "Time alignment");
    draw_button(SIDE_X + 14, PLOT_Y + 194, 40, 26, "-", 0, 0);
    snprintf(buf, sizeof(buf), "%5.1f ms", A.delay_ms);
    draw_button(SIDE_X + 60, PLOT_Y + 194, 100, 26, buf, 0, 0);
    draw_button(SIDE_X + 166, PLOT_Y + 194, 40, 26, "+", 0, 0);
    snprintf(buf, sizeof(buf), "Polarity %s", A.pol ? "-1" : "+1");
    draw_button(SIDE_X + 14, PLOT_Y + 228, 140, 26, buf, A.pol, 0);

    dtext(SIDE_X + 14, PLOT_Y + 288, A.text, "Output gain");
    {
        zp_cascade c2;
        zp_design_cascade(&A.design, A.cur_ch, &c2);
        snprintf(buf, sizeof(buf), "%+.2f dB", c2.gain_db);
        draw_button(SIDE_X + 14, PLOT_Y + 300, 140, 26, buf, 0, 0);
    }
    draw_button(SIDE_X + 14, PLOT_Y + 340, 140, 26, "Reset channel", 0, 0);

    frect(PLOT_X - 6, 540, PLOT_W + 12, 96, A.panel);
    dtext(PLOT_X + 4, 560, A.dim, "Band readout  (drag any handle in the plot)");
    for (k = 0; k < 8; k++) {
        static const int pick[8] = {0, 5, 10, 14, 17, 20, 25, 30};
        int b = pick[k];
        int is_hover = (b == A.hover_band);
        snprintf(buf, sizeof(buf), "%s%5.0f Hz  %+5.1f dB", is_hover ? ">" : " ",
                 zp_band_freq(b), A.design.ch[A.cur_ch].gain_db[b]);
        dtext(PLOT_X + 4 + (k % 4) * 172, 586 + (k / 4) * 22,
              is_hover ? A.curve : A.text, buf);
    }

    frect(0, WIN_H - 34, WIN_W, 34, A.panel);
    draw_button(PAD, WIN_H - 30, 130, 26, "Connect", A.connected, 1);
    draw_button(PAD + 140, WIN_H - 30, 130, 26, "Read ids", 0, 0);
    draw_button(PAD + 280, WIN_H - 30, 116, 26, A.ab ? "A (flat)" : "B (eq)", A.ab, 0);
    dtext(PAD + 410, WIN_H - 12, A.dim,
          "drag a handle  |  1-8 channel  |  up/down nudge");
    dtext(880, WIN_H - 12, A.good, A.nlog ? A.log[0] : "");

    XSetWindowBackgroundPixmap(A.dpy, A.win, A.buf);
    XClearWindow(A.dpy, A.win);
    XFlush(A.dpy);
}

static void apply_to_model(void)
{
    zp_channel_cfg *c = &A.design.ch[A.cur_ch];
    int i;
    c->hp_enabled = A.hpf_on;
    c->hp_freq = A.hpf_f;
    c->hp_order = A.slope;
    c->lp_enabled = A.lpf_on;
    c->lp_freq = A.lpf_f;
    c->lp_order = A.slope;
    c->delay_ms = A.delay_ms;
    c->polarity = A.pol;
    for (i = 0; i < ZP_BANDS; i++)
        c->gain_db[i] = 0;
    A.design.ch[A.cur_ch].q = 1.45;
}

static void sync_model(void)
{
    apply_to_model();
}

static void do_connect(void)
{
    char path[256];
    if (A.connected) {
        zp_close(&A.conn);
        A.connected = 0;
        logline("disconnected");
        return;
    }
    if (zp_find_hidraw(path, sizeof(path)) != ZP_OK) {
        logline("device not found (VID %04x PID %04x)", ZP_USB_VID, ZP_USB_PID);
        return;
    }
    if (zp_open(&A.conn, path) != ZP_OK) {
        logline("open %s failed - needs the udev rule, or run as root", path);
        return;
    }
    {
        uint8_t req[2] = {0, 0}, rep[64];
        size_t rl = 0;
        A.conn.timeout_ms = 400;
        if (zp_xfer(&A.conn, req, 2, ZP_CMD_ID, NULL, rep, sizeof(rep), &rl) ==
            ZP_OK) {
            A.connected = 1;
            logline("connected: 0x06 replied %zu byte(s)", rl);
        } else {
            zp_close(&A.conn);
            logline("opened but no reply to 0x06");
        }
    }
}

static void do_readids(void)
{
    char path[256];
    zp_conn c;
    uint8_t rep[64 * 4];
    size_t rl = 0, k;
    uint16_t ids[16];
    int found = 0;

    if (!A.connected && zp_find_hidraw(path, sizeof(path)) != ZP_OK) {
        logline("device not found");
        return;
    }
    if (!A.connected) {
        if (zp_open(&c, path) != ZP_OK) {
            logline("open failed - needs the udev rule");
            return;
        }
    } else {
        c = A.conn;
    }
    c.timeout_ms = 300;
    for (k = 0; k < 16; k++)
        ids[k] = (uint16_t)k;
    if (zp_id_query(&c, ids, 16, rep, sizeof(rep), &rl) != ZP_OK) {
        logline("id query failed");
        if (!A.connected)
            zp_close(&c);
        return;
    }
    for (k = 0; k * 4 + 3 < rl; k++) {
        uint16_t val = (uint16_t)(rep[k * 4 + 2] | (rep[k * 4 + 3] << 8));
        if (val) {
            char b[80];
            snprintf(b, sizeof(b), "id 0x%04zux = 0x%04x", k * 2, val);
            if (found < 5)
                logline("%s", b);
            found++;
        }
    }
    if (!found)
        logline("ids 0x0000-0x001f all zero");
    if (!A.connected)
        zp_close(&c);
}

static void nudge_band(int b, double delta)
{
    double *g = &A.design.ch[A.cur_ch].gain_db[b];
    *g += delta;
    if (*g > 12)
        *g = 12;
    if (*g < -12)
        *g = -12;
    A.design.ch[A.cur_ch].gain_db[b] = *g;
    {
        zp_channel_cfg *c = &A.design.ch[A.cur_ch];
        c->gain_db[b] = *g;
    }
}

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    XImage *im = XGetImage(A.dpy, A.buf, 0, 0, WIN_W, WIN_H, AllPlanes, ZPixmap);
    int x, y;
    if (!f || !im) {
        if (f)
            fclose(f);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", WIN_W, WIN_H);
    for (y = 0; y < WIN_H; y++) {
        for (x = 0; x < WIN_W; x++) {
            unsigned long p = XGetPixel(im, x, y);
            int r = im->red_mask ? (int)((p & im->red_mask) * 255 / im->red_mask)
                                 : (int)((p >> 16) & 0xFF);
            int g = im->green_mask
                        ? (int)((p & im->green_mask) * 255 / im->green_mask)
                        : (int)((p >> 8) & 0xFF);
            int b = im->blue_mask
                        ? (int)((p & im->blue_mask) * 255 / im->blue_mask)
                        : (int)(p & 0xFF);
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        }
    }
    im->f.destroy_image = NULL;
    fclose(f);
}

int main(int argc, char **argv)
{
    int scr;
    int selftest_ms = 0;
    const char *shot_path = NULL;
    XEvent ev;
    XSetWindowAttributes wa;
    XGCValues gcv;
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0 && argc > 2)
        selftest_ms = atoi(argv[2]);
    for (int ai = 1; ai < argc - 1; ai++)
        if (!strcmp(argv[ai], "--shot"))
            shot_path = argv[ai + 1];

    memset(&A, 0, sizeof(A));
    A.dpy = XOpenDisplay(NULL);
    if (!A.dpy) {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }
    scr = DefaultScreen(A.dpy);
    A.depth = DefaultDepth(A.dpy, scr);

    A.bg = 0x1e1e22;
    A.panel = 0x26262c;
    A.panel2 = 0x33333b;
    A.text = 0xe8e8ea;
    A.dim = 0x8a8a94;
    A.grid = 0x3a3a42;
    A.curve = 0x4ade80;
    A.xover = 0xf59e0b;
    A.warn = 0xef4444;
    A.good = 0x4ade80;
    A.accent = 0x4a9eff;
    A.sel = 0x3d5a80;

    A.buf = XCreatePixmap(A.dpy, RootWindow(A.dpy, scr), WIN_W, WIN_H,
                          (unsigned)A.depth);
    if (!A.buf) {
        fprintf(stderr, "cannot allocate backbuffer\n");
        return 1;
    }

    A.win = XCreateSimpleWindow(A.dpy, RootWindow(A.dpy, DefaultScreen(A.dpy)),
                                100, 60, WIN_W, WIN_H, 1, A.bg, A.bg);
    wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                    PointerMotionMask | KeyPressMask | StructureNotifyMask;
    wa.background_pixmap = A.buf;
    wa.background_pixel = A.bg;
    wa.border_pixel = 0x4a9eff;
    wa.override_redirect = True;
    XChangeWindowAttributes(A.dpy, A.win,
                            CWEventMask | CWBackPixmap | CWBackPixel |
                                CWBorderPixel | CWOverrideRedirect,
                            &wa);
    XStoreName(A.dpy, A.win, "ZP 8.4 AMP - DSP Editor");
    XSelectInput(A.dpy, A.win, wa.event_mask);

    gcv.graphics_exposures = False;
    A.gc = XCreateGC(A.dpy, A.win, GCGraphicsExposures, &gcv);
    A.font = XLoadFont(A.dpy, "fixed");
    A.font_struct = XLoadQueryFont(A.dpy, "fixed");
    if (!A.font_struct) {
        fprintf(stderr, "cannot load the core 'fixed' font\n");
        return 1;
    }

    A.cur_ch = 0;
    A.hpf_on = 1;
    A.lpf_on = 1;
    A.hpf_f = 80;
    A.lpf_f = 8000;
    A.slope = 24;
    A.delay_ms = 0;
    A.pol = 0;
    A.conn.fd = -1;
    zp_design_defaults(&A.design, 48000.0);
    for (int i = 0; i < 8; i++)
        A.design.ch[i].gain_db[0] = 0;
    sync_model();
    logline("ready - click Connect to talk to the amp");

    XMapWindow(A.dpy, A.win);
    XFlush(A.dpy);
    draw();

    if (selftest_ms > 0) {
        struct timespec ts;
        ts.tv_sec = selftest_ms / 1000;
        ts.tv_nsec = (long)(selftest_ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
        draw();
        XFlush(A.dpy);
        if (shot_path)
            dump_ppm(shot_path);
        XCloseDisplay(A.dpy);
        printf("selftest ok%s\n", shot_path ? " (wrote shot)" : "");
        return 0;
    }

    for (;;) {
        XNextEvent(A.dpy, &ev);
        switch (ev.type) {
        case Expose:
            draw();
            break;
        case ConfigureNotify:
        case MapNotify:
            draw();
            break;
        case MotionNotify: {
            int b = band_at(ev.xmotion.x, ev.xmotion.y);
            if (A.dragging >= 0 && b == A.dragging) {
                double db = db_of_y(ev.xmotion.y);
                A.design.ch[A.cur_ch].gain_db[b] = db;
                A.design.ch[A.cur_ch].gain_db[b] = db;
            } else if (b != A.hover_band) {
                A.hover_band = b;
                draw();
            }
            break;
        }
        case ButtonPress: {
            int mx = ev.xbutton.x, my = ev.xbutton.y, i;
            if (hit(SIDE_X + 14, PLOT_Y + 26, 78, 26, mx, my))
                A.hpf_on = !A.hpf_on;
            else if (hit(SIDE_X + 144, PLOT_Y + 58, 40, 26, mx, my))
                A.hpf_f = A.hpf_f > 20 ? A.hpf_f / 1.25 : 20;
            else if (hit(SIDE_X + 190, PLOT_Y + 58, 40, 26, mx, my))
                A.hpf_f = A.hpf_f < 20000 ? A.hpf_f * 1.25 : 20000;
            else if (hit(SIDE_X + 236, PLOT_Y + 58, 48, 26, mx, my))
                A.slope = SLOPES[(A.slope == 12) ? 1 : (A.slope == 24 ? 2 : 0)];
            else if (hit(SIDE_X + 14, PLOT_Y + 96, 78, 26, mx, my))
                A.lpf_on = !A.lpf_on;
            else if (hit(SIDE_X + 144, PLOT_Y + 128, 40, 26, mx, my))
                A.lpf_f = A.lpf_f > 20 ? A.lpf_f / 1.25 : 20;
            else if (hit(SIDE_X + 190, PLOT_Y + 128, 40, 26, mx, my))
                A.lpf_f = A.lpf_f < 20000 ? A.lpf_f * 1.25 : 20000;
            else if (hit(SIDE_X + 14, PLOT_Y + 194, 40, 26, mx, my))
                A.delay_ms -= 0.5;
            else if (hit(SIDE_X + 166, PLOT_Y + 194, 40, 26, mx, my))
                A.delay_ms += 0.5;
            else if (hit(SIDE_X + 14, PLOT_Y + 228, 140, 26, mx, my))
                A.pol = !A.pol;
            else if (hit(SIDE_X + 14, PLOT_Y + 340, 140, 26, mx, my)) {
                zp_channel_defaults(&A.design.ch[A.cur_ch]);
                logline("channel %d reset", A.cur_ch + 1);
            } else if (hit(PAD, WIN_H - 33, 130, 26, mx, my))
                do_connect();
            else if (hit(PAD + 140, WIN_H - 33, 130, 26, mx, my))
                do_readids();
            else if (hit(PAD + 280, WIN_H - 33, 110, 26, mx, my)) {
                A.ab = !A.ab;
                logline("bypass %s", A.ab ? "A (flat)" : "B (processed)");
            } else {
                for (i = 0; i < 8; i++)
                    if (hit(PAD, PLOT_Y + 4 + i * 46, 166, 40, mx, my)) {
                        A.cur_ch = i;
                        logline("channel %d (%s)", i + 1, CH_ROLE[i]);
                    }
                A.dragging = band_at(mx, my);
                if (A.dragging >= 0)
                    A.hover_band = A.dragging;
            }
            if (A.delay_ms < 0)
                A.delay_ms = 0;
            if (A.delay_ms > 50)
                A.delay_ms = 50;
            sync_model();
            draw();
            break;
        }
        case ButtonRelease:
            if (A.dragging >= 0) {
                logline("band %d (%.1f Hz) set to %+.1f dB", A.dragging,
                        zp_band_freq(A.dragging),
                        A.design.ch[A.cur_ch].gain_db[A.dragging]);
                A.dragging = -1;
                draw();
            }
            break;
        case KeyPress: {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks >= XK_1 && ks <= XK_8) {
                A.cur_ch = (int)(ks - XK_1);
                logline("channel %d (%s)", A.cur_ch + 1, CH_ROLE[A.cur_ch]);
            } else if (ks == XK_Up || ks == XK_Down) {
                int b = A.hover_band >= 0 ? A.hover_band : 0;
                nudge_band(b, ks == XK_Up ? 0.5 : -0.5);
            } else if (ks == XK_Escape) {
                goto done;
            }
            sync_model();
            draw();
            break;
        }
        }
    }
done:
    if (A.connected)
        zp_close(&A.conn);
    XFreePixmap(A.dpy, A.buf);
    XCloseDisplay(A.dpy);
    return 0;
}
