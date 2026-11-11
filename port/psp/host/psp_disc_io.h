/* Disc image reads through sceIo (psp_disc_io.c, EABI) for the o32 host.
 * Offsets and sizes go as lo/hi words: 64-bit arguments differ between the
 * ABIs. Every call takes <=4 int/pointer args. */
#ifndef PSP_DISC_IO_H
#define PSP_DISC_IO_H
typedef struct { int fd; unsigned off_lo, off_hi; void *dst; unsigned n; } PspDiscRead;
int psp_disc_io_open(const char *path, unsigned *size_lo, unsigned *size_hi);  /* fd, < 0 = none */
int psp_disc_io_read(const PspDiscRead *a);   /* bytes read */
void psp_disc_io_close(int fd);
void psp_disc_io_stats(unsigned *bytes, unsigned *us);  /* since the last call */
#endif
