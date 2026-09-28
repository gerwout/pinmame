#define SYS_EXIT  4001
#define SYS_WRITE 4004
#define MMIO_EIC  0xA0FFFFF0
#define MMIO_CT   0xA0FFFFF4

	.set noreorder
	.set noat

#define WRITE_EXIT(buf, len) \
	li $2, SYS_WRITE; li $4, 1; la $5, buf; li $6, len; syscall; \
	li $2, SYS_EXIT; li $4, 0; syscall

#define RDPGPR(rd, rt) .word (0x41400000 | ((rt) << 16) | ((rd) << 11))
#define WRPGPR(rd, rt) .word (0x41C00000 | ((rt) << 16) | ((rd) << 11))

#define ST(r) sw r, 0($23); addiu $23, $23, 4
