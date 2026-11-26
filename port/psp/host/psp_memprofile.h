/* P74: boot-time memory profile (psp_memprofile.c) */
#ifndef PSP_MEMPROFILE_H
#define PSP_MEMPROFILE_H

typedef struct {
    const char *name;    /* "full" or "low" */
    unsigned ee_heap;    /* bytes: the EE heap the game's sbrk grows in (psp_ee_heap.c) */
} PspMemProfile;

/* chosen on the first call (before the boot, psp_main.c) and logged */
const PspMemProfile *psp_mem_profile(void);

#endif
