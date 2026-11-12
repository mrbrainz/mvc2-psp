/* SCRIPT=path in env.txt (testing): upstream's --script runner
 * (port/host/fe/fe_main.c, copied; keep in step by hand) for the PSP build,
 * e.g. port/tools/scripts/arcade_ending_rank.txt. "shot" writes the
 * displayed frame as a BMP (psp_shot_fb, EABI). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "host_loop.h"
#include "pad.h"
#include "platform.h"
#include "psp_entry.h"

typedef uint8_t u8;
typedef uint16_t u16;
int psp_shot_fb(void **pix, int *stride);   /* psp_entry.c (EABI): displayed 8888 frame */

static void write_shot(const char *path)
{
    void *pix;
    int stride, x, y;
    FILE *f;
    static uint8_t row[480 * 3];
    uint8_t h[54] = { 'B', 'M' };
    uint32_t sz = 54 + 480 * 272 * 3;
    if (psp_shot_fb(&pix, &stride) != 0) { host_log("script: shot %s: no frame buffer", path); return; }
    if (!(f = fopen(path, "wb"))) { host_log("script: shot %s: cannot write", path); return; }
    memcpy(h + 2, &sz, 4);
    h[10] = 54; h[14] = 40; h[18] = 480 & 0xFF; h[19] = 480 >> 8; h[22] = 272 & 0xFF; h[23] = 272 >> 8;
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = 271; y >= 0; y--) {
        const uint32_t *s = (const uint32_t *)pix + (size_t)y * stride;
        for (x = 0; x < 480; x++) {
            uint32_t c = s[x] | 0;
            row[3 * x] = (uint8_t)(c >> 16); row[3 * x + 1] = (uint8_t)(c >> 8); row[3 * x + 2] = (uint8_t)c;
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
    host_log("script: shot %s at frame %ld", path, mvc2_host_frame);
}

static const struct { const char *n; u16 bit; } BTN[] = {
    {"start", PS2_START}, {"select", PS2_SELECT}, {"cross", PS2_CROSS}, {"circle", PS2_CIRCLE},
    {"square", PS2_SQUARE}, {"triangle", PS2_TRIANGLE}, {"up", PS2_UP}, {"down", PS2_DOWN},
    {"left", PS2_LEFT}, {"right", PS2_RIGHT}, {"l1", PS2_L1}, {"r1", PS2_R1}, {"l2", PS2_L2},
    {"r2", PS2_R2},
};

/* --script FILE (testing): state-driven scripted input for headless runs.
 * One command per line ('#' comments):
 *   state S [MAX]          wait until the game state (g[0..2] as "a/b/c",
 *                          a prefix such as "2/1" is enough) matches; give
 *                          up after MAX frames (default 7200) with a log line
 *   wait N                 let N frames pass
 *   press B[+B..] [LEN]    hold buttons on pad 1 for LEN frames (default 6),
 *                          then release for 6 frames; "p2:" before a button
 *                          name puts it on pad 2
 *   hold B[+B..] [LEN]     hold buttons for LEN frames (default 1), no release
 *                          after (for motion inputs)
 *   until S B[+B..] [GAP]  press B every GAP frames (default 30) until the
 *                          state matches S (at most 200 presses)
 *   shot NAME              write ms0:/PSP/GAME/mvc2/shots/NAME.bmp
 *   quit                   end the run */
static struct {
    char **line;
    int n, pc, timer, phase, count;
    u16 hold0, hold1;
} S;

static int state_match(const char *want)
{
    char cur[32];
    int st = mvc2_game_state();
    const u8 *g = (const u8 *)EE_BSS_PTR(0x004F4300);
    /* g[3], g[4] too: "2/1/2/3" names a sub-state; shorter names match as
     * prefixes, as before */
    snprintf(cur, sizeof cur, "%d/%d/%d/%d/%d", st >> 16, (st >> 8) & 0xFF, st & 0xFF, g[3], g[4]);
    return strncmp(cur, want, strlen(want)) == 0 &&
           (cur[strlen(want)] == 0 || cur[strlen(want)] == '/' || want[strlen(want) - 1] == '/');
}

static void script_buttons(const char *spec, u16 *b0, u16 *b1)
{
    char buf[128], *t, *save = NULL;
    int k;
    *b0 = *b1 = 0;
    snprintf(buf, sizeof buf, "%s", spec);
    for (t = strtok_r(buf, "+", &save); t; t = strtok_r(NULL, "+", &save)) {
        int p2 = !strncmp(t, "p2:", 3);
        const char *nm = p2 ? t + 3 : t;
        for (k = 0; k < (int)(sizeof BTN / sizeof BTN[0]); k++)
            if (!strcmp(nm, BTN[k].n))
                *(p2 ? b1 : b0) |= BTN[k].bit;
    }
}

static void write_shot(const char *path);

int psp_script_load(const char *path)
{
    FILE *f = fopen(path, "r");
    char ln[256];
    if (!f)
        return -1;
    while (fgets(ln, sizeof ln, f)) {
        char *c = strchr(ln, '#');
        if (c) *c = 0;
        for (c = ln + strlen(ln); c > ln && (c[-1] == '\n' || c[-1] == ' ' || c[-1] == '\r' || c[-1] == '\t'); c--)
            c[-1] = 0;
        for (c = ln; *c == ' ' || *c == '\t'; c++)
            ;
        if (!*c)
            continue;
        S.line = (char **)realloc(S.line, sizeof(char *) * (S.n + 1));
        S.line[S.n++] = strdup(c);
    }
    fclose(f);
    return 0;
}

/* one frame of the script: sets the buttons to hold this frame */
void psp_script_step(u16 *b0, u16 *b1)
{
    char cmd[32] = "", a1[160] = "", a2[64] = "", a3[32] = "";
    *b0 = *b1 = 0;
    while (S.pc < S.n) {
        /* clear the words a shorter line does not set (a "state" after an
         * "until" took the until's button name as its frame limit, 0) */
        cmd[0] = a1[0] = a2[0] = a3[0] = 0;
        sscanf(S.line[S.pc], "%31s %159s %63s %31s", cmd, a1, a2, a3);
        if (!strcmp(cmd, "state")) {
            int max = a2[0] ? atoi(a2) : 7200;
            if (state_match(a1)) { S.pc++; S.timer = 0; continue; }
            if (++S.timer > max) {
                host_log("script: line %d: state %s not reached in %d frames (now %06X)", S.pc + 1, a1, max, mvc2_game_state());
                S.pc++; S.timer = 0; continue;
            }
            return;
        } else if (!strcmp(cmd, "wait")) {
            if (S.timer++ < atoi(a1)) return;
            S.pc++; S.timer = 0; continue;
        } else if (!strcmp(cmd, "press")) {
            int len = a2[0] ? atoi(a2) : 6;
            if (S.timer < len) { script_buttons(a1, b0, b1); S.timer++; return; }
            if (S.timer++ < len + 6) return;
            S.pc++; S.timer = 0; continue;
        } else if (!strcmp(cmd, "hold")) {
            /* hold B for LEN frames (default 1) with no release after:
             * motion inputs (hold down 2, hold down+right 2, hold right+square 2) */
            int len = a2[0] ? atoi(a2) : 1;
            if (S.timer < len) { script_buttons(a1, b0, b1); S.timer++; return; }
            S.pc++; S.timer = 0; continue;
        } else if (!strcmp(cmd, "until")) {
            int gap = a3[0] ? atoi(a3) : 30;
            if (S.timer == 0 && state_match(a1)) { S.pc++; S.count = 0; continue; }
            if (S.count > 200) {
                host_log("script: line %d: state %s not reached after 200 presses (now %06X)", S.pc + 1, a1, mvc2_game_state());
                S.pc++; S.count = 0; S.timer = 0; continue;
            }
            if (S.timer < 6) script_buttons(a2, b0, b1);
            if (++S.timer >= gap) { S.timer = 0; S.count++; }
            return;
        } else if (!strcmp(cmd, "shot")) {
            char path[512];
            if (!S.phase) { S.phase = 1; return; }   /* the frame after: displayed */
            S.phase = 0;
            { extern const char *psp_game_dir(void); snprintf(path, sizeof path, "%s/shots/%s.bmp", psp_game_dir(), a1); }
            write_shot(path);
            S.pc++; continue;
        } else if (!strcmp(cmd, "quit")) {
            host_log("script: done at frame %ld", mvc2_host_frame);
            plat_request_quit();
            S.pc = S.n;
            return;
        } else {
            host_log("script: line %d: unknown command %s", S.pc + 1, cmd);
            S.pc++;
        }
    }
}
