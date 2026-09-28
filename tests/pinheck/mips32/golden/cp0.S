#include "common.h"
	.section .exc, "ax"
	li $2, SYS_EXIT
	li $4, 1
	syscall

	.text
	.globl __start
__start:
	la $23, out
	mfc0 $2, $12, 0
	ST($2)
	mfc0 $2, $15, 0
	ST($2)
	mfc0 $2, $15, 1
	ST($2)
	mfc0 $2, $16, 0
	ST($2)
	mfc0 $2, $16, 1
	ST($2)
	mfc0 $2, $16, 2
	ST($2)
	mfc0 $2, $16, 3
	ST($2)
	mfc0 $2, $12, 2
	ST($2)
	li $9, -1
	mtc0 $9, $12, 1
	mfc0 $2, $12, 1
	ST($2)
	mtc0 $0, $12, 1
	mtc0 $9, $12, 2
	mfc0 $2, $12, 2
	ST($2)
	mtc0 $0, $12, 2
	mtc0 $9, $13, 0
	mfc0 $2, $13, 0
	ST($2)
	mtc0 $0, $13, 0
	mtc0 $9, $7, 0
	mfc0 $2, $7, 0
	ST($2)
	mtc0 $9, $15, 1
	mfc0 $2, $15, 1
	ST($2)
	li $10, 0x80000000
	mtc0 $10, $15, 1
	mtc0 $9, $16, 0
	mfc0 $2, $16, 0
	ST($2)
	mtc0 $9, $12, 0
	mfc0 $2, $12, 0
	ST($2)
	mtc0 $0, $12, 0
	ehb
	li $10, 100
	mtc0 $10, $9
	mfc0 $2, $9
	nop
	nop
	nop
	mfc0 $3, $9
	ST($2)
	ST($3)
	rdhwr $2, $3
	ST($2)
	ei $2
	ST($2)
	mfc0 $2, $12
	ST($2)
	di $2
	ST($2)
	mfc0 $2, $12
	ST($2)
	li $8, 0x77
	li $10, 0x40
	mtc0 $10, $12, 2
	ehb
	li $11, 0x1234
	WRPGPR(8, 11)
	RDPGPR(2, 8)
	ST($2)
	ST($8)
	mtc0 $0, $12, 2
	WRITE_EXIT(out, 96)

	.data
	.align 4
out:	.space 256
