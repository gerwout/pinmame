#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
[ -n "$PINHECK_ZIP" ] && { PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2; }
SFX=${PINHECK_SFX:-IR0}
cd "$(dirname "$0")" || exit 2
fail=0

run() {
	B=build/pinmame/$1
	rm -rf $B && mkdir -p $B/roms $B/nvram || exit 2
	cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
	if [ -n "$PINHECK_ZIP" ]; then
		ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
	else
		(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/dominos.zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || exit 2
	fi
}

check_rate() {
	run $1
	(cd $B && PINHECK_INSERVICE=1 PINHECK_RESET_AT=10 PINHECK_UART1_LOG=$PWD/uart.log PINHECK_PROP_LOG=$PWD/prop.log PINHECK_WAV=$PWD/capture.wav \
		PINHECK_SND_LAG=$PWD/lag.log PINHECK_UART1_SEND_AT=11.5 PINHECK_UART1_SEND="[F00Z00]~~~[F00$SFX]" timeout 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram \
		-headless -frames_to_run 1860 -skip_gamewarnings -nothrottle -samplefreq $1 -fakesound > run.out 2>&1) || { echo "AUDIO FAIL ($1 Hz): PinMAME exited $?"; tail -5 $B/run.out; fail=1; return; }
	grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart.log || { echo "AUDIO FAIL ($1 Hz): no sync check"; fail=1; }
	grep -aqF "Playing SFX" $B/uart.log || { echo "AUDIO FAIL ($1 Hz): firmware did not echo Playing SFX"; fail=1; }
	grep -aqF "not modelled" $B/prop.log && { echo "AUDIO FAIL ($1 Hz): unmodelled audio path:"; grep -aF "not modelled" $B/prop.log; fail=1; }
	python3 lag.py $B/lag.log $1 || fail=1
	python3 corr.py $B/capture.wav "$PINHECK_UPDATE_DIR/SFX/_F$(echo $SFX | cut -c1)/$SFX.wav" 22052.59 > $B/corr.out || fail=1
	cat $B/corr.out
}

check_rate 48000
check_rate 22050
python3 - build/pinmame/48000/corr.out build/pinmame/22050/corr.out <<'PY' || fail=1
import re, sys
at = [float(re.search(r' at ([0-9.]+) s', open(p).read()).group(1)) for p in sys.argv[1:]]
d = abs(at[0] - at[1]) * 1000
print('drift: clip starts at %.4f s (48000 Hz) and %.4f s (22050 Hz), %.2f ms apart' % (at[0], at[1], d))
sys.exit(0 if d <= 2.0 else 1)
PY

run selftest
(cd $B && PINHECK_SND_SELFTEST=1 PINHECK_PROP_LOG=$PWD/prop.log timeout 600 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram \
	-headless -frames_to_run 30 -skip_gamewarnings -nothrottle -samplefreq 48000 -fakesound > run.out 2>&1) || { echo "AUDIO FAIL (selftest): PinMAME exited $?"; fail=1; }
grep -aqF "audio: counter mode 2 on P15 not modelled" $B/prop.log || { echo "AUDIO FAIL: audio messages do not reach PINHECK_PROP_LOG"; fail=1; }

[ $fail -eq 0 ] && echo "pinmame audio: ok"
[ $fail -eq 0 ]
