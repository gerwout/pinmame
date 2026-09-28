#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
SEEDS=${SEEDS:-200}
ASM="clang --target=mipsel-linux-gnu -march=mips32r2 -mno-abicalls -fno-pic -I."
mkdir -p $B/diff $B/golden $B/random
cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/mips32 -o $B/mips32run run.c ../../../src/cpu/mips32/mips32.c || exit 2
if [ -f dasm_test.c ]; then
	cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/mips32 -o $B/dasm_test dasm_test.c ../../../src/cpu/mips32/mips32dasm.c || exit 2
fi
if [ -f unit_test.c ]; then
	cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/mips32 -o $B/unit_test unit_test.c ../../../src/cpu/mips32/mips32.c || exit 2
fi
fail=0 pass=0
for c in ../../../src/cpu/mips32/*.c; do
	cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only -I../../../src/cpu/mips32 "$c" || { echo "C89 FAIL $c"; fail=$((fail + 1)); }
done

run_diff() {
	n=$(basename "$1" .S) o=$2/$(basename "$1" .S)
	if ! $ASM -c "$1" -o $o.o || ! ld.lld -m elf32ltsmip -static -e __start -Ttext=0x400000 $o.o -o $o.elf; then
		echo "BUILD FAIL $1"; fail=$((fail + 1)); return
	fi
	qemu-mipsel -cpu 4KEc $o.elf > $o.qemu || { echo "QEMU FAIL $1"; fail=$((fail + 1)); return; }
	$B/mips32run $o.elf > $o.ours || { echo "RUN FAIL $1 (exit $?)"; fail=$((fail + 1)); return; }
	if cmp -s $o.qemu $o.ours; then pass=$((pass + 1)); else echo "MISMATCH $1"; python3 cmpdump.py $o.qemu $o.ours; fail=$((fail + 1)); fi
}

[ -x $B/dasm_test ] && { ./$B/dasm_test || fail=$((fail + 1)); }
[ -x $B/unit_test ] && { ./$B/unit_test || fail=$((fail + 1)); }
for f in diff/*.S; do [ -e "$f" ] && run_diff "$f" $B/diff; done
if [ -f gen.py ]; then
	rm -rf $B/random && python3 gen.py --out $B/random --count "$SEEDS"
	for f in $B/random/*.S; do run_diff "$f" $B/random; done
fi
for f in golden/*.S; do
	[ -e "$f" ] || continue
	n=$(basename "$f" .S) o=$B/golden/$(basename "$f" .S)
	if ! $ASM -c "$f" -o $o.o || ! ld.lld -m elf32ltsmip -static -e __start -Ttext=0x80400000 --section-start=.exc=0x80000180 $o.o -o $o.elf; then
		echo "BUILD FAIL $f"; fail=$((fail + 1)); continue
	fi
	$B/mips32run -x -e $o.elf > $o.out
	if diff -u golden/$n.expect $o.out > $o.diff; then pass=$((pass + 1)); else echo "GOLDEN FAIL $f"; cat $o.diff; fail=$((fail + 1)); fi
done
echo "mips32: $pass passed, $fail failed"
[ $fail -eq 0 ]
