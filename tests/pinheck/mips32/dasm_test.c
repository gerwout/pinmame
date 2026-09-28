#include "mips32.h"
#include <stdio.h>
#include <string.h>

static const struct { uint32_t pc, op; const char *text; } cases[] = {
	{ 0x9D001000, 0x00000000, "nop" },
	{ 0x9D001004, 0x00851021, "addu    $v0,$a0,$a1" },
	{ 0x9D001008, 0x000940C0, "sll     $t0,$t1,3" },
	{ 0x9D00100C, 0x002941C2, "rotr    $t0,$t1,7" },
	{ 0x9D001010, 0x018B5007, "srav    $t2,$t3,$t4" },
	{ 0x9D001014, 0x018B5046, "rotrv   $t2,$t3,$t4" },
	{ 0x9D001018, 0x03E00008, "jr      $ra" },
	{ 0x9D00101C, 0x0320F809, "jalr    $ra,$t9" },
	{ 0x9D001020, 0x00001810, "mfhi    $v1" },
	{ 0x9D001024, 0x00850018, "mult    $a0,$a1" },
	{ 0x9D001028, 0x00C7001B, "divu    $a2,$a3" },
	{ 0x9D00102C, 0x00000034, "teq     $zero,$zero" },
	{ 0x9D001030, 0x0000000C, "syscall" },
	{ 0x9D001034, 0x27BDFFE0, "addiu   $sp,$sp,-32" },
	{ 0x9D001038, 0x3128FF00, "andi    $t0,$t1,0xff00" },
	{ 0x9D00103C, 0x3C1A9D00, "lui     $k0,0x9d00" },
	{ 0x9D001040, 0x8FA2FFF8, "lw      $v0,-8($sp)" },
	{ 0x9D001044, 0xA3800003, "sb      $zero,3($gp)" },
	{ 0x9D001048, 0x88880003, "lwl     $t0,3($a0)" },
	{ 0x9D00104C, 0xC0890000, "ll      $t1,0($a0)" },
	{ 0x9D001050, 0x10850003, "beq     $a0,$a1,0x9d001060" },
	{ 0x9D001054, 0x5480FFFD, "bnel    $a0,$zero,0x9d00104c" },
	{ 0x9D001058, 0x06110001, "bgezal  $s0,0x9d001060" },
	{ 0x9D00105C, 0x0B40048D, "j       0x9d001234" },
	{ 0x9D001060, 0x40086002, "mfc0    $t0,$12,2" },
	{ 0x9D001064, 0x409A7000, "mtc0    $k0,$14,0" },
	{ 0x9D001068, 0x42000018, "eret" },
	{ 0x9D00106C, 0x41686000, "di      $t0" },
	{ 0x9D001070, 0x41606020, "ei" },
	{ 0x9D001074, 0x415DE800, "rdpgpr  $sp,$sp" },
	{ 0x9D001078, 0x41CA4800, "wrpgpr  $t1,$t2" },
	{ 0x9D00107C, 0x712A4002, "mul     $t0,$t1,$t2" },
	{ 0x9D001080, 0x70850000, "madd    $a0,$a1" },
	{ 0x9D001084, 0x712C6020, "clz     $t4,$t1" },
	{ 0x9D001088, 0x7D093900, "ext     $t1,$t0,4,8" },
	{ 0x9D00108C, 0x7D28DE04, "ins     $t0,$t1,24,4" },
	{ 0x9D001090, 0x7C0850A0, "wsbh    $t2,$t0" },
	{ 0x9D001094, 0x7C085C20, "seb     $t3,$t0" },
	{ 0x9D001098, 0x7C085E20, "seh     $t3,$t0" },
	{ 0x9D00109C, 0x7C03183B, "rdhwr   $v1,$3" },
	{ 0x9D0010A0, 0x000000C0, "ehb" },
	{ 0x9D0010A4, 0xBC940000, "cache   0x14,0($a0)" },
	{ 0x9D0010A8, 0x42000020, "wait" },
	{ 0x9D0010AC, 0x0488FFFB, "tgei    $a0,-5" },
	{ 0x9D0010B0, 0xFC000000, ".word   0xfc000000" }
};

int main(void)
{
	char buf[80];
	unsigned i, bad = 0;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(buf, 0, sizeof(buf));
		if (mips32_dasm(buf, cases[i].pc, cases[i].op) != 4 || strcmp(buf, cases[i].text)) {
			printf("DASM FAIL %08x: got \"%s\", want \"%s\"\n", (unsigned)cases[i].op, buf, cases[i].text);
			bad++;
		}
	}
	printf("dasm: %u/%u\n", i - bad, i);
	return bad != 0;
}
