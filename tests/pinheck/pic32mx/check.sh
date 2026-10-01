#!/bin/sh
if [ -n "$PINHECK_UPDATE_DIR" ]; then PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2; fi
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic -I$S/cpu/pic32mx"
mkdir -p $B
fail=0
cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/cpu/pic32mx/pic32mx.c || { echo "C89 FAIL pic32mx.c"; fail=$((fail + 1)); }
cc $CF -o $B/soc_test soc_test.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c || exit 2
./$B/soc_test || fail=$((fail + 1))
# Intel HEX into program flash; AMH_HEX (America's Most Haunted's AMH_V023.hex) adds the real file
cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/hexload.c || { echo "C89 FAIL hexload.c"; fail=$((fail + 1)); }
cc $CF -fsanitize=address,undefined -fno-sanitize-recover=all -I$S/wpc/pinheck -o $B/hex_test hex_test.c $S/wpc/pinheck/hexload.c || exit 2
./$B/hex_test || fail=$((fail + 1))
if [ -f boot.c ]; then
	if [ -n "$PINHECK_UPDATE_DIR" ]; then
		cc $CF -o $B/pic32boot boot.c linkstub.c $S/wpc/pinheck/eeprom.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c || exit 2
		./$B/pic32boot -boots 2 -c 800000000 "$PINHECK_UPDATE_DIR/DOM_V006.PRG" > $B/boot.txt || { echo "BOOT FAIL: CPU exceptions during boot"; fail=$((fail + 1)); }
		python3 banner.py $B/boot.txt || fail=$((fail + 1))
		if [ -f pins.py ]; then python3 pins.py "$PINHECK_UPDATE_DIR/DOM_V006.PRG" || fail=$((fail + 1)); fi
	elif [ "$PINHECK_SKIP_FIRMWARE" != 1 ]; then
		echo "FIRMWARE MISSING: set PINHECK_UPDATE_DIR to the unzipped Domino's update, or PINHECK_SKIP_FIRMWARE=1"
		fail=$((fail + 1))
	fi
fi
./makefile_link_check.sh || fail=$((fail + 1))
python3 ../link/symbol_check.py ../../.. || fail=$((fail + 1))
# every PinMAME launch in a check can be killed: a plain SIGTERM may not stop the emulator
bad=$(grep -n '"[$]SDL3PINMAME" \(dominos\|pinheck\|[$]GAME\|"[$]name"\)' ../*/*.sh | grep -v 'timeout -k')
[ -z "$bad" ] || { echo "LAUNCH FAIL: PinMAME launched without timeout -k:"; echo "$bad"; fail=$((fail + 1)); }
# every timeout in a check can kill what it runs (emulators, machine programs, simulators)
bad=$(grep -n 'timeou[t] [0-9]' ../*/*.sh)
[ -z "$bad" ] || { echo "LAUNCH FAIL: timeout without -k:"; echo "$bad"; fail=$((fail + 1)); }
python3 ../link/vcxproj_check.py ../../../vcproj || fail=$((fail + 1))
echo "pic32mx: $fail failed"
[ $fail -eq 0 ]
