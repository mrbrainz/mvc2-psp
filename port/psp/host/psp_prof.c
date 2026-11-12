/* PROFILE=1 (env.txt, testing): average ms per mvc2_frame stage every 300
 * frames.  The stages are the calls in mvc2_frame (port/host/game_loop.c),
 * wrapped at link time (--wrap, port/psp/CMakeLists.txt) so upstream code is
 * not touched; "frame other" is mvc2_frame minus the wrapped stages (the
 * frame-top hooks and the small steps). */
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>

static int on = -1, frames;
static unsigned long long acc[64];   /* 0-9 frame stages; 10+ nested inside them (not subtracted) */
static const char *names[64];

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
        { const char *e = getenv("PROFILE"); on = e ? (atoi(e) >= 2 ? 2 : 1) : 0; }
    if (!on) { __real_mvc2_frame(); return; }
    t = now();
    for (i = 1; i < 10; i++) staged += acc[i];
    __real_mvc2_frame();
    for (i = 1; i < 10; i++) staged -= acc[i];
    acc[0] += now() - t + staged;   /* staged is minus this frame's stage time */
    names[0] = "frame other (top hooks, small steps)";
    if (++frames < 300)
        return;
    for (i = 0; i < 64; i++) {
        if (names[i] && acc[i] / 300 >= 100)   /* >= 0.1 ms */
            fprintf(stderr, "[prof] %6.2f ms  %s\n", acc[i] / 300 / 1000.0, names[i]);
        acc[i] = 0;
    }
    frames = 0;
}

/* renderer-side timings (r_gu.c): slot 20+ */
int psp_prof_on(void) { return on > 0; }
int psp_prof_fine(void) { return on >= 2; }   /* PROFILE=2: per-draw splits (their timer calls cost time) */
unsigned long long psp_prof_now(void) { return now(); }
void psp_prof_add(int slot, const char *label, unsigned long long us)
{
    if (on <= 0 || slot < 16 || slot >= 64) return;
    acc[slot] += us;
    names[slot] = label;
}

/* render pass 1 with each task-list entry (D_0058C800, D_004C2A5C entries)
 * timed through a per-slot trampoline (P35) */
typedef void (*TaskFn)(void);
extern TaskFn D_0058C800[];
extern int D_004C2A5C;
#define NTASK 32
static TaskFn task_orig[NTASK];
static unsigned long long task_acc[NTASK];
static TaskFn task_fn[NTASK];   /* last function seen in each slot */
#define TR(i) static void task_tr##i(void) { unsigned long long t = now(); task_orig[i](); task_acc[i] += now() - t; }
TR(0) TR(1) TR(2) TR(3) TR(4) TR(5) TR(6) TR(7) TR(8) TR(9) TR(10) TR(11) TR(12) TR(13) TR(14) TR(15)
TR(16) TR(17) TR(18) TR(19) TR(20) TR(21) TR(22) TR(23) TR(24) TR(25) TR(26) TR(27) TR(28) TR(29) TR(30) TR(31)
static const TaskFn task_tr[NTASK] = { task_tr0, task_tr1, task_tr2, task_tr3, task_tr4, task_tr5, task_tr6, task_tr7,
    task_tr8, task_tr9, task_tr10, task_tr11, task_tr12, task_tr13, task_tr14, task_tr15, task_tr16, task_tr17,
    task_tr18, task_tr19, task_tr20, task_tr21, task_tr22, task_tr23, task_tr24, task_tr25, task_tr26, task_tr27,
    task_tr28, task_tr29, task_tr30, task_tr31 };
static int task_frames;

void __real_func_003D7400(void);
void __wrap_func_003D7400(void)
{
    unsigned long long t;
    int n, i;
    if (on <= 0) { __real_func_003D7400(); return; }
    n = D_004C2A5C;
    if (n > NTASK) n = NTASK;
    for (i = 0; i < n; i++) {
        task_orig[i] = D_0058C800[i];
        if (task_fn[i] != task_orig[i]) { task_fn[i] = task_orig[i]; task_acc[i] = 0; }
        D_0058C800[i] = task_tr[i];
    }
    t = now();
    __real_func_003D7400();
    acc[4] += now() - t;
    names[4] = "func_003D7400 render pass 1";
    for (i = 0; i < n; i++) if (D_0058C800[i] == task_tr[i]) D_0058C800[i] = task_orig[i];
    if (++task_frames >= 300) {
        for (i = 0; i < NTASK; i++) {
            if (task_acc[i] / 300 >= 300)
                fprintf(stderr, "[prof]   task %2d %p: %.2f ms\n", i, (void *)task_fn[i], task_acc[i] / 300 / 1000.0);
            task_acc[i] = 0;
        }
        task_frames = 0;
    }
}
