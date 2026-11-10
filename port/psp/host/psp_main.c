/* o32 side of the PSP headless runner: the same sequence as
 * port/stubs/host_main.c (boot, then N frames), printing the logic-state hash
 * (ra_state_hash) every 60 frames for comparison with the native build. */
#include "mvc2_stubs.h"
#include "../../host/host_loop.h"
#include "../../host/runahead.h"
#include "psp_entry.h"


/* no varargs across o32/EABI: format here */
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *put_u(char *p, unsigned long v) {
    char t[12]; int n = 0;
    do t[n++] = '0' + v % 10; while (v /= 10);
    while (n) *p++ = t[--n];
    return p;
}
static char *put_x64(char *p, unsigned long long v) {
    for (int i = 60; i >= 0; i -= 4) *p++ = "0123456789abcdef"[(v >> i) & 15];
    return p;
}

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the o32/EABI boundary items mvc2_host relies on (port/docs/PSP.md) */
static void selftest(void) {
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

#include <stdlib.h>

/* testing: optional KEY=VALUE lines (the PSP has no environment);
 * FRAMES=N overrides the frame count */
static int load_env(int frames) {
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

#include "../../platform/platform.h"

/* env PROFILE=1: time per mvc2_frame stage (game_loop.c mvc2_step_hook),
 * reported every 300 frames as average ms per frame */
#include <psprtc.h>
#define NPROF 24
static struct { const char *name; unsigned long long us; } prof[NPROF];
static const char *prof_cur;
static unsigned long long prof_t0;
static int prof_frames;
extern void (*mvc2_step_hook)(const char *name);

static void prof_hook(const char *name)
{
    u64 t = 0;
    int i;
    sceRtcGetCurrentTick(&t);
    if (prof_cur) {
        for (i = 0; i < NPROF && prof[i].name && prof[i].name != prof_cur; i++) {}
        if (i < NPROF) { prof[i].name = prof_cur; prof[i].us += t - prof_t0; }
    }
    prof_cur = name;
    prof_t0 = t;
    if (!name && ++prof_frames == 300) {
        for (i = 0; i < NPROF && prof[i].name; i++) {
            if (prof[i].us / 300 >= 100)   /* >= 0.1 ms */
                fprintf(stderr, "[prof] %6.2f ms  %s\n", prof[i].us / 300 / 1000.0, prof[i].name);
            prof[i].us = 0;
        }
        prof_frames = 0;
    }
}

static long quit_after;   /* env QUIT_AFTER=N: testing, fire the exit callback at frame N */

static void rt_update(void *u)
{
    PlatStats st;
    (void)u;
    if (mvc2_step_hook)
        mvc2_step_hook("mvc2_frame top (unlock, hp cap, dump hooks)");
    mvc2_frame();
    if (quit_after && mvc2_host_frame == quit_after)
        psp_test_exit();
    if (mvc2_host_frame % 300 == 0) {   /* every 5 s */
        plat_stats(&st);
        fprintf(stderr, "[plat] frame %ld: logic %.2f fps, update %.2f ms, slowed %d\n",
                (long)mvc2_host_frame, st.logic_fps, st.update_ms, st.slowed);
    }
}
static void rt_pause(void *u, int paused) { (void)u; fprintf(stderr, "[plat] %s\n", paused ? "suspend" : "resume"); }

/* EE single-precision semantics on the Allegrex FPU itself: round toward
 * zero (FCSR RM = 1) and flush denormals (FS, bit 24).  For builds without
 * the ps2_float.c rewrite (MVC2_PS2_FLOAT=OFF); env FPU_RZ=1 (testing). */
static unsigned fpu_rz(void)
{
    unsigned fcsr;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcsr));
    fcsr = (fcsr & ~3u) | 1u | (1u << 24);
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcsr));
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcsr));
    return fcsr;
}

int mvc2_psp_run(int frames) {
    char b[96], *p;
    psp_log("[host] mvc2 PSP headless\n");
    frames = load_env(frames);
    if (getenv("FPU_RZ"))
        fprintf(stderr, "[host] FPU round-to-zero + flush: FCSR %08x\n", fpu_rz());
    selftest();
    psp_mem_report("before boot");
    psp_log("[host] boot\n");
    if (host_disc_open("ms0:/PSP/GAME/mvc2/mvc2.iso") != 0)
        psp_log("[host] no disc image: file loads will fail\n");
    mvc2_boot(1);
    psp_mem_report("after boot");
    if (frames > 0) {   /* headless: exactly N frames, as fast as possible */
        while (mvc2_host_frame < frames) {
            mvc2_frame();
            if (mvc2_host_frame % 60 == 0) {
                p = put_s(b, "frame ");
                p = put_u(p, (unsigned long)mvc2_host_frame);
                p = put_s(p, " hash ");
                p = put_x64(p, ra_state_hash());
                p = put_s(p, "\n");
                *p = 0;
                psp_log(b);
            }
        }
    } else {            /* real time: locked to the 59.94 Hz display */
        PlatApp app = {0, rt_update, 0, rt_pause};
        const char *q = getenv("QUIT_AFTER");
        quit_after = q ? atol(q) : 0;
        if (getenv("PROFILE"))
            mvc2_step_hook = prof_hook;
        plat_init(0);
        plat_run(&app);
        fprintf(stderr, "[host] quit at frame %ld\n", (long)mvc2_host_frame);
    }
    psp_mem_report("end");
    psp_log("[host] done\n");
    return 0;
}
