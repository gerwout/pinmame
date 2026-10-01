#!/bin/sh
# bench.sh contention stopped by SIGTERM or SIGINT leaves no busy loop behind.
cd "$(dirname "$0")" || exit 2
T=$PWD/build/trap_test
rm -rf "$T" && mkdir -p "$T/perf" || exit 2
cp bench.sh fixtime.c "$T/perf/" || exit 2
cat > "$T/stub" <<'STUB'
#!/bin/sh
nv= ks= prev=
for a; do case $prev in -nvram_directory) nv=$a ;; -key_script) ks=$a ;; esac; prev=$a; done
mkdir -p "$nv" && echo nv > "$nv/dominos.nv"
[ -n "$ks" ] && echo mark > "$ks.marks"
for f in "$PINHECK_UART1_LOG" "$PINHECK_FRAME_LOG" "$PINHECK_WAV" "$PINHECK_PROP_LOG"; do [ -n "$f" ] && echo x > "$f"; done
sleep 3
STUB
chmod +x "$T/stub" || exit 2
printf rom > "$T/rom"; printf zip > "$T/zip"
fail=0
for sig in TERM INT; do
	# SIGINT: a background job ignores it unless the shell runs with job control
	sh -mc "SDL3PINMAME=$T/stub P8X32A_ROM=$T/rom PINHECK_ZIP=$T/zip BENCH_CPU_A=0 BENCH_CPU_B=0 exec sh $T/perf/bench.sh contention busy" > "$T/$sig.out" 2>&1 &
	b=$!
	hog=
	for k in $(seq 1 600); do
		hog=$(pgrep -P $b -f 'while :; do :; done') && break
		sleep 0.05
	done
	[ -n "$hog" ] || { echo "TRAP FAIL $sig: no busy loop started"; cat "$T/$sig.out"; kill $b; fail=1; continue; }
	kill -$sig $b
	wait $b
	sleep 0.2
	if kill -0 $hog 2> /dev/null; then echo "TRAP FAIL $sig: the busy loop $hog runs on after bench.sh"; kill $hog; fail=1; fi
done
[ $fail -eq 0 ] && echo "bench trap: no busy loop left after SIGTERM or SIGINT"
rm -rf "$T"
exit $fail
