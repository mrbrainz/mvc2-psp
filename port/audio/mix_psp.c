/* PSP mixer inner loops (P31), linked over synth_render and rs_stream_run
 * (--wrap, port/psp/CMakeLists.txt). The Allegrex has no double FPU and the
 * 32-tap sinc costs more than a whole frame, so voices and the BGM stream are
 * interpolated with a 4-point Hermite on a 32.32 fixed-point position. The
 * envelope is synth.c's env_step, copied. Everything else is upstream's.
 * Packed VAGs (vag_psp.c) are decoded per voice as they play. */
#include "resample.h"
#include "synth.h"
#include "vag_psp.h"
#include <math.h>
#include <string.h>

#define DECLICK 64   /* synth.c */

static void env_step(SyVoice *v)   /* synth.c, unchanged */
{
    int rate, exp, dec;
    uint16_t a1 = v->smpl->adsr1, a2 = v->smpl->adsr2;
    switch (v->phase) {
    case 0: rate = (a1 >> 8) & 0x7F; exp = (a1 >> 15) & 1; dec = 0; break;
    case 1: rate = ((a1 >> 4) & 0xF) << 2; exp = 1; dec = 1; break;
    case 2: rate = (a2 >> 6) & 0x7F; exp = (a2 >> 15) & 1; dec = (a2 >> 14) & 1; break;
    case 3: rate = (a2 & 0x1F) << 2; exp = (a2 >> 5) & 1; dec = 1; break;
    default: return;
    }
    int shift = rate >> 2;
    int32_t step = dec ? -8 + (rate & 3) : 7 - (rate & 3);
    int32_t cycles = 1 << (shift > 11 ? shift - 11 : 0);
    step = shift < 11 ? step << (11 - shift) : step;
    if (exp && !dec && v->level > 0x6000) cycles *= 4;
    if (exp && dec) step = (int32_t)(((int64_t)step * v->level) >> 15);
    if (rate == 0x7F) return;
    if (--v->counter > 0) return;
    v->counter = cycles;
    v->level += step;
    if (v->level > 0x7FFF) v->level = 0x7FFF;
    if (v->level < 0) v->level = 0;
    if (v->phase == 0 && v->level >= 0x7FFF) { v->phase = 1; v->counter = 0; }
    else if (v->phase == 1 && (v->level >> 11) <= (a1 & 0xF)) { v->phase = 2; v->counter = 0; }
    else if (v->phase == 3 && v->level == 0) v->phase = 4;
}

static inline float herm(float xm1, float x0, float x1, float x2, float t)
{
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

static inline float smp(const VagPcm *src, int i)   /* synth.c's, int16 scale */
{
    if (i < 0) return 0;
    if (i >= src->len) {
        if (!src->looped) return 0;
        int body = src->len - src->loop_start;
        i = src->loop_start + (i - src->loop_start) % body;
    }
    return src->pcm[i];
}

/* double <-> 32.32 without soft-float in the loop */
static inline void fx_from(double d, int32_t *i, uint32_t *f)
{
    double fl = floor(d);
    *i = (int32_t)fl;
    *f = (uint32_t)((d - fl) * 4294967296.0);
}
static inline double fx_to(int32_t i, uint32_t f) { return i + f * (1.0 / 4294967296.0); }
#define FX_T(f) ((float)((f) >> 8) * (1.0f / 16777216.0f))

static void mix_render(Synth *s, float *out, int frames);
void __wrap_synth_render(Synth *s, float *out, int frames) { mix_render(s, out, frames); }

static VagDec vdec[SY_VOICES];

static void mix_render(Synth *s, float *out, int frames)
{
    for (int vi = 0; vi < SY_VOICES; vi++) {
        SyVoice *v = &s->v[vi];
        if (!v->active) continue;
        const VagPcm *src = v->src;
        const VagLazy *z = vag_psp_lazy(src);
        VagDec *d = &vdec[vi];
        int32_t pi, si;
        uint32_t pf, sf;
        fx_from(v->pos, &pi, &pf);
        fx_from(v->step, &si, &sf);
        float cl = v->cl, cr = v->cr;
        const float bg = vag_psp_gain(src);
        const float gl = v->gl * bg, gr = v->gr * bg;
        for (int o = 0; o < frames; o++) {
            if (!src->looped && pi >= src->len) { v->active = 0; break; }
            float env;
            if (v->kill) {
                env = v->kill_from * (v->kill / (float)DECLICK);
                if (--v->kill == 0) { v->active = 0; break; }
            } else {
                env_step(v);
                if (v->phase == 4) { v->active = 0; break; }
                env = v->level * (1.0f / 32767.0f);
            }
            float x, t = FX_T(pf);
            int w;
            if (z) {
                if (pi >= 1 && pi + 2 < src->len && ((w = vag_psp_fast(d, z, pi - 1)) >= 0 || (w = vag_psp_window(d, z, pi - 1, pi + 2)) >= 0)) {
                    const int16_t *p = d->buf + w;
                    x = herm(p[0], p[1], p[2], p[3], t);
                } else {
                    x = herm(vag_psp_get(d, src, z, pi - 1), vag_psp_get(d, src, z, pi),
                             vag_psp_get(d, src, z, pi + 1), vag_psp_get(d, src, z, pi + 2), t);
                }
            } else if (pi >= 1 && pi + 2 < src->len) {
                const int16_t *p = src->pcm + pi;
                x = herm(p[-1], p[0], p[1], p[2], t);
            } else {
                x = herm(smp(src, pi - 1), smp(src, pi), smp(src, pi + 1), smp(src, pi + 2), t);
            }
            cl += (gl - cl) * (1.0f / 64);
            cr += (gr - cr) * (1.0f / 64);
            float a = x * (1.0f / 32768.0f) * env;
            out[2 * o] += a * cl;
            out[2 * o + 1] += a * cr;
            uint32_t nf = pf + sf;
            pi += si + (nf < pf);
            pf = nf;
            if (src->looped && pi >= src->len) {
                int body = src->len - src->loop_start;
                pi = src->loop_start + (pi - src->loop_start) % body;
            }
        }
        v->cl = cl; v->cr = cr;
        v->pos = fx_to(pi, pf);
    }
}

/* resample.c's refill, for an int read position */
static void refill(RsStream *s, RsPull pull, void *ctx, int *pos, int need_upto)
{
    int keep_from = *pos - 4;
    if (keep_from > 0) {
        int n = s->fill - keep_from;
        memmove(s->buf[0], s->buf[0] + keep_from, n * sizeof(float));
        memmove(s->buf[1], s->buf[1] + keep_from, n * sizeof(float));
        s->fill = n;
        *pos -= keep_from;
        need_upto -= keep_from;
    }
    float tmp[2 * 512];
    while (s->fill < need_upto && s->fill < RS_BUF) {
        int want = RS_BUF - s->fill;
        if (want > 512) want = 512;
        int got = pull ? pull(ctx, tmp, want) : 0;
        if (got <= 0) {
            got = want;
            memset(tmp, 0, sizeof(float) * 2 * got);
        }
        for (int i = 0; i < got; i++) {
            s->buf[0][s->fill + i] = tmp[2 * i];
            s->buf[1][s->fill + i] = tmp[2 * i + 1];
        }
        s->fill += got;
    }
}

static inline float at(const float *b, int i, int fill) { return i < 0 || i >= fill ? 0.0f : b[i]; }

void __wrap_rs_stream_run(RsStream *s, RsPull pull, void *ctx, float *out, int frames)
{
    if (s->passthrough) {
        int done = 0;
        while (done < frames) {
            int got = pull ? pull(ctx, out + 2 * done, frames - done) : 0;
            if (got <= 0) {
                memset(out + 2 * done, 0, sizeof(float) * 2 * (frames - done));
                break;
            }
            done += got;
        }
        return;
    }
    int32_t pi, si;
    uint32_t pf, sf;
    fx_from(s->pos, &pi, &pf);
    fx_from(s->step, &si, &sf);
    for (int o = 0; o < frames; o++) {
        if (pi + 3 >= s->fill) refill(s, pull, ctx, &pi, pi + 4);
        const float *L = s->buf[0], *R = s->buf[1];
        float t = FX_T(pf);
        int n = s->fill;
        if (pi >= 1 && pi + 2 < n) {
            out[2 * o] = herm(L[pi - 1], L[pi], L[pi + 1], L[pi + 2], t);
            out[2 * o + 1] = herm(R[pi - 1], R[pi], R[pi + 1], R[pi + 2], t);
        } else {
            out[2 * o] = herm(at(L, pi - 1, n), at(L, pi, n), at(L, pi + 1, n), at(L, pi + 2, n), t);
            out[2 * o + 1] = herm(at(R, pi - 1, n), at(R, pi, n), at(R, pi + 1, n), at(R, pi + 2, n), t);
        }
        uint32_t nf = pf + sf;
        pi += si + (nf < pf);
        pf = nf;
    }
    s->pos = fx_to(pi, pf);
}

/* tsd_load_bank (under the mixer lock, from sink_rpc) decodes a whole bank's
 * VAGs: tens of ms, longer than a buffer. The port is unloaded and its voices
 * stopped first, so the mixer can run meanwhile; only tsd.c calls this. */
#include "audio.h"
#include "hdbank.h"
int __real_hd_bank_load(HdBank *b, const void *hd, uint32_t hd_size, const void *bd, uint32_t bd_size,
                        const void *tq, uint32_t tq_size, const void *tbl, uint32_t tbl_size);
int __wrap_hd_bank_load(HdBank *b, const void *hd, uint32_t hd_size, const void *bd, uint32_t bd_size,
                        const void *tq, uint32_t tq_size, const void *tbl, uint32_t tbl_size)
{
    audio_unlock();
    int r = __real_hd_bank_load(b, hd, hd_size, bd, bd_size, tq, tq_size, tbl, tbl_size);
    audio_lock();
    return r;
}
