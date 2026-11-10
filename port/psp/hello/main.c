/* P01 toolchain smoke test: prints to the debug screen and exits on HOME. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>

PSP_MODULE_INFO("mvc2_hello", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

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
    pspDebugScreenPrintf("MvC2 PSP: toolchain OK\n");
    for (;;) sceDisplayWaitVblankStart();
}
