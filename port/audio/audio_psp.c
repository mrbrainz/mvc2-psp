/* PSP build of port/audio/audio.c (P31): the same mixer and ADXT-shaped BGM,
 * with SDL's device replaced by an sceAudio output thread (port/psp/host/
 * psp_audio_hw.c, EABI) and its lock by a kernel semaphore. Keep the mixing
 * code in step with audio.c. */
#include "psp_audio_hw.h"
#include "audio.h"
#include "adx.h"
#include "dcse.h"
#include "resample.h"
#include "tsd.h"
#include "tsd_client.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct Bgm {
    AdxStream adx;
    uint8_t *own;          /* owned ADX bytes */
    int stat;
    int paused;            /* ADXT_Pause */
    int loop;              /* ADXT_SetLpFlg */
    int mono;
    int carry;             /* ADX_LOOP_CARRY: PS2-exact loop history */
    int vol;               /* 0.1 dB */
    float gain, cur;       /* target / smoothed linear gain */
    RsStream rs;           /* ADX rate -> 48 kHz (passthrough at 48 kHz) */
    uint32_t played;
} Bgm;

static struct {
    int init;
    Tsd tsd;
    Bgm bgm;
    float master;
    float music, sfx;      /* user volumes (audio.music / audio.sfx) */
    int user_paused;
    int dev_rate;
    RsStream out_rs;       /* 48 kHz -> device rate */
    AudioAfsLoader loader;
    void *loader_ctx;
    float *scratch;
    int scratch_n;

/* ---- locking ---- */
    int dev;
} A;

void audio_lock(void) { if (A.dev) psp_audio_hw_lock(); }
void audio_unlock(void) { if (A.dev) psp_audio_hw_unlock(); }

/* ---- tsd_client sink: flush -> driver, status -> driver ---- */
static void sink_rpc(void *ctx, const uint8_t *buf, size_t n)
{
    (void)ctx;
    audio_lock();
    tsd_exec(&A.tsd, buf, n);
    audio_unlock();
}

static int sink_status(void *ctx, int fno, uint32_t arg)
{
    (void)ctx;
    audio_lock();
    int r = tsd_status(&A.tsd, fno, arg);
    audio_unlock();
    return r;
}

struct Tsd *audio_tsd(void) { return &A.tsd; }

int audio_dcse_refresh(void)
{
    int n = 0;
    audio_lock();
    for (int p = 0; p < TSD_PORTS; p++) {
        if (!A.tsd.port[p].loaded) continue;
        for (int i = 0; i < SY_VOICES; i++) /* a voice may point at a sample being replaced */
            if (A.tsd.synth.v[i].active && A.tsd.synth.v[i].port == p) A.tsd.synth.v[i].active = 0;
        n += dcse_apply(&A.tsd.port[p].bank);
    }
    audio_unlock();
    return n;
}

/* ---- BGM ---- */
static float db10_to_gain(int v) { return v <= -999 ? 0.0f : powf(10.0f, v / 200.0f); }

static int bgm_pull(void *ctx, float *dst, int max)
{
    Bgm *b = (Bgm *)ctx;
    int16_t tmp[2 * 512];
    if (max > 512) max = 512;
    int n = adx_decode(&b->adx, tmp, max);
    int ch = b->adx.h.channels;
    for (int i = 0; i < n; i++) {
        float l = tmp[i * ch] * (1.0f / 32768.0f);
        float r = ch > 1 ? tmp[i * ch + 1] * (1.0f / 32768.0f) : l;
        dst[2 * i] = l;
        dst[2 * i + 1] = r;
    }
    if (n < max && b->adx.ended && b->stat == PADX_PLAYING) b->stat = PADX_PLAYEND;
    return n;
}

static void bgm_render(Bgm *b, float *out, int frames)
{
    if (b->stat != PADX_PLAYING || b->paused) return;
    if (A.scratch_n < frames) {
        free(A.scratch);
        A.scratch = (float *)malloc(sizeof(float) * 2 * frames);
        A.scratch_n = frames;
    }
    float *s = A.scratch;
    rs_stream_run(&b->rs, bgm_pull, b, s, frames);
    for (int i = 0; i < frames; i++) {
        if (b->cur != b->gain) {
            b->cur += (b->gain - b->cur) * (1.0f / 256);
            if (fabsf(b->cur - b->gain) < 1e-5f) b->cur = b->gain;
        }
        float l = s[2 * i], r = s[2 * i + 1];
        if (b->mono) l = r = 0.5f * (l + r);
        out[2 * i] += l * b->cur * A.music;
        out[2 * i + 1] += r * b->cur * A.music;
    }
    b->played += (uint32_t)frames;
}

static void bgm_free(Bgm *b)
{
    free(b->own);
    b->own = NULL;
    b->stat = PADX_STOP;
}

int padx_start_mem(const uint8_t *adx, size_t size, int take)
{
    audio_lock();
    Bgm *b = &A.bgm;
    bgm_free(b);
    if (take) b->own = (uint8_t *)adx;
    int r = adx_open_mem(&b->adx, adx, size, b->loop);
    if (r) {
        b->stat = PADX_ERROR;
    } else {
        b->adx.loop_mode = b->carry ? ADX_LOOP_CARRY : ADX_LOOP_RESTORE;
        rs_stream_init(&b->rs, b->adx.h.rate, AUDIO_RATE);
        b->stat = PADX_PLAYING; /* host decode is instant: DECINFO/PREP are skipped */
        b->played = 0;
    }
    audio_unlock();
    return r;
}

int padx_start_afs(int ptid, int fid)
{
    size_t n = 0;
    uint8_t *d = A.loader ? A.loader(A.loader_ctx, ptid, fid, &n) : NULL;
    if (!d) {
        audio_lock();
        bgm_free(&A.bgm);
        A.bgm.stat = PADX_ERROR;
        audio_unlock();
        return -1;
    }
    return padx_start_mem(d, n, 1);
}

void padx_stop(void) { audio_lock(); bgm_free(&A.bgm); audio_unlock(); }
int padx_get_stat(void) { return A.bgm.stat; }
void padx_set_out_vol(int vol)
{
    audio_lock();
    A.bgm.vol = vol;
    A.bgm.gain = db10_to_gain(vol);
    audio_unlock();
}
int padx_get_out_vol(void) { return A.bgm.vol; }
void padx_pause(int sw) { audio_lock(); A.bgm.paused = sw != 0; audio_unlock(); }
int padx_get_stat_pause(void) { return A.bgm.paused; }
void padx_set_lp_flg(int on)
{
    audio_lock();
    A.bgm.loop = on != 0;
    A.bgm.adx.loop = on && A.bgm.adx.h.has_loop;
    audio_unlock();
}
int padx_get_lp_cnt(void) { return (int)A.bgm.adx.loops; }
void padx_set_loop_carry(int carry) { audio_lock(); A.bgm.carry = carry != 0; A.bgm.adx.loop_mode = carry ? ADX_LOOP_CARRY : ADX_LOOP_RESTORE; audio_unlock(); }
void padx_set_output_mono(int mono) { audio_lock(); A.bgm.mono = mono != 0; audio_unlock(); }
uint32_t padx_get_time(void) { return A.bgm.played; }

/* ---- mixing ---- */
void audio_render(float *out, int frames)
{
    memset(out, 0, sizeof(float) * 2 * frames);
    tsd_render(&A.tsd, out, frames);
    if (A.sfx != 1.0f)
        for (int i = 0; i < 2 * frames; i++) out[i] *= A.sfx;
    bgm_render(&A.bgm, out, frames);
    float g = A.master;
    for (int i = 0; i < 2 * frames; i++) {
        float x = out[i] * g;
        out[i] = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
    }
}

static int pull48(void *ctx, float *dst, int max)
{
    (void)ctx;
    audio_render(dst, max);
    return max;
}
void audio_set_master(float g) { audio_lock(); A.master = g; audio_unlock(); }
void audio_set_music_volume(float g) { audio_lock(); A.music = g; audio_unlock(); }
void audio_set_sfx_volume(float g) { audio_lock(); A.sfx = g; audio_unlock(); }
int audio_device_rate(void) { return A.dev ? AUDIO_RATE : 0; }
void audio_pump(double ahead_s) { (void)ahead_s; }
int audio_needs_manual_render(void) { return !A.dev; }
int audio_unlocked(void) { return 1; }
void audio_set_paused(int paused) { A.user_paused = paused != 0; }

void audio_set_loader(AudioAfsLoader fn, void *ctx) { A.loader = fn; A.loader_ctx = ctx; }

/* sceAudio thread: 48 kHz stereo floats -> s16, silence while suspended */
static void device_fill(short *dst, int frames)
{
    static float buf[PSP_AUDIO_FRAMES * 2];
    static int fpu;
    if (!fpu) { __asm__ volatile("ctc1 %0, $31" ::"r"(0x01000000)); fpu = 1; } /* FS: denormals to zero */
    if (A.user_paused) { memset(dst, 0, sizeof(short) * 2 * frames); return; }
    audio_lock();
    audio_render(buf, frames);
    audio_unlock();
    for (int i = 0; i < 2 * frames; i++) dst[i] = (short)(buf[i] * 32767.0f);
}

int audio_init(int open_device)
{
    if (A.init) return 0;
    memset(&A, 0, sizeof A);
    rs_init();
    tsd_init(&A.tsd);
    A.tsd.resolve = tsdc_resolve;
    A.master = 1.0f;
    A.music = A.sfx = 1.0f;
    A.bgm.loop = 1;
    A.bgm.gain = A.bgm.cur = 1.0f;
    TsdcSink sink = { sink_rpc, sink_status, NULL };
    tsdc_set_sink(&sink);
    tsdc_init();
    A.init = 1;
    if (!open_device) return 0;
    if (psp_audio_hw_start(device_fill) != 0) return -1;
    A.dev = 1;
    return 0;
}

void audio_shutdown(void)
{
    if (!A.init) return;
    psp_audio_hw_stop();
    A.dev = 0;
    tsdc_set_sink(NULL);
    tsd_shutdown(&A.tsd);
    bgm_free(&A.bgm);
    free(A.scratch);
    A.scratch = NULL;
    A.init = 0;
}
