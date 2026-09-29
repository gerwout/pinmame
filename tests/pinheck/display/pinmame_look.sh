#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/look
rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg $B/snap $B/snap3 || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
python3 look.py plan $B || exit 2
launch() {
	(cd $B && PINHECK_INSERVICE=6 PINHECK_FRAME_LOG=$PWD/frames$1.bin PINHECK_PROP_LOG=$PWD/prop$1.log \
		timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -cfg_directory cfg -headless -frames_to_run $2 -skip_gamewarnings -nothrottle $3 > run$1.out 2>&1) \
		|| { echo "LOOK FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
# launch 1: first boot on blank NVRAM; launch 2 steps the display settings through every value;
# launch 3 restarts with the stored settings
launch 1 1200
launch 2 "$(cat $B/frames)" "-key_script keys.txt -snapshot_directory snap"
launch 3 1200 "-key_script snap3.txt -snapshot_directory snap3"
python3 look.py verify $B || exit 1
echo "pinmame look: ok"
