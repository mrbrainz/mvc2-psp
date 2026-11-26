/* P99: func_00151A90 (src/nq238_b.c), the object matrix build that
 * func_00151A20 runs on each object of a list (character select list 6:
 * ~165 objects a frame).  Same steps and float expressions as the game's
 * body.  The rotation angles, (float)(2*pi * h / 65536) in the game's
 * soft-float doubles (four fp-bit calls each, three an object), and their
 * sin and cos (func_003DE720) come from a memo keyed by the u16 and the FPU
 * rounding mode, filled by the game's own calls.  The 4x4 products,
 * identities, translations and rotations are inline.
 *   MVC2_WALK_GAME=1   the game's body
 *   MVC2_WALK_CHECK=1  both bodies on each call, "walk check" lines report
 *                      calls whose matrix or D_004CB0F0/4/8 differ
 * Wrapped at link time (port/psp/CMakeLists.txt); src/ is unchanged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;
typedef float f32;
typedef struct { f32 m[16]; } M16;
typedef struct { f32 x, y, z; } V3;
typedef union { u32 w; u16 h; } WH;

extern s32 D_004C2658;
extern WH D_004CB0F0[], D_004CB0F4[], D_004CB0F8[];
extern f32 D_004F1F58[], D_004F1F5C[], D_004F1F60[];
extern s32 D_004F7FD8[];
extern double func_00119B08(s32);
extern double func_001195D0(double, double);
extern double func_00119858(double, double);
extern f32 dptofp(double);
extern void func_003DDB20(M16 *, f32, f32, f32);
extern void func_003DDB70(M16 *, f32);
extern void func_003DDC30(M16 *, f32);
extern void func_003DE720(f32, f32 *, f32 *);   /* sin, cos (the PSP glue ABI) */
extern f32 func_003DE6F0(f32);
extern int psp_fpu_nearest(void);
extern volatile long mvc2_host_frame;

#define AM_SLOTS 256
static struct { u32 key; f32 v, s, c; } am[AM_SLOTS];

/* the game's dptofp(2*pi * (double)h / 65536.0) and func_003DE720's sin
 * and cos of it (func_00117240 / func_00116FA0), memoized */
static const f32 *angle(u16 h)
{
    u32 key = 0x80000000u | ((u32)psp_fpu_nearest() << 16) | h;
    u32 s = (h ^ (h >> 8)) & (AM_SLOTS - 1);
    if (am[s].key != key) {
        am[s].v = dptofp(func_00119858(func_001195D0(6.283185307179586, func_00119B08(h)), 65536.0));
        func_003DE720(am[s].v, &am[s].s, &am[s].c);
        am[s].key = key;
    }
    return &am[s].v;
}

/* func_003DE160(a, a, b) (port/host/vu0_host.c): row i of a applied to b,
 * the same products and left-to-right sums, without the copies */
static inline void mm(M16 *a, const M16 *b)
{
    f32 r[16];
    const f32 *t = b->m;
    int i, j;
    for (i = 0; i < 4; i++) {
        const f32 *v = a->m + 4 * i;
        for (j = 0; j < 4; j++)
            r[4 * i + j] = t[j] * v[0] + t[4 + j] * v[1] + t[8 + j] * v[2] + t[12 + j] * v[3];
    }
    memcpy(a, r, sizeof r);
}

/* func_003DD260 (src/grok_w5_003c.c) */
static inline void ident(M16 *m)
{
    static const M16 I = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    *m = I;
}

/* func_003DDEC0(m, x, y, z) (src/wave5.c): func_003DDB70 (src/t14_a.c),
 * func_003DDC30 (src/v3_j.c), func_003DDD00 (src/t7_b.c) in turn, their
 * expressions as written; X, Y, Z point at {angle, sin, cos} */
static void rot3(M16 *b, const f32 *X, const f32 *Y, const f32 *Z)
{
    f32 *m = b->m, a, c, x, y, z;
    a = X[1]; c = X[2];
    y = m[1]; z = m[2];   m[1] = c * y - a * z;  m[2] = a * y + c * z;
    y = m[5]; z = m[6];   m[5] = c * y - a * z;  m[6] = a * y + c * z;
    y = m[9]; z = m[10];  m[9] = c * y - a * z;  m[10] = a * y + c * z;
    a = Y[1]; c = Y[2];
    x = m[0]; z = m[2];   m[0] = c * x + a * z;  m[2] = -a * x + c * z;
    x = m[4]; z = m[6];   m[4] = c * x + a * z;  m[6] = -a * x + c * z;
    x = m[8]; z = m[10];  m[8] = c * x + a * z;  m[10] = -a * x + c * z;
    a = Z[1]; c = Z[2];
    x = m[0]; y = m[1];   m[0] = x * c - y * a;  m[1] = x * a + y * c;
    x = m[4]; y = m[5];   m[4] = x * c - y * a;  m[5] = x * a + y * c;
    x = m[8]; y = m[9];   m[8] = x * c - y * a;  m[9] = x * a + y * c;
}

static void walk(u8 *p, s32 mode)
{
    M16 a, b;
    u32 fl = *(u32 *)(p + 0xD0);
    f32 g20, g21;

    if (D_004C2658 != 0) return;
    ident(&a);
    if (fl & 0x10) {
        ident(&b);
        func_003DDB20(&b, *(f32 *)(p + 0x50), *(f32 *)(p + 0x54), *(f32 *)(p + 0x58));
        mm(&a, &b);
    }
    if (fl & 0xE) {
        ident(&b);
        D_004CB0F0[0].w = *(u32 *)(p + 0x40);
        D_004CB0F4[0].w = *(u32 *)(p + 0x44);
        D_004CB0F8[0].w = *(u32 *)(p + 0x48);
        if (!(fl & 8)) D_004CB0F8[0].w = 0;
        if (!(fl & 4)) D_004CB0F4[0].w = 0;
        if (!(fl & 2)) D_004CB0F0[0].w = 0;
        {   /* copied out: two angles can share a memo slot */
            f32 X[3], Y[3], Z[3];
            memcpy(X, angle(D_004CB0F0[0].h), sizeof X);
            memcpy(Y, angle(D_004CB0F4[0].h), sizeof Y);
            memcpy(Z, angle(D_004CB0F8[0].h), sizeof Z);
            rot3(&b, X, Y, Z);
        }
        mm(&a, &b);
    }
    if (fl & 0x80) {
        g20 = D_004F1F5C[0] - *(f32 *)(p + 0x38);
        g21 = D_004F1F60[0] - *(f32 *)(p + 0x3C);
        ident(&b);
        func_003DDB70(&b, func_003DE6F0(-g20 / g21));
        mm(&a, &b);
    }
    if (fl & 0x100) {
        g20 = D_004F1F58[0] - *(f32 *)(p + 0x34);
        g21 = D_004F1F60[0] - *(f32 *)(p + 0x3C);
        ident(&b);
        func_003DDC30(&b, func_003DE6F0(g20 / g21));
        mm(&a, &b);
    }
    if (fl & 1) {
        u32 m = (u32)mode & 0xFF;
        V3 v = *(V3 *)(p + 0x34);
        int scaled = -1;
        switch (m) {
        case 91: case 52: case 43: case 40: case 38: case 36: case 34: case 13:
        case 16: case 14: case 18: case 7: case 6: case 5:
            scaled = 0;
            break;
        default:
            if ((m >= 8 && m <= 90) && m != 12 && m != 15) scaled = 1;
            break;
        }
        if (scaled >= 0) {
            if (scaled) { v.x *= 0.1f; v.y *= 0.1f; v.z *= 0.1f; }
            ident(&b);
            b.m[12] = v.x; b.m[13] = v.y; b.m[14] = v.z;   /* func_003DDB60 */
            mm(&a, &b);
        }
    }
    if (D_004F7FD8[0] == 1 && (*(u32 *)(p + 0xD0) & 0x10000)) {
        u32 m = (u32)mode & 0xFF;
        if ((m == 13 || m == 14 || m == 18) && *(s32 *)(p + 0xC8) == 0) {
            ident(&b);
            func_003DDB20(&b, 1.0f, 0.93f, 1.0f);
            mm(&a, &b);
        }
    }
    if (fl & 0x4000) {
        memcpy(&b, p + 0x88, sizeof b);
        mm(&a, &b);
    }
    if (*(s32 *)(p + 0xC8) != 0) {
        memcpy(&b, *(f32 **)(p + 0xC8), sizeof b);
        mm(&a, &b);
    }
    memcpy(p + 0x88, &a, sizeof a);
}

void __real_func_00151A90(u8 *p, s32 mode);
void __wrap_func_00151A90(u8 *p, s32 mode)
{
    static int cfg = -1;   /* 0 PSP-side, 1 game, 2 check */
    static long checks, bad;
    u8 in[64], out[64];
    u32 g[3];

    if (__builtin_expect(cfg < 0, 0))
        cfg = getenv("MVC2_WALK_CHECK") ? 2 : getenv("MVC2_WALK_GAME") ? 1 : 0;
    if (cfg == 0) { walk(p, mode); return; }
    if (cfg == 1) { __real_func_00151A90(p, mode); return; }

    memcpy(in, p + 0x88, 64);
    __real_func_00151A90(p, mode);
    memcpy(out, p + 0x88, 64);
    g[0] = D_004CB0F0[0].w; g[1] = D_004CB0F4[0].w; g[2] = D_004CB0F8[0].w;
    memcpy(p + 0x88, in, 64);
    walk(p, mode);
    checks++;
    if (memcmp(out, p + 0x88, 64) || g[0] != D_004CB0F0[0].w || g[1] != D_004CB0F4[0].w
        || g[2] != D_004CB0F8[0].w) {
        if (bad++ < 8)
            fprintf(stderr, "walk check: frame %ld mode %d fl %08X differs\n",
                    (long)mvc2_host_frame, (int)mode, *(u32 *)(p + 0xD0));
    }
    if ((checks & 0x3FFF) == 0)
        fprintf(stderr, "walk check: %ld calls, %ld differ\n", checks, bad);
}
