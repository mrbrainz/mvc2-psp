/* PSP module entry (psp-gcc, EABI32): kernel setup, then the o32 host. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <stdio.h>
#include <malloc.h>
#include "psp_entry.h"

PSP_MODULE_INFO("mvc2", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);   /* all user memory but 1 MB (kernel free below = what is left) */

#ifndef MVC2_PSP_FRAMES
#define MVC2_PSP_FRAMES 0   /* 0: real time until HOME -> Exit; N: headless N frames */
#endif

static FILE *logf;

void psp_log(const char *s) {
    pspDebugScreenPrintf("%s", s);
    if (logf) { fputs(s, logf); fflush(logf); }
}

/* newlib's fseeko; o32 callers reach __wrap_fseeko (libc_o32.c, --wrap) */
int __real_fseeko(FILE *, off_t, int);
int psp_fseeko_shim(const struct psp_fseeko_args *a) { return __real_fseeko(a->f, a->off, a->whence); }

void psp_mem_report(const char *when) {
    char b[160];
    struct mallinfo mi = mallinfo();
    /* sniprintf: newlib's own (EABI); snprintf is libc_o32.c's o32 one */
    sniprintf(b, sizeof b, "[mem] %s: kernel free %u KB (largest %u KB), malloc arena %u KB, in use %u KB\n",
             when, sceKernelTotalFreeMemSize() / 1024, sceKernelMaxFreeMemSize() / 1024,
             (unsigned)mi.arena / 1024, (unsigned)mi.uordblks / 1024);
    psp_log(b);
}

/* HOME -> Exit: ask the loop to stop (plat_quit_requested), so the log is
 * closed and the module exits from main; give up after 3 s if it does not. */
static volatile int exit_req, suspended, main_done;
int psp_exit_requested(void) { return exit_req; }
int psp_suspended(void) { return suspended; }

static int exit_cb(int a, int b, void *c) {
    int i;
    exit_req = 1;
    for (i = 0; i < 300 && !main_done; i++)
        sceKernelDelayThread(10000);
    sceKernelExitGame();
    return 0;
}
static int power_cb(int unknown, int flags, void *c) {
    if (flags & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY))
        suspended = 1;
    else if (flags & PSP_POWER_CB_RESUME_COMPLETE)
        suspended = 0;
    return 0;
}
static SceUID exit_cbid = -1;
/* testing: fire the exit callback as HOME -> Exit does (PPSSPP has no HOME menu) */
void psp_test_exit(void) { if (exit_cbid >= 0) sceKernelNotifyCallback(exit_cbid, 0); }
static int cb_thread(SceSize a, void *b) {
    exit_cbid = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(exit_cbid);
    scePowerRegisterCallback(-1, sceKernelCreateCallback("power", power_cb, NULL));
    sceKernelSleepThreadCB();
    return 0;
}

const char *psp_eboot_path;   /* argv[0]: the EBOOT.PBP that was started */

int main(int argc, char **argv) {
    psp_eboot_path = argc > 0 ? argv[0] : NULL;
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0x1000, 0, 0);
    if (t >= 0) sceKernelStartThread(t, 0, 0);
    scePowerSetClockFrequency(333, 333, 166);   /* full speed (default 222 MHz) */
    pspDebugScreenInit();
    logf = fopen("ms0:/PSP/GAME/mvc2/mvc2_psp.log", "w");
    mvc2_psp_run(MVC2_PSP_FRAMES);
    if (logf) fclose(logf);
    main_done = 1;
    sceKernelExitGame();
    return 0;
}
