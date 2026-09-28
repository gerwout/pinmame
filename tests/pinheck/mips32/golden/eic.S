#include "common.h"

#define LOG(r) la $24, logptr; lw $25, 0($24); sw r, 0($25); addiu $25, $25, 4; sw $25, 0($24)

/* PRA = MD00090, ISA = MD00086; cited by section title. */
/* PRA "Exception Vector Locations": interrupt vector = EBase + 0x200 + vector * (IntCtl.VS << 5) when Cause.IV = 1. */
	.section .exc, "ax"
	li $2, SYS_EXIT
	li $4, 1
	syscall
	.org 0x80
	j isr0
	nop
	.org 0xe0
	j isr3
	nop

	.text
/* PRA "GPR Shadow Registers" and "SRSCtl Register": EIC interrupt entry sets PSS = CSS, CSS = EICSS. */
isr3:
	la $26, logptr
	lw $27, 0($26)
	sw $8, 0($27)
	mfc0 $8, $13
	sw $8, 4($27)
	mfc0 $8, $12, 2
	sw $8, 8($27)
	RDPGPR(8, 9)
	sw $8, 12($27)
	addiu $27, $27, 16
	sw $27, 0($26)
	li $8, MMIO_EIC
	sw $0, 0($8)
	eret

/* PRA "Compare Register": writing Compare clears Cause.TI. */
isr0:
	mfc0 $26, $13
	la $27, isr0cause
	sw $26, 0($27)
	mtc0 $0, $11
	li $27, MMIO_CT
	sw $0, 0($27)
	eret

	.globl __start
__start:
	mtc0 $0, $12
	ehb
	li $8, 0x20
	mtc0 $8, $12, 1
	li $8, 0x00800000
	mtc0 $8, $13
	li $8, 0x40
	mtc0 $8, $12, 2
	ehb
	li $9, 0x1111
	WRPGPR(8, 9)
	mtc0 $0, $12, 2
	ehb
	li $9, 0x2222
	la $8, logptr
	la $10, out
	sw $10, 0($8)
/* PRA "External Interrupt Controller Mode": taken only when RIPL > Status.IPL. */
	li $8, 0x1401
	mtc0 $8, $12
	ehb
	li $10, MMIO_EIC
	li $11, 0x10303
	sw $11, 0($10)
	nop
	nop
	li $12, 0xaaaa0001
	LOG($12)
/* PRA "Cause Register": RIPL latched at entry; IV set. */
	li $8, 0x0801
	mtc0 $8, $12
	ehb
	nop
	li $12, 0xaaaa0002
	LOG($12)
/* ISA "ERET": restores SRSCtl.CSS from PSS. */
	mfc0 $12, $12, 2
	LOG($12)
	mtc0 $0, $12
	mtc0 $0, $9
	li $8, 20
	mtc0 $8, $11
	li $8, 1
	mtc0 $8, $12
/* ISA "WAIT" and PRA "Count Register"/"Compare Register": Count reaching Compare sets Cause.TI and wakes WAIT. */
	wait
	li $12, 0xaaaa0003
	LOG($12)
	la $8, isr0cause
	lw $12, 0($8)
	LOG($12)
	mtc0 $0, $12
	WRITE_EXIT(out, 36)

	.data
	.align 4
logptr:	.word 0
isr0cause: .word 0
out:	.space 256
