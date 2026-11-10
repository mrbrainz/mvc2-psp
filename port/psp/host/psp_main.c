/* o32 side of the PSP runner (called from psp_entry.c, EABI).
 *
 * Real time by default: boot, then the 59.94 Hz loop of platform_psp.c until
 * HOME -> Exit.  Testing switches come from ms0:/PSP/GAME/mvc2/env.txt
 * (KEY=VALUE lines; the PSP has no environment):
 *   FRAMES=N        headless: exactly N frames as fast as possible, with the
 *                   logic hash (ra_state_hash) every 60 frames
 *   QUIT_AFTER=N    real time: fire the exit callback at frame N, as
 *                   HOME -> Exit does (PPSSPP has no HOME menu)
 *   PROFILE=1       stage timings (psp_prof.c)
 *   DRAWLOG=1       draw counters per 60 frames
 *   anything else   setenv, e.g. MVC2_STATE_DUMP / MVC2_STATE_SNAP for
 *                   port/tools/oracle/compare_ports.py, MVC2_PS2FP_OFF=1
 * See port/docs/PSP.md. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mvc2_stubs.h"
#include "../../host/host_loop.h"
#include "../../host/runahead.h"
#include "../../platform/platform.h"
#include "host.h"
#include "psp_entry.h"

unsigned psp_fpu_init(void);   /* psp_fpu.c */
int r_init(void);              /* port/render/r_gu.c */
void r_gu_present(void);
void fe_render_init(void);     /* port/host/fe/fe_render.c */

/* the o32/EABI boundary items the host relies on (port/docs/PSP.md) */
static void selftest(void)
{
    char b[128], w[8] = "";
    int x = 0, y = 0, ok;
    volatile double d = 2.0;
    FILE *f;
    snprintf(b, sizeof b, "%d %s %x %5.2f %lld", -7, "ok", 255, 3.14159, -12345678901LL);
    ok = !strcmp(b, "-7 ok ff  3.14 -12345678901");
    ok &= sscanf("1f/20 abc", "%x/%x %3s", &x, &y, w) == 3 && x == 31 && y == 32 && !strcmp(w, "abc");
    ok &= fmaf(1.0f + 0x1p-23f, 1.0f - 0x1p-23f, -1.0f) == -0x1p-46f;   /* needs single rounding */
    ok &= (int)(sqrt(d) * 1000) == 1414 && floor(-1.5) == -2.0 && d / 3 > 0.666 && scalbn(d, 3) == 16.0;
    ok &= (long long)(d * 1e10) / 7 == 2857142857LL;
    f = fopen("mvc2_psp_seek.tmp", "w+");
    ok &= f && fputs("0123456789", f) >= 0 && fseeko(f, 6, SEEK_SET) == 0 && fgetc(f) == '6';
    if (f) { fclose(f); remove("mvc2_psp_seek.tmp"); }
    fprintf(stderr, "[host] o32/EABI selftest %s (%s)\n", ok ? "PASS" : "FAIL", b);
}

static int load_env(int frames)
{
    char ln[256];
    FILE *f = fopen("ms0:/PSP/GAME/mvc2/env.txt", "r");
    if (!f)
        return frames;
    while (fgets(ln, sizeof ln, f)) {
        char *eq = strchr(ln, '='), *nl = strchr(ln, '\n');
        if (nl) *nl = 0;
        if (!eq || ln[0] == '#') continue;
        *eq = 0;
        if (!strcmp(ln, "FRAMES")) frames = atoi(eq + 1);
        else setenv(ln, eq + 1, 1);
        fprintf(stderr, "[host] env %s=%s\n", ln, eq + 1);
    }
    fclose(f);
    return frames;
}

static long quit_after;

/* DRAWLOG=1 (testing): the frontend's draw counters (fe_render.c fe_cnt:
 * tri_tex, tri, sprite, quad, fill, then rejected tri_tex/sprite/quad),
 * summed over each 60 frames */
extern int fe_cnt[8];
static int drawlog = -1;

static void rt_update(void *u)
{
    PlatStats st;
    (void)u;
    if (drawlog < 0)
        drawlog = getenv("DRAWLOG") != NULL;
    mvc2_frame();
    if (drawlog && mvc2_host_frame % 60 == 0) {
        const unsigned char *g = (const unsigned char *)EE_BSS_PTR(0x004F4300);
        fprintf(stderr, "[draw] frame %ld state %d %d %d: tt %d t %d sp %d q %d fill %d | rej tt %d sp %d q %d\n",
                (long)mvc2_host_frame, g[0], g[1], g[2], fe_cnt[0], fe_cnt[1], fe_cnt[2], fe_cnt[3], fe_cnt[4],
                fe_cnt[5], fe_cnt[6], fe_cnt[7]);
        memset(fe_cnt, 0, sizeof fe_cnt);
    }
    if (quit_after && mvc2_host_frame == quit_after)
        psp_test_exit();
    if (mvc2_host_frame % 300 == 0) {   /* every 5 s */
        plat_stats(&st);
        fprintf(stderr, "[plat] frame %ld: logic %.2f fps, update %.2f ms, slowed %d\n",
                (long)mvc2_host_frame, st.logic_fps, st.update_ms, st.slowed);
    }
}

static void rt_render(void *u)
{
    (void)u;
    r_gu_present();
}

static void rt_pause(void *u, int paused)
{
    (void)u;
    fprintf(stderr, "[plat] %s\n", paused ? "suspend" : "resume");
}

int mvc2_psp_run(int frames)
{
    unsigned fcsr;
    psp_log("[host] mvc2 PSP\n");
    frames = load_env(frames);
    fcsr = psp_fpu_init();
    fprintf(stderr, "[host] FCSR %08x (RM %u)\n", fcsr, fcsr & 3);
    selftest();
    psp_mem_report("before boot");
    psp_log("[host] boot\n");
    if (host_disc_open("ms0:/PSP/GAME/mvc2/mvc2.iso") != 0)
        psp_log("[host] no disc image: file loads will fail\n");
    if (r_init() == 0)
        fe_render_init();   /* the game's render-library calls draw with sceGu */
    mvc2_boot(1);
    psp_mem_report("after boot");
    if (frames > 0) {
        while (mvc2_host_frame < frames) {
            mvc2_frame();
            r_gu_present();
            if (mvc2_host_frame % 60 == 0)
                fprintf(stderr, "frame %ld hash %016llx\n", (long)mvc2_host_frame, ra_state_hash());
        }
    } else {
        PlatApp app = {0, rt_update, rt_render, rt_pause};
        const char *q = getenv("QUIT_AFTER");
        quit_after = q ? atol(q) : 0;
        plat_init(0);
        plat_run(&app);
        fprintf(stderr, "[host] quit at frame %ld\n", (long)mvc2_host_frame);
    }
    psp_mem_report("end");
    psp_log("[host] done\n");
    return 0;
}
