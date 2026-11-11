/* PSP system savedata (psp_save.c, EABI) for the o32 host: one entry,
 * ms0:/PSP/SAVEDATA/MVC2PSP000000/DATA.BIN holding the game's save block.
 * Every call takes <=4 int/pointer args. */
#ifndef PSP_SAVE_H
#define PSP_SAVE_H
/* No dialog (blocking): 0 = done (*size bytes read), 1 = no saved data,
 * < 0 = error (utility result). */
int psp_save_load(void *buf, unsigned cap, unsigned *size);
int psp_save_store(const void *buf, unsigned size);
/* System Save / Load list dialog: start it, then call psp_save_dialog_frame
 * once per display frame after the frame's GE list has finished and before
 * the swap (it draws over the frame); it returns 0 once the dialog has
 * closed.  psp_save_dialog_result: 0 = saved / loaded (*size bytes),
 * 1 = cancelled or no data, < 0 = error. */
int psp_save_dialog_start(int save, void *buf, unsigned n);
int psp_save_dialog_frame(void);
int psp_save_dialog_result(unsigned *size);
#endif
