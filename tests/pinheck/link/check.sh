#!/bin/sh
if [ -n "$PINHECK_UPDATE_DIR" ]; then PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2; fi
if [ -n "$P8X32A_ROM" ]; then P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2; fi
if [ -n "$TOOLS" ]; then TOOLS=$(realpath "$TOOLS") || exit 2; fi
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
TOOLS=${TOOLS:-$PWD/../p8x32a/build/tools}
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic -pthread -I$S/cpu/pic32mx"
CORE="$S/wpc/pinheck/prop.c $S/wpc/pinheck/eeprom.c $S/cpu/p8x32a/p8x32a.c"
SDSRC="$S/wpc/pinheck/sd.c $S/wpc/pinheck/vfat.c $S/wpc/pinheck/zipsrc.c -lz"
[ -f $S/wpc/pinheck/zipsrc.c ] || { echo "PLAN 4 MISSING: src/wpc/pinheck/sd.c, vfat.c, zipsrc.c must exist (execute Plan 4 first)"; exit 2; }
mkdir -p $B
fail=0
for f in prop.c rtc.c bootldr.c; do [ -f $S/wpc/pinheck/$f ] || continue; cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/$f || { echo "C89 FAIL $f"; fail=$((fail + 1)); }; done
cc -std=c89 -pedantic-errors -Wno-long-long -Wall -Werror -DPINHECK_NO_THREADS -fsyntax-only $S/wpc/pinheck/prop.c || { echo "NO_THREADS FAIL prop.c"; fail=$((fail + 1)); }
if command -v clang > /dev/null; then python3 relax_check.py $S/wpc/pinheck/prop.c || fail=$((fail + 1)); else echo "relax: clang missing, skipped"; fi
[ -x "$TOOLS/openspin/build/openspin" ] || { echo "TOOLS MISSING: run ../p8x32a/tools.sh or set TOOLS"; exit 2; }
"$TOOLS/openspin/build/openspin" -q echo.spin -o $B/echo.binary > $B/echo.log 2>&1 || { cat $B/echo.log; exit 2; }
python3 ../p8x32a/mkrom.py $B/echo.binary $B/echo.rom $B/echo.ram || exit 2
cc $CF -o $B/prop_test prop_test.c $CORE || exit 2
timeout -k 5 300 ./$B/prop_test $B/echo.rom $B/echo.ram || fail=$((fail + 1))
# the worker thread's lifetime under AddressSanitizer
cc $CF -g -fsanitize=address -fno-omit-frame-pointer -o $B/prop_test_asan prop_test.c $CORE || exit 2
ASAN_OPTIONS=detect_leaks=0 timeout -k 5 300 ./$B/prop_test_asan $B/echo.rom $B/echo.ram > $B/prop_test_asan.log 2>&1 || { head -c 4000 $B/prop_test_asan.log; fail=$((fail + 1)); }
# and under ThreadSanitizer
cc $CF -O1 -g -fsanitize=thread -o $B/prop_test_tsan prop_test.c $CORE || exit 2
TSAN_OPTIONS=halt_on_error=1 timeout -k 5 300 ./$B/prop_test_tsan $B/echo.rom $B/echo.ram > $B/prop_test_tsan.log 2>&1 || { head -c 4000 $B/prop_test_tsan.log; fail=$((fail + 1)); }
# PINHECK_WINE=1: the Windows worker (x86-64 and i686, mingw-w64) under wine, in a private prefix
if [ "$PINHECK_WINE" = 1 ]; then
	for t in x86_64 i686; do
		$t-w64-mingw32-gcc -O2 -std=c99 -Wall -Wextra -Werror -static -pthread -I$S/cpu/pic32mx -o $B/prop_test_$t.exe prop_test.c $CORE || exit 2
		WINEPREFIX=$PWD/$B/wine WINEDEBUG=-all timeout -k 30 600 wine $B/prop_test_$t.exe $B/echo.rom $B/echo.ram > $B/prop_test_$t.log 2>&1
		tr -d '\r' < $B/prop_test_$t.log | grep -qx "prop: ok" || { echo "wine $t:"; tr -d '\r' < $B/prop_test_$t.log | grep -v "^[0-9a-f]*:\(err\|fixme\)" | head -20; fail=$((fail + 1)); }
	done
	WINEPREFIX=$PWD/$B/wine wineserver -k 2> /dev/null
	rm -rf $B/wine
fi
if [ -f bootldr_test.c ]; then
	cc $CF -o $B/bootldr_test bootldr_test.c $S/wpc/pinheck/bootldr.c || exit 2
	./$B/bootldr_test || fail=$((fail + 1))
fi
if [ -f rtc_test.c ]; then
	cc $CF -o $B/rtc_test rtc_test.c $S/wpc/pinheck/rtc.c || exit 2
	./$B/rtc_test || fail=$((fail + 1))
fi
if [ -f machine.c ]; then
	if [ -n "$PINHECK_UPDATE_DIR" ] && [ -n "$P8X32A_ROM" ]; then
		# a runaway writer in the machine check is capped (stdout and stderr each at 100 MB)
		printf '#!/bin/sh\nhead -c 300000000 /dev/zero\nhead -c 300000000 /dev/zero >&2\n' > $B/runaway.sh
		MACHINE_OUT=$B/runaway PINHECK_ZIP=echo.spin sh ./machine_check.sh "sh $B/runaway.sh" x x > /dev/null 2>&1
		for f in fresh.txt fresh.err update.txt update.err; do
			sz=$(stat -c %s $B/runaway/$f 2> /dev/null || echo missing)
			[ "$sz" != missing ] && [ "$sz" -le 104857600 ] || { echo "CAP FAIL: machine check $f is $sz bytes"; fail=$((fail + 1)); }
		done
		rm -rf $B/runaway $B/runaway.sh
		cc $CF -o $B/linkmachine machine.c $CORE $S/wpc/pinheck/rtc.c $S/wpc/pinheck/bootldr.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c $SDSRC || exit 2
		sh ./machine_check.sh "$B/linkmachine" "$P8X32A_ROM" "$PINHECK_UPDATE_DIR" || fail=$((fail + 1))
	elif [ "$PINHECK_SKIP_FIRMWARE" != 1 ]; then
		echo "FIRMWARE MISSING: set PINHECK_UPDATE_DIR and P8X32A_ROM, or PINHECK_SKIP_FIRMWARE=1"
		fail=$((fail + 1))
	fi
fi
echo "link: $fail failed"
[ $fail -eq 0 ]
