/* PSP system savedata (psp-gcc, EABI32), see psp_save.h.  The utility runs
 * its own state machine: InitStart, Update once per frame while it is
 * visible (2), ShutdownStart when finished (3), done at none (0).  The
 * blocking calls (AUTOLOAD / AUTOSAVE: no dialog) spin on vblank; the list
 * dialogs are driven from the renderer's present (psp_save_dialog_frame). */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psputility.h>
#include <string.h>
#include "psp_save.h"
#include "psp_entry.h"
#include "psp_icon0.h"   /* generated: port/psp/tools/make_icon0.py (fallback) */

#define GAME "MVC2PSP00"
#define SAVE "0000"
#define NO_DATA 0x80110307   /* SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA */
#define CANCEL 1             /* base.result when the player backs out */

static char names[2][20] = { SAVE, "" };
static SceUtilitySavedataParam dlg;

/* Savedata icon: the EBOOT's own ICON0 section (XMB art the builder adds from
 * the player's disc), else the plain built-in icon. */
static unsigned char icon_buf[64 * 1024];
static const void *icon = psp_icon0;
static unsigned icon_size = sizeof psp_icon0;

static void load_icon(void)
{
    static int tried;
    unsigned h[10];
    SceUID fd;
    if (tried || !psp_eboot_path) return;
    tried = 1;
    fd = sceIoOpen(psp_eboot_path, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    if (sceIoRead(fd, h, sizeof h) == sizeof h && h[0] == 0x50425000) {
        unsigned n = h[4] - h[3];               /* ICON0 runs to ICON1 */
        if (n >= 8 && n <= sizeof icon_buf && sceIoLseek32(fd, h[3], PSP_SEEK_SET) == (int)h[3]
            && sceIoRead(fd, icon_buf, n) == (int)n
            && !memcmp(icon_buf, "\x89PNG", 4)) {
            icon = icon_buf;
            icon_size = n;
        }
    }
    sceIoClose(fd);
}
static int dlg_on, dlg_closing;

static void setup(SceUtilitySavedataParam *p, int mode)
{
    memset(p, 0, sizeof *p);
    p->base.size = sizeof *p;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &p->base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP, &p->base.buttonSwap);
    p->base.graphicsThread = 0x11;
    p->base.accessThread = 0x13;
    p->base.fontThread = 0x12;
    p->base.soundThread = 0x10;
    p->mode = mode;
    p->overwrite = 1;
    strcpy(p->gameName, GAME);
    strcpy(p->saveName, SAVE);
    p->saveNameList = names;
    strcpy(p->fileName, "DATA.BIN");
    strcpy(p->sfoParam.title, "MvC2 PSP");
    strcpy(p->sfoParam.savedataTitle, "Game data");
    strcpy(p->sfoParam.detail, "Options, records and unlocks.");
    p->sfoParam.parentalLevel = 1;
    load_icon();
    p->icon0FileData.buf = (void *)icon;
    p->icon0FileData.bufSize = icon_size;
    p->icon0FileData.size = icon_size;
}

static int run(SceUtilitySavedataParam *p)
{
    int st, spins = 0;
    if (sceUtilitySavedataInitStart(p) < 0) return -1;
    for (;;) {
        st = sceUtilitySavedataGetStatus();
        if (st == 2) sceUtilitySavedataUpdate(1);
        else if (st == 3) sceUtilitySavedataShutdownStart();
        else if (st == 0 && spins > 0) break;
        else if (st < 0) return st;
        sceDisplayWaitVblankStart();
        if (++spins > 60 * 30) return -2;
    }
    return p->base.result;
}

int psp_save_load(void *buf, unsigned cap, unsigned *size)
{
    SceUtilitySavedataParam p;
    int r;
    setup(&p, PSP_UTILITY_SAVEDATA_AUTOLOAD);
    p.dataBuf = buf;
    p.dataBufSize = cap;
    r = run(&p);
    if ((unsigned)r == NO_DATA) return 1;
    if (r < 0) return r;
    *size = p.dataSize;
    return 0;
}

int psp_save_store(const void *buf, unsigned size)
{
    SceUtilitySavedataParam p;
    setup(&p, PSP_UTILITY_SAVEDATA_AUTOSAVE);
    p.dataBuf = (void *)buf;
    p.dataBufSize = size;
    p.dataSize = size;
    return run(&p);
}

int psp_save_dialog_start(int save, void *buf, unsigned n)
{
    if (dlg_on) return -1;
    setup(&dlg, save ? PSP_UTILITY_SAVEDATA_LISTSAVE : PSP_UTILITY_SAVEDATA_LISTLOAD);
    dlg.dataBuf = buf;
    dlg.dataBufSize = n;
    dlg.dataSize = save ? n : 0;
    if (sceUtilitySavedataInitStart(&dlg) < 0) return -1;
    dlg_on = 1;
    dlg_closing = 0;
    return 0;
}

int psp_save_dialog_frame(void)
{
    int st;
    if (!dlg_on) return 0;
    st = sceUtilitySavedataGetStatus();
    if (st == 2) sceUtilitySavedataUpdate(1);
    else if (st == 3 && !dlg_closing) { sceUtilitySavedataShutdownStart(); dlg_closing = 1; }
    else if ((st == 0 && dlg_closing) || st < 0) dlg_on = 0;
    return dlg_on;
}

int psp_save_dialog_result(unsigned *size)
{
    int r = dlg.base.result;
    if (r == 0) { if (size) *size = dlg.dataSize; return 0; }
    if (r == CANCEL || (unsigned)r == NO_DATA) return 1;
    return r;
}
