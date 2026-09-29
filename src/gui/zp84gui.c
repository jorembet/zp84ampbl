#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

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
#define DSP_PAGE_SIZE 32
#define DSP_READ_BATCH 8

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
    XImage *car_image;
    int car_image_attempted;
    XImage *logo_image;
    int logo_image_attempted;
    Font font;
    XFontStruct *font_struct;
    XFontStruct *font_small, *font_heading, *font_logo, *active_font;
    int depth;
    int width, height;
    int window_width, window_height, offset_x, offset_y;
    int fullscreen, maximized;
    double ui_scale;
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
    uint16_t dsp_values[ZP_PARAM_COUNT];
    uint16_t dsp_pending[ZP_PARAM_COUNT];
    int dsp_view, dsp_page, dsp_reading, dsp_next, dsp_valid;
    int delay_unit, console_band, console_modal;
    unsigned int overlay_mask;
    char console_notice[160];
    char dsp_status[160];
    char dsp_time[32];
    char log[6][160];
    int nlog;
    int ab;
    int need_draw;
} app;

static app A;

static int x_error(Display *d, XErrorEvent *e)
{
    char buf[160];
    XGetErrorText(d, e->error_code, buf, sizeof(buf));
    fprintf(stderr, "X error %d (%s) request %d.%d\n", e->error_code, buf,
            e->request_code, e->minor_code);
    return 0;
}

static void draw(void);

static int g_quit;

static const char *CH_ROLE[8] = {"FL-Tweeter", "FR-Tweeter", "FL-Woofer", "FR-Woofer",
                                 "FL-Midrange", "FR-Midrange", "L-Subwoofer", "R-Subwoofer"};

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

static int ui_px(int logical)
{
    return (int)lround(logical * (A.ui_scale > 0 ? A.ui_scale : 1));
}

static XFontStruct *load_ui_font(int size, int bold)
{
    char name[160];
    snprintf(name, sizeof(name), "-adobe-helvetica-%s-r-normal--%d-*-*-*-*-*-iso8859-1",
             bold ? "bold" : "medium", ui_px(size));
    return XLoadQueryFont(A.dpy, name);
}

static void frect(int x, int y, int w, int h, unsigned long c)
{
    XSetForeground(A.dpy, A.gc, c);
    XFillRectangle(A.dpy, A.buf, A.gc, ui_px(x), ui_px(y), (unsigned)ui_px(w), (unsigned)ui_px(h));
}

static void fframe(int x, int y, int w, int h, unsigned long c)
{
    XSetForeground(A.dpy, A.gc, c);
    XDrawRectangle(A.dpy, A.buf, A.gc, ui_px(x), ui_px(y), (unsigned)(ui_px(w) - 1), (unsigned)(ui_px(h) - 1));
}

static void line(int x0, int y0, int x1, int y1, unsigned long c)
{
    XSetForeground(A.dpy, A.gc, c);
    XDrawLine(A.dpy, A.buf, A.gc, ui_px(x0), ui_px(y0), ui_px(x1), ui_px(y1));
}

static void dashed_v(int x, int y0, int y1, unsigned long c)
{
    static char dash[2] = {2, 4};
    XSetForeground(A.dpy, A.gc, c);
    XSetLineAttributes(A.dpy, A.gc, 1, LineOnOffDash, CapButt, JoinMiter);
    XSetDashes(A.dpy, A.gc, 0, dash, 2);
    XDrawLine(A.dpy, A.buf, A.gc, ui_px(x), ui_px(y0), ui_px(x), ui_px(y1));
    XSetLineAttributes(A.dpy, A.gc, 1, LineSolid, CapButt, JoinMiter);
}

static void dtext(int x, int y, unsigned long c, const char *s)
{
    XSetForeground(A.dpy, A.gc, c);
    XSetFont(A.dpy, A.gc, (A.active_font ? A.active_font : A.font_struct)->fid);
    XDrawString(A.dpy, A.buf, A.gc, ui_px(x), ui_px(y), s, (int)strlen(s));
}

static int text_width(const char *s)
{
    return (int)ceil(XTextWidth(A.active_font ? A.active_font : A.font_struct, s, (int)strlen(s)) / (A.ui_scale > 0 ? A.ui_scale : 1));
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

static void window_state_refresh(void)
{
    Atom actual;int format;unsigned long count,remaining;unsigned char *data=NULL;
    A.fullscreen=0;A.maximized=0;
    Atom full=XInternAtom(A.dpy,"_NET_WM_STATE_FULLSCREEN",False);
    Atom vert=XInternAtom(A.dpy,"_NET_WM_STATE_MAXIMIZED_VERT",False);
    Atom horz=XInternAtom(A.dpy,"_NET_WM_STATE_MAXIMIZED_HORZ",False);
    if(XGetWindowProperty(A.dpy,A.win,XInternAtom(A.dpy,"_NET_WM_STATE",False),0,64,False,XA_ATOM,
                          &actual,&format,&count,&remaining,&data)==Success&&actual==XA_ATOM&&format==32) {
        int v=0,h=0;Atom *states=(Atom*)data;
        for(unsigned long i=0;i<count;i++){if(states[i]==full)A.fullscreen=1;if(states[i]==vert)v=1;if(states[i]==horz)h=1;}
        A.maximized=v&&h;
    }
    if(data)XFree(data);
    A.need_draw=1;
}
static void window_toggle(int fullscreen)
{
    XEvent event={0};event.xclient.type=ClientMessage;event.xclient.window=A.win;
    event.xclient.message_type=XInternAtom(A.dpy,"_NET_WM_STATE",False);event.xclient.format=32;
    event.xclient.data.l[0]=2; /* EWMH toggle; the window manager owns restore geometry. */
    event.xclient.data.l[1]=XInternAtom(A.dpy,fullscreen?"_NET_WM_STATE_FULLSCREEN":"_NET_WM_STATE_MAXIMIZED_VERT",False);
    event.xclient.data.l[2]=fullscreen?0:XInternAtom(A.dpy,"_NET_WM_STATE_MAXIMIZED_HORZ",False);
    event.xclient.data.l[3]=1;
    XSendEvent(A.dpy,DefaultRootWindow(A.dpy),False,SubstructureRedirectMask|SubstructureNotifyMask,&event);
    XFlush(A.dpy);
}

static void present(int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
    XCopyArea(A.dpy, A.buf, A.win, A.gc, 0, 0,
              (unsigned)A.width, (unsigned)A.height, A.offset_x, A.offset_y);
}

static void flush_draw(void)
{
    if (!A.need_draw)
        return;
    A.need_draw = 0;
    draw();
}

/* USB register mapping recovered from vendor PC table at 0x667080.
   Frequency conversion: 0x41d256; gain: 0x41d2ee; Q: 0x41d2a7.
   These values describe a snapshot, never a hardware write operation. */
static double dsp_frequency(uint16_t value)
{
    return value & 0x8000 ? (value & 0x7fff) / 10.0 : value;
}

/* Vendor Android d/o.k + g/a.f: display scale, not acoustic dB SPL. */
static int dsp_channel_level(uint16_t raw)
{
    int level = (raw >= 5000 ? raw - 5000 : raw) / 10;
    return level > 40 ? level - 40 : level;
}

static double dsp_delay_ms(uint16_t raw)
{
    return round(round(raw * 48.0 / 1000) * 1000 / 48) / 1000;
}

static void do_connect(void);
static void do_readids(void);

#define ZP_CONFIG_DIR ".config/zp84-dsp"
#define ZP_FIRST_RUN_MARKER "first-run"

static int first_run_pending(void)
{
    char path[512];
    const char *home = getenv("HOME");
    if (!home)
        return 0;
    snprintf(path, sizeof(path), "%s/%s/%s", home, ZP_CONFIG_DIR,
             ZP_FIRST_RUN_MARKER);
    return access(path, F_OK) != 0;
}

static void first_run_mark(void)
{
    char path[512];
    const char *home = getenv("HOME");
    FILE *f;
    if (!home)
        return;
    snprintf(path, sizeof(path), "%s/%s", home, ZP_CONFIG_DIR);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/%s/%s", home, ZP_CONFIG_DIR,
             ZP_FIRST_RUN_MARKER);
    f = fopen(path, "w");
    if (f)
        fclose(f);
}

static int device_lock = -1;

static int lock_device(void)
{
    char path[512];
    const char *home = getenv("HOME");
    int fd;
    if (!home)
        return 0;
    snprintf(path, sizeof(path), "%s/%s", home, ZP_CONFIG_DIR);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/%s/device.lock", home, ZP_CONFIG_DIR);
    fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
        return 0;
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;
    }
    device_lock = fd;
    return 1;
}

static void unlock_device(void)
{
    if (device_lock >= 0) {
        flock(device_lock, LOCK_UN);
        close(device_lock);
        device_lock = -1;
    }
}

#include "console.h"

static void draw(void)
{
    int i, k;
    char buf[64];
    zp_cascade casc;
    A.active_font = A.font_struct;
    if (A.dsp_view == 2) {
        draw_console();
        present(0, 0, WIN_W, WIN_H);
        XFlush(A.dpy);
        return;
    }

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

    draw_button(PAD, 44, 166, 28, "Panel DSP", A.dsp_view == 2, 0);
    draw_button(190, 44, 166, 28, "Register USB", A.dsp_view == 1, 0);
    draw_button(368, 44, 130, 28, "Simulasi lokal", A.dsp_view == 0, 0);
    if (A.dsp_view == 1) {
        char row[160];
        frect(PAD, 86, WIN_W - 2 * PAD, 658, A.panel);
        dtext(28, 110, A.text, "Data langsung dari amplifier - register mentah, bukan nilai EQ/delay");
        dtext(28, 134, A.dsp_reading ? A.xover : A.text, A.dsp_status);
        snprintf(row, sizeof(row), "Snapshot terakhir: %s | %s",
                 A.dsp_valid ? A.dsp_time : "belum ada",
                 A.connected ? "USB terhubung" : "USB terputus; data tersimpan mungkin sudah berubah");
        dtext(28, 158, A.dim, row);
        for (int col = 0; col < 2; col++) {
            int x = 28 + col * 606;
            dtext(x, 198, A.dim, "ID register       HEX       Desimal (unsigned 16-bit)");
            for (int r = 0; r < DSP_PAGE_SIZE / 2; r++) {
                int id = A.dsp_page * DSP_PAGE_SIZE + col * (DSP_PAGE_SIZE / 2) + r;
                if (id >= ZP_PARAM_COUNT)
                    break;
                if (A.dsp_valid)
                    snprintf(row, sizeof(row), "0x%04X            0x%04X    %5u", id,
                             A.dsp_values[id], A.dsp_values[id]);
                else
                    snprintf(row, sizeof(row), "0x%04X            ----      belum dibaca", id);
                dtext(x, 226 + r * 26, A.text, row);
            }
        }
        draw_button(28, 674, 150, 28, "< Sebelumnya", 0, 0);
        draw_button(190, 674, 150, 28, "Berikutnya >", 0, 0);
        snprintf(row, sizeof(row), "Halaman %d/%d | PgUp/PgDn | R: baca ulang",
                 A.dsp_page + 1, (ZP_PARAM_COUNT + DSP_PAGE_SIZE - 1) / DSP_PAGE_SIZE);
        dtext(365, 693, A.dim, row);
        dtext(28, 730, A.dim, "Nilai nol tetap ditampilkan. Editor lokal belum disinkronkan dengan parameter ini.");
        goto footer;
    }
    dtext(520, 63, A.dim, "Simulasi lokal - tidak dikirim ke DSP");
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

footer:
    frect(0, WIN_H - 34, WIN_W, 34, A.panel);
    draw_button(PAD, WIN_H - 30, 130, 26, A.connected ? "Disconnect" : "Connect", A.connected, 1);
    draw_button(PAD + 140, WIN_H - 30, 130, 26, A.dsp_reading ? "Membaca..." : "Baca DSP", A.dsp_reading, 0);
    if (!A.dsp_view)
        draw_button(PAD + 280, WIN_H - 30, 116, 26, A.ab ? "A (flat)" : "B (eq)", A.ab, 0);
    dtext(PAD + 410, WIN_H - 12, A.dim,
          A.dsp_view ? "Baca DSP: perbarui snapshot USB" : "drag a handle  |  1-8 channel  |  up/down nudge");
    dtext(760, WIN_H - 12, A.dim, A.dsp_view ? (A.dsp_reading ? "Membaca DSP..." : (A.connected ? "USB terhubung / snapshot" : "USB terputus / data mungkin usang")) : "Simulasi lokal");

    present(0, 0, WIN_W, WIN_H);
    XFlush(A.dpy);
}

static void apply_to_model(void)
{
    zp_channel_cfg *c = &A.design.ch[A.cur_ch];
    c->hp_enabled = A.hpf_on;
    c->hp_freq = A.hpf_f;
    c->hp_order = A.slope;
    c->lp_enabled = A.lpf_on;
    c->lp_freq = A.lpf_f;
    c->lp_order = A.slope;
    c->delay_ms = A.delay_ms;
    c->polarity = A.pol;
    /* gain_db stays owned by the plot/drag handlers: clearing it here
       would wipe every EQ edit on the next button/key press. */
}

static void sync_model(void)
{
    apply_to_model();
}

static void do_connect(void)
{
    fader_kind=0;
    memset(eq_undo_valid,0,sizeof(eq_undo_valid));
    char path[256];
    if (A.connected) {
        zp_close(&A.conn);
        A.connected = 0;
        unlock_device();
        if (A.dsp_reading)
            snprintf(A.dsp_status, sizeof(A.dsp_status), "Pembacaan dibatalkan; snapshot terakhir dipertahankan.");
        A.dsp_reading = 0;
        logline("disconnected");
        return;
    }
    if (zp_find_hidraw(path, sizeof(path)) != ZP_OK) {
        snprintf(A.console_notice,sizeof(A.console_notice),"USB tidak ditemukan. Sambungkan amplifier lalu Connect.");
        logline("device not found (VID %04x PID %04x)", ZP_USB_VID, ZP_USB_PID);
        return;
    }
    {
        int lk = lock_device();
        if (lk < 0) {
            snprintf(A.console_notice,sizeof(A.console_notice),"Perangkat sedang dipakai aplikasi lain (desktop/web). Tutup aplikasi tersebut lalu coba lagi.");
            logline("device busy: another app holds the lock");
            return;
        }
    }
    if (zp_open(&A.conn, path) != ZP_OK) {
        int e = A.conn.last_errno;
        snprintf(A.console_notice,sizeof(A.console_notice),"USB gagal dibuka: %s",e?strerror(e):"periksa koneksi");
        logline("open %s failed: %s", path,
                e ? strerror(e) : "unknown error");
        logline(e == EACCES ? "no access: udev rule/plugdev, or run as root"
                            : "device gone? unplug, replug, Connect again");
        unlock_device();
        return;
    }
    {
        uint8_t req[2] = {0, 0}, rep[64];
        size_t rl = 0;
        A.conn.timeout_ms = 400;
        if (zp_xfer(&A.conn, req, 2, ZP_CMD_ID, NULL, rep, sizeof(rep), &rl) ==
            ZP_OK) {
            A.connected = 1;
            A.dsp_valid=0;
            snprintf(A.console_notice,sizeof(A.console_notice),"USB terhubung. Klik Baca DSP untuk mengaktifkan kontrol.");
            logline("connected: 0x06 replied %zu byte(s)", rl);
        } else {
            zp_close(&A.conn);
            unlock_device();
            snprintf(A.console_notice,sizeof(A.console_notice),"USB tidak merespons. Periksa amplifier lalu Connect kembali.");
            logline("opened but no reply to 0x06");
        }
    }
}

static void do_readids(void)
{
    if (!A.dsp_view) A.dsp_view = 2;
    A.dragging = -1;
    fader_kind=0;
    if (A.dsp_reading)
        return;
    if (!A.connected)
        do_connect();
    if (!A.connected) {
        snprintf(A.dsp_status, sizeof(A.dsp_status), "Tidak bisa terhubung. %.137s",
                 A.nlog ? A.log[A.nlog - 1] : "Periksa USB.");
        return;
    }
    A.dsp_next = 0;
    A.dsp_reading = 1;
    snprintf(A.dsp_status, sizeof(A.dsp_status), "Membaca 0/%d parameter...", ZP_PARAM_COUNT);
}

/* One bounded USB transaction per event-loop iteration keeps navigation and
   disconnect usable. Commit a snapshot only when every record is validated. */
static void read_dsp_batch(void)
{
    uint16_t ids[DSP_READ_BATCH];
    uint8_t rep[DSP_READ_BATCH * ZP_ID_RECORD_BYTES];
    size_t len = 0;
    int count = ZP_PARAM_COUNT - A.dsp_next;
    if (count > DSP_READ_BATCH)
        count = DSP_READ_BATCH;
    for (int i = 0; i < count; i++)
        ids[i] = (uint16_t)(A.dsp_next + i);
    int result = zp_id_query(&A.conn, ids, (size_t)count, rep, sizeof(rep), &len);
    if (result == ZP_OK && len != (size_t)count * ZP_ID_RECORD_BYTES)
        result = ZP_ERR_SHORT;
    if (result == ZP_OK) {
        for (int i = 0; i < count; i++) {
            uint16_t id = (uint16_t)((rep[i * 4] << 8) | rep[i * 4 + 1]);
            if (id != ids[i]) {
                result = ZP_ERR_SYNC;
                break;
            }
            A.dsp_pending[id] = (uint16_t)((rep[i * 4 + 2] << 8) | rep[i * 4 + 3]);
        }
    }
    if (result != ZP_OK) {
        snprintf(A.dsp_status, sizeof(A.dsp_status),
                 "Gagal pada ID 0x%04X: %s. Snapshot terakhir dipertahankan.",
                 A.dsp_next, zp_strerror((uint32_t)result));
        A.dsp_reading = 0;
        zp_close(&A.conn);
        A.connected = 0;
        unlock_device();
    } else {
        A.dsp_next += count;
        snprintf(A.dsp_status, sizeof(A.dsp_status), "Membaca %d/%d parameter...",
                 A.dsp_next, ZP_PARAM_COUNT);
        if (A.dsp_next == ZP_PARAM_COUNT) {
            time_t now = time(NULL);
            struct tm local;
            memcpy(A.dsp_values, A.dsp_pending, sizeof(A.dsp_values));
            A.dsp_valid = 1;
            A.dsp_reading = 0;
            if (localtime_r(&now, &local))
                strftime(A.dsp_time, sizeof(A.dsp_time), "%Y-%m-%d %H:%M:%S", &local);
            snprintf(A.dsp_status, sizeof(A.dsp_status),
                     "Berhasil membaca %d/%d parameter dari USB.", ZP_PARAM_COUNT, ZP_PARAM_COUNT);
        }
    }
    A.need_draw = 1;
}

static void dsp_page_move(int delta)
{
    A.dsp_page += delta;
    if (A.dsp_page < 0)
        A.dsp_page = 0;
    if (A.dsp_page > (ZP_PARAM_COUNT - 1) / DSP_PAGE_SIZE)
        A.dsp_page = (ZP_PARAM_COUNT - 1) / DSP_PAGE_SIZE;
    A.need_draw = 1;
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
    XImage *im = XGetImage(A.dpy, A.buf, 0, 0, (unsigned)A.width, (unsigned)A.height, AllPlanes, ZPixmap);
    int x, y;
    if (!f || !im) {
        if (f)
            fclose(f);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", A.width, A.height);
    for (y = 0; y < A.height; y++) {
        for (x = 0; x < A.width; x++) {
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
    XDestroyImage(im);
    fclose(f);
}

static int selftest_spot_check(void)
{
    /* Sample the backbuffer, not the window: under Xvfb/forwarding the
       window may not be mapped yet when the selftest runs, which made
       XGetImage on the window fail and the check report 0. */
    XImage *im = XGetImage(A.dpy, A.buf, 0, 0, (unsigned)A.width, (unsigned)A.height, AllPlanes, ZPixmap);
    int x, y, painted = 0;
    unsigned long bg;
    if (!im)
        return 0;
    bg = XGetPixel(im, 2, 2);
    for (y = 0; y < A.height; y += 4)
        for (x = 0; x < A.width; x += 4)
            if (XGetPixel(im, x, y) != bg)
                painted++;
    XDestroyImage(im);
    return painted;
}

static void resize_ui(int width,int height)
{
    if(width<=0||height<=0||(width==A.window_width&&height==A.window_height))return;
    A.window_width=width;A.window_height=height;
    A.ui_scale=fmin(width/(double)WIN_W,height/(double)WIN_H);
    A.width=ui_px(WIN_W);A.height=ui_px(WIN_H);
    A.offset_x=(width-A.width)/2;A.offset_y=(height-A.height)/2;
    fader_kind=0;A.dragging=-1;layout_cancel();
    XFreePixmap(A.dpy,A.buf);
    A.buf=XCreatePixmap(A.dpy,A.win,(unsigned)A.width,(unsigned)A.height,(unsigned)A.depth);
    XSetWindowBackground(A.dpy,A.win,0x101720);XClearWindow(A.dpy,A.win);
    XFontStruct *font=load_ui_font(13,0);
    if(!font)font=XLoadQueryFont(A.dpy,"fixed");
    if(font) {
        XFreeFont(A.dpy,A.font_struct);A.font_struct=font;A.font=font->fid;
        if(A.font_small)XFreeFont(A.dpy,A.font_small);
        if(A.font_heading)XFreeFont(A.dpy,A.font_heading);
        if(A.font_logo)XFreeFont(A.dpy,A.font_logo);
        A.font_small=load_ui_font(10,0);A.font_heading=load_ui_font(17,1);A.font_logo=load_ui_font(58,0);
        A.active_font=A.font_struct;
    }
    if(A.car_image)XDestroyImage(A.car_image);
    if(A.logo_image)XDestroyImage(A.logo_image);
    A.car_image=NULL;A.logo_image=NULL;A.car_image_attempted=0;A.logo_image_attempted=0;
    A.need_draw=1;
}

static void handle_event(XEvent *ev)
{
    if (A.ui_scale > 0) {
        if (ev->type == ButtonPress || ev->type == ButtonRelease) {
            ev->xbutton.x = (int)floor((ev->xbutton.x-A.offset_x) / A.ui_scale);
            ev->xbutton.y = (int)floor((ev->xbutton.y-A.offset_y) / A.ui_scale);
        } else if (ev->type == MotionNotify) {
            ev->xmotion.x = (int)floor((ev->xmotion.x-A.offset_x) / A.ui_scale);
            ev->xmotion.y = (int)floor((ev->xmotion.y-A.offset_y) / A.ui_scale);
        }
    }
    switch (ev->type) {
    case Expose:
        present(ev->xexpose.x, ev->xexpose.y, ev->xexpose.width,
                ev->xexpose.height);
        XFlush(A.dpy);
        break;
    case ConfigureNotify:
        resize_ui(ev->xconfigure.width,ev->xconfigure.height);
        break;
    case PropertyNotify:
        if(ev->xproperty.atom==XInternAtom(A.dpy,"_NET_WM_STATE",False))window_state_refresh();
        break;
    case MapNotify:
        A.need_draw = 1;
        break;
    case MotionNotify: {
        if(A.dsp_view==2&&layout.ready&&layout.dragging>=0){layout_motion(ev->xmotion.x,ev->xmotion.y);A.need_draw=1;break;}
        if(A.dsp_view==2&&fader_kind){fader_y=ev->xmotion.y;fader_x=ev->xmotion.x;A.need_draw=1;break;}
        if (A.dsp_view)
            break;
        int b = band_at(ev->xmotion.x, ev->xmotion.y);
        if (A.dragging >= 0 && b == A.dragging) {
            double db = db_of_y(ev->xmotion.y);
            A.design.ch[A.cur_ch].gain_db[b] = db;
            A.need_draw = 1;
        } else if (b != A.hover_band) {
            A.hover_band = b;
            A.need_draw = 1;
        }
        break;
    }
    case ButtonPress: {
        int mx = ev->xbutton.x, my = ev->xbutton.y, i;
        if (A.dsp_view == 2) {
            if(ev->xbutton.button==Button1)console_click(mx, my);
            A.need_draw = 1;
            break;
        }
        if (A.console_modal == 11)
            break;
        if (hit(PAD, 44, 166, 28, mx, my) || hit(190, 44, 166, 28, mx, my) || hit(368, 44, 130, 28, mx, my)) {
            A.dsp_view = mx >= 368 ? 0 : (mx >= 190 ? 1 : 2);
            A.dragging = -1;
            A.need_draw = 1;
            break;
        }
        if (A.dsp_view == 1) {
            if (hit(PAD, WIN_H - 30, 130, 26, mx, my))
                do_connect();
            else if (hit(PAD + 140, WIN_H - 30, 130, 26, mx, my))
                do_readids();
            else if (hit(28, 674, 150, 28, mx, my) || ev->xbutton.button == Button4)
                dsp_page_move(-1);
            else if (hit(190, 674, 150, 28, mx, my) || ev->xbutton.button == Button5)
                dsp_page_move(1);
            A.need_draw = 1;
            break;
        }
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
        A.need_draw = 1;
        break;
    }
    case ButtonRelease:
        if(A.dsp_view==2&&layout.ready&&layout.dragging>=0&&ev->xbutton.button==Button1){layout_release(ev->xbutton.x,ev->xbutton.y);A.need_draw=1;break;}
        if(A.dsp_view==2){if(ev->xbutton.button==Button1){fader_x=ev->xbutton.x;control_release(ev->xbutton.y);}A.need_draw=1;break;}
        if (A.dragging >= 0) {
            logline("band %d (%.1f Hz) set to %+.1f dB", A.dragging,
                    zp_band_freq(A.dragging),
                    A.design.ch[A.cur_ch].gain_db[A.dragging]);
            A.dragging = -1;
            A.need_draw = 1;
        }
        break;
    case KeyPress: {
        KeySym ks = XLookupKeysym(&ev->xkey, 0);
        if(ks==XK_F11){fader_kind=0;layout_cancel();window_toggle(1);break;}
        if(ks==XK_Escape&&A.fullscreen&&!A.console_modal&&!fader_kind&&(!layout.ready||layout.dragging<0)){window_toggle(1);break;}
        if(layout.ready&&layout.dragging>=0){if(ks==XK_Escape)layout_cancel();A.need_draw=1;break;}
        if(A.dsp_view==2&&A.console_modal==6){control_key(&ev->xkey,ks);A.need_draw=1;break;}
        if(A.dsp_view==2&&A.console_modal==11){password_key(&ev->xkey,ks);A.need_draw=1;break;}
        if(A.dsp_view==2&&ks==XK_Escape&&fader_kind){fader_kind=0;A.need_draw=1;break;}
        if (A.dsp_view == 2 && A.console_modal) {
            if(ks==XK_Escape)A.console_modal = 0;
            A.need_draw = 1;
            break;
        }
        if (A.dsp_view && ks != XK_Escape && ks != XK_q) {
            if (A.dsp_view == 2 && ks >= XK_1 && ks <= XK_8) {
                fader_kind=0;
                A.cur_ch = (int)(ks - XK_1);
                A.overlay_mask = 1u << A.cur_ch;
            }
            else if (ks == XK_Next) dsp_page_move(1);
            else if (ks == XK_Prior) dsp_page_move(-1);
            else if (ks == XK_r || ks == XK_R) do_readids();
            A.need_draw = 1;
            break;
        }
        if (ks >= XK_1 && ks <= XK_8) {
            A.cur_ch = (int)(ks - XK_1);
            logline("channel %d (%s)", A.cur_ch + 1, CH_ROLE[A.cur_ch]);
        } else if (ks == XK_Up || ks == XK_Down) {
            int b = A.hover_band >= 0 ? A.hover_band : 0;
            nudge_band(b, ks == XK_Up ? 0.5 : -0.5);
        } else if (ks == XK_Escape || ks == XK_q) {
            g_quit = 1;
            return;
        }
        sync_model();
        A.need_draw = 1;
        break;
    }
    case ClientMessage:
        if ((Atom)ev->xclient.data.l[0] ==
                XInternAtom(A.dpy, "WM_DELETE_WINDOW", False))
            g_quit = 1;
        break;
    }
}

static void pump(int wait_ms)
{
    for (;;) {
        while (XPending(A.dpy)) {
            XEvent ev;
            XNextEvent(A.dpy, &ev);
            handle_event(&ev);
            if (g_quit)
                return;
        }
        if (A.dsp_reading) {
            read_dsp_batch();
            flush_draw();
            return;
        }
        flush_draw();
        if (wait_ms <= 0)
            return;
        {
            fd_set fds;
            struct timeval tv;
            int fd = XConnectionNumber(A.dpy);
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            tv.tv_sec = wait_ms / 1000;
            tv.tv_usec = (wait_ms % 1000) * 1000;
            if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
                return;
        }
    }
}

int main(int argc, char **argv)
{
    int scr;
    int selftest_ms = 0;
    int read_on_start = 0;
    int exit_code = 0;
    const char *shot_path = NULL;
    XSetWindowAttributes wa;
    XGCValues gcv;
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0 && argc > 2)
        selftest_ms = atoi(argv[2]);
    for (int ai = 1; ai < argc - 1; ai++)
        if (!strcmp(argv[ai], "--shot"))
            shot_path = argv[ai + 1];

    for (int ai = 1; ai < argc; ai++)
        if (!strcmp(argv[ai], "--read-dsp"))
            read_on_start = 1;
    memset(&A, 0, sizeof(A));
    A.dsp_view = 2;
    A.cur_ch = 2;
    snprintf(A.dsp_status, sizeof(A.dsp_status), "Klik Baca DSP untuk mengambil data amplifier.");
    A.dpy = XOpenDisplay(NULL);
    if (!A.dpy) {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }
    XSetErrorHandler(x_error);
    scr = DefaultScreen(A.dpy);
    A.depth = DefaultDepth(A.dpy, scr);
    A.ui_scale = fmin((DisplayWidth(A.dpy, scr)-60.0)/WIN_W,
                     (DisplayHeight(A.dpy, scr)-100.0)/WIN_H);
    A.ui_scale = zp_clampd(A.ui_scale, 0.75, 1.75);
    for (int ai=1; ai<argc-1; ai++) {
        if (!strcmp(argv[ai], "--scale")) {
            char *end;
            double requested=strtod(argv[ai+1], &end);
            if (*end || !isfinite(requested) || requested<0.75 || requested>2.5) {
                fprintf(stderr, "--scale must be 0.75..2.5\n");
                XCloseDisplay(A.dpy);
                return 1;
            }
            A.ui_scale=requested;
        }
    }
    A.width=ui_px(WIN_W); A.height=ui_px(WIN_H);
    A.window_width=A.width;A.window_height=A.height;

    A.bg = 0x191a18;
    A.panel = 0x1d2936;
    A.panel2 = 0x464743;
    A.text = 0xe8e8ea;
    A.dim = 0xa5b4c4;
    A.grid = 0x3a3a42;
    A.curve = 0x4ade80;
    A.xover = 0xf59e0b;
    A.warn = 0xef4444;
    A.good = 0x4ade80;
    A.accent = 0x59d8cd;
    A.sel = 0xffbe18;

    A.buf = XCreatePixmap(A.dpy, RootWindow(A.dpy, scr), (unsigned)A.width, (unsigned)A.height,
                          (unsigned)A.depth);
    if (!A.buf) {
        fprintf(stderr, "cannot allocate backbuffer\n");
        return 1;
    }

    A.win = XCreateSimpleWindow(A.dpy, RootWindow(A.dpy, DefaultScreen(A.dpy)),
                                20, 30, (unsigned)A.width, (unsigned)A.height, 1, A.bg, A.bg);
    wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                    PointerMotionMask | KeyPressMask | StructureNotifyMask | PropertyChangeMask;
    wa.background_pixmap = A.buf;
    wa.background_pixel = A.bg;
    wa.border_pixel = 0x4a9eff;
    XChangeWindowAttributes(A.dpy, A.win,
                            CWEventMask | CWBackPixmap | CWBackPixel |
                                CWBorderPixel,
                            &wa);
    XStoreName(A.dpy, A.win, "ZP 8.4 AMP - DSP Editor");
    XSetIconName(A.dpy, A.win, "ZP 8.4 DSP");
    XSelectInput(A.dpy, A.win, wa.event_mask);

    {
        XClassHint cls;
        XSizeHints sh;
        XWMHints wh;
        Atom proto[1];
        char *title = "ZP 8.4 AMP - DSP Editor";
        char *cls_name = "zp84gui";
        char *cls_class = "Zp84Dsp";

        cls.res_name = cls_name;
        cls.res_class = cls_class;
        XSetClassHint(A.dpy, A.win, &cls);
        Xutf8SetWMProperties(A.dpy, A.win, title, "ZP 8.4 DSP", NULL, 0, NULL, 0,
                             NULL);

        memset(&sh, 0, sizeof(sh));
        sh.flags = PSize | PMinSize;
        sh.width = A.width;
        sh.height = A.height;
        sh.min_width = WIN_W*3/4;
        sh.min_height = WIN_H*3/4;
        XSetWMNormalHints(A.dpy, A.win, &sh);

        memset(&wh, 0, sizeof(wh));
        wh.flags = InputHint | StateHint;
        wh.input = True;
        wh.initial_state = NormalState;
        XSetWMHints(A.dpy, A.win, &wh);

        proto[0] = XInternAtom(A.dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(A.dpy, A.win, proto, 1);
    }

    gcv.graphics_exposures = False;
    A.gc = XCreateGC(A.dpy, A.win, GCGraphicsExposures, &gcv);
    A.font_struct = load_ui_font(13, 0);
    if (!A.font_struct) A.font_struct = XLoadQueryFont(A.dpy, "fixed");
    if (!A.font_struct) { fprintf(stderr, "cannot load UI font\n"); return 1; }
    A.font_small = load_ui_font(10, 0);
    A.font_heading = load_ui_font(17, 1);
    A.font_logo = load_ui_font(58, 0);
    A.font = A.font_struct->fid;

    A.cur_ch = 2;
    A.overlay_mask = 1u << 2;
    A.console_band = 26;
    A.dragging = -1;
    A.hover_band = -1;
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

    if (first_run_pending())
        A.console_modal = 11;

    layout_load();
    output_marks_load();
    XMapWindow(A.dpy, A.win);
    XFlush(A.dpy);
    draw();

    if (read_on_start)
        do_readids();

    if (selftest_ms > 0) {
        int painted;
        pump(selftest_ms);
        while (A.dsp_reading && !g_quit)
            pump(0);
        flush_draw();
        if (read_on_start) {
            exit_code = A.dsp_valid ? 0 : 1;
            printf("USB: %s; id 0 = 0x%04X\n", A.dsp_status, A.dsp_values[0]);
        }
        painted = selftest_spot_check();
        if (shot_path)
            dump_ppm(shot_path);
        printf("selftest ok: window shows %d painted sample(s)%s\n", painted,
               shot_path ? " (wrote shot)" : "");
        goto done;
    }

    for (;;) {
        pump(200);
        if (g_quit)
            break;
    }
done:
    if (A.connected)
        zp_close(&A.conn);
    if (A.font_small) XFreeFont(A.dpy, A.font_small);
    if (A.font_heading) XFreeFont(A.dpy, A.font_heading);
    if (A.font_logo) XFreeFont(A.dpy, A.font_logo);
    XFreeFont(A.dpy, A.font_struct);
    if (A.car_image) XDestroyImage(A.car_image);
    if (A.logo_image) XDestroyImage(A.logo_image);
    XFreePixmap(A.dpy, A.buf);
    XCloseDisplay(A.dpy);
    return exit_code;
}
