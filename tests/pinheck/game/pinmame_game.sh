#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update (for the .VID clips)}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
python3 game.py plan $B || exit 2
launch() {
	(cd $B && PINHECK_RTC=$(cat rtc) PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out$1.log PINHECK_UART1_LOG=$PWD/uart$1.log PINHECK_LINK_LOG=$PWD/link$1.log \
		PINHECK_FRAME_LOG=$PWD/frames$1.bin timeout -k 30 5400 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -cfg_directory cfg -headless \
		-frames_to_run $2 -skip_gamewarnings -nothrottle $3 > run$1.out 2>&1) \
		|| { echo "GAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
launch 1 1200
[ -s $B/link1.log ] || { echo "GAME FAIL: launch 1 logged no link packets (PINHECK_LINK_LOG)"; exit 1; }
# the sync check packet, command $23: bytes 1-14 count up from $30
grep -q ' 30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 23 gap' $B/link1.log || { echo "GAME FAIL: launch 1 logged no sync check packet"; exit 1; }
PINHECK_UART1_SEND_AT=10 PINHECK_UART1_SEND="$(cat $B/send)" launch 2 "$(cat $B/frames)" "-key_script keys.txt"
python3 game.py verify $B "$PINHECK_UPDATE_DIR/DMD" || exit 1
echo "pinmame game: ok"
