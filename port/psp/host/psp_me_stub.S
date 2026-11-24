/* P114 spike: Media Engine reset stub, copied to the ME reset vector
 * (0xBFC00000) by psp_me.c.  Runs uncached on the ME: bus clocks on, NMI
 * flags cleared, Status = CU0|CU1 (FPU, interrupts off), caches dropped,
 * then jumps to the entry, stack and argument psp_me.c left at 0xBFC00600. */
	.set noreorder
	.set noat
	.text
	.globl psp_me_stub
	.globl psp_me_stub_end
psp_me_stub:
	lui	$k0, 0xBC10
	lw	$k1, 0x50($k0)		/* bus clock enable: ME bits on */
	ori	$k1, $k1, 0x0F
	sw	$k1, 0x50($k0)
	lw	$k1, 0x40($k0)		/* Tachyon config: bit 1 */
	ori	$k1, $k1, 0x02
	sw	$k1, 0x40($k0)
	li	$k1, -1
	sw	$k1, 0x04($k0)		/* NMI flags */
	sync
	li	$k0, 0x30000000
	mtc0	$k0, $12		/* Status */
	mtc0	$zero, $13		/* Cause */
	li	$k0, 0
	li	$k1, 8192
1:	cache	0x04, 0($k0)		/* I: index invalidate, both ways */
	cache	0x04, 0($k0)
	cache	0x14, 0($k0)		/* D: index writeback-invalidate, both ways */
	cache	0x14, 0($k0)
	addiu	$k0, $k0, 64
	bne	$k0, $k1, 1b
	nop
	sync
	lui	$t0, 0xBFC0
	lw	$t1, 0x600($t0)		/* entry (kseg0) */
	lw	$sp, 0x604($t0)		/* stack top (kseg0) */
	lw	$a0, 0x608($t0)		/* argument */
	jr	$t1
	nop
psp_me_stub_end:
