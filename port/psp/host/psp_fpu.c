/* EE single-precision rounding on the Allegrex FPU (o32).
 *
 * PSP builds compile the game without the ps2_fp_rewrite helpers
 * (MVC2_PS2_FLOAT=OFF in port/cmake/psp-clang.cmake): those compute in
 * double, which is libgcc soft-float here (6-38 ms a frame).  The FPU gives
 * the EE's results itself with FCSR RM = 1 (toward zero; overflow is the
 * largest finite) and FS = 1 (denormals to zero): 0 diverging words against
 * the helpers over 600 frames (port/docs/PSP.md).
 *
 * psp_fpu_init() sets that on the calling thread (FCSR is per thread: every
 * thread that runs game code must call it).  MVC2_PS2FP_OFF=1, like the
 * helpers' A/B switch, rounds to nearest.  The arcade setting
 * (ps2_float_set_nearest, port/host/fe/fe_arcade.c) is wrapped at link time
 * (--wrap) so it switches the FPU as well as the helpers' flag. */
#include <stdlib.h>
#include "psp_entry.h"

static int nearest;

static void fcsr_apply(void)
{
    unsigned fcsr;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcsr));
    /* RM, FS = 1; and no FPU exceptions: PSP threads start with the
     * invalid / divide-by-zero / overflow traps enabled (0x0e00), the EE
     * never traps (enables 7-11 and causes 12-17 cleared) */
    fcsr = (fcsr & ~(3u | 0x1F80u | 0x3F000u)) | (nearest ? 0u : 1u) | (1u << 24);
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcsr));
}

unsigned psp_fpu_init(void)
{
    const char *e = getenv("MVC2_PS2FP_OFF");
    unsigned fcsr;
    nearest = e && *e && *e != '0';
    fcsr_apply();
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcsr));
    return fcsr;
}

void __real_ps2_float_set_nearest(int on);
void __wrap_ps2_float_set_nearest(int on)
{
    const char *e = getenv("MVC2_PS2FP_OFF");
    __real_ps2_float_set_nearest(on);
    nearest = on || (e && *e && *e != '0');
    fcsr_apply();
}

/* rounding mode in effect (psp_walk.c keys its angle memo on it) */
int psp_fpu_nearest(void)
{
    return nearest;
}

/* P42: double -> float follows FCSR's rounding mode.  The game's doubles
 * are libgcc soft-float here, and libgcc's __truncdfsf2 always rounds to
 * nearest; the EE game converts with the soft-float dptofp, toward zero,
 * which the other ports' helpers give (port/host/ps2_float.c ps2_d2f: toward
 * zero, denormal results to signed zero, finite overflow to the largest
 * float).  So in round-toward-zero (the game's threads, psp_fpu_init) this
 * is ps2_d2f, and in round-to-nearest (MVC2_PS2FP_OFF, arcade.float_round,
 * psp_libm.c's calls) libgcc's own.  --wrap=__truncdfsf2
 * (port/psp/CMakeLists.txt). */
float __real___truncdfsf2(double d);
float __wrap___truncdfsf2(double d)
{
    union { double d; unsigned long long u; } in = { d };
    union { unsigned u; float f; } out;
    unsigned fcsr, sign, e;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcsr));
    if ((fcsr & 3) != 1)
        return __real___truncdfsf2(d);
    sign = (unsigned)(in.u >> 32) & 0x80000000u;
    e = (unsigned)(in.u >> 52) & 0x7FF;
    if (e == 0x7FF)                    /* inf, NaN: as converted */
        return __real___truncdfsf2(d);
    if ((int)e - 1023 + 127 >= 255)    /* beyond the largest float */
        out.u = sign | 0x7F7FFFFFu;
    else if ((int)e - 1023 + 127 <= 0) /* below the smallest normal: signed zero */
        out.u = sign;
    else
        out.u = sign | (e - 1023 + 127) << 23 | (unsigned)(in.u >> 29) & 0x7FFFFFu;
    return out.f;
}
