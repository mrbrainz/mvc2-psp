/* P88: VFPU kernels for r_gu_mesh.c (main thread has THREAD_ATTR_VFPU).
 * Assembled by psp-gcc; plain pointer/int args in a0-a3, so the o32 callers
 * and this code agree. Matrices are row-major float[16], row vectors:
 * out = a * b, k = (x, y, z, 1) * m. Scalar loads/stores: no alignment rule.
 * Clobbers t-registers and any VFPU matrix (nothing else uses the VFPU). */
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

/* int vfpu_bbox_inside(const float m[16], const float bb[6])  (P83)
 * 1 when every corner k = (c, 1) * m of the box bb (min xyz, max xyz) has
 * k.w - s > 0 and |k.x|, |k.y|, |k.z| + s < k.w - s, s = 1e-5 * smax, where
 * smax = max over columns of (max(|min|, |max|), 1) * |m| (>= the per-corner
 * bound bbox_inside's C path uses); 0 when smax >= 1e30 or m is not finite.
 * Corners are P + sums of the edge vectors (rounding far below s).  Corners
 * go in the columns of M500/M600, so rows R5x3/R6x3 are four corners' w.
 * Registers only: no stores, so no CPU/VFPU memory round trips. */
    .globl vfpu_bbox_inside
    .ent vfpu_bbox_inside
vfpu_bbox_inside:
    LOADM   $a0
    lv.s    S300, 0($a1)
    lv.s    S301, 4($a1)
    lv.s    S302, 8($a1)
    lv.s    S310, 12($a1)
    lv.s    S311, 16($a1)
    lv.s    S312, 20($a1)
    vone.s  S303
    vabs.q  C200, C000
    vabs.q  C210, C010
    vabs.q  C220, C020
    vabs.q  C230, C030
    vtfm4.q C400, M000, C300            /* P = (min, 1) * m */
    vsub.t  C320, C310, C300            /* d = max - min */
    vzero.q C330
    vmov.s  S330, S320
    vtfm4.q C410, M000, C330            /* D0 = d.x * row 0 */
    vzero.q C330
    vmov.s  S331, S321
    vtfm4.q C420, M000, C330
    vzero.q C330
    vmov.s  S332, S322
    vtfm4.q C430, M000, C330
    vmov.q  C500, C400
    vadd.q  C510, C400, C410
    vadd.q  C520, C400, C420
    vadd.q  C530, C510, C420
    vadd.q  C600, C500, C430
    vadd.q  C610, C510, C430
    vadd.q  C620, C520, C430
    vadd.q  C630, C530, C430
    vabs.t  C100, C300                  /* smax */
    vabs.t  C110, C310
    vmax.t  C100, C100, C110
    vone.s  S103
    vtfm4.q C110, M200, C100
    vmax.s  S700, S110, S111
    vmax.s  S701, S112, S113
    vmax.s  S700, S700, S701
    li      $t0, 0x3727C5AC             /* 1e-5f */
    mtv     $t0, S701
    vmul.s  S701, S700, S701            /* s */
    vone.q  C130
    vscl.q  C130, C130, S701            /* s x4 */
    vadd.q  C110, C130, C130            /* 2s x4 */
    vsub.q  C120, R503, C110            /* w - 2s */
    vsub.q  C100, R503, C130            /* w - s */
    vabs.q  R500, R500
    vabs.q  R501, R501
    vabs.q  R502, R502
    vsub.q  R500, C120, R500
    vsub.q  R501, C120, R501
    vsub.q  R502, C120, R502
    vmin.q  C100, C100, R500
    vmin.q  C100, C100, R501
    vmin.q  C100, C100, R502
    vsub.q  C120, R603, C110
    vsub.q  R603, R603, C130
    vmin.q  C100, C100, R603
    vabs.q  R600, R600
    vabs.q  R601, R601
    vabs.q  R602, R602
    vsub.q  R600, C120, R600
    vsub.q  R601, C120, R601
    vsub.q  R602, C120, R602
    vmin.q  C100, C100, R600
    vmin.q  C100, C100, R601
    vmin.q  C100, C100, R602
    vmin.s  S100, S100, S101
    vmin.s  S102, S102, S103
    vmin.s  S100, S100, S102
    vadd.q  C200, C000, C010            /* finite check of m */
    vadd.q  C210, C020, C030
    vadd.q  C200, C200, C210
    vfad.q  S710, C200
    mfv     $t0, S100
    mfv     $t1, S700
    mfv     $t2, S710
    li      $t3, 0x7F800000
    slt     $v0, $zero, $t0             /* min > 0 (and not NaN/inf below) */
    slt     $t4, $t0, $t3
    and     $v0, $v0, $t4
    li      $t5, 0x7149F2CA             /* 1e30f */
    sltu    $t4, $t1, $t5
    and     $v0, $v0, $t4
    and     $t6, $t2, $t3
    xor     $t6, $t6, $t3
    sltu    $t4, $zero, $t6
    jr      $ra
    and     $v0, $v0, $t4
    .end vfpu_bbox_inside

/* int vfpu_clipbox_inside(const float m[16], const float bb[6])  (P110)
 * clip_bbox_inside in VFPU registers: 1 when every corner k = (c, 1) * m of
 * the box bb has k.w - s >= 1 and |k.x|, |k.y| < 4 k.w - s, s = 5e-5 * smax
 * (smax as vfpu_bbox_inside: >= the C path's per-corner bound); 0 when
 * smax >= 1e30 or m is not finite.  Corners as vfpu_bbox_inside. */
    .globl vfpu_clipbox_inside
    .ent vfpu_clipbox_inside
vfpu_clipbox_inside:
    LOADM   $a0
    lv.s    S300, 0($a1)
    lv.s    S301, 4($a1)
    lv.s    S302, 8($a1)
    lv.s    S310, 12($a1)
    lv.s    S311, 16($a1)
    lv.s    S312, 20($a1)
    vone.s  S303
    vabs.q  C200, C000
    vabs.q  C210, C010
    vabs.q  C220, C020
    vabs.q  C230, C030
    vtfm4.q C400, M000, C300
    vsub.t  C320, C310, C300
    vzero.q C330
    vmov.s  S330, S320
    vtfm4.q C410, M000, C330
    vzero.q C330
    vmov.s  S331, S321
    vtfm4.q C420, M000, C330
    vzero.q C330
    vmov.s  S332, S322
    vtfm4.q C430, M000, C330
    vmov.q  C500, C400
    vadd.q  C510, C400, C410
    vadd.q  C520, C400, C420
    vadd.q  C530, C510, C420
    vadd.q  C600, C500, C430
    vadd.q  C610, C510, C430
    vadd.q  C620, C520, C430
    vadd.q  C630, C530, C430
    vabs.t  C100, C300                  /* smax */
    vabs.t  C110, C310
    vmax.t  C100, C100, C110
    vone.s  S103
    vtfm4.q C110, M200, C100
    vmax.s  S700, S110, S111
    vmax.s  S701, S112, S113
    vmax.s  S700, S700, S701
    li      $t0, 0x3851B717             /* 5e-5f = 1e-5 * (GUARD + 1) */
    mtv     $t0, S701
    vmul.s  S701, S700, S701            /* s */
    vfim.s  S702, 4.0
    vone.q  C130
    vscl.q  C130, C130, S701            /* s x4 */
    vscl.q  C120, R503, S702            /* gw = 4w - s */
    vsub.q  C120, C120, C130
    vabs.q  R500, R500
    vabs.q  R501, R501
    vsub.q  R500, C120, R500
    vsub.q  R501, C120, R501
    vmin.q  C100, R500, R501            /* A: gw - |x|, gw - |y| */
    vsub.q  C110, R503, C130            /* B: w - s */
    vscl.q  C120, R603, S702
    vsub.q  C120, C120, C130
    vabs.q  R600, R600
    vabs.q  R601, R601
    vsub.q  R600, C120, R600
    vsub.q  R601, C120, R601
    vmin.q  C100, C100, R600
    vmin.q  C100, C100, R601
    vsub.q  R603, R603, C130
    vmin.q  C110, C110, R603
    vmin.s  S100, S100, S101
    vmin.s  S102, S102, S103
    vmin.s  S100, S100, S102
    vmin.s  S110, S110, S111
    vmin.s  S112, S112, S113
    vmin.s  S110, S110, S112
    vone.s  S111
    vsub.s  S110, S110, S111            /* min(w - s) - 1, must be >= 0 */
    vadd.q  C200, C000, C010            /* finite check of m */
    vadd.q  C210, C020, C030
    vadd.q  C200, C200, C210
    vfad.q  S710, C200
    mfv     $t0, S100
    mfv     $t7, S110
    mfv     $t1, S700
    mfv     $t2, S710
    li      $t3, 0x7F800000
    slt     $v0, $zero, $t0             /* A > 0, finite */
    slt     $t4, $t0, $t3
    and     $v0, $v0, $t4
    slt     $t4, $t7, $t3               /* B - 1 >= 0 (signed: sign bit clear), finite */
    and     $v0, $v0, $t4
    slt     $t4, $t7, $zero
    xori    $t4, $t4, 1
    and     $v0, $v0, $t4
    li      $t5, 0x7149F2CA             /* 1e30f */
    sltu    $t4, $t1, $t5
    and     $v0, $v0, $t4
    and     $t6, $t2, $t3
    xor     $t6, $t6, $t3
    sltu    $t4, $zero, $t6
    jr      $ra
    and     $v0, $v0, $t4
    .end vfpu_clipbox_inside

/* int vfpu_clip_any(const float m[16], const void *gvtx, int n)  (P110)
 * 0 when no vertex k = (pos_i, 1) * m (pos at +12, stride 24) would get a
 * vfpu_clip_codes outcode (sign of w - 1, 4w - x, 4w + x, 4w - y, 4w + y)
 * and the sum of every k is finite; 1 otherwise (maybe: run the full codes).
 * Registers only, no stores. */
    .globl vfpu_clip_any
    .ent vfpu_clip_any
vfpu_clip_any:
    LOADM   $a0
    vone.s  S103
    vone.s  S131
    vfim.s  S130, 4.0
    vzero.q C200                        /* sum of k */
    li      $t0, 0x7F7FFFFF             /* FLT_MAX */
    mtv     $t0, S701
    vone.q  C210
    vscl.q  C210, C210, S701            /* min accumulators */
    vmov.p  C220, C210
    li      $v0, 0
    beqz    $a2, 2f
    nop
1:  lv.s    S100, 12($a1)
    lv.s    S101, 16($a1)
    lv.s    S102, 20($a1)
    vtfm4.q C110, M000, C100
    addiu   $a1, $a1, 24
    addiu   $a2, $a2, -1
    vadd.q  C200, C200, C110
    vmul.s  S120, S113, S130            /* gw = 4w, as vfpu_clip_codes */
    vsub.s  S121, S113, S131
    vsub.s  S122, S120, S110
    vadd.s  S123, S120, S110
    vsub.s  S300, S120, S111
    vadd.s  S301, S120, S111
    vmin.q  C210, C210, C120
    bnez    $a2, 1b
    vmin.p  C220, C220, C300
    vmin.s  S210, S210, S211
    vmin.s  S212, S212, S213
    vmin.s  S210, S210, S212
    vmin.s  S220, S220, S221
    vmin.s  S210, S210, S220
    vfad.q  S710, C200
    mfv     $t0, S210
    mfv     $t2, S710
    li      $t3, 0x7F800000
    srl     $v0, $t0, 31                /* a negative (or -0) difference */
    and     $t6, $t2, $t3
    xor     $t6, $t6, $t3
    sltiu   $t4, $t6, 1                 /* sum not finite */
    or      $v0, $v0, $t4
2:  jr      $ra
    nop
    .end vfpu_clip_any

/* void vfpu_mesh_mats(const float *p[7])  (P110)
 * p = { a, b, c, t, cl, clip_out, cm_out }: wv = a * b, clip_out =
 * (wv * c) * t, and cm_out = wv * cl when cm_out is not NULL.  The same
 * vtfm4 rows as chained vfpu_mat_mul calls (bit-identical); wv and wv * c
 * stay in registers (M400, M500). */
.macro ROWS4 src, base0, base1, base2, base3
    lv.s    S100, 0(\src)
    lv.s    S101, 4(\src)
    lv.s    S102, 8(\src)
    lv.s    S103, 12(\src)
    vtfm4.q \base0, M000, C100
    lv.s    S100, 16(\src)
    lv.s    S101, 20(\src)
    lv.s    S102, 24(\src)
    lv.s    S103, 28(\src)
    vtfm4.q \base1, M000, C100
    lv.s    S100, 32(\src)
    lv.s    S101, 36(\src)
    lv.s    S102, 40(\src)
    lv.s    S103, 44(\src)
    vtfm4.q \base2, M000, C100
    lv.s    S100, 48(\src)
    lv.s    S101, 52(\src)
    lv.s    S102, 56(\src)
    lv.s    S103, 60(\src)
    vtfm4.q \base3, M000, C100
.endm
.macro STROW c0, c1, c2, c3, dst, off
    sv.s    \c0, \off(\dst)
    sv.s    \c1, \off+4(\dst)
    sv.s    \c2, \off+8(\dst)
    sv.s    \c3, \off+12(\dst)
.endm
    .globl vfpu_mesh_mats
    .ent vfpu_mesh_mats
vfpu_mesh_mats:
    lw      $t0, 0($a0)                 /* a */
    lw      $t1, 4($a0)                 /* b */
    LOADM   $t1
    ROWS4   $t0, C400, C410, C420, C430 /* wv rows */
    lw      $t1, 8($a0)                 /* c */
    LOADM   $t1
    vtfm4.q C500, M000, C400            /* mvp rows */
    vtfm4.q C510, M000, C410
    vtfm4.q C520, M000, C420
    vtfm4.q C530, M000, C430
    lw      $t1, 12($a0)                /* t */
    lw      $t2, 20($a0)                /* clip_out */
    LOADM   $t1
    vtfm4.q C110, M000, C500
    vtfm4.q C120, M000, C510
    vtfm4.q C130, M000, C520
    vtfm4.q C100, M000, C530
    STROW   S110, S111, S112, S113, $t2, 0
    STROW   S120, S121, S122, S123, $t2, 16
    STROW   S130, S131, S132, S133, $t2, 32
    STROW   S100, S101, S102, S103, $t2, 48
    lw      $t3, 24($a0)                /* cm_out */
    beqz    $t3, 1f
    lw      $t1, 16($a0)                /* cl */
    LOADM   $t1
    vtfm4.q C110, M000, C400
    vtfm4.q C120, M000, C410
    vtfm4.q C130, M000, C420
    vtfm4.q C100, M000, C430
    STROW   S110, S111, S112, S113, $t3, 0
    STROW   S120, S121, S122, S123, $t3, 16
    STROW   S130, S131, S132, S133, $t3, 32
    STROW   S100, S101, S102, S103, $t3, 48
1:  jr      $ra
    nop
    .end vfpu_mesh_mats
