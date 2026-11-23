/* P101: call counts per function for the hot-routine ordering
 * (-DMVC2_PSP_HOTPROF=ON builds every o32 TU with
 * -finstrument-functions-after-inlining).  psp_hot_dump writes
 * <game dir>/hot.txt: "ref <runtime address of psp_hot_dump>", then one
 * "<runtime address> <calls>" line per function; port/psp/tools/hot_order.py
 * maps them to names with the ELF's symbols.  The audio thread's calls race
 * with the main thread's: counts may lose a few, a slot may appear twice
 * (the tool adds duplicates). */
#include <stdint.h>
#include <stdio.h>

#define NI __attribute__((no_instrument_function))
#define SLOTS (1u << 16)
static struct { uint32_t fn, n; } tab[SLOTS];

NI void __cyg_profile_func_enter(void *fn, void *site)
{
    uint32_t a = (uint32_t)(uintptr_t)fn, h = (a >> 2) * 2654435761u >> 16;
    (void)site;
    for (unsigned k = 0; k < SLOTS; k++, h = (h + 1) & (SLOTS - 1)) {
        if (tab[h].fn == a) { tab[h].n++; return; }
        if (!tab[h].fn) { tab[h].fn = a; tab[h].n = 1; return; }
    }
}
NI void __cyg_profile_func_exit(void *fn, void *site) { (void)fn; (void)site; }

NI void psp_hot_dump(void)
{
    extern const char *psp_game_dir(void);
    char path[256];
    FILE *f;
    snprintf(path, sizeof path, "%s/hot.txt", psp_game_dir());
    if (!(f = fopen(path, "w"))) return;
    fprintf(f, "ref %08x\n", (unsigned)(uintptr_t)&psp_hot_dump);
    for (unsigned k = 0; k < SLOTS; k++)
        if (tab[k].fn) fprintf(f, "%08x %u\n", (unsigned)tab[k].fn, (unsigned)tab[k].n);
    fclose(f);
}
