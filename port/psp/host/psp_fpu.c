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
    fcsr = (fcsr & ~3u) | (nearest ? 0u : 1u) | (1u << 24);
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
