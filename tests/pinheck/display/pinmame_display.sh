#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to the game's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
. ../games.sh
B=build/pinmame
CLIP=${PINHECK_CLIP:-$CLIP}
SEND_AT=${PINHECK_CLIP_AT:-12}
rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg $B/snap $B/snap2 || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/$GAME.zip || exit 2
launch() {
	(cd $B && PINHECK_INSERVICE=6 PINHECK_FRAME_LOG=$PWD/frames$1.bin PINHECK_UART1_LOG=$PWD/uart$1.log PINHECK_PROP_LOG=$PWD/prop$1.log \
		timeout -k 30 3000 "$SDL3PINMAME" $GAME -rompath roms -nvram_directory nvram -cfg_directory cfg -headless -frames_to_run $2 -skip_gamewarnings -nothrottle $3 > run$1.out 2>&1) \
		|| { echo "PINMAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
# F12 just before the end of launch 1: a screen snapshot for render.py
echo "1190 tap 2 KEYCODE_F12" > $B/snap.ks
launch 1 1200 "-key_script $PWD/$B/snap.ks -snapshot_directory $PWD/$B/snap"
# and at the end of launch 2, when attract mode has switched GI on and off: the core's solenoid log must show
echo "890 tap 2 KEYCODE_F12" > $B/snap2.ks
PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND="[V00$CLIP]" launch 2 900 "-key_script $PWD/$B/snap2.ks -snapshot_directory $PWD/$B/snap2"
fail=0
grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart2.log || { echo "PINMAME FAIL: no sync check"; fail=1; }
grep -aq "Playing Video" $B/uart2.log || { echo "PINMAME FAIL: [V00$CLIP] not acknowledged"; fail=1; }
grep -q "^display: config" $B/prop1.log || { echo "PINMAME FAIL: no display config packet"; fail=1; }
grep "^display: \(frame\|latch\|mode\)" $B/prop1.log $B/prop2.log && { echo "PINMAME FAIL: malformed display transfers"; fail=1; }
python3 frames.py $B/frames1.bin || fail=1
python3 render.py $B/snap/$GAME.png $B/frames1.bin --config "$(grep '^display: config ' $B/prop1.log | tail -1 | cut -d' ' -f3-)" || fail=1
dir=$(echo "$CLIP" | cut -c1)
python3 frames.py $B/frames2.bin --after "$SEND_AT" --vid "$PINHECK_UPDATE_DIR/DMD/_D$dir/$CLIP.VID" || fail=1
python3 render.py $B/snap2/$GAME.png $B/frames2.bin --sol-log --config "$(grep '^display: config ' $B/prop2.log | tail -1 | cut -d' ' -f3-)" || fail=1
[ $fail -eq 0 ] || exit 1
echo "pinmame display: ok"
