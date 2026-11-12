/* PSP mixer inner loops (P31), linked over synth_render and rs_stream_run
 * (--wrap, port/psp/CMakeLists.txt). The Allegrex has no double FPU and the
 * 32-tap sinc costs more than a whole frame, so voices and the BGM stream are
 * interpolated linearly on a 32.32 fixed-point position and mixed in fixed
 * point into audio_psp.c's int32 buffer (P92: float is slow on the Allegrex).
 * The
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

static inline float lerp(float x0, float x1, float t) { return x0 + (x1 - x0) * t; }

float mix_psp_gain = 1.0f;   /* audio_psp.c: SFX bus x master */

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

static void mix_render(Synth *s, int32_t *out, int frames);
/* P92: audio_psp.c passes an int32 stereo buffer (int16 scale) as tsd_render's float * */
void __wrap_synth_render(Synth *s, float *out, int frames) { mix_render(s, (int32_t *)(void *)out, frames); }

static VagDec vdec[SY_VOICES];

/* rate field for the voice's current phase (env_step's switch) */
static inline int env_rate(const SyVoice *v)
{
    uint16_t a1 = v->smpl->adsr1, a2 = v->smpl->adsr2;
    switch (v->phase) {
    case 0: return (a1 >> 8) & 0x7F;
    case 1: return ((a1 >> 4) & 0xF) << 2;
    case 2: return (a2 >> 6) & 0x7F;
    case 3: return (a2 & 0x1F) << 2;
    default: return 0x7F;
    }
}

/* P92: fixed point. Integer interpolation and envelope; the envelope's rate
 * decode runs only when its counter expires (env_step only decrements
 * otherwise, or does nothing at rate 0x7F). Gains are Q12 (bus gain folded
 * in), smoothed like synth.c's; v->cl/cr are kept in synth.c's units. */
static void mix_render(Synth *s, int32_t *out, int frames)
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
        const float sc = vag_psp_gain(src) * mix_psp_gain * 4096.0f;
        const int gl = (int)(v->gl * sc), gr = (int)(v->gr * sc);
        int cl = (int)(v->cl * sc), cr = (int)(v->cr * sc);
        const int kf = (int)(v->kill_from * 32767.0f);
        int ph = -1, frozen = 0;
        const int len = src->len;
        for (int o = 0; o < frames; o++) {
            if (!src->looped && pi >= len) { v->active = 0; break; }
            int x, t = (int)(pf >> 17);   /* Q15 */
            int w;
            /* run: steady level and gains, samples [pi, pi+1] already in
             * memory; stops at the window/sample end or the envelope's step */
            if (!v->kill && v->phase == ph && cl == gl && cr == gr && (frozen || v->counter > 1) && pi >= 0) {
                const int16_t *b = NULL;
                int limit = len - 2;
                if (!z) b = src->pcm;
                else if (pi + 1 < len && ((w = vag_psp_fast(d, z, pi)) >= 0 || (w = vag_psp_window(d, z, pi, pi + 1)) >= 0)) {
                    b = d->buf + w - pi;   /* indexed by sample number */
                    if (pi - w + 54 < limit) limit = pi - w + 54;
                }
                if (b && pi <= limit) {
                    int oend = frames;
                    if (!frozen && v->counter - 1 < frames - o) oend = o + v->counter - 1;
                    const int ml = (v->level * cl) >> 15, mr = (v->level * cr) >> 15;
                    int o0 = o;
                    do {
                        int x0 = b[pi], u = (int)(pf >> 17);
                        int y = x0 + (((b[pi + 1] - x0) * u) >> 15);
                        out[2 * o] += (y * ml) >> 12;
                        out[2 * o + 1] += (y * mr) >> 12;
                        uint32_t nf = pf + sf;
                        pi += si + (nf < pf);
                        pf = nf;
                        o++;
                    } while (o < oend && pi <= limit);
                    if (!frozen) v->counter -= o - o0;
                    o--;
                    if (src->looped && pi >= len) {
                        int body = len - src->loop_start;
                        pi = src->loop_start + (pi - src->loop_start) % body;
                    }
                    continue;
                }
            }
            if (z) {
                if (pi >= 0 && pi + 1 < len && ((w = vag_psp_fast(d, z, pi)) >= 0 || (w = vag_psp_window(d, z, pi, pi + 1)) >= 0)) {
                    const int16_t *p = d->buf + w;
                    x = p[0] + (((p[1] - p[0]) * t) >> 15);
                } else {
                    int x0 = vag_psp_get(d, src, z, pi), x1 = vag_psp_get(d, src, z, pi + 1);
                    x = x0 + (((x1 - x0) * t) >> 15);
                }
            } else if (pi >= 0 && pi + 1 < len) {
                const int16_t *p = src->pcm + pi;
                x = p[0] + (((p[1] - p[0]) * t) >> 15);
            } else {
                int x0 = (int)smp(src, pi), x1 = (int)smp(src, pi + 1);
                x = x0 + (((x1 - x0) * t) >> 15);
            }
            int lev;
            if (v->kill) {
                lev = (kf * v->kill) >> 6;   /* kill_from * kill / DECLICK */
                if (--v->kill == 0) { v->active = 0; break; }
            } else {
                if (v->phase != ph) {
                    ph = v->phase;
                    if (ph == 4) { v->active = 0; break; }
                    frozen = env_rate(v) == 0x7F;
                }
                if (!frozen) {
                    if (v->counter > 1) v->counter--;
                    else {
                        env_step(v);
                        if (v->phase == 4) { v->active = 0; break; }
                    }
                }
                lev = v->level;
            }
            if (cl != gl) { int e = gl - cl; cl = e > -64 && e < 64 ? gl : cl + (e >> 6); }
            if (cr != gr) { int e = gr - cr; cr = e > -64 && e < 64 ? gr : cr + (e >> 6); }
            int a = (x * lev) >> 15;
            out[2 * o] += (a * cl) >> 12;
            out[2 * o + 1] += (a * cr) >> 12;
            uint32_t nf = pf + sf;
            pi += si + (nf < pf);
            pf = nf;
            if (src->looped && pi >= len) {
                int body = len - src->loop_start;
                pi = src->loop_start + (pi - src->loop_start) % body;
            }
        }
        v->cl = cl == gl || sc <= 0.0f ? v->gl : cl / sc;
        v->cr = cr == gr || sc <= 0.0f ? v->gr : cr / sc;
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
        if (pi + 1 >= s->fill) refill(s, pull, ctx, &pi, pi + 2);
        const float *L = s->buf[0], *R = s->buf[1];
        float t = FX_T(pf);
        int n = s->fill;
        if (pi >= 0 && pi + 1 < n) {
            out[2 * o] = lerp(L[pi], L[pi + 1], t);
            out[2 * o + 1] = lerp(R[pi], R[pi + 1], t);
        } else {
            out[2 * o] = lerp(at(L, pi, n), at(L, pi + 1, n), t);
            out[2 * o + 1] = lerp(at(R, pi, n), at(R, pi + 1, n), t);
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
