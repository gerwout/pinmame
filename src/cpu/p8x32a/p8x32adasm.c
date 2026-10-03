// license:BSD-3-Clause

#include "p8x32a.h"
#include <stdio.h>
#include <string.h>

static const char *const conds[16] = {
	"if_never", "if_nc_and_nz", "if_nc_and_z", "if_nc", "if_c_and_nz", "if_nz", "if_c_ne_z", "if_nc_or_nz",
	"if_c_and_z", "if_c_eq_z", "if_z", "if_nc_or_z", "if_c", "if_c_or_nz", "if_c_or_z", ""
};

static const char *const ops[64] = {
	"wrbyte", "wrword", "wrlong", "hubop", NULL, NULL, NULL, NULL,
	"ror", "rol", "shr", "shl", "rcr", "rcl", "sar", "rev",
	"mins", "maxs", "min", "max", "movs", "movd", "movi", "jmpret",
	"and", "andn", "or", "xor", "muxc", "muxnc", "muxz", "muxnz",
	"add", "sub", "addabs", "subabs", "sumc", "sumnc", "sumz", "sumnz",
	"mov", "neg", "abs", "absneg", "negc", "negnc", "negz", "negnz",
	"cmps", "cmpsx", "addx", "subx", "adds", "subs", "addsx", "subsx",
	"cmpsub", "djnz", "tjnz", "tjz", "waitpeq", "waitpne", "waitcnt", "waitvid"
};

static const char *const sysops[8] = { "clkset", "cogid", "coginit", "cogstop", "locknew", "lockret", "lockset", "lockclr" };

static const char *const sregs[16] = {
	"par", "cnt", "ina", "inb", "outa", "outb", "dira", "dirb",
	"ctra", "ctrb", "frqa", "frqb", "phsa", "phsb", "vcfg", "vscl"
};

static void reg(char *b, unsigned r)
{
	if (r >= 0x1F0) strcpy(b, sregs[r - 0x1F0]);
	else sprintf(b, "$%03X", r);
}

unsigned p8x32a_dasm(char *buf, uint32_t op)
{
	unsigned o = op >> 26, cond = (op >> 18) & 15, d = (op >> 9) & 511, s = op & 511;
	int wz = (op >> 25) & 1, wc = (op >> 24) & 1, wr = (op >> 23) & 1, im = (op >> 22) & 1, defwr = 1, sys = 0;
	const char *name = ops[o];
	char db[16], sb[16], fl[16];

	if (op == 0) { strcpy(buf, "nop"); return 4; }
	if (!name) { sprintf(buf, "%-12s %-7s $%08X", "", "long", (unsigned)op); return 4; }
	switch (o) {
	case 0x00: case 0x01: case 0x02:
		if (wr) name = o == 0 ? "rdbyte" : o == 1 ? "rdword" : "rdlong";
		defwr = wr;
		break;
	case 0x03:
		if (im && s < 8) { name = sysops[s]; sys = 1; defwr = s == 1 || s == 4; }
		else defwr = 0;
		break;
	case 0x17: if (!wr) name = "jmp"; defwr = wr; break;
	case 0x18: if (!wr) name = "test"; defwr = wr; break;
	case 0x19: if (!wr) name = "testn"; defwr = wr; break;
	case 0x21: if (!wr) name = "cmp"; defwr = wr; break;
	case 0x33: if (!wr) name = "cmpx"; defwr = wr; break;
	case 0x30: case 0x31: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3F: defwr = 0; break;
	}
	reg(db, d);
	if (im) sprintf(sb, "#$%03X", s);
	else reg(sb, s);
	fl[0] = 0;
	if (wz) strcat(fl, " wz");
	if (wc) strcat(fl, " wc");
	if (wr != defwr) strcat(fl, wr ? " wr" : " nr");
	if (sys) sprintf(buf, "%-12s %-7s %s%s", conds[cond], name, db, fl);
	else if (o == 0x17 && !wr) sprintf(buf, "%-12s %-7s %s%s", conds[cond], name, sb, fl);
	else sprintf(buf, "%-12s %-7s %s,%s%s", conds[cond], name, db, sb, fl);
	return 4;
}
