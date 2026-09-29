/* Idle loops compare cog snapshots (the cog state from ptr on) with memcmp: the snapshot must hold no unnamed padding. */
#include "p8x32a.h"
#include <stdio.h>

#define F(m) sizeof(((p8x32a_cog *)0)->m)

int main(void)
{
	size_t named = F(ptr) + F(ix) + F(nix) + F(i) + F(s) + F(d) + F(p) + F(px) + F(c) + F(z) + F(cancel) + F(run) + F(cond) +
	               F(pad) + F(ev) + F(ev_t) + F(t0) + F(latch) + F(disable_at) + F(restart_at) + F(outa) + F(dira) +
	               F(ctr) + F(frq) + F(phs) + F(vcfg) + F(vscl) + F(phs_t) + F(ctr_old) + F(frq_old) + F(phs_old) +
	               F(phs_t_old) + F(ctr_at) + F(ctr_seen) + F(frq_seen);
	if (sizeof(p8x32a_reg) != F(outa.prev) + F(outa.cur) + F(outa.at) || named != P8X32A_SNAP) {
		printf("SNAP FAIL: %u bytes of named fields, %u in the snapshot\n", (unsigned)named, (unsigned)P8X32A_SNAP);
		return 1;
	}
	printf("snap: %u bytes, no padding\n", (unsigned)named);
	return 0;
}
