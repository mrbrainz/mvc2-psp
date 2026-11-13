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
