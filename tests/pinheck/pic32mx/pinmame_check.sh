#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
[ -n "$PINHECK_ZIP" ] && { PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2; }
cd "$(dirname "$0")" || exit 2
. ../games.sh
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
if [ -n "$PINHECK_ZIP" ]; then
	ln -s "$PINHECK_ZIP" $B/roms/$GAME.zip || exit 2
else
	(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/$GAME.zip" $PRG $PRP DMD SFX) || exit 2
fi
launch() {
	(cd $B && PINHECK_UART1_LOG=$PWD/uart$1.log PINHECK_PROP_LOG=$PWD/prop$1.log timeout -k 30 3000 "$SDL3PINMAME" $GAME -rompath roms -nvram_directory nvram -cfg_directory cfg \
		-headless -frames_to_run $2 -skip_gamewarnings -nothrottle > run$1.out 2>&1) || { echo "PINMAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
# the first boot flashes the update; the Propeller then asks for a restart, which comes 12 s later
PINHECK_RESET_AT=$UPDATED_AT launch 1 $(((UPDATED_AT + 12) * 60))
PINHECK_UART1_SEND_AT=11.5 PINHECK_UART1_SEND='[E97000]' launch 2 900
fail=0
for want in "boot: sign-on 0" "boot: $UPDATE_END, $PROGRAMMED bytes programmed" "prop: CLKSET 6f"; do
	grep -qF "$want" $B/prop1.log || { echo "PINMAME FAIL: launch 1 lacks '$want'"; fail=1; }
done
grep -q "boot: sign-on" $B/prop2.log && { echo "PINMAME FAIL: launch 2 ran the update again"; fail=1; }
n=$(grep -c "prop: CLKSET 80" $B/prop2.log)
[ "$n" = "$REBOOTS" ] || { echo "PINMAME FAIL: the Propeller rebooted $n times before the sync of launch 2, expected $REBOOTS"; fail=1; }
grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart1.log || { echo "PINMAME FAIL: no sync after the update and reset"; fail=1; }
python3 banner.py uart $B/uart2.log "PROPELLER SYNC CHECK|$STORED|$BANNER|Ball Search: DISABLED" || fail=1
grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart2.log || { echo "PINMAME FAIL: launch 2 sync check did not pass"; fail=1; }
[ -s $B/nvram/$GAME.nv ] || { echo "PINMAME FAIL: no NVRAM written"; fail=1; }
if [ $fail -ne 0 ]; then cat -v $B/uart2.log | grep -av "EEPROM\|Checksum\|Timeout\|Read OK\|Write\|Exchanging" | tail -20; exit 1; fi
./romset_check.sh || exit 1
echo "pinmame: ok"
