#!/bin/sh
# Headless benchmark and determinism check (Milestone 9).
#   bench.sh [attract] [video]     time each workload with $SDL3PINMAME; with $REFERENCE set, run it too
#                                  and require byte-identical UART1, frame, sound and NVRAM output
#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
[ -n "$REFERENCE" ] && { REFERENCE=$(realpath "$REFERENCE") || exit 2; }
cd "$(dirname "$0")" || exit 2
B=$PWD/build
FIXTIME=1790683200   # 2026-09-29 12:00:00 UTC
mkdir -p $B && cc -O2 -shared -fPIC -o $B/fixtime.so fixtime.c || exit 2
PERF=$(command -v perf)

# workload: frames to run, frame the timed window starts at, UART1 commands
spec() {
	case $1 in
	attract) FRAMES=2400 MARK=600 SEND_AT=0 SEND= ;;
	video) FRAMES=2400 MARK=960 SEND_AT=10 SEND='[E96000]~[V00AT9]~[F00ZM0]~[F00BWI]~[F00IF0]~[F00NBI]~~~~~~~~~~[F00NBI]~~~~[F00IF0]' ;;
	*) echo "bench: unknown workload $1"; exit 2 ;;
	esac
}

# machine directory for one build: romset, and NVRAM after a first boot in service (kept while the binary, mask ROM and romset are the same)
machine() {
	M=$B/$1
	sum=$(cat "$2" "$P8X32A_ROM" "$PINHECK_ZIP" | sha1sum)
	[ -s $M/nvram.base/dominos.nv ] && [ "$(cat $M/binary.sha 2> /dev/null)" = "$sum" ] && return
	rm -rf $M && mkdir -p $M/roms $M/cfg $M/nvram.base || exit 2
	cp "$P8X32A_ROM" $M/p8x32a.rom && (cd $M && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
	ln -s "$PINHECK_ZIP" $M/roms/dominos.zip || exit 2
	(cd $M && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 timeout -k 30 3000 "$2" dominos -rompath roms \
		-nvram_directory nvram.base -cfg_directory cfg -headless -frames_to_run 1200 -skip_gamewarnings -nothrottle > boot.out 2>&1) \
		|| { echo "BENCH FAIL: first boot with $2 exited $?"; exit 1; }
	echo "$sum" > $M/binary.sha
}

# run one workload with one build under perf stat, perf record (mode "record") or neither
run() {
	label=$1 bin=$2 w=$3
	spec $w
	machine $label $bin
	D=$B/$label/$w
	# the reference's runs are kept while its machine and the workload are the same
	key="$FRAMES $MARK $SEND_AT $SEND"
	if [ $label = ref ] && [ -s $D/bench.txt ] && [ "$(cat $D/spec.txt 2> /dev/null)" = "$key" ]; then cat $D/bench.txt; return; fi
	rm -rf $D && mkdir -p $D && cp -r $B/$label/nvram.base $D/nvram || exit 2
	echo "$key" > $D/spec.txt
	wrap=
	[ -n "$PERF" ] && wrap="$PERF stat -e task-clock,instructions:u,cycles:u -o $D/perf.txt"
	[ -n "$PERF" ] && [ "$4" = record ] && wrap="$PERF record -F 999 -o $D/perf.data --"
	echo "$MARK mark window" > $D/keys.txt
	t0=$(date +%s.%N)
	(cd $D && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 PINHECK_UART1_LOG=$D/uart.log \
		PINHECK_FRAME_LOG=$D/frames.bin PINHECK_WAV=$D/snd.wav PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND_GAP=1 PINHECK_UART1_SEND="$SEND" \
		timeout -k 30 5400 $wrap "$bin" dominos -rompath ../roms -nvram_directory nvram -cfg_directory ../cfg -headless \
		-frames_to_run $FRAMES -skip_gamewarnings -nothrottle -samplefreq 48000 -fakesound -key_script keys.txt > run.out 2>&1) \
		|| { echo "BENCH FAIL: $w with $bin exited $?"; tail -3 $D/run.out; exit 1; }
	t1=$(date +%s.%N)
	[ -s $D/keys.txt.marks ] || { echo "BENCH FAIL: $w never reached frame $MARK"; exit 1; }
	python3 - "$D" "$label" "$w" "$t0" "$t1" "$MARK" "$FRAMES" <<'PY'
import os, re, sys
d, label, w, t0, t1, mark, frames = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), float(sys.argv[5]), int(sys.argv[6]), int(sys.argv[7])
emu = (frames - 1 - mark) / 60.0          # -frames_to_run N emulates N-1 frames
wall = t1 - os.stat(d + '/keys.txt.marks').st_mtime
line = 'bench %s %s: %.3fx over %.1f s emulated (%.1f s wall)' % (w, label, emu / wall, emu, wall)
try:
    st = open(d + '/perf.txt').read()
    ins = float(re.search(r'([\d,]+)\s+instructions', st).group(1).replace(',', ''))
    task = float(re.search(r'([\d,.]+) msec task-clock', st).group(1).replace(',', '')) / 1000
    line += ', whole run %.1f G instructions and %.1f s CPU per emulated s' % (ins / 1e9 / ((frames - 1) / 60.0), task / ((frames - 1) / 60.0))
except (OSError, AttributeError):
    pass
line += ', load %s' % open('/proc/loadavg').read().split()[0]
print(line)
open(d + '/bench.txt', 'w').write(line + '\n')
PY
}

if [ "$1" = profile ]; then
	[ -n "$PERF" ] || { echo "bench: perf not found"; exit 2; }
	run prof "$SDL3PINMAME" "${2:-attract}" record || exit 1
	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort srcfile 2> /dev/null | python3 components.py
	exit 0
fi
[ $# -eq 0 ] && set -- attract video
fail=0
if [ -n "$REFERENCE" ]; then
	machine opt "$SDL3PINMAME"
	machine ref "$REFERENCE"
	if cmp -s $B/opt/nvram.base/dominos.nv $B/ref/nvram.base/dominos.nv; then echo "determinism first boot: identical nvram.base/dominos.nv"
	else echo "DETERMINISM FAIL first boot: nvram.base/dominos.nv differs from the reference build's"; fail=1; fi
fi
for w in "$@"; do
	run opt "$SDL3PINMAME" $w || exit 1
	[ -n "$REFERENCE" ] || continue
	run ref "$REFERENCE" $w || exit 1
	same=
	for f in uart.log frames.bin snd.wav nvram/dominos.nv; do
		if cmp -s $B/opt/$w/$f $B/ref/$w/$f; then same="$same $f"; else echo "DETERMINISM FAIL $w: $f differs from the reference build's"; fail=1; fi
	done
	echo "determinism $w: identical$same"
done
[ $fail -eq 0 ]
