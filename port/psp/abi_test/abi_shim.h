/* Calls between clang (o32) code and psp-gcc/PSP SDK (EABI32) code.
 * Safe directly: <=4 int/pointer args, int/pointer/void/float return,
 * no varargs, no 64-bit or struct-by-value args. Anything else: a psp-gcc
 * shim that takes one pointer to an argument struct. */
struct g6f_args { int a, b, c, d, e; float x; };
int g4(int, int, int, int);
int g6f(int, int, int, int, int, float);
float g_half(int);
int shim_g6f(const struct g6f_args *);
int call_cb(int (*)(int, int, int, int), int *);
void report(const char *, int, int);
void run_o32_tests(void);
