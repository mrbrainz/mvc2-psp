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
    __real_fe_sync_handle(h);
    seen[k].h = h;
    seen[k].frame = mvc2_host_frame;
}

void func_003EA660__game(int n, void *list);
void __wrap_func_003EA660(int n, void *list)
{
    const unsigned *l = (const unsigned *)list;
    func_003EA660__game(n, list);
    for (int i = 0; i < n; i++)
        __wrap_fe_sync_handle(l[i]);
}
