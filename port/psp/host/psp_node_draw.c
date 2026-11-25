/*
 * P98: the model node-tree draw (func_001538B0, src/sa34_e.c) and its
 * per-node material state (func_001537D0, src/v10_a.c), PSP-side.
 * Wrapped at link time (port/psp/CMakeLists.txt); src/ is unchanged.
 *
 * Per node the game sets the world matrix (state 0x19), then through
 * func_001537D0 -> func_00153A40 the blend states 0xD and 0x5D, and the UV
 * matrix 0x1A for some meshes.  Each goes through func_003DF850 (the game's
 * setter, then psp_fe_sync.c's renderer sync): ~14 us a node in character
 * select (~170 nodes a frame), I-cache bound.  Here the blend states are
 * skipped when they cannot change anything (the game's mode word already
 * holds them and the renderer is in sync), and the matrix sets are the
 * game's copy and set bit (plus the renderer's copy when it does not read
 * the game's, P96), without the setter's switch.  Same game state (oracle dump) and pictures as the game's bodies.
 *   MVC2_NODE_GAME=1  the game's bodies (A/B)
 */
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "../../render/render.h"

typedef float f32;

extern u32 D_004C2ABC;
extern u32 D_00444880[];
extern u32 D_004448A0[];
extern void func_003DE3A0(u8 *, u8 *);
extern void func_003DE160(u8 *, u8 *, u8 *);
extern void func_003DDAD0(f32 *, f32, f32, f32);
extern void func_003DDDC0(f32 *, f32, f32, f32);
extern void func_003DD260(f32 *);
extern void func_00153A40(s32, s32);
uint32_t r_state_ad0(void);
uint32_t r_state_ad4(void);
const float *r_gu_bound_matrix(int id);

#define AD0 EE_BSS(u32, 0x004C2AD0)       /* the game's mode word (states 0xD, 0x5D) */
#define AD4 EE_BSS(u32, 0x004C2AD4)
#define MSET EE_BSS(u32, 0x004C2AA0)      /* matrices set, bit id - 0x19 */
#define MTX(id) ((u8 *)EE_BSS_PTR(0x0061B8A0 + (id) * 64))

static int game = -1;

static int synced(void) { return r_state_ad0() == AD0 && r_state_ad4() == AD4; }
static inline void copy64(void *d, const void *s)   /* inline: libc memcpy is outside the hot block */
{
    u32 *x = (u32 *)d;
    const u32 *y = (const u32 *)s;
    for (int i = 0; i < 16; i++) x[i] = y[i];
}

/* func_003DF850(id, m) for a matrix state (0x19-0x38) as the game and
 * psp_fe_sync.c's wrap run it: the game's copy and set bit, the renderer's
 * copy unless it reads the game's */
static void set_matrix(int id, const void *m)
{
    copy64(MTX(id), m);
    MSET |= 1u << (id - 0x19);
    if (r_gu_bound_matrix(id) != (const float *)MTX(id))   /* bound (P96): the renderer reads the game's copy */
        fl_set_render_state(id, (uintptr_t)m);
    fl_sync_mode_words(AD0, AD4);
}

/* func_001537D0's body */
static inline void node_state(u32 a, u32 b, f32 x, f32 y)
{
    static f32 rot[16] __attribute__((aligned(16)));   /* func_003DDAD0(0, 0, 0) then func_003DDDC0(0, 0, 0) */
    static int rot_ok;
    s32 sa = 1, sb = 0;
    if (b & 0x100) { sa = (b >> 16) & 7; sb = (b >> 19) & 7; }
    /* func_00153A40: state 0xD = 0, then 0x5D = v; no-ops when the mode word holds them */
    if ((AD0 & 0xC00) || (AD0 & 0xFF) != (D_00444880[sa] | D_004448A0[sb]) || !synced())
        func_00153A40(sa, sb);
    if (b & 2) {   /* the UV matrix; the game clears it after each mesh */
        f32 m[16] __attribute__((aligned(16)));
        if (a & 0x20000) {
            /* the rotation by constant zero angles is the same every time: made once */
            if (!rot_ok) { func_003DDAD0(rot, 0.0f, 0.0f, 0.0f); func_003DDDC0(rot, 0.0f, 0.0f, 0.0f); rot_ok = 1; }
            copy64(m, rot);
            m[12] = x;
            m[13] = y;
        } else {
            func_003DD260(m);
        }
        set_matrix(0x1A, m);
    }
}

void __real_func_001537D0(u32 a, u32 b, f32 x, f32 y);
void __wrap_func_001537D0(u32 a, u32 b, f32 x, f32 y)
{
    if (game < 0) game = getenv("MVC2_NODE_GAME") != NULL;
    if (game) __real_func_001537D0(a, b, x, y);
    else node_state(a, b, x, y);
}

void __real_func_001538B0(u8 *o, u8 *n, u8 *q);
void __wrap_func_001538B0(u8 *o, u8 *n, u8 *q)
{
    u8 buf[0x40] __attribute__((aligned(16)));
    u8 *t = *(u8 **)(o + 0x84), *a2 = q;
    int first = 0;
    if (game < 0) game = getenv("MVC2_NODE_GAME") != NULL;
    if (game) { __real_func_001538B0(o, n, q); return; }
    for (;;) {
        if (!first) { first = 1; func_003DE3A0(buf, a2); }
        else func_003DE160(buf, n + 0x54, a2);
        set_matrix(0x19, buf);
        s32 k = *(s32 *)(n + 0x180);
        if (k != -1) {
            u8 *e = *(u8 **)(t + 0x14) + k * 180;
            if (*(s32 *)e != -1) {
                u32 c = D_004C2ABC;
                if (*(u32 *)(e + 4) & 0x200)
                    func_003DF850(0x66, (int)((((c >> 24) & 0xFF) >> 1) << 24 | (c & 0xFFFFFF)));
                node_state(*(u32 *)(o + 0xD0), *(u32 *)(e + 4), *(volatile f32 *)(o + 0xE0), *(f32 *)(o + 0xE4));
                func_003F0500(*(s32 *)e);   /* the game passes a second, unused 0 */
                if (*(u32 *)(e + 4) & 0x200) func_003DF850(0x66, (int)c);
            }
        }
        if (*(u8 **)(n + 0x10)) {
            n = *(u8 **)(n + 0x10);
            a2 = buf;
        } else {
            n = *(u8 **)(n + 0xC);
            if (!n) break;
            a2 = q;
        }
    }
}
