#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
TOOLS=${TOOLS:-$PWD/build/tools}
SEEDS=${SEEDS:-100}
OPENSPIN=$TOOLS/openspin/build/openspin
SPINSIM=$TOOLS/spinsim/build/spinsim
P1RTL=$TOOLS/p1rtl/p1rtl
CORE=../../../src/cpu/p8x32a
DEV=../../../src/wpc/pinheck
CC="cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic"
fail=0 pass=0

for c in $CORE/*.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c; do
	[ -e "$c" ] || continue
	cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only -I$CORE -I$DEV "$c" || { echo "C89 FAIL $c"; fail=$((fail + 1)); }
	# the xpinmame build lists split INLINE="static __inline__" (Linux) / "static __inline" (macOS) into macros
	for std in c89 gnu99; do
		cc -std=$std -Wno-long-long -D__inline__ -fsyntax-only -I$CORE -I$DEV "$c" || { echo "XPINMAME FAIL $c ($std; __inline__ is a macro there)"; fail=$((fail + 1)); }
	done
	# macOS's list makes __inline a macro too (glibc's own headers use it, so it cannot be defined here): no such token
	sed 's|/\*.*\*/||g' "$c" | grep -nw '__inline' && { echo "XPINMAME FAIL $c (__inline is a macro in the macOS build)"; fail=$((fail + 1)); }
done
mkdir -p $B/rtl $B/spin $B/boot
if [ -f ../eeprom/eeprom_test.c ]; then
	$CC -I$DEV -o $B/eeprom_test ../eeprom/eeprom_test.c $DEV/eeprom.c || exit 2
	./$B/eeprom_test || fail=$((fail + 1))
fi
[ -f $CORE/p8x32a.c ] || { echo "p8x32a: core not present yet"; exit 2; }
$CC -I$CORE -I$DEV -o $B/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
# P8X32A_JIT=1: every test runs with local instruction runs translated (x86-64 only)
if [ "$P8X32A_JIT" = 1 ]; then
	ASMJIT=../../../ext/asmjit
	JF="-O2 -std=c++17 -DASMJIT_STATIC -DASMJIT_NO_FOREIGN -DASMJIT_NO_UJIT -I$ASMJIT"
	mkdir -p $B/asmjit
	if [ ! -f $B/asmjit/libasmjit.a ]; then
		for f in $ASMJIT/asmjit/core/*.cpp $ASMJIT/asmjit/x86/*.cpp $ASMJIT/asmjit/support/*.cpp; do
			c++ $JF -c "$f" -o $B/asmjit/$(basename "$f" .cpp).o || exit 2
		done
		ar rcs $B/asmjit/libasmjit.a $B/asmjit/*.o || exit 2
	fi
	c++ $JF -Wall -Wextra -Werror -I$CORE -c $CORE/p8x32ajit.cpp -o $B/p8x32ajit.o || exit 2
	for f in run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c; do
		$CC -DP8X32A_JIT -I$CORE -I$DEV -c "$f" -o $B/jit-$(basename "$f" .c).o || exit 2
	done
	c++ -o $B/p8run $B/jit-*.o $B/p8x32ajit.o $B/asmjit/libasmjit.a -lz -lpthread || exit 2
	c++ $JF -Wall -Wextra -Werror -I$CORE -o $B/jit_oom_test jit_oom_test.cpp $B/p8x32ajit.o $B/asmjit/libasmjit.a -lpthread || exit 2
	./$B/jit_oom_test || fail=$((fail + 1))
	echo "p8run: translated"
fi
if [ -f dasm_test.c ]; then
	$CC -I$CORE -o $B/dasm_test dasm_test.c $CORE/p8x32adasm.c || exit 2
	./$B/dasm_test || fail=$((fail + 1))
fi
$CC -I$CORE -o $B/snap_test snap_test.c && ./$B/snap_test || fail=$((fail + 1))
if [ -f alu_test.c ]; then
	$CC -I$CORE -o $B/alu_test alu_test.c || exit 2
	./$B/alu_test || fail=$((fail + 1))
fi
for t in "$OPENSPIN" "$SPINSIM" "$P1RTL"; do
	[ -x "$t" ] || { echo "missing $t: run tools.sh first"; exit 2; }
done

compile() {
	$OPENSPIN -q "$1" -o "$2.binary" > "$2.clog" 2>&1 || { echo "COMPILE FAIL $1"; cat "$2.clog"; return 1; }
}

rtl_case() {
	o=$2/$(basename "$1" .spin)
	args=$(sed -n "s/^' ARGS: //p" "$1")
	compile "$1" "$o" || { fail=$((fail + 1)); return; }
	python3 mkrom.py "$o.binary" "$o.rom" "$o.ram"
	$P1RTL -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args -dump "$o.rtlhub" > "$o.rtl"
	sleeps=$(sed -n "s/^' EXPECT-SLEEPS: //p" "$1")
	lazies=$(sed -n "s/^' EXPECT-LAZY: //p" "$1")
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 ${sleeps:+-sleeps} ${lazies:+-lazies} -dump "$o.ourhub" > "$o.our" 2> "$o.log"
	if cmp -s "$o.rtl" "$o.our" && cmp -s "$o.rtlhub" "$o.ourhub"; then pass=$((pass + 1))
	else echo "RTL MISMATCH $1"; diff "$o.rtl" "$o.our" | head -6; fail=$((fail + 1)); fi
	exp=$(sed -n "s/^' EXPECT-LOG: //p" "$1")
	if [ -n "$exp" ] && ! grep -qF "$exp" "$o.log"; then echo "LOG MISSING $1: $exp"; fail=$((fail + 1)); fi
	if [ -n "$sleeps" ] && ! grep -qxF "p8run: $sleeps idle-loop sleeps" "$o.log"; then echo "SLEEPS $1: $(grep -F idle-loop "$o.log"), expected $sleeps"; fail=$((fail + 1)); fi
	if [ -n "$lazies" ] && ! grep -qxF "p8run: $lazies lazy cogs" "$o.log"; then echo "LAZY $1: $(grep -F 'lazy cogs' "$o.log"), expected $lazies"; fail=$((fail + 1)); fi
	if grep -q "p8x32a: lazy\|p8run: lazy" "$o.log"; then echo "LAZY $1: $(grep "p8x32a: lazy\|p8run: lazy" "$o.log" | head -3)"; fail=$((fail + 1)); fi
	# EXPECT-CLKSHIFT: d v = with CLKSET moving queued edges d cycles earlier, the long at $6000 is v
	set -- "$1" $(sed -n "s/^' EXPECT-CLKSHIFT: //p" "$1")
	[ $# -eq 3 ] || return
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args -clkshift "$2" -dump "$o.shifthub" > /dev/null 2>&1
	got=$(od -An -tu4 -j 24576 -N 4 "$o.shifthub" | tr -d ' ')
	if [ "$got" = "$3" ]; then pass=$((pass + 1)); else echo "CLKSHIFT $1: \$6000 = $got, expected $3"; fail=$((fail + 1)); fi
}

spin_case() {
	o=$2/$(basename "$1" .spin)
	compile "$1" "$o" || { fail=$((fail + 1)); return; }
	python3 mkrom.py --launch "$o.binary" "$o.lrom" "$o.lram"
	./$B/p8run -rom "$o.lrom" -ram "$o.lram" -halt -cycles 400000 -dump "$o.lhub" > /dev/null 2>&1
	rm -f "$o.sdump"
	SPINSIM_DUMP=6000,400,$o.sdump timeout -k 5 60 $SPINSIM "$o.binary" > /dev/null 2>&1
	dd if="$o.lhub" of="$o.lwin" bs=1024 skip=24 count=1 2> /dev/null
	if cmp -s "$o.sdump" "$o.lwin"; then pass=$((pass + 1))
	else echo "SPINSIM MISMATCH $1"; python3 cmpwin.py "$o.sdump" "$o.lwin"; fail=$((fail + 1)); fi
}

for f in chip/*.spin isa/*.spin; do [ -e "$f" ] && rtl_case "$f" $B/rtl; done
mkdir -p $B/rtl-q && rtl_case chip/waitext.spin $B/rtl-q "-quantum 1000"
rtl_case chip/idle_ina.spin $B/rtl-q "-quantum 1000"
if [ -f gen.py ]; then
	rm -rf $B/rtl/rand $B/spin/rand
	python3 gen.py --out $B/rtl/rand --count "$SEEDS" --hubflags --workers 0.3
	for f in $B/rtl/rand/*.spin; do rtl_case "$f" $B/rtl/rand; done
	python3 gen.py --out $B/spin/rand --count "$SEEDS"
	for f in $B/spin/rand/*.spin; do spin_case "$f" $B/spin/rand; done
fi
for f in isa/*.spin; do [ -e "$f" ] && spin_case "$f" $B/spin; done
# a lazy cog's pin changes past the hold buffer end the run (built with no room: lazy.spin holds one)
mkdir -p $B/lzh
$CC -DP8X32A_LZH_CAP=0 -I$CORE -I$DEV -o $B/lzh/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
if ./$B/lzh/p8run -rom $B/rtl/lazy.rom -ram $B/rtl/lazy.ram -halt -cycles 400000 -quantum 1000 > /dev/null 2> $B/lzh/log ||
   ! grep -q "lazy cog's pin changes overflow" $B/lzh/log; then
	echo "LAZY HOLD: overflow did not end the run"; fail=$((fail + 1))
else pass=$((pass + 1)); fi
# mutations of the lazy-cog core that the cases above must catch
TOOLS=$TOOLS ./mutate_lazy.sh || fail=$((fail + 1))

boot_case() {
	ext=$1
	python3 -c "import sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(d+b'\xff'*(131072-len(d)))" "$DOMINOS_PRP" $B/boot/eeprom.bin
	key=$(cat "$P8X32A_ROM" $B/boot/eeprom.bin | sha1sum | cut -c1-16)-$ext
	ref=$B/boot/$key
	if [ ! -s "$ref.rtl" ]; then
		echo "boot: building RTL reference for ext=$ext (about 2 minutes, cached afterwards)"
		$P1RTL -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext "$ext" -cycles 60000000 -stop 0 7c01 -dump "$ref.rtlhub" > "$ref.tmp" && mv "$ref.tmp" "$ref.rtl"
	fi
	./$B/p8run -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext "$ext" -cycles 60000000 -stop 0 7c01 -dump "$ref.ourhub" > "$ref.our"
	if ! grep -q "^S [0-9]* 0 0007c01$" "$ref.our"; then echo "BOOT FAIL ext=$ext: interpreter never started"; fail=$((fail + 1))
	elif cmp -s "$ref.rtl" "$ref.our" && cmp -s "$ref.rtlhub" "$ref.ourhub"; then
		pass=$((pass + 1)); echo "boot ext=$ext: $(wc -l < "$ref.our") trace lines match, interpreter start at $(sed -n 's/^S \([0-9]*\) 0 0007c01$/\1/p' "$ref.our")"
	else echo "BOOT MISMATCH ext=$ext"; diff "$ref.rtl" "$ref.our" | head -6; fail=$((fail + 1)); fi
}

sd_case() {
	python3 -c "import sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(d+b'\xff'*(131072-len(d)))" "$DOMINOS_PRP" $B/boot/eeprom.bin
	ref=$B/boot/sd-$(cat "$P8X32A_ROM" $B/boot/eeprom.bin "$DOMINOS_ZIP" | sha1sum | cut -c1-16)
	if [ ! -s "$ref.rtl" ]; then
		echo "sdboot: building RTL reference to cycle 48100000 (about 6 minutes, cached afterwards)"
		$P1RTL -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext 01000000 -cycles 48100000 -sd "$DOMINOS_ZIP" -dump "$ref.rtlhub" > "$ref.tmp" &&
			python3 nodac.py "$ref.tmp" > "$ref.rtl" && rm -f "$ref.tmp"
	fi
	./$B/p8run -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext 01000000 -cycles 48100000 -sd "$DOMINOS_ZIP" -dump "$ref.ourhub" > "$ref.raw"
	python3 nodac.py "$ref.raw" > "$ref.our"
	if cmp -s "$ref.rtl" "$ref.our" && cmp -s "$ref.rtlhub" "$ref.ourhub"; then
		pass=$((pass + 1)); echo "sdboot: $(wc -l < "$ref.our") trace lines and hub RAM match the RTL to cycle 48100000 (P14/P15 audio DUTY masked)"
	else echo "SDBOOT MISMATCH"; diff "$ref.rtl" "$ref.our" | head -6; fail=$((fail + 1)); fi
	./$B/p8run -rom "$P8X32A_ROM" -eeprom $B/boot/eeprom.bin -ext 01000000 -cycles 520000000 -notrace -sd "$DOMINOS_ZIP" > $B/boot/sd5s.trace 2> $B/boot/sd5s.log
	if [ -s $B/boot/sd5s.log ]; then echo "SDBOOT LOG:"; cat $B/boot/sd5s.log; fail=$((fail + 1))
	elif python3 sdcheck.py $B/boot/sd5s.trace "$DOMINOS_ZIP"; then pass=$((pass + 1))
	else fail=$((fail + 1)); fi
}

if [ -n "$P8X32A_ROM" ] && [ -n "$DOMINOS_PRP" ]; then
	crc=$(python3 -c "import zlib,sys; print('%08x' % (zlib.crc32(open(sys.argv[1],'rb').read()) & 0xffffffff))" "$P8X32A_ROM")
	[ "$crc" = f99b3070 ] || echo "note: P8X32A_ROM crc32 $crc, expected f99b3070"
	boot_case 0
	boot_case 80000000
	if [ -n "$DOMINOS_ZIP" ]; then sd_case; else echo "sdboot: skipped (set DOMINOS_ZIP to a romset zip to run it)"; fi
else
	echo "boot: skipped (set P8X32A_ROM and DOMINOS_PRP to run it)"
fi
echo "p8x32a: $pass passed, $fail failed"
[ $fail -eq 0 ]
