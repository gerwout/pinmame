#!/bin/sh
run=$1 rom=$2 upd=$3
B=build
zip=${PINHECK_ZIP:-$B/dominos-stored.zip}
if [ ! -f "$zip" ]; then
	echo "machine: building $zip from $upd (stored, once)"
	(cd "$upd" && zip -q -0 -r "$OLDPWD/$zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || { echo "MACHINE FAIL: cannot build $zip"; exit 1; }
fi
args="-zip $zip -rom $rom -prp $upd/PRP_V008.BIN $upd/DOM_V006.PRG"
timeout 3000 $run -v -boots 3 -first 11000000000 -c 1040000000 -send 900000000 '[E97000]' $args > $B/fresh.txt 2> $B/fresh.err &
timeout 3000 $run -v -inservice 6 -updatecode -stale -boots 2 -first 80000000 -c 11000000000 $args > $B/update.txt 2> $B/update.err &
wait
fail=0
for f in fresh update; do
	for want in "boot: sign-on" "boot: leave, 204288 bytes programmed" "eeprom: \$8000=0600bafa \$8004=abba0002" "flash_matches_prg=1" "exceptions=0" "prop: CLKSET 6f"; do
		grep -qF "$want" $B/$f.err || { echo "MACHINE FAIL ($f): stderr lacks '$want'"; fail=$((fail + 1)); }
	done
done
awk '/=== BOOT 1 ===/{f=1;next} /=== BOOT 2 ===/{f=0} f' $B/fresh.txt | grep -aq "PROPELLER SYNC CHECK" || { echo "MACHINE FAIL: the application did not start after the update"; fail=$((fail + 1)); }
awk '/=== BOOT 1 ===/{f=1;next} /=== BOOT 2 ===/{f=0} f' $B/fresh.txt | grep -aq "CHECK\.*OK" && { echo "MACHINE FAIL: the Propeller served the link during its update"; fail=$((fail + 1)); }
t1=$(sed -n 's/^boot 1: first UART1 byte at //p' $B/fresh.err | tr -d s)
r1=$(sed -n 's/^boot 1: bootloader released the application at //p' $B/fresh.err | tr -d s)
awk -v t="$t1" -v r="$r1" 'BEGIN { exit !(r != "" && r > 100 && t >= r) }' || { echo "MACHINE FAIL: the application printed at ${t1}s, before the update released it (release ${r1:-none}s)"; fail=$((fail + 1)); }
for n in 2 3; do
	t=$(sed -n "s/^boot $n: first UART1 byte at //p" $B/fresh.err | tr -d s)
	awk -v t="$t" 'BEGIN { exit !(t >= 3.25 && t < 3.35) }' || { echo "MACHINE FAIL: boot $n printed at ${t}s, not 0.3 s after the 3 s window"; fail=$((fail + 1)); }
done
python3 ../pic32mx/banner.py $B/fresh.txt 2 || fail=$((fail + 1))
grep -aq "Ball Search: DISABLED" $B/fresh.txt || { echo "MACHINE FAIL: no reply to [E97000]"; fail=$((fail + 1)); }
tail -1 $B/fresh.err
[ $fail -eq 0 ] && echo "machine: ok"
[ $fail -eq 0 ]
