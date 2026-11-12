/* P84: the game's mesh draw body (func_003F0500, src/fb47_g.c) builds VU1
 * packets per draw: microcode setup, a vertex/UV packet, DMA tags, DMA
 * kicks. The PSP renderer (fe_mesh.c, r_gu_mesh.c) reads none of them: it
 * takes the mesh from its own clay and the matrices and lights from game
 * memory. This body keeps what the renderer and the game read back (the
 * format word and the packet rebuild when it changes, texture selection,
 * the globals the body sets) and leaves the per-draw packets out
 * (MVC2_PACKET_FULL=1: the game's own body). Wrapped at link time
 * (port/psp/CMakeLists.txt); src/ is unchanged. */
#include <stdlib.h>
#include "host.h"

typedef struct { u8 pad0[4]; s32 w4; s32 w8; s32 wC; s32 w10; s32 w14; } B40;
typedef struct {   /* as O3F0500 in src/fb47_g.c */
    u32 flags;
    u8 pad4[8];
    s32 wC;
    u8 pad10[0xC];
    s32 w1C;
    s32 w20;
    s32 w24;
    s32 w28;
    s32 w2C;
    u8 pad30[0xC];
    s32 w3C;
    B40 *w40;
    u8 pad44[0x2C];
    u8 b70[0x20];
    u8 padsp[0x170 - 0x90];
} Mesh;

extern void func_003DC9C0(void);
extern void func_003DD020(s32);
extern void func_003DD040(void);
extern void func_003EE930(Mesh *, u8 *);
extern s32 func_003EE8C0(Mesh *, u8 *);
int __real_func_003F0500__game(int h);

static int lite_body(int idx)
{
    Mesh *o = (Mesh *)EE_BSS_PTR(0x005BF2F0) + (idx - 1);
    u8 tmp[0x30];
    if (!(o->flags & 4) && o->wC != EE_BSS(s32, 0x004C2AD4)) {
        func_003DC9C0();
        o->wC = EE_BSS(s32, 0x004C2AD4);
        if (o->w1C != 0) func_003DD020(o->w1C);
        if (o->w20 != 0) func_003DD020(o->w20);
        if (o->w24 != 0) func_003DD020(o->w24);
        func_003EE930(o, tmp);
        if (func_003EE8C0(o, tmp) == 0) return 0;
        func_003DD040();
    }
    if (o->w2C != 0)
        func_003EA660(o->w2C, (u8 *)o + 0xF0);
    EE_BSS(s32, 0x0062BFC8) = (s32)((u32)(o->w40->wC - o->w40->w8) >> 3);
    EE_BSS(s32, 0x004C2AA0) = 0;
    EE_BSS(s32, 0x0062BFC4) = o->w3C;
    return 1;
}

int __wrap_func_003F0500__game(int h)
{
    static int lite = -1;
    extern int psp_prof_on(void);
    extern unsigned long long psp_prof_now(void);
    extern void psp_prof_add(int, const char *, unsigned long long);
    unsigned long long t = psp_prof_on() ? psp_prof_now() : 0;
    int r;
    if (lite < 0) lite = getenv("MVC2_PACKET_FULL") == NULL;
    r = lite ? lite_body(h) : __real_func_003F0500__game(h);
    if (t) psp_prof_add(17, "    func_003F0500__game (VU1 packet build)", psp_prof_now() - t);
    return r;
}
