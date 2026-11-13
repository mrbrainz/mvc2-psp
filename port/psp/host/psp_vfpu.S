/* P88: VFPU kernels for r_gu_mesh.c (main thread has THREAD_ATTR_VFPU).
 * Assembled by psp-gcc; plain pointer/int args in a0-a3, so the o32 callers
 * and this code agree. Matrices are row-major float[16], row vectors:
 * out = a * b, k = (x, y, z, 1) * m. Scalar loads/stores: no alignment rule.
 * Clobbers only t-registers and VFPU matrices 0 and 1. */
    .set noreorder
    .set noat
    .text

/* void vfpu_mat_mul(float out[16], const float a[16], const float b[16]) */
    .globl vfpu_mat_mul
    .ent vfpu_mat_mul
vfpu_mat_mul:
    lv.s    S000, 0($a2)
    lv.s    S010, 4($a2)
    lv.s    S020, 8($a2)
    lv.s    S030, 12($a2)
    lv.s    S001, 16($a2)
    lv.s    S011, 20($a2)
    lv.s    S021, 24($a2)
    lv.s    S031, 28($a2)
    lv.s    S002, 32($a2)
    lv.s    S012, 36($a2)
    lv.s    S022, 40($a2)
    lv.s    S032, 44($a2)
    lv.s    S003, 48($a2)
    lv.s    S013, 52($a2)
    lv.s    S023, 56($a2)
    lv.s    S033, 60($a2)
    lv.s    S100, 0($a1)
    lv.s    S101, 4($a1)
    lv.s    S102, 8($a1)
    lv.s    S103, 12($a1)
    vtfm4.q C110, M000, C100
    sv.s    S110, 0($a0)
    sv.s    S111, 4($a0)
    sv.s    S112, 8($a0)
    sv.s    S113, 12($a0)
    lv.s    S100, 16($a1)
    lv.s    S101, 20($a1)
    lv.s    S102, 24($a1)
    lv.s    S103, 28($a1)
    vtfm4.q C110, M000, C100
    sv.s    S110, 16($a0)
    sv.s    S111, 20($a0)
    sv.s    S112, 24($a0)
    sv.s    S113, 28($a0)
    lv.s    S100, 32($a1)
    lv.s    S101, 36($a1)
    lv.s    S102, 40($a1)
    lv.s    S103, 44($a1)
    vtfm4.q C110, M000, C100
    sv.s    S110, 32($a0)
    sv.s    S111, 36($a0)
    sv.s    S112, 40($a0)
    sv.s    S113, 44($a0)
    lv.s    S100, 48($a1)
    lv.s    S101, 52($a1)
    lv.s    S102, 56($a1)
    lv.s    S103, 60($a1)
    vtfm4.q C110, M000, C100
    sv.s    S110, 48($a0)
    sv.s    S111, 52($a0)
    sv.s    S112, 56($a0)
    sv.s    S113, 60($a0)
    jr      $ra
    nop
    .end vfpu_mat_mul

/* void vfpu_xform_mvtx(const float m[16], const void *v, int n, float *out4)
 * out4[4i..4i+3] = (pos_i, 1) * m; pos at +0, stride 48 */
    .globl vfpu_xform_mvtx
    .ent vfpu_xform_mvtx
vfpu_xform_mvtx:
    lv.s    S000, 0($a0)
    lv.s    S010, 4($a0)
    lv.s    S020, 8($a0)
    lv.s    S030, 12($a0)
    lv.s    S001, 16($a0)
    lv.s    S011, 20($a0)
    lv.s    S021, 24($a0)
    lv.s    S031, 28($a0)
    lv.s    S002, 32($a0)
    lv.s    S012, 36($a0)
    lv.s    S022, 40($a0)
    lv.s    S032, 44($a0)
    lv.s    S003, 48($a0)
    lv.s    S013, 52($a0)
    lv.s    S023, 56($a0)
    lv.s    S033, 60($a0)
    vone.s  S103
    beqz    $a2, 2f
    nop
1:  lv.s    S100, 0($a1)
    lv.s    S101, 4($a1)
    lv.s    S102, 8($a1)
    vtfm4.q C110, M000, C100
    addiu   $a1, $a1, 48
    addiu   $a2, $a2, -1
    sv.s    S110, 0($a3)
    sv.s    S111, 4($a3)
    sv.s    S112, 8($a3)
    sv.s    S113, 12($a3)
    bnez    $a2, 1b
    addiu   $a3, $a3, 16
2:  jr      $ra
    nop
    .end vfpu_xform_mvtx

/* void vfpu_xform_gvtx(const float m[16], const void *v, int n, float *out4)
 * out4[4i..4i+3] = (pos_i, 1) * m; pos at +12, stride 24 */
    .globl vfpu_xform_gvtx
    .ent vfpu_xform_gvtx
vfpu_xform_gvtx:
    lv.s    S000, 0($a0)
    lv.s    S010, 4($a0)
    lv.s    S020, 8($a0)
    lv.s    S030, 12($a0)
    lv.s    S001, 16($a0)
    lv.s    S011, 20($a0)
    lv.s    S021, 24($a0)
    lv.s    S031, 28($a0)
    lv.s    S002, 32($a0)
    lv.s    S012, 36($a0)
    lv.s    S022, 40($a0)
    lv.s    S032, 44($a0)
    lv.s    S003, 48($a0)
    lv.s    S013, 52($a0)
    lv.s    S023, 56($a0)
    lv.s    S033, 60($a0)
    vone.s  S103
    beqz    $a2, 2f
    nop
1:  lv.s    S100, 12($a1)
    lv.s    S101, 16($a1)
    lv.s    S102, 20($a1)
    vtfm4.q C110, M000, C100
    addiu   $a1, $a1, 24
    addiu   $a2, $a2, -1
    sv.s    S110, 0($a3)
    sv.s    S111, 4($a3)
    sv.s    S112, 8($a3)
    sv.s    S113, 12($a3)
    bnez    $a2, 1b
    addiu   $a3, $a3, 16
2:  jr      $ra
    nop
    .end vfpu_xform_gvtx

