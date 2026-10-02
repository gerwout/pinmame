#!/bin/sh
# Mutations of the core that the RTL cases must catch: each mutant p8run (time-order checked; translated where the
# mutation is in the translator's path) must differ from the RTL, stop on a time-order fault, or miss a chip test's
# EXPECT-JITVAR, on one of chip/*.spin and gen.py's programs (SEEDS of each kind, default 100). Each case runs with
# p8run's quantum of 4,096 cycles and with one of 400,000 (one run_until for the whole run, as long as PinMAME's).
# P8X32A_JIT=1 runs the mutants in the translator's path (x86-64 only), else the others; MUTANTS names some of them.
# Each also runs from cycle 2^36 (p8run -t0) with both quanta, where a time or key cut to 32 bits shows.
set -u
cd "$(dirname "$0")" || exit 2
TOOLS=${TOOLS:-$PWD/build/tools}
SEEDS=${SEEDS:-100}
OPENSPIN=$TOOLS/openspin/build/openspin
P1RTL=$TOOLS/p1rtl/p1rtl
CORE=../../../src/cpu/p8x32a
DEV=../../../src/wpc/pinheck
ASMJIT=../../../ext/asmjit
JF="-O2 -std=c++17 -DASMJIT_STATIC -DASMJIT_NO_FOREIGN -DASMJIT_NO_UJIT -I$ASMJIT"
B=build/mutcore
MODE=0
if [ "${P8X32A_JIT:-0}" = 1 ]; then
	[ "$(uname -m)" = x86_64 ] || { echo "mutate_core: the translator needs x86-64"; exit 2; }
	MODE=1
fi
mkdir -p $B/case $B/obj build/asmjit
rm -rf $B/gen
python3 gen.py --out $B/gen --count "$SEEDS" --first 700001 --hubflags --workers 0.3
python3 gen.py --out $B/gen --count "$SEEDS" --first 800001 --cogs 3 --hubflags
cases="$(ls chip/*.spin) $(ls $B/gen/*.spin)"
for f in $cases; do
	o=$B/case/$(basename "$f" .spin)
	# the RTL's run is kept while the program is the same (gen.py writes the same programs again)
	[ -s "$o.rtl" ] && cmp -s "$f" "$o.spin" && continue
	rm -f "$o.spin"
	$OPENSPIN -q "$f" -o "$o.binary" > /dev/null 2>&1 || { echo "COMPILE FAIL $f"; exit 2; }
	python3 mkrom.py "$o.binary" "$o.rom" "$o.ram"
	$P1RTL -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 -dump "$o.rtlhub" > "$o.rtl" && cp "$f" "$o.spin"
done
if [ $MODE = 1 ] && [ ! -f build/asmjit/libasmjit.a ]; then
	for f in $ASMJIT/asmjit/core/*.cpp $ASMJIT/asmjit/x86/*.cpp $ASMJIT/asmjit/support/*.cpp; do
		c++ $JF -c "$f" -o build/asmjit/$(basename "$f" .cpp).o || exit 2
	done
	ar rcs build/asmjit/libasmjit.a build/asmjit/*.o || exit 2
fi
fail=0
# mutant NAME FILE FROM TO JIT: the core's FILE with FROM replaced by TO; JIT 1 builds p8run translated
mutant() {
	name=$1 file=$2 jit=$5
	[ "$jit" = $MODE ] || return 0
	# MUTANTS: only these (and the unmutated core)
	[ -z "${MUTANTS:-}" ] || case " $MUTANTS none-interpreted none-translated " in *" $name "*) ;; *) return 0 ;; esac
	cp $CORE/p8x32a.c $CORE/p8x32ajit.cpp $B/
	python3 - "$3" "$4" $CORE/$file $B/$file <<'PY' || { echo "MUTANT $name: no match"; fail=$((fail + 1)); return; }
import sys
frm, to, src, dst = sys.argv[1:]
s = open(src).read()
if frm and s.count(frm) != 1: sys.exit(1)
open(dst, 'w').write(s.replace(frm, to) if frm else s)
PY
	if [ "$jit" = 1 ]; then
		c++ $JF -I$CORE -c $B/p8x32ajit.cpp -o $B/obj/jit.o || exit 2
		for f in run.c $B/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c; do
			cc -O2 -std=c99 -DP8X32A_JIT -DP8X32A_CHECK -I$CORE -I$DEV -c "$f" -o $B/obj/$(basename "$f" .c).o || exit 2
		done
		c++ -o $B/p8run $B/obj/*.o build/asmjit/libasmjit.a -lz -lpthread || exit 2
	else
		cc -O2 -std=c99 -DP8X32A_CHECK -I$CORE -I$DEV -o $B/p8run run.c $B/p8x32a.c $DEV/eeprom.c $DEV/sd.c $DEV/vfat.c $DEV/zipsrc.c -lz || exit 2
	fi
	bad=0 first=
	for q in 4096 400000 t0 t0q; do
		for f in $cases; do
			o=$B/case/$(basename "$f" .spin)
			jv=$(sed -n "s/^' EXPECT-JITVAR: //p" "$f")
			qa="-quantum $q"
			[ $q = t0 ] && qa="-t0 68719476736"
			[ $q = t0q ] && qa="-t0 68719476736 -quantum 400000"
			# a mutant that loops for ever is caught too
			timeout -k 5 20 ./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $qa ${jv:+-jitvar} -dump "$o.mhub" > "$o.m" 2> "$o.mlog"
			if ! cmp -s "$o.rtl" "$o.m" || ! cmp -s "$o.rtlhub" "$o.mhub" || grep -q "time order" "$o.mlog" ||
			   { [ -n "$jv" ] && [ "$jit" = 1 ] && ! grep -qxF "p8run: $jv block lookups left to the interpreter" "$o.mlog"; }; then
				bad=$((bad + 1)); [ -n "$first" ] || first="$(basename "$f") at $q"
			fi
		done
	done
	if [ "${name%-*}" = none ]; then
		if [ $bad = 0 ]; then echo "unmutated core ($name): all cases match"; else echo "UNMUTATED CORE ($name) differs on $bad cases (first $first)"; fail=$((fail + 1)); fi
	elif [ $bad = 0 ]; then echo "MUTANT SURVIVED $name"; fail=$((fail + 1))
	else echo "mutant $name caught by $bad cases (first $first)"; fi
}
mutant none-interpreted p8x32a.c '' '' 0
mutant none-translated p8x32a.c '' '' 1
# a pin read that sends the pins ahead of the other cogs (the cause of Plan 12b's lost bits)
mutant ina-ahead p8x32a.c '	if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);
	flush(p, t);' '	if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);
	flush(p, t + 64);' 0
# a hub long write that leaves its bytes as they were, run ahead of the other cogs' earlier events: only a read of
# them that runs after it but is earlier in time shows it
mutant ahead-samewrite p8x32a.c '(h << 4 | (uint64_t)n) >= lim ||' \
	'((h << 4 | (uint64_t)n) >= lim && (FWR(ix) || op != 2 || (e->fl & (F_IMM | F_SPEC)) || rd32(p, ram[e->src] & 0xFFFC) != ram[e->dst])) ||' 0
# a reloaded cog's changed words kept, its blocks kept (the load's writes mark its words as changing)
mutant restart-jvar p8x32a.c '		memset(p->jvar[n], 0, sizeof(p->jvar[n]));' '' 1
mutant restart-drop p8x32a.c '		jit_drop(p, n);
		memset(p->jvar[n]' '		memset(p->jvar[n]' 1
# the scheduler's key of the cog that ran left as it was when its next event is a hub operation; another cog's
# event moved without sched_gen counting it: by a system operation, a sleeper woken by a pin change or a hub write
mutant keys-ran p8x32a.c '			uint64_t k = ev_key(b, best);' '			uint64_t k = b->ev == EV_HUB ? bk : ev_key(b, best);' 0
mutant gen-sys p8x32a.c '	p->sched_gen++;
	while (newx' '	while (newx' 0
mutant gen-notify p8x32a.c '(pins & p->loop[n].wake) && t < p->cog[n].ev_t) { p->cog[n].ev_t = t; p->sched_gen++; }' \
	'(pins & p->loop[n].wake) && t < p->cog[n].ev_t) { p->cog[n].ev_t = t; }' 0
mutant gen-hubwrite p8x32a.c 'h < p->cog[n].ev_t) { p->cog[n].ev_t = h; p->sched_gen++; }' 'h < p->cog[n].ev_t) { p->cog[n].ev_t = h; }' 0
# a translated CNT four cycles late; PAR not shifted
mutant jit-cnt p8x32ajit.cpp '(int)(4 * k)));
				a.mov(x86::ecx, x86::eax);' '(int)(4 * k + 4)));
				a.mov(x86::ecx, x86::eax);' 1
mutant jit-par p8x32a.c 'st.par = (c->ptr >> 14) << 2;' 'st.par = c->ptr << 2;' 1
# a translated hub read or write: its key cut to 32 bits (only times past 2^28 show it), the sched_gen test dropped,
# a read into a fixed slot of a block not reported
mutant hub-key32 p8x32ajit.cpp '			a.mov(x86::edi, stf(offsetof(p8x32a_jst, n)));
			a.or_(x86::rsi, x86::rdi);' '			a.or_(x86::esi, stf(offsetof(p8x32a_jst, n)));' 1
mutant hub-gen p8x32ajit.cpp '			a.cmp(x86::esi, stf(offsetof(p8x32a_jst, gen)));
			a.jne(wait);' '' 1
# the hub decision's test of the hub cycle against the run's end, of the slot against the cog's stop
mutant hub-end p8x32ajit.cpp '			a.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, t)));
			a.ja(wait);' '' 1
mutant hub-stop p8x32ajit.cpp '			a.lea(x86::rsi, x86::ptr(x86::rax, 5));
			a.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, dis)));
			a.jae(wait);' '' 1
mutant hub-report p8x32ajit.cpp '			a.bt(x86::dword_ptr(x86::rdi, (int)((dst >> 5) * 4)), dst & 31);
			a.jnc(same);' '			a.jmp(same);' 1
# a block whose link outlives it; a block reached from another's exit that stops before its first slot without its
# exit state
mutant link-void p8x32a.c '	p->jlink[n][a].len = ~0u;' '' 1
# a run that changed more than one fixed code slot leaves the blocks in place
mutant inv-many p8x32a.c '					for (j = 0; j < 512; j++)
						if (p->jblk[n][j]) jit_void(p, n, j);' '' 1
mutant exit-k0 p8x32ajit.cpp '				a.mov(stf(offsetof(p8x32a_jst, px)), base);
				a.mov(stf(offsetof(p8x32a_jst, nix)), CURW);' '				a.mov(stf(offsetof(p8x32a_jst, nix)), CURW);' 1
exit $((fail != 0))
