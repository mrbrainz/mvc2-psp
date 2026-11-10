/* o32 side of the PSP headless runner: the same sequence as
 * port/stubs/host_main.c (boot, then N frames), printing the logic-state hash
 * (ra_state_hash) every 60 frames for comparison with the native build. */
#include "mvc2_stubs.h"
#include "../../host/host_loop.h"
#include "../../host/runahead.h"
#include "psp_entry.h"

void host_state_dump_open(const char *path);   /* port/host/state_dump.c */

/* no varargs across o32/EABI: format here */
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *put_u(char *p, unsigned long v) {
    char t[12]; int n = 0;
    do t[n++] = '0' + v % 10; while (v /= 10);
    while (n) *p++ = t[--n];
    return p;
}
static char *put_x64(char *p, unsigned long long v) {
    for (int i = 60; i >= 0; i -= 4) *p++ = "0123456789abcdef"[(v >> i) & 15];
    return p;
}

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the o32/EABI boundary items mvc2_host relies on (port/docs/PSP.md) */
static void selftest(void) {
    char b[128], w[8] = "";
    int x = 0, y = 0, ok;
    volatile double d = 2.0;
    FILE *f;
    snprintf(b, sizeof b, "%d %s %x %5.2f %lld", -7, "ok", 255, 3.14159, -12345678901LL);
    ok = !strcmp(b, "-7 ok ff  3.14 -12345678901");
    ok &= sscanf("1f/20 abc", "%x/%x %3s", &x, &y, w) == 3 && x == 31 && y == 32 && !strcmp(w, "abc");
    ok &= fmaf(1.0f + 0x1p-23f, 1.0f - 0x1p-23f, -1.0f) == -0x1p-46f;   /* needs single rounding */
    ok &= (int)(sqrt(d) * 1000) == 1414 && floor(-1.5) == -2.0 && d / 3 > 0.666 && scalbn(d, 3) == 16.0;
    ok &= (long long)(d * 1e10) / 7 == 2857142857LL;
    f = fopen("mvc2_psp_seek.tmp", "w+");
    ok &= f && fputs("0123456789", f) >= 0 && fseeko(f, 6, SEEK_SET) == 0 && fgetc(f) == '6';
    if (f) { fclose(f); remove("mvc2_psp_seek.tmp"); }
    fprintf(stderr, "[host] o32/EABI selftest %s (%s)\n", ok ? "PASS" : "FAIL", b);
}

int mvc2_psp_run(int frames) {
    char b[96], *p;
    psp_log("[host] mvc2 PSP headless\n");
    selftest();
    host_state_dump_open("ms0:/PSP/GAME/mvc2/state.bin");   /* compare.py input */
    psp_log("[host] boot\n");
    if (host_disc_open("ms0:/PSP/GAME/mvc2/mvc2.iso") != 0)
        psp_log("[host] no disc image: file loads will fail\n");
    mvc2_boot(1);
    while (mvc2_host_frame < frames) {
        mvc2_frame();
        if (mvc2_host_frame % 60 == 0) {
            p = put_s(b, "frame ");
            p = put_u(p, (unsigned long)mvc2_host_frame);
            p = put_s(p, " hash ");
            p = put_x64(p, ra_state_hash());
            p = put_s(p, "\n");
            *p = 0;
            psp_log(b);
        }
    }
    psp_log("[host] done\n");
    return 0;
}
