/* PSP EE heap (P55): boot.c's sbrk (func_001044E0) and its base/top
 * accessors, linked over with --wrap, over a 23 MB heap instead of 28 MB.
 * The game's malloc takes its 22 MB arena first and breaks only ~22.5 MB in
 * a full arcade run (P54); the margin is 0.5 MB.  A refused growth is
 * logged, since the game's malloc then returns NULL. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PSP_EE_HEAP (23u << 20)
static uint8_t *ee_heap, *ee_brk, *ee_peak;

static uint8_t *base(void) { return (uint8_t *)(((uintptr_t)ee_heap + 63) & ~(uintptr_t)63); }

void *__wrap_func_001044E0(int increment)
{
    uint8_t *old;
    if (!ee_heap) {
        ee_heap = (uint8_t *)calloc(1, PSP_EE_HEAP + 64);
        if (!ee_heap)
            return (void *)-1;
        ee_brk = ee_peak = base();
    }
    if (increment < 0 ? (size_t)-increment > (size_t)(ee_brk - base())
                      : (size_t)increment > (size_t)(ee_heap + PSP_EE_HEAP - ee_brk)) {
        fprintf(stderr, "[mem] EE heap: sbrk(%d) refused at +%u KB of %u KB\n", increment,
                (unsigned)((ee_brk - base()) >> 10), PSP_EE_HEAP >> 10);
        return (void *)-1;
    }
    old = ee_brk;
    ee_brk += increment;
    if (ee_brk > ee_peak) ee_peak = ee_brk;
    return old;
}

uint8_t *__wrap_host_ee_heap_top(void) { return ee_brk; }
uint8_t *__wrap_host_ee_heap_base(void) { return ee_heap ? base() : NULL; }
unsigned psp_ee_heap_peak_kb(void) { return ee_heap ? (unsigned)((ee_peak - base()) >> 10) : 0; }
