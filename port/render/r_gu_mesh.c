/* PSP mesh path (the game's "clay" meshes, fl_clay_* of render.h) on sceGu,
 * for the PSP build only.  A copy of r_mesh.c's GL-free logic (POL mesh
 * chunk parsing, VU1 descriptor selection, the colour maths of its shader),
 * kept in step with it by hand; r_mesh.c needs GL.
 *
 * Each clay keeps the POL vertices (the game rewrites them through
 * fl_clay_vertices) and a GE vertex buffer (u, v, 8888 colour, x, y, z)
 * whose colours are recomputed only when what they depend on changes (the
 * 0x66 modulate, the format, a vertex write).  Lit meshes are coloured per
 * draw into list memory.  Transform, perspective-correct texturing and
 * clipping are the GE's: one matrix = world * view * projection * the
 * func_003DD5D0 viewport * canvas-to-screen (r_gu_canvas_to_ge).
 *
 * Not reproduced yet (r_mesh.c has them): fog (state 0x8000), the VU1 trivial
 * reject (ADC; the GE clips such triangles and draws them), UV-matrix cross
 * terms (only scale and offset), more than 65536 vertices in one mesh. */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspgu.h>
#include <pspkernel.h>
#include "render.h"

void r_flush(void);
void r_gu_sync(void);
void r_gu_begin(unsigned bytes);
const float *r_matrix(int id);
uint32_t r_state_ad0(void);
uint32_t r_state_ad4(void);
uint32_t r_global_mod(void);
uint32_t r_material_color(void);
const uint8_t *r_material_slot(int i);
const uint8_t *r_light(int i);
void r_viewport(int vi[4], float vf[2]);
uint8_t r_gs_to_unorm(unsigned g);
void r_gu_canvas_to_ge(float m[16]);
void r_gu_canvas_key(float k[4]);
int r_gu_mesh_draw(const float clip[16], uint32_t tex, const void *verts, const void *ge_verts, const uint16_t *idx, const uint16_t *ge_idx,
                   int count, float su, float sv, float ou, float ov);

int r_gu_mesh_cnt[3];   /* DRAWLOG: mesh draws, triangles, microseconds in fl_clay_draw */
int fl_mesh_debug;
int r_gu_mesh_bytes;   /* clay buffers held */      /* fe_mesh.c sets it (MVC2_MESH_DUMP); r_mesh.c logs with it, unused here */

typedef struct MVtx { float pos[3], nrm[3]; float col[4]; float uv[2]; } MVtx;   /* = FlMeshVtx */
typedef struct GVtx { float u, v; uint32_t c; float x, y, z; } GVtx;               /* GE vertex */

typedef struct MRange {
    int first, count;        /* in indices */
    uint32_t tex;            /* material texture handle (mat + 0x44) */
    float diffuse[4], c0[4]; /* mat + 0x04, mat + 0x24 */
} MRange;

typedef struct MMesh {
    uint32_t flags, fmt, key;
    int desc, textured;
    int nverts, nidx;
    MVtx *cpu;
    GVtx *gv;
    uint16_t *idx;
    MRange *range;
    int nrange;
    int dirty;               /* cpu positions/uvs changed */
    uint32_t ckey[3];        /* what gv's colours were computed for */
    int cvalid;
    int gvwb;                /* P88: gv written since its last D-cache writeback */
    int vstale;              /* P108: gv written since its VRAM copy */
    uint32_t voff, vused;    /* P108: VRAM copy of gv (offset, 0 none), pool epoch of its last use */
    uint32_t ioff;           /* P83: VRAM copy of idx after gv's (0 none) */
    long used;               /* host frame gv was last handed to the GE (+1) */
    int bbok;                /* bb holds the cpu positions' bounding box */
    float bb[6];             /* min xyz, max xyz */
    float acm[16];           /* P110: the ADC matrix the memo below was made for */
    int avalid, ahave;       /* memo valid; triangles rejected (aidx/acount hold what is drawn) */
    uint16_t *aidx;          /* the kept triangles' indices (cached RAM, written back), then acount */
    int *acount;             /* kept indices per range */
    long aused;              /* host frame aidx was last handed to the GE (+1) */
} MMesh;

#define MAX_CLAY 0x402
static MMesh *CLAY;
static float clipm[16];
static int have_clipm;

/* ---------------- matrices (as r_mesh.c) ---------------- */
void fl_mat_identity(float m[16])
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

/* P88: VFPU kernels (port/psp/host/psp_vfpu.S); MVC2_NO_VFPU (env.txt) keeps
 * the scalar C for A/B testing */
void vfpu_mat_mul(float out[16], const float a[16], const float b[16]);
int vfpu_adc_codes(const float m[16], const void *mvtx, int n, uint8_t *out);
int vfpu_clip_codes(const float m[16], const void *gvtx, int n, void *out[2]);
void vfpu_bbox_corners(const float m[16], float k[32], float *smax);
void vfpu_bbox(const void *mvtx, int n, float out[6]);
int vfpu_bbox_inside(const float m[16], const float bb[6]);
int vfpu_clipbox_inside(const float m[16], const float bb[6]);
int vfpu_clip_any(const float m[16], const void *gvtx, int n);
void vfpu_mesh_mats(const float *p[7]);
void vfpu_lit(const float p[26], const void *mvtx, const uint16_t *list, float *out3);
static int use_vfpu = -1;
static void fl_mat_mul_c(float out[16], const float a[16], const float b[16]);
static void vfpu_init(void)
{
    float a[16], b[16], r1[16], r2[16], e = 0.f;
    use_vfpu = getenv("MVC2_NO_VFPU") == NULL;
    if (!use_vfpu) return;
    for (int i = 0; i < 16; i++) { a[i] = (float)((i * 37) % 11) - 4.5f; b[i] = (float)((i * 23) % 7) * 0.25f - 0.6f; }
    fl_mat_mul_c(r1, a, b);
    vfpu_mat_mul(r2, a, b);
    for (int i = 0; i < 16; i++) { float d = fabsf(r1[i] - r2[i]); if (d > e) e = d; }
    {   /* P93: vfpu_lit against the scalar formula */
        float p[26], o[3];
        MVtx v;
        uint16_t one = 0;
        memset(&v, 0, sizeof v);
        v.nrm[0] = 0.3f; v.nrm[1] = -0.5f; v.nrm[2] = 0.8f;
        for (int i = 0; i < 18; i++) p[i] = (float)((i * 29) % 13) * 0.1f - 0.4f;
        p[18] = 3.f; p[19] = 5.f; p[20] = 7.f; p[21] = 9.f; p[22] = 20.f; p[23] = 13.f; p[24] = 17.f;
        { int n = 1; memcpy(&p[25], &n, 4); }
        vfpu_lit(p, &v, &one, o);
        for (int c = 0; c < 3; c++) {
            float s = p[18 + c];
            for (int j = 0; j < 3; j++) {
                float d = v.nrm[0] * p[j * 3] + v.nrm[1] * p[j * 3 + 1] + v.nrm[2] * p[j * 3 + 2];
                s += p[9 + j * 3 + c] * (d < 0.f ? 0.f : d);
            }
            float d = fabsf(floorf(fminf(s, p[21]) * p[22 + c]) - o[c]);
            if (d > e) e = d;
        }
    }
    if (e > 1e-4f) { fprintf(stderr, "[gu] VFPU self-test failed (err %g): scalar path\n", (double)e); use_vfpu = 0; }
}

void fl_mat_mul(float out[16], const float a[16], const float b[16])
{
    if (use_vfpu < 0) vfpu_init();
    if (use_vfpu) { vfpu_mat_mul(out, a, b); return; }
    fl_mat_mul_c(out, a, b);
}

static void fl_mat_mul_c(float out[16], const float a[16], const float b[16])
{
    float t[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j] +
                           a[i * 4 + 3] * b[3 * 4 + j];
    memcpy(out, t, sizeof t);
}

void fl_set_clip_matrix(const float m[16])
{
    if (m && memcmp(clipm, m, sizeof clipm)) memcpy(clipm, m, sizeof clipm);   /* P89 */
    have_clipm = m != NULL;
}

/* P96: the game's clip matrix, read at draw time (r_gu_mesh_bind) */
static const float *clip_src;
void r_gu_mesh_bind_clip(const float *m) { clip_src = m; }
void (*r_gu_mesh_check)(void);   /* MVC2_MESH_CHECK (psp_mesh_state.c) */
void r_gu_mesh_lights(void);

/* ---------------- POL parsing (r_mesh.c) ---------------- */
typedef struct Pol { const uint8_t *d; size_t n; } Pol;

static uint32_t rd(const Pol *p, size_t o) { return o + 4 <= p->n ? (uint32_t)p->d[o] | p->d[o + 1] << 8 | p->d[o + 2] << 16 | (uint32_t)p->d[o + 3] << 24 : 0; }
static float rdf(const Pol *p, size_t o) { uint32_t u = rd(p, o); float f; memcpy(&f, &u, 4); return f; }

static size_t chunk_find(const Pol *p, size_t base, uint32_t type, int by_mask)
{
    uint32_t cnt = rd(p, base + 4);
    size_t o = base + 12;
    for (uint32_t i = 0; i < cnt && o + 12 <= p->n; i++) {
        uint32_t t = rd(p, o), sz = rd(p, o + 8);
        if (by_mask ? (t & type) != 0 : t == type) return o;
        if (sz < 12) break;
        o += sz;
    }
    return 0;
}

static uint32_t f2u(float f)
{
    if (!(f > 0.f)) return 0;
    if (f >= 4294967295.f) return 0xFFFFFFFFu;
    return (uint32_t)f;
}

static int desc_select(uint32_t w, uint32_t key)
{
    uint32_t top = w & 0xFF000000u, low = w & 0xFFFFFFu;
    int tex = key == 0x21035 || key == 0x421035, untex = key == 0x21025 || key == 0x421025,
        ext = key == 0x521235 || key == 0x121235;
    if (top == 0x01000000u) return key == 0x21025 ? 47 : -1;
    if (top == 0x06000000u) return ext ? 48 : -1;
    if (top != 0) return -1;
    switch (low) {
    case 0x102: return tex ? 0 : ext ? 1 : untex ? 2 : -1;
    case 0x20102: return tex ? 3 : untex ? 4 : -1;
    case 0x106: return tex ? 5 : ext ? 6 : -1;
    case 0x100: return tex ? 7 : untex ? 8 : -1;
    case 0x20100: return tex ? 9 : untex ? 10 : -1;
    case 0x0: return tex ? 11 : untex ? 12 : -1;
    case 0x2: return tex ? 13 : untex ? 14 : -1;
    case 0x20000: return tex ? 15 : untex ? 16 : -1;
    case 0x20002: return tex ? 17 : untex ? 18 : -1;
    }
    if (!tex) return -1;
    switch (low) {
    case 0x142: return 19;
    case 0x8102: return 20;
    case 0x28102: return 21;
    case 0x8100: return 22;
    case 0x8000: return 23;
    case 0x8002: return 24;
    case 0x28100: return 25;
    case 0x28000: return 26;
    case 0x28002: return 27;
    case 0x4102: return 28;
    case 0x24102: return 29;
    case 0x4100: return 30;
    case 0x24100: return 31;
    case 0x4002: return 32;
    case 0x24002: return 33;
    case 0x4000: return 34;
    case 0x24000: return 35;
    case 0xC102: return 36;
    case 0x2C102: return 37;
    case 0xC100: return 38;
    case 0x2C100: return 39;
    case 0xC002: return 40;
    case 0x2C002: return 41;
    case 0xC000: return 42;
    case 0x2C000: return 43;
    case 0x28040: return 44;
    case 0x2C040: return 45;
    case 0x2C140: return 46;
    }
    return -1;
}

/* uncached pointers (the display list, 0x4xxxxxxx since P85) need none */
static void wb(const void *p, size_t n) { if (p && n && !((uintptr_t)p & 0x40000000u)) sceKernelDcacheWritebackRange(p, (unsigned)n); }

static void build_mesh(MMesh *me, const Pol *p, size_t mo)
{
    uint32_t type = rd(p, mo), attr = type & 0xFFC3FF00u;
    size_t ss, sm, sp, sn, su, sc, cap = 0, nidx = 0, o;
    int nstrip, nv, kind, multi = 0, cur = -1;
    uint32_t *tmp;
    me->desc = -1;
    if ((type & 0xFF) != 2 || (attr != 0xDF00 && attr != 0x9F00)) return;
    ss = chunk_find(p, mo, 0x100, 0); sm = chunk_find(p, mo, 0x400, 0); sp = chunk_find(p, mo, 0x800, 0);
    sn = chunk_find(p, mo, 0x1000, 0); su = chunk_find(p, mo, 0x4000, 0); sc = chunk_find(p, mo, 0x8000, 0);
    if (!ss || !sp) return;
    nstrip = (int)rd(p, ss + 4); nv = (int)rd(p, sp + 4);
    if (nstrip <= 0 || nstrip > (1 << 20) || nv <= 0 || nv > (1 << 20)) return;
    kind = attr == 0xDF00 ? 0x35 : 0x25;
    if (sm) {
        uint32_t first = rd(p, sm + 12);
        for (int i = 1; i < nstrip; i++) if (rd(p, sm + 12 + 4 * i) != first) multi = 1;
    }
    me->key = 0x21000u | (multi ? 0x400000u : 0) | (uint32_t)kind;
    me->fmt = r_state_ad4();
    me->desc = desc_select(me->fmt, me->key);
    me->textured = kind == 0x35;
    if (me->desc < 0) return;
    if (nv > 65536) {
        fprintf(stderr, "[gu] mesh with %d vertices (> 65536): not drawn\n", nv);
        me->desc = -1;
        return;
    }
    me->cpu = (MVtx *)calloc((size_t)nv, sizeof *me->cpu);
    me->gv = (GVtx *)memalign(16, (size_t)nv * sizeof *me->gv);
    if (!me->cpu || !me->gv) { me->desc = -1; return; }
    me->gvwb = me->vstale = 1;
    for (int i = 0; i < nv; i++) {
        MVtx *v = &me->cpu[i];
        for (int k = 0; k < 3; k++) v->pos[k] = rdf(p, sp + 12 + 12 * i + 4 * k);
        if (sn && i < (int)rd(p, sn + 4)) for (int k = 0; k < 3; k++) v->nrm[k] = rdf(p, sn + 12 + 12 * i + 4 * k);
        if (su && i < (int)rd(p, su + 4)) for (int k = 0; k < 2; k++) v->uv[k] = rdf(p, su + 12 + 8 * i + 4 * k);
        if (sc && i < (int)rd(p, sc + 4)) {
            uint32_t r = f2u(rdf(p, sc + 12 + 16 * i)), g = f2u(rdf(p, sc + 16 + 16 * i)),
                     b = f2u(rdf(p, sc + 20 + 16 * i)), a = f2u(rdf(p, sc + 24 + 16 * i));
            uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
            v->col[0] = (float)(uint8_t)(argb >> 16); v->col[1] = (float)(uint8_t)(argb >> 8);
            v->col[2] = (float)(uint8_t)argb; v->col[3] = (float)(uint8_t)(argb >> 24);
        }
    }
    o = ss + 12;
    for (int s = 0; s < nstrip; s++) {
        uint32_t n = rd(p, o) & 0x7FFFFFFFu;
        if (o + 4 + 4 * (size_t)n > p->n) { nstrip = s; break; }
        if (n >= 3) cap += 3 * (n - 2);
        o += 4 + 4 * (size_t)n;
    }
    tmp = (uint32_t *)malloc((cap ? cap : 1) * sizeof *tmp);
    me->range = (MRange *)calloc((size_t)nstrip + 1, sizeof *me->range);
    o = ss + 12;
    for (int s = 0; s < nstrip; s++) {
        uint32_t n = rd(p, o) & 0x7FFFFFFFu;
        int slot = multi ? (int)rd(p, sm + 12 + 4 * s) : 0;
        if (slot != cur) {
            MRange *r = &me->range[me->nrange++];
            const uint8_t *mat = r_material_slot(slot & 31);
            r->first = (int)nidx;
            memcpy(r->diffuse, mat + 0x04, 16);
            memcpy(r->c0, mat + 0x24, 16);
            memcpy(&r->tex, mat + 0x44, 4);
            cur = slot;
        }
        for (uint32_t j = 0; j + 2 < n; j++) {
            uint32_t a = rd(p, o + 4 + 4 * j), b = rd(p, o + 8 + 4 * j), c = rd(p, o + 12 + 4 * j);
            if (a == b || b == c || a == c) continue;
            if ((int)a >= nv || (int)b >= nv || (int)c >= nv) continue;
            tmp[nidx++] = a; tmp[nidx++] = b; tmp[nidx++] = c;
        }
        me->range[me->nrange - 1].count = (int)nidx - me->range[me->nrange - 1].first;
        o += 4 + 4 * (size_t)n;
    }
    me->idx = (uint16_t *)memalign(16, (nidx ? nidx : 1) * sizeof *me->idx);
    for (size_t k = 0; k < nidx; k++) me->idx[k] = (uint16_t)tmp[k];
    free(tmp);
    wb(me->idx, nidx * sizeof *me->idx);
    me->nverts = nv;
    me->nidx = (int)nidx;
    r_gu_mesh_bytes += nv * (int)(sizeof(MVtx) + sizeof(GVtx)) + (int)nidx * 2 + (me->nrange + 1) * (int)sizeof(MRange);
    me->dirty = 1;
    me->bbok = 0;
    me->cvalid = 0;
}

/* ---------------- clay API ---------------- */
static void clay_free(int h)
{
    MMesh *me = &CLAY[h];
    if (me->gv || me->idx) r_gu_sync();
    if (me->gv) r_gu_mesh_bytes -= me->nverts * (int)(sizeof(MVtx) + sizeof(GVtx)) + me->nidx * 2 + (me->nrange + 1) * (int)sizeof(MRange);   /* queued draws may still read them */
    free(me->cpu);
    free(me->gv);
    free(me->idx);
    free(me->range);
    free(me->aidx);
    memset(me, 0, sizeof *me);
    me->desc = -1;
}

int fl_clay_build(int h, const uint8_t *chunk, size_t avail, uint32_t draw_flags)
{
    Pol P = { chunk, avail };
    if (h <= 0 || h >= MAX_CLAY || !chunk) return 0;
    if (!CLAY) {
        CLAY = (MMesh *)calloc(MAX_CLAY, sizeof *CLAY);
        if (!CLAY) return 0;
        for (int i = 0; i < MAX_CLAY; i++) CLAY[i].desc = -1;
    }
    r_flush();
    clay_free(h);
    CLAY[h].flags = draw_flags;
    build_mesh(&CLAY[h], &P, 0);
    return CLAY[h].desc >= 0;
}

int fl_clay_textures(int h, uint32_t *out, int max)
{
    int n = 0;
    if (!CLAY || h <= 0 || h >= MAX_CLAY || CLAY[h].desc < 0) return 0;
    for (int r = 0; r < CLAY[h].nrange && n < max; r++)
        if (CLAY[h].range[r].tex) out[n++] = CLAY[h].range[r].tex;
    return n;
}

uint32_t fl_clay_fmt(int h) { return (CLAY && h > 0 && h < MAX_CLAY) ? CLAY[h].fmt : 0; }

FlMeshVtx *fl_clay_vertices(int h, int *nv)
{
    if (!CLAY || h <= 0 || h >= MAX_CLAY || CLAY[h].desc < 0 || !CLAY[h].cpu) return NULL;
    if (nv) *nv = CLAY[h].nverts;
    return (FlMeshVtx *)CLAY[h].cpu;
}

void fl_clay_vertices_changed(int h)
{
    if (CLAY && h > 0 && h < MAX_CLAY) { CLAY[h].dirty = 1; CLAY[h].bbok = 0; CLAY[h].avalid = 0; }
}

void fl_clay_release(int h)
{
    if (CLAY && h > 0 && h < MAX_CLAY) {
        r_flush();
        clay_free(h);
    }
}

/* ---------------- func_003F0500 ---------------- */
static float gsf(uint32_t c, int sh) { return (float)((c >> sh) & 0xFF); }

/* P83: exact inline stand-ins for the libm calls on the per-vertex path.
 * ffl matches floorf for every finite value (|x| >= 2^23 is already whole);
 * fmn matches fminf including NaN operands. */
static inline float ffl(float x)
{
    if (!(x > -8388608.f && x < 8388608.f)) return floorf(x);
    float t = (float)(int)x;
    return t > x ? t - 1.f : t;
}
static inline float fmn(float a, float b) { return b < a ? b : (a == a ? a : b); }
#define floorf ffl
#define fminf fmn

/* r_gs_to_unorm for 0..1023 (ge_color's clamp range), filled on first use */
static uint8_t unorm_lut[1024];
static int unorm_ok;
#define COLD __attribute__((noinline, cold))   /* P110: off the per-draw path (I-cache) */
#define RARE(x) __builtin_expect(!!(x), 0)      /* P110: the branch laid out after the per-draw path */
/* P110: n words differ (inline: libc memcmp is outside the hot block) */
static inline int words_differ(const void *a, const void *b, int n)
{
    const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
    uint32_t d = 0;
    for (int i = 0; i < n; i++) d |= x[i] ^ y[i];
    return d != 0;
}
COLD static void unorm_init(void)
{
    for (unsigned k = 0; k < 1024; k++) unorm_lut[k] = r_gs_to_unorm(k);
    unorm_ok = 1;
}
#define r_gs_to_unorm(g) unorm_lut[g]

/* GS colour (0x80 = 1.0 when modulating a texture) -> GE 8888 */
static uint32_t ge_color(const float c[4], int textured)
{
    unsigned ch[4];
    for (int k = 0; k < 4; k++) {
        float f = c[k] < 0.f ? 0.f : c[k] > 1023.f ? 1023.f : c[k];
        ch[k] = (unsigned)f;
    }
    if (textured) return r_gs_to_unorm(ch[0]) | (uint32_t)r_gs_to_unorm(ch[1]) << 8 | (uint32_t)r_gs_to_unorm(ch[2]) << 16 |
                         (uint32_t)r_gs_to_unorm(ch[3]) << 24;
    /* untextured: rgb / 255, a / 128 */
    return (ch[0] > 255 ? 255u : ch[0]) | (ch[1] > 255 ? 255u : ch[1]) << 8 | (ch[2] > 255 ? 255u : ch[2]) << 16 |
           (uint32_t)r_gs_to_unorm(ch[3]) << 24;
}

COLD static void positions(MMesh *me, GVtx *g)
{
    for (int i = 0; i < me->nverts; i++) {
        const MVtx *v = &me->cpu[i];
        g[i].x = v->pos[0]; g[i].y = v->pos[1]; g[i].z = v->pos[2];
        g[i].u = v->uv[0]; g[i].v = v->uv[1];
    }
}

/* unlit colours: vertex colour programs and material colour programs */
COLD static void colours_unlit(MMesh *me, GVtx *g, int matcol, const float modv[4], float cscale)
{
    if (!matcol) {
        float cs[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        if (!me->textured) cs[0] = cs[1] = cs[2] = 1.f;   /* func_003EEC30 kind 0x25: rgb not halved */
        for (int i = 0; i < me->nverts; i++) {
            float c[4];
            for (int k = 0; k < 4; k++) c[k] = floorf(me->cpu[i].col[k] * cs[k] * modv[k]);
            g[i].c = ge_color(c, me->textured);
        }
        return;
    }
    for (int r = 0; r < me->nrange; r++) {
        const MRange *rg = &me->range[r];
        float mc[4];
        uint32_t c;
        for (int k = 0; k < 3; k++) mc[k] = floorf(fminf(rg->diffuse[k] * cscale, cscale) * modv[k]);
        mc[3] = floorf(rg->diffuse[3] * 128.f * modv[3]);
        c = ge_color(mc, me->textured);
        for (int k = 0; k < rg->count; k++) g[me->idx[rg->first + k]].c = c;
    }
}

static void v3norm(float v[3])
{
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 0.f) { v[0] /= n; v[1] /= n; v[2] /= n; }
}

static uint32_t *lit_stamp, lit_tag;
static uint16_t *lit_list;
static float *lit_out;
static int lit_cap;

/* lit programs (format 0x100), per draw: the world matrix moves the lights */
COLD static void colours_lit(MMesh *me, GVtx *g, const float modv[4], float cscale)
{
    float ldir[3][3], lamb_base[3];
    const float *w = r_matrix(0x19);
    r_gu_mesh_lights();   /* P96 */
    uint32_t e = r_material_color();
    for (int k = 0; k < 3; k++) {
        const uint8_t *l = r_light(k);
        float d[3];
        memcpy(d, l + 0x34, 12);
        for (int j = 0; j < 3; j++) ldir[k][j] = -(d[0] * w[j * 4 + 0] + d[1] * w[j * 4 + 1] + d[2] * w[j * 4 + 2]);
        v3norm(ldir[k]);
    }
    for (int k = 0; k < 3; k++) {
        float a0, a1, a2;
        memcpy(&a0, r_light(0) + 0x24 + 4 * k, 4); memcpy(&a1, r_light(1) + 0x24 + 4 * k, 4); memcpy(&a2, r_light(2) + 0x24 + 4 * k, 4);
        lamb_base[k] = cscale * (a2 + (a0 + a1)) + gsf(e, 16 - 8 * k) / 255.f * cscale;
    }
    float lcols[3][3];
    for (int k = 0; k < 3; k++) memcpy(lcols[k], r_light(k) + 0x04, 12);
    if (lit_cap < me->nverts) {
        free(lit_stamp); free(lit_list); free(lit_out);
        lit_cap = me->nverts;
        lit_stamp = (uint32_t *)calloc((size_t)lit_cap, sizeof *lit_stamp);
        lit_list = (uint16_t *)malloc((size_t)lit_cap * sizeof *lit_list);
        lit_out = (float *)malloc((size_t)lit_cap * 3 * sizeof *lit_out);
        if (!lit_stamp || !lit_list || !lit_out) { lit_cap = 0; return; }
    }
    for (int r = 0; r < me->nrange; r++) {
        const MRange *rg = &me->range[r];
        float lc[3][3], la[4];
        for (int k = 0; k < 3; k++) {
            for (int c = 0; c < 3; c++) {
                float lcol = lcols[k][c];
                lc[k][c] = fminf(cscale, cscale * rg->diffuse[c] * lcol);
            }
        }
        for (int c = 0; c < 3; c++) la[c] = fminf(cscale, lamb_base[c] * rg->c0[c]);
        la[3] = floorf(128.f * rg->diffuse[3]);
        /* P93: each vertex once per range (indices repeat across triangles) */
        int nu = 0;
        if (++lit_tag == 0) { memset(lit_stamp, 0, (size_t)lit_cap * sizeof *lit_stamp); lit_tag = 1; }
        for (int k = 0; k < rg->count; k++) {
            int i = me->idx[rg->first + k];
            if (lit_stamp[i] != lit_tag) { lit_stamp[i] = lit_tag; lit_list[nu++] = (uint16_t)i; }
        }
        if (use_vfpu > 0) {
            float p[26], col[4];
            for (int j = 0; j < 3; j++) memcpy(&p[j * 3], ldir[j], 12);
            for (int j = 0; j < 3; j++) memcpy(&p[9 + j * 3], lc[j], 12);
            memcpy(&p[18], la, 12); p[21] = cscale; memcpy(&p[22], modv, 12); memcpy(&p[25], &nu, 4);
            vfpu_lit(p, me->cpu, lit_list, lit_out);
            col[3] = floorf(la[3] * modv[3]);
            for (int q = 0; q < nu; q++) {
                memcpy(col, &lit_out[q * 3], 12);
                g[lit_list[q]].c = ge_color(col, me->textured);
            }
            continue;
        }
        for (int k = 0; k < nu; k++) {
            int i = lit_list[k];
            const float *n = me->cpu[i].nrm;
            float d[3], col[4];
            for (int j = 0; j < 3; j++) {
                d[j] = n[0] * ldir[j][0] + n[1] * ldir[j][1] + n[2] * ldir[j][2];
                if (d[j] < 0.f) d[j] = 0.f;
            }
            for (int c = 0; c < 3; c++)
                col[c] = floorf(fminf(lc[0][c] * d[0] + lc[1][c] * d[1] + lc[2][c] * d[2] + la[c], cscale) * modv[c]);
            col[3] = floorf(la[3] * modv[3]);
            g[i].c = ge_color(col, me->textured);
        }
    }
}

static int clay_draw(int h);
int r_discarding(void);
int fl_clay_draw(int h)
{
    if (r_discarding()) return 1;   /* frame skip (P88): the logic ran, nothing is drawn */
    static int dl = -1;   /* P110: the DRAWLOG time costs two timer syscalls a draw */
    unsigned t0;
    int r;
    if (RARE(dl < 0)) dl = getenv("DRAWLOG") != NULL;
    if (RARE(r_gu_mesh_check != NULL)) r_gu_mesh_check();
    t0 = dl ? sceKernelGetSystemTimeLow() : 0;
    r = clay_draw(h);   /* one call site: clay_draw stays inlined in the P101 hot block */
    if (RARE(dl)) r_gu_mesh_cnt[2] += (int)(sceKernelGetSystemTimeLow() - t0);
    return r;
}

/* The clipping VU1 programs (19, 44-46) clip triangles that cross the camera
 * plane; the GE draws with its clip planes off, where a vertex with w <= 0
 * projects to garbage (P28: water near the camera turned grey). Clip those
 * triangles at w = NEAR_W on the CPU. Returns the new index count, or -1 when
 * nothing crosses. */
#define NEAR_W 1.f
#define GUARD 4.f   /* GE NDC; the GE's 4096-pixel coordinate window spans about 8 screens at 480 */
typedef struct CVtx { GVtx v; float x, y, w; } CVtx;   /* vertex + clip-space x, y, w */

static float cdist(const CVtx *p, int pl)
{
    switch (pl) {
    case 0: return p->w - NEAR_W;
    case 1: return GUARD * p->w - p->x;
    case 2: return GUARD * p->w + p->x;
    case 3: return GUARD * p->w - p->y;
    default: return GUARD * p->w + p->y;
    }
}

static CVtx lerp_c(const CVtx *a, const CVtx *b, float t)
{
    CVtx o;
    o.v.u = a->v.u + (b->v.u - a->v.u) * t; o.v.v = a->v.v + (b->v.v - a->v.v) * t;
    o.v.x = a->v.x + (b->v.x - a->v.x) * t; o.v.y = a->v.y + (b->v.y - a->v.y) * t; o.v.z = a->v.z + (b->v.z - a->v.z) * t;
    o.x = a->x + (b->x - a->x) * t; o.y = a->y + (b->y - a->y) * t; o.w = a->w + (b->w - a->w) * t;
    o.v.c = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (float)((a->v.c >> s) & 0xFF), cb = (float)((b->v.c >> s) & 0xFF);
        o.v.c |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
    }
    return o;
}

/* outcodes per vertex (bit per plane), computed once per draw */
static uint8_t *cl_code;
static float *cl_xyw;
static int cl_cap;

COLD static int bbox_corners(MMesh *me, const float cm[16], float k[8][4], float *smaxp);

/* P83: no outcode can be set when every bounding-box corner is inside the
 * near plane and the guard band with a rounding margin (convex, linear) */
COLD static int clip_bbox_inside_c(MMesh *me, const float clip[16])
{
    float k[8][4], smax, m;
    if (!bbox_corners(me, clip, k, &smax)) return 0;
    m = 1e-5f * smax * (GUARD + 1.f);
    for (int c = 0; c < 8; c++) {
        float gw = GUARD * k[c][3] - m;
        if (!(k[c][3] - m >= NEAR_W) || !(fabsf(k[c][0]) < gw) || !(fabsf(k[c][1]) < gw)) return 0;
    }
    return 1;
}
static int clip_bbox_inside(MMesh *me, const float clip[16])
{
    if (use_vfpu > 0 && me->bbok) return vfpu_clipbox_inside(clip, me->bb);   /* P110: in VFPU registers */
    return clip_bbox_inside_c(me, clip);
}

COLD static int clip_prepare(const float clip[16], const GVtx *g, int nverts)
{
    int any = 0;
    if (cl_cap < nverts) {
        free(cl_code); free(cl_xyw);
        cl_cap = nverts;
        cl_code = (uint8_t *)malloc((size_t)nverts);
        cl_xyw = (float *)malloc((size_t)nverts * 3 * sizeof(float));
        if (!cl_code || !cl_xyw) { cl_cap = 0; return 0; }
    }
    if (use_vfpu > 0) {   /* P93: transform and outcodes on the VFPU */
        void *o[2] = { cl_code, cl_xyw };
        return vfpu_clip_codes(clip, g, nverts, o);
    }
    for (int i = 0; i < nverts; i++) {
        const GVtx *p = &g[i];
        float x, y, w;
        x = p->x * clip[0] + p->y * clip[4] + p->z * clip[8] + clip[12];
        y = p->x * clip[1] + p->y * clip[5] + p->z * clip[9] + clip[13];
        w = p->x * clip[3] + p->y * clip[7] + p->z * clip[11] + clip[15];
        float gw = GUARD * w;
        uint8_t c = (uint8_t)((w < NEAR_W) | (x > gw) << 1 | (-x > gw) << 2 | (y > gw) << 3 | (-y > gw) << 4);
        cl_code[i] = c; any |= c;
        cl_xyw[i * 3] = x; cl_xyw[i * 3 + 1] = y; cl_xyw[i * 3 + 2] = w;
    }
    return any;
}

/* Returns the new index count into *oi, or -1 when no triangle of this
 * range needs clipping (draw it as it is).  P110: the vertices go in one
 * buffer per draw (b): the mesh's own, copied once, then each clipped
 * polygon's; triangles that need no clipping keep their indices.  The GE
 * gets the same triangles in the same order as when every one was copied. */
typedef struct NcBuf { GVtx *v; int nv, room; } NcBuf;   /* room: clipped polygons' vertices */
COLD static int near_clip(NcBuf *b, const GVtx *g, int nverts, const uint16_t *idx, int count, uint16_t **oi)
{
    int n = 0, any = 0;
    uint16_t *ix;
    for (int k = 0; k < count && !any; k++) any = cl_code[idx[k]];
    if (!any) return -1;
    if (!b->v) {
        b->v = (GVtx *)sceGuGetMemory((unsigned)(nverts + b->room) * sizeof *b->v);
        memcpy(b->v, g, (size_t)nverts * sizeof *g);   /* list memory: the uncached mirror */
        b->nv = nverts;
    }
    ix = (uint16_t *)sceGuGetMemory(((unsigned)count * 12 + 15) & ~15u);
    for (int k = 0; k + 2 < count; k += 3) {
        const uint16_t *t3 = idx + k;
        uint8_t c0 = cl_code[t3[0]], c1 = cl_code[t3[1]], c2 = cl_code[t3[2]];
        CVtx a[9], c[9], *in = a, *out = c, *tp;
        int m = 3, base;
        if (c0 & c1 & c2) continue;                       /* all outside one plane */
        if (!(c0 | c1 | c2)) {
            ix[n] = t3[0]; ix[n + 1] = t3[1]; ix[n + 2] = t3[2];
            n += 3;
            continue;
        }
        for (int j = 0; j < 3; j++) {
            a[j].v = g[t3[j]];
            a[j].x = cl_xyw[t3[j] * 3]; a[j].y = cl_xyw[t3[j] * 3 + 1]; a[j].w = cl_xyw[t3[j] * 3 + 2];
        }
        for (int pl = 0; pl < 5 && m >= 3; pl++) {
            int o = 0;
            if (!((c0 | c1 | c2) >> pl & 1)) continue;
            for (int j = 0; j < m; j++) {
                const CVtx *p0 = &in[j], *p1 = &in[(j + 1) % m];
                float d0 = cdist(p0, pl), d1 = cdist(p1, pl);
                if (d0 >= 0.f) out[o++] = *p0;
                if ((d0 >= 0.f) != (d1 >= 0.f)) out[o++] = lerp_c(p0, p1, d0 / (d0 - d1));
            }
            m = o; tp = in; in = out; out = tp;
        }
        if (m < 3) continue;
        base = b->nv;
        for (int j = 0; j < m; j++) b->v[b->nv++] = in[j].v;
        for (int q = 1; q + 1 < m; q++) {
            ix[n] = (uint16_t)base; ix[n + 1] = (uint16_t)(base + q); ix[n + 2] = (uint16_t)(base + q + 1);
            n += 3;
        }
    }
    *oi = ix;
    return n;
}

/* P83: when every corner of the mesh's bounding box is inside the ADC box
 * with w > 0 (with a margin above float rounding), every vertex is too: the
 * box is a convex cone and the transform is linear, so the per-vertex test
 * would mark nothing and can be skipped. */
COLD static int bbox_corners(MMesh *me, const float cm[16], float k[8][4], float *smaxp)
{
    float smax = 0.f;
    if (!me->bbok && use_vfpu > 0 && me->nverts > 0) {   /* P93 */
        vfpu_bbox(me->cpu, me->nverts, me->bb);
        if (!(me->bb[0] == me->bb[0]) || !(me->bb[1] == me->bb[1]) || !(me->bb[2] == me->bb[2]) ||
            !(me->bb[3] == me->bb[3]) || !(me->bb[4] == me->bb[4]) || !(me->bb[5] == me->bb[5])) return 0;
        me->bbok = 1;
    }
    if (!me->bbok) {
        for (int j = 0; j < 3; j++) { me->bb[j] = 3e38f; me->bb[3 + j] = -3e38f; }
        for (int i = 0; i < me->nverts; i++)
            for (int j = 0; j < 3; j++) {
                float q = me->cpu[i].pos[j];
                if (!(q == q)) return 0;
                if (q < me->bb[j]) me->bb[j] = q;
                if (q > me->bb[3 + j]) me->bb[3 + j] = q;
            }
        me->bbok = 1;
    }
    if (use_vfpu > 0) {   /* P93 */
        for (int c = 0; c < 8; c++) {
            k[c][0] = me->bb[(c & 1) ? 3 : 0]; k[c][1] = me->bb[(c & 2) ? 4 : 1]; k[c][2] = me->bb[(c & 4) ? 5 : 2];
        }
        vfpu_bbox_corners(cm, &k[0][0], &smax);
        *smaxp = smax;
        return smax < 1e30f;
    }
    for (int c = 0; c < 8; c++) {
        float q[3] = { me->bb[(c & 1) ? 3 : 0], me->bb[(c & 2) ? 4 : 1], me->bb[(c & 4) ? 5 : 2] };
        for (int j = 0; j < 4; j++) {
            float sa = fabsf(q[0] * cm[j]) + fabsf(q[1] * cm[4 + j]) + fabsf(q[2] * cm[8 + j]) + fabsf(cm[12 + j]);
            k[c][j] = q[0] * cm[j] + q[1] * cm[4 + j] + q[2] * cm[8 + j] + cm[12 + j];
            if (sa > smax) smax = sa;
        }
    }
    *smaxp = smax;
    return smax < 1e30f;
}

COLD static int bbox_inside_c(MMesh *me, const float cm[16])
{
    float k[8][4], smax;
    if (!bbox_corners(me, cm, k, &smax)) return 0;
    {
        float m = 1e-5f * smax;
        for (int c = 0; c < 8; c++) {
            float w = k[c][3] - m;
            if (!(w > 0.f) || !(fabsf(k[c][0]) + m < w) || !(fabsf(k[c][1]) + m < w) || !(fabsf(k[c][2]) + m < w)) return 0;
        }
    }
    return 1;
}
static int bbox_inside(MMesh *me, const float cm[16])
{
    if (use_vfpu > 0 && me->bbok) return vfpu_bbox_inside(cm, me->bb);   /* P83: in VFPU registers, about 5x faster */
    return bbox_inside_c(me, cm);
}

/* PROFILE=1 split of fl_clay_draw (psp_prof.c slots 30-35) */
int psp_prof_fine(void);
unsigned long long psp_prof_now(void);
void psp_prof_add(int slot, const char *label, unsigned long long us);
#define PT(v) unsigned long long v = RARE(prof) ? psp_prof_now() : 0
#define PA(slot, label, v) do { if (RARE(prof)) psp_prof_add(slot, label, psp_prof_now() - v); } while (0)

/* P108: mesh vertices for the GE in VRAM.  The GE fetches one vertex per
 * index from main RAM, and in fights that bus traffic slowed the CPU about
 * 3.6 ms a drawn step (PSP.md).  Unlit meshes drawn from gv get a copy in a
 * pool at the top of VRAM (r_gu_mesh_vram), refreshed when gv is rewritten;
 * the CPU keeps reading gv.  Like the P88 texture cache, a full pool is kept
 * while the frame used at least half of it and reset otherwise (a new scene)
 * or for a system dialog. */
static uint32_t mv_base, mv_size, mv_next, mv_epoch = 1;
static int mv_init, mv_full, mv_idx;
static unsigned mv_stat[3];   /* draws from VRAM, copies, KB copied */
uint32_t r_gu_mesh_vram(uint32_t *size);
int r_gu_vram_ok(void);
int psp_prof_on(void);
void r_gu_mesh_vram_frame(int drop)
{
    if (psp_prof_on()) {
        psp_prof_add(49, "  mesh vertex VRAM draws (count)", 1000ull * mv_stat[0]);
        psp_prof_add(50, "  mesh vertex VRAM copies (count)", 1000ull * mv_stat[1]);
        psp_prof_add(51, "  mesh vertex VRAM KB copied (count)", 1000ull * (mv_stat[2] >> 10));
    }
    mv_stat[0] = mv_stat[1] = mv_stat[2] = 0;
    if (CLAY && mv_full && !drop) {
        unsigned used = 0;
        for (int h = 1; h < MAX_CLAY; h++)
            if (CLAY[h].voff && CLAY[h].vused == mv_epoch)
                used += (((unsigned)CLAY[h].nverts * sizeof(GVtx) + 63u) & ~63u) +
                        (CLAY[h].ioff ? ((unsigned)CLAY[h].nidx * 2u + 63u) & ~63u : 0);
        if (used * 2 >= mv_next) mv_full = 0;
    }
    mv_epoch++;
    if (!CLAY || (!mv_full && !drop)) return;
    for (int h = 1; h < MAX_CLAY; h++) CLAY[h].voff = CLAY[h].ioff = 0;
    mv_next = 0;
    mv_full = 0;
}
/* the GE's copy of me->gv, or NULL (gv itself) */
static const void *mesh_vram(MMesh *me)
{
    unsigned vbytes = ((unsigned)me->nverts * sizeof(GVtx) + 63u) & ~63u, bytes = vbytes;
    if (!mv_init) { mv_base = r_gu_mesh_vram(&mv_size); mv_init = 1; mv_idx = getenv("MVC2_MESH_VRAM_NOIDX") == NULL; }
    if (!mv_size || !r_gu_vram_ok()) return NULL;
    if (mv_idx) bytes += ((unsigned)me->nidx * 2u + 63u) & ~63u;
    if (!me->voff) {
        if (mv_next + bytes > mv_size) { mv_full = 1; return NULL; }
        me->voff = mv_base + mv_next;
        mv_next += bytes;
        me->vstale = 1;
        if (mv_idx) {   /* P83: the GE reads indices from VRAM too (MVC2_MESH_VRAM_NOIDX=1: off); they never change after build_mesh: copied once */
            memcpy((void *)(uintptr_t)(0x44000000u + me->voff + vbytes), me->idx, (size_t)me->nidx * 2);
            me->ioff = me->voff + vbytes;
        }
    }
    if (me->vstale) {   /* not in this frame's list yet (clay_draw's stale check), so safe to rewrite */
        memcpy((void *)(uintptr_t)(0x44000000u + me->voff), me->gv, (size_t)me->nverts * sizeof(GVtx));   /* uncached VRAM */
        me->vstale = 0;
        mv_stat[1]++; mv_stat[2] += bytes;
    }
    me->vused = mv_epoch;
    mv_stat[0]++;
    return (const void *)(uintptr_t)(0x04000000u + me->voff);
}

/* this draw's vertices in list memory: lit, or unlit with gv already in
 * this frame's list (clay_draw) */
COLD static GVtx *colours_list(MMesh *me, int lit, int matcol, const float modv[4], float cscale)
{
    GVtx *g;
    if (lit && me->dirty) { positions(me, me->gv); me->dirty = 0; me->cvalid = 0; me->gvwb = me->vstale = 1; }
    g = (GVtx *)sceGuGetMemory((unsigned)me->nverts * sizeof *g);   /* per draw */
    memcpy(g, me->gv, (size_t)me->nverts * sizeof *g);
    if (lit) { colours_lit(me, g, modv, cscale); return g; }
    if (me->dirty) positions(me, g);
    colours_unlit(me, g, matcol, modv, cscale);
    wb(g, (size_t)me->nverts * sizeof *g);
    return g;
}

COLD static int grow_int(int **p, int *cap, int n)
{
    free(*p);
    *cap = n;
    *p = (int *)malloc((size_t)n * sizeof **p);
    if (!*p) { *cap = 0; return 0; }
    return 1;
}

/* viewport * canvas -> GE (clay_draw, on a viewport change) */
COLD static void vp_build(const int vi[4], const float vf[2], float t[16])
{
    float vpm[16], ge[16], hw, hh;
    hw = (float)(vi[2] / 2);
    hh = (float)((vi[3] / 2) / 2) * 2.f;
    memset(vpm, 0, sizeof vpm);
    vpm[0] = hw / 1.3333330154418945f / (FL_SCREEN_W / 2.f);
    vpm[12] = ((float)vi[0] + hw) / (FL_SCREEN_W / 2.f) - 1.f;
    vpm[5] = hh / (FL_SCREEN_H / 2.f);
    vpm[13] = 1.f - ((float)vi[1] + hh) / (FL_SCREEN_H / 2.f);
    vpm[10] = -(vf[1] - vf[0]);
    vpm[15] = 1.f;
    vpm[3] = vpm[7] = vpm[11] = 0.f;
    /* x' = x * ax + w * bx: the w row carries bx (row 3 is w's contribution) */
    r_gu_canvas_to_ge(ge);
    fl_mat_mul(t, vpm, ge);
}

/* the scalar matrix chain (no VFPU) */
COLD static void mats_c(const float *t, const float *cs, float clip[16], float *cm)
{
    float wv[16], mvp[16];
    fl_mat_mul(wv, r_matrix(0x19), r_matrix(0x16));
    fl_mat_mul(mvp, wv, r_matrix(0x17));
    fl_mat_mul(clip, mvp, t);
    if (cm) fl_mat_mul(cm, wv, cs);
}

/* the scalar ADC outcodes (no VFPU) */
COLD static int adc_c(const MMesh *me, const float cm[16], uint8_t *out)
{
    int any = 0;
    for (int i = 0; i < me->nverts; i++) {
        const float *q = me->cpu[i].pos;
        float k[4];
        for (int j = 0; j < 4; j++) k[j] = q[0] * cm[j] + q[1] * cm[4 + j] + q[2] * cm[8 + j] + cm[12 + j];
        k[3] = fabsf(k[3]);
        out[i] = fabsf(k[0]) > k[3] || fabsf(k[1]) > k[3] || fabsf(k[2]) > k[3];
        any |= out[i];
    }
    return any;
}

/* MESH_DUMP: one range's clip-space extent */
COLD static void mesh_dump(const MMesh *me, int h, int r, const MRange *rg, int n, uint32_t fmt, const GVtx *g,
                           const uint16_t *idx, const float clip[16], const int vi[4])
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, z0 = 1e9f, z1 = -1e9f, w0 = 1e9f, w1 = -1e9f;
    for (int k = 0; k < me->nverts; k++) {
        const float *q = me->cpu[k].pos;
        float c[4];
        for (int j = 0; j < 4; j++) c[j] = q[0] * clip[j] + q[1] * clip[4 + j] + q[2] * clip[8 + j] + clip[12 + j];
        if (c[3] == 0.f) continue;
        { float X = c[0] / c[3], Y = c[1] / c[3], Z = c[2] / c[3];
          if (X < x0) x0 = X; if (X > x1) x1 = X; if (Y < y0) y0 = Y; if (Y > y1) y1 = Y; if (Z < z0) z0 = Z; if (Z > z1) z1 = Z; if (c[3] < w0) w0 = c[3]; if (c[3] > w1) w1 = c[3]; }
    }
    fprintf(stderr, "[mesh] %d r%d fmt %08X desc %d tex %08X n %d/%d col %08X\n", h, r, (unsigned)fmt, me->desc,
            (unsigned)rg->tex, n, rg->count, (unsigned)g[idx[rg->first]].c);
    fprintf(stderr, "[mesh]   ndc x %.2f..%.2f y %.2f..%.2f\n", x0, x1, y0, y1);
    fprintf(stderr, "[mesh]   z %.3f..%.3f ad0 %08X\n", z0, z1, (unsigned)r_state_ad0());
    fprintf(stderr, "[mesh]   clip %.3f %.3f %.3f %.3f\n", clip[0], clip[5], clip[10], clip[11]);
    fprintf(stderr, "[mesh]   vi %d %d %d %d w %.2f..%.2f\n", vi[0], vi[1], vi[2], vi[3], w0, w1);
}

/* The ADC reject for a matrix the memo does not hold: the kept triangles'
 * indices go in the mesh's own buffer (memo) unless an earlier draw this
 * frame handed that buffer to the GE, then in list memory (no memo). */
extern volatile long mvc2_host_frame;
static __attribute__((noinline)) const uint16_t *adc_build(MMesh *me, const float cm[16], int *adc_count, int prof)
{
    static uint8_t *out;
    static int out_cap;
    int any = 0, keep = me->aused != mvc2_host_frame + 1;
    uint16_t *f;
    if (bbox_inside(me, cm)) {
        if (RARE(prof)) psp_prof_add(36, "        clay ADC bbox skips per frame (count)", 1000);
    } else {
        if (RARE(out_cap < me->nverts)) {
            free(out);
            out_cap = me->nverts;
            out = (uint8_t *)malloc((size_t)out_cap);
        }
        if (out && use_vfpu > 0) any = vfpu_adc_codes(cm, me->cpu, me->nverts, out);   /* P93 */
        else if (out) any = adc_c(me, cm, out);
    }
    if (!any) {
        for (int r = 0; r < me->nrange; r++) adc_count[r] = me->range[r].count;
        memcpy(me->acm, cm, 64);
        me->avalid = 1;
        me->ahave = 0;
        return me->idx;
    }
    if (keep && RARE(!me->aidx)) {
        size_t ib = ((size_t)me->nidx * 2 + 63) & ~(size_t)63;
        me->aidx = (uint16_t *)memalign(64, ib + (size_t)me->nrange * sizeof(int));
        if (me->aidx) me->acount = (int *)((uint8_t *)me->aidx + ib);
        else keep = 0;
    }
    f = keep ? me->aidx : (uint16_t *)sceGuGetMemory(((unsigned)me->nidx * 2 + 15) & ~15u);
    for (int r = 0; r < me->nrange; r++) {
        MRange *rg = &me->range[r];
        int n = 0;
        for (int k = 0; k < rg->count; k += 3) {
            const uint16_t *t = me->idx + rg->first + k;
            if (out[t[0]] || out[t[1]] || out[t[2]]) continue;
            f[rg->first + n] = t[0]; f[rg->first + n + 1] = t[1]; f[rg->first + n + 2] = t[2];
            n += 3;
        }
        adc_count[r] = n;
    }
    if (keep) {
        wb(f, (size_t)me->nidx * 2);
        memcpy(me->acount, adc_count, (size_t)me->nrange * sizeof *adc_count);
        memcpy(me->acm, cm, 64);
        me->avalid = 1;
        me->ahave = 1;
        me->aused = mvc2_host_frame + 1;
    } else {
        me->avalid = 0;
    }
    return f;
}

static int clay_draw(int h)
{
    static int *adc_count, adc_cap;
    const int prof = psp_prof_fine();
    int dump;
    MMesh *me;
    uint32_t fmt, gm;
    int lit, matcol, mod, vi[4], clipping;
    float cscale, modv[4] = { 1.f, 1.f, 1.f, 1.f }, clip[16], vf[2];
    float su = 1.f, sv = 1.f, ou = 0.f, ov = 0.f;
    GVtx *g;
    const void *gge = NULL;   /* P108: where the GE reads g, NULL = g */
    if (!CLAY || h <= 0 || h >= MAX_CLAY) return 0;
    me = &CLAY[h];
    if (RARE(!unorm_ok)) unorm_init();
    if (me->desc < 0 || !me->nidx) return 0;
    {
        static long at = -2;
        extern volatile long mvc2_host_frame;
        if (RARE(at == -2)) { const char *e = getenv("MESH_DUMP"); at = e ? atol(e) : -1; }
        dump = at == mvc2_host_frame;
    }
    if (RARE(adc_cap < me->nrange) && !grow_int(&adc_count, &adc_cap, me->nrange)) return 0;
    fmt = me->fmt;
    cscale = (me->desc == 2 || me->desc == 4 || me->desc == 8 || me->desc == 10 || me->desc == 12 || me->desc == 14 ||
              me->desc == 16 || me->desc == 18) ? 255.f : 128.f;
    lit = (fmt & 0x100) != 0; matcol = (fmt & 2) != 0; mod = (fmt & 0x20000) != 0;
    gm = r_global_mod();
    if (mod) { modv[0] = gsf(gm, 16) / 255.f; modv[1] = gsf(gm, 8) / 255.f; modv[2] = gsf(gm, 0) / 255.f; modv[3] = gsf(gm, 24) / 255.f; }

    if (RARE(prof)) {
        psp_prof_add(33, "        clay draws per frame (count)", 1000);
        psp_prof_add(34, "        clay verts per frame (count)", 1000ull * (unsigned)me->nverts);
        psp_prof_add(35, "        clay lit draws per frame (count)", lit ? 1000 : 0);
    }
    PT(t_col);
    r_gu_begin((unsigned)me->nverts * sizeof(GVtx) + (unsigned)me->nidx * 2 + 64 +
               ((me->desc == 19 || (me->desc >= 44 && me->desc <= 46)) ? (unsigned)me->nverts * sizeof(GVtx) + (unsigned)me->nidx * (6 * sizeof(GVtx) + 12) + 64 : 0));
    if (lit) {
        g = colours_list(me, 1, matcol, modv, cscale);
    } else {
        extern volatile long mvc2_host_frame;
        uint32_t key[3] = { mod ? gm : 0xFFFFFFFFu, fmt, (uint32_t)matcol };
        int stale = me->dirty || !me->cvalid || words_differ(key, me->ckey, 3);
        if (RARE(stale && me->used == mvc2_host_frame + 1)) {
            /* gv is already in this frame's display list: rewriting it would
             * change that earlier draw too (grey water, flickering edges), so
             * build this draw's copy in list memory */
            g = colours_list(me, 0, matcol, modv, cscale);
        } else {
            if (RARE(me->dirty)) { positions(me, me->gv); me->dirty = 0; me->cvalid = 0; me->gvwb = me->vstale = 1; }
            if (!me->cvalid || words_differ(key, me->ckey, 3)) {
                colours_unlit(me, me->gv, matcol, modv, cscale);
                memcpy(me->ckey, key, sizeof key);
                me->cvalid = 1;
                me->gvwb = me->vstale = 1;
            }
            g = me->gv;
            if (!(gge = mesh_vram(me)) && me->gvwb) { wb(g, (size_t)me->nverts * sizeof *g); me->gvwb = 0; }   /* P88: only when written */
            me->used = mvc2_host_frame + 1;
        }
    }

    PA(30, "        clay colours + vertex copy", t_col);
    PT(t_adc);
    /* world * view * projection, then the viewport (func_003DD5D0, canvas NDC), then the screen */
    static int adc_off = -1;
    if (RARE(adc_off < 0)) adc_off = getenv("MVC2_NO_ADC") != NULL;
    const int adc = !adc_off && (have_clipm || clip_src) && !(me->desc == 19 || (me->desc >= 44 && me->desc <= 48));
    float cm[16];
    {   /* P88: viewport * canvas -> GE depends only on these; recomputed on change
         * (P110: looked at only when r_gu.c says one may have changed) */
        static float key[10], t[16];
        static int ok;
        static unsigned gen;
        extern unsigned r_gu_vp_gen;
        if (RARE(gen != r_gu_vp_gen)) {
            float k[10];
            gen = r_gu_vp_gen;
            r_viewport(vi, vf);
            memcpy(k, vi, 16); memcpy(k + 4, vf, 8); r_gu_canvas_key(k + 6);
            if (!ok || memcmp(k, key, sizeof k)) {
                vp_build(vi, vf, t);
                memcpy(key, k, sizeof k);
                ok = 1;
            }
        }
        if (use_vfpu > 0) {   /* P110: the chain below in one VFPU call, intermediates in registers */
            const float *mp[7] = { r_matrix(0x19), r_matrix(0x16), r_matrix(0x17), t, clip_src ? clip_src : clipm, clip, adc ? cm : NULL };
            vfpu_mesh_mats(mp);
        } else {
            mats_c(t, clip_src ? clip_src : clipm, clip, adc ? cm : NULL);
        }
    }

    /* UV matrix slot 0x1A (format 0x4000): scale and offset */
    if (fmt & 0x4000) {
        const float *u = r_matrix(0x1A);
        su = u[0]; sv = u[5]; ou = u[12]; ov = u[13];
    }

    /* Trivial reject (every VU1 program but the clipping ones, 19 and 44-46,
     * and the special 47/48): a triangle with a vertex outside the clip box
     * of world * view * D_0061BDE0 (the projection widened to the guard
     * band) is not drawn by the GS; the GE would clip and draw it. */
    const uint16_t *idx = me->idx;
    {
        if (adc) {
            if (me->avalid && !words_differ(cm, me->acm, 16)) {   /* P110: same matrix and vertices as the last ADC */
                if (RARE(prof)) psp_prof_add(62, "        clay ADC memo hits (count)", 1000);
                if (me->ahave) {
                    memcpy(adc_count, me->acount, (size_t)me->nrange * sizeof *adc_count);
                    idx = me->aidx;
                    me->aused = mvc2_host_frame + 1;
                } else {
                    for (int r = 0; r < me->nrange; r++) adc_count[r] = me->range[r].count;
                }
            } else {
                if (RARE(prof)) psp_prof_add(63, "        clay ADC memo misses (count)", 1000);
                idx = adc_build(me, cm, adc_count, prof);
            }
        } else {
            for (int r = 0; r < me->nrange; r++) adc_count[r] = me->range[r].count;
        }
    }

    PA(31, "        clay matrices + ADC reject", t_adc);
    PT(t_sub);
    { PT(t_cp);
    clipping = (me->desc == 19 || (me->desc >= 44 && me->desc <= 46)) && !clip_bbox_inside(me, clip) &&
               (use_vfpu <= 0 || vfpu_clip_any(clip, g, me->nverts)) &&   /* P110: registers-only pre-pass */
               clip_prepare(clip, g, me->nverts);
    PA(26, "        clay clip_prepare", t_cp);
    if (RARE(prof) && (me->desc == 19 || (me->desc >= 44 && me->desc <= 46))) {
        psp_prof_add(27, "        clay clipping-program draws (count)", 1000);
        psp_prof_add(28, "        clay clipping-program verts (count)", 1000ull * (unsigned)me->nverts);
        if (clipping) psp_prof_add(29, "        clay draws needing near_clip (count)", 1000);
    } }
    NcBuf nb = { NULL, 0, 0 };
    if (RARE(clipping))   /* room for the clipped polygons: at most 8 vertices each */
        for (int r = 0; r < me->nrange; r++)
            for (int k = 0; k + 2 < adc_count[r]; k += 3) {
                const uint16_t *t3 = idx + me->range[r].first + k;
                uint8_t c0 = cl_code[t3[0]], c1 = cl_code[t3[1]], c2 = cl_code[t3[2]];
                if ((c0 | c1 | c2) && !(c0 & c1 & c2)) nb.room += 8;
            }
    static int merge = -1;   /* MVC2_MESH_NOMERGE=1: one submit per range (A/B) */
    if (RARE(merge < 0)) merge = getenv("MVC2_MESH_NOMERGE") == NULL;
    for (int r = 0; r < me->nrange; r++) {
        const MRange *rg = &me->range[r];
        int n = adc_count[r];
        if (!n) continue;
        if (RARE(clipping)) {
            uint16_t *ci;
            PT(t_nc);
            int n = near_clip(&nb, g, me->nverts, idx + rg->first, adc_count[r], &ci);
            PA(25, "        clay near_clip", t_nc);
            if (n >= 0) {
                if (n) r_gu_mesh_draw(clip, me->textured ? rg->tex : 0, nb.v, NULL, ci, NULL, n, su, sv, ou, ov);
                continue;
            }
        }
        /* P107: the ranges after it with the same texture whose indices follow
         * on go in the same submit (their colours are in the vertices) */
        while (merge && r + 1 < me->nrange && adc_count[r + 1] && rg->first + n == me->range[r + 1].first &&
               (!me->textured || me->range[r + 1].tex == rg->tex))
            n += adc_count[++r];
        r_gu_mesh_draw(clip, me->textured ? rg->tex : 0, g, gge, idx + rg->first,
                       gge && idx == me->idx && me->ioff ? (const uint16_t *)(uintptr_t)(0x04000000u + me->ioff) + rg->first : NULL,
                       n, su, sv, ou, ov);
        if (RARE(dump)) r_viewport(vi, vf), mesh_dump(me, h, r, rg, adc_count[r], fmt, g, idx, clip, vi);
        r_gu_mesh_cnt[0]++;
        r_gu_mesh_cnt[1] += adc_count[r] / 3;
    }
    PA(32, "        clay GU submit (r_gu_mesh_draw)", t_sub);
    return 1;
}
