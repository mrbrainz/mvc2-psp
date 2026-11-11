/* PSP character sprite path (fl_char_* of render.h) on sceGu, for the PSP
 * build only.  A copy of r_char.c's GL-free logic (bank tables, tile decode,
 * part placement), kept in step with it by hand; r_char.c needs GL headers.
 *
 * Tiles are decoded on first use into 512x512 T4 atlas pages in RAM
 * (128 KB each, at most PAGES_MAX per bank; a full bank resets its atlas),
 * and each bank keeps its 16 palettes as 16-colour 8888 CLUTs. */
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>
#include "render.h"
#include "decode.h"

#define PAGE 512
#define PAGES_MAX 4

void r_flush(void);
void r_gu_sync(void);
void r_gu_char_quad(const void *pix, int pw, int ph, const uint32_t *clut,
                    const float x[4], const float y[4], const float u[4], const float v[4],
                    uint32_t rgba, float z);
float r_gu_snap_x(float x);
float r_gu_snap_y(float y);
float r_gu_depth(float z);

int r_gu_char_cnt[3];
int r_gu_char_bytes;   /* atlas pages held */   /* DRAWLOG: draws, parts, tile decodes */

typedef struct CharTile {
    int16_t page;       /* -1 = not decoded yet */
    uint16_t x, y;
    uint8_t fw8, fh8, w8, h8;
} CharTile;

struct FlCharBank {
    const uint8_t *d;
    size_t n;
    uint32_t tile_tab, frame_tab, pal_off;
    int n_tiles, n_frames, n_pals;
    CharTile *tiles;
    uint8_t *pages[PAGES_MAX];   /* T4, low nibble = left pixel */
    int n_pages;
    int shelf_x, shelf_y, shelf_h;
    uint32_t *clut;              /* 16 x 16 colours, 16-byte aligned */
};

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rd16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

FlCharBank *fl_char_bank_create(const uint8_t *d, size_t n)
{
    FlCharBank *b;
    if (n < 0x20) return NULL;
    b = (FlCharBank *)calloc(1, sizeof *b);
    if (!b) return NULL;
    b->d = d;
    b->n = n;
    b->tile_tab = rd32(d + 0x04);
    b->frame_tab = rd32(d + 0x0C);
    b->pal_off = rd32(d + 0x14);
    if (b->tile_tab + 4 > n || b->frame_tab + 4 > n || b->pal_off + 32 * 16 > n) { free(b); return NULL; }
    b->n_tiles = (int)(rd32(d + b->tile_tab) / 4);
    b->n_frames = (int)(rd32(d + b->frame_tab) / 4);
    b->n_pals = (int)((n - b->pal_off) / 32);
    if (b->n_pals > 48) b->n_pals = 48;
    b->tiles = (CharTile *)calloc((size_t)b->n_tiles, sizeof(CharTile));
    b->clut = (uint32_t *)memalign(16, 16 * 16 * 4);
    if (!b->tiles || !b->clut) { fl_char_bank_destroy(b); return NULL; }
    for (int i = 0; i < b->n_tiles; i++) {
        uint32_t o = b->tile_tab + rd32(d + b->tile_tab + 4 * i);
        b->tiles[i].page = -1;
        if (o + 4 <= n) {
            b->tiles[i].fw8 = d[o]; b->tiles[i].fh8 = d[o + 1];
            b->tiles[i].w8 = d[o + 2]; b->tiles[i].h8 = d[o + 3];
        }
    }
    for (int p = 0; p < 16; p++) fl_char_load_palette(b, p, p);
    return b;
}

void fl_char_bank_destroy(FlCharBank *b)
{
    if (!b) return;
    r_gu_sync();
    for (int i = 0; i < b->n_pages; i++) { free(b->pages[i]); r_gu_char_bytes -= PAGE * PAGE / 2; }
    free(b->tiles);
    free(b->clut);
    free(b);
}

int fl_char_frame_count(const FlCharBank *b) { return b->n_frames; }
int fl_char_tile_count(const FlCharBank *b) { return b->n_tiles; }

void fl_char_set_palette_rgba(FlCharBank *b, int dst, const uint8_t *rgba)
{
    uint32_t *c = b->clut + 16 * (dst & 15);
    r_flush();   /* batched quads may still use the old colours */
    memcpy(c, rgba, 64);   /* RGBA bytes = GU 8888 */
    sceKernelDcacheWritebackRange(c, 64);
}

void fl_char_set_palette_1555(FlCharBank *b, int dst, const uint16_t *c)
{
    uint8_t rgba[16 * 4];
    for (int k = 0; k < 16; k++) argb1555_to_rgba8(c[k], rgba + 4 * k);
    fl_char_set_palette_rgba(b, dst, rgba);
}

void fl_char_load_palette(FlCharBank *b, int dst, int src)
{
    uint16_t c[16];
    const uint8_t *p;
    if (src < 0 || src >= b->n_pals) return;
    p = b->d + b->pal_off + 32 * src;
    for (int k = 0; k < 16; k++) c[k] = (uint16_t)(p[2 * k] | p[2 * k + 1] << 8);
    fl_char_set_palette_1555(b, dst, c);
}

static void atlas_reset(FlCharBank *b)
{
    r_gu_sync();   /* queued draws still read the pages */
    for (int i = 0; i < b->n_tiles; i++) b->tiles[i].page = -1;
    for (int i = 0; i < b->n_pages; i++) memset(b->pages[i], 0, PAGE * PAGE / 2);
    b->n_pages = b->n_pages ? 1 : 0;   /* keep page 0, free the rest */
    for (int i = 1; i < PAGES_MAX; i++) { if (b->pages[i]) r_gu_char_bytes -= PAGE * PAGE / 2; free(b->pages[i]); b->pages[i] = NULL; }
    b->shelf_x = b->shelf_y = b->shelf_h = 0;
}

/* Decode tile `ti` (func_00413A80 + square sub-block layout) into a page. */
static int ensure_tile(FlCharBank *b, int ti)
{
    CharTile *t = &b->tiles[ti];
    int w, h, s, nsub;
    uint32_t o;
    uint8_t raw[64 * 64 / 2 + 32];
    uint8_t *pg;
    size_t p = 0;
    if (t->page >= 0) return 0;
    w = t->w8 * 8; h = t->h8 * 8;
    if (w <= 0 || h <= 0 || w > 64 || h > 64) return -1;
    o = b->tile_tab + rd32(b->d + b->tile_tab + 4 * ti);
    r_gu_char_cnt[2]++;
    lz8_decode(b->d + o + 4, b->n - o - 4, raw, (size_t)(w * h / 2));
    /* shelf packer; x on even pixels (a byte holds two), 1-2 px gutter */
    if (b->n_pages == 0 || b->shelf_x + w + 2 > PAGE) {
        b->shelf_x = 0;
        b->shelf_y += b->shelf_h + 1;
        b->shelf_h = 0;
    }
    if (b->n_pages == 0 || b->shelf_y + h > PAGE) {
        if (b->n_pages == PAGES_MAX) {
            atlas_reset(b);   /* keeps page 0, cleared */
        } else {
            uint8_t *np = (uint8_t *)memalign(16, PAGE * PAGE / 2);
            if (!np) return -1;
            memset(np, 0, PAGE * PAGE / 2);   /* index 0 = transparent */
            b->pages[b->n_pages++] = np;
            r_gu_char_bytes += PAGE * PAGE / 2;
        }
        b->shelf_x = b->shelf_y = b->shelf_h = 0;
    }
    t->page = (int16_t)(b->n_pages - 1);
    t->x = (uint16_t)b->shelf_x;
    t->y = (uint16_t)b->shelf_y;
    b->shelf_x += w + 2;
    if (h > b->shelf_h) b->shelf_h = h;
    pg = b->pages[t->page];
    s = w < h ? w : h;
    nsub = (w > h ? w : h) / s;
    /* PSMT4 source and GE T4 both keep the left pixel in the low nibble */
    for (int k = 0; k < nsub; k++) {
        int ox = w >= h ? k * s : 0, oy = w >= h ? 0 : k * s;
        for (int y = 0; y < s; y++) {
            memcpy(pg + ((size_t)(t->y + oy + y) * PAGE + t->x + ox) / 2, raw + p, (size_t)s / 2);
            p += (size_t)s / 2;
        }
    }
    sceKernelDcacheWritebackRange(pg, PAGE * PAGE / 2);
    return 0;
}

static const uint8_t *frame_rec(const FlCharBank *b, int f, int *nparts)
{
    uint32_t o;
    int n;
    if (f < 0 || f >= b->n_frames) return NULL;
    o = b->frame_tab + rd32(b->d + b->frame_tab + 4 * f);
    if (o + 2 > b->n) return NULL;
    n = rd16s(b->d + o);
    if (n < 0 || o + 2 + 8 * (size_t)n > b->n) return NULL;
    *nparts = n;
    return b->d + o + 2;
}

/* Part placement (func_00412260 + func_00413600): positions are cumulative
 * (pos -= (dx, dy)); a flipped part anchors at pos + flipExtent*8 - size. */
typedef struct PartRect { float x0, y0, x1, y1; int ti; uint16_t flags; } PartRect;

static int part_rect(const FlCharBank *b, const uint8_t *rec, int k, float *px, float *py, PartRect *r)
{
    int16_t dx = rd16s(rec + 8 * k), dy = rd16s(rec + 8 * k + 2);
    uint16_t flags = (uint16_t)rd16s(rec + 8 * k + 4);
    int ti = rd16s(rec + 8 * k + 6);
    const CharTile *t;
    int w, h;
    *px -= dx;
    *py -= dy;
    if (ti < 0 || ti >= b->n_tiles) return -1;
    t = &b->tiles[ti];
    w = t->w8 * 8; h = t->h8 * 8;
    r->x0 = (flags & 0x8000) ? *px + t->fw8 * 8 - w : *px;
    r->y0 = (flags & 0x4000) ? *py + t->fh8 * 8 - h : *py;
    r->x1 = r->x0 + w;
    r->y1 = r->y0 + h;
    r->ti = ti;
    r->flags = flags;
    return 0;
}

int fl_char_frame_bounds(FlCharBank *b, int f, int *x0, int *y0, int *x1, int *y1)
{
    int n;
    const uint8_t *rec = frame_rec(b, f, &n);
    float px = 0, py = 0, a = 1e9f, c = 1e9f, e = -1e9f, g = -1e9f;
    if (!rec || !n) return -1;
    for (int k = 0; k < n; k++) {
        PartRect r;
        if (part_rect(b, rec, k, &px, &py, &r)) continue;
        if (r.x0 < a) a = r.x0;
        if (r.y0 < c) c = r.y0;
        if (r.x1 > e) e = r.x1;
        if (r.y1 > g) g = r.y1;
    }
    *x0 = (int)a; *y0 = (int)c; *x1 = (int)e; *y1 = (int)g;
    return 0;
}

int fl_char_draw(FlCharBank *b, int f, const float m[16], uint32_t argb, int palette)
{
    int n, axis, drawn = 0;
    const uint8_t *rec = frame_rec(b, f, &n);
    uint32_t col = 0;
    float z, px = 0, py = 0;
    r_gu_char_cnt[0]++;
    if (!rec) return 0;
    {   /* RGBA = (c + 1) >> 1 per channel (GS 0x80 = 1.0) */
        unsigned ch[4] = { (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, argb >> 24 };
        for (int i = 0; i < 4; i++) {
            unsigned g = (ch[i] + 1) >> 1, v = (g * 255 + 64) / 128;
            col |= (v > 255 ? 255u : v) << (8 * i);
        }
    }
    axis = m[1] == 0.f && m[4] == 0.f;
    z = r_gu_depth(m[14]);
    /* parts later in the list are drawn later (on top), as in the original loop */
    for (int k = 0; k < n; k++) {
        PartRect r;
        const CharTile *t;
        float cx[4], cy[4], cu[4], cv[4], X[4], Y[4], u0, v0, u1, v1, s;
        if (part_rect(b, rec, k, &px, &py, &r)) continue;
        if (ensure_tile(b, r.ti)) continue;
        t = &b->tiles[r.ti];
        u0 = t->x; v0 = t->y;
        u1 = (float)(t->x + t->w8 * 8); v1 = (float)(t->y + t->h8 * 8);
        if (r.flags & 0x8000) { s = u0; u0 = u1; u1 = s; }
        if (r.flags & 0x4000) { s = v0; v0 = v1; v1 = s; }
        cx[0] = r.x0; cx[1] = r.x1; cx[2] = r.x1; cx[3] = r.x0;
        cy[0] = r.y0; cy[1] = r.y0; cy[2] = r.y1; cy[3] = r.y1;
        cu[0] = u0; cu[1] = u1; cu[2] = u1; cu[3] = u0;
        cv[0] = v0; cv[1] = v0; cv[2] = v1; cv[3] = v1;
        for (int i = 0; i < 4; i++) {
            float x = m[0] * cx[i] + m[4] * cy[i] + m[12];
            float y = m[1] * cx[i] + m[5] * cy[i] + m[13];
            X[i] = axis ? r_gu_snap_x(x) : x;
            Y[i] = axis ? r_gu_snap_y(y) : y;
        }
        r_gu_char_quad(b->pages[t->page], PAGE, PAGE, b->clut + 16 * (((r.flags >> 4) + palette) & 15),
                       X, Y, cu, cv, col, z);
        drawn++;
        r_gu_char_cnt[1]++;
    }
    return drawn;
}
