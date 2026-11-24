/* P114 spike: run our own code on the Media Engine (PSP-2003, ARK-4).
 *
 * ME_SPIKE=1 in env.txt: boot the ME (kubridge kuKernelCall: stub to the ME
 * reset vector, ME reset pulsed) into a loop that counts a heartbeat.
 * ME_SPIKE=2: the ME copies a 64 KB block over and over (worst-case bus load).
 * ME_SPIKE=3: once per logic step the ME copies ME_KB KB (default 512), a
 * stand-in for building a frame's GE list.
 * The [me] line (every 5 s) gives the heartbeat and the KB the ME moved.
 *
 * Main and ME share no cache: the main CPU reads and writes the shared words
 * through the uncached mirror (0x40000000), the ME through kseg1 (0xA0000000);
 * the ME's buffers and stack are its own (kseg0, cached on the ME only). */
#include <pspkernel.h>
#include <kubridge.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>

extern char psp_me_stub[], psp_me_stub_end[];

/* shared words, each group in its own cache line */
enum { SH_BEAT = 0, SH_CMD = 16, SH_MODE, SH_SRC, SH_DST, SH_BYTES, SH_DONE = 32, SH_KB };
static uint32_t me_shared[64] __attribute__((aligned(64)));
static uint8_t me_stack[16384] __attribute__((aligned(64)));
#define ME_BUF (64u << 10)

static volatile uint32_t *sh_main(void) { return (volatile uint32_t *)((uintptr_t)me_shared | 0x40000000u); }
static uint32_t kseg0(const void *p) { return (uint32_t)(uintptr_t)p | 0x80000000u; }

/* ---------------- runs on the ME ---------------- */
__attribute__((noinline))   /* volatile: no memcpy call, the ME runs only this unit's code */
static void me_copy(volatile uint32_t *d, const volatile uint32_t *s, unsigned words)
{
    for (unsigned i = 0; i < words; i += 4) { d[i] = s[i]; d[i + 1] = s[i + 1]; d[i + 2] = s[i + 2]; d[i + 3] = s[i + 3]; }
}

__attribute__((noinline))
void psp_me_main(volatile uint32_t *sh)
{
    for (;;) {
        uint32_t mode = sh[SH_MODE];
        sh[SH_BEAT]++;
        if (mode == 2) {
            me_copy((uint32_t *)sh[SH_DST], (const uint32_t *)sh[SH_SRC], ME_BUF / 4);
            sh[SH_KB] += ME_BUF >> 10;
            continue;
        }
        if (mode == 3) {
            uint32_t c = sh[SH_CMD];
            if (c != sh[SH_DONE]) {
                for (uint32_t left = sh[SH_BYTES]; left; ) {
                    uint32_t n = left < ME_BUF ? left : ME_BUF;
                    me_copy((uint32_t *)sh[SH_DST], (const uint32_t *)sh[SH_SRC], n / 4);
                    left -= n;
                }
                sh[SH_KB] += sh[SH_BYTES] >> 10;
                sh[SH_DONE] = c;
            }
        }
        for (int k = 0; k < 2000; k++) __asm__ volatile("nop");   /* poll without touching the bus */
    }
}

/* ---------------- main CPU ---------------- */
/* kernel mode (kuKernelCall): stub and its words to the ME reset vector, ME reset pulsed */
static int me_boot_k(uint32_t entry, uint32_t sp, uint32_t arg)
{
    volatile uint32_t *v = (volatile uint32_t *)0xBFC00000u;
    const uint32_t *s = (const uint32_t *)psp_me_stub;
    unsigned n = (unsigned)(psp_me_stub_end - psp_me_stub) / 4;
    volatile uint32_t *rst = (volatile uint32_t *)0xBC10004Cu;
    uint32_t r;
    for (unsigned i = 0; i < n; i++) v[i] = s[i];
    v[0x600 / 4] = entry; v[0x604 / 4] = sp; v[0x608 / 4] = arg;
    __asm__ volatile("sync");
    r = *rst;
    *rst = r | 0x14u;
    __asm__ volatile("sync");
    *rst = r & ~0x14u;
    __asm__ volatile("sync");
    return (int)r;
}

static int me_mode;
static int me_boot(void)
{
    KernelCallArg a = {0};
    int ret;
    sceKernelDcacheWritebackInvalidateAll();   /* shared words, stack and buffers in RAM, not in our cache */
    a.arg1 = kseg0((const void *)psp_me_main);
    a.arg2 = kseg0(me_stack + sizeof me_stack);
    a.arg3 = (uint32_t)(uintptr_t)me_shared | 0xA0000000u;
    ret = kuKernelCall((void *)me_boot_k, &a);
    sceKernelDelayThread(20000);
    return ret;
}

static uint32_t me_bytes, me_beat0, me_kb0;

int psp_me_spike_init(void)
{
    const char *e = getenv("ME_SPIKE");
    void *src = NULL, *dst = NULL;
    int ret;
    if (!e || (me_mode = atoi(e)) <= 0) return 0;
    if ((unsigned)(psp_me_stub_end - psp_me_stub) > 0x600) return -2;
    { const char *k = getenv("ME_KB"); me_bytes = (k ? (unsigned)atoi(k) : 512u) << 10; }
    if (me_mode >= 2) {
        src = memalign(64, ME_BUF); dst = memalign(64, ME_BUF);
        if (!src || !dst) return -3;
        for (unsigned i = 0; i < ME_BUF; i++) ((uint8_t *)src)[i] = (uint8_t)i;
    }
    me_shared[SH_MODE] = (uint32_t)me_mode;
    me_shared[SH_SRC] = src ? kseg0(src) : 0;
    me_shared[SH_DST] = dst ? kseg0(dst) : 0;
    me_shared[SH_BYTES] = me_bytes;
    ret = me_boot();
    return ret;
}

/* once per logic step (mode 3): post the next block if the last one is done */
void psp_me_spike_step(void)
{
    volatile uint32_t *sh;
    if (me_mode != 3) return;
    sh = sh_main();
    if (sh[SH_DONE] == sh[SH_CMD]) sh[SH_CMD] = sh[SH_CMD] + 1;
}

int psp_me_spike_report(unsigned out[4])
{
    volatile uint32_t *sh;
    uint32_t b, k;
    if (!me_mode) return 0;
    sh = sh_main();
    b = sh[SH_BEAT]; k = sh[SH_KB];
    out[0] = b - me_beat0; out[1] = k - me_kb0; out[2] = sh[SH_CMD]; out[3] = sh[SH_DONE];
    me_beat0 = b; me_kb0 = k;
    return me_mode;
}

/* after a resume: 1 = our code still runs, else the result of booting it again */
int psp_me_spike_resumed(void)
{
    uint32_t b;
    if (!me_mode) return 0;
    b = sh_main()[SH_BEAT];
    sceKernelDelayThread(20000);
    if (sh_main()[SH_BEAT] != b) return 1;
    return 100 + me_boot();
}
