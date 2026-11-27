/* PSP module entry (psp-gcc, EABI32): kernel setup, then the o32 host. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspdmac.h>
#include <psppower.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <kubridge.h>
#include "psp_entry.h"

PSP_MODULE_INFO("mvc2", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);   /* all user memory but 1 MB (kernel free below = what is left) */

#ifndef MVC2_PSP_FRAMES
#define MVC2_PSP_FRAMES 0   /* 0: real time until HOME -> Exit; N: headless N frames */
#endif

/* The game folder: ms0:/PSP/GAME/mvc2, or the EBOOT's own folder when it was
 * started from PSPLink's host0: (env.txt, the log, shots, settings). */
static char game_dir[160] = "ms0:/PSP/GAME/mvc2";
const char *psp_game_dir(void) { return game_dir; }
static const char *gpath(char *b, unsigned n, const char *name)
{   /* no snprintf here: varargs differ across the o32/EABI boundary */
    if (strlen(game_dir) + 1 + strlen(name) >= n) { b[0] = 0; return b; }
    strcpy(b, game_dir); strcat(b, "/"); strcat(b, name);
    return b;
}
static char lp[200];
#define LOG_PATH (lp[0] ? lp : gpath(lp, sizeof lp, "mvc2_psp.log"))
static int log_ok;

/* Each line is appended and the file closed again: on a memory stick a
 * flushed but open file keeps its old size, so a crash lost the last lines. */
/* No log file unless asked for: "log=on" in settings.txt, or a testing
 * set-up (env.txt or mvc2exc.prx in the game folder). */
/* Log lines on the debug screen too: debug builds only (mvc2exc.prx next to
 * the EBOOT, or SCREENLOG=1 in env.txt); cleared once the renderer owns the
 * display either way.  The log file, when on, gets every line. */
int psp_log_screen;

void psp_log(const char *s) {
    if (psp_log_screen)   /* the debug screen draws into the game's framebuffer */
        pspDebugScreenPrintf("%s", s);
    if (log_ok) {
        SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_APPEND | PSP_O_CREAT, 0777);
        if (fd >= 0) { sceIoWrite(fd, s, strlen(s)); sceIoClose(fd); }
    }
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
static volatile int exit_req, suspended, main_done, sleep_asked, woke;
int psp_exit_requested(void) { return exit_req; }
int psp_suspended(void) { return suspended; }

static int exit_cb(int a, int b, void *c) {
    int i;
    exit_req = 1;
    for (i = 0; i < 300 && !main_done; i++)
        sceKernelDelayThread(10000);
    { extern void r_gu_me_stop(void); r_gu_me_stop(); }   /* P104: main did not get there */
    sceKernelExitGame();
    return 0;
}
/* P117: sleep_asked (power switch, suspend, standby; not the switch bit
 * that comes with a wake) and woke (resume complete), read and cleared by
 * the ME hand-back in r_gu.c */
int psp_sleep_asked(void) { int r = sleep_asked; sleep_asked = 0; return r; }
int psp_woke(void) { int r = woke; woke = 0; return r; }
static int power_cb(int unknown, int flags, void *c) {
    {   /* P117: every power event in the log */
        char b[48];
        sniprintf(b, sizeof b, "[power] flags %08x\n", (unsigned)flags);
        psp_log(b);
    }
    if (flags & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY))
        suspended = 1;
    else if (flags & PSP_POWER_CB_RESUME_COMPLETE)
        suspended = 0;
    if (flags & PSP_POWER_CB_RESUME_COMPLETE) { woke = 1; sleep_asked = 0; }
    else if ((flags & (PSP_POWER_CB_POWER_SWITCH | PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY)) &&
             !(flags & PSP_POWER_CB_RESUMING))
        sleep_asked = 1;
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

/* testing (psp_script.c "shot"): the displayed 8888 frame, uncached */
int psp_shot_fb(void **pix, int *stride) {
    void *top; int bufw, fmt;
    static int made;
    if (!made) { { char sd[200]; sceIoMkdir(gpath(sd, sizeof sd, "shots"), 0777); }; made = 1; }
    if (sceDisplayGetFrameBuf(&top, &bufw, &fmt, PSP_DISPLAY_SETBUF_NEXTFRAME) < 0 || !top || fmt != PSP_DISPLAY_PIXEL_FORMAT_8888) return -1;
    /* DMAC copy: PPSSPP keeps the frame on the GPU and only downloads it to
     * VRAM for a copy it can see; a plain read gets a stale frame */
    static void *buf;
    unsigned n = (unsigned)bufw * 272 * 4;
    if (!buf && !(buf = memalign(64, n))) return -1;
    sceKernelDcacheWritebackInvalidateRange(buf, n);
    if (sceDmacMemcpy(buf, top, n) < 0) return -1;
    *pix = (void *)((unsigned)buf | 0x40000000u);
    *stride = bufw;
    return 0;
}

const char *psp_eboot_path;   /* argv[0]: the EBOOT.PBP that was started */

/* Debug builds: mvc2exc.prx next to the EBOOT (port/psp/exc) is a kernel
 * module that shows a register dump when any thread faults; loaded through
 * the CFW's kubridge.  The code base lets EPC/RA be matched to mvc2_psp.elf. */
int main(int argc, char **argv);

/* 1 if a line of the text file in the game folder starts with key (0: none,
 * -1: no such file) */
static int file_has(const char *name, const char *key)
{
    static char buf[2048];
    char ep[200];
    SceUID fd = sceIoOpen(gpath(ep, sizeof ep, name), PSP_O_RDONLY, 0);
    int n;
    if (fd < 0) return -1;
    n = sceIoRead(fd, buf, sizeof buf - 1);
    sceIoClose(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    {
        const char *p;
        for (p = strstr(buf, key); p; p = strstr(p + 1, key))
            if (p == buf || p[-1] == '\n') return 1;
    }
    return 0;
}

static int log_wanted(void)
{
    char ep[200];
    SceIoStat st;
    return file_has("settings.txt", "log=on") > 0 || file_has("env.txt", "") >= 0 ||
           sceIoGetstat(gpath(ep, sizeof ep, "mvc2exc.prx"), &st) >= 0;
}


/* Debug builds (with mvc2exc.prx): a watchdog logs the main thread's state
 * every second, so a hang shows whether it spins or waits, and on what. */
static SceUID main_thid;
static int watchdog(SceSize a, void *b)
{
    char line[200];
    int sec = 0;
    (void)a; (void)b;
    for (;;) {
        SceKernelThreadInfo ti;
        sceKernelDelayThread(1000000);
        sec++;
        memset(&ti, 0, sizeof ti);
        ti.size = sizeof ti;
        if (sceKernelReferThreadStatus(main_thid, &ti) < 0) continue;
        sniprintf(line, sizeof line, "[wd] %ds main: status %x wait type %x wait id %x pc? entry %p run clocks %u\n",
                  sec, (unsigned)ti.status, (unsigned)ti.waitType, (unsigned)ti.waitId, ti.entry,
                  (unsigned)ti.runClocks.low);
        psp_log(line);
    }
    return 0;
}

static void exc_load(void)
{
    char path[256], line[160];
    const char *e = psp_eboot_path ? strrchr(psp_eboot_path, '/') : NULL;
    SceUID m;
    int r = -1;
    sniprintf(line, sizeof line, "[host] code base: main %p\n", (void *)main);
    psp_log(line);
    if (!e || (size_t)(e - psp_eboot_path) + 16 > sizeof path) return;
    memcpy(path, psp_eboot_path, (size_t)(e - psp_eboot_path));
    strcpy(path + (e - psp_eboot_path), "/mvc2exc.prx");
    m = kuKernelLoadModule(path, 0, NULL);
    if (m >= 0) r = sceKernelStartModule(m, 0, NULL, NULL, NULL);
    if (m >= 0 || m != (SceUID)0x80010002) {   /* not "file not found" */
        sniprintf(line, sizeof line, "[host] mvc2exc.prx: load %08x start %08x\n", (unsigned)m, (unsigned)r);
        psp_log(line);
    }
    if (m < 0) return;
    psp_log_screen = 1;
    main_thid = sceKernelGetThreadId();
    {
        SceUID t = sceKernelCreateThread("mvc2_wd", watchdog, 0x10, 0x2000, PSP_THREAD_ATTR_USER, NULL);
        if (t >= 0) sceKernelStartThread(t, 0, NULL);
    }
    if (file_has("env.txt", "CRASHTEST=1") > 0) {
        psp_log("[host] CRASHTEST: writing to address 0\n");
        *(volatile int *)0 = 1;
    }
}

int main(int argc, char **argv) {
    psp_eboot_path = argc > 0 ? argv[0] : NULL;
    if (psp_eboot_path && !strncmp(psp_eboot_path, "host", 4)) {
        const char *e = strrchr(psp_eboot_path, '/');
        unsigned n = e ? (unsigned)(e - psp_eboot_path) : 0;
        if (n && n < sizeof game_dir) { memcpy(game_dir, psp_eboot_path, n); game_dir[n] = 0; }
    }
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0x1000, 0, 0);
    if (t >= 0) sceKernelStartThread(t, 0, 0);
    scePowerSetClockFrequency(333, 333, 166);   /* full speed (default 222 MHz) */
    pspDebugScreenInit();
    if (log_wanted()) {
        char prev[200];   /* the last run's log kept (a crash's log survives the next start) */
        gpath(prev, sizeof prev, "mvc2_psp.prev.log");
        sceIoRemove(prev);
        sceIoRename(LOG_PATH, prev);
        SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0) { sceIoClose(fd); log_ok = 1; }
    }
    if (file_has("env.txt", "SCREENLOG=1") > 0)
        psp_log_screen = 1;
    exc_load();
    mvc2_psp_run(MVC2_PSP_FRAMES);
    main_done = 1;
    sceKernelExitGame();
    return 0;
}
