#include "common.h"

#define ARM(res, trig) \
	la $8, res; la $9, resume; sw $8, 0($9); la $8, trig; la $9, trigger; sw $8, 0($9)

/* PRA = MD00090 (MIPS32 Privileged Resource Architecture), ISA = MD00086 (MIPS32 Instruction Set); cited by section title. */
/* PRA "Exception Vector Locations": general exceptions at EBase + 0x180 when BEV=0. */
	.section .exc, "ax"
	mfc0 $26, $13
	sw $26, 0($22)
	mfc0 $26, $14
	la $27, trigger
	lw $27, 0($27)
	subu $26, $26, $27
	sw $26, 4($22)
	mfc0 $26, $8
	sw $26, 8($22)
	addiu $22, $22, 12
	la $27, resume
	lw $27, 0($27)
	mtc0 $27, $14
	ehb
	eret

	.text
	.globl __start
__start:
	mtc0 $0, $12
	ehb
	la $22, out
/* PRA "System Call Exception": ExcCode Sys = 8. */
	li $2, 0
	ARM(1f, 2f)
2:	syscall
1:/* PRA "Breakpoint Exception": ExcCode Bp = 9. */
	ARM(1f, 2f)
2:	break
1:/* PRA "Trap Exception": ExcCode Tr = 13. */
	ARM(1f, 2f)
2:	teq $0, $0
/* PRA "Integer Overflow Exception": ExcCode Ov = 12; ISA "ADD": destination not modified on overflow. */
1:	li $10, 0x7fffffff
	li $11, 1
	li $12, 0x5555
	ARM(1f, 2f)
2:	add $12, $10, $11
1:	sw $12, 0($22)
	addiu $22, $22, 4
/* PRA "Reserved Instruction Exception": ExcCode RI = 10. */
	ARM(1f, 2f)
2:	.word 0x0000003f
/* PRA "Address Error Exception": load, ExcCode AdEL = 4, BadVAddr = failing address. */
1:	li $10, 0x80500001
	ARM(1f, 2f)
2:	lw $11, 0($10)
/* PRA "Address Error Exception": store, ExcCode AdES = 5. */
1:	li $10, 0x80500002
	ARM(1f, 2f)
2:	sw $11, 0($10)
/* PRA "Coprocessor Unusable Exception": ExcCode CpU = 11, Cause.CE = 1 for COP1. */
1:	ARM(1f, 2f)
2:	.word 0x44000000
/* PRA "Bus Error Exception": data, ExcCode DBE = 7; BadVAddr is not updated. */
1:	li $10, 0xa1000000
	ARM(1f, 2f)
2:	lw $11, 0($10)
/* PRA "General Exception Processing": fault in a delay slot sets Cause.BD and EPC = the branch. */
1:	ARM(1f, 2f)
2:	beq $0, $0, 1f
	break
	nop
/* PRA "Address Error Exception": instruction fetch, EPC = BadVAddr = misaligned PC. */
1:	li $10, 0x80400003
	ARM(1f, 3f)
	la $9, trigger
	sw $10, 0($9)
	jr $10
	nop
3:	nop
/* ISA "ERET": clears Status.EXL. */
1:	mfc0 $12, $12
	sw $12, 0($22)
	addiu $22, $22, 4
	WRITE_EXIT(out, 140)

	.data
	.align 4
resume:	.word 0
trigger: .word 0
out:	.space 256
