/* sceGu calls with more than four arguments, shimmed for o32 callers
 * (psp_gu_shim.c is EABI). */
#ifndef PSP_GU_SHIM_H
#define PSP_GU_SHIM_H
typedef struct { int op, src, dst; unsigned srcfix, dstfix; } GuBlendArgs;
typedef struct { int prim, vtype, count; const void *indices, *vertices; } GuDrawArgs;
typedef struct { int mipmap, width, height, tbw; const void *tbp; } GuTexImageArgs;
void gu_blend(const GuBlendArgs *a);
void gu_draw(const GuDrawArgs *a);
void gu_teximage(const GuTexImageArgs *a);
#endif
