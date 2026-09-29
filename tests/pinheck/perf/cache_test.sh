#!/bin/sh
# bench.sh reuses a machine directory and the reference's runs only while the binary, mask ROM, romset and workload are the same.
cd "$(dirname "$0")" || exit 2
T=$PWD/build/cache_test
rm -rf "$T" && mkdir -p "$T/perf" || exit 2
cp bench.sh fixtime.c "$T/perf/" || exit 2
cat > "$T/stub" <<'STUB'
#!/bin/sh
nv= ks= prev=
for a; do case $prev in -nvram_directory) nv=$a ;; -key_script) ks=$a ;; esac; prev=$a; done
echo "$(basename "$(dirname "$PWD")")/$(basename "$PWD")" >> "$STUB_LOG"
mkdir -p "$nv" && echo nv > "$nv/dominos.nv"
[ -n "$ks" ] && echo mark > "$ks.marks" && touch -d "1 second ago" "$ks.marks"
for f in "$PINHECK_UART1_LOG" "$PINHECK_FRAME_LOG" "$PINHECK_WAV"; do [ -n "$f" ] && echo x > "$f"; done
exit 0
STUB
chmod +x "$T/stub"
printf rom1 > "$T/rom1"; printf rom2 > "$T/rom2"; printf zip1 > "$T/zip1"; printf zip2 > "$T/zip2"
fail=0

# bench case: the reference's runs in this bench, then whether they must be fresh ones
bench() {
	name=$1 rom=$2 zip=$3 want=$4
	: > "$T/log"
	SDL3PINMAME=$T/stub REFERENCE=$T/stub P8X32A_ROM=$T/$rom PINHECK_ZIP=$T/$zip STUB_LOG=$T/log \
		sh "$T/perf/bench.sh" attract > "$T/$name.out" 2>&1 || { echo "CACHE FAIL $name: bench exited $?"; cat "$T/$name.out"; fail=1; return; }
	got=$(grep -c '^ref/attract$' "$T/log")
	[ "$got" = "$want" ] || { echo "CACHE FAIL $name: $got reference attract runs, expected $want"; fail=1; }
}

bench first rom1 zip1 1
bench same rom1 zip1 0
bench romset rom1 zip2 1
bench maskrom rom2 zip2 1
sed -i 's/attract) FRAMES=2400/attract) FRAMES=2401/' "$T/perf/bench.sh"
bench workload rom2 zip2 1
[ $fail -eq 0 ] && echo "bench cache: reused only for the same binary, mask ROM, romset and workload"
rm -rf "$T"
exit $fail
