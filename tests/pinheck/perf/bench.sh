#!/bin/sh
# Headless benchmark and determinism check (Milestone 9).
#   bench.sh [attract] [video]     time each workload with $SDL3PINMAME; with $REFERENCE set, run it too
#                                  and require byte-identical UART1, frame, sound and NVRAM output; each timing
#                                  also gives the slowest 100 ms of host time (PINHECK_TIME_LOG)
#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py
#   bench.sh contention [CASE...]  10 s of attract mode where CPUs are scarce (Linux, taskset; BENCH_CPU_A and BENCH_CPU_B,
#                                  default 2 and 4, name two allowed CPUs): on one CPU (one), on two CPUs while a busy
#                                  loop shares the first (busy), with the worker switched every second (flip); each
#                                  against the same run without the worker thread, byte-identical and at least 0.9x,
#                                  0.8x, - its speed. stall: throttled, the process stopped 0.3 s at 10 s emulated,
#                                  and the worker must stay on
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
	short) FRAMES=1200 MARK=600 SEND_AT=0 SEND= ;;
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

# run one workload with one build under perf stat, perf record (mode "record") or neither; BENCH_MACHINE names
# another label's machine directory, BENCH_CPUS the CPUs it runs on, BENCH_THROTTLE=1 runs it throttled
run() {
	label=$1 bin=$2 w=$3
	spec $w
	ml=${BENCH_MACHINE:-$label}
	machine $ml $bin
	D=$B/$label/$w
	# the reference's runs are kept while its machine and the workload are the same
	key="$FRAMES $MARK $SEND_AT $SEND"
	if [ $label = ref ] && [ -s $D/bench.txt ] && [ "$(cat $D/spec.txt 2> /dev/null)" = "$key" ]; then cat $D/bench.txt; return; fi
	rm -rf $D && mkdir -p $D && cp -r $B/$ml/nvram.base $D/nvram || exit 2
	echo "$key" > $D/spec.txt
	wrap=
	[ -n "$PERF" ] && wrap="$PERF stat -e task-clock,instructions:u,cycles:u -o $D/perf.txt"
	[ -n "$PERF" ] && [ "$4" = record ] && wrap="$PERF record -F 999 -o $D/perf.data --"
	echo "$MARK mark window" > $D/keys.txt
	t0=$(date +%s.%N)
	[ -n "$BENCH_CPUS" ] && wrap="taskset -c $BENCH_CPUS $wrap"
	thr=-nothrottle
	[ "$BENCH_THROTTLE" = 1 ] && thr=
	(cd $D && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 PINHECK_UART1_LOG=$D/uart.log PINHECK_PROP_LOG=$D/prop.log \
		PINHECK_FRAME_LOG=$D/frames.bin PINHECK_TIME_LOG=$D/time.log PINHECK_WAV=$D/snd.wav PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND_GAP=1 PINHECK_UART1_SEND="$SEND" \
		timeout -k 30 5400 $wrap "$bin" dominos -rompath $B/$ml/roms -nvram_directory nvram -cfg_directory $B/$ml/cfg -headless \
		-frames_to_run $FRAMES -skip_gamewarnings $thr -samplefreq 48000 -fakesound -key_script keys.txt > run.out 2>&1) \
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
# the slowest 100 ms (host time) of the timed window, from the per-vblank emulated and host times
try:
    ts = [tuple(map(float, l.split())) for l in open(d + '/time.log')]
    ts = [x for x in ts if x[0] >= mark / 60.0]
    worst, j = None, 0
    for i in range(len(ts)):
        while j < len(ts) and ts[j][1] - ts[i][1] < 0.1:
            j += 1
        if j == len(ts):
            break
        v = (ts[j][0] - ts[i][0]) / (ts[j][1] - ts[i][1])
        worst = v if worst is None or v < worst else worst
    line += ', worst 100 ms %.3fx' % worst
except (OSError, ValueError, TypeError):
    line += ', worst 100 ms unknown'
line += ', load %s' % open('/proc/loadavg').read().split()[0]
print(line)
open(d + '/bench.txt', 'w').write(line + '\n')
PY
}

if [ "$1" = profile ]; then
	[ -n "$PERF" ] || { echo "bench: perf not found"; exit 2; }
	run prof "$SDL3PINMAME" "${2:-attract}" record || exit 1
	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort srcfile 2> /dev/null | python3 components.py
	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort dso 2> /dev/null | awk '/\[JIT\]/ { s += $1 } END { printf "%6.1f%%  of it translated Propeller code\n", s }'
	exit 0
fi
# stop the emulator running in directory $1 for $3 s once its emulated time reaches $2 s (SIGSTOP, SIGCONT)
stop_once() {
	while :; do
		for c in /proc/[0-9]*/cwd; do
			p=${c#/proc/} p=${p%/cwd}
			[ "$(readlink $c 2> /dev/null)" = "$1" ] && [ "$(readlink /proc/$p/exe 2> /dev/null)" = "$SDL3PINMAME" ] || continue
			[ -s $1/time.log ] && awk -v at=$2 '{ e = $1 } END { exit !(e >= at) }' $1/time.log || continue
			kill -STOP $p && sleep $3 && kill -CONT $p && echo "stopped for $3 s at $(tail -1 $1/time.log)" > $1/stall.txt
			return
		done
		sleep 0.05
	done
}

if [ "$1" = contention ]; then
	command -v taskset > /dev/null || { echo "bench: taskset not found"; exit 2; }
	A=${BENCH_CPU_A:-2} C=${BENCH_CPU_B:-4} fail=0 hog= stopper=
	trap '[ -n "$hog" ] && kill $hog 2> /dev/null; [ -n "$stopper" ] && kill $stopper 2> /dev/null' EXIT
	trap 'exit 1' INT TERM
	export BENCH_MACHINE=opt
	shift
	[ $# -eq 0 ] && set -- one busy flip stall
	for m; do
		case $m in
		one) cpus=$A need=0.9 log='one CPU allowed' ;;
		busy) cpus=$A,$C need=0.8 log='worker thread off' ;;
		flip) cpus= need=0 log='worker thread switched' ;;
		stall) cpus= need=0 log='worker thread off' ;;
		*) echo "bench: unknown contention case $m"; exit 2 ;;
		esac
		if [ $m = busy ]; then taskset -c $A sh -c 'while :; do :; done' & hog=$!; fi
		[ $m = stall ] && export BENCH_THROTTLE=1
		BENCH_CPUS=$cpus PINHECK_THREADS=0 run ${m}0 "$SDL3PINMAME" short > /dev/null || exit 1
		if [ $m = flip ]; then PINHECK_THREAD_FLIP=1 run $m "$SDL3PINMAME" short > /dev/null || exit 1
		elif [ $m = stall ]; then
			rm -rf $B/$m/short
			stop_once $B/$m/short 10 0.3 & stopper=$!
			run $m "$SDL3PINMAME" short > /dev/null || exit 1
			wait $stopper
			stopper=
			unset BENCH_THROTTLE
		else BENCH_CPUS=$cpus run $m "$SDL3PINMAME" short > /dev/null || exit 1; fi
		if [ -n "$hog" ]; then kill $hog; wait $hog 2> /dev/null; hog=; fi
		s0=$(sed 's/.*: \([0-9.]*\)x over.*/\1/' $B/${m}0/short/bench.txt) s=$(sed 's/.*: \([0-9.]*\)x over.*/\1/' $B/$m/short/bench.txt)
		n=$(grep -c "$log" $B/$m/short/prop.log)
		echo "contention $m: ${s}x with the worker thread requested, ${s0}x without; \"$log\" $n times"
		for f in uart.log frames.bin snd.wav nvram/dominos.nv; do
			cmp -s $B/$m/short/$f $B/${m}0/short/$f || { echo "CONTENTION FAIL $m: $f differs from the run without the worker thread"; fail=1; }
		done
		if [ $m = stall ]; then
			[ -s $B/$m/short/stall.txt ] || { echo "CONTENTION FAIL stall: the process was not stopped"; fail=1; }
			[ $n -eq 0 ] || { echo "CONTENTION FAIL stall: \"$log\" logged"; fail=1; }
			continue
		fi
		[ $n -gt 0 ] || { echo "CONTENTION FAIL $m: \"$log\" not logged"; fail=1; }
		awk "BEGIN { exit !($s >= $need * $s0) }" || { echo "CONTENTION FAIL $m: slower than $need of the run without the worker thread"; fail=1; }
	done
	[ $fail -eq 0 ] && echo "contention: ok"
	exit $fail
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
