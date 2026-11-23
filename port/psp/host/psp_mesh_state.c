/*
 * P96: the mesh draw's render state, read where the game keeps it.
 *
 * Upstream fe_mesh.c hands the renderer the game's view, projection, world
 * and UV matrices, the clip matrix and three lights on every mesh draw
 * (8 calls, ~1.2 ms a fight frame on the PSP, cache-miss bound).  The PSP
 * build compiles a patched copy (port/psp/patches) that calls
 * psp_mesh_handoff() there instead.  The game's copies never move, so the
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

void psp_mesh_handoff(void)
{
    static int mode = -1;   /* 0 bound, 1 upstream hand-off */
    if (mode < 0) {
        mode = getenv("MVC2_MESH_HANDOFF") != NULL;
        if (!mode)
            r_gu_mesh_bind((const float *)EE_BSS_PTR(0x0061C720), (const float *)EE_BSS_PTR(0x0061C6E0),
                           (const float *)EE_BSS_PTR(0x0061BEE0), (const float *)EE_BSS_PTR(0x0061BF20),
                           (const float *)EE_BSS_PTR(0x0061BDE0), EE_BSS_PTR(0x0061B2F0),
                           EE_BSS_PTR(0x0061B354), EE_BSS_PTR(0x0061B3B8));
        if (getenv("MVC2_MESH_CHECK"))
            r_gu_mesh_check = check_draw;
    }
    if (r_gu_mesh_check)
        for (int k = 0; k < 8; k++)
            memcpy(snap[k], EE_BSS_PTR(src[k].ee), src[k].n);
    if (!mode)
        return;
    fl_set_render_state(0x16, (uintptr_t)EE_BSS_PTR(0x0061C720));
    fl_set_render_state(0x17, (uintptr_t)EE_BSS_PTR(0x0061C6E0));
    fl_set_render_state(0x19, (uintptr_t)EE_BSS_PTR(0x0061BEE0));
    fl_set_render_state(0x1A, (uintptr_t)EE_BSS_PTR(0x0061BF20));
    fl_set_clip_matrix((const float *)EE_BSS_PTR(0x0061BDE0));
    fl_set_render_state(0x59, (uintptr_t)EE_BSS_PTR(0x0061B2F0));
    fl_set_render_state(0x5A, (uintptr_t)EE_BSS_PTR(0x0061B354));
    fl_set_render_state(0x5B, (uintptr_t)EE_BSS_PTR(0x0061B3B8));
}
