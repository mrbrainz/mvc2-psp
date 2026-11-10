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
int r_gu_mesh_draw(const float clip[16], uint32_t tex, const void *verts, const uint16_t *idx, int count,
                   float su, float sv, float ou, float ov);

int r_gu_mesh_cnt[3];   /* DRAWLOG: mesh draws, triangles, microseconds in fl_clay_draw */
int fl_mesh_debug;      /* fe_mesh.c sets it (MVC2_MESH_DUMP); r_mesh.c logs with it, unused here */

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

void fl_mat_mul(float out[16], const float a[16], const float b[16])
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
    if (m) memcpy(clipm, m, sizeof clipm);
    have_clipm = m != NULL;
}

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

static void wb(const void *p, size_t n) { if (p && n) sceKernelDcacheWritebackRange(p, (unsigned)n); }

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
    me->dirty = 1;
    me->cvalid = 0;
}

/* ---------------- clay API ---------------- */
static void clay_free(int h)
{
    MMesh *me = &CLAY[h];
    if (me->gv || me->idx) r_gu_sync();   /* queued draws may still read them */
    free(me->cpu);
    free(me->gv);
    free(me->idx);
    free(me->range);
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
    if (CLAY && h > 0 && h < MAX_CLAY) CLAY[h].dirty = 1;
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

static void positions(MMesh *me, GVtx *g)
{
    for (int i = 0; i < me->nverts; i++) {
        const MVtx *v = &me->cpu[i];
        g[i].x = v->pos[0]; g[i].y = v->pos[1]; g[i].z = v->pos[2];
        g[i].u = v->uv[0]; g[i].v = v->uv[1];
    }
}

/* unlit colours: vertex colour programs and material colour programs */
static void colours_unlit(MMesh *me, GVtx *g, int matcol, const float modv[4], float cscale)
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

/* lit programs (format 0x100), per draw: the world matrix moves the lights */
static void colours_lit(MMesh *me, GVtx *g, const float modv[4], float cscale)
{
    float ldir[3][3], lamb_base[3];
    const float *w = r_matrix(0x19);
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
    for (int r = 0; r < me->nrange; r++) {
        const MRange *rg = &me->range[r];
        float lc[3][3], la[4];
        for (int k = 0; k < 3; k++) {
            const uint8_t *l = r_light(k);
            for (int c = 0; c < 3; c++) {
                float lcol;
                memcpy(&lcol, l + 0x04 + 4 * c, 4);
                lc[k][c] = fminf(cscale, cscale * rg->diffuse[c] * lcol);
            }
        }
        for (int c = 0; c < 3; c++) la[c] = fminf(cscale, lamb_base[c] * rg->c0[c]);
        la[3] = floorf(128.f * rg->diffuse[3]);
        for (int k = 0; k < rg->count; k++) {
            int i = me->idx[rg->first + k];
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
int fl_clay_draw(int h)
{
    unsigned t0 = sceKernelGetSystemTimeLow();
    int r = clay_draw(h);
    r_gu_mesh_cnt[2] += (int)(sceKernelGetSystemTimeLow() - t0);
    return r;
}

static int clay_draw(int h)
{
    static int *adc_count, adc_cap;
    int dump;
    MMesh *me;
    uint32_t fmt, gm;
    int lit, matcol, mod, vi[4];
    float cscale, modv[4] = { 1.f, 1.f, 1.f, 1.f }, wv[16], mvp[16], vpm[16], ge[16], clip[16], vf[2];
    float hw, hh, su = 1.f, sv = 1.f, ou = 0.f, ov = 0.f;
    GVtx *g;
    if (!CLAY || h <= 0 || h >= MAX_CLAY) return 0;
    me = &CLAY[h];
    if (me->desc < 0 || !me->nidx) return 0;
    {
        static long at = -2;
        extern volatile long mvc2_host_frame;
        if (at == -2) { const char *e = getenv("MESH_DUMP"); at = e ? atol(e) : -1; }
        dump = at == mvc2_host_frame;
    }
    if (adc_cap < me->nrange) {
        free(adc_count);
        adc_cap = me->nrange;
        adc_count = (int *)malloc((size_t)adc_cap * sizeof *adc_count);
        if (!adc_count) { adc_cap = 0; return 0; }
    }
    fmt = me->fmt;
    cscale = (me->desc == 2 || me->desc == 4 || me->desc == 8 || me->desc == 10 || me->desc == 12 || me->desc == 14 ||
              me->desc == 16 || me->desc == 18) ? 255.f : 128.f;
    lit = (fmt & 0x100) != 0; matcol = (fmt & 2) != 0; mod = (fmt & 0x20000) != 0;
    gm = r_global_mod();
    if (mod) { modv[0] = gsf(gm, 16) / 255.f; modv[1] = gsf(gm, 8) / 255.f; modv[2] = gsf(gm, 0) / 255.f; modv[3] = gsf(gm, 24) / 255.f; }

    r_gu_begin((lit ? (unsigned)me->nverts * sizeof(GVtx) : 0) + (unsigned)me->nidx * 2 + 64);
    /* vertices: positions on a write, colours when their inputs change */
    if (me->dirty) {
        positions(me, me->gv);
        me->dirty = 0;
        me->cvalid = 0;
    }
    if (lit) {
        g = (GVtx *)sceGuGetMemory((unsigned)me->nverts * sizeof *g);   /* per draw */
        memcpy(g, me->gv, (size_t)me->nverts * sizeof *g);
        colours_lit(me, g, modv, cscale);
    } else {
        uint32_t key[3] = { mod ? gm : 0xFFFFFFFFu, fmt, (uint32_t)matcol };
        if (!me->cvalid || memcmp(key, me->ckey, sizeof key)) {
            colours_unlit(me, me->gv, matcol, modv, cscale);
            memcpy(me->ckey, key, sizeof key);
            me->cvalid = 1;
        }
        g = me->gv;
        wb(g, (size_t)me->nverts * sizeof *g);
    }

    /* world * view * projection, then the viewport (func_003DD5D0, canvas NDC), then the screen */
    fl_mat_mul(wv, r_matrix(0x19), r_matrix(0x16));
    fl_mat_mul(mvp, wv, r_matrix(0x17));
    r_viewport(vi, vf);
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
    {
        float t[16];
        r_gu_canvas_to_ge(ge);
        fl_mat_mul(t, vpm, ge);
        fl_mat_mul(clip, mvp, t);
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
        static int off = -1;
        static uint8_t *out;
        static int out_cap;
        if (off < 0) off = getenv("MVC2_NO_ADC") != NULL;
        if (!off && have_clipm && !(me->desc == 19 || (me->desc >= 44 && me->desc <= 48))) {
            float cm[16];
            int any = 0;
            fl_mat_mul(cm, wv, clipm);
            if (out_cap < me->nverts) {
                free(out);
                out_cap = me->nverts;
                out = (uint8_t *)malloc((size_t)out_cap);
            }
            for (int i = 0; out && i < me->nverts; i++) {
                const float *q = me->cpu[i].pos;
                float k[4];
                for (int j = 0; j < 4; j++) k[j] = q[0] * cm[j] + q[1] * cm[4 + j] + q[2] * cm[8 + j] + cm[12 + j];
                k[3] = fabsf(k[3]);
                out[i] = fabsf(k[0]) > k[3] || fabsf(k[1]) > k[3] || fabsf(k[2]) > k[3];
                any |= out[i];
            }
            if (any) {
                uint16_t *f = (uint16_t *)sceGuGetMemory(((unsigned)me->nidx * 2 + 15) & ~15u);
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
                idx = f;
            }
            if (!any) for (int r = 0; r < me->nrange; r++) adc_count[r] = me->range[r].count;
        } else {
            for (int r = 0; r < me->nrange; r++) adc_count[r] = me->range[r].count;
        }
    }

    for (int r = 0; r < me->nrange; r++) {
        const MRange *rg = &me->range[r];
        if (!adc_count[r]) continue;
        r_gu_mesh_draw(clip, me->textured ? rg->tex : 0, g, idx + rg->first, adc_count[r], su, sv, ou, ov);
        if (dump) {
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
                    (unsigned)rg->tex, adc_count[r], rg->count, (unsigned)g[idx[rg->first]].c);
            fprintf(stderr, "[mesh]   ndc x %.2f..%.2f y %.2f..%.2f\n", x0, x1, y0, y1);
            fprintf(stderr, "[mesh]   z %.3f..%.3f ad0 %08X\n", z0, z1, (unsigned)r_state_ad0());
            fprintf(stderr, "[mesh]   clip %.3f %.3f %.3f %.3f\n", clip[0], clip[5], clip[10], clip[11]);
            fprintf(stderr, "[mesh]   vi %d %d %d %d w %.2f..%.2f\n", vi[0], vi[1], vi[2], vi[3], w0, w1);
        }
        r_gu_mesh_cnt[0]++;
        r_gu_mesh_cnt[1] += adc_count[r] / 3;
    }
    return 1;
}
