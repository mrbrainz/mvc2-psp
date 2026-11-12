/* PSP: keep VAGs packed (P53).  vag_decode (from hdbank.c, --wrap) records
 * where a sample's blocks are in the bank's BD, which the bank keeps for its
 * whole life, instead of decoding it to 16-bit PCM (3.5x the size).  The
 * VagPcm fields are those upstream sets; pcm points at a VagLazy record in a
 * static pool, which only mix_psp.c reads.  dcse_apply (--wrap) first turns
 * the samples it may replace into real PCM, since it reads and frees them.
 * Decoding is vag.c's, block for block, so the samples are identical. */
#include "vag_psp.h"
#include "hdbank.h"
#include "dcse.h"
#include <stdlib.h>
#include <string.h>
#include "dcse_map.h"

#define POOL 2048
static VagLazy pool[POOL];
static int pool_next;            /* records are handed out round robin */
static uint8_t pool_used[POOL];
static uint32_t pool_gen;

static const int F[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };

static void block(const uint8_t *p, int32_t *h1, int32_t *h2, int16_t *dst)   /* vag.c's */
{
    int sh = p[0] & 0xF, pr = p[0] >> 4;
    int f0 = pr < 5 ? F[pr][0] : 0, f1 = pr < 5 ? F[pr][1] : 0;
    for (int k = 0; k < 28; k++) {
        int b = p[2 + (k >> 1)];
        int n = (k & 1) ? (b >> 4) : (b & 0xF);
        int32_t s = (int32_t)(int16_t)(uint16_t)(n << 12);
        s = (s >> sh) + ((*h1 * f0 + *h2 * f1) >> 6);
        if (s < -32768) s = -32768;
        if (s > 32767) s = 32767;
        *h2 = *h1;
        *h1 = s;
        dst[k] = (int16_t)s;
    }
}

const VagLazy *vag_psp_lazy(const VagPcm *v)
{
    const VagLazy *z = (const VagLazy *)(const void *)v->pcm;
    return (z >= pool && z < pool + POOL) ? z : NULL;
}

int __real_vag_decode(const uint8_t *bd, size_t limit, uint32_t off, uint32_t rate, VagPcm *out);
void __real_vag_free(VagPcm *v);
int __real_dcse_apply(HdBank *b);

int __wrap_vag_decode(const uint8_t *bd, size_t limit, uint32_t off, uint32_t rate, VagPcm *out)
{
    memset(out, 0, sizeof *out);
    out->rate = rate;
    if (off >= limit) return -1;
    size_t nblk = 0, loop_blk = (size_t)-1;   /* vag.c's pass 1 */
    int looped = 0;
    for (size_t p = off; p + 16 <= limit; p += 16) {
        uint8_t fl = bd[p + 1];
        if ((fl & 4) && loop_blk == (size_t)-1) loop_blk = nblk;
        nblk++;
        if (fl & 1) { looped = (fl & 2) != 0; break; }
    }
    if (!nblk) return -1;
    if (looped && loop_blk == (size_t)-1) loop_blk = 0;
    int slot = -1;
    for (int k = 0; k < POOL; k++) {
        int i = (pool_next + k) % POOL;
        if (!pool_used[i]) { slot = i; break; }
    }
    if (slot < 0) return __real_vag_decode(bd, limit, off, rate, out);   /* pool full: plain PCM */
    pool_next = (slot + 1) % POOL;
    pool_used[slot] = 1;
    VagLazy *z = &pool[slot];
    z->base = bd + off;
    z->nblk = (int)nblk;
    z->loop_blk = looped ? (int)loop_blk : 0;
    z->gen = ++pool_gen;
    out->pcm = (int16_t *)(void *)z;
    out->first_len = 28 * (int)nblk;
    out->len = out->first_len;
    if (looped) {
        out->loop_start = out->first_len;
        out->len += 28 * (int)(nblk - loop_blk);
        out->looped = 1;
    }
    return 0;
}

void __wrap_vag_free(VagPcm *v)
{
    const VagLazy *z = vag_psp_lazy(v);
    if (z) { pool_used[z - pool] = 0; memset(v, 0, sizeof *v); return; }
    __real_vag_free(v);
}

/* decode a packed sample to plain PCM (as vag_decode would have) */
static void materialize(VagPcm *v)
{
    const VagLazy *z = vag_psp_lazy(v);
    if (!z) return;
    int16_t *pcm = (int16_t *)malloc(sizeof(int16_t) * (size_t)v->len);
    if (!pcm) return;
    int32_t h1 = 0, h2 = 0;
    for (int b = 0; b < z->nblk; b++) block(z->base + 16 * b, &h1, &h2, pcm + 28 * b);
    for (int b = z->loop_blk; v->looped && b < z->nblk; b++)
        block(z->base + 16 * b, &h1, &h2, pcm + v->first_len + 28 * (b - z->loop_blk));
    pool_used[z - pool] = 0;
    v->pcm = pcm;
}

/* P32: the PS2 recordings sit below the Dreamcast common SFX that dcse puts
 * in, so every sample dcse did not replace gets this boost. */
#define PSP_PS2_GAIN 2.239f   /* +7 dB */
#define NDC 256
static const VagPcm *dc_smp[NDC];   /* samples now holding Dreamcast PCM */

int __wrap_dcse_apply(HdBank *b)
{
    enum { NM = sizeof DCSE_MAP / sizeof DCSE_MAP[0] };
    const int16_t *was[NM];
    for (int k = 0; k < NDC; k++)   /* forget this bank's old marks */
        if (b && dc_smp[k] >= b->vag && dc_smp[k] < b->vag + b->nvag) dc_smp[k] = NULL;
    for (size_t r = 0; r < NM; r++) {
        was[r] = NULL;
        if (!dcse_loaded() || !b || !b->vag || DCSE_MAP[r].vag >= b->nvag) continue;
        VagPcm *v = &b->vag[DCSE_MAP[r].vag];
        if (!v->looped && (uint32_t)v->first_len == DCSE_MAP[r].ps2_len && v->rate == DCSE_MAP[r].ps2_hz)
            materialize(v);
        was[r] = v->pcm;
    }
    int ret = __real_dcse_apply(b);
    for (size_t r = 0; r < NM; r++) {
        if (!was[r]) continue;
        const VagPcm *v = &b->vag[DCSE_MAP[r].vag];
        if (v->pcm == was[r]) continue;
        for (int k = 0; k < NDC; k++) if (!dc_smp[k]) { dc_smp[k] = v; break; }
    }
    return ret;
}

float vag_psp_gain(const VagPcm *v)
{
    for (int k = 0; k < NDC; k++) if (dc_smp[k] == v) return 1.0f;
    return PSP_PS2_GAIN;
}

/* ---- decoding while playing (mix_psp.c) ---- */
static void dec_next(VagDec *d)
{
    const VagLazy *z = d->z;
    int lb = d->blk + 1;
    int pb = lb < z->nblk ? lb : z->loop_blk + (lb - z->nblk);
    if (d->blk >= 0) { memcpy(d->buf, d->buf + 28, 28 * sizeof(int16_t)); d->prev = 1; }
    block(z->base + 16 * pb, &d->h1, &d->h2, d->buf + 28);
    d->blk = lb;
    if (lb == z->nblk - 1) {
        d->e_ok = 1; d->e1 = d->h1; d->e2 = d->h2;
        memcpy(d->e, d->buf + 28, sizeof d->e);
    }
}

static void dec_seek(VagDec *d, const VagLazy *z, int lb)
{
    if (d->z != z || d->gen != z->gen) {
        d->z = z; d->gen = z->gen; d->e_ok = 0; d->blk = -2;
    }
    if (d->blk >= -1 && (lb == d->blk || (lb == d->blk - 1 && d->prev) || lb > d->blk)) {
        while (d->blk < lb) dec_next(d);
        return;
    }
    if (lb >= z->nblk && d->e_ok) {   /* loop restart: history from the end block */
        d->blk = z->nblk - 1; d->prev = 0;
        d->h1 = d->e1; d->h2 = d->e2;
        memcpy(d->buf + 28, d->e, sizeof d->e);
    } else {
        d->blk = -1; d->prev = 0; d->h1 = d->h2 = 0;
    }
    while (d->blk < lb) dec_next(d);
}

int vag_psp_window(VagDec *d, const VagLazy *z, int lo, int hi)
{
    int lb = lo / 28, hb = hi / 28;
    if (hb - lb > 1) return -1;
    dec_seek(d, z, hb);   /* hb is now d->blk, or d->blk - 1 with prev */
    if (lb < d->blk && !d->prev) return -1;
    return lo - 28 * (d->blk - 1);
}

int16_t vag_psp_get(VagDec *d, const VagPcm *v, const VagLazy *z, int i)
{
    if (i < 0) return 0;
    if (i >= v->len) {
        if (!v->looped) return 0;
        int body = v->len - v->loop_start;
        i = v->loop_start + (i - v->loop_start) % body;
    }
    int lb = i / 28;
    dec_seek(d, z, lb);
    if (lb == d->blk) return d->buf[28 + i % 28];
    return d->buf[i % 28];   /* lb == blk - 1 with prev */
}
