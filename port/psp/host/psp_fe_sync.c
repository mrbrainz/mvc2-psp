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
#include <stdint.h>

extern volatile long mvc2_host_frame;
void __real_fe_sync_handle(unsigned h);

#define SLOTS 1024   /* direct-mapped by handle; a collision just syncs again */
static struct { unsigned h; long frame; } seen[SLOTS];

/* P85: the real sync hashes the texture and palette at a stride (fe_render.c
 * sample_sig, ~1 KB each) and converts them when the hash moved; on hardware
 * those strided texture reads cost ~225 us a handle (a cache miss a byte),
 * ~6 ms a fight frame, and uncached reads are no faster. Here both
 * descriptors, the palette samples and the texture's last byte are checked
 * every frame, and a quarter of the texture samples in turn (all of them
 * every 4 frames); the real sync runs when any of that changed. A texture
 * rewritten without the game's unlock call (func_003EA070, which syncs at
 * once) can so show up to 4 frames late; palettes and unlocks are as before. */
extern unsigned char mvc2_ee_bss[];
extern int func_003DD030(int);
#define SYNC_BSS(a) (mvc2_ee_bss + ((unsigned)(a) - 0x004C2580u))
#define CK_SLOTS 0x800
#define PARTS 4
static struct { unsigned h, fixed, part[PARTS]; int ok; } ck[CK_SLOTS];

static unsigned fnv(unsigned x, const unsigned char *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++) x = (x ^ p[i]) * 16777619u;
    return x;
}

/* sample_sig's samples (fe_render.c); part >= 0: only every PARTS-th of them */
static unsigned samples(unsigned x, const unsigned char *p, unsigned n, int part)
{
    unsigned i, step;
    if (!p || !n) return x;
    step = n > 1024 ? (n / 1024) | 1 : 1;
    if (part < 0) {
        for (i = 0; i < n; i += step) x = (x ^ p[i]) * 16777619u;
        return (x ^ p[n - 1]) * 16777619u;
    }
    for (i = (unsigned)part * step; i < n; i += step * PARTS) x = (x ^ p[i]) * 16777619u;
    return x;
}

static const unsigned char *tex_src(unsigned ti, unsigned *size)
{
    const unsigned char *e = SYNC_BSS(0x006207A0) + (ti - 1) * 64;
    if (!*(const short *)(e + 0x2C) || !*(const int *)(e + 0x28)) return 0;
    *size = *(const unsigned *)(e + 8);
    return (const unsigned char *)(uintptr_t)(unsigned)func_003DD030(*(const int *)(e + 0x28));
}

/* everything but the texture's strided samples */
static unsigned check_fixed(unsigned h)
{
    unsigned ti = h & 0xFFFF, pi = h >> 16, x = 2166136261u ^ h, size;
    if (ti && ti < 0x181) {
        const unsigned char *src;
        x = fnv(x, SYNC_BSS(0x006207A0) + (ti - 1) * 64, 64);
        src = tex_src(ti, &size);
        x ^= (unsigned)(uintptr_t)src * 2654435761u;
        if (src && size) x = (x ^ src[size - 1]) * 16777619u;
    }
    if (pi && pi < 0x101) {
        const unsigned char *e = SYNC_BSS(0x0061C7A0) + (pi - 1) * 64;
        x = fnv(x, e, 64);
        if (*(const short *)(e + 0x2C) && *(const int *)(e + 0x28)) {
            const unsigned char *src = (const unsigned char *)(uintptr_t)(unsigned)func_003DD030(*(const int *)(e + 0x28));
            int n = *(const short *)(e + 0x18) * *(const short *)(e + 0x1A), cpsm = *(const int *)(e + 0x1C);
            if (n <= 0 || n > 256) n = 16;
            x ^= (unsigned)(uintptr_t)src * 2654435761u;
            x = samples(x, src, (unsigned)(cpsm == 2 ? n * 2 : n * 4), -1);
        }
    }
    return x;
}

static unsigned check_part(unsigned h, int part)
{
    unsigned ti = h & 0xFFFF, size;
    const unsigned char *src;
    if (!ti || ti >= 0x181 || !(src = tex_src(ti, &size))) return 0;
    return samples(2166136261u, src, size, part);
}

void __wrap_fe_sync_handle(unsigned h)
{
    unsigned k = (h ^ (h >> 16) * 31u) & (SLOTS - 1);
    unsigned c = (h * 2654435761u >> 21) & (CK_SLOTS - 1), fixed, pt;
    int part = (int)(mvc2_host_frame & (PARTS - 1));
    if (seen[k].h == h && seen[k].frame == mvc2_host_frame)
        return;
    seen[k].h = h;
    seen[k].frame = mvc2_host_frame;
    {
        static int exact = -1;   /* MVC2_EXACT_TEX_SYNC=1: every frame, as upstream */
        if (exact < 0) { extern char *getenv(const char *); exact = getenv("MVC2_EXACT_TEX_SYNC") != 0; }
        if (exact) { __real_fe_sync_handle(h); return; }
    }
    fixed = check_fixed(h);
    if (ck[c].ok && ck[c].h == h && ck[c].fixed == fixed) {
        pt = check_part(h, part);
        if (ck[c].part[part] == pt) return;   /* nothing checked has changed */
    }
    {
        extern int psp_prof_on(void);
        extern unsigned long long psp_prof_now(void);
        extern void psp_prof_add(int, const char *, unsigned long long);
        unsigned long long t = psp_prof_on() ? psp_prof_now() : 0;
        __real_fe_sync_handle(h);
        if (t) psp_prof_add(42, "      fe_sync_handle (real)", psp_prof_now() - t);
    }
    ck[c].h = h; ck[c].fixed = fixed; ck[c].ok = 1;
    for (int q = 0; q < PARTS; q++) ck[c].part[q] = check_part(h, q);
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

