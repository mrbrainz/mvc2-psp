/* sceAudio output (psp-gcc, EABI32): an SRC channel at 48 kHz stereo fed by
 * its own thread, which calls the o32 mixer for each buffer. A buffer whose
 * fill ends after the previous one has finished playing is an underrun. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_audio_hw.h"
#include "psp_entry.h"

static PspAudioFill fill_fn;
static SceUID thid = -1, sema = -1;
static volatile int running;
static short buf[2][PSP_AUDIO_FRAMES * 2] __attribute__((aligned(64)));
/* MVC2_PCM_CAPTURE=S (testing): S seconds of the output from second
 * MVC2_PCM_CAPTURE_AT (default 5),
 * s16 stereo 48 kHz, to pcm.raw in the game folder (written once full) */
static short *cap;
static unsigned cap_n, cap_at, cap_skip;
const char *psp_game_dir(void);
static void cap_put(const short *b)
{
    if (cap_skip) { cap_skip--; return; }
    unsigned n = PSP_AUDIO_FRAMES * 2;
    if (cap_at + n > cap_n) n = cap_n - cap_at;
    memcpy(cap + cap_at, b, n * sizeof *cap);
    cap_at += n;
    if (cap_at == cap_n) {
        char path[200];
        strcpy(path, psp_game_dir()); strcat(path, "/pcm.raw");
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0) { sceIoWrite(fd, cap, cap_n * sizeof *cap); sceIoClose(fd); }
        psp_log(fd >= 0 ? "[audio] pcm.raw written\n" : "[audio] pcm.raw: cannot write\n");
        free(cap); cap = NULL;
    }
}
static volatile unsigned fills, underruns, worst_us, total_us;
volatile unsigned psp_audio_busy_us;   /* never reset: psp_prof.c takes fill time out of its timers (P91) */

static unsigned now_us(void) { return (unsigned)sceKernelGetSystemTimeLow(); }

static int audio_thread(SceSize args, void *argp)
{
    {   /* no FPU exception traps on this thread either (see psp_fpu.c) */
        unsigned f;
        __asm__ volatile("cfc1 %0, $31" : "=r"(f));
        f &= ~(0x1F80u | 0x3F000u);
        __asm__ volatile("ctc1 %0, $31" : : "r"(f));
    }
    const unsigned period = PSP_AUDIO_FRAMES * 1000000u / 48000u;
    unsigned due = 0;   /* when the queued buffer runs out */
    int cur = 0;
    (void)args; (void)argp;
    while (running) {
        unsigned t0 = now_us(), dt;
        fill_fn(buf[cur], PSP_AUDIO_FRAMES);
        dt = now_us() - t0;
        if (dt > worst_us) worst_us = dt;
        total_us += dt;
        psp_audio_busy_us += dt;
        fills++;
        if (due && (int)(now_us() - due) > 0) underruns++;
        if (cap) cap_put(buf[cur]);
        sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, buf[cur]);
        /* returns once the previous buffer is done: this one plays for one period */
        due = now_us() + period;
        cur ^= 1;
    }
    return 0;
}

int psp_audio_hw_start(PspAudioFill fill)
{
    int r;
    fill_fn = fill;
    {
        const char *e = getenv("MVC2_PCM_CAPTURE");
        if (e && atoi(e) > 0) {
            cap_n = (unsigned)atoi(e) * 48000u * 2u;
            const char *at = getenv("MVC2_PCM_CAPTURE_AT");
            cap_skip = (unsigned)(at ? atoi(at) : 5) * 48000u / PSP_AUDIO_FRAMES;
            cap = (short *)malloc(cap_n * sizeof *cap);
        }
    }
    sema = sceKernelCreateSema("mvc2_audio_lock", 0, 1, 1, NULL);
    if (sema < 0) return -1;
    r = sceAudioSRCChReserve(PSP_AUDIO_FRAMES, 48000, 2);
    if (r < 0) { psp_log("[audio] sceAudioSRCChReserve failed\n"); return -1; }
    running = 1;
    /* above the game thread (0x20) so a busy frame cannot starve the output */
    thid = sceKernelCreateThread("mvc2_audio", audio_thread, 0x12, 0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (thid < 0 || sceKernelStartThread(thid, 0, NULL) < 0) { running = 0; sceAudioSRCChRelease(); return -1; }
    psp_log("[audio] sceAudio SRC 48000 Hz, 1024 frames/buffer\n");
    return 0;
}

void psp_audio_hw_stop(void)
{
    if (thid < 0) return;
    running = 0;
    sceKernelWaitThreadEnd(thid, NULL);
    sceKernelDeleteThread(thid);
    thid = -1;
    while (sceAudioSRCChRelease() < 0) sceKernelDelayThread(1000);
}

void psp_audio_hw_lock(void) { sceKernelWaitSema(sema, 1, NULL); }
void psp_audio_hw_unlock(void) { sceKernelSignalSema(sema, 1); }

void psp_audio_hw_report(void)
{
    static unsigned last;
    char b[192];
    unsigned t = now_us(), wall = last ? t - last : 0, per = 0;
    if (wall) per = (unsigned)((unsigned long long)total_us * 16683u / wall);   /* us of mixing per 60 Hz frame */
    sniprintf(b, sizeof b, "[audio] fills %u, mean fill %u us, worst fill %u us, underruns %u total; mixing %u.%02u ms per 60 Hz frame over %u ms\n",
              fills, fills ? total_us / fills : 0, worst_us, underruns, per / 1000, (per % 1000) / 10, wall / 1000);
    psp_log(b);
    fills = worst_us = total_us = 0;
    last = t;
}
