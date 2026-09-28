#include "common.h"
	.text
	.globl __start
__start:
	la $23, out
	li $8, 0x80000000
	li $9, 0xffffffff
	li $10, 0x7fffffff
	li $11, 33
	li $12, 0x00008080
	li $13, 1
	sll $2, $9, 31
	ST($2)
	srl $2, $8, 31
	ST($2)
	sra $2, $8, 31
	ST($2)
	sra $2, $10, 0
	ST($2)
	rotr $2, $8, 0
	ST($2)
	rotr $2, $8, 31
	ST($2)
	sllv $2, $9, $11
	ST($2)
	rotrv $2, $10, $11
	ST($2)
	srav $2, $8, $11
	ST($2)
	slt $2, $8, $10
	ST($2)
	sltu $2, $8, $10
	ST($2)
	slti $2, $8, -1
	ST($2)
	sltiu $2, $10, -1
	ST($2)
	clz $2, $0
	ST($2)
	clo $2, $9
	ST($2)
	clz $2, $8
	ST($2)
	seb $2, $12
	ST($2)
	seh $2, $12
	ST($2)
	wsbh $2, $8
	ST($2)
	ext $2, $9, 0, 32
	ST($2)
	ext $2, $8, 31, 1
	ST($2)
	move $2, $9
	ins $2, $0, 0, 32
	ST($2)
	move $2, $0
	ins $2, $9, 31, 1
	ST($2)
	li $2, 7
	movz $2, $9, $0
	ST($2)
	li $2, 7
	movn $2, $9, $0
	ST($2)
	mult $8, $9
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	multu $9, $9
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	mthi $0
	mtlo $9
	maddu $13, $13
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	mthi $0
	mtlo $0
	msub $13, $13
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	mthi $8
	mtlo $0
	madd $8, $9
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	div $zero, $8, $13
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	li $14, -7
	li $15, 2
	div $zero, $14, $15
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	divu $zero, $14, $15
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	mul $2, $8, $9
	ST($2)
	addiu $2, $10, 1
	ST($2)
	lui $2, 0xffff
	ST($2)
	nor $2, $0, $0
	ST($2)
	WRITE_EXIT(out, 256)
	.data
	.align 4
out:	.space 256
