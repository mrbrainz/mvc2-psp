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


/* P93: load row-major m into M000 for vtfm4.q C110, M000, C100 */
.macro LOADM r
    lv.s    S000, 0(\r)
    lv.s    S010, 4(\r)
    lv.s    S020, 8(\r)
    lv.s    S030, 12(\r)
    lv.s    S001, 16(\r)
    lv.s    S011, 20(\r)
    lv.s    S021, 24(\r)
    lv.s    S031, 28(\r)
    lv.s    S002, 32(\r)
    lv.s    S012, 36(\r)
    lv.s    S022, 40(\r)
    lv.s    S032, 44(\r)
    lv.s    S003, 48(\r)
    lv.s    S013, 52(\r)
    lv.s    S023, 56(\r)
    lv.s    S033, 60(\r)
.endm

/* int vfpu_adc_codes(const float m[16], const void *mvtx, int n, uint8_t *out)
 * k = (pos_i, 1) * m (pos at +0, stride 48);
 * out[i] = max(|kx|,|ky|,|kz|) > |kw|; returns the OR of out[] */
    .globl vfpu_adc_codes
    .ent vfpu_adc_codes
vfpu_adc_codes:
    LOADM   $a0
    vone.s  S103
    move    $v0, $zero
    beqz    $a2, 2f
    nop
1:  lv.s    S100, 0($a1)
    lv.s    S101, 4($a1)
    lv.s    S102, 8($a1)
    vtfm4.q C110, M000, C100
    addiu   $a1, $a1, 48
    vabs.q  C120, C110
    vmax.s  S130, S120, S121
    vmax.s  S130, S130, S122
    vsub.s  S131, S123, S130
    addiu   $a2, $a2, -1
    mfv     $t0, S131
    srl     $t0, $t0, 31
    sb      $t0, 0($a3)
    or      $v0, $v0, $t0
    bnez    $a2, 1b
    addiu   $a3, $a3, 1
2:  jr      $ra
    nop
    .end vfpu_adc_codes

/* int vfpu_clip_codes(const float m[16], const void *gvtx, int n, void *out[2])
 * k = (pos_i, 1) * m (pos at +12, stride 24); out[0] (uint8_t) gets the
 * outcode (w < 1 | x > 4w << 1 | -x > 4w << 2 | y > 4w << 3 | -y > 4w << 4),
 * out[1] (float) gets x, y, w; returns the OR of the outcodes */
    .globl vfpu_clip_codes
    .ent vfpu_clip_codes
vfpu_clip_codes:
    LOADM   $a0
    lw      $t8, 0($a3)
    lw      $t9, 4($a3)
    vone.s  S103
    vone.s  S131
    vfim.s  S130, 4.0
    move    $v0, $zero
    beqz    $a2, 2f
    nop
1:  lv.s    S100, 12($a1)
    lv.s    S101, 16($a1)
    lv.s    S102, 20($a1)
    vtfm4.q C110, M000, C100
    addiu   $a1, $a1, 24
    vmul.s  S120, S113, S130
    vsub.s  S121, S113, S131
    vsub.s  S122, S120, S110
    vadd.s  S123, S120, S110
    vsub.s  S132, S120, S111
    vadd.s  S133, S120, S111
    sv.s    S110, 0($t9)
    sv.s    S111, 4($t9)
    sv.s    S113, 8($t9)
    mfv     $t0, S121
    mfv     $t1, S122
    mfv     $t2, S123
    mfv     $t3, S132
    mfv     $t4, S133
    srl     $t0, $t0, 31
    srl     $t1, $t1, 31
    srl     $t2, $t2, 31
    srl     $t3, $t3, 31
    srl     $t4, $t4, 31
    ins     $t0, $t1, 1, 1
    ins     $t0, $t2, 2, 1
    ins     $t0, $t3, 3, 1
    ins     $t0, $t4, 4, 1
    sb      $t0, 0($t8)
    or      $v0, $v0, $t0
    addiu   $t8, $t8, 1
    addiu   $a2, $a2, -1
    bnez    $a2, 1b
    addiu   $t9, $t9, 12
2:  jr      $ra
    nop
    .end vfpu_clip_codes

/* void vfpu_lit(const float p[26], const void *mvtx, const uint16_t *list, float *out3)
 * p: ldir[3][3], lc[3][3], la[3], cscale, modv[3], (int) n.
 * For i = list[q]: d_j = max(0, nrm_i . ldir_j);
 * out3[3q + c] = floor(min(sum_j lc[j][c] d_j + la[c], cscale) * modv[c]) */
    .globl vfpu_lit
    .ent vfpu_lit
vfpu_lit:
    lv.s    S000, 0($a0)
    lv.s    S001, 4($a0)
    lv.s    S002, 8($a0)
    lv.s    S010, 12($a0)
    lv.s    S011, 16($a0)
    lv.s    S012, 20($a0)
    lv.s    S020, 24($a0)
    lv.s    S021, 28($a0)
    lv.s    S022, 32($a0)
    lv.s    S200, 36($a0)
    lv.s    S210, 40($a0)
    lv.s    S220, 44($a0)
    lv.s    S201, 48($a0)
    lv.s    S211, 52($a0)
    lv.s    S221, 56($a0)
    lv.s    S202, 60($a0)
    lv.s    S212, 64($a0)
    lv.s    S222, 68($a0)
    lv.s    S300, 72($a0)
    lv.s    S301, 76($a0)
    lv.s    S302, 80($a0)
    lv.s    S310, 84($a0)
    lv.s    S320, 88($a0)
    lv.s    S321, 92($a0)
    lv.s    S322, 96($a0)
    lw      $t9, 100($a0)
    vmov.t  C310, C310[x, x, x]
    vzero.t C330
    beqz    $t9, 2f
    nop
1:  lhu     $t0, 0($a2)
    sll     $t1, $t0, 5
    sll     $t0, $t0, 4
    addu    $t0, $t0, $t1
    addu    $t0, $t0, $a1
    lv.s    S100, 12($t0)
    lv.s    S101, 16($t0)
    lv.s    S102, 20($t0)
    vtfm3.t C110, M000, C100
    vmax.t  C110, C110, C330
    vtfm3.t C120, M200, C110
    vadd.t  C120, C120, C300
    vmin.t  C120, C120, C310
    vmul.t  C120, C120, C320
    vf2id.t C120, C120, 0
    vi2f.t  C120, C120, 0
    sv.s    S120, 0($a3)
    sv.s    S121, 4($a3)
    sv.s    S122, 8($a3)
    addiu   $a2, $a2, 2
    addiu   $t9, $t9, -1
    bnez    $t9, 1b
    addiu   $a3, $a3, 12
2:  jr      $ra
    nop
    .end vfpu_lit

/* void vfpu_bbox(const void *mvtx, int n, float out[6])  (n >= 1)
 * out = min xyz, max xyz of the positions (+0, stride 48) */
    .globl vfpu_bbox
    .ent vfpu_bbox
vfpu_bbox:
    lv.s    S100, 0($a0)
    lv.s    S101, 4($a0)
    lv.s    S102, 8($a0)
    vmov.t  C110, C100
    vmov.t  C120, C100
    addiu   $a1, $a1, -1
    beqz    $a1, 2f
    addiu   $a0, $a0, 48
1:  lv.s    S100, 0($a0)
    lv.s    S101, 4($a0)
    lv.s    S102, 8($a0)
    addiu   $a1, $a1, -1
    vmin.t  C110, C110, C100
    vmax.t  C120, C120, C100
    bnez    $a1, 1b
    addiu   $a0, $a0, 48
2:  sv.s    S110, 0($a2)
    sv.s    S111, 4($a2)
    sv.s    S112, 8($a2)
    sv.s    S120, 12($a2)
    sv.s    S121, 16($a2)
    sv.s    S122, 20($a2)
    jr      $ra
    nop
    .end vfpu_bbox

/* void vfpu_bbox_corners(const float m[16], float k[32], float *smax)
 * k holds 8 corners (xyz at k[4c]); k[4c..4c+3] = (corner, 1) * m;
 * *smax = max over corners and columns of (|corner|, 1) * |m| */
    .globl vfpu_bbox_corners
    .ent vfpu_bbox_corners
vfpu_bbox_corners:
    LOADM   $a0
    vabs.q  C200, C000
    vabs.q  C210, C010
    vabs.q  C220, C020
    vabs.q  C230, C030
    vone.s  S103
    vone.s  S123
    vzero.s S300
    li      $t0, 8
1:  lv.s    S100, 0($a1)
    lv.s    S101, 4($a1)
    lv.s    S102, 8($a1)
    vtfm4.q C110, M000, C100
    vabs.t  C120, C100
    vtfm4.q C130, M200, C120
    vmax.s  S300, S300, S130
    vmax.s  S300, S300, S131
    vmax.s  S300, S300, S132
    vmax.s  S300, S300, S133
    sv.s    S110, 0($a1)
    sv.s    S111, 4($a1)
    sv.s    S112, 8($a1)
    sv.s    S113, 12($a1)
    addiu   $t0, $t0, -1
    bnez    $t0, 1b
    addiu   $a1, $a1, 16
    sv.s    S300, 0($a2)
    jr      $ra
    nop
    .end vfpu_bbox_corners
