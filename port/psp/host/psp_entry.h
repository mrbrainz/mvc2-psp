/* Calls between the EABI32 module entry (psp_entry.c, psp-gcc) and the o32
 * game host (clang). Each takes <=4 int/pointer args (port/docs/PSP.md). */
#ifndef PSP_ENTRY_H
#define PSP_ENTRY_H
#include <stdio.h>
#include <sys/types.h>
void psp_log(const char *s);        /* EABI: debug screen + log file */
struct psp_fseeko_args { FILE *f; off_t off; int whence; };
int psp_fseeko_shim(const struct psp_fseeko_args *a);  /* EABI */
void psp_mem_report(const char *when);  /* EABI: kernel free memory to the log */
int mvc2_psp_run(int frames);       /* o32: boot + N frames */
#endif
