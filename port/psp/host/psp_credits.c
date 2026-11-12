/* Ending staff roll (P65): the port's credits go first, right after the
 * "STAFF CREDIT" title. func_0016B440 (credits init) points the roll's byte
 * stream D_004C2678 at the game's script (FD/FE end a line and pick the next
 * line's style, FF stops); this --wrap points it at a copy with our lines
 * inserted and lengthens the sequence timer (h8) by the time they take, one
 * line every 0x20 frames, so the original roll still finishes. */
#include <string.h>

typedef unsigned char u8;
extern u8 gCreditsScriptBytes_004B42B0[0x5CF];
extern u8 *D_004C2678;
extern u8 *D_004C2840;

#define TITLE_LEN 15   /* "STAFF CREDIT" FE FE FE */
static const char ours[] =
    "GAME PSP PORT BY\375"
    " MR BRAINZ &\375"
    " LLM VIBES\376"
    "\376"
    "\376"
    "ORIGINAL GAME DECOMP\375"
    " G. GUTHRIE\376"
    "\376"
    "\376";
#define OURS_LEN (sizeof ours - 1)
#define OURS_LINES 9

static u8 script[sizeof gCreditsScriptBytes_004B42B0 + OURS_LEN];

void __real_func_0016B440(void);
void __wrap_func_0016B440(void)
{
    __real_func_0016B440();
    if (D_004C2678 != gCreditsScriptBytes_004B42B0)
        return;
    if (!script[0]) {
        memcpy(script, gCreditsScriptBytes_004B42B0, TITLE_LEN);
        memcpy(script + TITLE_LEN, ours, OURS_LEN);
        memcpy(script + TITLE_LEN + OURS_LEN, gCreditsScriptBytes_004B42B0 + TITLE_LEN,
               sizeof gCreditsScriptBytes_004B42B0 - TITLE_LEN);
    }
    D_004C2678 = script;
    *(short *)(D_004C2840 + 8) += OURS_LINES * 0x20;
}
