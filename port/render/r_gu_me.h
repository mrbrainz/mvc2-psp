/* P104: mesh submit GE words built off sceGu (r_gu_me.c), on the Media
 * Engine or the main CPU.  An item is one recorded submit with everything
 * the main CPU resolved: texture/CLUT addresses, the depth-zoomed
 * projection (float bits), blend/depth state.  All fields are words. */
#ifndef R_GU_ME_H
#define R_GU_ME_H
#include <stdint.h>

enum { MI_TEX = 1, MI_BIND = 2, MI_IDX = 4, MI_SWZ = 8, MI_NEAREST = 16, MI_CLAMP = 32 };
enum { MJ_RESET_BD = 1, MJ_NEWLIST = 2 };   /* job flags: blend/depth key cleared; new list (GE state unknown) */

typedef struct MeItem {
    uint32_t proj[16];      /* float bits, depth zoom applied */
    uint32_t ts[4];         /* su, sv, ou, ov float bits (MI_TEX) */
    uint32_t vaddr, iaddr, count, flags;
    uint32_t tbp, pw, ph, clut;
    uint32_t bd[8];         /* blend_ok, op, src, dst, srcfix, dstfix, ztest, zwrite */
} MeItem;

typedef struct MeShadow {
    uint32_t bd_ok, clip_off, ts_ok, proj_ok, ident_ok;
    uint32_t bd[8], ts[4], proj[16];
} MeShadow;

/* words one item can take (bind 16, blend/depth 7, texscale 4, clip 1,
 * projection 17, draw 6), the per-list VIEW/MODEL words and the RET */
#define ME_ITEM_WORDS 51
#define ME_EXTRA_WORDS 27

/* writes the items' words and a RET to o; returns the word count */
int gume_build(const MeItem *it, unsigned n, unsigned flags, MeShadow *s, volatile uint32_t *o);

/* shared words (uncached on both sides) */
enum { MS_BEAT = 0, MS_HEAD = 16, MS_QUIT, MS_PARK, MS_JOBS = 32, MS_NJOB = 4, MS_TAIL = 64, MS_PARKED, MS_WORDS = 80 };
/* MS_QUIT set: the ME sets MS_PARKED and jumps to MS_PARK (psp_me_park, in its own memory) */
void gume_serve(volatile uint32_t *sh);   /* the ME's loop */
#endif
