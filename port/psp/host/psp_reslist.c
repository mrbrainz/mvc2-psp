/* P90: O(1) func_003EBA40 (src/b10s_0_f.c), the walk of the GS-resident list
 * D_004C2AB4 that tex select calls ~220 times a frame. --wrap, so upstream is
 * untouched. Nodes are in the static pool D_006267A0 (src/s2m.c) and are
 * zeroed when unlinked (func_003EB9C0) or reset, and callers only insert a
 * key after a failed lookup, so keys are unique: a cached node that still has
 * the key and is still linked is exactly what the walk would return. */
#include <stdlib.h>
typedef struct N { struct N *prev; struct N *next; int key; } N;   /* b10s_0_f.c's */
extern N *D_004C2AB4;
N *__real_func_003EBA40(int key);

/* keys are texture (D_006207A0[384]) or palette (D_0061C7A0[256]) records,
 * stride 0x40: one slot each; anything else goes to the walk */
extern char D_006207A0[], D_0061C7A0[];
static N *cache[384 + 256];

static inline int slot(int key)
{
    unsigned t = (unsigned)key - (unsigned)D_006207A0, p = (unsigned)key - (unsigned)D_0061C7A0;
    if (t < 384 * 0x40 && !(t & 0x3F)) return (int)(t >> 6);
    if (p < 256 * 0x40 && !(p & 0x3F)) return 384 + (int)(p >> 6);
    return -1;
}

static inline int linked(const N *n, int key)
{
    return n->key == key && (n == D_004C2AB4 ? !n->prev : n->prev && n->prev->next == n);
}

static int off = -1;   /* MVC2_NO_RESLIST_CACHE (env.txt): A/B testing */

N *__wrap_func_003EBA40(int key)
{
    if (off < 0) off = getenv("MVC2_NO_RESLIST_CACHE") != NULL;
    int s = off ? -1 : slot(key);
    if (s < 0) return __real_func_003EBA40(key);
    N *n = cache[s];
    if (n && linked(n, key)) return n;
    n = __real_func_003EBA40(key);
    if (n) cache[s] = n;
    return n;
}

/* func_003EA660 (src/s9b_a.c) does nothing when every texture/palette in its
 * list is already resident: psp_fe_sync.c skips the call then. */
int psp_reslist_all_resident(int n, const unsigned *list)
{
    if (off < 0) off = getenv("MVC2_NO_RESLIST_CACHE") != NULL;
    if (off) return 0;
    for (int i = 0; i < n; i++) {
        unsigned v = list[i], t = v & 0xFFFF, p = v >> 16;
        if (t && !__wrap_func_003EBA40((int)(D_006207A0 + (t - 1) * 0x40))) return 0;
        if (p && !__wrap_func_003EBA40((int)(D_0061C7A0 + (p - 1) * 0x40))) return 0;
    }
    return 1;
}

/* P95: func_003EA660 (src/s9b_a.c) for lists with a non-resident entry.
 * The residency bookkeeping (lookup, GS block allocation func_003EBE70 /
 * func_003EBF30 with its retry, insert func_003EBA80 / func_003EBC20) is the
 * game's, call for call: the game reads that list back.  The GS upload
 * packets (func_003DD160, func_003DBD80, the D_0062BBA0 DMA queue,
 * func_003DC120) are left out: the PSP renderer takes textures from the
 * records through psp_fe_sync.c / fe_render.c, never from the GS, as P84
 * left out the mesh packets.  MVC2_TEXSEL_FULL=1: the game's own body. */
typedef struct {
    char pad0[0x10];
    short h10;
} TexRec;
typedef struct LN { char pad[4]; struct LN *next; } LN;
extern int func_003EBE70(int);
extern int func_003EBA80(int, void *);
extern int func_003EBF30(void *, unsigned *, int);
extern int func_003EBC20(int, void *);
extern void func_003ED0D0(void *);
extern char D_004BA9D0[];
void func_003EA660__game(int n, void *list);

void psp_texsel(int n, unsigned *list)
{
    static int full = -1;
    if (full < 0) full = getenv("MVC2_TEXSEL_FULL") != NULL;
    if (full) { func_003EA660__game(n, list); return; }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < 2; j++) {
            unsigned v = list[i], idx = j ? v >> 16 : v & 0xFFFF;
            TexRec *e;
            int r;
            if (!idx) continue;
            e = (TexRec *)((j ? D_0061C7A0 : D_006207A0) + (idx - 1) * 0x40);
            if (__wrap_func_003EBA40((int)e)) continue;
            r = func_003EBE70(e->h10);
            if (r != -1) {
                if (!func_003EBA80(r, e)) func_003ED0D0(D_004BA9D0);
                continue;
            }
            while ((r = func_003EBF30(e, list, n)) == -1)
                ;
            if (r == 1) {
                LN *o = (LN *)D_004C2AB4;
                while (*(LN * volatile *)&o->next) o = o->next;
                if (!func_003EBA80((int)o, e)) func_003ED0D0(D_004BA9D0);
            } else if (!func_003EBC20(r, e))
                func_003ED0D0(D_004BA9D0);
        }
}
