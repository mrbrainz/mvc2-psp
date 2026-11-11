/* PSP disc intake (o32): find the player's PS2 image on the memory stick and
 * hand it to the host as an FsSource read through sceIo (psp_disc_io.c).
 * Looked for, in order: DISC=path (env.txt), the game data packed into this
 * EBOOT.PBP's DATA.PSAR (port/psp/pack/psp_pack.py), the game folder, ms0:/ISO/. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "fs.h"
#include "host_cri.h"
#include "psp_disc_io.h"
#include "psp_entry.h"

typedef struct { int fd; uint32_t base; } SceSrc;
static SceSrc src_ctx;

static size_t sce_read_at(void *ctx, uint64_t off, void *dst, size_t n)
{
    const SceSrc *c = (const SceSrc *)ctx;
    off += c->base;
    PspDiscRead a = { c->fd, (unsigned)off, (unsigned)(off >> 32), dst, (unsigned)n };
    int r = psp_disc_io_read(&a);
    return r > 0 ? (size_t)r : 0;
}
static void sce_close(void *ctx) { psp_disc_io_close(((SceSrc *)ctx)->fd); }

/* An EBOOT.PBP's DATA.PSAR, when packed: base and size, 0 = none */
static uint32_t pbp_psar(int fd, uint32_t file_size, uint32_t *size)
{
    uint32_t h[10];
    PspDiscRead a = { fd, 0, 0, h, sizeof h };
    if (psp_disc_io_read(&a) != (int)sizeof h || h[0] != 0x50425000u) return 0;
    if (h[9] >= file_size) return 0;
    *size = file_size - h[9];
    return h[9];
}

static const char *const paths[] = {
    "ms0:/PSP/GAME/mvc2/mvc2.iso",
    "ms0:/ISO/mvc2.iso",
};

int psp_disc_open(void)
{
    const char *env = getenv("DISC");
    char b[200];
    for (int i = -2; i < (int)(sizeof paths / sizeof *paths); i++) {
        const char *p = i == -2 ? env : i == -1 ? psp_eboot_path : paths[i];
        unsigned lo, hi;
        int fd;
        if (!p || !*p) continue;
        fd = psp_disc_io_open(p, &lo, &hi);
        if (fd < 0) continue;
        src_ctx.fd = fd;
        src_ctx.base = 0;
        uint64_t size = (uint64_t)hi << 32 | lo;
        if (i == -1) {   /* the EBOOT itself: only with packed data */
            uint32_t n = 0;
            src_ctx.base = hi ? 0 : pbp_psar(fd, lo, &n);
            if (!src_ctx.base) { psp_disc_io_close(fd); continue; }
            size = n;
        }
        FsSource src = { &src_ctx, sce_read_at, sce_close, size };
        if (host_disc_open_source(src) == 0) {
            snprintf(b, sizeof b, "[host] disc: %s (%u MB)\n", p, (unsigned)(src.size >> 20));
            psp_log(b);
            return 0;
        }
        psp_disc_io_close(fd);   /* sceIo: harmless if the host already closed it */
        snprintf(b, sizeof b, "[host] %s is not a Marvel vs. Capcom 2 PS2 disc image\n", p);
        psp_log(b);
    }
    psp_log("[host] No game data. Pack your own MvC2 PS2 disc image into this\n"
            "       EBOOT.PBP (port/psp/pack/pack_eboot.sh), or copy the ISO to\n"
            "       ms0:/PSP/GAME/mvc2/mvc2.iso or ms0:/ISO/mvc2.iso\n");
    return -1;
}

void psp_disc_report(void)
{
    unsigned bytes, us;
    char b[96];
    psp_disc_io_stats(&bytes, &us);
    if (!bytes) return;
    snprintf(b, sizeof b, "[disc] %u KB in %u ms (%u KB/s)\n", bytes >> 10, us / 1000,
             us ? (unsigned)((unsigned long long)bytes * 1000000 / us >> 10) : 0);
    psp_log(b);
}

