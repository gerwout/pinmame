#!/bin/sh
# Domino's through libpinmame: display, sound, outputs, switches and mechanics as a host receives them;
# with PINHECK_GAME=jetsons The Jetsons' 128x64 display and its sound only; with amh America's Most Haunted's raw DMD
: "${LIBPINMAME:?set LIBPINMAME to the built libpinmame.so}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to the game's romset zip}"
LIBPINMAME=$(realpath "$LIBPINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
. ../games.sh
S=../../../src
B=build/pinmame
rm -rf $B && mkdir -p $B/boot/roms $B/boot/nvram $B/boot/cfg || exit 2
c++ -std=c++20 -O1 -Wall -Wextra -Werror -I$S/libpinmame host.cpp "$LIBPINMAME" -Wl,-rpath,"$(dirname "$LIBPINMAME")" -pthread -o $B/host || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j boot/roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/boot/roms/$GAME.zip || exit 2
launch() {
	name=$1
	shift
	[ -d $B/$name ] || cp -r $B/boot $B/$name || exit 2
	rm -f $B/$name/*.log $B/$name/*.bin $B/$name/*.raw $B/$name/*.wav
	(cd $B/$name && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out.log PINHECK_FRAME_LOG=$PWD/frames.log PINHECK_WAV=$PWD/capture.wav PINHECK_PROP_LOG=$PWD/prop.log \
		PINHECK_DMD_LOG=$PWD/dmd.log PINHECK_UART1_LOG=$PWD/uart.log timeout -k 30 3000 ../host "$@" > run.out 2>&1) || { echo "VPX FAIL: $name exited $?"; tail -5 $B/$name/run.out; exit 1; }
}
fail=0
# the first boot writes the NVRAM every other launch starts from
launch boot $GAME 1200 .
python3 vpx.py displays $B/boot || fail=1
if [ $LOOK = dmd ]; then
	# the DMD's frames through the callback and the plugin path, with a clip playing
	python3 vpx.py dmd $B/boot || fail=1
	PINHECK_UART1_SEND_AT=10 PINHECK_UART1_SEND="[E96000]~~[V00$CLIP]" launch media -p $GAME 1200 .
	grep -aq "Playing Video" $B/media/uart.log || { echo "VPX FAIL: [V00$CLIP] not acknowledged"; fail=1; }
	python3 vpx.py dmd $B/media || fail=1
	# the raw frames the core makes for colorizers (PINMAME_DMD_MODE_RAW): the sum of the last 16 subframes, held to 15
	launch raw -D $GAME 600 .
	python3 vpx.py dmd $B/raw raw || fail=1
	# a second session of another game in the same process: no trace of the first one's Intel HEX
	if [ -n "$DOMINOS_ZIP" ]; then
		[ -d $B/twogames ] || cp -r $B/boot $B/twogames || exit 2
		ln -sf "$(realpath "$DOMINOS_ZIP")" $B/twogames/roms/dominos.zip || exit 2
		launch twogames -R -n dominos $GAME 300 .
		n=$(grep -c '^hex: ' $B/twogames/prop.log)
		[ "$n" = 1 ] && echo "sessions: the Intel HEX logged once over $GAME and dominos" || { echo "VPX FAIL: $n hex lines over $GAME and dominos"; fail=1; }
	else
		echo "sessions: DOMINOS_ZIP not set, the second game's session not run"
	fi
	[ $fail -eq 0 ] || exit 1
	echo "pinmame vpx: ok ($GAME: the raw DMD)"
	exit 0
fi
python3 vpx.py media $B/boot $B/boot/capture.wav || fail=1
if [ $GAME = jetsons ]; then
	# a video clip through the plugin message API, after The Jetsons' later sync
	PINHECK_UART1_SEND_AT=14 PINHECK_UART1_SEND="[V00$CLIP]" launch media -p $GAME 1500 .
	grep -aq "Playing Video" $B/media/uart.log || { echo "VPX FAIL: [V00$CLIP] not acknowledged"; fail=1; }
	python3 vpx.py media $B/media $B/media/capture.wav || fail=1
	[ $fail -eq 0 ] || exit 1
	echo "pinmame vpx: ok ($GAME: display and sound)"
	exit 0
fi
# a video clip, then a sound effect, through the plugin message API
PINHECK_UART1_SEND_AT=12 PINHECK_UART1_SEND="[V00LT5]~~~~[F00Z00]~~~[F00IR0]" launch media -p dominos 2100 .
grep -aq "Playing Video" $B/media/uart.log || { echo "VPX FAIL: [V00LT5] not acknowledged"; fail=1; }
python3 vpx.py media $B/media $B/media/capture.wav || fail=1
python3 ../audio/corr.py $B/media/api.wav "$PINHECK_UPDATE_DIR/SFX/_FI/IR0.wav" 22052.59 || fail=1
# console coil commands and the service menu's servo, lamp and RGB tests, the flippers pressed as a table does,
# once as a legacy host sees the outputs and once as physical outputs (Controller.SolMask(2) = 2)
python3 vpx.py plan $B || exit 2
cp $B/switches.txt $B/mech.txt $B/boot/ || exit 2
board() {
	PINHECK_UART1_SEND_AT=$(cat $B/send_at) PINHECK_UART1_SEND_GAP=$(cat $B/send_gap) PINHECK_UART1_SEND="$(cat $B/send)" \
		launch $1 -p -o -x $2 -s switches.txt dominos "$(cat $B/frames)" .
	python3 vpx.py outputs $B/$1 || fail=1
}
board board
board physout -P
# the service menu's servo test turns the Noid: HandleMechanics 1 simulates it, 2 leaves it to the table
launch mech1 -o -m 1 -s mech.txt dominos 1200 .
launch mech2 -o -m 2 -s mech.txt dominos 1200 .
python3 vpx.py mech $B/mech1 $B/mech2 || fail=1
# two sessions in one process, as a table's reset key does: the link log and its packet decoder
PINHECK_RTC=1790683200 PINHECK_LINK_LOG=$PWD/$B/restart/link.log launch restart -R dominos 900 .
python3 vpx.py restart $B/restart || fail=1
[ $fail -eq 0 ] || exit 1
echo "pinmame vpx: ok"
