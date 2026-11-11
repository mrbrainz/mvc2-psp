/* sceAudio output (psp-gcc, EABI32): an SRC channel at 48 kHz stereo fed by
 * its own thread, which calls the o32 mixer for each buffer. A buffer whose
 * fill ends after the previous one has finished playing is an underrun. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <psprtc.h>
#include <stdio.h>
#include "psp_audio_hw.h"
#include "psp_entry.h"

static PspAudioFill fill_fn;
static SceUID thid = -1, sema = -1;
static volatile int running;
static short buf[2][PSP_AUDIO_FRAMES * 2] __attribute__((aligned(64)));
static volatile unsigned fills, underruns, worst_us;

static unsigned now_us(void) { return (unsigned)sceKernelGetSystemTimeLow(); }

static int audio_thread(SceSize args, void *argp)
{
    const unsigned period = PSP_AUDIO_FRAMES * 1000000u / 48000u;
    unsigned due = 0;   /* when the queued buffer runs out */
    int cur = 0;
    (void)args; (void)argp;
    while (running) {
        unsigned t0 = now_us(), dt;
        fill_fn(buf[cur], PSP_AUDIO_FRAMES);
        dt = now_us() - t0;
        if (dt > worst_us) worst_us = dt;
        fills++;
        if (due && (int)(now_us() - due) > 0) underruns++;
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
    char b[96];
    sniprintf(b, sizeof b, "[audio] fills %u, worst fill %u us, underruns %u total\n", fills, worst_us, underruns);
    psp_log(b);
    fills = worst_us = 0;
}
