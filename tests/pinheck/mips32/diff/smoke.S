#include "common.h"
	.text
	.globl __start
__start:
	lui $8, 0x1234
	ori $8, $8, 0x5678
	ext $9, $8, 4, 8
	ins $8, $9, 24, 4
	wsbh $10, $8
	seb $11, $8
	clz $12, $9
	la $4, out
	sw $8, 0($4)
	sw $9, 4($4)
	sw $10, 8($4)
	sw $11, 12($4)
	sw $12, 16($4)
	WRITE_EXIT(out, 20)
	.data
	.align 4
out:	.space 64
