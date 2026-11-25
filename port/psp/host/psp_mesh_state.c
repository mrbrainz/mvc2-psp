/*
 * P96: the mesh draw's render state, read where the game keeps it.
 *
 * Upstream fe_mesh.c hands the renderer the game's view, projection, world
 * and UV matrices, the clip matrix and three lights on every mesh draw
 * (8 calls, ~1.2 ms a fight frame on the PSP, cache-miss bound).  The PSP
 * build compiles a patched copy (port/psp/patches) that calls
 * psp_mesh_predraw() between the game's body and fl_clay_draw instead of
 * that whole stretch: the same state sync, texture re-sync and logs, in one
 * call, with the hand-off below.  The game's copies never move, so the
 * renderer is told their addresses once and reads them at draw time
 * (r_gu.c r_matrix, r_gu_mesh.c clip matrix, lights for lit draws only).
 * Nothing runs between the hand-off and fl_clay_draw but host logging, so
 * the draw reads the same bytes.
 *
 *   MVC2_MESH_HANDOFF=1  the upstream per-draw hand-off (A/B)
 *   MVC2_MESH_CHECK=1    snapshot the state at the hand-off and compare it
 *                        when the draw starts; logs "mesh check" lines
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "host.h"
#include "../../render/render.h"

void r_gu_mesh_bind(const float *view, const float *proj, const float *world, const float *uv,
                    const float *clip, const void *l0, const void *l1, const void *l2);
extern void (*r_gu_mesh_check)(void);
extern volatile long mvc2_host_frame;

static const struct { uint32_t ee, n; } src[8] = {
    { 0x0061C720, 64 }, { 0x0061C6E0, 64 }, { 0x0061BEE0, 64 }, { 0x0061BF20, 64 },
    { 0x0061BDE0, 64 }, { 0x0061B2F0, 100 }, { 0x0061B354, 100 }, { 0x0061B3B8, 100 },
};
static uint8_t snap[8][100];
static long checks, bad;

static void check_draw(void)
{
    int k, diff = 0;
    for (k = 0; k < 8; k++)
        diff |= memcmp(snap[k], EE_BSS_PTR(src[k].ee), src[k].n) != 0;
    checks++;
    if (diff && bad++ < 20)
        fprintf(stderr, "mesh check: frame %ld: state changed between hand-off and draw\n", (long)mvc2_host_frame);
    if (checks % 20000 == 0)
        fprintf(stderr, "mesh check: %ld draws, %ld changed\n", checks, bad);
}

#define COLD __attribute__((noinline, cold))   /* P98: off the per-draw path (I-cache) */
#define RARE(x) __builtin_expect(!!(x), 0)

static int handoff_mode = -1;   /* 0 bound, 1 upstream hand-off */
COLD static void handoff_init(void)
{
    {
        handoff_mode = getenv("MVC2_MESH_HANDOFF") != NULL;
        if (!handoff_mode)
            r_gu_mesh_bind((const float *)EE_BSS_PTR(0x0061C720), (const float *)EE_BSS_PTR(0x0061C6E0),
                           (const float *)EE_BSS_PTR(0x0061BEE0), (const float *)EE_BSS_PTR(0x0061BF20),
                           (const float *)EE_BSS_PTR(0x0061BDE0), EE_BSS_PTR(0x0061B2F0),
                           EE_BSS_PTR(0x0061B354), EE_BSS_PTR(0x0061B3B8));
        if (getenv("MVC2_MESH_CHECK"))
            r_gu_mesh_check = check_draw;
    }
}

COLD static void handoff_snap(void)
{
    for (int k = 0; k < 8; k++)
        memcpy(snap[k], EE_BSS_PTR(src[k].ee), src[k].n);
}

/* MVC2_MESH_HANDOFF=1: the upstream per-draw hand-off */
COLD static void handoff_upstream(void)
{
    fl_set_render_state(0x16, (uintptr_t)EE_BSS_PTR(0x0061C720));
    fl_set_render_state(0x17, (uintptr_t)EE_BSS_PTR(0x0061C6E0));
    fl_set_render_state(0x19, (uintptr_t)EE_BSS_PTR(0x0061BEE0));
    fl_set_render_state(0x1A, (uintptr_t)EE_BSS_PTR(0x0061BF20));
    fl_set_clip_matrix((const float *)EE_BSS_PTR(0x0061BDE0));
    fl_set_render_state(0x59, (uintptr_t)EE_BSS_PTR(0x0061B2F0));
    fl_set_render_state(0x5A, (uintptr_t)EE_BSS_PTR(0x0061B354));
    fl_set_render_state(0x5B, (uintptr_t)EE_BSS_PTR(0x0061B3B8));
}

static inline void psp_mesh_handoff(void)
{
    if (RARE(handoff_mode < 0)) handoff_init();
    if (RARE(r_gu_mesh_check != NULL)) handoff_snap();
    if (RARE(handoff_mode)) handoff_upstream();
}

/* fe_mesh.c func_003F0500 from fe_sync_state() to fl_clay_draw, as upstream:
 * fe_sync_state (fe_render.c sync_origin), the clay's textures through
 * fe_sync_handle, the hand-off, MVC2_MESH_DUMP and VERBOSE>1 logs */
void fe_sync_handle(u32 hnd);
static long dump_at = -2;

COLD static void predraw_dump(int h, u32 fmt)
{
    if (dump_at == -2) {
        const char *e = getenv("MVC2_MESH_DUMP");
        dump_at = e ? atol(e) : -1;
    }
    if (dump_at >= 0) {
        extern int fl_mesh_debug;
        fl_mesh_debug = dump_at == mvc2_host_frame;
        if (fl_mesh_debug) {
            const float *w = (const float *)EE_BSS_PTR(0x0061BEE0);
            host_log("clay %d fmt %08X world %.3f %.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f | t %.2f %.2f %.2f",
                     h, (unsigned)fmt, w[0], w[1], w[2], w[4], w[5], w[6], w[8], w[9], w[10], w[12], w[13], w[14]);
        }
    }
}

COLD static void predraw_verbose(int h, u32 fmt)
{
    static long last = -1;
    if (mvc2_host_frame % 60 == 0 && last != mvc2_host_frame) {
        const float *v = (const float *)EE_BSS_PTR(0x0061C720), *pj = (const float *)EE_BSS_PTR(0x0061C6E0),
                    *w = (const float *)EE_BSS_PTR(0x0061BEE0);
        last = mvc2_host_frame;
        host_log("mesh %d frame %ld fmt %08X", h, (long)mvc2_host_frame, (unsigned)fmt);
        for (int k = 0; k < 4; k++)
            host_log("  view %8.3f %8.3f %8.3f %8.3f | proj %8.3f %8.3f %8.3f %8.3f | world %8.3f %8.3f %8.3f %8.3f",
                     v[4 * k], v[4 * k + 1], v[4 * k + 2], v[4 * k + 3], pj[4 * k], pj[4 * k + 1], pj[4 * k + 2],
                     pj[4 * k + 3], w[4 * k], w[4 * k + 1], w[4 * k + 2], w[4 * k + 3]);
    }
}

void psp_mesh_predraw(int h, u32 fmt)
{
    uint32_t tx[64];
    int n;
    fl_set_origin(EE_BSS(s32, 0x0062BF8C), EE_BSS(s32, 0x0062BF90));
    fl_sync_mode_words(EE_BSS(u32, 0x004C2AD0), EE_BSS(u32, 0x004C2AD4));
    n = fl_clay_textures(h, tx, 64);
    for (int k = 0; k < n; k++)
        fe_sync_handle(tx[k]);
    psp_mesh_handoff();
    if (RARE(dump_at != -1)) predraw_dump(h, fmt);
    if (RARE(host_verbose > 1)) predraw_verbose(h, fmt);
}
