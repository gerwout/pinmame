#!/bin/sh
# Mutations of the lazy-cog core that the RTL cases must catch: each mutant p8run must differ from the RTL on at least
# one of chip/lazy*.spin, chip/lz*.spin and gen.py's scan-cog programs (SEEDS of them, default 200).
set -u
cd "$(dirname "$0")" || exit 2
TOOLS=${TOOLS:-$PWD/build/tools}
SEEDS=${SEEDS:-200}
OPENSPIN=$TOOLS/openspin/build/openspin
P1RTL=$TOOLS/p1rtl/p1rtl
CORE=../../../src/cpu/p8x32a
DEV=../../../src/wpc/pinheck
B=build/mutate
mkdir -p $B/case
rm -rf $B/gen
python3 gen.py --out $B/gen --count "$SEEDS" --first 900001 --hubflags --workers 1
cases="chip/lazy.spin $(ls chip/lz*.spin) $(ls $B/gen/*.spin)"
for f in $cases; do
	o=$B/case/$(basename "$f" .spin)
	[ -s "$o.rtl" ] && [ "$o.rtl" -nt "$f" ] && continue
	$OPENSPIN -q "$f" -o "$o.binary" > /dev/null 2>&1 || { echo "COMPILE FAIL $f"; exit 2; }
	python3 mkrom.py "$o.binary" "$o.rom" "$o.ram"
	$P1RTL -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 -dump "$o.rtlhub" > "$o.rtl"
done
fail=0
mutant() {
	name=$1
	python3 - "$2" "$3" $CORE/p8x32a.c $B/p8x32a.c <<'PY' || { echo "MUTANT $name: no match"; fail=$((fail + 1)); return; }
import sys
frm, to, src, dst = sys.argv[1:]
s = open(src).read()
if frm and s.count(frm) != 1: sys.exit(1)
open(dst, 'w').write(s.replace(frm, to) if frm else s)
PY
	cc -O2 -std=c99 -I$CORE -I$DEV -o $B/p8run run.c $B/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
	bad=0 first=
	for f in $cases; do
		o=$B/case/$(basename "$f" .spin)
		./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 -dump "$o.mhub" > "$o.m" 2> "$o.mlog"
		if ! cmp -s "$o.rtl" "$o.m" || ! cmp -s "$o.rtlhub" "$o.mhub" || grep -q "p8x32a: lazy\|p8run: lazy" "$o.mlog"; then
			bad=$((bad + 1)); [ -n "$first" ] || first=$(basename "$f")
		fi
	done
	if [ "$name" = none ]; then
		if [ $bad = 0 ]; then echo "unmutated core: all cases match"; else echo "UNMUTATED CORE differs on $bad cases (first $first)"; fail=$((fail + 1)); fi
	elif [ $bad = 0 ]; then echo "MUTANT SURVIVED $name"; fail=$((fail + 1))
	else echo "mutant $name caught by $bad cases (first $first)"; fi
}
mutant none '' ''
mutant journal-read 'if (p->jn_t[k] > t && (p->jn_a[k] & ~3u) == a)' 'if (0)'
mutant journal-earliest 'for (k = p->jn - 1; k >= 0; k--)
		if (p->jn_t[k] > t' 'for (k = 0; k < p->jn; k++)
		if (p->jn_t[k] > t'
mutant journal-full 'if (!p->jn_off) p->jn_full++;
		lazy_catch(p, h - 1);' 'if (!p->jn_off) p->jn_full++;'
mutant journal-old 'p->jn_old[p->jn][k] = p->hub[ha + k];' 'p->jn_old[p->jn][k] = (uint8_t)(v >> (8 * k));'
mutant ina-lazy 'if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);' ';'
mutant waitpxx-exit 'if (p->lz_on && (c->s & p->lz_pins)) lazy_exit(p);' ';'
mutant cogop-exit 'if (p->lz_on && (op == 2 || op == 3) && num == p->lz) lazy_exit(p);' ';'
mutant exit-pending 'add_pending(p, p->now, p->lz_pins, P8X32A_PEND_COG(p->lz));' ';'
mutant hub-read-wc 'if (FWC(w)) return 0;' ';'
mutant clkset-catch-up 'lazy_catch(p, h);
		if (p->bus.clkset)' 'if (p->bus.clkset)'
exit $((fail != 0))
