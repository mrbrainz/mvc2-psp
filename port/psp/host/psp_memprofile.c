/* P74: boot-time memory profile.  The PSPSDK gives newlib's heap all user
 * memory but 1 MB at start-up (PSP_HEAP_SIZE_KB(-1024), psp_entry.c), so the
 * largest block malloc can get before the boot is the user memory actually
 * available: about 50 MB on a PSP-2000+ with MEMSIZE=1, about 23 MB on a
 * PSP-1000.  Below 32 MB the profile is low: the P73 budgets
 * (port/docs/PSP.md "PSP-1000 budget (P73)"); otherwise full, today's sizes.
 * The probe block is freed again (newlib trims it back to the heap). */
#include <stdio.h>
#include <stdlib.h>
#include "psp_memprofile.h"

#define MB (1u << 20)
#define STEP (64u << 10)

static const PspMemProfile full = { "full", 23 * MB };
static const PspMemProfile low = { "low", 33 * MB / 2 };   /* EE heap target 16.5 MB (P73); the arena needs ~20.9 MB live (P75) */
static const PspMemProfile *cur;
static unsigned avail_kb;

static unsigned probe_kb(void)
{
    unsigned lo = 0, hi = 64 * MB / STEP;   /* in steps; hi never fits */
    while (hi - lo > 1) {
        unsigned mid = (lo + hi) / 2;
        void *volatile p = malloc((size_t)mid * STEP);   /* volatile: clang drops an unused malloc/free pair as if it succeeded */
        if (p) { free(p); lo = mid; } else hi = mid;
    }
    return lo * (STEP >> 10);
}

const PspMemProfile *psp_mem_profile(void)
{
    if (!cur) {
        avail_kb = probe_kb();
        const char *e = getenv("MVC2_MEM_PROFILE");   /* testing: low or full, whatever the memory */
        if (e && (e[0] == 'l' || e[0] == 'f')) cur = e[0] == 'l' ? &low : &full;
        else cur = avail_kb < 32 * 1024 ? &low : &full;
        fprintf(stderr, "[mem] profile %s: largest malloc block %u KB at boot; EE heap %u KB\n",
                cur->name, avail_kb, cur->ee_heap >> 10);
    }
    return cur;
}
