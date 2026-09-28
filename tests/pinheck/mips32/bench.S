#include "common.h"
	.text
	.globl __start
__start:
	li $8, 20000000
	li $9, 0
	la $10, buf
1:	addu $9, $9, $8
	sll $11, $9, 3
	xor $9, $9, $11
	lw $12, 0($10)
	addu $12, $12, $9
	sw $12, 0($10)
	addiu $8, $8, -1
	bnez $8, 1b
	nop
	WRITE_EXIT(buf, 4)
	.data
	.align 4
buf:	.word 0
