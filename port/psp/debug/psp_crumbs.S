/* Debug: [crumb] log line on entry to each listed function (MVC2_PSP_CRUMBS).
 * Saves a0-a3, ra, f12, f14, logs, restores, tail-jumps to __real_<name>,
 * so stack arguments pass through.  Function list: CRUMB lines below. */
    .set noreorder
    .text
.macro CRUMB name
    .globl __wrap_\name
    .type __wrap_\name, @function
__wrap_\name:
    addiu $sp, $sp, -48
    sw $a0, 16($sp)
    sw $a1, 20($sp)
    sw $a2, 24($sp)
    sw $a3, 28($sp)
    sw $ra, 32($sp)
    swc1 $f12, 36($sp)
    swc1 $f14, 40($sp)
    lui $a0, %hi(.Lname_\name)
    jal psp_crumb
    addiu $a0, $a0, %lo(.Lname_\name)
    lw $a0, 16($sp)
    lw $a1, 20($sp)
    lw $a2, 24($sp)
    lw $a3, 28($sp)
    lw $ra, 32($sp)
    lwc1 $f12, 36($sp)
    lwc1 $f14, 40($sp)
    j __real_\name
    addiu $sp, $sp, 48
    .section .rodata
.Lname_\name: .asciz "\name"
    .text
.endm
    CRUMB func_003DCD40
    CRUMB func_0011CB70
    CRUMB func_003F12C0
    CRUMB func_003F0940
    CRUMB func_003DCFC0
    CRUMB func_003DB980
    CRUMB func_003DD070
    CRUMB func_00106628
    CRUMB func_0010AC48
    CRUMB func_0010F3C8
    CRUMB func_0010F8A0
    CRUMB func_0010C098
    CRUMB func_0010C060
    CRUMB func_00107E58
    CRUMB func_003DCAD0
    CRUMB func_00418C70
    CRUMB func_00419EE0
    CRUMB func_00418B50
    CRUMB func_00100478
    CRUMB func_001025A8
    CRUMB func_00102580
    CRUMB func_00103958
    CRUMB func_003DC250
    CRUMB func_001002D8
    CRUMB func_001020E0
    CRUMB func_003ED0D0
    CRUMB func_003EB900
    CRUMB func_00418CC0
    CRUMB func_00419F70
    CRUMB func_00418B80
    CRUMB func_003DAD60
    CRUMB func_003ED000
    CRUMB func_00100E68
    CRUMB func_00101D50
    CRUMB func_003DF7D0
    CRUMB func_00100468
    CRUMB func_00100590
    CRUMB func_001004E0
    CRUMB func_00107E90
    CRUMB func_001084D0
    CRUMB func_00108118
    CRUMB func_00119DA0
    CRUMB func_00100580
    CRUMB func_00100A90
    CRUMB func_00101E78
    CRUMB func_00100C78
    CRUMB func_001009C8
