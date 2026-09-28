#include "common.h"

#define ACC(op, a, b, h, l) \
	li $1, h; mthi $1; li $1, l; mtlo $1; li $8, a; li $9, b; op $8, $9; mfhi $2; ST($2); mflo $2; ST($2)

	.text
	.globl __start
__start:
	la $23, out
	ACC(mult,  -3, 7, 0, 0)
	ACC(multu, -3, 7, 0, 0)
	ACC(madd,  -3, 7, 0, 5)
	ACC(maddu, -3, 7, 0, 5)
	ACC(msub,  -3, 7, 0, 5)
	ACC(msubu, -3, 7, 0, 5)
	ACC(madd,  0x80000000, 0x80000000, 0xffffffff, 0xffffffff)
	ACC(msub,  0x80000000, -1, 0x7fffffff, 0)
	ACC(maddu, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff)
	ACC(msubu, 0xffffffff, 2, 0, 1)
	ACC(madd,  0x7fffffff, -2, 1, 0x80000000)
	ACC(msub,  -1, -1, 0, 0)
	WRITE_EXIT(out, 96)
	.data
	.align 4
out:	.space 128
