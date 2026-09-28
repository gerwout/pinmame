#include "common.h"
	.text
	.globl __start
__start:
	la $23, out
	la $20, buf
	li $9, 0xaabbccdd
	li $2, 0x01020304
	lwl $2, 0($20)
	ST($2)
	li $2, 0x01020304
	lwl $2, 1($20)
	ST($2)
	li $2, 0x01020304
	lwl $2, 2($20)
	ST($2)
	li $2, 0x01020304
	lwl $2, 3($20)
	ST($2)
	li $2, 0x01020304
	lwr $2, 0($20)
	ST($2)
	li $2, 0x01020304
	lwr $2, 1($20)
	ST($2)
	li $2, 0x01020304
	lwr $2, 2($20)
	ST($2)
	li $2, 0x01020304
	lwr $2, 3($20)
	ST($2)
	lwr $2, 1($20)
	lwl $2, 4($20)
	ST($2)
	swl $9, 16($20)
	swl $9, 21($20)
	swl $9, 26($20)
	swl $9, 31($20)
	swr $9, 32($20)
	swr $9, 37($20)
	swr $9, 42($20)
	swr $9, 47($20)
	lb $2, 3($20)
	ST($2)
	lbu $2, 3($20)
	ST($2)
	lh $2, 2($20)
	ST($2)
	lhu $2, 2($20)
	ST($2)
	lh $2, 6($20)
	ST($2)
	sb $9, 49($20)
	sh $9, 50($20)
	ll $2, 52($20)
	addiu $2, $2, 1
	sc $2, 52($20)
	ST($2)
	lw $3, 52($20)
	ST($3)
	WRITE_EXIT(out, 256)
	.data
	.align 4
out:	.space 128
buf:	.byte 0x11, 0x22, 0x33, 0x84, 0x55, 0x66, 0x77, 0x88
	.byte 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
	.space 36
	.word 0x7ffffffe
	.space 72
