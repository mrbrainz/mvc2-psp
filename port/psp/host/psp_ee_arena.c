/* P75: live peaks of the game arena and its handle pool, for the [hwm]
 * lines.  boot.c's flInitialize (func_003EC160) mallocs the 22 MB arena,
 * sets up the bump allocator over it (ctl at EE 0x0062C020) and takes the
 * 6 MB handle pool from its top.
 *
 * The bump allocator (func_003F09C0): mode 0 grows cur up from start, mode 1
 * lowers lim; it only ever grows between resets, so its peak is its live
 * size.  The handle pool (func_003F0C60): handles in a 0x3000-entry table,
 * `total` live bytes, `cur` its extent (the game compacts it with
 * func_003F0E60).  Read-only: the wraps call through unchanged. */
#include <stdint.h>
#include <stdio.h>
#include "mvc2_stubs.h"
#include "host.h"

typedef struct { u32 base, start, end, cur, lim, align; } Bump;   /* lim: end less what mode 1 took */
typedef struct { s32 n; s32 cap; void *tab; s32 dir; u32 cur; u32 base; s32 align; u32 total; } Pool;

void *__real_func_003F09C0(Bump *h, u32 n, s32 commit);
u32 __real_func_003F0C60(Pool *a, u32 size, u32 align);

static u32 bump_peak, pool_live_peak, pool_ext_peak, pool_fails;

static Bump *ctl(void) { return (Bump *)EE_BSS_PTR(0x0062C020); }

void *__wrap_func_003F09C0(Bump *h, u32 n, s32 commit)
{
    void *r = __real_func_003F09C0(h, n, commit);
    if (h == ctl() && h->cur - h->start > bump_peak) bump_peak = h->cur - h->start;
    return r;
}

u32 __wrap_func_003F0C60(Pool *a, u32 size, u32 align)
{
    u32 r = __real_func_003F0C60(a, size, align);
    if (a == (Pool *)EE_BSS_PTR(0x005BF280)) {
        if (!r) pool_fails++;
        if (a->total > pool_live_peak) pool_live_peak = a->total;
        if (a->cur - a->base > pool_ext_peak) pool_ext_peak = a->cur - a->base;
    }
    return r;
}

/* P80: the fixed game heaps func_00414CB0 (src/hk11_a.c) carves from the
 * arena: pages ever written (the heap is calloc'd) and the highest one, and
 * for the 8 MB one a map of written KB per 256 KB */
static void heap_scan(const char *name, u32 ee, u32 n, int map)
{
    const uint8_t *a = (const uint8_t *)(uintptr_t)EE_BSS(u32, ee);
    u32 pages = 0, hi = 0, slice = 0;
    char line[512];
    int len = 0;
    if (!a) return;
    for (u32 o = 0; o + 4096 <= n; o += 4096) {
        const u32 *w = (const u32 *)(const void *)(a + o);
        u32 k = 0;
        while (k < 1024 && !w[k]) k++;
        if (k < 1024) { pages++; slice++; hi = o + 4096; }
        if (map && ((o + 4096) & 0x3FFFF) == 0) {
            len += snprintf(line + len, sizeof line - len, " %u", slice * 4);
            slice = 0;
        }
    }
    fprintf(stderr, "[hwm] heap %s: %u KB: written %u KB, highest %u KB%s%s\n", name, n >> 10,
            pages * 4, hi >> 10, map ? "; per 256 KB:" : "", map ? line : "");
}

void psp_ee_arena_report(void)
{
    const Bump *h = ctl();
    heap_scan("4C28BC", 0x004C28BC, 0x7E0000, 1);
    heap_scan("4C28B8", 0x004C28B8, 0x200000, 1);
    heap_scan("4C28B4", 0x004C28B4, 0x60000, 0);
    heap_scan("4C28B0", 0x004C28B0, 0x150000, 0);
    heap_scan("4C28AC", 0x004C28AC, 0x180000, 0);
    heap_scan("4C2878", 0x004C2878, 0x100000, 0);
    heap_scan("4C287C", 0x004C287C, 0xD0000, 0);
    fprintf(stderr, "[hwm] arena %u KB: bump peak %u KB of %u KB below the pool (%u KB never used); "
            "handle pool %u KB: live peak %u KB, extent peak %u KB, %u failed allocs\n",
            (h->end - h->start) >> 10, bump_peak >> 10, (h->lim - h->start) >> 10, (h->lim - h->start - bump_peak) >> 10,
            EE_BSS(u32, 0x0062BFD0) >> 10, pool_live_peak >> 10, pool_ext_peak >> 10, pool_fails);
}
