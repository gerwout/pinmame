#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
python3 check5.py plan $B || exit 2
launch() {
	(cd $B && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out$1.log PINHECK_UART1_LOG=$PWD/uart$1.log PINHECK_PROP_LOG=$PWD/prop$1.log \
		timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -cfg_directory cfg -headless -frames_to_run $2 -skip_gamewarnings -nothrottle $3 > run$1.out 2>&1) \
		|| { echo "BOARD FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
launch 1 1200
[ -s $B/out1.log ] || { echo "BOARD FAIL: launch 1 wrote no board log (PINHECK_OUT_LOG)"; exit 1; }
python3 check5.py timing $B/out1.log || exit 1
PINHECK_FRAME_LOG=$PWD/$B/frames2.bin PINHECK_UART1_SEND_AT=$(cat $B/send_at) PINHECK_UART1_SEND_GAP=$(cat $B/send_gap) PINHECK_UART1_SEND="$(cat $B/send)" \
	launch 2 "$(cat $B/frames)" "-key_script keys.txt"
python3 check5.py verify $B || exit 1
echo "pinmame board: ok"
