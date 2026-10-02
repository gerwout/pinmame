#!/bin/sh
# Mutations of the core that the RTL cases must catch: each mutant p8run (time-order checked; translated where the
# mutation is in the translator's path) must differ from the RTL, stop on a time-order fault, or miss a chip test's
# EXPECT-JITVAR, on one of chip/*.spin and gen.py's programs (SEEDS of each kind, default 100).
set -u
cd "$(dirname "$0")" || exit 2
TOOLS=${TOOLS:-$PWD/build/tools}
SEEDS=${SEEDS:-100}
OPENSPIN=$TOOLS/openspin/build/openspin
P1RTL=$TOOLS/p1rtl/p1rtl
CORE=../../../src/cpu/p8x32a
DEV=../../../src/wpc/pinheck
ASMJIT=../../../ext/asmjit
JF="-O2 -std=c++17 -DASMJIT_STATIC -DASMJIT_NO_FOREIGN -DASMJIT_NO_UJIT -I$ASMJIT"
B=build/mutcore
mkdir -p $B/case $B/obj build/asmjit
rm -rf $B/gen
python3 gen.py --out $B/gen --count "$SEEDS" --first 700001 --hubflags --workers 0.3
cases="$(ls chip/*.spin) $(ls $B/gen/*.spin)"
for f in $cases; do
	o=$B/case/$(basename "$f" .spin)
	[ -s "$o.rtl" ] && [ "$o.rtl" -nt "$f" ] && continue
	$OPENSPIN -q "$f" -o "$o.binary" > /dev/null 2>&1 || { echo "COMPILE FAIL $f"; exit 2; }
	python3 mkrom.py "$o.binary" "$o.rom" "$o.ram"
	$P1RTL -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 -dump "$o.rtlhub" > "$o.rtl"
done
if [ ! -f build/asmjit/libasmjit.a ]; then
	for f in $ASMJIT/asmjit/core/*.cpp $ASMJIT/asmjit/x86/*.cpp $ASMJIT/asmjit/support/*.cpp; do
		c++ $JF -c "$f" -o build/asmjit/$(basename "$f" .cpp).o || exit 2
	done
	ar rcs build/asmjit/libasmjit.a build/asmjit/*.o || exit 2
fi
fail=0
# mutant NAME FILE FROM TO JIT: the core's FILE with FROM replaced by TO; JIT 1 builds p8run translated
mutant() {
	name=$1 file=$2 jit=$5
	cp $CORE/p8x32a.c $CORE/p8x32ajit.cpp $B/
	python3 - "$3" "$4" $CORE/$file $B/$file <<'PY' || { echo "MUTANT $name: no match"; fail=$((fail + 1)); return; }
import sys
frm, to, src, dst = sys.argv[1:]
s = open(src).read()
if frm and s.count(frm) != 1: sys.exit(1)
open(dst, 'w').write(s.replace(frm, to) if frm else s)
PY
	if [ "$jit" = 1 ]; then
		c++ $JF -I$CORE -c $B/p8x32ajit.cpp -o $B/obj/jit.o || exit 2
		for f in run.c $B/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c; do
			cc -O2 -std=c99 -DP8X32A_JIT -DP8X32A_CHECK -I$CORE -I$DEV -c "$f" -o $B/obj/$(basename "$f" .c).o || exit 2
		done
		c++ -o $B/p8run $B/obj/*.o build/asmjit/libasmjit.a -lz -lpthread || exit 2
	else
		cc -O2 -std=c99 -DP8X32A_CHECK -I$CORE -I$DEV -o $B/p8run run.c $B/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
	fi
	bad=0 first=
	for f in $cases; do
		o=$B/case/$(basename "$f" .spin)
		jv=$(sed -n "s/^' EXPECT-JITVAR: //p" "$f")
		# a mutant that loops for ever is caught too
		timeout -k 5 20 ./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 ${jv:+-jitvar} -dump "$o.mhub" > "$o.m" 2> "$o.mlog"
		if ! cmp -s "$o.rtl" "$o.m" || ! cmp -s "$o.rtlhub" "$o.mhub" || grep -q "time order" "$o.mlog" ||
		   { [ -n "$jv" ] && [ "$jit" = 1 ] && ! grep -qxF "p8run: $jv block lookups left to the interpreter" "$o.mlog"; }; then
			bad=$((bad + 1)); [ -n "$first" ] || first=$(basename "$f")
		fi
	done
	if [ "${name%-*}" = none ]; then
		if [ $bad = 0 ]; then echo "unmutated core ($name): all cases match"; else echo "UNMUTATED CORE ($name) differs on $bad cases (first $first)"; fail=$((fail + 1)); fi
	elif [ $bad = 0 ]; then echo "MUTANT SURVIVED $name"; fail=$((fail + 1))
	else echo "mutant $name caught by $bad cases (first $first)"; fi
}
mutant none-interpreted p8x32a.c '' '' 0
mutant none-translated p8x32a.c '' '' 1
# a pin read that sends the pins ahead of the other cogs (the cause of Plan 12b's lost bits)
mutant ina-ahead p8x32a.c '	if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);
	flush(p, t);' '	if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);
	flush(p, t + 64);' 0
exit $((fail != 0))
