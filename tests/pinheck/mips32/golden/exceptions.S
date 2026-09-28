#include "common.h"

#define ARM(res, trig) \
	la $8, res; la $9, resume; sw $8, 0($9); la $8, trig; la $9, trigger; sw $8, 0($9)

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
	li $2, 0
	ARM(1f, 2f)
2:	syscall
1:	ARM(1f, 2f)
2:	break
1:	ARM(1f, 2f)
2:	teq $0, $0
1:	li $10, 0x7fffffff
	li $11, 1
	li $12, 0x5555
	ARM(1f, 2f)
2:	add $12, $10, $11
1:	sw $12, 0($22)
	addiu $22, $22, 4
	ARM(1f, 2f)
2:	.word 0x0000003f
1:	li $10, 0x80500001
	ARM(1f, 2f)
2:	lw $11, 0($10)
1:	li $10, 0x80500002
	ARM(1f, 2f)
2:	sw $11, 0($10)
1:	ARM(1f, 2f)
2:	.word 0x44000000
1:	li $10, 0xa1000000
	ARM(1f, 2f)
2:	lw $11, 0($10)
1:	ARM(1f, 2f)
2:	beq $0, $0, 1f
	break
	nop
1:	li $10, 0x80400003
	ARM(1f, 3f)
	la $9, trigger
	sw $10, 0($9)
	jr $10
	nop
3:	nop
1:	mfc0 $12, $12
	sw $12, 0($22)
	addiu $22, $22, 4
	WRITE_EXIT(out, 140)

	.data
	.align 4
resume:	.word 0
trigger: .word 0
out:	.space 256
