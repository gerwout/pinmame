#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
[ -n "$PINHECK_ZIP" ] && { PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2; }
SFX=${PINHECK_SFX:-ATD}
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
if [ -n "$PINHECK_ZIP" ]; then
	ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
else
	(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/dominos.zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || exit 2
fi
(cd $B && PINHECK_INSERVICE=1 PINHECK_RESET_AT=10 PINHECK_UART1_LOG=$PWD/uart.log PINHECK_PROP_LOG=$PWD/prop.log PINHECK_WAV=$PWD/capture.wav \
	PINHECK_UART1_SEND_AT=11.5 PINHECK_UART1_SEND="[F00Z00]~~~[F00$SFX]" timeout 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram \
	-headless -frames_to_run 1740 -skip_gamewarnings -nothrottle -samplefreq 48000 -fakesound > run.out 2>&1) || { echo "AUDIO FAIL: PinMAME exited $?"; tail -5 $B/run.out; exit 1; }
fail=0
grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart.log || { echo "AUDIO FAIL: no sync check"; fail=1; }
grep -aqF "Playing SFX" $B/uart.log || { echo "AUDIO FAIL: firmware did not echo Playing SFX"; fail=1; }
grep -aqF "not modelled" $B/prop.log && { echo "AUDIO FAIL: unmodelled audio path:"; grep -aF "not modelled" $B/prop.log; fail=1; }
python3 corr.py $B/capture.wav "$PINHECK_UPDATE_DIR/SFX/_F$(echo $SFX | cut -c1)/$SFX.wav" 22052.59 || fail=1
[ $fail -eq 0 ] && echo "pinmame audio: ok"
[ $fail -eq 0 ]
