#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
TOOLS=${TOOLS:-$PWD/../p8x32a/build/tools}
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic"
mkdir -p $B
fail=0
cc $CF -I$S/cpu/p8x32a -I$S/wpc/pinheck -o $B/p8run ../p8x32a/run.c $S/cpu/p8x32a/p8x32a.c $S/wpc/pinheck/eeprom.c $S/wpc/pinheck/sd.c $S/wpc/pinheck/vfat.c $S/wpc/pinheck/zipsrc.c -lz || exit 2
[ -x "$TOOLS/openspin/build/openspin" ] || { echo "TOOLS MISSING: build them with ../p8x32a/tools.sh or set TOOLS"; exit 2; }
$TOOLS/openspin/build/openspin -q duty.spin -o $B/duty.binary > $B/duty.clog 2>&1 || { echo "COMPILE FAIL duty.spin"; cat $B/duty.clog; exit 2; }
python3 ../p8x32a/mkrom.py $B/duty.binary $B/duty.rom $B/duty.ram
rm -f $B/duty.ctr
./$B/p8run -rom $B/duty.rom -ram $B/duty.ram -halt -cycles 400000 -ctrlog $B/duty.ctr > $B/duty.our 2> $B/duty.log
if cmp -s duty.ctr.expect $B/duty.ctr; then echo "sink: ok"; else echo "SINK MISMATCH"; diff duty.ctr.expect $B/duty.ctr | head -6; fail=$((fail + 1)); fi
if [ -f audio_test.c ]; then
	cc -std=c99 -pedantic-errors -Werror=declaration-after-statement -Wno-long-long -fsyntax-only -I$S/wpc/pinheck $S/wpc/pinheck/audio.c || { echo "C FAIL audio.c"; fail=$((fail + 1)); }
	cc $CF -I$S/wpc/pinheck -o $B/audio_test audio_test.c $S/wpc/pinheck/audio.c -lm || exit 2
	./$B/audio_test || fail=$((fail + 1))
fi
if [ -f dutywin.c ]; then
	cc $CF -I$S/wpc/pinheck -o $B/dutywin dutywin.c $S/wpc/pinheck/audio.c -lm || exit 2
	[ -s $B/duty.rtl ] || timeout -k 30 600 $TOOLS/p1rtl/p1rtl -rom $B/duty.rom -ram $B/duty.ram -halt -cycles 400000 -dump $B/duty.rtlhub > $B/duty.rtl
	./$B/dutywin $B/duty.rtl $B/duty.our $B/duty.ctr 500 || fail=$((fail + 1))
fi
echo "audio: $fail failed"
[ $fail -eq 0 ]
