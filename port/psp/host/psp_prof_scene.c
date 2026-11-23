/* P91: PROFILE=1 timers on the scene steps of the two render tasks, so
 * render pass 1 splits into its parts, each without the mesh draws
 * (func_003F0500) made inside it.  Each step runs a few times a frame
 * (timer cost negligible).  Wrapped at link time (port/psp/CMakeLists.txt);
 * src/ is unchanged.  Slots 64+ of psp_prof.c; nested slots are not
 * subtracted from their parents.
 *   fights etc. func_001529A0 (src/w177_e.c)
 *   character select func_00152570 (src/phase_callback_00152570.c) */
extern int psp_prof_on(void);
extern unsigned long long psp_prof_now(void);
extern void psp_prof_add(int slot, const char *label, unsigned long long us);
extern unsigned long long psp_prof_mesh_total(void);
/* each step's time minus the mesh draws (func_003F0500) inside it */
#define T0 unsigned long long t = psp_prof_now(), m = psp_prof_mesh_total()
#define T1 (psp_prof_now() - t - (psp_prof_mesh_total() - m))

/* the steps take at most two word arguments: forwarded as they come */
#define W(slot, fn, label)                                            \
    int __real_##fn(int a, int b);                                    \
    int __wrap_##fn(int a, int b)                                     \
    {                                                                 \
        int r;                                                        \
        if (!psp_prof_on()) return __real_##fn(a, b);                 \
        T0;                                                           \
        r = __real_##fn(a, b);                                        \
        psp_prof_add(slot, label, T1);                                \
        return r;                                                     \
    }

/* one slot per first argument (the layer / list number) */
#define WA(slot, fn, label, ...)                                      \
    int __real_##fn(int a, int b);                                    \
    int __wrap_##fn(int a, int b)                                     \
    {                                                                 \
        static const int keys[] = { __VA_ARGS__ };                    \
        static const char *const lab[] = { label "(" #__VA_ARGS__ ") #1", \
            label " #2", label " #3", label " #4", label " other" };   \
        int r, k;                                                     \
        if (!psp_prof_on()) return __real_##fn(a, b);                 \
        for (k = 0; k < (int)(sizeof keys / sizeof *keys) && keys[k] != a; k++) ;   \
        T0;                                                           \
        r = __real_##fn(a, b);                                        \
        psp_prof_add(slot + k, k < (int)(sizeof keys / sizeof *keys) ? lab[k] : lab[4], T1);   \
        return r;                                                     \
    }

/* func_001529A0's steps */
W(64, func_0016D130, "  scene: func_0016D130 mode")
WA(65, func_00153710, "  scene: func_00153710 layer ", 0xE, 0x12)
W(70, func_00153590, "  scene: func_00153590")
W(71, func_001536C0, "  scene: func_001536C0")
W(72, func_00152AD0, "  scene: func_00152AD0 render active objects")
WA(73, func_001534D0, "  scene: func_001534D0 layer ", 0xB)
W(78, func_00153390, "  scene: func_00153390")
WA(79, func_00153350, "  scene: func_00153350 layer ", 5, 7)
W(84, func_00153250, "  scene: func_00153250")
W(85, func_00153310, "  scene: func_00153310")
W(86, func_003ED1C0, "  scene: func_003ED1C0")
W(87, func_00153A40, "  scene: func_00153A40")
W(88, func_0016DF90, "  scene: func_0016DF90")
/* func_00152570's steps */
W(90, func_00123090, "  select: func_00123090")
WA(91, func_00151A20, "  select: func_00151A20 list ", 13, 6, 8, 1)
WA(96, func_001520E0, "  select: func_001520E0 list ", 13, 6, 8, 1)
W(101, func_00122FE0, "  select: func_00122FE0")
W(102, func_00123060, "  select: func_00123060")
W(103, func_0016E080, "  select: func_0016E080")
