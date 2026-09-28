#include "p8x32a.h"
#include <stdio.h>
#include <string.h>

static const struct { uint32_t op; const char *text; } cases[] = {
	{ 0xA0FC0000U, "             mov     $000,#$000" },
	{ 0x08BC001BU, "             rdlong  $000,$01B" },
	{ 0x007C001FU, "             wrbyte  $000,#$01F" },
	{ 0x0CFC3601U, "             cogid   $01B" },
	{ 0x0DFC3602U, "             coginit $01B wc wr" },
	{ 0x0DFC0004U, "             locknew $000 wc" },
	{ 0x0C7C3607U, "             lockclr $01B" },
	{ 0x0C7C0000U, "             clkset  $000" },
	{ 0x5C7C0000U, "             jmp     #$000" },
	{ 0x5CFC381BU, "             jmpret  $01C,#$01B" },
	{ 0x627C0001U, "             test    $000,#$001 wz" },
	{ 0x603C001BU, "             test    $000,$01B" },
	{ 0x873C001BU, "             cmp     $000,$01B wz wc" },
	{ 0x84FC0003U, "             sub     $000,#$003" },
	{ 0xC13C001BU, "             cmps    $000,$01B wc" },
	{ 0xE4D40000U, "if_nz        djnz    $000,#$000" },
	{ 0xEC7C001BU, "             tjz     $000,#$01B" },
	{ 0xF8BC001BU, "             waitcnt $000,$01B" },
	{ 0xF03C001BU, "             waitpeq $000,$01B" },
	{ 0xA0FFE8FFU, "             mov     outa,#$0FF" },
	{ 0xA0BC01FCU, "             mov     $000,phsa" },
	{ 0x80A001F1U, "if_c_and_z   add     $000,cnt" },
	{ 0x29FC0007U, "             shr     $000,#$007 wc" },
	{ 0x3CFC0010U, "             rev     $000,#$010" },
	{ 0x50FC0155U, "             movs    $000,#$155" },
	{ 0x74BC001BU, "             muxnc   $000,$01B" },
	{ 0x4CBC001BU, "             max     $000,$01B" },
	{ 0xE1FC0005U, "             cmpsub  $000,#$005 wc" },
	{ 0x5C7C0000U, "             jmp     #$000" },
	{ 0x00000000U, "nop" },
	{ 0x10000000U, "             long    $10000000" },
};

int main(void)
{
	char buf[80];
	unsigned i, bad = 0;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(buf, 0, sizeof(buf));
		if (p8x32a_dasm(buf, cases[i].op) != 4 || strcmp(buf, cases[i].text)) {
			printf("DASM FAIL %08x: got \"%s\", want \"%s\"\n", (unsigned)cases[i].op, buf, cases[i].text);
			bad++;
		}
	}
	printf("dasm: %u/%u\n", i - bad, i);
	return bad != 0;
}
