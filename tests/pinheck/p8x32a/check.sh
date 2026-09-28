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

for c in $CORE/*.c $DEV/eeprom.c; do
	[ -e "$c" ] || continue
	cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only -I$CORE -I$DEV "$c" || { echo "C89 FAIL $c"; fail=$((fail + 1)); }
done
mkdir -p $B/rtl $B/spin $B/boot
if [ -f ../eeprom/eeprom_test.c ]; then
	$CC -I$DEV -o $B/eeprom_test ../eeprom/eeprom_test.c $DEV/eeprom.c || exit 2
	./$B/eeprom_test || fail=$((fail + 1))
fi
[ -f $CORE/p8x32a.c ] || { echo "p8x32a: core not present yet"; exit 2; }
$CC -I$CORE -I$DEV -o $B/p8run run.c $CORE/p8x32a.c $DEV/eeprom.c || exit 2
if [ -f dasm_test.c ]; then
	$CC -I$CORE -o $B/dasm_test dasm_test.c $CORE/p8x32adasm.c || exit 2
	./$B/dasm_test || fail=$((fail + 1))
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
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 -dump "$o.ourhub" > "$o.our" 2> "$o.log"
	if cmp -s "$o.rtl" "$o.our" && cmp -s "$o.rtlhub" "$o.ourhub"; then pass=$((pass + 1))
	else echo "RTL MISMATCH $1"; diff "$o.rtl" "$o.our" | head -6; fail=$((fail + 1)); fi
	exp=$(sed -n "s/^' EXPECT-LOG: //p" "$1")
	if [ -n "$exp" ] && ! grep -qF "$exp" "$o.log"; then echo "LOG MISSING $1: $exp"; fail=$((fail + 1)); fi
}

spin_case() {
	o=$2/$(basename "$1" .spin)
	compile "$1" "$o" || { fail=$((fail + 1)); return; }
	python3 mkrom.py --launch "$o.binary" "$o.lrom" "$o.lram"
	./$B/p8run -rom "$o.lrom" -ram "$o.lram" -halt -cycles 400000 -dump "$o.lhub" > /dev/null 2>&1
	rm -f "$o.sdump"
	SPINSIM_DUMP=6000,400,$o.sdump timeout 60 $SPINSIM "$o.binary" > /dev/null 2>&1
	dd if="$o.lhub" of="$o.lwin" bs=1024 skip=24 count=1 2> /dev/null
	if cmp -s "$o.sdump" "$o.lwin"; then pass=$((pass + 1))
	else echo "SPINSIM MISMATCH $1"; python3 cmpwin.py "$o.sdump" "$o.lwin"; fail=$((fail + 1)); fi
}

for f in chip/*.spin isa/*.spin; do [ -e "$f" ] && rtl_case "$f" $B/rtl; done
mkdir -p $B/rtl-q && rtl_case chip/waitext.spin $B/rtl-q "-quantum 1000"
if [ -f gen.py ]; then
	rm -rf $B/rtl/rand $B/spin/rand
	python3 gen.py --out $B/rtl/rand --count "$SEEDS" --hubflags
	for f in $B/rtl/rand/*.spin; do rtl_case "$f" $B/rtl/rand; done
	python3 gen.py --out $B/spin/rand --count "$SEEDS"
	for f in $B/spin/rand/*.spin; do spin_case "$f" $B/spin/rand; done
fi
for f in isa/*.spin; do [ -e "$f" ] && spin_case "$f" $B/spin; done

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

if [ -n "$P8X32A_ROM" ] && [ -n "$DOMINOS_PRP" ]; then
	crc=$(python3 -c "import zlib,sys; print('%08x' % (zlib.crc32(open(sys.argv[1],'rb').read()) & 0xffffffff))" "$P8X32A_ROM")
	[ "$crc" = f99b3070 ] || echo "note: P8X32A_ROM crc32 $crc, expected f99b3070"
	boot_case 0
	boot_case 80000000
else
	echo "boot: skipped (set P8X32A_ROM and DOMINOS_PRP to run it)"
fi
echo "p8x32a: $pass passed, $fail failed"
[ $fail -eq 0 ]
