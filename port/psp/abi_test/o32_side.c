/* clang (o32) side of the P05 ABI test: how the game objects are built. */
#include "abi_shim.h"

static int cb(int a, int b, int c, int d) { return a * 1000 + b * 100 + c * 10 + d; }

void run_o32_tests(void) {
    struct g6f_args s = {1, 2, 3, 4, 5, 1.25f};
    int ok = 0;
    report("direct 4 int args", g4(1, 2, 3, 4), 30);
    report("float return", (int)(g_half(7) * 10), 35);
    report("shim 5 int + float", shim_g6f(&s), 185);
    report("o32 callback from EABI", call_cb(cb, &ok), 1234);
    report("callback caller canary", ok, 1);
    /* expected to FAIL: shows why the shim rule exists */
    report("direct 6 args (no shim)", g6f(1, 2, 3, 4, 5, 1.25f), 185);
}
