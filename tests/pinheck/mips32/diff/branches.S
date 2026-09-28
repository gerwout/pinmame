#include "common.h"
	.text
	.globl __start
__start:
	la $23, out
	li $16, 0
	li $9, 1
	beq $0, $0, 1f
	addiu $16, $16, 1
	addiu $16, $16, 100
1:	ST($16)
	bne $0, $0, 2f
	addiu $16, $16, 2
	addiu $16, $16, 4
2:	ST($16)
	beql $0, $9, 3f
	addiu $16, $16, 8
	addiu $16, $16, 16
3:	ST($16)
	bnel $0, $9, 4f
	addiu $16, $16, 32
	addiu $16, $16, 64
4:	ST($16)
	bltzal $9, 5f
	nop
5:	ST($31)
	bgezall $0, 6f
	addiu $16, $16, 1
	addiu $16, $16, 1000
6:	ST($16)
	ST($31)
	bltzall $9, 7f
	addiu $16, $16, 3
	addiu $16, $16, 5
7:	ST($16)
	ST($31)
	jal 8f
	addiu $16, $16, 1
	addiu $16, $16, 1000
8:	ST($16)
	ST($31)
	la $1, 9f
	jalr $30, $1
	addiu $16, $16, 1
	addiu $16, $16, 1000
9:	ST($16)
	ST($30)
	la $1, 10f
	jr $1
	addiu $16, $16, 1
	addiu $16, $16, 1000
10:	ST($16)
	li $17, 5
	li $18, 0
11:	addiu $18, $18, 3
	addiu $17, $17, -1
	bgtz $17, 11b
	addiu $18, $18, 1
	ST($18)
	li $17, 3
	li $18, 0
12:	addiu $17, $17, -1
	bgtzl $17, 12b
	addiu $18, $18, 10
	ST($18)
	li $19, -1
	blez $19, 13f
	addiu $16, $16, 1
	addiu $16, $16, 1000
13:	ST($16)
	bltz $0, 14f
	addiu $16, $16, 1
	addiu $16, $16, 2
14:	ST($16)
	bgezl $19, 15f
	addiu $16, $16, 1000
	addiu $16, $16, 7
15:	ST($16)
	blezl $9, 16f
	addiu $16, $16, 1000
	addiu $16, $16, 9
16:	ST($16)
	WRITE_EXIT(out, 256)
	.data
	.align 4
out:	.space 256
