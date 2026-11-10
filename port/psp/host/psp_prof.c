/* PROFILE=1 (env.txt, testing): average ms per mvc2_frame stage every 300
 * frames.  The stages are the calls in mvc2_frame (port/host/game_loop.c),
 * wrapped at link time (--wrap, port/psp/CMakeLists.txt) so upstream code is
 * not touched; "frame other" is mvc2_frame minus the wrapped stages (the
 * frame-top hooks and the small steps). */
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>

static int on = -1, frames;
static unsigned long long acc[32];   /* 0-9 frame stages; 10+ nested inside them (not subtracted) */
static const char *names[32];

static unsigned long long now(void)
{
    u64 t = 0;
    sceRtcGetCurrentTick(&t);
    return t;
}

#define STAGE(i, fn, label)                               \
    void __real_##fn(void);                               \
    void __wrap_##fn(void)                                \
    {                                                     \
        unsigned long long t;                             \
        if (on <= 0) { __real_##fn(); return; }           \
        t = now();                                        \
        __real_##fn();                                    \
        acc[i] += now() - t;                              \
        names[i] = label;                                 \
    }

STAGE(1, func_0012F460, "func_0012F460 counters/CRI/pad")
STAGE(2, func_001E42A0, "func_001E42A0 sound tick")
STAGE(3, func_00170D30, "func_00170D30 begin scene")
STAGE(4, func_003D7400, "func_003D7400 render pass 1")
STAGE(5, func_003D7640, "func_003D7640 render pass 2")
STAGE(6, func_0012DD90, "func_0012DD90 file loader tick")
STAGE(7, func_00170D70, "func_00170D70 end scene")
STAGE(8, func_003ECD00, "func_003ECD00 vsync/flip")

#define SUB(i, ret, fn, params, args, label)               \
    ret __real_##fn params;                               \
    ret __wrap_##fn params                                \
    {                                                     \
        unsigned long long t;                             \
        ret r;                                            \
        if (on <= 0) return __real_##fn args;             \
        t = now();                                        \
        r = __real_##fn args;                             \
        acc[i] += now() - t;                              \
        names[i] = label;                                 \
        return r;                                         \
    }
SUB(10, int, func_003F0500, (int h), (h), "  func_003F0500 mesh draw (game body + fl_clay_draw)")
SUB(11, int, fl_clay_draw, (int h), (h), "    fl_clay_draw (GU)")
SUB(15, int, func_003F0500__game, (int h), (h), "    func_003F0500__game (VU1 packet build)")
#define VSUB(i, fn, params, args, label)                   \
    void __real_##fn params;                              \
    void __wrap_##fn params                               \
    {                                                     \
        unsigned long long t;                             \
        if (on <= 0) { __real_##fn args; return; }        \
        t = now();                                        \
        __real_##fn args;                                 \
        acc[i] += now() - t;                              \
        names[i] = label;                                 \
    }
SUB(12, int, func_00412260, (int s, int f, void *m, unsigned c, int p), (s, f, m, c, p), "  func_00412260 character draw")

void __real_func_0041BB30(void *a);
void __wrap_func_0041BB30(void *a)
{
    unsigned long long t;
    if (on <= 0) { __real_func_0041BB30(a); return; }
    t = now();
    __real_func_0041BB30(a);
    acc[13] += now() - t;
    names[13] = "  func_0041BB30 vertex-write end";
}

void __real_fe_sync_state(void);
void __wrap_fe_sync_state(void)
{
    unsigned long long t;
    if (on <= 0) { __real_fe_sync_state(); return; }
    t = now();
    __real_fe_sync_state();
    acc[14] += now() - t;
    names[14] = "  fe_sync_state";
}

/* func_003D6FF0 takes four args (the game update) */
void __real_func_003D6FF0(int a, int b, int c, int d);
void __wrap_func_003D6FF0(int a, int b, int c, int d)
{
    unsigned long long t;
    if (on <= 0) { __real_func_003D6FF0(a, b, c, d); return; }
    t = now();
    __real_func_003D6FF0(a, b, c, d);
    acc[9] += now() - t;
    names[9] = "func_003D6FF0 game update";
}

void __real_mvc2_frame(void);
void __wrap_mvc2_frame(void)
{
    unsigned long long t, staged = 0;
    int i;
    if (on < 0)
        on = getenv("PROFILE") != NULL;
    if (!on) { __real_mvc2_frame(); return; }
    t = now();
    for (i = 1; i < 10; i++) staged += acc[i];
    __real_mvc2_frame();
    for (i = 1; i < 10; i++) staged -= acc[i];
    acc[0] += now() - t + staged;   /* staged is minus this frame's stage time */
    names[0] = "frame other (top hooks, small steps)";
    if (++frames < 300)
        return;
    for (i = 0; i < 32; i++) {
        if (names[i] && acc[i] / 300 >= 100)   /* >= 0.1 ms */
            fprintf(stderr, "[prof] %6.2f ms  %s\n", acc[i] / 300 / 1000.0, names[i]);
        acc[i] = 0;
    }
    frames = 0;
}
