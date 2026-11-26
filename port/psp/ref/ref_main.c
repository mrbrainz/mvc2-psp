/* P42: reference runner for the PSP determinism check (wasm, under node).
 *
 * The upstream headless host (port/stubs/host_main.c) with the frontend
 * the PSP links too (fe_render.c, fe_mesh.c, fe_char.c; renderer on
 * render/swgl.c and port/audio with no device, as port/generic/mvc2g.c), and
 * the PSP build's real-time frame step (port/psp/host/psp_main.c rt_update):
 * psp_script.c's input for pads 1 and 2, the PSP's idle pad (sticks centred,
 * analog mode), host_pad_set, mvc2_frame; then 1/60 s of sound mixed.
 * The PSP's game-flow changes come along: psp_1p.c (one player: VS skipped
 * in the main menu, P2's button-config column) and psp_credits.c, with the
 * PSP's --wrap flags, and the boot memory-card screen as the PSP leaves it
 * with no savedata (psp_saveui.c __wrap_func_00130040, "no data" path).
 * Linked from build/port-em's mvc2_host objects by port/psp/ref/build.sh, in
 * place of host_main.c.
 *
 *   node mvc2_psp_ref.js --iso DISC --frames N [--script FILE] [--no-unlock]
 *
 * Testing switches come from the environment as on the PSP's env.txt
 * (MVC2_STATE_DUMP, MVC2_RESYNC, MVC2_P1_TEAM, ...).  The run ends after N
 * frames or at the script's "quit". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "host_loop.h"
#include "pad.h"
#include "platform.h"
#include "../render/render.h"
#include "../render/swgl.h"
#include "../audio/audio.h"

void fe_render_init(void);
int fe_audio_init(int open_device);
/* no settings store: every setting keeps its default (as mvc2g.c) */
const char *settings_get(const char *key) { (void)key; return 0; }

void psp_1p_init(void);
void func_0012FC40(void);

/* psp_saveui.c __wrap_func_00130040 when psp_save_load finds no savedata:
 * autosave off, on to the next front-end screen */
void __wrap_func_00130040(void)
{
    uint8_t *g = EE_BSS(uint8_t *, 0x004C2840);    /* G_MENU */
    EE_BSS(uint8_t, 0x004D179B) = 0;               /* D_AUTOSAVE */
    fprintf(stderr, "[save] boot autoload: no data (1, 0 bytes)\n");
    g[1]++;
    g[2] = g[3] = g[4] = g[5] = 0;
    if (EE_BSS(uint8_t, 0x004D17CB)) { EE_BSS(uint8_t, 0x004D17CB) = 0; func_0012FC40(); }   /* D_PENDING */
}

int psp_script_load(const char *path);
void psp_script_step(uint16_t *b0, uint16_t *b1);

static int quit;

/* what psp_script.c calls on the PSP */
void plat_request_quit(void) { quit = 1; }
int psp_shot_fb(void **pix, int *stride) { (void)pix; (void)stride; return -1; }
const char *psp_game_dir(void) { return "."; }

int main(int argc, char **argv)
{
    long frames = 0;
    const char *iso = "private/disc/mvc2.iso", *script = NULL;
    int i, script_on = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = strtol(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "--iso") && i + 1 < argc) iso = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--no-unlock")) mvc2_unlock_all = 0;
        else {
            fprintf(stderr, "usage: %s --iso DISC --frames N [--script FILE] [--no-unlock]\n", argv[0]);
            return 2;
        }
    }
    if (script) {
        script_on = psp_script_load(script) == 0;
        fprintf(stderr, "[host] script %s: %s\n", script, script_on ? "loaded" : "cannot read");
        if (!script_on) return 2;
    }
    if (host_disc_open(iso) != 0)
        fprintf(stderr, "[host] no disc image %s: file loads will fail\n", iso);
    swgl_install();
    swgl_window(FL_SCREEN_W, FL_SCREEN_H);
    if (r_init()) { fprintf(stderr, "[host] r_init failed\n"); return 1; }
    fe_render_init();
    fe_audio_init(0);
    psp_1p_init();
    mvc2_boot(1);
    while (mvc2_host_frame < frames && !quit) {
        PadRaw p0, p1;
        memset(&p0, 0, sizeof p0);
        p0.lx = p0.ly = p0.rx = p0.ry = 0x80;
        p0.analog = 1;
        p1 = p0;
        if (script_on && mvc2_host_frame >= 0) {
            uint16_t b0, b1;
            psp_script_step(&b0, &b1);
            p0.buttons |= b0;
            p1.buttons |= b1;
        }
        p0.connected = p1.connected = 1;
        host_pad_set(0, &p0);
        host_pad_set(1, &p1);
        r_set_discard(1);   /* no picture needed; the game state is the same either way */
        mvc2_frame();
        {
            static float mix[AUDIO_RATE / 60 * 2];
            audio_render(mix, AUDIO_RATE / 60);
        }
    }
    fprintf(stderr, "[host] quit at frame %ld\n", (long)mvc2_host_frame);
    return 0;
}
