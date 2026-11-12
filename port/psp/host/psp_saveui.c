/* PSP SAVE/LOAD (P59): the main menu's SAVE and LOAD screens (the PS2 slot
 * screens, func_0041DF70 / func_0041EE80, called from the SAVE/LOAD menu
 * func_0041DBA0) are replaced, with --wrap, by the system savedata Save /
 * Load list dialogs (psp_save.c).
 *   save: func_0038AA30(1) builds the game's save block (D_0058C020, 0x754
 *         bytes, what the game writes to the card), which the dialog saves.
 *   load: the dialog reads the block into D_005852A0 (where the game's card
 *         loader reads it); func_0038C210(1) checks it and func_0038B8C0(1)
 *         applies it, as func_0038C4A0 does after a card read.
 * Both then return to the SAVE/LOAD menu as the game's screens do.
 *   boot (P58): the boot memory-card screen (front end 1/8, func_00130040)
 *         loads the savedata without a dialog, applies it, turns autosave
 *         on when that worked (as the game does) and leaves as that screen does, without its PS2 card messages.
 *   autosave: the game's card writer for its save block
 *         (func_0038B2E0(1, ...), from every autosave screen) builds the
 *         block and stores it without a dialog instead.  The
 * dialog is drawn from r_gu_present (psp_saveui_frame). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mvc2_stubs.h"
#include "host.h"
#include "psp_save.h"

#define BLOCK 0x754
#define G_MENU 0x004C2840      /* front-end work: b3 sub-state, b5 state, b6, b7 */
#define D_SAVEBUF 0x0058C020   /* func_0038D1B0(1) */
#define D_LOADBUF 0x005852A0   /* func_0038C4A0's read buffer */
#define D_LOADLEN 0x004C29B0    /* bytes the card loader read */
#define D_MSG 0x004D17C9       /* the screen's message id */
#define D_AUTOSAVE 0x004D179B  /* 1: autosave on (set by a successful autoload) */
#define D_PENDING 0x004D17CB   /* 1/8's exit: run func_0012FC40 when set */

void func_0038AA30(int a);
int func_0038C210(int a);
void func_0038B8C0(int a);
void func_0012FC40(void);

static uint8_t buf[BLOCK] __attribute__((aligned(64)));
static int phase, open_;
static int apply_loaded(void);

/* r_gu_present: 1 while the dialog is up (it drew over the frame) */
int psp_saveui_frame(void)
{
    if (!open_) return 0;
    open_ = psp_save_dialog_frame();
    return 1;
}

static void back_to_menu(void)
{
    uint8_t *g = EE_BSS(uint8_t *, G_MENU);
    g[3] = 1; g[5] = 0; g[6] = 0; g[7] = 0;
    EE_BSS(uint8_t, D_MSG) = 0;
    phase = 0;
}

static int start(int save)
{
    int r = psp_save_dialog_start(save, buf, BLOCK);
    fprintf(stderr, "[save] %s dialog start: %d\n", save ? "save" : "load", r);
    if (r != 0) return -1;
    open_ = 1;
    phase = 1;
    return 0;
}

void __wrap_func_0041DF70(unsigned input)   /* SAVE */
{
    unsigned n = 0;
    (void)input;
    if (phase == 0) {
        func_0038AA30(1);
        memcpy(buf, EE_BSS_PTR(D_SAVEBUF), BLOCK);
        if (start(1) != 0) back_to_menu();
        return;
    }
    if (open_) return;
    fprintf(stderr, "[save] dialog save: %d\n", psp_save_dialog_result(&n));
    back_to_menu();
}

void __wrap_func_0041EE80(int arg)   /* LOAD */
{
    unsigned n = 0;
    int r;
    (void)arg;
    if (phase == 0) {
        if (start(0) != 0) back_to_menu();
        return;
    }
    if (open_) return;
    r = psp_save_dialog_result(&n);
    if (r == 0 && n == BLOCK) {
        r = apply_loaded();
        fprintf(stderr, "[save] dialog load: check %d\n", r);
    } else
        fprintf(stderr, "[save] dialog load: %d (%u bytes)\n", r, n);
    back_to_menu();
}

static int apply_loaded(void)
{
    memcpy(EE_BSS_PTR(D_LOADBUF), buf, BLOCK);
    EE_BSS(int32_t, D_LOADLEN) = BLOCK;   /* func_0038C210 checks the read size */
    if (func_0038C210(1) != 0) return -1;
    func_0038B8C0(1);
    return 0;
}

void __wrap_func_00130040(void)   /* boot memory-card screen (1/8) */
{
    uint8_t *g = EE_BSS(uint8_t *, G_MENU);
    unsigned n = 0;
    int r = psp_save_load(buf, BLOCK, &n);
    if (r == 0 && n == BLOCK) r = apply_loaded();
    EE_BSS(uint8_t, D_AUTOSAVE) = r == 0;   /* as 1/8: autosave on after a good autoload */
    fprintf(stderr, "[save] boot autoload: %s (%d, %u bytes)\n", r == 0 ? "loaded" : r == 1 ? "no data" : "failed", r, n);
    /* func_00130040 case 6: on to the next front-end screen */
    g[1]++;
    g[2] = g[3] = g[4] = g[5] = 0;
    if (EE_BSS(uint8_t, D_PENDING)) { EE_BSS(uint8_t, D_PENDING) = 0; func_0012FC40(); }
}

int __wrap_func_0038B2E0(int a, int slot, int c)   /* card writer: save block */
{
    int __real_func_0038B2E0(int, int, int);
    int r;
    if (a != 1) return __real_func_0038B2E0(a, slot, c);
    func_0038AA30(1);
    memcpy(buf, EE_BSS_PTR(D_SAVEBUF), BLOCK);
    r = psp_save_store(buf, BLOCK);
    fprintf(stderr, "[save] autosave: %d\n", r);
    return r == 0 ? 0 : -0x10000;
}

/* mc.c (the PS2 card emulation the autosave screens still query) creates
 * ./mvc2_memcard on sceMcInit / sceMcGetInfo.  Saves go to PSP savedata, so
 * nothing is ever written there: report the folder made without making it
 * (mkdir is --wrap'ed; every other path is passed through). */
int __real_mkdir(const char *path, unsigned mode);
int __wrap_mkdir(const char *path, unsigned mode)
{
    if (!strncmp(path, "mvc2_memcard", 12))
        return 0;
    return __real_mkdir(path, mode);
}
