#!/bin/sh
for v in PINHECK_UPDATE_DIR PINHECK_ZIP P8X32A_ROM SDL3PINMAME; do
	eval "x=\$$v"
	if [ -n "$x" ]; then x=$(realpath "$x") || exit 2; eval "$v=\$x"; fi
done
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic"
mkdir -p $B
fail=0
cc -std=c99 -pedantic-errors -Werror=declaration-after-statement -Wno-long-long -fsyntax-only $S/wpc/pinheck/display.c || fail=$((fail + 1))
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
cc $CF $SAN -I$S/wpc/pinheck -o $B/display_test display_test.c $S/wpc/pinheck/display.c || exit 2
./$B/display_test || fail=$((fail + 1))
cc -std=c99 -pedantic-errors -Werror=declaration-after-statement -Wno-long-long -fsyntax-only $S/wpc/pinheck/dmd.c || fail=$((fail + 1))
cc $CF $SAN -I$S/wpc/pinheck -o $B/dmd_test dmd_test.c $S/wpc/pinheck/dmd.c || exit 2
./$B/dmd_test || fail=$((fail + 1))
cc $CF $SAN -I$S/wpc/pinheck -o $B/lookdump lookdump.c $S/wpc/pinheck/display.c || exit 2
python3 look.py crosscheck ./$B/lookdump || fail=$((fail + 1))
calls() { cc -E "$@" -I$S -I$S/wpc -I$S/unix -I$S/unix/sysdep $S/wpc/pinheck.c | grep -c 'pinheck_display_render(&disp_look'; }
if [ "$(calls)" = 1 ] && [ "$(calls -DLIBPINMAME)" = 0 ]; then
	echo "look: sdl3pinmame renders the look, libpinmame gets the frame as sent"
else
	echo "LOOK FAIL: the look must be in the standalone renderer and not in libpinmame's"; fail=$((fail + 1))
fi
if [ "$PINHECK_SKIP_FIRMWARE" != 1 ] && [ -n "$PINHECK_UPDATE_DIR" ] && [ -n "$P8X32A_ROM" ]; then
	zip=$PINHECK_ZIP
	if [ -z "$zip" ]; then
		zip=$PWD/$B/dominos-stored.zip
		[ -f "$zip" ] || (cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || exit 2
	fi
	cc $CF -I$S/wpc/pinheck -I$S/cpu/p8x32a -o $B/dispboot dispboot.c $S/wpc/pinheck/display.c $S/cpu/p8x32a/p8x32a.c \
		$S/wpc/pinheck/eeprom.c $S/wpc/pinheck/sd.c $S/wpc/pinheck/vfat.c $S/wpc/pinheck/zipsrc.c -lz || exit 2
	if timeout -k 30 600 ./$B/dispboot -rom "$P8X32A_ROM" -prp "$PINHECK_UPDATE_DIR/PRP_V008.BIN" -zip "$zip" > $B/dispboot.txt; then
		grep "^dispboot:" $B/dispboot.txt
	else
		echo "DISPBOOT FAIL"; grep -v "^[#.]*$" $B/dispboot.txt | tail -8; fail=$((fail + 1))
	fi
	if [ -n "$SDL3PINMAME" ]; then
		PINHECK_ZIP=$zip sh ./pinmame_display.sh || fail=$((fail + 1))
	fi
elif [ "$PINHECK_SKIP_FIRMWARE" != 1 ]; then
	echo "FIRMWARE MISSING: set PINHECK_UPDATE_DIR and P8X32A_ROM, or PINHECK_SKIP_FIRMWARE=1"; fail=$((fail + 1))
fi
echo "display: $fail failed"
[ $fail -eq 0 ]
