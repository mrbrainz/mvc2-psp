/* psp-gcc (EABI32) side of the P05 ABI test: the PSP SDK's convention. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include "abi_shim.h"

PSP_MODULE_INFO("mvc2_abi", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

int g4(int a, int b, int c, int d) { return a + 2 * b + 3 * c + 4 * d; }
int g6f(int a, int b, int c, int d, int e, float x) { return a + b + c + d + 10 * e + (int)(x * 100); }
float g_half(int a) { return a * 0.5f; }

/* shim: one pointer in, int out; identical in o32 and EABI32 */
int shim_g6f(const struct g6f_args *p) { return g6f(p->a, p->b, p->c, p->d, p->e, p->x); }

/* EABI caller of an o32 callback; canaries sit around the outgoing area */
int call_cb(int (*cb)(int, int, int, int), int *canary_ok) {
    volatile int c0 = 0x1234, c1 = 0x5678, c2 = 0x9abc, c3 = 0xdef0;
    int r = cb(1, 2, 3, 4);
    *canary_ok = c0 == 0x1234 && c1 == 0x5678 && c2 == 0x9abc && c3 == 0xdef0;
    return r;
}

void report(const char *name, int got, int want) {
    pspDebugScreenPrintf("%-24s %s (got %d want %d)\n", name, got == want ? "PASS" : "FAIL", got, want);
}

static int exit_cb(int a, int b, void *c) { sceKernelExitGame(); return 0; }
static int cb_thread(SceSize a, void *b) {
    sceKernelRegisterExitCallback(sceKernelCreateCallback("exit", exit_cb, NULL));
    sceKernelSleepThreadCB();
    return 0;
}

int main(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0x1000, 0, 0);
    if (t >= 0) sceKernelStartThread(t, 0, 0);
    pspDebugScreenInit();
    pspDebugScreenPrintf("MvC2 PSP: o32 <-> EABI32 test\n\n");
    run_o32_tests();
    pspDebugScreenPrintf("\ndone\n");
    for (;;) sceDisplayWaitVblankStart();
}
