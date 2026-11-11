/* o32 definitions of the newlib functions that cannot be called across the
 * o32/EABI32 boundary (port/docs/PSP.md): varargs and va_list (printf/scanf
 * family), three float args (fmaf) and a 64-bit arg before an int (fseeko,
 * by --wrap).
 * Defined here, they replace newlib's at link time for all o32 callers. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "psp_entry.h"

/* ---- printf family ------------------------------------------------------ */

struct out { char *p; size_t n, len; };
static void put(struct out *o, char c) { if (o->len + 1 < o->n) o->p[o->len] = c; o->len++; }
static void puts_n(struct out *o, const char *s, int n) { while (n-- > 0) put(o, *s++); }
static void pad(struct out *o, char c, int n) { while (n-- > 0) put(o, c); }

static int utoa(char *b, unsigned long long v, int base, int upper) {
    const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char t[24]; int n = 0, i;
    do t[n++] = d[v % base]; while (v /= base);
    for (i = 0; i < n; i++) b[i] = t[n - 1 - i];
    return n;
}

/* fixed or exponent form of |v| into b (diagnostic precision, not glibc-exact) */
static int dtoa(char *b, double v, int prec, char conv) {
    int n = 0, e = 0, i;
    if (v != v) return (memcpy(b, "nan", 3), 3);
    if (v > 1e308) return (memcpy(b, "inf", 3), 3);
    if (conv == 'g' || conv == 'G') {
        double t = v; int x = 0;
        if (prec == 0) prec = 1;
        while (t >= 10) { t /= 10; x++; }
        while (t > 0 && t < 1) { t *= 10; x--; }
        conv = (x < -4 || x >= prec) ? 'e' : 'f';
        prec = conv == 'e' ? prec - 1 : prec - 1 - x;
        if (prec < 0) prec = 0;
    }
    if (conv == 'e' || conv == 'E') {
        while (v >= 10) { v /= 10; e++; }
        while (v > 0 && v < 1) { v *= 10; e--; }
    }
    {
        double r = 0.5;
        for (i = 0; i < prec; i++) r /= 10;
        v += r;
        if (conv == 'e' && v >= 10) { v /= 10; e++; }
    }
    {
        unsigned long long ip = (unsigned long long)v;
        double f = v - (double)ip;
        n = utoa(b, ip, 10, 0);
        if (prec) b[n++] = '.';
        for (i = 0; i < prec; i++) { f *= 10; int d = (int)f; b[n++] = '0' + d; f -= d; }
    }
    if (conv == 'e' || conv == 'E') {
        b[n++] = conv;
        b[n++] = e < 0 ? '-' : '+';
        if (e < 0) e = -e;
        if (e < 10) b[n++] = '0';
        n += utoa(b + n, (unsigned)e, 10, 0);
    }
    return n;
}

int vsnprintf(char *buf, size_t size, const char *f, va_list ap) {
    struct out o = {buf, size, 0};
    for (; *f; f++) {
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1, lng = 0;
        char tmp[64], sign = 0;
        const char *s;
        int n;
        if (*f != '%') { put(&o, *f); continue; }
        for (f++;; f++) {
            if (*f == '-') left = 1; else if (*f == '0') zero = 1; else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1; else if (*f == '#') alt = 1; else break;
        }
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) left = 1, width = -width; f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + *f++ - '0';
        }
        for (;; f++) {
            if (*f == 'l') lng++; else if (*f == 'h') ;
            else if (*f == 'z' || *f == 't') lng = lng; else if (*f == 'j' || *f == 'q') lng = 2;
            else if (*f == 'L') ; else break;
        }
        switch (*f) {
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = v < 0 ? -(unsigned long long)v : (unsigned long long)v;
            sign = v < 0 ? '-' : plus ? '+' : space ? ' ' : 0;
            n = utoa(tmp, u, 10, 0);
            goto num;
        }
        case 'u': case 'x': case 'X': case 'o': case 'p': {
            unsigned long long u;
            int base = *f == 'o' ? 8 : *f == 'u' ? 10 : 16;
            if (*f == 'p') { u = (unsigned long)va_arg(ap, void *); alt = 1; }
            else u = lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            n = utoa(tmp, u, base, *f == 'X');
            if (alt && base == 16 && u) { put(&o, '0'); put(&o, *f == 'X' ? 'X' : 'x'); width -= 2; }
        num:
            if (prec >= 0) zero = 0;
            {
                int z = prec > n ? prec - n : 0, w = width - n - z - (sign != 0);
                if (!left && !zero) pad(&o, ' ', w);
                if (sign) put(&o, sign);
                if (!left && zero) pad(&o, '0', w);
                pad(&o, '0', z);
                puts_n(&o, tmp, n);
                if (left) pad(&o, ' ', w);
            }
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = va_arg(ap, double);
            sign = v < 0 ? '-' : plus ? '+' : space ? ' ' : 0;
            n = dtoa(tmp, v < 0 ? -v : v, prec < 0 ? 6 : prec, *f);
            prec = -1;
            goto num;
        }
        case 'c': tmp[0] = (char)va_arg(ap, int); s = tmp; n = 1; goto str;
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            n = (int)strlen(s);
            if (prec >= 0 && n > prec) n = prec;
        str:
            if (!left) pad(&o, ' ', width - n);
            puts_n(&o, s, n);
            if (left) pad(&o, ' ', width - n);
            break;
        case 'n': *va_arg(ap, int *) = (int)o.len; break;
        case '%': put(&o, '%'); break;
        default: put(&o, '%'); if (*f) put(&o, *f); else f--; break;
        }
    }
    if (size) buf[o.len < size ? o.len : size - 1] = 0;
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *f, ...) {
    va_list ap; int r;
    va_start(ap, f); r = vsnprintf(buf, size, f, ap); va_end(ap);
    return r;
}

int vfprintf(FILE *fp, const char *f, va_list ap) {
    char b[1024];
    int r = vsnprintf(b, sizeof b, f, ap);
    size_t n = r < (int)sizeof b ? (size_t)r : sizeof b - 1;
    if (fp == stderr || fp == stdout) psp_log(b);   /* debug screen + log file */
    else fwrite(b, 1, n, fp);
    return r;
}

int fprintf(FILE *fp, const char *f, ...) {
    va_list ap; int r;
    va_start(ap, f); r = vfprintf(fp, f, ap); va_end(ap);
    return r;
}

/* fputs/fputc on stderr/stdout (host_log's prefix and newline) to the log
 * too, not newlib's stream (P07); --wrap, port/psp/CMakeLists.txt. */
int __real_fputs(const char *s, FILE *fp);
int __real_fputc(int c, FILE *fp);
int __wrap_fputs(const char *s, FILE *fp) {
    if (fp == stderr || fp == stdout) { psp_log(s); return 0; }
    return __real_fputs(s, fp);
}
int __wrap_fputc(int c, FILE *fp) {
    if (fp == stderr || fp == stdout) { char b[2] = { (char)c, 0 }; psp_log(b); return (unsigned char)c; }
    return __real_fputc(c, fp);
}

/* ---- sscanf: d i u x o (h/l/ll), s, c, [set], n, *, width ---------------- */

static int isspace_(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

static int vsscanf_(const char *in, const char *f, va_list ap) {
    const char *s = in;
    int got = 0;
    for (; *f; f++) {
        int skip = 0, width = 0, lng = 0, hlf = 0;
        if (isspace_((unsigned char)*f)) { while (isspace_((unsigned char)*s)) s++; continue; }
        if (*f != '%' || f[1] == '%') {
            if (*f == '%') f++;
            if (*s != *f) break;
            s++; continue;
        }
        f++;
        if (*f == '*') skip = 1, f++;
        while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
        for (;; f++) { if (*f == 'l') lng++; else if (*f == 'h') hlf++; else break; }
        if (*f == 'n') { if (!skip) *va_arg(ap, int *) = (int)(s - in); continue; }
        if (*f != 'c' && *f != '[') while (isspace_((unsigned char)*s)) s++;
        if (!*s) return got ? got : -1;
        if (!width) width = *f == 'c' ? 1 : 1 << 30;
        if (*f == 's' || *f == 'c' || *f == '[') {
            char set[256] = {0}, *d = skip ? 0 : va_arg(ap, char *);
            int inv = 0, k = 0;
            if (*f == '[') {
                f++;
                if (*f == '^') inv = 1, f++;
                if (*f == ']') set[']'] = 1, f++;
                for (; *f && *f != ']'; f++)
                    if (f[1] == '-' && f[2] && f[2] != ']') { for (int c = (unsigned char)*f; c <= (unsigned char)f[2]; c++) set[c] = 1; f += 2; }
                    else set[(unsigned char)*f] = 1;
            }
            while (*s && k < width) {
                unsigned char c = (unsigned char)*s;
                if (*f == 's' && isspace_(c)) break;
                if (*f == ']' && set[c] == inv) break;
                if (d) d[k] = (char)c;
                k++, s++;
            }
            if (!k) break;
            if (d && *f != 'c') d[k] = 0;
            if (!skip) got++;
            continue;
        }
        {
            int base = *f == 'x' || *f == 'X' ? 16 : *f == 'o' ? 8 : *f == 'i' ? 0 : 10, neg = 0, k = 0;
            unsigned long long v = 0;
            if (*s == '-' || *s == '+') neg = *s++ == '-', width--;
            if ((base == 16 || base == 0) && s[0] == '0' && (s[1] | 32) == 'x') s += 2, base = 16, width -= 2;
            else if (base == 0) base = *s == '0' ? 8 : 10;
            for (; k < width; k++, s++) {
                int c = *s, dv = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : 99;
                if (dv >= base) break;
                v = v * base + dv;
            }
            if (!k) break;
            if (neg) v = -v;
            if (!skip) {
                if (lng >= 2) *va_arg(ap, long long *) = (long long)v;
                else if (lng) *va_arg(ap, long *) = (long)v;
                else if (hlf) *va_arg(ap, short *) = (short)v;
                else *va_arg(ap, int *) = (int)v;
                got++;
            }
        }
    }
    return got;
}

int sscanf(const char *in, const char *f, ...) {
    va_list ap; int r;
    va_start(ap, f); r = vsscanf_(in, f, ap); va_end(ap);
    return r;
}

/* ---- fmaf: one rounding (SH-4 FMAC in arcade_code.c) --------------------- */
/* a*b is exact in double; the sum is made exact with TwoSum, then rounded to
 * odd so the final double->float rounding is the only one. */
float fmaf(float a, float b, float c) {
    double p = (double)a * (double)b, s = p + c, bb = s - p;
    double err = (p - (s - bb)) + ((double)c - bb);
    if (err != 0 && s != 0) {
        union { double d; unsigned long long u; } x = {s};
        if ((err > 0) != (s > 0)) x.u -= 1;   /* toward zero: truncated value */
        x.u |= 1;                              /* round to odd */
        s = x.d;
    }
    return (float)s;
}

/* ---- fseeko: 64-bit off_t then int; through the EABI shim ---------------- */
/* linked with --wrap=fseeko: o32 callers land here, the shim calls newlib */
int __wrap_fseeko(FILE *fp, off_t off, int whence) {
    struct psp_fseeko_args a = {fp, off, whence};
    return psp_fseeko_shim(&a);
}

/* newlib's libm is EABI: a second float argument arrives in $f13, but o32
 * callers pass it in $f14, so two-float libm calls must be defined here */
float fminf(float a, float b) { return a != a ? b : b != b ? a : a < b ? a : b; }
float fmaxf(float a, float b) { return a != a ? b : b != b ? a : a > b ? a : b; }
