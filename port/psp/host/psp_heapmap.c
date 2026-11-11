/* PSP heap map (diagnostic, -DMVC2_PSP_HEAPMAP=ON): wraps malloc/calloc/
 * realloc/memalign/free to track live blocks with their caller, and
 * psp_heapmap_dump() lists the large free gaps between them with the blocks
 * on each side, plus live bytes by caller.  Calls inside newlib itself are
 * not seen. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__real_memalign(size_t, size_t);
void __real_free(void *);

#define N 32768   /* power of two */
typedef struct { uintptr_t p; uint32_t n; uintptr_t ra; } Ent;
static Ent tab[N];
static int idx[N], lost;

static uint32_t h(uintptr_t p) { return (uint32_t)((p >> 4) * 2654435761u) & (N - 1); }

static void put(void *q, size_t n, void *ra)
{
    uintptr_t p = (uintptr_t)q;
    if (!p) return;
    for (uint32_t i = h(p), k = 0; k < N; k++, i = (i + 1) & (N - 1))
        if (tab[i].p == 0 || tab[i].p == 1) { tab[i].p = p; tab[i].n = (uint32_t)n; tab[i].ra = (uintptr_t)ra; return; }
    lost++;
}

static void del(void *q)
{
    uintptr_t p = (uintptr_t)q;
    if (!p) return;
    for (uint32_t i = h(p), k = 0; k < N && tab[i].p; k++, i = (i + 1) & (N - 1))
        if (tab[i].p == p) { tab[i].p = 1; return; }   /* 1 = tombstone */
}

#define RA __builtin_return_address(0)
void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); put(p, n, RA); return p; }
void *__wrap_calloc(size_t a, size_t b) { void *p = __real_calloc(a, b); put(p, a * b, RA); return p; }
void *__wrap_memalign(size_t a, size_t n) { void *p = __real_memalign(a, n); put(p, n, RA); return p; }
void __wrap_free(void *p) { del(p); __real_free(p); }
void *__wrap_realloc(void *o, size_t n)
{
    void *p = __real_realloc(o, n);
    if (p || !n) { del(o); put(p, n, RA); }
    return p;
}

static int by_addr(const void *a, const void *b)
{
    uintptr_t x = tab[*(const int *)a].p, y = tab[*(const int *)b].p;
    return x < y ? -1 : x > y;
}

static int by_ra(const void *a, const void *b)
{
    uintptr_t x = tab[*(const int *)a].ra, y = tab[*(const int *)b].ra;
    return x < y ? -1 : x > y;
}

void psp_heapmap_dump(void)
{
    int m = 0;
    for (int i = 0; i < N; i++) if (tab[i].p > 1) idx[m++] = i;
    qsort(idx, (size_t)m, sizeof *idx, by_addr);
    fprintf(stderr, "[heapmap] %d live blocks, %d untracked, span %08x-%08x\n", m, lost,
            m ? (unsigned)tab[idx[0]].p : 0, m ? (unsigned)(tab[idx[m - 1]].p + tab[idx[m - 1]].n) : 0);
    for (int k = 0; k + 1 < m; k++) {
        Ent *a = &tab[idx[k]], *b = &tab[idx[k + 1]];
        uintptr_t gap = b->p - (a->p + a->n);
        if (gap < (192u << 10)) continue;
        fprintf(stderr, "[heapmap] gap %5u KB at %08x | before %u B ra %08x | after %u B ra %08x\n",
                (unsigned)(gap >> 10), (unsigned)(a->p + a->n), a->n, (unsigned)a->ra, b->n, (unsigned)b->ra);
    }
    /* live bytes by caller, top 12 */
    qsort(idx, (size_t)m, sizeof *idx, by_ra);
    {
        uintptr_t tra[12] = {0}; uint32_t tb[12] = {0}, tc[12] = {0};
        for (int k = 0; k < m;) {
            uintptr_t ra = tab[idx[k]].ra; uint32_t s = 0, c = 0;
            for (; k < m && tab[idx[k]].ra == ra; k++) { s += tab[idx[k]].n; c++; }
            for (int r = 0; r < 12; r++) if (s > tb[r]) {
                memmove(&tra[r + 1], &tra[r], (11 - r) * sizeof *tra);
                memmove(&tb[r + 1], &tb[r], (11 - r) * sizeof *tb);
                memmove(&tc[r + 1], &tc[r], (11 - r) * sizeof *tc);
                tra[r] = ra; tb[r] = s; tc[r] = c; break;
            }
        }
        for (int r = 0; r < 12 && tb[r]; r++)
            fprintf(stderr, "[heapmap] caller %08x: %u KB in %u\n", (unsigned)tra[r], tb[r] >> 10, tc[r]);
    }
}
