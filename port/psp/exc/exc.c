/* mvc2exc.prx: debug-only kernel module.  Placed next to EBOOT.PBP, the game
 * loads it at boot (kubridge, psp_entry.c); a CPU exception in any thread then
 * shows the registers on screen instead of the PSP switching off.  Match
 * EPC/RA against the "[host] code base" log line and mvc2_psp.elf. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <stdarg.h>
#include <stddef.h>

/* The kernel libc has no vsnprintf, which the debug screen needs: enough of
 * one for the register dump (%s %c %d %u %x %X %p, width, zero pad). */
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    size_t o = 0;
#define PUT(ch) do { if (o + 1 < n) buf[o] = (ch); o++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        int zero = 0, width = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h') fmt++;
        char tmp[16];
        int len = 0, neg = 0;
        unsigned v;
        const char *str = NULL;
        switch (*fmt) {
        case 's': str = va_arg(ap, const char *); if (!str) str = "(null)"; break;
        case 'c': tmp[len++] = (char)va_arg(ap, int); break;
        case 'd': { int d = va_arg(ap, int); if (d < 0) { neg = 1; v = 0u - (unsigned)d; } else v = (unsigned)d;
                    do { tmp[len++] = (char)('0' + v % 10); v /= 10; } while (v); break; }
        case 'u': v = va_arg(ap, unsigned); do { tmp[len++] = (char)('0' + v % 10); v /= 10; } while (v); break;
        case 'p': case 'x': case 'X': {
            const char *dig = *fmt == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
            v = *fmt == 'p' ? (unsigned)(size_t)va_arg(ap, void *) : va_arg(ap, unsigned);
            do { tmp[len++] = dig[v & 15]; v >>= 4; } while (v);
            break; }
        case '%': PUT('%'); continue;
        default: PUT('%'); PUT(*fmt); continue;
        }
        if (str) { while (*str) PUT(*str++); continue; }
        if (neg) PUT('-');
        for (int k = len + neg; k < width; k++) PUT(zero ? '0' : ' ');
        while (len) PUT(tmp[--len]);
    }
    if (n) buf[o < n ? o : n - 1] = 0;
#undef PUT
    return (int)o;
}

PSP_MODULE_INFO("mvc2exc", 0x1000, 1, 1);
PSP_NO_CREATE_MAIN_THREAD();

static void on_exception(PspDebugRegBlock *regs)
{
    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0x00400000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();
    pspDebugScreenPrintf("MvC2 PSP crashed. Photograph this screen.\n\n");
    pspDebugScreenPrintf("EPC %08X  RA %08X  BadVAddr %08X\nCause %08X  Status %08X\n\n",
                         (unsigned)regs->epc, (unsigned)regs->r[31], (unsigned)regs->badvaddr,
                         (unsigned)regs->cause, (unsigned)regs->status);
    pspDebugDumpException(regs);
    for (;;)
        sceKernelDelayThread(1000000);
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    pspDebugInstallErrorHandler(on_exception);
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
