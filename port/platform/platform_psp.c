/* PSP platform layer (o32, clang): the loop and lifecycle parts of platform.h.
 * Input is P12, the renderer P21 (render() is called but draws nothing yet).
 *
 * Pacing: the PSP display refreshes at 59.94 Hz, the NAOMI's and the PS2's
 * rate, so logic steps are locked to vblanks: one step per vblank, and after
 * a frame that overran, one step per vblank that was missed (the vblank
 * counter, not wall-clock time, says how many), up to plat_set_max_steps().
 * Beyond that the game slows down rather than spiral; render frames are
 * dropped, logic frames are not.  plat_set_logic_hz() is accepted for the
 * contract but the display rate is the step rate.
 *
 * Every SDK call here takes <= 4 int/pointer args (port/docs/PSP.md).
 * Lifecycle: the HOME-menu exit and the power callbacks run on an EABI thread
 * (port/psp/host/psp_entry.c) and only set flags read here. */
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psprtc.h>
#include <string.h>
#include "platform.h"

/* port/psp/host/psp_entry.c (EABI, no args) */
int psp_exit_requested(void);
int psp_suspended(void);

static struct {
    int quit, hold, fixed_step, max_steps, frame_skip, force_render, last_step, paused;
    unsigned vcount;
    PlatStats st;
    /* per-second counters */
    unsigned sec_vcount;
    int sec_steps, sec_renders, sec_dropped;
    unsigned long long upd_us, ren_us;
    int upd_n, ren_n;
} P = {.max_steps = 8, .frame_skip = 1};

static PadRaw no_pad[PLAT_MAX_PADS];

static unsigned long long now_us(void)
{
    u64 t = 0;
    sceRtcGetCurrentTick(&t);   /* 1 MHz */
    return t;
}

int plat_init(const PlatConfig *cfg) { (void)cfg; P.vcount = sceDisplayGetVcount(); return 0; }
void plat_shutdown(void) {}
void plat_drawable_size(int *w, int *h) { *w = 480; *h = 272; }
void plat_swap(void) {}
const char *plat_gl_description(void) { return "PSP (no GL; sceGu renderer pending)"; }

void plat_request_quit(void) { P.quit = 1; }
int plat_quit_requested(void) { return P.quit || psp_exit_requested(); }
void plat_set_speed(int n) { (void)n; }
void plat_set_logic_hz(double hz) { (void)hz; }
void plat_set_fixed_step(int on) { P.fixed_step = on != 0; }
int plat_paused(void) { return P.paused; }
void plat_set_hold(int on) { P.hold = on != 0; }
int plat_hold(void) { return P.hold; }
int plat_step_is_last(void) { return P.last_step; }
void plat_set_max_steps(int n) { P.max_steps = n < 1 ? 1 : n > 8 ? 8 : n; }
void plat_set_frame_skip(int on) { P.frame_skip = on != 0; }
void plat_set_vsync(int on) { (void)on; }
void plat_set_fullscreen(int on) { (void)on; }
int plat_fullscreen(void) { return 1; }
void plat_request_render(void) { P.force_render = 1; }
void plat_stats(PlatStats *s) { *s = P.st; }

/* Controls (P12): the PSP's own pad is player 1, mapped 1:1 onto the PS2 pad
 * the game reads, so the game's BUTTON CONFIG applies as on the PS2. With its
 * default config the six attacks are SQUARE LP, TRIANGLE HP, CROSS LK,
 * CIRCLE HK, L A1, R A2 (PSP L/R = PS2 L1/R1). The analog stick is the PS2
 * left stick (used by the game when the d-pad is idle). PS2 L2/R2/L3/R3 have
 * no PSP button. Player 2 is not connected. */
static void pad_read(PadRaw *p)
{
    static const struct { unsigned psp; uint16_t ps2; } map[] = {
        { PSP_CTRL_UP, PS2_UP }, { PSP_CTRL_DOWN, PS2_DOWN },
        { PSP_CTRL_LEFT, PS2_LEFT }, { PSP_CTRL_RIGHT, PS2_RIGHT },
        { PSP_CTRL_SQUARE, PS2_SQUARE }, { PSP_CTRL_TRIANGLE, PS2_TRIANGLE },
        { PSP_CTRL_CROSS, PS2_CROSS }, { PSP_CTRL_CIRCLE, PS2_CIRCLE },
        { PSP_CTRL_LTRIGGER, PS2_L1 }, { PSP_CTRL_RTRIGGER, PS2_R1 },
        { PSP_CTRL_START, PS2_START }, { PSP_CTRL_SELECT, PS2_SELECT },
    };
    SceCtrlData d;
    uint16_t b = 0;
    if (sceCtrlPeekBufferPositive(&d, 1) <= 0)
        return;
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (d.Buttons & map[i].psp) b |= map[i].ps2;
    p->buttons = b;
    p->lx = d.Lx;
    p->ly = d.Ly;
    p->rx = p->ry = 0x80;
    p->connected = 1;
    p->analog = 1;
}

void plat_poll_input(void)
{
    static int init;
    if (!init) {
        sceCtrlSetSamplingCycle(0);
        sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
        init = 1;
    }
    pad_read(&no_pad[0]);
}
const PadRaw *plat_pad(int player) { return &no_pad[player < 0 || player >= PLAT_MAX_PADS ? 0 : player]; }
int plat_pad_connected(int slot) { return slot == 0; }

static void stats_tick(unsigned vc)
{
    unsigned n = vc - P.sec_vcount;
    if (n < 60)
        return;
    P.st.logic_fps = P.sec_steps * 59.94f / n;
    P.st.render_fps = P.sec_renders * 59.94f / n;
    P.st.dropped_render = P.sec_dropped;
    P.st.refresh_hz = 59.94f;
    P.st.update_ms = P.upd_n ? (float)P.upd_us / P.upd_n / 1000.0f : 0;
    P.st.render_ms = P.ren_n ? (float)P.ren_us / P.ren_n / 1000.0f : 0;
    P.sec_vcount = vc;
    P.sec_steps = P.sec_renders = P.sec_dropped = 0;
    P.upd_us = P.ren_us = 0;
    P.upd_n = P.ren_n = 0;
}

void plat_frame(PlatApp *app)
{
    unsigned vc = sceDisplayGetVcount();
    int due, paused = psp_suspended(), i;
    unsigned long long t;

    if (paused != P.paused) {
        P.paused = paused;
        if (app->on_pause)
            app->on_pause(app->user, paused);
        P.vcount = vc;   /* time does not accumulate while suspended */
    }
    if (paused)
        return;
    due = P.fixed_step ? 1 : (int)(vc - P.vcount);
    if (due < 1)
        due = 1;
    if (due > P.max_steps) {
        P.st.slowed += due - P.max_steps;
        due = P.max_steps;
    }
    P.vcount = vc;
    if (P.hold) {
        plat_poll_input();
        due = 0;
    }
    for (i = 0; i < due; i++) {
        P.last_step = i == due - 1 || !P.frame_skip;
        plat_poll_input();
        t = now_us();
        app->update(app->user);
        P.upd_us += now_us() - t;
        P.upd_n++;
        P.sec_steps++;
        if (!P.last_step)
            P.sec_dropped++;
    }
    if (due || P.hold || P.force_render) {
        P.force_render = 0;
        t = now_us();
        if (app->render)
            app->render(app->user);
        P.ren_us += now_us() - t;
        P.ren_n++;
        P.sec_renders++;
    }
    stats_tick(vc);
}

void plat_run(PlatApp *app)
{
    P.vcount = sceDisplayGetVcount();
    P.sec_vcount = P.vcount;
    while (!plat_quit_requested()) {
        plat_frame(app);
        sceDisplayWaitVblankStart();
    }
}
