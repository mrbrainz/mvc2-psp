/* PSP module entry (psp-gcc, EABI32): kernel setup, then the o32 host. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <stdio.h>
#include "psp_entry.h"

PSP_MODULE_INFO("mvc2", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#ifndef MVC2_PSP_FRAMES
#define MVC2_PSP_FRAMES 600
#endif

static FILE *logf;

void psp_log(const char *s) {
    pspDebugScreenPrintf("%s", s);
    if (logf) { fputs(s, logf); fflush(logf); }
}

/* newlib's fseeko; o32 callers reach __wrap_fseeko (libc_o32.c, --wrap) */
int __real_fseeko(FILE *, off_t, int);
int psp_fseeko_shim(const struct psp_fseeko_args *a) { return __real_fseeko(a->f, a->off, a->whence); }

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
    logf = fopen("ms0:/PSP/GAME/mvc2/mvc2_psp.log", "w");
    mvc2_psp_run(MVC2_PSP_FRAMES);
    if (logf) fclose(logf);
    for (;;) sceDisplayWaitVblankStart();
}
