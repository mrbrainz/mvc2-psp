/* Texture/palette sync once per frame per handle (PSP).  Upstream
 * fe_sync_handle hashes about 1 KB of the game's texture and palette data
 * at a stride on every mesh draw, to catch reloads; character select and
 * fights draw about 135 meshes a frame over a few handles, and on the PSP
 * those strided reads cost about 8 ms a frame.  Wrapped at link time
 * (port/psp/CMakeLists.txt): a handle already synced in this logic frame is
 * not hashed again, so a texture rewritten between two draws of one frame
 * shows its new contents from the next frame.  func_003EA660 (fe_render.c:
 * game body, then fe_sync_handle per record) calls it within its own file,
 * where --wrap does not reach, so it is wrapped too, with the same steps. */
#include <string.h>

extern volatile long mvc2_host_frame;
void __real_fe_sync_handle(unsigned h);

#define SLOTS 1024   /* direct-mapped by handle; a collision just syncs again */
static struct { unsigned h; long frame; } seen[SLOTS];

void __wrap_fe_sync_handle(unsigned h)
{
    unsigned k = (h ^ (h >> 16) * 31u) & (SLOTS - 1);
    if (seen[k].h == h && seen[k].frame == mvc2_host_frame)
        return;
    {
        extern int psp_prof_on(void);
        extern unsigned long long psp_prof_now(void);
        extern void psp_prof_add(int, const char *, unsigned long long);
        unsigned long long t = psp_prof_on() ? psp_prof_now() : 0;
        __real_fe_sync_handle(h);
        if (t) psp_prof_add(42, "      fe_sync_handle (real)", psp_prof_now() - t);
    }
    seen[k].h = h;
    seen[k].frame = mvc2_host_frame;
}

void func_003EA660__game(int n, void *list);
void __wrap_func_003EA660(int n, void *list)
{
    const unsigned *l = (const unsigned *)list;
    extern int psp_prof_on(void);
    extern unsigned long long psp_prof_now(void);
    extern void psp_prof_add(int, const char *, unsigned long long);
    unsigned long long t = psp_prof_on() ? psp_prof_now() : 0;
    func_003EA660__game(n, list);
    if (t) psp_prof_add(43, "      func_003EA660__game tex select", psp_prof_now() - t);
    for (int i = 0; i < n; i++)
        __wrap_fe_sync_handle(l[i]);
}

/* The sprite path (fe_render.c fe_sprite -> tex_ok) calls fe_sync_handle in
 * its own file, so the once-per-frame memo above never saw it: in the late
 * arcade fights (hundreds of textured sprites a frame) that re-hashing cost
 * about 10 ms a frame (P35). fe_render_init is wrapped to install a copy of
 * the ops with a PSP sprite routine: the same steps, through the memo. */
#include "host.h"
#include "host_render.h"
#include "../../render/render.h"
extern int fe_cnt[];
extern int func_003E0DB0(unsigned int, void *, void *, unsigned int);
void __real_fe_render_init(void);
static HostRenderOps psp_ops;

static int psp_sprite(const void *v)
{
    u8 t0[8], t1[8];
    u32 cur = EE_BSS(u32, 0x004C2AEC);
    if (func_003E0DB0(cur, t0, t1, EE_BSS(u32, 0x004C2AD0)) == 0) {
        fe_cnt[6]++;
        return 0;
    }
    __wrap_fe_sync_handle(cur);
    fl_set_render_state(4, cur);
    fl_set_origin(EE_BSS(s32, 0x0062BF8C), EE_BSS(s32, 0x0062BF90));
    fl_sync_mode_words(EE_BSS(u32, 0x004C2AD0), EE_BSS(u32, 0x004C2AD4));
    fe_cnt[2]++;
    return fl_sprite_tex((const FlSprite *)v);
}

void __wrap_fe_render_init(void)
{
    __real_fe_render_init();
    psp_ops = *host_render_ops;
    psp_ops.sprite_tex = psp_sprite;
    host_render_ops = &psp_ops;
}

/* func_003DF850 (fe_render.c) syncs texture ids 4-7 through its in-file
 * fe_sync_handle as well: in the Abyss fights that was another ~11 ms a
 * frame (P35). Same steps, through the memo (MVC2_LOG_STATE logging left
 * out). */
int func_003DF850__game(int id, int v);
int __wrap_func_003DF850(int id, int v)
{
    int r = func_003DF850__game(id, v);
    fl_set_render_state(id, (uintptr_t)(u32)v);
    fl_sync_mode_words(EE_BSS(u32, 0x004C2AD0), EE_BSS(u32, 0x004C2AD4));
    if (id >= 4 && id <= 7)
        __wrap_fe_sync_handle((u32)v);
    return r;
}

/* Textured triangles (func_003DF0C0: the ending, ~900 a frame) and text
 * quads (func_00420BF0) take the same in-file tex_ok path: same steps,
 * through the memo (P35). */
static int psp_tex_ok(void)
{
    u8 t0[8], t1[8];
    u32 cur = EE_BSS(u32, 0x004C2AEC);
    if (func_003E0DB0(cur, t0, t1, EE_BSS(u32, 0x004C2AD0)) == 0)
        return 0;
    __wrap_fe_sync_handle(cur);
    fl_set_render_state(4, cur);
    fl_set_origin(EE_BSS(s32, 0x0062BF8C), EE_BSS(s32, 0x0062BF90));
    fl_sync_mode_words(EE_BSS(u32, 0x004C2AD0), EE_BSS(u32, 0x004C2AD4));
    return 1;
}

int func_003DF0C0__game(void *v);
int __wrap_func_003DF0C0(void *v)
{
    int r = func_003DF0C0__game(v);
    if (r && psp_tex_ok()) {
        fl_tri_tex((const FlTri *)v);
        fe_cnt[0]++;
    } else
        fe_cnt[5]++;
    return r;
}

int func_00420BF0__game(void *writer, void *quad);
int __wrap_func_00420BF0(void *writer, void *quad)
{
    int r = func_00420BF0__game(writer, quad);
    if (r && psp_tex_ok()) {
        fl_quad_tex((const FlQuad *)quad);
        fe_cnt[3]++;
    } else
        fe_cnt[7]++;
    return r;
}
