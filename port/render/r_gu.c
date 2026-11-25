/* PSP renderer (sceGu): the 2D core of the "fl" render library (render.h),
 * for the PSP build only.  r_gl.c is the GL implementation and is not used
 * or changed; the GL-free game logic here (state setter, blend decode,
 * primitive set-up, texture/palette handle tables) follows it and must be
 * kept in step with it by hand.  Characters (fl_char_*) and meshes
 * (fl_model_*, fl_clay_*) are P24/P25.
 *
 * Compiled o32 (clang) like the rest of the host; sceGu is EABI, so the calls
 * with more than four arguments go through port/psp/host/psp_gu_shim.c.
 *
 * Differences from the GL path, by design or for now:
 *  - No 640x480 canvas: primitives are scaled straight into the 480x272 back
 *    buffer, fitting the displayed area (fl_set_display, 640x448 in game)
 *    with its aspect ratio kept (P23 owns presentation options).
 *  - Indexed textures use the GE's own CLUT (T8 + 256-entry 8888 palette),
 *    so bilinear filtering of them is the GE's (after the lookup), as GL's
 *    hand-written 4-tap filter also is.
 *  - 2D vertices are affine (GU_TRANSFORM_2D): the GS's perspective STQ on
 *    triangles with w != 1 is not reproduced.
 *  - Destination alpha (blend factors 4/5): the PSP keeps stencil in the
 *    frame buffer's alpha, so those modes are approximate.
 *  - Textures wider or taller than 512 are not drawn (logged once each). */
#include <malloc.h>
#include <math.h>
#include <pspgu.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "decode.h"
#include "../psp/host/psp_gu_shim.h"
#include "r_gu_me.h"

/* sceGu calls with > 4 args must go through the EABI shim */
#define sceGuBlendFunc use_gu_blend
#define sceGuDrawArray use_gu_draw
#define sceGuTexImage use_gu_teximage

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
extern int r_gu_char_swz;   /* r_gu_char.c (P109) */
#define MAX_TEX 384             /* the game's texture records (D_006207A0[384]), P77 */
#define MAX_PAL 256             /* its palette records (D_0061C7A0[256]), P77 */
#define MAX_VERTS 4096          /* per batch */
#define LIST_BYTES (1 << 20)    /* display list; restarted mid-frame when nearly full */

enum { RM_FLAT = 0, RM_RGBA = 1, RM_INDEX = 2, RM_RAW4 = 3 };

typedef struct GuVtx { float u, v; uint32_t c; float x, y, z; } GuVtx;   /* TEXTURE_32BITF|COLOR_8888|VERTEX_32BITF */
#define GU_VTYPE (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

typedef struct TexRec { uint8_t *pix; int w, h, fmt, used, pw, ph; uint8_t warned, swz; unsigned gen; uint32_t voff, vused; } TexRec;   /* voff: VRAM copy (P87), 0 none; vused: vr_epoch of its last use (P88); swz: pixels stored swizzled (P109) */
typedef struct PalRec { uint32_t *clut; int ncolors, used; unsigned gen; } PalRec;

static unsigned int __attribute__((aligned(16))) list[LIST_BYTES / 4];
static uint32_t vr_base, vr_next, vr_top;   /* P87 mesh texture VRAM cache (offsets) */
static void vr_frame(void);
static int vr_full, vr_off = -1, vr_pause, vr_drop;
static uint32_t mv_base, mv_size;   /* P108: mesh vertex pool, the top of VRAM (r_gu_mesh.c) */   /* pause: frames left without the cache (system dialog up); drop: reset at the next frame end */
static unsigned vr_epoch = 1;   /* presented frames (P88) */
static int is_index(int fmt);
static uint32_t __attribute__((aligned(16))) black_clut[256];

static struct {
    int inited, list_open, drawn_since_swap;
    TexRec tex[MAX_TEX];
    PalRec pal[MAX_PAL];
    GuVtx verts[MAX_VERTS];
    int nverts;
    /* batch key */
    unsigned list_gen;                /* lists closed so far: a record whose gen equals it is in the open list */
    int cur_tex, cur_mode, cur_pal;   /* tex/pal: record index, -1 none */
    const void *raw_pix; const uint32_t *raw_clut; int raw_w, raw_h;   /* RM_RAW4 (r_gu_char.c) */
    /* fl state (func_003DF850) */
    uint32_t state_ad0, state_ad4, cur_texture, tex_slots[4];
    uint32_t material_color, fog_color, global_mod, clear_color, state_5f;
    float fog_a, fog_b;
    float matrices[0x60][16];
    uint8_t materials[32][76];
    uint8_t lights[3][100];
    int vp_i[4];
    float vp_f[2];
    int ztest, zwrite;
    int blend_ok, bl_op, bl_src, bl_dst;
    unsigned bl_srcfix, bl_dstfix;
    int scissor[4];
    int origin_x, origin_y;
    int flip_clear, clear_pending, drawn;
    int disp_w, disp_h;
    float sx, sy, ox, oy;    /* game display area -> screen */
} R;


static int mem_tex, mem_ntex;   /* texture pixel bytes held, count */

/* ---------------- display list ---------------- */
/* Presentation (P23): FIT keeps the game's aspect (640x448 -> 389x272,
 * centred), STRETCH fills 480x272. COLOR16: 5650 frame buffers, dithered
 * (half the VRAM and fill bandwidth; set before r_init). */
static int present_stretch, present_c16;
void r_gu_set_present(int stretch, int color16) { present_stretch = stretch != 0; present_c16 = color16 != 0; }

static void set_mapping(void)
{
    int dw = R.disp_w > 0 ? R.disp_w : FL_SCREEN_W, dh = R.disp_h > 0 ? R.disp_h : FL_SCREEN_H;
    float s = (float)SCR_W / dw < (float)SCR_H / dh ? (float)SCR_W / dw : (float)SCR_H / dh;
    R.sx = R.sy = s;
    if (present_stretch) { R.sx = (float)SCR_W / dw; R.sy = (float)SCR_H / dh; }
    R.ox = (SCR_W - dw * R.sx) * 0.5f;
    R.oy = (SCR_H - dh * R.sy) * 0.5f;
}

/* switch FIT/STRETCH while running (the next frame's clear covers the bars) */
void r_gu_toggle_stretch(void)
{
    present_stretch = !present_stretch;
    set_mapping();
}
int r_gu_stretch(void) { return present_stretch; }

static void rec_build(void);   /* P103 */
static void me_wait_all(void), me_prof(void), me_diag(void), me_init(void), me_release(void);   /* P104 */
static int me_pend_n;
static void apply_scissor(void)
{
    rec_build();
    int x0 = (int)floorf(R.scissor[0] * R.sx + R.ox), y0 = (int)floorf(R.scissor[1] * R.sy + R.oy);
    int x1 = (int)ceilf((R.scissor[0] + R.scissor[2]) * R.sx + R.ox);
    int y1 = (int)ceilf((R.scissor[1] + R.scissor[3]) * R.sy + R.oy);
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    sceGuScissor(x0, y0, x1 - x0, y1 - y0);   /* PSPSDK: x, y, width, height */
}

/* P83: the last mesh texture binding (tex_bind) still in the GE: mesh draws
 * in a row with the same texture skip re-sending it. Anything else that can
 * change texture state or texture/CLUT memory clears it. */
static uint32_t mesh_tex_key[7];
static int mesh_tex_ok;
/* the same for blend + depth state (apply_blend_depth) */
static uint32_t mesh_bd_key[8];
static int mesh_bd_ok;

/* The GE runs a GU_DIRECT list while it is still being written and fetches
 * it from RAM, so commands and sceGuGetMemory data still in the CPU's data
 * cache would be read stale (a frame drawn with a wrong pixel format when
 * the memory layout moved, P85). The list is written through the uncached
 * mirror instead; its cache lines are dropped once first. */
static unsigned int *list_ptr(void)
{
    static int done;
    if (!done) { sceKernelDcacheWritebackInvalidateRange(list, sizeof list); done = 1; }
    return (unsigned int *)((uintptr_t)list | 0x40000000u);
}

static void list_open(void)
{
    if (R.list_open) return;
    mesh_tex_ok = mesh_bd_ok = 0;
    sceGuStart(GU_DIRECT, list_ptr());
    R.list_open = 1;
    sceGuEnable(GU_SCISSOR_TEST);
    apply_scissor();
}

int psp_prof_on(void);
int psp_prof_fine(void);
unsigned long long psp_prof_now(void);
void psp_prof_add(int slot, const char *label, unsigned long long us);

static void list_close(void)
{
    unsigned long long t = 0;
    if (!R.list_open) return;
    rec_build();
    me_wait_all();   /* P104: the ME's CALL blocks written, the stall released */
    me_prof();
    if (psp_prof_on()) t = psp_prof_now();
    sceGuFinish();
    me_diag();   /* P104 */
    sceGuSync(0, 0);
    if (t) { psp_prof_add(20, "  GE list run (sceGuSync)", psp_prof_now() - t); psp_prof_add(21, "  GE lists per frame (count, not ms)", 1000); }
    R.list_open = 0;
    R.list_gen++;
}

int r_init(void)
{
    memset(&R, 0, sizeof R);
    R.cur_tex = R.cur_pal = -1;
    R.vp_i[2] = FL_SCREEN_W; R.vp_i[3] = FL_SCREEN_H; R.vp_f[1] = 1.f;
    for (int i = 0x16; i < 0x39; i++)
        for (int k = 0; k < 16; k++) R.matrices[i][k] = (k % 5) == 0 ? 1.f : 0.f;
    R.global_mod = 0xFFFFFFFFu;
    R.fog_a = R.fog_b = 10000000.f;
    set_mapping();

    /* VRAM: draw + display 8888 (512x272 each; 5650 with COLOR16), depth 16-bit */
    sceGuInit();
    sceGuStart(GU_DIRECT, list_ptr());
    {
        int bpp = present_c16 ? 2 : 4;
        sceGuDrawBuffer(present_c16 ? GU_PSM_5650 : GU_PSM_8888, (void *)0, BUF_W);
        sceGuDispBuffer(SCR_W, SCR_H, (void *)(BUF_W * SCR_H * bpp), BUF_W);
        sceGuDepthBuffer((void *)(BUF_W * SCR_H * bpp * 2), BUF_W);
        vr_base = vr_next = (uint32_t)(BUF_W * SCR_H * bpp * 2 + BUF_W * SCR_H * 2);
        vr_top = 2u << 20;
        {   /* P108: MVC2_MESH_VRAM_KB (default 256, 0 = off) of it for mesh vertices */
            const char *e = getenv("MVC2_MESH_VRAM_KB");
            unsigned kb = e ? (unsigned)atoi(e) : 256u;
            if (kb > 512u) kb = 512u;
            mv_size = kb << 10;
            vr_top -= mv_size;
            mv_base = vr_top;
        }
        if (present_c16) {
            static const ScePspIMatrix4 dith = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };
            sceGuSetDither(&dith);
            sceGuEnable(GU_DITHER);
        }
    }
    sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(0, 65535);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xFF);   /* GS TEST: alpha != 0 */
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
    sceGuClearColor(0);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    R.inited = 1;
    fl_default_state();
    me_init();   /* P104 */
    return 0;
}

void r_shutdown(void)
{
    list_close();
    sceGuTerm();
    R.inited = 0;
}

/* The game's display list for this frame is finished: show it.  Called once
 * per display frame by the PSP runner (the platform's render callback). */
void r_gu_present(void)
{
    if (!R.inited) return;
    if (R.nverts) {
        extern void r_flush(void);
        r_flush();
    }
    list_close();
    vr_frame();
    {   /* the system Save / Load dialog draws over the finished frame (P59) */
        extern int psp_saveui_frame(void);
        if (psp_saveui_frame()) {
            R.drawn_since_swap = 1;
            vr_drop = 1; vr_pause = 2;   /* the dialog may use VRAM: drop the texture cache (P87) */
        }
    }
    if (R.drawn_since_swap) {
        sceGuSwapBuffers();
        R.drawn_since_swap = 0;
    }
}

/* ---------------- state application ---------------- */
static void apply_blend_depth(void)
{
    if (R.blend_ok) {
        GuBlendArgs b = { R.bl_op, R.bl_src, R.bl_dst, R.bl_srcfix, R.bl_dstfix };
        sceGuEnable(GU_BLEND);
        gu_blend(&b);
    } else {
        sceGuDisable(GU_BLEND);
    }
    sceGuEnable(GU_DEPTH_TEST);
    switch (R.ztest) {
    case 0: sceGuDepthFunc(GU_NEVER); break;
    case 3: sceGuDepthFunc(GU_GREATER); break;
    case 7: sceGuDepthFunc(GU_ALWAYS); break;
    default: sceGuDepthFunc(GU_GEQUAL); break;
    }
    sceGuDepthMask(R.zwrite ? GU_FALSE : GU_TRUE);   /* 1 = writes masked */
}

/* the clear due after a flip, before the first draw of the new frame */
static void canvas_bind(void)
{
    list_open();
    if (R.clear_pending) {
        rec_build();
        uint32_t c = R.clear_color;
        mesh_bd_ok = 0;   /* depth mask */
        sceGuScissor(0, 0, SCR_W, SCR_H);
        sceGuDepthMask(GU_FALSE);
        sceGuClearColor(0xFF000000u | (c & 0xFF) << 16 | (c & 0xFF00) | ((c >> 16) & 0xFF));
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        apply_scissor();
        R.clear_pending = 0;
    }
    R.drawn = 1;
    R.drawn_since_swap = 1;
}

static int texture_filter_linear(void) { return (R.state_ad0 & 0x10000) ? 0 : 1; }

/* texture + CLUT + filter/wrap for one record (the mesh path) */
/* P87: mesh textures are copied into the VRAM past the frame and depth
 * buffers on first use, so the GE does not read them over the main bus.
 * When it is full, textures stay in RAM until the next frame boundary
 * (GE idle), where the cache starts again. tex_copy drops a stale copy. */
static int vr_stat[5];   /* PROFILE: hits, copies, bytes copied, RAM binds, RAM KB */

static const void *tex_addr(TexRec *t)
{
    unsigned bytes = (unsigned)t->pw * (unsigned)t->ph * (is_index(t->fmt) ? 1u : 4u);
    if (vr_off < 0) { const char *e = getenv("MVC2_NO_VRAM_TEX"); vr_off = e != NULL; }
    if (vr_off || vr_pause || !vr_base || !t->pix) { vr_stat[3]++; vr_stat[4] += (int)bytes; return t->pix; }
    if (t->voff) { vr_stat[0]++; t->vused = vr_epoch; return (const void *)(uintptr_t)(0x04000000u + t->voff); }
    bytes = (bytes + 63u) & ~63u;
    if (vr_next + bytes > vr_top) { vr_full = 1; vr_stat[3]++; vr_stat[4] += (int)bytes; return t->pix; }
    memcpy((void *)(uintptr_t)(0x44000000u + vr_next), t->pix, bytes);   /* uncached VRAM */
    t->voff = vr_next;
    t->vused = vr_epoch;
    vr_next += bytes;
    vr_stat[1]++; vr_stat[2] += (int)bytes;
    return (const void *)(uintptr_t)(0x04000000u + t->voff);
}

/* frame boundary, list closed and run: start the cache again if it filled */
static void vr_frame(void)
{
    if (psp_prof_on()) {
        psp_prof_add(22, "  mesh tex VRAM hits (count)", 1000ull * (unsigned)vr_stat[0]);
        psp_prof_add(23, "  mesh tex VRAM copies (count)", 1000ull * (unsigned)vr_stat[1]);
        psp_prof_add(24, "  mesh tex VRAM KB copied (count)", 1000ull * (unsigned)(vr_stat[2] >> 10));
    }
    if (psp_prof_on()) {
        psp_prof_add(52, "  mesh tex RAM binds (count)", 1000ull * (unsigned)vr_stat[3]);
        psp_prof_add(53, "  mesh tex RAM KB bound (count)", 1000ull * (unsigned)(vr_stat[4] >> 10));
    }
    vr_stat[0] = vr_stat[1] = vr_stat[2] = vr_stat[3] = vr_stat[4] = 0;
    if (vr_pause) vr_pause--;
    /* P88: a full cache is kept while this frame used at least half of what
     * it holds (fights: the textures that fit stay, the rest come from RAM).
     * Resetting it whenever it was full recopied ~690 KB per shown frame on
     * the PSP-2003 (~7-8 ms) and still missed half the binds.  It resets when
     * most of it went unused (a new scene) or on request (system dialog). */
    static int reset_full = -1;   /* MVC2_VRAM_RESET=1: reset whenever full, as P87 (A/B) */
    if (reset_full < 0) reset_full = getenv("MVC2_VRAM_RESET") != NULL;
    if (vr_full && !vr_drop && !reset_full) {
        unsigned used = 0;
        for (int i = 0; i < MAX_TEX; i++) {
            const TexRec *t = &R.tex[i];
            if (t->voff && t->vused == vr_epoch)
                used += ((unsigned)t->pw * (unsigned)t->ph * (is_index(t->fmt) ? 1u : 4u) + 63u) & ~63u;
        }
        if (used * 2 >= vr_next - vr_base) vr_full = 0;
    }
    {   /* P108: the mesh vertex pool's own frame policy (r_gu_mesh.c) */
        extern void r_gu_mesh_vram_frame(int drop);
        r_gu_mesh_vram_frame(vr_drop);
    }
    vr_epoch++;
    if (!vr_full && !vr_drop) return;
    for (int i = 0; i < MAX_TEX; i++) R.tex[i].voff = 0;
    vr_next = vr_base;
    vr_full = vr_drop = 0;
}

static void tex_bind(const TexRec *t, int pal)
{
    GuTexImageArgs ti = { 0, t->pw, t->ph, t->pw, tex_addr((TexRec *)t) };
    ((TexRec *)t)->gen = R.list_gen;
    sceGuEnable(GU_TEXTURE_2D);
    if (t->fmt == FL_TEX_IDX4 || t->fmt == FL_TEX_IDX8) {
        const uint32_t *clut = (pal >= 0 && pal < MAX_PAL && R.pal[pal].used) ? R.pal[pal].clut : black_clut;
        if (clut != black_clut) R.pal[pal].gen = R.list_gen;
        sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
        sceGuClutLoad(256 / 8, clut);
        sceGuTexMode(GU_PSM_T8, 0, 0, t->swz);
    } else {
        sceGuTexMode(GU_PSM_8888, 0, 0, t->swz);
    }
    gu_teximage(&ti);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter((R.state_ad0 & 0x10000) ? GU_NEAREST : GU_LINEAR, (R.state_ad0 & 0x10000) ? GU_NEAREST : GU_LINEAR);
    sceGuTexWrap((R.state_ad0 & 0x20000) ? GU_CLAMP : GU_REPEAT, (R.state_ad0 & 0x20000) ? GU_CLAMP : GU_REPEAT);
    sceGuTexFlush();
}

/* P88: frame skip, as r_gl.c.  When the platform runs several logic steps
 * in one display frame, psp_main.c discards all but the last: the logic
 * runs, nothing reaches the GE.  Drawing them all left fights at ~4 frames
 * a second on the PSP-2003 (every step drawn, one in eight shown). */
static int r_discard;
void r_flush(void);
void r_set_discard(int on) { r_flush(); r_discard = on != 0; }
int r_discarding(void) { return r_discard; }

void r_flush(void)
{
    GuVtx *v;
    if (!R.nverts || !R.inited || r_discard) { R.nverts = 0; return; }
    rec_build();   /* P103: recorded mesh submits come first */
    mesh_tex_ok = mesh_bd_ok = 0;   /* the 2D batch sets its own texture, blend and depth state */
    canvas_bind();
    if (sceGuCheckList() > LIST_BYTES - (int)sizeof R.verts - 4096) {   /* nearly full */
        list_close();
        list_open();
    }
    if (R.cur_mode == RM_FLAT || (R.cur_tex < 0 && R.cur_mode != RM_RAW4)) {
        sceGuDisable(GU_TEXTURE_2D);
    } else {
        TexRec *t = R.cur_mode == RM_RAW4 ? &R.tex[0] : &R.tex[R.cur_tex];
        GuTexImageArgs ti = { 0, t->pw, t->ph, t->pw, R.cur_mode == RM_RAW4 ? t->pix : tex_addr(t) };
        t->gen = R.list_gen;
        sceGuEnable(GU_TEXTURE_2D);
        if (R.cur_mode == RM_RAW4) {
            GuTexImageArgs ri = { 0, R.raw_w, R.raw_h, R.raw_w, R.raw_pix };
            ti = ri;
            sceGuClutMode(GU_PSM_8888, 0, 0x0F, 0);
            sceGuClutLoad(16 / 8, R.raw_clut);
            sceGuTexMode(GU_PSM_T4, 0, 0, r_gu_char_swz > 0);   /* P109 */
        } else if (R.cur_mode == RM_INDEX) {
            const uint32_t *clut = (R.cur_pal >= 0 && R.pal[R.cur_pal].used) ? R.pal[R.cur_pal].clut : black_clut;
            if (clut != black_clut) R.pal[R.cur_pal].gen = R.list_gen;
            sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
            sceGuClutLoad(256 / 8, clut);
            sceGuTexMode(GU_PSM_T8, 0, 0, t->swz);
        } else {
            sceGuTexMode(GU_PSM_8888, 0, 0, t->swz);
        }
        gu_teximage(&ti);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexFilter(texture_filter_linear() ? GU_LINEAR : GU_NEAREST,
                       texture_filter_linear() ? GU_LINEAR : GU_NEAREST);
        sceGuTexWrap((R.state_ad0 & 0x20000) ? GU_CLAMP : GU_REPEAT,
                     (R.state_ad0 & 0x20000) ? GU_CLAMP : GU_REPEAT);
        sceGuTexFlush();
    }
    apply_blend_depth();
    v = (GuVtx *)sceGuGetMemory(sizeof(GuVtx) * R.nverts);
    memcpy(v, R.verts, sizeof(GuVtx) * R.nverts);
    {
        GuDrawArgs d = { GU_TRIANGLES, GU_VTYPE, R.nverts, 0, v };
        gu_draw(&d);
    }
    R.nverts = 0;
}

static void use(int tex, int mode, int pal)
{
    if (tex != R.cur_tex || mode != R.cur_mode || (mode == RM_INDEX && pal != R.cur_pal)) {
        r_flush();
        R.cur_tex = tex;
        R.cur_mode = mode;
        R.cur_pal = pal;
    }
}

static float depth_of(float z);

/* Character path (r_gu_char.c): one T4 page + 16-colour CLUT, quad given in
 * game display coordinates (already snapped), depth as r_gl's r_depth_of. */
void r_gu_char_quad(const void *pix, int pw, int ph, const uint32_t *clut,
                    const float x[4], const float y[4], const float u[4], const float v[4],
                    uint32_t rgba, float z)
{
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    GuVtx *o;
    if (R.cur_mode != RM_RAW4 || R.raw_pix != pix || R.raw_clut != clut) {
        r_flush();
        R.cur_tex = -2; R.cur_mode = RM_RAW4; R.cur_pal = -1;
        R.raw_pix = pix; R.raw_clut = clut; R.raw_w = pw; R.raw_h = ph;
    }
    if (R.nverts + 6 > MAX_VERTS) r_flush();
    o = &R.verts[R.nverts];
    R.nverts += 6;
    for (int k = 0; k < 6; k++) {
        int i = idx[k];
        o[k].x = x[i] * R.sx + R.ox;
        o[k].y = y[i] * R.sy + R.oy;
        o[k].z = z;
        o[k].u = u[i];
        o[k].v = v[i];
        o[k].c = rgba;
    }
}

float r_gu_snap_x(float x) { return floorf(x - (float)R.origin_x); }
float r_gu_snap_y(float y) { return floorf(y - (float)R.origin_y); }
float r_gu_depth(float z) { return depth_of(z); }
/* finish everything queued so far (before reusing texture memory) */
void r_gu_sync(void)
{
    int was = R.list_open;
    r_flush();
    if (was) { list_close(); list_open(); }
}

static GuVtx *verts(int n)
{
    GuVtx *v;
    if (R.nverts + n > MAX_VERTS) r_flush();
    v = &R.verts[R.nverts];
    R.nverts += n;
    return v;
}

/* ---------------- colours, depth, coordinates (as r_gl.c) ---------------- */
static uint8_t gs_to_unorm(unsigned g) { unsigned v = (g * 255 + 64) / 128; return (uint8_t)(v > 255 ? 255 : v); }
static unsigned halve(unsigned t) { return t == 0xFF ? 0x80 : t ? ((t >> 1) ? (t >> 1) : 1) : 0; }

static uint32_t color_tex(uint32_t c)
{
    return gs_to_unorm(halve((c >> 16) & 0xFF)) | (uint32_t)gs_to_unorm(halve((c >> 8) & 0xFF)) << 8 |
           (uint32_t)gs_to_unorm(halve(c & 0xFF)) << 16 | (uint32_t)gs_to_unorm(halve(c >> 24)) << 24;
}

static uint32_t color_flat(uint32_t c)
{
    return ((c >> 16) & 0xFF) | (c & 0xFF00) | (c & 0xFF) << 16 | (uint32_t)gs_to_unorm(halve(c >> 24)) << 24;
}

/* GS: larger Z is nearer; depth buffer cleared to 0, tested GEQUAL.
 * The game's depths all sit at the far end (stage meshes at NDC -1.000 to
 * -0.998, sprites to -0.78): a 16-bit buffer over the whole range left the
 * water and riverbed ~65 steps, so they z-fought (P28). DEPTH_ZOOM spreads
 * [-1, -1 + 2/DEPTH_ZOOM] over the buffer, nearer clamps to the top; meshes
 * get the same map (r_gu_mesh_draw). */
#define DEPTH_ZOOM 8.f
/* GS: larger Z is nearer; depth buffer cleared to 0, tested GEQUAL. */
static float depth_of(float z)
{
    float d = -z;
    d = (d + 1.f) * (DEPTH_ZOOM * 0.5f);
    d = d < 0.f ? 0.f : d > 1.f ? 1.f : d;
    return d * 65535.f;
}

static void put(GuVtx *o, const FlVec4 *v, const float *uv, uint32_t c, int tw, int th)
{
    o->x = floorf(v->x - (float)R.origin_x) * R.sx + R.ox;
    o->y = floorf(v->y - (float)R.origin_y) * R.sy + R.oy;
    o->z = depth_of(v->z);
    o->u = uv ? uv[0] * tw : 0.f;
    o->v = uv ? uv[1] * th : 0.f;
    o->c = c;
}

/* ---------------- blend: func_003E06D0 (GU factors) ---------------- */
static void set_blend_from_state(void)
{
    uint32_t f = R.state_ad0;
    int src = f & 0xF, dst = (f >> 4) & 0xF, ok = 1;
    int op = GU_ADD, sf = GU_FIX, df = GU_FIX;
    unsigned sfix = 0, dfix = 0;
    enum { ONE = -1, ZERO = -2 };
#define B(o, s, d) do { op = (o); sf = (s); df = (d); } while (0)
    switch (src) {
    case 1:
        switch (dst) {
        case 1: if ((f & 0xC00) == 0x400) B(GU_SUBTRACT, ONE, ONE); else B(GU_ADD, ONE, ONE); break;
        case 0: B(GU_ADD, ONE, ZERO); break;
        case 2: B(GU_ADD, ONE, GU_SRC_ALPHA); break;
        case 3: B(GU_SUBTRACT, ONE, GU_SRC_ALPHA); break;
        case 4: B(GU_ADD, ONE, GU_DST_ALPHA); break;
        case 5: B(GU_SUBTRACT, ONE, GU_DST_ALPHA); break;
        default: ok = 0;
        }
        break;
    case 0:
        switch (dst) {
        case 1: B(GU_ADD, ZERO, ONE); break;
        case 0: B(GU_ADD, ZERO, ZERO); break;
        case 2: B(GU_ADD, ZERO, GU_SRC_ALPHA); break;
        case 3: B(GU_ADD, ZERO, GU_ONE_MINUS_SRC_ALPHA); break;
        case 4: B(GU_ADD, ZERO, GU_DST_ALPHA); break;
        case 5: B(GU_ADD, ZERO, GU_ONE_MINUS_DST_ALPHA); break;
        default: ok = 0;
        }
        break;
    case 2:
        switch (dst) {
        case 1: B(GU_ADD, GU_SRC_ALPHA, ONE); break;
        case 0: B(GU_ADD, GU_SRC_ALPHA, ZERO); break;
        case 3: B(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA); break;
        default: ok = 0;
        }
        break;
    case 3:
        switch (dst) {
        case 0: B(GU_ADD, GU_ONE_MINUS_SRC_ALPHA, ZERO); break;
        case 2: B(GU_ADD, GU_ONE_MINUS_SRC_ALPHA, GU_SRC_ALPHA); break;
        default: ok = 0;
        }
        break;
    case 4:
        switch (dst) {
        case 1: B(GU_ADD, GU_DST_ALPHA, ONE); break;
        case 0: B(GU_ADD, GU_DST_ALPHA, ZERO); break;
        case 5: B(GU_ADD, GU_DST_ALPHA, GU_ONE_MINUS_DST_ALPHA); break;
        default: ok = 0;
        }
        break;
    case 5:
        switch (dst) {
        case 0: B(GU_ADD, GU_ONE_MINUS_DST_ALPHA, ZERO); break;
        case 4: B(GU_ADD, GU_ONE_MINUS_DST_ALPHA, GU_DST_ALPHA); break;
        default: ok = 0;
        }
        break;
    default: ok = 0;
    }
#undef B
    if (!ok) return;   /* rejected pairs keep the previous blend, as on the PS2 */
    if (sf == ONE) { sf = GU_FIX; sfix = 0xFFFFFF; } else if (sf == ZERO) { sf = GU_FIX; sfix = 0; }
    if (df == ONE) { df = GU_FIX; dfix = 0xFFFFFF; } else if (df == ZERO) { df = GU_FIX; dfix = 0; }
    if (R.blend_ok && op == R.bl_op && sf == R.bl_src && df == R.bl_dst && sfix == R.bl_srcfix && dfix == R.bl_dstfix)
        return;
    r_flush();
    R.blend_ok = 1;
    R.bl_op = op; R.bl_src = sf; R.bl_dst = df; R.bl_srcfix = sfix; R.bl_dstfix = dfix;
}

/* ---------------- func_003DF850 (as r_gl.c) ---------------- */
int fl_set_render_state(int id, uintptr_t val)
{
    uint32_t v = (uint32_t)val;
    if (id >= 0x19 && id < 0x39) {
        if (val && memcmp(R.matrices[id], (const void *)val, 64)) memcpy(R.matrices[id], (const void *)val, 64);
        return 1;
    }
    if (id >= 0x39 && id < 0x59) {
        if (val) memcpy(R.materials[id - 0x39], (const void *)val, 76);
        return 1;
    }
    if (id >= 0x59 && id < 0x5C) {
        /* P89: fe_mesh.c hands these over on every mesh draw; they change
         * at scene setup. Same source bytes, same normalized light. */
        static uint8_t raw[3][100];
        static int raw_ok[3];
        uint8_t *l = R.lights[id - 0x59];
        float d[3], n;
        if (val && raw_ok[id - 0x59] && !memcmp(raw[id - 0x59], (const void *)val, 100)) return 1;
        if (val) { memcpy(l, (const void *)val, 100); memcpy(raw[id - 0x59], l, 100); raw_ok[id - 0x59] = 1; }
        else raw_ok[id - 0x59] = 0;
        memcpy(d, l + 0x34, 12);
        n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n > 0.f) { d[0] /= n; d[1] /= n; d[2] /= n; }
        memcpy(l + 0x34, d, 12);
        return 1;
    }
    switch (id) {
    case 0xE: R.material_color = v; break;
    case 0xF: R.fog_color = v; break;
    case 0x10: memcpy(&R.fog_a, &v, 4); break;
    case 0x11: memcpy(&R.fog_b, &v, 4); break;
    case 0x12: R.state_ad4 = (R.state_ad4 & 0xFFFE7FFF) | (v ? 0x8000 : 0); break;
    case 1:
        R.state_ad4 &= 0xFFFFFC7F;
        if (v == 1) R.state_ad4 |= 0x100;
        else if (v == 2) R.state_ad4 |= 0x80;
        break;
    case 2:
        R.state_ad4 &= 0xFFFFFFF3;
        if (v == 1) R.state_ad4 |= 4;
        else if (v == 2) R.state_ad4 |= 8;
        break;
    case 0xD:
        if ((R.state_ad0 & 0xC00) != v) {
            R.state_ad0 = (R.state_ad0 & 0xFFFFF3FF) | v;
            set_blend_from_state();
        }
        break;
    case 0x5D:
        if ((R.state_ad0 & 0xFF) != v) {
            R.state_ad0 = (R.state_ad0 & 0xFFFFFF00) | v;
            set_blend_from_state();
        }
        break;
    case 4: case 5: case 6: case 7:
        if (id == 4) R.cur_texture = v;
        R.tex_slots[id - 4] = v;
        break;
    case 0x14: fl_set_clear_color(v); break;
    case 0x16: case 0x17:   /* P89: skip the store when unchanged */
        if (memcmp(R.matrices[id], (const void *)val, 64)) memcpy(R.matrices[id], (const void *)val, 64);
        break;
    case 0x18:
        if (val) {
            const int32_t *q = (const int32_t *)val;
            memcpy(R.vp_i, q, 16);
            memcpy(R.vp_f, q + 4, 8);
            fl_set_scissor(R.vp_i[0], R.vp_i[1], R.vp_i[2], R.vp_i[3]);
        }
        break;
    case 0x15:
        R.state_ad4 &= 0xFFFFFFFC;
        if (v == 2) R.state_ad4 |= 2;
        break;
    case 0x5C: R.state_ad4 = (R.state_ad4 & 0xFFFFFF) | v; break;
    case 0x5F: R.state_5f = v; break;
    case 0x61: R.state_ad4 = (R.state_ad4 & 0xFFFFBFFF) | (v ? 0x4000 : 0); break;
    case 0x62:
        if (((R.state_ad0 & 0x10000) != 0) != (v == 0x10000)) r_flush();
        R.state_ad0 = (R.state_ad0 & 0xFFFEFFFF) | (v == 0x10000 ? 0x10000 : 0);
        break;
    case 0x63:
        if (((R.state_ad0 & 0x20000) != 0) != (v == 0x20000)) r_flush();
        R.state_ad0 = (R.state_ad0 & 0xFFF9FFFF) | (v == 0x20000 ? 0x20000 : 0);
        break;
    case 0x65: R.state_ad4 = (R.state_ad4 & 0xFFFDFFFF) | (v ? 0x20000 : 0); break;
    case 0x66: R.global_mod = v; break;
    case 0xC: R.state_ad4 = (R.state_ad4 & 0xFFFFFFBF) | (v == 1 ? 0x40 : 0); break;
    case 0x6C:
        if ((v == 0 || v == 1 || v == 3 || v == 7) && R.ztest != (int)v) {
            r_flush();
            R.ztest = (int)v;
        }
        /* fall through: the original 0x6C runs the Z-write case too */
    case 0x6B:
        if (R.zwrite != (v != 0)) r_flush();
        R.zwrite = v != 0;
        break;
    default: break;
    }
    return 1;
}

uintptr_t fl_get_render_state(int id)
{
    switch (id) {
    case 1:
        switch (R.state_ad4 & 0x380) { case 0x100: return 1; case 0x80: return 2; default: return 0; }
    case 0x15:
        switch (R.state_ad4 & 3) { case 0: return 1; case 2: return 2; default: return 0; }
    case 4: case 5: case 6: case 7: return R.tex_slots[id - 4];
    case 0x14: return R.clear_color;
    default:
        if (id >= 0x16 && id < 0x39) return (uintptr_t)R.matrices[id];
        if (id >= 0x39 && id < 0x59) return (uintptr_t)R.materials[id - 0x39];
        return 0;
    }
}

void fl_sync_mode_words(uint32_t ad0, uint32_t ad4)
{
    if (ad0 == R.state_ad0 && ad4 == R.state_ad4) return;
    r_flush();
    R.state_ad0 = ad0;
    R.state_ad4 = ad4;
    set_blend_from_state();
}

void fl_default_state(void)
{
    r_flush();
    R.state_ad4 = 0x102;
    R.state_ad0 = 0x38B032;
    R.blend_ok = 0;
    set_blend_from_state();
    R.ztest = -1;
    fl_set_render_state(0x6C, 1);
    fl_set_render_state(0x62, 0);
    fl_set_scissor(0, 0, FL_SCREEN_W, FL_SCREEN_H);
}

void fl_set_scissor(int x, int y, int w, int h)
{
    if (x < 0) x = 0;
    if (x + w > FL_SCREEN_W) w = FL_SCREEN_W - x;
    if (y < 0) y = 0;
    if (y + h > FL_SCREEN_H) h = FL_SCREEN_H - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    r_flush();
    R.scissor[0] = x; R.scissor[1] = y; R.scissor[2] = w; R.scissor[3] = h;
    if (R.list_open) apply_scissor();
}

void fl_set_clear_color(uint32_t argb)
{
    if (R.flip_clear && argb != R.clear_color && !R.drawn && !r_discard) R.clear_pending = 1;
    R.clear_color = argb;
}
void fl_set_origin(int x, int y) { R.origin_x = x; R.origin_y = y; }
void fl_set_flip_clear(int on) { R.flip_clear = on != 0; }

void fl_set_display(int w, int h)
{
    if (w <= 0 || w > FL_SCREEN_W) w = FL_SCREEN_W;
    if (h <= 0 || h > FL_SCREEN_H) h = FL_SCREEN_H;
    if (w == R.disp_w && h == R.disp_h) return;
    r_flush();
    R.disp_w = w;
    R.disp_h = h;
    set_mapping();
    if (R.list_open) apply_scissor();
}

void fl_get_display(int *w, int *h)
{
    if (w) *w = R.disp_w > 0 ? R.disp_w : FL_SCREEN_W;
    if (h) *h = R.disp_h > 0 ? R.disp_h : FL_SCREEN_H;
}

/* ---------------- frame ---------------- */
void fl_frame_flip(void)
{
    r_flush();
    R.clear_pending = 1;
    R.drawn = 0;
}

void fl_frame_begin(void)
{
    r_flush();
    if (!R.flip_clear) R.clear_pending = 1;
    fl_set_render_state(0x5D, 0x32);
    fl_set_render_state(0x62, 0x10000);
    fl_set_scissor(R.scissor[0], R.scissor[1], R.scissor[2], R.scissor[3]);
}

void fl_frame_end(void) { r_flush(); }

/* The GE reads textures and CLUTs when the list runs (list_close), not when a
 * draw is recorded: rewriting or freeing one the open list already uses would
 * change those earlier draws too (P28: grey water). Run the list first. */
static void in_use_sync(unsigned gen)
{
    rec_build();   /* P103: recorded submits bind the old texture/CLUT */
    mesh_tex_ok = 0;   /* a texture or CLUT is about to change */
    if (R.list_open && gen == R.list_gen) r_gu_sync();
}

/* ---------------- palettes ---------------- */
static uint32_t *clut_new(void)
{
    uint32_t *c = (uint32_t *)memalign(16, 256 * 4);
    if (c) memset(c, 0, 256 * 4);
    return c;
}

static void clut_write(uint32_t *clut, int first, int n, const uint8_t *rgba)
{
    if (first < 0 || first >= 256) return;
    if (first + n > 256) n = 256 - first;
    r_flush();   /* a batched draw may still use the old colours */
    memcpy(clut + first, rgba, (size_t)n * 4);   /* RGBA bytes = GU 8888 */
    sceKernelDcacheWritebackRange(clut + first, (unsigned)n * 4);
}

uint32_t fl_pal_create(int ncolors, const uint8_t *rgba)
{
    for (int i = 0; i < MAX_PAL; i++) {
        if (R.pal[i].used) continue;
        if (!(R.pal[i].clut = clut_new())) return 0;
        R.pal[i].used = 1;
        R.pal[i].ncolors = ncolors;
        if (rgba) clut_write(R.pal[i].clut, 0, ncolors, rgba);
        return (uint32_t)(i + 1) << 16;
    }
    return 0;
}

void fl_pal_update(uint32_t handle, int first, int n, const uint8_t *rgba)
{
    int i = (int)(handle >> 16) - 1;
    if (i < 0 || i >= MAX_PAL || !R.pal[i].used) return;
    in_use_sync(R.pal[i].gen);
    clut_write(R.pal[i].clut, first, n, rgba);
}

/* P97: bumped on every define, so psp_fe_sync.c sees a redefinition made by
 * a path that bypasses its memo (texture unlock, speculative resync) */
unsigned short r_gu_tex_gen[MAX_TEX + 1], r_gu_pal_gen[MAX_PAL + 1];

int fl_pal_define(uint32_t handle, int ncolors, const uint8_t *rgba)
{
    int i = (int)(handle >> 16) - 1;
    if (i < 0 || i >= MAX_PAL) return -1;
    r_gu_pal_gen[i + 1]++;
    if (!R.pal[i].used) {
        if (!(R.pal[i].clut = clut_new())) return -1;
        R.pal[i].used = 1;
    }
    in_use_sync(R.pal[i].gen);
    R.pal[i].ncolors = ncolors;
    if (rgba) clut_write(R.pal[i].clut, 0, ncolors > 256 ? 256 : ncolors, rgba);
    return 0;
}

/* ---------------- textures ---------------- */
static int pow2(int n, int min) { int p = min; while (p < n) p <<= 1; return p; }
static int is_index(int fmt) { return fmt == FL_TEX_IDX4 || fmt == FL_TEX_IDX8; }

/* P109: the GE fetches a swizzled texture in 16-byte x 8-row blocks, far
 * fewer bus transactions than row-linear texels (most fight textures come
 * from main RAM: the VRAM cache holds about a third).  Sampling is the same.
 * Byte (xb, y) of a row-stride-S texture sits at block (xb / 16, y / 8),
 * blocks row-major, 128 bytes each, 16 bytes a row inside. */
static int swz_off = -1;   /* MVC2_NO_SWIZZLE=1: row-linear textures (A/B) */
static void swz_row(uint8_t *dst, unsigned stride, int y, unsigned xb, const uint8_t *src, size_t n)
{
    uint8_t *base = dst + (size_t)(y >> 3) * stride * 8 + (size_t)(y & 7) * 16;
    while (n) {
        unsigned in = xb & 15u, c = 16u - in;
        if (c > n) c = (unsigned)n;
        memcpy(base + (size_t)(xb >> 4) * 128 + in, src, c);
        xb += c; src += c; n -= c;
    }
}

/* copy w x h pixels into the padded texture at (x, y) */
static void tex_copy(TexRec *t, int x, int y, int w, int h, const void *pixels)
{
    int bpp = is_index(t->fmt) ? 1 : 4;
    const uint8_t *s = (const uint8_t *)pixels;
    if (t->swz && pixels && t->pix) {
        uint8_t *vr = t->voff ? (uint8_t *)(uintptr_t)(0x44000000u + t->voff) : NULL;   /* uncached, as below */
        unsigned stride = (unsigned)t->pw * (unsigned)bpp;
        for (int r = 0; r < h && y + r < t->ph; r++) {
            size_t n = (size_t)(x + w <= t->pw ? w : t->pw - x) * bpp;
            swz_row(t->pix, stride, y + r, (unsigned)(x * bpp), s + (size_t)r * w * bpp, n);
            if (vr) swz_row(vr, stride, y + r, (unsigned)(x * bpp), s + (size_t)r * w * bpp, n);
        }
        sceKernelDcacheWritebackRange(t->pix, (unsigned)(t->pw * t->ph * bpp));
        return;
    }
    /* P88: the VRAM copy (P87) is updated in place.  Callers have waited for
     * the GE if this frame's list uses the texture (in_use_sync), as for the
     * RAM copy.  Dropping it instead leaked its slot until the next reset. */
    uint8_t *vr = t->voff ? (uint8_t *)(uintptr_t)(0x44000000u + t->voff) : NULL;   /* uncached */
    if (!pixels || !t->pix) return;
    for (int r = 0; r < h && y + r < t->ph; r++) {
        size_t o = ((size_t)(y + r) * t->pw + x) * bpp, n = (size_t)(x + w <= t->pw ? w : t->pw - x) * bpp;
        memcpy(t->pix + o, s + (size_t)r * w * bpp, n);
        if (vr) memcpy(vr + o, s + (size_t)r * w * bpp, n);
    }
    sceKernelDcacheWritebackRange(t->pix, (unsigned)(t->pw * t->ph * bpp));
}

static int tex_alloc(TexRec *t, int w, int h, int fmt)
{
    int bpp = is_index(fmt) ? 1 : 4;
    memset(t, 0, sizeof *t);
    t->w = w; t->h = h; t->fmt = fmt;
    /* buffer width: 16-byte multiple (T8: 16 px, 8888: 4 px) */
    t->pw = pow2(w, is_index(fmt) ? 16 : 4);
    t->ph = pow2(h, 1);
    if (swz_off < 0) swz_off = getenv("MVC2_NO_SWIZZLE") != NULL;
    t->swz = !swz_off && (t->ph & 7) == 0;   /* pw * bpp is a 16-byte multiple (above) */
    t->pix = (uint8_t *)memalign(16, (size_t)t->pw * t->ph * bpp);
    if (!t->pix) return -1;
    mem_tex += t->pw * t->ph * bpp; mem_ntex++;
    memset(t->pix, 0, (size_t)t->pw * t->ph * bpp);
    t->used = 1;
    return 0;
}

/* DRAWLOG memory report: bytes held by the renderer (r_gu_mem_log) */
void r_gu_mem_log(void)
{
    extern int r_gu_char_bytes, r_gu_mesh_bytes;
    int pal = 0;
    for (int i = 0; i < MAX_PAL; i++) pal += R.pal[i].used ? 1024 : 0;
    fprintf(stderr, "[mem] renderer: textures %d KB (%d), cluts %d KB, char pages %d KB, meshes %d KB\n",
            mem_tex >> 10, mem_ntex, pal >> 10, r_gu_char_bytes >> 10, r_gu_mesh_bytes >> 10);
}

static void tex_free(TexRec *t, int idx)
{
    r_flush();
    if (R.cur_tex == idx) R.cur_tex = -1;
    if (t->pix) { mem_tex -= t->pw * t->ph * (is_index(t->fmt) ? 1 : 4); mem_ntex--; }
    free(t->pix);
    memset(t, 0, sizeof *t);
}

uint32_t fl_tex_create(int w, int h, int fmt, const void *pixels)
{
    for (int i = 0; i < MAX_TEX; i++) {
        if (R.tex[i].used) continue;
        if (tex_alloc(&R.tex[i], w, h, fmt)) return 0;
        tex_copy(&R.tex[i], 0, 0, w, h, pixels);
        return (uint32_t)(i + 1);
    }
    return 0;
}

int fl_tex_define(uint32_t handle, int w, int h, int fmt, const void *pixels)
{
    int i = (int)(handle & 0xFFFF) - 1;
    TexRec *t;
    if (i < 0 || i >= MAX_TEX || w <= 0 || h <= 0) {
        fprintf(stderr, "[gu] texture handle %08x (%dx%d): out of range, not defined\n", (unsigned)handle, w, h);
        return -1;
    }
    r_gu_tex_gen[i + 1]++;
    t = &R.tex[i];
    r_flush();
    if (t->used) in_use_sync(t->gen);
    if (!t->used || t->w != w || t->h != h || t->fmt != fmt) {
        if (t->used) tex_free(t, i);
        if (tex_alloc(t, w, h, fmt)) {
            fprintf(stderr, "[gu] texture %d (%dx%d): out of memory, not defined\n", i + 1, w, h);
            return -1;
        }
    }
    tex_copy(t, 0, 0, w, h, pixels);
    return 0;
}

int fl_tex_defined(uint32_t handle)
{
    int i = (int)(handle & 0xFFFF) - 1;
    return i >= 0 && i < MAX_TEX && R.tex[i].used;
}

void fl_tex_update(uint32_t handle, int x, int y, int w, int h, const void *pixels)
{
    int i = (int)(handle & 0xFFFF) - 1;
    if (i < 0 || i >= MAX_TEX || !R.tex[i].used) return;
    r_flush();
    in_use_sync(R.tex[i].gen);
    tex_copy(&R.tex[i], x, y, w, h, pixels);
}

void fl_tex_release(uint32_t handle)
{
    int i = (int)(handle & 0xFFFF) - 1, p = (int)(handle >> 16) - 1;
    if (i >= 0 && i < MAX_TEX && R.tex[i].used) tex_free(&R.tex[i], i);
    if (p >= 0 && p < MAX_PAL && R.pal[p].used) {
        r_flush();
        if (R.cur_pal == p) R.cur_pal = -1;
        free(R.pal[p].clut);
        memset(&R.pal[p], 0, sizeof R.pal[p]);
    }
}

int fl_tex_info(uint32_t handle, int *w, int *h, int *fmt)
{
    int i = (int)(handle & 0xFFFF) - 1;
    if (i < 0 || i >= MAX_TEX || !R.tex[i].used) return -1;
    if (w) *w = R.tex[i].w;
    if (h) *h = R.tex[i].h;
    if (fmt) *fmt = R.tex[i].fmt;
    return 0;
}

uint32_t fl_tex_load_tim2(const uint8_t *data, size_t size, int pic)
{
    Tim2Pic tp;
    uint32_t h = 0;
    if (tim2_decode(data, size, pic, &tp)) { tim2_free(&tp); return 0; }
    if (tp.index) {
        h = fl_tex_create(tp.w, tp.h, tp.image_type == 4 ? FL_TEX_IDX4 : FL_TEX_IDX8, tp.index);
        if (h) h |= fl_pal_create(tp.clut_colors ? tp.clut_colors : 16, tp.clut);
    } else {
        h = fl_tex_create(tp.w, tp.h, tp.image_type == 1 ? FL_TEX_RGBA16 : tp.image_type == 2 ? FL_TEX_RGBA24 : FL_TEX_RGBA32, tp.rgba);
    }
    tim2_free(&tp);
    return h;
}

/* ---------------- 2D primitives ---------------- */
/* Bind state 4 (D_004C2AEC); unknown handles are rejected as the original
 * rejects textures without a VRAM record. */
static TexRec *bind_current(void)
{
    int i = (int)(R.cur_texture & 0xFFFF) - 1, p = (int)(R.cur_texture >> 16) - 1;
    TexRec *t;
    if (i < 0 || i >= MAX_TEX || !R.tex[i].used) return 0;
    t = &R.tex[i];
    if (t->pw > 512 || t->ph > 512) {
        if (!t->warned) fprintf(stderr, "[gu] texture %d is %dx%d (> 512): not drawn\n", i + 1, t->w, t->h);
        t->warned = 1;
        return 0;
    }
    use(i, is_index(t->fmt) ? RM_INDEX : RM_RGBA, (p >= 0 && p < MAX_PAL && R.pal[p].used) ? p : -1);
    return t;
}

int fl_tri_tex(const FlTri *t)
{
    TexRec *tx = bind_current();
    GuVtx *o;
    if (!tx) return 0;
    o = verts(3);
    for (int k = 0; k < 3; k++)
        put(&o[k], &t->v[k], t->uv[k], color_tex(t->argb[k]), tx->w, tx->h);
    return 1;
}

int fl_tri(const FlTri *t)
{
    GuVtx *o;
    use(-1, RM_FLAT, -1);
    o = verts(3);
    for (int k = 0; k < 3; k++)
        put(&o[k], &t->v[k], NULL, color_flat(t->argb[k]), 0, 0);
    return 1;
}

int fl_sprite_tex(const FlSprite *s)
{
    TexRec *tx = bind_current();
    GuVtx *o;
    uint32_t c;
    FlVec4 a, b, tr, bl;
    float k, uv0[2], uv1[2], uv_tr[2], uv_bl[2];
    if (!tx) return 0;
    c = color_tex(s->argb);
    a = s->v[0]; b = s->v[1];
    a.z = b.z;   /* GS SPRITE: Z from the second vertex */
    k = (a.w != 0.f) ? b.w / a.w : 1.f;   /* one RGBAQ: uv1 * w1 / w0 */
    uv0[0] = s->uv[0][0]; uv0[1] = s->uv[0][1];
    uv1[0] = s->uv[1][0] * k; uv1[1] = s->uv[1][1] * k;
    tr = (FlVec4){ b.x, a.y, b.z, 1.f };
    bl = (FlVec4){ a.x, b.y, b.z, 1.f };
    uv_tr[0] = uv1[0]; uv_tr[1] = uv0[1];
    uv_bl[0] = uv0[0]; uv_bl[1] = uv1[1];
    o = verts(6);
    put(&o[0], &a, uv0, c, tx->w, tx->h);
    put(&o[1], &tr, uv_tr, c, tx->w, tx->h);
    put(&o[2], &b, uv1, c, tx->w, tx->h);
    put(&o[3], &a, uv0, c, tx->w, tx->h);
    put(&o[4], &b, uv1, c, tx->w, tx->h);
    put(&o[5], &bl, uv_bl, c, tx->w, tx->h);
    return 1;
}

int fl_quad_tex(const FlQuad *q)
{
    TexRec *tx = bind_current();
    const FlVec4 *v[4] = { &q->t[0].v[0], &q->t[0].v[1], &q->t[0].v[2], &q->t[1].v[2] };
    const float *uv[4] = { q->t[0].uv[0], q->t[0].uv[1], q->t[0].uv[2], q->t[1].uv[2] };
    uint32_t col[4] = { q->t[0].argb[0], q->t[0].argb[1], q->t[0].argb[2], q->t[1].argb[2] };
    static const int strip[6] = { 0, 1, 2, 1, 3, 2 };
    GuVtx *o;
    if (!tx) return 0;
    o = verts(6);
    for (int k = 0; k < 6; k++)
        put(&o[k], v[strip[k]], uv[strip[k]], color_tex(col[strip[k]]), tx->w, tx->h);
    return 1;
}

/* func_003ED1C0: mode 0 writes only Z = 0, mode 1 colour (A = 0) and Z;
 * both within the scissor. */
int fl_fill_screen(int mode, uint32_t c)
{
    if (mode != 0 && mode != 1) return 0;
    r_flush();
    rec_build();
    if (!R.inited || r_discard) return 0;
    canvas_bind();
    mesh_bd_ok = 0;
    sceGuDepthMask(GU_FALSE);
    sceGuClearDepth(0);
    if (mode == 1) {
        sceGuClearColor((c & 0xFF) << 16 | (c & 0xFF00) | ((c >> 16) & 0xFF));
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    } else {
        sceGuClear(GU_DEPTH_BUFFER_BIT);
    }
    return 0;
}

void r_stats(int *d, int *v) { if (d) *d = 0; if (v) *v = 0; }

/* ---------------- mesh path support (r_gu_mesh.c) ---------------- */
/* P96 (port/psp/host/psp_mesh_state.c): once bound, the mesh draw reads the
 * game's view, projection, world and UV matrices and lights where the game
 * keeps them, instead of fe_mesh.c copying them in on every draw */
static const float *mesh_mtx[4];        /* 0x16, 0x17, 0x19, 0x1A */
static uintptr_t mesh_light[3];
void r_gu_mesh_bind_clip(const float *m);
void r_gu_mesh_bind(const float *view, const float *proj, const float *world, const float *uv,
                    const float *clip, const void *l0, const void *l1, const void *l2)
{
    mesh_mtx[0] = view; mesh_mtx[1] = proj; mesh_mtx[2] = world; mesh_mtx[3] = uv;
    mesh_light[0] = (uintptr_t)l0; mesh_light[1] = (uintptr_t)l1; mesh_light[2] = (uintptr_t)l2;
    r_gu_mesh_bind_clip(clip);
}
/* lit draws: copy (and normalize) the lights as the per-draw hand-off did */
void r_gu_mesh_lights(void)
{
    if (mesh_light[0])
        for (int k = 0; k < 3; k++) fl_set_render_state(0x59 + k, mesh_light[k]);
}
const float *r_matrix(int id)
{
    if (mesh_mtx[0])
        switch (id) {
        case 0x16: return mesh_mtx[0];
        case 0x17: return mesh_mtx[1];
        case 0x19: return mesh_mtx[2];
        case 0x1A: return mesh_mtx[3];
        }
    return (id >= 0 && id < 0x60) ? R.matrices[id] : r_matrix(0x19);
}
uint32_t r_state_ad0(void) { return R.state_ad0; }
uint32_t r_state_ad4(void) { return R.state_ad4; }
uint32_t r_global_mod(void) { return R.global_mod; }
uint32_t r_material_color(void) { return R.material_color; }
const uint8_t *r_material_slot(int i) { return R.materials[i & 31]; }
const uint8_t *r_light(int i) { return R.lights[i < 0 ? 0 : i > 2 ? 2 : i]; }
void r_viewport(int vi[4], float vf[2]) { memcpy(vi, R.vp_i, 16); memcpy(vf, R.vp_f, 8); }
uint8_t r_gs_to_unorm(unsigned g) { return gs_to_unorm(g); }

/* P108: the mesh vertex pool (offsets from the VRAM base; size 0 = none) and
 * whether VRAM may be used this frame (not while a system dialog is up) */
uint32_t r_gu_mesh_vram(uint32_t *size) { *size = mv_size; return mv_base; }
int r_gu_vram_ok(void) { return !vr_pause && !vr_drop; }

/* before a mesh draw: 2D batches out, list open with room for `bytes` of
 * sceGuGetMemory (a restart later would overwrite it) */
void r_gu_begin(unsigned bytes)
{
    unsigned long long t = 0;
    if (psp_prof_fine()) {
        t = psp_prof_now();
        if (R.nverts) psp_prof_add(46, "          r_gu_begin: 2D batches flushed (count)", 1000);
        psp_prof_add(47, "          r_gu_begin: 2D verts flushed (count)", 1000ull * R.nverts);
    }
    r_flush();
    if (t) { psp_prof_add(44, "          r_gu_begin: r_flush", psp_prof_now() - t); t = psp_prof_now(); }
    canvas_bind();
    if (sceGuCheckList() > LIST_BYTES - (int)bytes - 65536) {
        list_close();
        list_open();
        canvas_bind();
        if (t) psp_prof_add(41, "          r_gu_begin: list restarts (count)", 1000);
    }
    if (t) psp_prof_add(45, "          r_gu_begin: check list", psp_prof_now() - t);
}

/* canvas NDC (640x480, as r_mesh.c's u_vp output) -> GE clip space for the
 * 480x272 viewport, so the GE's own 3D path (perspective-correct UVs) draws
 * meshes where the 2D core puts the same canvas pixels */
/* the canvas mapping r_gu_canvas_to_ge uses (P88: r_gu_mesh.c memo key) */
void r_gu_canvas_key(float k[4]) { k[0] = R.sx; k[1] = R.sy; k[2] = R.ox; k[3] = R.oy; }

void r_gu_canvas_to_ge(float m[16])
{
    const float sx = R.sx * 320.f, sy = R.sy * 240.f;
    /* gx = ((x + 1) * 320 * R.sx + R.ox - 240) / 240, gy = (136 - ((1 - y) * 240 * R.sy + R.oy)) / 136 */
    float ax = sx / (SCR_W / 2.f), bx = (sx + R.ox - SCR_W / 2.f) / (SCR_W / 2.f);
    float ay = sy / (SCR_H / 2.f), by = (SCR_H / 2.f - sy - R.oy) / (SCR_H / 2.f);
    memset(m, 0, 64);
    m[0] = ax; m[5] = ay; m[10] = 1.f; m[15] = 1.f;
    m[12] = bx; m[13] = by;   /* row-major v' = v * M, applied to (x/w, y/w): as w-weighted terms */
}

/* P102: mesh submit GE words written straight into sceGu's list (same
 * words sceGu writes, PROJ 0x3E/0x3F, TEXSCALE 0x48-0x4B, CLIPENABLE 0x1C,
 * VTYPE 0x12, BASE 0x10, IADDR 0x02, VADDR 0x01, PRIM 0x04), skipping the
 * ones the GE already holds, and the stall-address syscall sceGuDrawArray
 * makes on every draw (now every 8th mesh draw).  The writer keeps its own
 * state (ge_w); only the list pointer is shared with sceGu.  MVC2_GE_SCEGU=1: the sceGu calls;
 * MVC2_GE_CHECK=1: compare the words with sceGu's (ge_mesh_check). */
typedef struct { unsigned int *start, *current; int parent_context; } GuListInt;   /* libpspgu's GuDisplayList */
extern GuListInt *gu_list;
extern int ge_list_executed[];
static struct {
    unsigned gen;              /* list_gen the shadow below belongs to */
    uint32_t clip_off, ts_ok, proj_ok;
    uint32_t ts[4], proj[16];
    unsigned since_stall;
} ge_w = { ~0u };
static int ge_mode = -1;       /* 0 sceGu, 1 writer; MVC2_GE_CHECK adds 4 */
#define GE_F(cmd, f) ((uint32_t)(cmd) << 24 | ge_fbits(f) >> 8)
static inline uint32_t ge_fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static void ge_sync_shadow(void)
{
    if (ge_w.gen != R.list_gen) { ge_w.gen = R.list_gen; ge_w.clip_off = ge_w.ts_ok = ge_w.proj_ok = 0; ge_w.since_stall = 0; }
}
/* words for texscale/offset, projection and the draw into o; returns the count */
static int ge_mesh_words(uint32_t *o, const GuTexScaleArgs *ts, const float *proj, const void *idx, const void *verts, int count)
{
    uint32_t *b = o;
    if (ts) {
        uint32_t w[4] = { GE_F(0x48, ts->su), GE_F(0x49, ts->sv), GE_F(0x4A, ts->ou), GE_F(0x4B, ts->ov) };
        if (!ge_w.ts_ok || memcmp(w, ge_w.ts, sizeof w)) {
            memcpy(o, w, sizeof w); o += 4;
            memcpy(ge_w.ts, w, sizeof w); ge_w.ts_ok = 1;
        }
    }
    if (!ge_w.clip_off) { *o++ = 0x1C000000u; ge_w.clip_off = 1; }
    {
        uint32_t w[16];
        for (int k = 0; k < 16; k++) w[k] = GE_F(0x3F, proj[k]);
        if (!ge_w.proj_ok || memcmp(w, ge_w.proj, sizeof w)) {
            *o++ = 0x3E000000u;
            memcpy(o, w, sizeof w); o += 16;
            memcpy(ge_w.proj, w, sizeof w); ge_w.proj_ok = 1;
        }
    }
    *o++ = 0x12000000u | ((GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D) & 0xFFFFFF);
    *o++ = 0x10000000u | (((uint32_t)(uintptr_t)idx >> 8) & 0xF0000u);
    *o++ = 0x02000000u | ((uint32_t)(uintptr_t)idx & 0xFFFFFFu);
    *o++ = 0x10000000u | (((uint32_t)(uintptr_t)verts >> 8) & 0xF0000u);
    *o++ = 0x01000000u | ((uint32_t)(uintptr_t)verts & 0xFFFFFFu);
    *o++ = 0x04000000u | ((((uint32_t)GU_TRIANGLES << 16) | (uint32_t)count) & 0xFFFFFFu);
    return (int)(o - b);
}
/* MVC2_GE_CHECK: the words sceGu writes for the same state (into a scratch
 * list), against the writer's with its shadow cleared */
static void ge_mesh_check(const GuTexScaleArgs *ts, const float *proj, const void *idx, const void *verts, int count)
{
    static unsigned bad, n;
    uint32_t a[48], b[48], *save = gu_list->current, keep[sizeof ge_w / 4];
    int na, nb;
    gu_list->current = a;
    if (ts) gu_texscale(ts);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4 *)proj);
    na = (int)(gu_list->current - a);
    gu_list->current = save;
    memcpy(keep, &ge_w, sizeof keep);
    ge_w.clip_off = ge_w.ts_ok = ge_w.proj_ok = 0;
    nb = ge_mesh_words(b, ts, proj, idx, verts, count) - 6;   /* the draw words are sceGuDrawArray's by construction */
    memcpy(&ge_w, keep, sizeof keep);
    n++;
    if (na != nb || memcmp(a, b, (size_t)na * 4)) {
        if (bad++ < 8) fprintf(stderr, "[ge] check: draw %u: %d sceGu words, %d writer words differ\n", n, na, nb);
    }
    if ((n & 0x3FFF) == 0) fprintf(stderr, "[ge] check: %u draws, %u differ\n", n, bad);
}
static void ge_mesh_emit(const GuTexScaleArgs *ts, const float *proj, const void *idx, const void *verts, int count)
{
    uint32_t buf[48];
    int n;
    ge_sync_shadow();
    n = ge_mesh_words(buf, ts, proj, idx, verts, count);
    {   /* uncached, as sceGu: through cached lines and a write-back was slower on hardware */
        volatile uint32_t *d = gu_list->current;
        for (int k = 0; k < n; k++) d[k] = buf[k];
    }
    gu_list->current += n;
    if (++ge_w.since_stall >= 8) {   /* let the GE run what is written */
        sceGeListUpdateStallAddr(ge_list_executed[0], gu_list->current);
        ge_w.since_stall = 0;
    }
}

/* One indexed 3D draw: GuVtx-layout vertices (u, v, 8888 colour, x, y, z),
 * transformed by clip (row-major, v' = v * M, GE clip space), textured from
 * an fl texture handle (0 = untextured) with UVs scaled (su, sv) and offset
 * (ou, ov) in texture units of the handle's w x h.  ge_verts: where the GE
 * reads the same vertices (P108 VRAM copy), NULL = verts; ge_idx likewise for idx. */
typedef struct GuVtxIdx { float u, v; uint32_t c; float x, y, z; } GuVtxIdx;   /* r_gu_mesh.c GVtx layout */
/* depth zoom: z' = (z + w) * DEPTH_ZOOM - w, as depth_of.  A mesh
     * wholly nearer than the zoomed window (the ending's CAPCOM logo at
     * NDC -0.6) would land past the GE's depth range and be dropped;
     * depth_of clamps such depths to the top, so clamp the mesh too
     * (sampled from a few of its vertices) */
static void depth_zoom(float *m, const void *verts, const uint16_t *idx, int count)
{
    const GuVtxIdx *vv = (const GuVtxIdx *)verts;
    int near = 1;
    float zs = -1.f;
    for (int k = 0; k < 4 && near; k++) {
        const float *q = &vv[idx[k * (count - 1) / 3]].x;
        float zc = q[0] * m[2] + q[1] * m[6] + q[2] * m[10] + m[14];
        float wc = q[0] * m[3] + q[1] * m[7] + q[2] * m[11] + m[15];
        near = wc > 0.f && zc / wc > -1.f + 2.f / DEPTH_ZOOM;
        if (near && zc / wc > zs) zs = zc / wc;
    }
    /* near: the same zoom, anchored so the mesh's nearest sampled depth
     * sits just under the top (its layers keep their order) */
    for (int r = 0; r < 4; r++)
        m[r * 4 + 2] = near ? m[r * 4 + 2] * DEPTH_ZOOM + m[r * 4 + 3] * (0.999f - zs * DEPTH_ZOOM)
                            : (m[r * 4 + 2] + m[r * 4 + 3]) * DEPTH_ZOOM - m[r * 4 + 3];
}

static int mesh_emit(const float clip[16], uint32_t tex, const void *verts, const void *ge_verts, const uint16_t *idx,
                     const uint16_t *ge_idx, int count, float su, float sv, float ou, float ov, int flushed)
{
    static ScePspFMatrix4 ident = { {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1} };
    static unsigned ident_gen = ~0u;   /* list_gen whose GE state already has VIEW/MODEL = identity */
    ScePspFMatrix4 proj;
    GuTexScaleArgs tsp;
    const int prof = psp_prof_fine();
    unsigned long long t0 = prof ? psp_prof_now() : 0, t1;
    int i = (int)(tex & 0xFFFF) - 1, p = (int)(tex >> 16) - 1;
    if (!R.inited || count <= 0) return 0;
    if (ge_mode < 0) ge_mode = (getenv("MVC2_GE_SCEGU") ? 0 : 1) | (getenv("MVC2_GE_CHECK") ? 4 : 0);
    tsp.su = -1e30f;
    if (!flushed) { r_flush(); canvas_bind(); }
    if (prof) { t1 = psp_prof_now(); psp_prof_add(37, "          submit: flush + canvas", t1 - t0); t0 = t1; }
    if (tex && i >= 0 && i < MAX_TEX && R.tex[i].used && R.tex[i].pw <= 512 && R.tex[i].ph <= 512) {
        TexRec *t = &R.tex[i];
        GuTexScaleArgs ts = { su * t->w / t->pw, sv * t->h / t->ph, ou * t->w / t->pw, ov * t->h / t->ph };
        const uint32_t *clut = (t->fmt == FL_TEX_IDX4 || t->fmt == FL_TEX_IDX8) && p >= 0 && p < MAX_PAL && R.pal[p].used
                                   ? R.pal[p].clut : NULL;
        uint32_t key[7] = { (uint32_t)i, (uint32_t)p, (uint32_t)(uintptr_t)t->pix, (uint32_t)t->pw << 16 | (uint32_t)t->ph,
                            (uint32_t)t->fmt, (uint32_t)(uintptr_t)clut, R.state_ad0 & 0x30000u };
        if (mesh_tex_ok && !memcmp(key, mesh_tex_key, sizeof key)) {
            t->gen = R.list_gen;   /* still in use by this list (in_use_sync) */
            if (clut) R.pal[p].gen = R.list_gen;
        } else {
            tex_bind(t, p);
            memcpy(mesh_tex_key, key, sizeof key);
            mesh_tex_ok = 1;
        }
        if (ge_mode & 3) tsp = ts; else gu_texscale(&ts);
    } else {
        sceGuDisable(GU_TEXTURE_2D);
        mesh_tex_ok = 0;
    }
    if (prof) { t1 = psp_prof_now(); psp_prof_add(38, "          submit: tex bind", t1 - t0); t0 = t1; }
    {
        uint32_t bk[8] = { (uint32_t)R.blend_ok, (uint32_t)R.bl_op, (uint32_t)R.bl_src, (uint32_t)R.bl_dst,
                           (uint32_t)R.bl_srcfix, (uint32_t)R.bl_dstfix, (uint32_t)R.ztest, (uint32_t)R.zwrite };
        if (!mesh_bd_ok || memcmp(bk, mesh_bd_key, sizeof bk)) {
            apply_blend_depth();
            memcpy(mesh_bd_key, bk, sizeof bk);
            mesh_bd_ok = 1;
        }
    }
    memcpy(&proj, clip, sizeof proj);   /* sceGuSetMatrix copies the values into the list */
    depth_zoom((float *)&proj, verts, idx, count);
    /* GE clip planes cull clip-space z < 0, where the game's depth range
     * (larger = nearer, as the GS) puts every mesh; the VU1 reject above
     * (r_gu_mesh.c) stands in for the GS's own guard-band handling */
    if (ident_gen != R.list_gen) {   /* nothing else sets VIEW/MODEL (2D draws do not transform) */
        sceGuSetMatrix(GU_VIEW, &ident);
        sceGuSetMatrix(GU_MODEL, &ident);
        ident_gen = R.list_gen;
    }
    if (ge_mode & 3) {
        if (prof) { t1 = psp_prof_now(); psp_prof_add(39, "          submit: blend/depth + matrices", t1 - t0); t0 = t1; }
        if (ge_mode & 4) ge_mesh_check(tsp.su != -1e30f ? &tsp : NULL, (const float *)&proj, ge_idx ? ge_idx : idx, ge_verts ? ge_verts : verts, count);
        ge_mesh_emit(tsp.su != -1e30f ? &tsp : NULL, (const float *)&proj, ge_idx ? ge_idx : idx, ge_verts ? ge_verts : verts, count);
    } else {
        sceGuDisable(GU_CLIP_PLANES);
        sceGuSetMatrix(GU_PROJECTION, &proj);
        if (prof) { t1 = psp_prof_now(); psp_prof_add(39, "          submit: blend/depth + matrices", t1 - t0); t0 = t1; }
        GuDrawArgs d = { GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                         count, ge_idx ? ge_idx : idx, ge_verts ? ge_verts : verts };
        gu_draw(&d);
    }
    if (prof) psp_prof_add(40, "          submit: draw", psp_prof_now() - t0);
    R.cur_tex = -1; R.cur_mode = -1;   /* texture state changed: the next 2D batch rebinds */
    return 1;
}

/* P103: mesh submits are recorded (clip copied, the state they read
 * snapshotted) and turned into GE words by rec_build in one pass, before
 * anything else writes to the list (2D batch, clear, fill, scissor, a
 * texture or CLUT about to change, list close).  The words are mesh_emit's,
 * run with each item's state in R.  MVC2_REC_OFF=1: emit at the call, as
 * before.  MVC2_REC_CHECK=1: hash each item's indices and the vertices they
 * use when recorded and again when built; a difference is logged. */
typedef struct RecItem {
    float clip[16];
    const void *verts, *ge_verts;
    const uint16_t *idx, *ge_idx;
    uint32_t tex, ad0, chk;
    int count;
    float su, sv, ou, ov;
    int blend_ok, bl_op, bl_src, bl_dst, ztest, zwrite;
    unsigned bl_srcfix, bl_dstfix;
} RecItem;
#define REC_MAX 128
static RecItem rec[REC_MAX];
static int rec_n, rec_mode = -1, rec_busy;
static unsigned rec_bad, rec_items;

static uint32_t rec_hash(const RecItem *it)
{
    uint32_t h = 2166136261u;
    int top = 0;
    for (int k = 0; k < it->count; k++) { h = (h ^ it->idx[k]) * 16777619u; if (it->idx[k] > top) top = it->idx[k]; }
    for (int k = 0; k < (top + 1) * 24; k++) h = (h ^ ((const uint8_t *)it->verts)[k]) * 16777619u;
    return h;
}

/* P104: a run's words built by gume_build (r_gu_me.c) into a block
 * reserved in the list (sceGuGetMemory) that the list CALLs.  The main CPU
 * resolves what needs its state (texture key, VRAM copies, in-use gens,
 * depth zoom); the rest runs on the Media Engine (MVC2_ME_BUILD=1) while
 * the main CPU goes on.  Until the ME is done the stall address is held at
 * the CALL (__wrap_sceGeListUpdateStallAddr); list_close waits for it.
 * MVC2_ME_BUILD=2 (the default, P116): the main CPU runs gume_build at the
 * hand-off; =0: mesh_emit's sceGu path (A/B).  MVC2_ME_CHECK=1 (implies 2):
 * every item's words against sceGu's for the same state. */
static int me_on;   /* 0 mesh_emit, 1 ME, 2 main CPU (default); +4 check */
static uint32_t __attribute__((aligned(64))) me_shared[96];
static uint8_t __attribute__((aligned(64))) me_stack[16384];
static MeItem __attribute__((aligned(64))) me_items[MS_NJOB][REC_MAX];
static MeShadow me_shadow;
static uint32_t me_head, me_words0;
static unsigned me_gen = ~0u;
static uint32_t me_bind[5];   /* tbp, pw, ph, clut, flag bits of the last bind (the GE holds it while mesh_tex_ok) */
static struct { uint32_t seq; void *pos; } me_pend[MS_NJOB];
static void *me_stall;
static unsigned me_held, me_local_words, me_last_bound, me_jobs;
static uint32_t *me_last_blk;
static volatile uint32_t *me_sh(void) { return (volatile uint32_t *)((uintptr_t)me_shared | 0x40000000u); }

int __real_sceGeListUpdateStallAddr(int qid, void *stall);
static void me_poll(void)
{
    uint32_t tail = me_sh()[MS_TAIL];
    int k = 0;
    while (k < me_pend_n && (int32_t)(tail - me_pend[k].seq) > 0) k++;
    if (!k) return;
    for (int j = k; j < me_pend_n; j++) me_pend[j - k] = me_pend[j];
    me_pend_n -= k;
}
int __wrap_sceGeListUpdateStallAddr(int qid, void *stall)
{
    me_stall = stall;
    if (me_pend_n) {
        me_poll();
        if (me_pend_n) { stall = me_pend[0].pos; me_held++; }
    }
    return __real_sceGeListUpdateStallAddr(qid, stall);
}
/* jobs the ME finished: let the GE run on to the next pending CALL */
static void me_release(void)
{
    int n0 = me_pend_n;
    if (!n0) return;
    me_poll();
    if (me_pend_n != n0) __real_sceGeListUpdateStallAddr(ge_list_executed[0], me_pend_n ? me_pend[0].pos : me_stall);
}
static void me_wait_all(void)
{
    unsigned long long t;
    if (!me_pend_n) return;
    t = psp_prof_on() ? psp_prof_now() : 0;
    while (me_sh()[MS_TAIL] != me_head)
        for (int k = 0; k < 50; k++) __asm__ volatile("nop");
    me_release();
    if (t) psp_prof_add(54, "  ME list build wait (list close)", psp_prof_now() - t);
}

void r_gu_me_stop(void);
/* boot our loop on the ME (start, and after a wake, P117); falls back to
 * the main CPU when it does not start */
static int me_want, me_away, me_saw_suspend;   /* ME asked for; given back for a sleep; suspend seen since */
static unsigned long long me_away_t;
__attribute__((noinline, cold)) static int me_take(void)
{
    extern int psp_me_boot_into(void (*entry)(volatile uint32_t *), void *stack_top, void *shared);
    uint32_t b;
    int r;
    memset(me_shared, 0, sizeof me_shared);
    me_head = me_words0 = 0;
    me_pend_n = 0;
    r = psp_me_boot_into(gume_serve, me_stack + sizeof me_stack, me_shared);
    b = me_sh()[MS_BEAT];
    sceKernelDelayThread(2000);
    if (me_sh()[MS_BEAT] == b) {   /* back to the firmware, which also drops the power lock */
        extern int psp_me_give_back(int *state);
        int st = 0, g = psp_me_give_back(&st);
        fprintf(stderr, "[me] list build: ME did not start (%d; given back %d, state %d), main CPU\n", r, g, st);
        me_on = (me_on & 4) | 2;
        return 0;
    }
    me_on = (me_on & 4) | 1;
    return 1;
}

/* P117, once per logic step: on a sleep request the ME goes back to the
 * firmware (its sleep waits on the firmware's ME code) and the list build to
 * the main CPU; after the wake (or 3 s without a suspend) it is taken again */
void r_gu_me_poll(void)
{
    extern int psp_sleep_asked(void), psp_woke(void), psp_suspended(void);
    extern int psp_me_give_back(int *state);
    if (!me_want) return;
    if (!me_away) {
        int r, st = 0;
        static long test_at = -2;   /* MVC2_ME_GIVEBACK_AT=frame: as if sleep were asked then (testing) */
        extern volatile long mvc2_host_frame;
        int asked = psp_sleep_asked();
        if (test_at == -2) { const char *e = getenv("MVC2_ME_GIVEBACK_AT"); test_at = e ? atol(e) : -1; }
        if (test_at >= 0 && mvc2_host_frame == test_at) asked = 1;
        if (!asked || (me_on & 3) != 1) return;
        fprintf(stderr, "[me] sleep asked: giving the ME back\n");
        me_wait_all();
        r_gu_me_stop();
        r = psp_me_give_back(&st);
        fprintf(stderr, "[me] sleep asked: ME given back to the firmware (%d, state %d)\n", r, st);
        me_away = 1; me_saw_suspend = 0;
        me_away_t = sceKernelGetSystemTimeWide();
        return;
    }
    if (psp_suspended()) me_saw_suspend = 1;
    if (psp_woke() || (!me_saw_suspend && sceKernelGetSystemTimeWide() - me_away_t > 3000000ull)) {
        me_away = 0;
        fprintf(stderr, "[me] %s: list build %s\n", me_saw_suspend ? "wake" : "no sleep came",
                me_take() ? "on the Media Engine again" : "stays on the main CPU");
    }
}

__attribute__((noinline, cold)) static void me_init(void)
{
    const char *e = getenv("MVC2_ME_BUILD");
    me_on = e ? atoi(e) & 3 : 2;   /* default: the main CPU (P116) */
    if (getenv("MVC2_ME_CHECK")) me_on = 2 | 4;
    if (me_on == 3) me_on = 0;
    if (me_on == 1 && getenv("ME_SPIKE")) { fprintf(stderr, "[me] ME_SPIKE set: list build stays on the main CPU\n"); me_on = 2; }
    if (me_on == 1) { me_want = 1; me_take(); }
    if (me_on & 5) fprintf(stderr, "[me] list build on the %s%s\n", (me_on & 3) == 1 ? "Media Engine" : "main CPU", me_on & 4 ? ", checked against sceGu" : "");
}

/* program exit: the ME leaves our code (which the next program overwrites)
 * for the park loop in its own memory */
void r_gu_me_stop(void)
{
    extern uint32_t psp_me_park_addr(void);
    volatile uint32_t *sh = me_sh();
    if ((me_on & 3) != 1) return;
    sh[MS_PARK] = psp_me_park_addr();
    __asm__ volatile("sync" ::: "memory");
    sh[MS_QUIT] = 1;
    for (int i = 0; i < 100 && !sh[MS_PARKED]; i++) sceKernelDelayThread(100);
    fprintf(stderr, "[me] list build: ME %s\n", sh[MS_PARKED] ? "parked" : "did not answer");
    me_on = 2;
    memset(&me_shadow, 0, sizeof me_shadow);   /* the GE's state is not known here */
}

/* MVC2_ME_CHECK: one item's words (bind forced, shadow empty) against the
 * sceGu calls mesh_emit made for the same state, into a scratch list */
__attribute__((noinline, cold)) static void me_check(const MeItem *m)
{
    static ScePspFMatrix4 ident = { {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1} };
    static unsigned bad, n;
    MeItem c = *m;
    MeShadow s0;
    uint32_t a[ME_ITEM_WORDS + ME_EXTRA_WORDS], b[ME_ITEM_WORDS + ME_EXTRA_WORDS + 8], *save = gu_list->current;
    int na, nb;
    const uint32_t *d = m->bd;
    memset(&s0, 0, sizeof s0);
    if (c.flags & MI_TEX) c.flags |= MI_BIND;
    na = gume_build(&c, 1, MJ_RESET_BD | MJ_NEWLIST, &s0, a);
    if (na > ME_ITEM_WORDS + ME_EXTRA_WORDS && bad++ < 8) fprintf(stderr, "[me] check: item %u: %d words, over the bound\n", n + 1, na);
    na -= 7;   /* draw words (sceGuDrawArray's, P102) and RET */
    gu_list->current = b;
    if (c.flags & MI_TEX) {
        GuTexImageArgs ti = { 0, (int)c.pw, (int)c.ph, (int)c.pw, (const void *)(uintptr_t)c.tbp };
        int filt = (c.flags & MI_NEAREST) ? GU_NEAREST : GU_LINEAR, wrap = (c.flags & MI_CLAMP) ? GU_CLAMP : GU_REPEAT;
        sceGuEnable(GU_TEXTURE_2D);
        if (c.flags & MI_IDX) {
            sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
            sceGuClutLoad(256 / 8, (const void *)(uintptr_t)c.clut);
            sceGuTexMode(GU_PSM_T8, 0, 0, (c.flags & MI_SWZ) ? 1 : 0);
        } else {
            sceGuTexMode(GU_PSM_8888, 0, 0, (c.flags & MI_SWZ) ? 1 : 0);
        }
        gu_teximage(&ti);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexFilter(filt, filt);
        sceGuTexWrap(wrap, wrap);
        sceGuTexFlush();
    } else {
        sceGuDisable(GU_TEXTURE_2D);
    }
    if (d[0]) { GuBlendArgs bl = { (int)d[1], (int)d[2], (int)d[3], d[4], d[5] }; sceGuEnable(GU_BLEND); gu_blend(&bl); }
    else sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(d[6] == 0 ? GU_NEVER : d[6] == 3 ? GU_GREATER : d[6] == 7 ? GU_ALWAYS : GU_GEQUAL);
    sceGuDepthMask(d[7] ? GU_FALSE : GU_TRUE);
    sceGuSetMatrix(GU_VIEW, &ident);
    sceGuSetMatrix(GU_MODEL, &ident);
    if (c.flags & MI_TEX) {
        GuTexScaleArgs ts;
        memcpy(&ts, c.ts, sizeof ts);
        gu_texscale(&ts);
    }
    sceGuDisable(GU_CLIP_PLANES);
    sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4 *)c.proj);
    nb = (int)(gu_list->current - b);
    gu_list->current = save;
    n++;
    if (na != nb || memcmp(a, b, (size_t)na * 4)) {
        if (bad++ < 8) {
            int k = 0;
            while (k < na && k < nb && a[k] == b[k]) k++;
            fprintf(stderr, "[me] check: item %u: %d words, sceGu %d; first difference at %d: %08x, sceGu %08x\n",
                    n, na, nb, k, k < na ? (unsigned)a[k] : 0u, k < nb ? (unsigned)b[k] : 0u);
        }
    }
    if ((n & 0x3FFF) == 0) fprintf(stderr, "[me] check: %u items, %u differ\n", n, bad);
}

/* rec[] -> one job: items resolved here, words by gume_build */
static void rec_build_me(void)
{
    volatile uint32_t *sh = me_sh();
    int slot = (int)(me_head % MS_NJOB);
    MeItem *mi = me_items[(me_on & 3) == 1 ? slot : 0];
    unsigned flags = 0, bound = ME_EXTRA_WORDS;
    uint32_t *blk;
    if ((me_on & 3) == 1 && (int32_t)(me_head - sh[MS_TAIL]) >= MS_NJOB) {   /* every slot still being built */
        unsigned long long t = psp_prof_on() ? psp_prof_now() : 0;
        while ((int32_t)(me_head - sh[MS_TAIL]) >= MS_NJOB) for (int k = 0; k < 50; k++) __asm__ volatile("nop");
        me_release();
        if (t) psp_prof_add(55, "  ME list build wait (slots full)", psp_prof_now() - t);
    }
    for (int k = 0; k < rec_n; k++) {
        const RecItem *it = &rec[k];
        MeItem *m = &mi[k];
        int i = (int)(it->tex & 0xFFFF) - 1, p = (int)(it->tex >> 16) - 1;
        uint32_t f = 0;
        if (rec_mode == 2 && rec_hash(it) != it->chk && rec_bad++ < 8)
            fprintf(stderr, "[rec] item %u: vertex or index data changed before the build\n", rec_items + (unsigned)k);
        if (it->tex && i >= 0 && i < MAX_TEX && R.tex[i].used && R.tex[i].pw <= 512 && R.tex[i].ph <= 512) {
            TexRec *t = &R.tex[i];
            float ts[4] = { it->su * t->w / t->pw, it->sv * t->h / t->ph, it->ou * t->w / t->pw, it->ov * t->h / t->ph };
            const uint32_t *clut = (t->fmt == FL_TEX_IDX4 || t->fmt == FL_TEX_IDX8) && p >= 0 && p < MAX_PAL && R.pal[p].used
                                       ? R.pal[p].clut : NULL;
            uint32_t key[7] = { (uint32_t)i, (uint32_t)p, (uint32_t)(uintptr_t)t->pix, (uint32_t)t->pw << 16 | (uint32_t)t->ph,
                                (uint32_t)t->fmt, (uint32_t)(uintptr_t)clut, it->ad0 & 0x30000u };
            f = MI_TEX;
            if (mesh_tex_ok && !memcmp(key, mesh_tex_key, sizeof key)) {
                t->gen = R.list_gen;
                if (clut) R.pal[p].gen = R.list_gen;
            } else {
                int idx8 = t->fmt == FL_TEX_IDX4 || t->fmt == FL_TEX_IDX8;
                me_bind[0] = (uint32_t)(uintptr_t)tex_addr(t);
                t->gen = R.list_gen;
                me_bind[1] = (uint32_t)t->pw; me_bind[2] = (uint32_t)t->ph;
                me_bind[3] = 0;
                if (idx8) {
                    const uint32_t *c = clut ? clut : black_clut;
                    if (clut) R.pal[p].gen = R.list_gen;
                    me_bind[3] = (uint32_t)(uintptr_t)c;
                }
                me_bind[4] = (idx8 ? MI_IDX : 0) | (t->swz ? MI_SWZ : 0) | ((it->ad0 & 0x10000) ? MI_NEAREST : 0) | ((it->ad0 & 0x20000) ? MI_CLAMP : 0);
                memcpy(mesh_tex_key, key, sizeof key);
                mesh_tex_ok = 1;
                f |= MI_BIND;
            }
            f |= me_bind[4];
            m->tbp = me_bind[0]; m->pw = me_bind[1]; m->ph = me_bind[2]; m->clut = me_bind[3];
            memcpy(m->ts, ts, sizeof ts);
        } else {
            mesh_tex_ok = 0;
        }
        m->flags = f;
        {
            float pj[16];
            memcpy(pj, it->clip, sizeof pj);
            depth_zoom(pj, it->verts, it->idx, it->count);
            memcpy(m->proj, pj, sizeof pj);
        }
        m->vaddr = (uint32_t)(uintptr_t)(it->ge_verts ? it->ge_verts : it->verts);
        m->iaddr = (uint32_t)(uintptr_t)(it->ge_idx ? it->ge_idx : it->idx);
        m->count = (uint32_t)it->count;
        m->bd[0] = (uint32_t)it->blend_ok; m->bd[1] = (uint32_t)it->bl_op; m->bd[2] = (uint32_t)it->bl_src;
        m->bd[3] = (uint32_t)it->bl_dst; m->bd[4] = it->bl_srcfix; m->bd[5] = it->bl_dstfix;
        m->bd[6] = (uint32_t)it->ztest; m->bd[7] = (uint32_t)it->zwrite;
        if (me_on & 4) me_check(m);
        bound += ME_ITEM_WORDS;
    }
    if (!mesh_bd_ok) flags |= MJ_RESET_BD;
    mesh_bd_ok = 1;
    if (me_gen != R.list_gen) { flags |= MJ_NEWLIST; me_gen = R.list_gen; }
    /* the block, reserved by hand (as sceGuGetMemory: BASE + JUMP over it),
     * so the GE never reaches its words before they are final: the main CPU
     * writes them before the JUMP; with the ME the GE is held before the JUMP */
    {
        uint32_t *jmp = (uint32_t *)gu_list->current, *end;
        blk = jmp + 2;
        end = blk + bound;
        if ((me_on & 3) == 1) {
            volatile uint32_t *j = sh + MS_JOBS + 4 * slot;
            sceKernelDcacheWritebackRange(mi, (unsigned)rec_n * sizeof *mi);
            j[0] = (uint32_t)(uintptr_t)mi | 0xA0000000u;
            j[1] = (uint32_t)rec_n;
            j[2] = flags;
            j[3] = ((uint32_t)(uintptr_t)blk & 0x1FFFFFFFu) | 0xA0000000u;
            me_pend[me_pend_n].seq = me_head; me_pend[me_pend_n].pos = jmp; me_pend_n++;
            __asm__ volatile("sync" ::: "memory");
            sh[MS_HEAD] = ++me_head;
        } else {
            unsigned w = (unsigned)gume_build(mi, (unsigned)rec_n, flags, &me_shadow, blk);
            me_local_words += w;
            if (w > bound) { fprintf(stderr, "[me] block overflow: %u words, %u reserved\n", w, bound); abort(); }
        }
        me_last_blk = blk; me_last_bound = bound; me_jobs++;
        jmp[0] = 0x10000000u | (((uint32_t)(uintptr_t)end >> 8) & 0xF0000u);
        jmp[1] = 0x08000000u | ((uint32_t)(uintptr_t)end & 0xFFFFFFu);
        gu_list->current = end;
        sceGuCallList(blk);   /* its stall update: held at jmp while the ME builds */
    }
    R.cur_tex = -1; R.cur_mode = -1;
}

/* P104 diagnosis: a list that does not end in 2 s is logged */
static void me_diag(void)
{
    if (me_on) {
        int st = 0, k;
        for (k = 0; k < 2000 && (st = sceGeListSync(ge_list_executed[0], 1)) != PSP_GE_LIST_DONE; k++) sceKernelDelayThread(1000);
        if (st != PSP_GE_LIST_DONE) {
            fprintf(stderr, "[me] list stuck: state %d, list %p..%p, stall %p, pend %d, jobs %u, last block %p (%u words)\n",
                    st, (void *)list, (void *)gu_list->current, me_stall, me_pend_n, me_jobs, (void *)me_last_blk, me_last_bound);
            fprintf(stderr, "[me] GE: base %08x vaddr %08x iaddr %08x prim %08x vtype %08x tbp %08x tsize %08x clut %08x\n",
                    sceGeGetCmd(0x10), sceGeGetCmd(0x01), sceGeGetCmd(0x02), sceGeGetCmd(0x04), sceGeGetCmd(0x12),
                    sceGeGetCmd(0xA0), sceGeGetCmd(0xB8), sceGeGetCmd(0xB0));
            fflush(stderr);
        }
    }
}

/* PROFILE: per list close */
static void me_prof(void)
{
    uint32_t w;
    if (!me_on || !psp_prof_on()) return;
    static int last = -1;
    w = (me_on & 3) == 1 ? me_sh()[MS_WORDS] : me_local_words;
    if ((me_on & 3) != last) { last = me_on & 3; me_words0 = w; }   /* the counter changed sides */
    psp_prof_add(56, "  ME list build words (count)", 1000ull * (w - me_words0));
    psp_prof_add(57, "  GE held at an ME call (count)", 1000ull * me_held);
    me_words0 = w; me_held = 0;
}

static void rec_build(void)
{
    if (!rec_n || rec_busy) return;
    rec_busy = 1;
    if (me_on) {
        unsigned long long t = psp_prof_on() ? psp_prof_now() : 0;
        rec_build_me();
        if (t) {
            psp_prof_add(58, "  rec_build (mesh items -> words)", psp_prof_now() - t);
            psp_prof_add(59, "  rec_build runs (count)", 1000);
            psp_prof_add(60, "  rec_build items (count)", 1000ull * (unsigned)rec_n);
        }
        rec_items += (unsigned)rec_n;
        rec_n = 0;
        rec_busy = 0;
        return;
    }
    int b0 = R.blend_ok, b1 = R.bl_op, b2 = R.bl_src, b3 = R.bl_dst, z0 = R.ztest, z1 = R.zwrite;
    unsigned f0 = R.bl_srcfix, f1 = R.bl_dstfix;
    uint32_t a0 = R.state_ad0;
    for (int i = 0; i < rec_n; i++) {
        const RecItem *it = &rec[i];
        if (rec_mode == 2 && rec_hash(it) != it->chk && rec_bad++ < 8)
            fprintf(stderr, "[rec] item %u: vertex or index data changed before the build\n", rec_items + (unsigned)i);
        R.blend_ok = it->blend_ok; R.bl_op = it->bl_op; R.bl_src = it->bl_src; R.bl_dst = it->bl_dst;
        R.bl_srcfix = it->bl_srcfix; R.bl_dstfix = it->bl_dstfix; R.ztest = it->ztest; R.zwrite = it->zwrite;
        R.state_ad0 = it->ad0;
        mesh_emit(it->clip, it->tex, it->verts, it->ge_verts, it->idx, it->ge_idx, it->count, it->su, it->sv, it->ou, it->ov, 1);
    }
    R.blend_ok = b0; R.bl_op = b1; R.bl_src = b2; R.bl_dst = b3; R.ztest = z0; R.zwrite = z1;
    R.bl_srcfix = f0; R.bl_dstfix = f1; R.state_ad0 = a0;
    if (rec_mode == 2 && ((rec_items + (unsigned)rec_n) >> 14) != (rec_items >> 14))
        fprintf(stderr, "[rec] check: %u items, %u changed\n", rec_items + (unsigned)rec_n, rec_bad);
    rec_items += (unsigned)rec_n;
    rec_n = 0;
    rec_busy = 0;
}

int r_gu_mesh_draw(const float clip[16], uint32_t tex, const void *verts, const void *ge_verts, const uint16_t *idx,
                   const uint16_t *ge_idx, int count, float su, float sv, float ou, float ov)
{
    RecItem *it;
    if (!R.inited || count <= 0) return 0;
    if (rec_mode < 0) rec_mode = getenv("MVC2_REC_OFF") ? 0 : getenv("MVC2_REC_CHECK") ? 2 : 1;
    if (!rec_mode) return mesh_emit(clip, tex, verts, ge_verts, idx, ge_idx, count, su, sv, ou, ov, 0);
    if (me_pend_n) me_release();   /* P104: a CALL the ME finished lets the GE on */
    r_flush();       /* a pending 2D batch (and the items before it) first */
    canvas_bind();   /* the frame's clear first */
    if (rec_n == REC_MAX) { if (psp_prof_on()) psp_prof_add(61, "  rec_build at REC_MAX (count)", 1000); rec_build(); }
    it = &rec[rec_n++];
    memcpy(it->clip, clip, sizeof it->clip);
    it->verts = verts; it->ge_verts = ge_verts; it->idx = idx; it->ge_idx = ge_idx;
    it->tex = tex; it->count = count; it->su = su; it->sv = sv; it->ou = ou; it->ov = ov;
    it->blend_ok = R.blend_ok; it->bl_op = R.bl_op; it->bl_src = R.bl_src; it->bl_dst = R.bl_dst;
    it->bl_srcfix = R.bl_srcfix; it->bl_dstfix = R.bl_dstfix; it->ztest = R.ztest; it->zwrite = R.zwrite;
    it->ad0 = R.state_ad0;
    if (rec_mode == 2) it->chk = rec_hash(it);
    R.cur_tex = -1; R.cur_mode = -1;   /* as mesh_emit: the next 2D batch rebinds */
    return 1;
}
