#!/bin/sh
# Mutations of mips32.c's gpr_use that uncertain_test must catch: each mutant built and run must fail.
set -u
cd "$(dirname "$0")" || exit 2
B=build/mutate
mkdir -p $B
fail=0
mutant() {
	name=$1 from=$2 to=$3
	python3 - "$from" "$to" ../../../src/cpu/mips32/mips32.c $B/mips32.c <<'PY' || { echo "MUTANT $name: no match"; fail=$((fail + 1)); return; }
import sys
frm, to, src, dst = sys.argv[1:]
s = open(src).read()
if s.count(frm) != 1: sys.exit(1)
open(dst, 'w').write(s.replace(frm, to))
PY
	cc -O2 -std=c99 -I../../../src/cpu/mips32 -o $B/uncertain_test uncertain_test.c $B/mips32.c || exit 2
	if ./$B/uncertain_test > $B/$name.log 2>&1; then echo "MUTANT SURVIVED $name"; fail=$((fail + 1))
	else echo "mutant $name caught: $(tail -1 $B/$name.log)"; fi
}
mutant bshfl 'case 0x20: *rd = R1(rt); return (int)d;' 'case 0x20: *rd = 0; return (int)d;'
mutant multdiv 'case 0x18: case 0x19: case 0x1A: case 0x1B: return -1;' 'case 0x18: case 0x19: case 0x1A: case 0x1B: *rd = 0; return -1;'
mutant sra 'case 0x00: case 0x02: case 0x03: *rd = R1(rt); return (int)d;' 'case 0x00: case 0x02: *rd = R1(rt); return (int)d;
		case 0x03: *rd = 0; return (int)d;'
exit $((fail != 0))
