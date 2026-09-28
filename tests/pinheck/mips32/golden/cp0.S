#include "common.h"
/* PRA = MD00090, ISA = MD00086; cited by section title. Values not fixed by the architecture follow the plan's Global Constraints. */
	.section .exc, "ax"
	li $2, SYS_EXIT
	li $4, 1
	syscall

	.text
	.globl __start
__start:
	la $23, out
/* PRA "Reset Exception": BEV = 1, ERL = 1. PRId is supplied by the runner (0x00018700). */
	mfc0 $2, $12, 0
	ST($2)
	mfc0 $2, $15, 0
	ST($2)
/* PRA "EBase Register (CP0 Register 15, Select 1)": resets to 0x80000000. */
	mfc0 $2, $15, 1
	ST($2)
/* PRA "Config Register" .. "Config3 Register": AR = 1, MT = 3 (fixed mapping), Config3 VEIC|VInt. */
	mfc0 $2, $16, 0
	ST($2)
	mfc0 $2, $16, 1
	ST($2)
	mfc0 $2, $16, 2
	ST($2)
	mfc0 $2, $16, 3
	ST($2)
/* PRA "SRSCtl Register (CP0 Register 12, Select 2)": HSS = shadow sets - 1. */
	mfc0 $2, $12, 2
	ST($2)
	li $9, -1
/* PRA "IntCtl Register": VS is the writable field. */
	mtc0 $9, $12, 1
	mfc0 $2, $12, 1
	ST($2)
	mtc0 $0, $12, 1
/* PRA "SRSCtl Register": ESS and PSS writable, HSS/EICSS/CSS read-only. */
	mtc0 $9, $12, 2
	mfc0 $2, $12, 2
	ST($2)
	mtc0 $0, $12, 2
/* PRA "Cause Register": IV, DC, IP1..0 writable; WP writable per the plan mask, not the PRA (no watch registers). */
	mtc0 $9, $13, 0
	mfc0 $2, $13, 0
	ST($2)
	mtc0 $0, $13, 0
/* PRA "HWREna Register (CP0 Register 7, Select 0)": bits 3..0. */
	mtc0 $9, $7, 0
	mfc0 $2, $7, 0
	ST($2)
/* PRA "EBase Register": bits 29..12 writable, bit 31 reads 1. */
	mtc0 $9, $15, 1
	mfc0 $2, $15, 1
	ST($2)
	li $10, 0x80000000
	mtc0 $10, $15, 1
/* PRA "Config Register": K0 writable. */
	mtc0 $9, $16, 0
	mfc0 $2, $16, 0
	ST($2)
/* PRA "Status Register": plan mask 0x1A48FF17; NMI settable here although the PRA makes it write-0-only. */
	mtc0 $9, $12, 0
	mfc0 $2, $12, 0
	ST($2)
	mtc0 $0, $12, 0
	ehb
/* PRA "Count Register": counts every other cycle (plan timing model). */
	li $10, 100
	mtc0 $10, $9
	mfc0 $2, $9
	nop
	nop
	nop
	mfc0 $3, $9
	ST($2)
	ST($3)
/* ISA "RDHWR": hardware register 3 is CCRes = 2. */
	rdhwr $2, $3
	ST($2)
/* ISA "EI" / "DI": return the previous Status. */
	ei $2
	ST($2)
	mfc0 $2, $12
	ST($2)
	di $2
	ST($2)
	mfc0 $2, $12
	ST($2)
/* ISA "RDPGPR" / "WRPGPR" and PRA "GPR Shadow Registers": access the set in SRSCtl.PSS. */
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
