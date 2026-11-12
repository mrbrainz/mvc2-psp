/* PSP: VAGs kept as SPU2 ADPCM in the bank's BD and decoded while they play
 * (P53). vag_psp.c wraps vag_decode/vag_free/dcse_apply; mix_psp.c reads
 * packed samples through VagDec. */
#ifndef PORT_AUDIO_VAG_PSP_H
#define PORT_AUDIO_VAG_PSP_H
#include "vag.h"

typedef struct VagLazy {
    const uint8_t *base;   /* first block in the BD */
    int nblk, loop_blk;
    uint32_t gen;          /* changes when the record is reused */
} VagLazy;

/* Per-voice decoder: the last two decoded blocks, contiguous. */
typedef struct VagDec {
    const VagLazy *z;
    uint32_t gen;
    int blk;               /* logical block in buf[28..55]; buf[0..27] holds blk-1 when prev */
    int prev;
    int32_t h1, h2;
    int e_ok;              /* end of the first pass saved (for loop restarts) */
    int32_t e1, e2;
    int16_t e[28];
    int16_t buf[56];
} VagDec;

const VagLazy *vag_psp_lazy(const VagPcm *v);   /* NULL: plain PCM */
/* Make samples [lo, hi] available in d->buf; returns buf index of sample lo,
 * or -1 if they do not fit (use vag_psp_get). Indices are in [0, len). */
int vag_psp_window(VagDec *d, const VagLazy *z, int lo, int hi);
/* buf index of 4 samples from lo when they are already decoded, else -1 */
static inline int vag_psp_fast(const VagDec *d, const VagLazy *z, int lo)
{
    int base = 28 * (d->blk - 1);
    if (d->z != z || d->gen != z->gen || lo < (d->prev ? base : base + 28) || lo + 3 >= base + 56) return -1;
    return lo - base;
}
int16_t vag_psp_get(VagDec *d, const VagPcm *v, const VagLazy *z, int i);
float vag_psp_gain(const VagPcm *v);   /* P32: 1 for Dreamcast SFX, boost for PS2 samples */

#endif
