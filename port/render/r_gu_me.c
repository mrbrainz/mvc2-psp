/* P104: mesh submit GE words without sceGu (see r_gu_me.h).  The words and
 * their order are r_gu.c mesh_emit's: texture bind or texture off,
 * blend/depth, VIEW/MODEL identity once per list, texscale/offset, clip
 * planes off, projection, draw; each state group skipped when the GE
 * already holds it.  MVC2_ME_CHECK compares them with sceGu's.
 *
 * This unit also runs on the Media Engine: no libc, no static data, no
 * jump tables or floats (built -ffreestanding -fno-jump-tables); the ME
 * reaches the items, the shared words and the output through kseg1. */
#include <pspgu.h>
#include "r_gu_me.h"

#define W(cmd, v) ((uint32_t)(cmd) << 24 | ((uint32_t)(v) & 0xFFFFFFu))
#define F(cmd, bits) ((uint32_t)(cmd) << 24 | (bits) >> 8)

static int same(const uint32_t *a, const uint32_t *b, int n)
{
    for (int k = 0; k < n; k++) if (a[k] != b[k]) return 0;
    return 1;
}
static void put(uint32_t *d, const uint32_t *s, int n) { for (int k = 0; k < n; k++) d[k] = s[k]; }
static uint32_t ge_exp(uint32_t v) { uint32_t i = 9; while (i > 0 && !((v >> i) & 1)) i--; return i; }

int gume_build(const MeItem *it, unsigned n, unsigned flags, MeShadow *s, volatile uint32_t *o)
{
    volatile uint32_t *b = o;
    if (flags & MJ_NEWLIST) s->clip_off = s->ts_ok = s->proj_ok = s->ident_ok = 0;
    if (flags & MJ_RESET_BD) s->bd_ok = 0;
    for (unsigned i = 0; i < n; i++, it++) {
        uint32_t f = it->flags;
        if (!(f & MI_TEX)) {
            *o++ = W(0x1E, 0);
        } else if (f & MI_BIND) {
            uint32_t swz = (f & MI_SWZ) ? 1 : 0, filt = (f & MI_NEAREST) ? GU_NEAREST : GU_LINEAR;
            uint32_t wrap = (f & MI_CLAMP) ? GU_CLAMP : GU_REPEAT;
            *o++ = W(0x1E, 1);
            if (f & MI_IDX) {
                *o++ = W(0xC5, GU_PSM_8888 | 0xFF << 8);
                *o++ = W(0xB0, it->clut);
                *o++ = W(0xB1, (it->clut >> 8) & 0xF0000u);
                *o++ = W(0xC4, 256 / 8);
                *o++ = W(0xC2, swz);
                *o++ = W(0xC3, GU_PSM_T8);
                *o++ = W(0xCB, 0);   /* sceGuTexMode's flush */
            } else {
                *o++ = W(0xC2, swz);
                *o++ = W(0xC3, GU_PSM_8888);
                *o++ = W(0xCB, 0);
            }
            *o++ = W(0xA0, it->tbp);
            *o++ = W(0xA8, ((it->tbp >> 8) & 0xF0000u) | it->pw);
            *o++ = W(0xB8, ge_exp(it->ph) << 8 | ge_exp(it->pw));
            *o++ = W(0xCB, 0);
            *o++ = W(0xC9, GU_TCC_RGBA << 8 | GU_TFX_MODULATE);
            *o++ = W(0xC6, filt << 8 | filt);
            *o++ = W(0xC7, wrap << 8 | wrap);
            *o++ = W(0xCB, 0);
        }
        if (!s->bd_ok || !same(it->bd, s->bd, 8)) {
            const uint32_t *d = it->bd;
            uint32_t z = d[6], fn = z == 0 ? GU_NEVER : z == 3 ? GU_GREATER : z == 7 ? GU_ALWAYS : GU_GEQUAL;
            if (d[0]) {
                *o++ = W(0x21, 1);
                *o++ = W(0xDF, d[2] | d[3] << 4 | d[1] << 8);
                *o++ = W(0xE0, d[4]);
                *o++ = W(0xE1, d[5]);
            } else {
                *o++ = W(0x21, 0);
            }
            *o++ = W(0x23, 1);
            *o++ = W(0xDE, fn);
            *o++ = W(0xE7, d[7] ? GU_FALSE : GU_TRUE);
            put(s->bd, d, 8); s->bd_ok = 1;
        }
        if (!s->ident_ok) {
            for (int m = 0; m < 2; m++) {
                *o++ = W(m ? 0x3A : 0x3C, 0);
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 3; c++) *o++ = F(m ? 0x3B : 0x3D, r == c ? 0x3F800000u : 0u);
            }
            s->ident_ok = 1;
        }
        if ((f & MI_TEX) && (!s->ts_ok || !same(it->ts, s->ts, 4))) {
            *o++ = F(0x48, it->ts[0]); *o++ = F(0x49, it->ts[1]);
            *o++ = F(0x4A, it->ts[2]); *o++ = F(0x4B, it->ts[3]);
            put(s->ts, it->ts, 4); s->ts_ok = 1;
        }
        if (!s->clip_off) { *o++ = W(0x1C, 0); s->clip_off = 1; }
        if (!s->proj_ok || !same(it->proj, s->proj, 16)) {
            *o++ = W(0x3E, 0);
            for (int k = 0; k < 16; k++) *o++ = F(0x3F, it->proj[k]);
            put(s->proj, it->proj, 16); s->proj_ok = 1;
        }
        *o++ = W(0x12, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D);
        *o++ = W(0x10, (it->iaddr >> 8) & 0xF0000u);
        *o++ = W(0x02, it->iaddr);
        *o++ = W(0x10, (it->vaddr >> 8) & 0xF0000u);
        *o++ = W(0x01, it->vaddr);
        *o++ = W(0x04, (uint32_t)GU_TRIANGLES << 16 | it->count);
    }
    *o++ = W(0x0B, 0);   /* RET */
    return (int)(o - b);
}

/* The ME's loop: jobs MS_JOBS + 4 * (k % MS_NJOB) = items, count, flags,
 * output (all kseg1), taken in order while MS_HEAD is ahead of MS_TAIL.
 * The shadow is the ME's own (its stack, kseg0). */
void gume_serve(volatile uint32_t *sh)
{
    MeShadow s;
    uint32_t tail = sh[MS_TAIL];
    s.bd_ok = s.clip_off = s.ts_ok = s.proj_ok = s.ident_ok = 0;
    for (;;) {
        sh[MS_BEAT]++;
        if (sh[MS_HEAD] != tail) {
            volatile uint32_t *j = sh + MS_JOBS + 4 * (tail % MS_NJOB);
            int w = gume_build((const MeItem *)j[0], j[1], j[2], &s, (volatile uint32_t *)j[3]);
            __asm__ volatile("sync" ::: "memory");   /* the words in RAM before the GE may run them */
            tail++;
            sh[MS_WORDS] += (uint32_t)w;
            sh[MS_TAIL] = tail;
            continue;
        }
        if (sh[MS_QUIT]) {
            void (*park)(void) = (void (*)(void))(uintptr_t)sh[MS_PARK];
            sh[MS_PARKED] = 1;
            __asm__ volatile("sync" ::: "memory");
            park();
        }
        for (int k = 0; k < 200; k++) __asm__ volatile("nop");   /* poll without touching the bus */
    }
}
