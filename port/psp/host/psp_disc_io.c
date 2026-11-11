/* sceIo disc reader (psp-gcc, EABI32): the ISO without newlib's stdio buffer.
 * Sequential reads skip the seek. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include "psp_disc_io.h"

static SceOff pos = -1;
static int pos_fd = -1;
static unsigned st_bytes, st_us;

int psp_disc_io_open(const char *path, unsigned *size_lo, unsigned *size_hi)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    SceOff sz;
    if (fd < 0) return -1;
    sz = sceIoLseek(fd, 0, PSP_SEEK_END);
    if (sz <= 0) { sceIoClose(fd); return -1; }
    *size_lo = (unsigned)sz;
    *size_hi = (unsigned)(sz >> 32);
    pos_fd = -1;
    return fd;
}

int psp_disc_io_read(const PspDiscRead *a)
{
    SceOff off = (SceOff)a->off_lo | (SceOff)a->off_hi << 32;
    unsigned t0 = sceKernelGetSystemTimeLow();
    int r;
    if (pos_fd != a->fd || pos != off) {
        if (sceIoLseek(a->fd, off, PSP_SEEK_SET) != off) { pos_fd = -1; return 0; }
        pos_fd = a->fd;
    }
    r = sceIoRead(a->fd, a->dst, a->n);
    if (r < 0) { pos_fd = -1; return 0; }
    pos = off + r;
    st_bytes += (unsigned)r;
    st_us += sceKernelGetSystemTimeLow() - t0;
    return r;
}

void psp_disc_io_close(int fd) { sceIoClose(fd); if (fd == pos_fd) pos_fd = -1; }

void psp_disc_io_stats(unsigned *bytes, unsigned *us)
{
    *bytes = st_bytes; *us = st_us;
    st_bytes = st_us = 0;
}
