/* Single-player PSP (P61-P64): player 2 is never connected
 * (plat_pad_connected, slot 0 only), so the game already keeps P2 out and
 * its main-menu cursor skips VS (func_00128CA0 needs both pads). What is
 * left are the empty side's debug-text prompts, blanked here before boot:
 *   PRESS START BUTTON / NO CHALLENGER (residual_00172B70, func_003D7350)
 * An empty string still takes a text record but draws nothing. */
extern char gPressStartLabel_004B5640[];
extern char gNoChallengerLabel_004B5630[];

void psp_1p_init(void)
{
    gPressStartLabel_004B5640[0] = 0;
    gNoChallengerLabel_004B5630[0] = 0;
}

/* Main menu (P62): the cursor skips VS when func_00128CA0 != 0, which the
 * game returns unless both D_004F8010 and D_004F8014 are 1. Those read 1 on
 * PSP, so report "not both" always (the game's own skip; only caller is
 * func_00128CD0). */
int __wrap_func_00128CA0(void)
{
    return 1;
}

/* Button config (P64): func_00149690(p) draws one player's column (title,
 * button rows, vibrate, command, defaults, exit), p[1] = player. Skip the
 * PLAYER2 column; P2's settings stay in the save block untouched. */
void __real_func_00149690(unsigned char *p);
void __wrap_func_00149690(unsigned char *p)
{
    if (p[1] == 1)
        return;
    __real_func_00149690(p);
}

/* func_00149020 draws both players' cursor bars from D_004F7D10 + i*16
 * (e[0] = row, e[0xC] = bar entry, which func_00149690 resets each frame).
 * For its duration only, give P2 a row past the screen: no case matches,
 * so e[0xC] is left alone and the bar is placed below the display. */
extern signed char D_004F7D20[];
void __real_func_00149020(void);
void __wrap_func_00149020(void)
{
    signed char row = D_004F7D20[0];
    D_004F7D20[0] = 127;
    __real_func_00149020();
    D_004F7D20[0] = row;
}
