/* psp-gcc (EABI32): the sceGu calls with more than four arguments, for the
 * o32 renderer (port/render/r_gu.c).  Each takes one pointer to its
 * arguments, which both conventions pass in a0 (port/docs/PSP.md). */
#include <pspgu.h>
#include "psp_gu_shim.h"

void gu_blend(const GuBlendArgs *a)
{
    sceGuBlendFunc(a->op, a->src, a->dst, a->srcfix, a->dstfix);
}

void gu_draw(const GuDrawArgs *a)
{
    sceGuDrawArray(a->prim, a->vtype, a->count, a->indices, a->vertices);
}

void gu_teximage(const GuTexImageArgs *a)
{
    sceGuTexImage(a->mipmap, a->width, a->height, a->tbw, a->tbp);
}
