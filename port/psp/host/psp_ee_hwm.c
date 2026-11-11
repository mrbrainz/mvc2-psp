/* EE heap high-water marks (P54), read-only.  boot.c allocates the 28 MB EE
 * heap zeroed and the game carves its 22 MB arena (first request) and 6 MB
 * handle pool from it, so a 4 KB page still all zero was never written: the
 * count of written pages and the highest written offset are the footprint
 * each region has ever needed.  Logged at the end of a run. */
#include <stdint.h>
#include <stdio.h>
#include "mvc2_stubs.h"
#include "host.h"

uint8_t *host_ee_heap_base(void);
uint8_t *host_ee_heap_top(void);

#define PG 4096u

static void scan(const char *name, const uint8_t *a, uint32_t n)
{
    uint32_t pages = 0, hi = 0;
    for (uint32_t o = 0; o + PG <= n; o += PG) {
        const uint32_t *w = (const uint32_t *)(const void *)(a + o);
        uint32_t k = 0;
        while (k < PG / 4 && !w[k]) k++;
        if (k < PG / 4) { pages++; hi = o + PG; }
    }
    fprintf(stderr, "[hwm] %s: %u KB at %p: written %u KB, highest written offset %u KB\n",
            name, n >> 10, (const void *)a, (pages * PG) >> 10, hi >> 10);
}

void psp_ee_hwm(void)
{
    uint8_t *base = host_ee_heap_base(), *top = host_ee_heap_top();
    if (!base) return;
    uint8_t *pool = EE_BSS(uint8_t *, 0x0062BFCC);
    uint32_t pool_n = EE_BSS(uint32_t, 0x0062BFD0);
    const uint32_t *ctl = (const uint32_t *)EE_BSS_PTR(0x0062C020);
    extern unsigned psp_ee_heap_peak_kb(void);
    fprintf(stderr, "[hwm] ee heap %p, break +%u KB (peak %u KB); arena ctl %08x %08x %08x %08x\n", (void *)base,
            (unsigned)((top - base) >> 10), psp_ee_heap_peak_kb(), (unsigned)ctl[0], (unsigned)ctl[1], (unsigned)ctl[2], (unsigned)ctl[3]);
    scan("ee heap (to break)", base, (uint32_t)(top - base));
    scan("arena (22 MB from heap base)", base, 0x1600000);
    if (pool && pool_n) scan("handle pool", pool, pool_n);
}
