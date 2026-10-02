# Plan 12d: America's Most Haunted's dispatch made cheaper (the scheduler's keys in order, hub operations at the ends of translated blocks, a link table and per-exit chains, run_local's linked path) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make each Propeller dispatch cheaper, not fewer, so that America's Most Haunted (`amh`) reaches real time with video and in play: every output byte-identical to the current build, every suite green.

**Architecture:** Four exact changes to the Propeller core and its translator, each with RTL tests and mutants, and the test tools they need. (1) *The scheduler keeps its keys in order* (a key holds its cog), so a dispatch takes the first two keys and puts the cog that ran back in its place, instead of scanning eight. (2) *A hub read or write ends a translated block*: the block decides as `event_run` would whether the operation runs now (its key below the other cogs' earliest, by the horizon and the stop time, `sched_gen` unchanged); a read runs in the block, a write through `hub_rw`; one that must wait is issued by `run_local` from the block's exit state, and when it is due the block's *resume entry* completes it and runs on, so a dispatch is one call into translated code. (3) *A link table and per-exit chains*: the blocks' exits find the next block through a 32-byte link per cog address (entry, first word and mask, length; a void link has length ~0) and jump to it from their own site, a backward jump calling a shared copy of `loop_edge`'s search step. (4) *run_local takes a linked block before decoding*: the link of the next address replaces the decode, the variance check and the block lookup when it matches. Tests: `p8run -t0` (a run from cycle 2^36, where a time or a key cut to 32 bits shows), every RTL case also from 2^36, two chip tests, six mutants, `mutate_core.sh`'s `MUTANTS` filter, and `bench.sh worker` (the worker thread's cycles per emulated second from a per-thread recording).

**Tech Stack:** C (C89-syntax-clean core), C++17 (asmjit translator), Spin/PASM test programs (openspin, the P1 Verilog RTL), Python 3 (no third-party modules), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §4 (Milestone 9: at least 1.0× on the reference machine, aim 1.2×; byte-identical determinism; every suite green including the RTL, spinsim and QEMU differentials), applied to America's Most Haunted. Measured reasons: Plan 12c's Result and "what remains" (with the profile-guided build 0.96× with video and 0.87× in play; its three Spin interpreters stop at almost every hub operation, about 5.8 million dispatches per emulated second).

## Prerequisites

- `pinheck` at `863dc6e9` (Plan 12c merged with its review fixes).
- Before starting, from the repository root, export what Plan 12c's checks use (`TOOLS`, `P8X32A_ROM`, `PINHECK_UPDATE_DIR`, `PINHECK_ZIP`, `DOMINOS_PRP`, `DOMINOS_ZIP`, `RZSPOOK_ZIP`, `RZSPOOK_UPDATE_DIR`, `JETSONS_ZIP`, `JETSONS_UPDATE_DIR`, `AMH_ZIP`, `AMH_UPDATE_DIR`, `AMH_HEX`, `SDL_AUDIO_DRIVER=dummy`) and define the game functions once:
  ```sh
  amh() { PINHECK_GAME=amh PINHECK_ZIP=$AMH_ZIP PINHECK_UPDATE_DIR=$AMH_UPDATE_DIR "$@"; }
  jet() { PINHECK_GAME=jetsons PINHECK_ZIP=$JETSONS_ZIP PINHECK_UPDATE_DIR=$JETSONS_UPDATE_DIR "$@"; }
  rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
  ```
  A Domino's run is the command without a function (`env`). Run the steps with `bash`.
- The machine is shared: only one emulator runs at a time machine-wide, so every step that runs PinMAME, libpinmame or a firmware program (the Propeller suite's boots included) goes through `flock /code/spooky_domino/work/emu.lock` (on another machine drop the `flock`); builds and heavy suites run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6`. Every PinMAME launch has a `timeout -k 30`, a private `-nvram_directory` and `-cfg_directory`, and no sound device (`SDL_AUDIO_DRIVER=dummy`); `bench.sh` and the machine checks do this themselves.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit. The scripts were generated from the prototype and replayed in this order on a fresh worktree of `863dc6e9`.
- Speeds are wall-clock on the Ryzen 7 7730U while other work shares it; the load average is printed with every speed. Instruction counts on one thread (`PINHECK_THREADS=0`) do not depend on the load. `bench.sh worker` counts the worker thread's cycles over a whole run (about ±0.5% between runs at the loads measured, against ±3–5% for wall-clock speeds).

## Global Constraints

- **Byte-identical:** every game's UART1 log, display frames (with a raw DMD its 16-subframe cycles and its subframes as the core gets them), sound and NVRAM equal those of the reference build (Task 1: `pinheck` at `863dc6e9`) in attract mode, with video and in play, with and without the worker thread, with `PINHECK_JIT=0`, `PINHECK_LAZY=0`, `PINHECK_RF13=0`, `PINHECK_JOURNAL=0`, and for the profile-guided build; the scripted Domino's game's four logs are byte-identical.
- **All suites green:** `tests/pinheck/{mips32,pic32mx,storage,link,p8x32a,display,audio,board}/check.sh` (the `p8x32a` suite with and without the translator, `P8X32A_JIT=1`, with `mutate_lazy.sh` and `mutate_core.sh` inside it), and the machine checks of Plans 5–12c for all four games.
- No new switch: every change is in the translator's path (off with `PINHECK_JIT=0`) or a rearrangement of the scheduler with the same decisions.
- Code comments short and factual; the core C89-syntax-clean; no `__inline__` or `__inline` (`P8_INLINE`/`P8_COLD`).
- Never commit the mask ROM, firmware, SD cards, romsets or profiles.

## Rulings

Decisions taken on measurements of the prototype (branch `pinheck-12d-proto`). Unless they say otherwise, figures are for America's Most Haunted with video (`bench.sh video`, 2,400 frames, about 40 s emulated); "the worker" is the Propeller worker thread, measured over the whole run by a per-thread recording (`bench.sh worker`, three runs each, ±0.5%); "the proxy" is `p8run` (translated, no trace) running the game's Propeller firmware and SD card alone, from 5 s to 10 s of emulated time.

1. **Where a dispatch's time goes.** The reference's worker spends 3.66 G cycles per emulated second (one thread: 15.6 G host instructions per emulated second in all; the PIC32 thread 1.8 G cycles). By symbol, in cycles: `run_local` 29.7% (its entry and exit, the decode, the variance check and block lookup before each block, `event_run` and the issue of a hub operation that must wait), the translated blocks' own code 21.3%, the scheduler (`p8x32a_run_until`, inlined into `prop.c`'s `do_catch_up`) 12.5%, the translator's shared tail (a block's exit: the count, `loop_edge`'s search step, the next block's lookup and an indirect jump shared by every block) 12.3%, `hub_rw` 6.0%, `flush` 4.1%, the pin devices, the lazy scan cog and the rest 14%. At about 5.8 million dispatches per emulated second that is about 630 cycles a dispatch: about 190 in `run_local`, 80 in the scheduler, 80 in the tail, 40 in `hub_rw` and 135 in the PASM work itself. Branch mispredictions (about 4 per dispatch) and level-1 data misses (about 10 per dispatch: the cogs' state, their decoded words, the block table and block headers) are spread over the same symbols in the same proportions. The interpreters' blocks are short (about 3.7 PASM instructions between exits in the proxy, 17 million exits per emulated second).
2. **The measure.** Instruction counts alone mislead: the first prototype cut the one-thread count by 6.6% while its cycles rose 3% (twice the level-1 instruction-cache misses from larger translated code), and the proxy (the Propeller alone, no PIC32, no `prop.c` devices) showed −13% cycles for a version that saved 1% in PinMAME's worker. So each change was judged by the worker's cycles in PinMAME (`bench.sh worker`, added in Task 2), with the one-thread instruction count and the RTL as the other checks.
3. **The scheduler keeps its keys in order** (Task 3). `p8x32a_run_until` scanned its eight cached keys at every dispatch; the keys are now kept sorted (a key holds its cog), a dispatch reads the first two, and the cog that ran moves back to its place (a short loop over a contiguous array; an index array, branch-free insertion and a branch-free rank were each slower). A recount when `sched_gen` moves sorts all eight. Same decisions (keys are unique but for `P8X32A_NEVER`). The worker: −1.5% (152.5 → 150.2 G cycles over the run).
4. **A hub read or write ends a translated block** (Task 4). A read or write without `WC`, with an immediate, cog-RAM or `PAR` source and (for a read) a destination below `$1F0` is translated as a block's last slot. Its code finds its hub slot (`next_slot`) and runs it at once when `event_run` would: its time `h` by the horizon `t`, `h`'s key below `lim`, `latch + 5` before the stop time, `*pgen == gen`. A read is done in the block (its write to D reported as any instruction's: a fixed code slot changed ends the run), a write through `st.hubfn` (`hub_rw`: the journal, the sleepers, the time-order check). Otherwise the block stops before it with its operands, slot and the address of its *resume entry*; `run_local` issues it as `exec` would and keeps the entry for the cog (`jres`, with the word, the address and the cog's count of blocks translated again, `jep`, since translating again frees the old code). When the operation is due, `run_local` calls the entry, which completes it as `hub_rw` would and runs on into the next blocks, so a dispatch is one call. The read's time-order check (test builds) calls `st.chkfn`. A word that never runs (`NOP`, condition 0) stays a no-op inside a block. A block of none whose first slot's S or D field changed keeps that slot's mask, so it is not translated again at every lookup (the prototype rebuilt one such block 48,000 times a second). Alone the change costs cycles (worker +1.6%: the hub operation's code in each block, with the old shared tail); with Task 5 it pays.
5. **A link table and per-exit chains** (Task 5). A block's exit to a known address (a sequential stop, or a taken jump) counts its instructions, takes `loop_edge`'s search step for a backward jump (a call to one shared copy), and jumps to the next block through that address's link (`p8x32a_jlink`, 32 bytes: the entry, the first word and its mask, the length, ~0 when void) from its own site; `jit_void` keeps the link with the block's validity. The shared tail remains for a lazy cog's blocks and for a jump that did not jump. A block reached from another's exit that stops before its first slot (the S or D field now names a special register) now leaves its own exit state: the prototype's per-exit chains left the previous block's, which the RTL cases never reached and America's Most Haunted's firmware trace caught at cycle 21,984,600 (`chip/jit_chain.spin` and the `exit-k0` mutant). Worker with Tasks 3–5: −3.7% (146.9 G).
6. **run_local takes a linked block before decoding** (Task 6). The next address's link, when it is valid for the word fetched and the block fits the time left, replaces the decode, the variance check and the table lookup. Worker with Tasks 3–6: −5.7% (143.8 G), one thread −11% host instructions (8.7 against 9.7 G per emulated second in the worker).
7. **Rejected after measurement:** each exit's whole chain inlined (instruction-cache misses doubled; one-thread cycles +3%); the exits' chains as calls to shared stubs (worker +3% against inline exits); the cog's translator state kept in `p8x32a` instead of on the stack (proxy +1.5% cycles); the shared tail with the link table instead of per-exit chains (worker 145 G against 142 G); the link path for a lazy cog's blocks (no gain); `run_local`'s resume through a flag instead of a jump into its block (+2% instructions); a lookup table of resume entries by address (more data-cache misses than the per-cog entry). Hub operations before other cogs' earlier events were not built again: Plans 12b and 12c measured them exact and dearer than the dispatches they save.
8. **Found in the prototype and now tested.** A hub slot's key computed with a 32-bit `OR` (`or esi, ...`) dropped the time's top bits once times passed 2^28 cycles; America's Most Haunted's firmware traces stayed identical over 200 million cycles while the operations ran ahead of other cogs, and no RTL case (all under 400,000 cycles) could show it. `p8run -t0 N` starts the chip at cycle N (inputs and the trace keep cycle 0), `check.sh` runs every RTL case also from 2^36 and `mutate_core.sh` the multi-cog programs; the `hub-key32` mutant is caught there only. A hub read that loads a later block's fixed slot must report it (`chip/jit_hubcode.spin`, the `hub-report` mutant). Two mutants of the prototype survived and were dropped as redundant: the link path without its word check (a valid link's first word is the word at its address: a write to it voids the link) and without its time check (the call repeats it).
9. **Real time.** With the worker thread (the replay, loads 1.2–1.9): America's Most Haunted 1.25× in attract mode, 0.95× with video, 0.87× in play without a profile, and 1.27×, 1.00×, 0.92× with the profile-guided build (Plan 12c: 1.18×, 0.91×, 0.86× and 1.22×, 0.96×, 0.87×). Real time is reached with video with the profile-guided build only, with no margin; in play it is not reached: the worker needs 4.26 G cycles per emulated second (4.14 G with the profile), more than one core gives it at these loads. The other games keep real time with the profile (The Jetsons 1.24×, 1.13×, 1.12×). The best proven stage is this plan's; what bounds play is in "What remains". Cost if wrong: none to exactness; the statement is a measurement.

## Review Focus

- **A hub slot's decision** in the translated block against `event_run`'s: the key from the 64-bit time (`hub-key32`), `h <= t`, the stop time, `sched_gen` compared with the dispatch's `gen` (a write through `hubfn` may wake a sleeper and move it: `hub-gen`), `lim` set to 0 when the scheduler would not run events (`p->stop`, a cog not searching for a loop).
- **The resume entry's lifetime:** `jres` is used only while the cog's word, address and `jep` match what the issue recorded; `jep` counts every translation that frees a block's code (`jit_get` building again). A block made invalid but not translated again keeps its code, and its resume completes exactly the operation it issued.
- **The exit state:** every way a block returns to `run_local` (a sequential stop, a jump, a jump that did not jump, a hub operation that waits, a first slot that no longer fits after a chain) leaves `pc`, `px`, `nix`, `jmp`, `jc` (and `s`, `d`, `w` for a jump) as the shared tail did.
- **The link's validity:** every place that makes a block invalid goes through `jit_void` (`jit_written`, the `inv == ~0` path, `jit_drop`), and a block's link is written when it is built.
- **The scheduler's order:** the sorted keys are rebuilt when `sched_gen` moves and the cog that ran is reinserted after `goto again`'s test; ties occur only between `P8X32A_NEVER` keys.

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/p8x32a/p8x32a.c` | sorted scheduling keys; `jit_hub`, `jit_chk`, `jit_void`; the link table and `jres`/`jep` kept; `run_local`'s resume entry, linked path and issue from a block's exit (modified) |
| `src/cpu/p8x32a/p8x32a.h` | `p8x32a_jlink`; `p8x32a_jst`'s hub-slot, resume and link fields; `p8x32a_dec.jh`; `p8x32a.jlink`, `jres`, `jres_i`, `jres_px`, `jres_ep`, `jep` (modified) |
| `src/cpu/p8x32a/p8x32ajit.cpp` | hub slots, resume entries, `hub_access`, `after_hub`; per-exit chains (`chain`, `seq_exit`) and the shared `loop_edge` step (`build_edge`); the shared tail through the link table (modified) |
| `tests/pinheck/p8x32a/{run.c,check.sh,mutate_core.sh}` | `p8run -t0`; every RTL case also from 2^36; `mutate_core.sh`'s runs from 2^36, `MUTANTS`, six mutants (modified) |
| `tests/pinheck/p8x32a/chip/{jit_hubcode,jit_chain}.spin` | a hub read into a later block's slot; a chained block whose first slot no longer fits (new) |
| `tests/pinheck/perf/{bench.sh,threads.py}` | `bench.sh worker`: the worker's and the PIC32 thread's cycles per emulated second (modified, new) |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | what Plan 12d established; its roadmap row (modified) |

---
### Task 1: Reference build

**Files:** none (a build).

**Interfaces:**
- Consumes: `pinheck` at `863dc6e9`, Plan 9's `bench.sh`.
- Produces: `build/reference/sdl3pinmame`, which every later task compares with.

- [ ] **Step 1: Build the reference**

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12d.log 2>&1; tail -1 build/sdl3pinmame/12d.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```

Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: The reference on one thread**

America's Most Haunted with video and in play, the instruction counts later steps compare with (about 4 minutes with the first boot; on one thread the count does not depend on the load):

```sh
amh env SDL3PINMAME=build/reference/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video play | grep 'bench '
```

Expected (the speeds vary with the load):

```text
bench video opt: 0.672x over 24.0 s emulated (35.7 s wall), whole run 15.3 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.568x, load 2.24
bench play opt: 0.640x over 55.0 s emulated (86.0 s wall), whole run 17.2 G instructions and 1.4 s CPU per emulated s, worst 100 ms 0.286x, load 1.66
```

### Task 2: Tests: runs from cycle 2^36, a filter for mutants, the worker's cycles

**Files:**
- Create: `tests/pinheck/perf/threads.py`
- Modify: `tests/pinheck/p8x32a/run.c`, `tests/pinheck/p8x32a/check.sh`, `tests/pinheck/p8x32a/mutate_core.sh`, `tests/pinheck/perf/bench.sh`

**Interfaces:**
- Consumes: `p8x32a_reset`, `p8x32a_run_until`; Plan 12c's `mutate_core.sh`; `bench.sh`'s `run`.
- Produces: `p8run -t0 N` (the chip starts at cycle N; inputs, `-extat`, `-uart`, `-stop` and the trace keep cycle 0); check.sh's `RTL MISMATCH ... (-t0 68719476736)`; `mutate_core.sh`'s runs of the multi-cog programs from 2^36 (`at t0`) and `MUTANTS="name ..."` (only those mutants and the unmutated core); `bench.sh worker WORKLOAD`: `bench worker W: the Propeller worker X G cycles and Y G instructions per emulated s, the PIC32 thread Z G cycles (whole run, S s emulated)`.

- [ ] **Step 1: The tests and the measure**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)

edit('tests/pinheck/p8x32a/check.sh', [
    (r'''	if cmp -s "$o.rtl" "$o.q" && cmp -s "$o.rtlhub" "$o.qhub" && ! grep -q "p8x32a: time order" "$o.qlog"; then pass=$((pass + 1))
	else echo "RTL MISMATCH $1 (-quantum 400000)"; diff "$o.rtl" "$o.q" | head -6; grep "p8x32a: time order" "$o.qlog"; fail=$((fail + 1)); fi
	# EXPECT-CLKSHIFT: d v = with CLKSET moving queued edges d cycles earlier, the long at $6000 is v
	set -- "$1" $(sed -n "s/^' EXPECT-CLKSHIFT: //p" "$1")''',
     r'''	if cmp -s "$o.rtl" "$o.q" && cmp -s "$o.rtlhub" "$o.qhub" && ! grep -q "p8x32a: time order" "$o.qlog"; then pass=$((pass + 1))
	else echo "RTL MISMATCH $1 (-quantum 400000)"; diff "$o.rtl" "$o.q" | head -6; grep "p8x32a: time order" "$o.qlog"; fail=$((fail + 1)); fi
	# the same run from cycle 2^36: a time or a scheduling key cut to 32 bits shows
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args -t0 68719476736 -dump "$o.thub" > "$o.t" 2> "$o.tlog"
	if cmp -s "$o.rtl" "$o.t" && cmp -s "$o.rtlhub" "$o.thub" && ! grep -q "p8x32a: time order" "$o.tlog"; then pass=$((pass + 1))
	else echo "RTL MISMATCH $1 (-t0 68719476736)"; diff "$o.rtl" "$o.t" | head -6; grep "p8x32a: time order" "$o.tlog"; fail=$((fail + 1)); fi
	# EXPECT-CLKSHIFT: d v = with CLKSET moving queued edges d cycles earlier, the long at $6000 is v
	set -- "$1" $(sed -n "s/^' EXPECT-CLKSHIFT: //p" "$1")'''),
])
edit('tests/pinheck/p8x32a/mutate_core.sh', [
    (r'''# EXPECT-JITVAR, on one of chip/*.spin and gen.py's programs (SEEDS of each kind, default 100). Each case runs with
# p8run's quantum of 4,096 cycles and with one of 400,000 (one run_until for the whole run, as long as PinMAME's).
# P8X32A_JIT=1 runs the mutants in the translator's path (x86-64 only), else the others.
set -u
cd "$(dirname "$0")" || exit 2''',
     r'''# EXPECT-JITVAR, on one of chip/*.spin and gen.py's programs (SEEDS of each kind, default 100). Each case runs with
# p8run's quantum of 4,096 cycles and with one of 400,000 (one run_until for the whole run, as long as PinMAME's).
# P8X32A_JIT=1 runs the mutants in the translator's path (x86-64 only), else the others; MUTANTS names some of them.
# The multi-cog programs also run from cycle 2^36 (p8run -t0), where a time or key cut to 32 bits shows.
set -u
cd "$(dirname "$0")" || exit 2'''),
    (r'''	name=$1 file=$2 jit=$5
	[ "$jit" = $MODE ] || return 0
	cp $CORE/p8x32a.c $CORE/p8x32ajit.cpp $B/
	python3 - "$3" "$4" $CORE/$file $B/$file <<'PY' || { echo "MUTANT $name: no match"; fail=$((fail + 1)); return; }''',
     r'''	name=$1 file=$2 jit=$5
	[ "$jit" = $MODE ] || return 0
	# MUTANTS: only these (and the unmutated core)
	[ -z "${MUTANTS:-}" ] || case " $MUTANTS none-interpreted none-translated " in *" $name "*) ;; *) return 0 ;; esac
	cp $CORE/p8x32a.c $CORE/p8x32ajit.cpp $B/
	python3 - "$3" "$4" $CORE/$file $B/$file <<'PY' || { echo "MUTANT $name: no match"; fail=$((fail + 1)); return; }'''),
    (r'''	fi
	bad=0 first=
	for q in 4096 400000; do
		for f in $cases; do
			o=$B/case/$(basename "$f" .spin)
			jv=$(sed -n "s/^' EXPECT-JITVAR: //p" "$f")
			# a mutant that loops for ever is caught too
			timeout -k 5 20 ./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 -quantum $q ${jv:+-jitvar} -dump "$o.mhub" > "$o.m" 2> "$o.mlog"
			if ! cmp -s "$o.rtl" "$o.m" || ! cmp -s "$o.rtlhub" "$o.mhub" || grep -q "time order" "$o.mlog" ||
			   { [ -n "$jv" ] && [ "$jit" = 1 ] && ! grep -qxF "p8run: $jv block lookups left to the interpreter" "$o.mlog"; }; then''',
     r'''	fi
	bad=0 first=
	for q in 4096 400000 t0; do
		for f in $cases; do
			o=$B/case/$(basename "$f" .spin)
			jv=$(sed -n "s/^' EXPECT-JITVAR: //p" "$f")
			# from cycle 2^36: the multi-cog programs
			qa="-quantum $q"
			if [ $q = t0 ]; then case $(basename "$f") in m*) qa="-t0 68719476736" ;; *) continue ;; esac; fi
			# a mutant that loops for ever is caught too
			timeout -k 5 20 ./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $qa ${jv:+-jitvar} -dump "$o.mhub" > "$o.m" 2> "$o.mlog"
			if ! cmp -s "$o.rtl" "$o.m" || ! cmp -s "$o.rtlhub" "$o.mhub" || grep -q "time order" "$o.mlog" ||
			   { [ -n "$jv" ] && [ "$jit" = 1 ] && ! grep -qxF "p8run: $jv block lookups left to the interpreter" "$o.mlog"; }; then'''),
])
edit('tests/pinheck/p8x32a/run.c', [
    (r'''static uint64_t known_to = P8X32A_NEVER;
static uint64_t clkshift, clk_at;
static struct ev { uint64_t t; size_t seq; char line[96]; } *evs;
static size_t nev, cap;''',
     r'''static uint64_t known_to = P8X32A_NEVER;
static uint64_t clkshift, clk_at;
/* -t0 n: the chip starts at cycle n (every time above 2^32 for n = 2^36); the trace and the inputs keep cycle 0 */
static uint64_t t0off;
#define TT(t) ((t) - t0off)
static struct ev { uint64_t t; size_t seq; char line[96]; } *evs;
static size_t nev, cap;'''),
    (r'''	int k;
	(void)ctx;
	for (k = 0; k < nat && at_t[k] <= t; k++) v = at_v[k];
	if (have_sd) v = (v & ~1u) | sdbit;
	return have_ee ? (v & ~0x30000000u) | eebits : v;''',
     r'''	int k;
	(void)ctx;
	for (k = 0; k < nat && at_t[k] <= TT(t); k++) v = at_v[k];
	if (have_sd) v = (v & ~1u) | sdbit;
	return have_ee ? (v & ~0x30000000u) | eebits : v;'''),
    (r'''	(void)ctx;
	for (k = 0; k < nat; k++)
		if (at_t[k] > t) return at_t[k] <= known_to ? at_t[k] : P8X32A_NEVER;
	return P8X32A_NEVER;
}''',
     r'''	(void)ctx;
	for (k = 0; k < nat; k++)
		if (at_t[k] > TT(t)) return at_t[k] <= known_to ? at_t[k] + t0off : P8X32A_NEVER;
	return P8X32A_NEVER;
}'''),
    (r'''		eebits = 0x10000000u | (uint32_t)drv << 29;
	}
	cur_t = t;
	if (have_sd) {
		int cs = (dir >> 3 & 1) ? (int)(out >> 3 & 1) : 1;''',
     r'''		eebits = 0x10000000u | (uint32_t)drv << 29;
	}
	cur_t = TT(t);
	if (have_sd) {
		int cs = (dir >> 3 & 1) ? (int)(out >> 3 & 1) : 1;'''),
    (r'''	{
		char b[48];
		sprintf(b, "N %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
		emit(t, b);
	}
}''',
     r'''	{
		char b[48];
		sprintf(b, "N %llu %08x %08x", (unsigned long long)TT(t), (unsigned)out, (unsigned)dir);
		emit(TT(t), b);
	}
}'''),
    (r'''	(void)ctx;
	if (notrace) return;
	sprintf(b, "M %llu %08x %08x %08x", (unsigned long long)t, (unsigned)mask, (unsigned)out, (unsigned)dir);
	emit(t, b);
}
''',
     r'''	(void)ctx;
	if (notrace) return;
	sprintf(b, "M %llu %08x %08x %08x", (unsigned long long)TT(t), (unsigned)mask, (unsigned)out, (unsigned)dir);
	emit(TT(t), b);
}
'''),
    (r'''	if (t < clk_at) fprintf(stderr, "p8run: lazy pin change at %llu after a CLKSET at %llu\n", (unsigned long long)t, (unsigned long long)clk_at);
	if (notrace) return;
	sprintf(b, "L %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
	emit(t, b);
}
''',
     r'''	if (t < clk_at) fprintf(stderr, "p8run: lazy pin change at %llu after a CLKSET at %llu\n", (unsigned long long)t, (unsigned long long)clk_at);
	if (notrace) return;
	sprintf(b, "L %llu %08x %08x", (unsigned long long)TT(t), (unsigned)out, (unsigned)dir);
	emit(TT(t), b);
}
'''),
    (r'''	(void)ctx;
	char b[48];
	sprintf(b, "S %llu %d %07x", (unsigned long long)t, cog, (unsigned)ptr);
	emit(t, b);
	if (cog == stop_cog && ptr == stop_ptr) { stop_at = t; chip.stop = 1; }
}
''',
     r'''	(void)ctx;
	char b[48];
	sprintf(b, "S %llu %d %07x", (unsigned long long)TT(t), cog, (unsigned)ptr);
	emit(TT(t), b);
	if (cog == stop_cog && ptr == stop_ptr) { stop_at = TT(t); chip.stop = 1; }
}
'''),
    (r'''{
	(void)ctx;
	if (ctrlog) fprintf(ctrlog, "C %llu %d %d %08x %08x\n", (unsigned long long)t, cog, ctr, (unsigned)ctr_reg, (unsigned)frq);
}
''',
     r'''{
	(void)ctx;
	if (ctrlog) fprintf(ctrlog, "C %llu %d %d %08x %08x\n", (unsigned long long)TT(t), cog, ctr, (unsigned)ctr_reg, (unsigned)frq);
}
'''),
    (r'''	(void)ctx;
	clk_at = t;
	for (k = 0; k < nat; k++)
		if (at_t[k] > t + 1) at_t[k] = at_t[k] - clkshift > t + 1 ? at_t[k] - clkshift : t + 1;''',
     r'''	(void)ctx;
	clk_at = t;
	t = TT(t);
	for (k = 0; k < nat; k++)
		if (at_t[k] > t + 1) at_t[k] = at_t[k] - clkshift > t + 1 ? at_t[k] - clkshift : t + 1;'''),
    (r'''		const p8x32a_cog *c = &chip.cog[n];
		if (c->ev != 0) return P8X32A_NEVER;
		if (c->disable_at != P8X32A_NEVER && c->disable_at > last) last = c->disable_at;
	}
	return last;''',
     r'''		const p8x32a_cog *c = &chip.cog[n];
		if (c->ev != 0) return P8X32A_NEVER;
		if (c->disable_at != P8X32A_NEVER && TT(c->disable_at) > last) last = TT(c->disable_at);
	}
	return last;'''),
    (r'''		else if (!strcmp(argv[i], "-quantum") && i + 1 < argc) quantum = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-clkshift") && i + 1 < argc) clkshift = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 8192) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else { fprintf(stderr, "usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-quantum n] [-sd romset.zip] [-notrace] [-sleeps] [-ctrlog f] [-uart cycle hexbytes] [-extat cycle hex]... [-clkshift n] [-dump f]\n"); return 2; }
	}
	if (!rom) { fprintf(stderr, "p8run: -rom is required\n"); return 2; }
	p8x32a_init(&chip, &bus);
#ifdef P8X32A_JIT
	if ((chip.jit = p8x32a_jit_new()) != NULL) chip.jit_build = p8x32a_jit_build;''',
     r'''		else if (!strcmp(argv[i], "-quantum") && i + 1 < argc) quantum = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-clkshift") && i + 1 < argc) clkshift = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-t0") && i + 1 < argc) t0off = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-extat") && i + 2 < argc && nat < 8192) { at_t[nat] = strtoull(argv[i + 1], NULL, 0); at_v[nat++] = (uint32_t)strtoul(argv[i + 2], NULL, 16); i += 2; }
		else { fprintf(stderr, "usage: p8run -rom f [-ram f] [-eeprom f] [-ext hex] [-cycles n] [-stop cog ptrhex] [-halt] [-quantum n] [-sd romset.zip] [-notrace] [-sleeps] [-ctrlog f] [-uart cycle hexbytes] [-extat cycle hex]... [-clkshift n] [-t0 n] [-dump f]\n"); return 2; }
	}
	if (!rom) { fprintf(stderr, "p8run: -rom is required\n"); return 2; }
	p8x32a_init(&chip, &bus);
	if (t0off) {
		p8x32a_reset(&chip, t0off);
	}
#ifdef P8X32A_JIT
	if ((chip.jit = p8x32a_jit_new()) != NULL) chip.jit_build = p8x32a_jit_build;'''),
    (r'''		uint64_t h;
		if (quantum != 4096) known_to = to;
		p8x32a_run_until(&chip, to);
		if (chip.stop) { end = stop_at + 1; break; }
		if (halt && (h = halted()) != P8X32A_NEVER && h >= 2) { end = h; break; }''',
     r'''		uint64_t h;
		if (quantum != 4096) known_to = to;
		p8x32a_run_until(&chip, t0off + to);
		if (chip.stop) { end = stop_at + 1; break; }
		if (halt && (h = halted()) != P8X32A_NEVER && h >= 2) { end = h; break; }'''),
])
edit('tests/pinheck/perf/bench.sh', [
    (r'''#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py (BENCH_PERF_EV: perf
#                                  record's event options, default -F 999)
#   bench.sh contention [CASE...]  10 s of attract mode where CPUs are scarce (Linux, taskset; BENCH_CPU_A and BENCH_CPU_B,
#                                  default 2 and 4, name two allowed CPUs): on one CPU (one), on two CPUs while a busy''',
     r'''#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py (BENCH_PERF_EV: perf
#                                  record's event options, default -F 999)
#   bench.sh worker WORKLOAD       one workload with $SDL3PINMAME recorded per thread (perf, cycles and instructions):
#                                  the Propeller worker's and the PIC32 thread's cycles per emulated second
#   bench.sh contention [CASE...]  10 s of attract mode where CPUs are scarce (Linux, taskset; BENCH_CPU_A and BENCH_CPU_B,
#                                  default 2 and 4, name two allowed CPUs): on one CPU (one), on two CPUs while a busy'''),
    (r'''	[ -n "$PERF" ] && wrap="$PERF stat -e task-clock,instructions:u,cycles:u -o $D/perf.txt"
	[ -n "$PERF" ] && [ "$4" = record ] && wrap="$PERF record ${BENCH_PERF_EV:--F 999} -o $D/perf.data --"
	{ echo "$MARK mark window"; [ -z "$KEYS" ] || echo "$KEYS"; } > $D/keys.txt
	t0=$(date +%s.%N)''',
     r'''	[ -n "$PERF" ] && wrap="$PERF stat -e task-clock,instructions:u,cycles:u -o $D/perf.txt"
	[ -n "$PERF" ] && [ "$4" = record ] && wrap="$PERF record ${BENCH_PERF_EV:--F 999} -o $D/perf.data --"
	[ -n "$PERF" ] && [ "$4" = threads ] && wrap="$PERF record -q -s -e cycles:u,instructions:u -c 100000 -o $D/perf.data --"
	{ echo "$MARK mark window"; [ -z "$KEYS" ] || echo "$KEYS"; } > $D/keys.txt
	t0=$(date +%s.%N)'''),
    (r'''	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort srcfile 2> /dev/null | python3 components.py
	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort dso 2> /dev/null | awk '/\[JIT\]/ { s += $1 } END { printf "%6.1f%%  of it translated Propeller code\n", s }'
	exit 0
fi''',
     r'''	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort srcfile 2> /dev/null | python3 components.py
	$PERF report -i $B/prof/${2:-attract}/perf.data --stdio --sort dso 2> /dev/null | awk '/\[JIT\]/ { s += $1 } END { printf "%6.1f%%  of it translated Propeller code\n", s }'
	exit 0
fi
if [ "$1" = worker ]; then
	[ -n "$PERF" ] || { echo "bench: perf not found"; exit 2; }
	run work "$SDL3PINMAME" "${2:-video}" threads || exit 1
	spec "${2:-video}"
	$PERF report -i $B/work/${2:-video}/perf.data --stdio -n --sort pid,sym 2> /dev/null |
		python3 threads.py "${2:-video}" "$(awk "BEGIN { print ($FRAMES - 1) / 60 }")" 100000 || exit 1
	rm -f $B/work/${2:-video}/perf.data
	exit 0
fi'''),
])
open('tests/pinheck/perf/threads.py', 'w').write(r'''#!/usr/bin/env python3
"""bench.sh worker: cycles and instructions per emulated second of the PIC32 thread (the one running pic32cpu_execute)
and of every other thread together (the Propeller worker, started again by the governor at times), from
`perf report --stdio -n --sort pid,sym` of a per-thread recording with a fixed period. Arguments: workload, emulated
seconds, period."""
import collections
import re
import sys

w, emu, period = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
ev = None
count = collections.defaultdict(collections.Counter)
pic = collections.Counter()
for line in sys.stdin:
    m = re.match(r"# Samples: .* of event '([^':]+)", line)
    if m:
        ev = m.group(1)
        continue
    m = re.match(r"\s+[\d.]+%\s+(\d+)\s+(\d+):\S+\s+\[.\]\s+(.*)$", line)
    if m and ev:
        n, pid, sym = int(m.group(1)), m.group(2), m.group(3).strip()
        count[ev][pid] += n
        if ev == 'cycles' and sym == 'pic32cpu_execute':
            pic[pid] += n
if not pic:
    sys.exit('threads: no PIC32 thread in the recording')
main = pic.most_common(1)[0][0]
per = lambda ev, other: sum(n for p, n in count[ev].items() if (p != main) == other) * period / 1e9 / emu
print('bench worker %s: the Propeller worker %.3f G cycles and %.2f G instructions per emulated s, the PIC32 thread '
      '%.3f G cycles (whole run, %.1f s emulated)' % (w, per('cycles', True), per('instructions', True), per('cycles', False), emu))
''')
EOF
```

- [ ] **Step 2: Every case from 2^36 on the present core**

The unmutated core matches the RTL from cycle 2^36 too, and Plan 12c's first mutant is still caught:

```sh
MUTANTS=ina-ahead nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
```

Expected (about 3 minutes with the RTL references of a fresh worktree):

```text
unmutated core (none-interpreted): all cases match
mutant ina-ahead caught by 367 cases (first clk_idle.spin at 4096)
```

- [ ] **Step 3: The reference's worker**

```sh
amh env SDL3PINMAME=build/reference/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh worker video | grep 'bench worker'
```

Expected (about 2 minutes with the first boot):

```text
bench worker video: the Propeller worker 3.803 G cycles and 9.74 G instructions per emulated s, the PIC32 thread 1.790 G cycles (whole run, 40.0 s emulated)
```

- [ ] **Step 4: Commit**

```sh
git add tests/pinheck/p8x32a/run.c tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/mutate_core.sh tests/pinheck/perf/bench.sh tests/pinheck/perf/threads.py
git commit -m "p8x32a, perf: runs from cycle 2^36 (p8run -t0, every RTL case), mutate_core.sh's MUTANTS, bench.sh worker"
```

### Task 3: The scheduler keeps its keys in order

**Files:**
- Modify: `src/cpu/p8x32a/p8x32a.c`, `tests/pinheck/p8x32a/mutate_core.sh`

**Interfaces:**
- Consumes: `ev_key`, `sched_gen`; Task 2's `MUTANTS`.
- Produces: `p8x32a_run_until`'s `key[9]` in ascending order (`key[8]` = `P8X32A_NEVER`); the `keys-ran` mutant on the new code.

- [ ] **Step 1: The keys in order, and the mutant on the new code**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)

edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''void p8x32a_run_until(p8x32a *p, uint64_t t)
{
	uint64_t key[8];
	unsigned kgen = p->sched_gen - 1;
	p->horizon = t;
	for (;;) {
		int n, best;
		uint64_t bk = P8X32A_NEVER, bk2 = P8X32A_NEVER;
		unsigned gen;
		p8x32a_cog *b;
		/* the keys change only for the cog that ran, unless sched_gen counts a change to another cog's event */
		if (kgen != p->sched_gen) {
			for (n = 0; n < 8; n++) key[n] = ev_key(&p->cog[n], n);
			kgen = p->sched_gen;
		}
		/* the next event, and bk2 the one after it */
		for (n = 0; n < 8; n++) {
			uint64_t k = key[n];
			if (k < bk) { bk2 = bk; bk = k; }
			else if (k < bk2) bk2 = k;
		}
		if (bk == P8X32A_NEVER) break;
		best = (int)(bk & 7);''',
     r'''void p8x32a_run_until(p8x32a *p, uint64_t t)
{
	uint64_t key[9];
	unsigned kgen = p->sched_gen - 1;
	p->horizon = t;
	for (;;) {
		int n, j, best;
		uint64_t bk, bk2;
		unsigned gen;
		p8x32a_cog *b;
		/* the keys, in order (a key holds its cog), change only for the cog that ran, unless sched_gen counts a change
		   to another cog's event */
		if (kgen != p->sched_gen) {
			for (n = 0; n < 8; n++) {
				uint64_t k = ev_key(&p->cog[n], n);
				for (j = n; j > 0 && key[j - 1] > k; j--) key[j] = key[j - 1];
				key[j] = k;
			}
			key[8] = P8X32A_NEVER;
			kgen = p->sched_gen;
		}
		/* the next event, and bk2 the one after it */
		bk = key[0];
		bk2 = key[1];
		if (bk == P8X32A_NEVER) break;
		best = (int)(bk & 7);'''),
    (r'''	again:
		p->now = b->ev_t;
		switch (b->ev) {
		case EV_HUB:
			if (!b->run || p->loop[best].state != LOOP_SEARCH || !run_local(p, best, t, bk2, gen)) do_hub(p, best);
			break;
		case EV_EXEC:
			if (!b->run || p->loop[best].state != LOOP_SEARCH || !run_local(p, best, t, bk2, gen)) exec(p, best);''',
     r'''	again:
		p->now = b->ev_t;
		if (b->ev == EV_HUB) {
			if (!b->run || p->loop[best].state != LOOP_SEARCH || !run_local(p, best, t, bk2, gen)) do_hub(p, best);
		} else switch (b->ev) {
		case EV_EXEC:
			if (!b->run || p->loop[best].state != LOOP_SEARCH || !run_local(p, best, t, bk2, gen)) exec(p, best);'''),
    (r'''		/* while no other cog's next event has changed, this cog goes again if it is still the earliest */
		if (gen == p->sched_gen && b->ev != EV_NONE && b->ev_t <= t && !p->stop && ev_key(b, best) < bk2) goto again;
		key[best] = ev_key(b, best);
	}
	lazy_catch(p, t);''',
     r'''		/* while no other cog's next event has changed, this cog goes again if it is still the earliest */
		if (gen == p->sched_gen && b->ev != EV_NONE && b->ev_t <= t && !p->stop && ev_key(b, best) < bk2) goto again;
		/* best's new key moves back to its place */
		{
			uint64_t k = ev_key(b, best);
			for (j = 0; key[j + 1] < k; j++) key[j] = key[j + 1];
			key[j] = k;
		}
	}
	lazy_catch(p, t);'''),
])
edit('tests/pinheck/p8x32a/mutate_core.sh', [
    (r'''# the scheduler's key of the cog that ran left as it was when its next event is a hub operation; another cog's
# event moved without sched_gen counting it: by a system operation, a sleeper woken by a pin change or a hub write
mutant keys-ran p8x32a.c '		key[best] = ev_key(b, best);' '		if (b->ev != EV_HUB) key[best] = ev_key(b, best);' 0
mutant gen-sys p8x32a.c '	p->sched_gen++;
	while (newx' '	while (newx' 0''',
     r'''# the scheduler's key of the cog that ran left as it was when its next event is a hub operation; another cog's
# event moved without sched_gen counting it: by a system operation, a sleeper woken by a pin change or a hub write
mutant keys-ran p8x32a.c '			uint64_t k = ev_key(b, best);' '			uint64_t k = b->ev == EV_HUB ? bk : ev_key(b, best);' 0
mutant gen-sys p8x32a.c '	p->sched_gen++;
	while (newx' '	while (newx' 0'''),
])
EOF
```

- [ ] **Step 2: The mutant is caught**

```sh
MUTANTS=keys-ran nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
```

Expected (under a minute):

```text
unmutated core (none-interpreted): all cases match
mutant keys-ran caught by 365 cases (first cogs.spin at 4096)
```

- [ ] **Step 3: America's Most Haunted byte-identical**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12d.log 2>&1; tail -1 build/sdl3pinmame/12d.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep -E 'bench video opt|determinism|FAIL'
```

Expected (about 3 minutes; compare Task 1's count):

```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/amh.nv
bench video opt: 0.694x over 24.0 s emulated (34.5 s wall), whole run 15.1 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.585x, load 1.48
determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 4: Commit**

```sh
git add src/cpu/p8x32a/p8x32a.c tests/pinheck/p8x32a/mutate_core.sh
git commit -m "p8x32a: the scheduler keeps its keys in order"
```

### Task 4: A hub read or write ends a translated block

**Files:**
- Create: `tests/pinheck/p8x32a/chip/jit_hubcode.spin`
- Modify: `tests/pinheck/p8x32a/mutate_core.sh`, `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32ajit.cpp`

**Interfaces:**
- Consumes: `event_run`'s conditions, `hub_rw`, `next_slot`, `jit_get`, `run_local`'s issue of a hub operation that waits.
- Produces: `p8x32a_dec.jh`; `p8x32a_jst.{t, lim, dis, hlatch, pgen, gen, n, hiss, hs, hd, hres, chip, hubfn, pnow, chkfn}`; `p8x32a.{jres, jres_i, jres_px, jres_ep, jep}`; `jit_hub`, `jit_chk`; a block's last slot a hub read or write, its resume entry; the mutants `hub-key32`, `hub-gen`, `hub-report`.

- [ ] **Step 1: The tests**

`chip/jit_hubcode.spin` loads an `ADD` and a `SUB` in turn into a later block's slot with the `RDLONG` that ends a block; three mutants cut the hub slot's key to 32 bits, drop its `sched_gen` test, and leave a read into a fixed slot unreported:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)

open('tests/pinheck/p8x32a/chip/jit_hubcode.spin', 'w').write(r'''PUB main
DAT
        long    $C0DE5EED
        org     0
' a hub read that ends a translated block loads the instruction of a later block's slot (an ADD and a SUB in turn):
' that block is translated again; the sum at $6000
entry   mov     p, tbl
        mov     k, #64
fill    wrlong  i1, p
        add     p, #4
        wrlong  i2, p
        add     p, #4
        djnz    k, #fill
        mov     p, tbl
        mov     k, #128
lp      rdlong  ins, p
        add     p, #4
        xor     junk, #1
ins     xor     junk, #2
        rol     acc, #1
        djnz    k, #lp
        wrlong  acc, r0
        cogid   k
        cogstop k
i1      add     acc, #1
i2      sub     acc, #3
tbl     long    $6100
r0      long    $6000
p       long    0
k       long    0
acc     long    1
junk    long    0
        long    $C0DEE0D0
''')
edit('tests/pinheck/p8x32a/mutate_core.sh', [
    (r'''				a.mov(x86::ecx, x86::eax);' 1
mutant jit-par p8x32a.c 'st.par = (c->ptr >> 14) << 2;' 'st.par = c->ptr << 2;' 1
exit $((fail != 0))
''',
     r'''				a.mov(x86::ecx, x86::eax);' 1
mutant jit-par p8x32a.c 'st.par = (c->ptr >> 14) << 2;' 'st.par = c->ptr << 2;' 1
# a translated hub read or write: its key cut to 32 bits (only times past 2^28 show it), the sched_gen test dropped,
# a read into a fixed slot of a block not reported
mutant hub-key32 p8x32ajit.cpp '			a.mov(x86::edi, stf(offsetof(p8x32a_jst, n)));
			a.or_(x86::rsi, x86::rdi);' '			a.or_(x86::esi, stf(offsetof(p8x32a_jst, n)));' 1
mutant hub-gen p8x32ajit.cpp '			a.cmp(x86::esi, stf(offsetof(p8x32a_jst, gen)));
			a.jne(wait);' '' 1
mutant hub-report p8x32ajit.cpp '			a.bt(x86::dword_ptr(x86::rdi, (int)((dst >> 5) * 4)), dst & 31);
			a.jnc(same);' '			a.jmp(same);' 1
exit $((fail != 0))
'''),
])
EOF
```

- [ ] **Step 2: Run them to see them fail**

```sh
P8X32A_JIT=1 MUTANTS="hub-key32 hub-gen hub-report" nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
```

Expected (under a minute): the mutants have no code to change yet:

```text
unmutated core (none-translated): all cases match
MUTANT hub-key32: no match
MUTANT hub-gen: no match
MUTANT hub-report: no match
```

- [ ] **Step 3: Hub operations at the ends of blocks, and their resume entries**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)

edit('src/cpu/p8x32a/p8x32a.h', [
    (r'''	const uint8_t *jmap;   /* hub longs with a journal entry (p8x32a.jmap): a block stops before reading one */
	uint32_t par;          /* the cog's PAR (CNT is the time) */
} p8x32a_jst;
#define P8X32A_JOUT 256 /* the OUTA writes a run of blocks may leave */''',
     r'''	const uint8_t *jmap;   /* hub longs with a journal entry (p8x32a.jmap): a block stops before reading one */
	uint32_t par;          /* the cog's PAR (CNT is the time) */
	/* a block's last hub read or write (not a lazy cog's) runs at once while run_local's event_run would run it: its
	   time h <= t, h's key below lim, latch + 5 < dis and *pgen == gen; hubfn runs it (st->latch its slot) and returns
	   fl. Else the block stops before it with hiss = 1, hs and hd its operands and hlatch its slot */
	uint64_t t, lim, dis, hlatch;
	const unsigned *pgen;
	unsigned gen, n, hiss, hs, hd;
	uint32_t (*hres)(struct p8x32a_jst *st); /* with hiss: the block's resume entry */
	void *chip;
	uint32_t (*hubfn)(struct p8x32a_jst *st, uint32_t s, uint32_t d, uint32_t i, uint32_t fl);
	uint64_t *pnow;        /* p8x32a.now, set to h by a block's hub read as event_run sets it */
	void (*chkfn)(struct p8x32a_jst *st, uint32_t a, uint32_t sz); /* test builds: a block's hub read at st->latch + 2 */
} p8x32a_jst;
#define P8X32A_JOUT 256 /* the OUTA writes a run of blocks may leave */'''),
    (r'''	uint32_t word;
	uint16_t src, dst;
	uint8_t kind, fl, cond, pad;
} p8x32a_dec;
''',
     r'''	uint32_t word;
	uint16_t src, dst;
	uint8_t kind, fl, cond, jh; /* jh: a hub read or write a translated block may end with */
} p8x32a_dec;
'''),
    (r'''	void *jit;
	p8x32a_jblk *jblk[8][512];
	uint32_t jvar[8][512];
	uint64_t jit_refused; /* block lookups left to the interpreter: the slot's word changed beyond its S and D fields */''',
     r'''	void *jit;
	p8x32a_jblk *jblk[8][512];
	/* the resume entry of the block that issued cog n's hub read or write jres_i at jres_px - 1, while jep[n] (counts the
	   cog's blocks translated again, which frees their code) is jres_ep[n] */
	uint32_t (*jres[8])(p8x32a_jst *st);
	uint32_t jres_i[8];
	unsigned jres_px[8], jres_ep[8], jep[8];
	uint32_t jvar[8][512];
	uint64_t jit_refused; /* block lookups left to the interpreter: the slot's word changed beyond its S and D fields */'''),
])
edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''	if (FWR(i) && DST(i) == 0x1F4) e->fl |= F_OUTA;
	if (op <= 2 && FWR(i) && !FWC(i)) e->fl |= F_HUBRD;
}
''',
     r'''	if (FWR(i) && DST(i) == 0x1F4) e->fl |= F_OUTA;
	if (op <= 2 && FWR(i) && !FWC(i)) e->fl |= F_HUBRD;
	/* as p8x32a_jit_build takes it */
	e->jh = (uint8_t)(op <= 2 && !FWC(i) && (FIM(i) || SRC(i) <= 0x1F0) && (!FWR(i) || DST(i) < 0x1F0));
}
'''),
    (r'''

/* the translated block for ix at cog address a */
P8_INLINE p8x32a_jblk *jit_get(p8x32a *p, int n, unsigned a, uint32_t ix)''',
     r'''

/* a translated block's hub read or write that event_run would run: as event_run runs it */
static uint32_t jit_hub(p8x32a_jst *st, uint32_t s, uint32_t d, uint32_t i, uint32_t fl)
{
	p8x32a *p = (p8x32a *)st->chip;
	int n = (int)st->n;
	p->now = st->latch + 2;
	p->cog[n].latch = st->latch;
	return hub_rw(p, n, i, s, d, st->latch + 2, st->latch + 4, fl);
}

#ifdef P8X32A_CHECK
static void jit_chk(p8x32a_jst *st, uint32_t a, uint32_t sz)
{
	CHK_READ(a, sz, st->latch + 2);
}
#endif

/* the translated block for ix at cog address a */
P8_INLINE p8x32a_jblk *jit_get(p8x32a *p, int n, unsigned a, uint32_t ix)'''),
    (r'''		if (var[a] & ~P8X32A_JDYN) { p->jit_refused++; return NULL; }
	}
	b = p->jit_build(p->jit, b, a, ix, p->cog[n].ram, var, p->lz_on && n == p->lz);
	p->jblk[n][a] = b;
	if (b && b->valid)
		for (k = 0; k < b->len; k++)''',
     r'''		if (var[a] & ~P8X32A_JDYN) { p->jit_refused++; return NULL; }
	}
	/* translating again frees the old code: resume entries taken from it are void */
	if (b) p->jep[n]++;
	b = p->jit_build(p->jit, b, a, ix, p->cog[n].ram, var, p->lz_on && n == p->lz);
	p->jblk[n][a] = b;

	if (b && b->valid)
		for (k = 0; k < b->len; k++)'''),
    (r'''	uint64_t t2in = t2, hub = 0;
	p8x32a_jst st;

	st.ram = ram;''',
     r'''	uint64_t t2in = t2, hub = 0;
	p8x32a_jst st;
	unsigned px = 0, jc = 0;

	st.ram = ram;'''),
    (r'''	st.loop = l;
	st.par = (c->ptr >> 14) << 2;

	if (c->ev == EV_HUB) {
		uint64_t m3 = c->latch + 4;
		if (OP(c->i) > 2 || m3 + 1 >= dis) return 0;
		fl = hub_rw(p, n, c->i, c->s, c->d, t2, m3, fl);
		nins++;''',
     r'''	st.loop = l;
	st.par = (c->ptr >> 14) << 2;
	st.t = t;
	st.lim = p->stop || l->state != LOOP_SEARCH ? 0 : lim;
	st.dis = dis;
	st.pgen = &p->sched_gen;
	st.gen = gen;
	st.n = (unsigned)n;
	st.chip = p;
	st.hubfn = jit_hub;
	st.pnow = &p->now;
#ifdef P8X32A_CHECK
	st.chkfn = jit_chk;
#else
	st.chkfn = NULL;
#endif
	st.hiss = 0;
	st.tl = tl;
	st.slot = p->slot_base + 3 + 2 * (uint64_t)n;
	st.hub = p->hub;

	if (c->ev == EV_HUB) {
		uint64_t m3 = c->latch + 4;
		if (OP(c->i) > 2 || m3 + 1 >= dis) return 0;
		done = 1;
		/* the translated block that issued this hub read or write completes it and runs on */
		if (p->jres[n] && p->jres_ep[n] == p->jep[n] && p->jres_i[n] == c->i && p->jres_px[n] == c->px && m3 + 3 <= tl &&
		    !(p->lz_on && n == p->lz)) {
			st.hs = c->s;
			st.hd = c->d;
			st.latch = c->latch;
			st.nix = c->nix;
			pc = c->px;
			ix = c->i;
			e = &dec[(pc - 1) & 511];
			goto resume;
		}
		fl = hub_rw(p, n, c->i, c->s, c->d, t2, m3, fl);
		nins++;'''),
    (r'''		ix = c->nix;
		t2 = m3 + 3;
		done = 1;
	}
	if (t2 > tl || dis < 2) goto limit;
	for (;;) {
		uint32_t s, d, r, nix;
		unsigned px = pc, jc = 0;
		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		/* event instructions are never translated */
		if (p->jit_build && !(fl & 4) && (e->kind != K_NL || ((e->fl & (F_OUTA | F_HUBRD)) && p->lz_on && n == p->lz)) && !(e->fl & F_INA)) {
			unsigned a = (pc - 1) & 511;
			p8x32a_jblk *b = jit_get(p, n, a, ix);
			unsigned k;
			if (b && b->len && (b->part ? t2 <= tl : t2 + 4 * (uint64_t)(b->len - 1) <= tl)) {
				l->nins = (uint16_t)(l->nins + nins);
				nins = 0;
				st.t2 = t2;
				st.budget = b->part ? 0x7FFFFFFFu : (uint32_t)((tl - t2) / 4 + 1);
				st.ix = ix;
				st.fl = fl & 3;
				st.inv = st.edge = 0;
				if (b->part) {
					st.ot = p->jot;
					st.ov = p->jov;
					st.on = 0;
					st.tl = tl;
					st.slot = p->slot_base + 3 + 2 * (uint64_t)n;
					st.hub = p->hub;
					st.jmap = p->jmap;
					st.latch = 0;
				}
				k = b->fn(&st);
				if (b->part) {
					unsigned j;
					if (st.latch) c->latch = st.latch;''',
     r'''		ix = c->nix;
		t2 = m3 + 3;
	}
	if (t2 > tl || dis < 2) goto limit;
	for (;;) {
		uint32_t s, d, r, nix;
		px = pc;
		jc = 0;
		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		/* event instructions are never translated */
		if (p->jit_build && !(fl & 4) && (e->kind != K_NL || (p->lz_on && n == p->lz ? e->fl & (F_OUTA | F_HUBRD) : e->jh)) && !(e->fl & F_INA)) {
			unsigned a = (pc - 1) & 511;
			p8x32a_jblk *b = jit_get(p, n, a, ix);
			unsigned k;
			if (b && b->len && (b->part ? t2 <= tl : t2 + 4 * (uint64_t)(b->len - 1) <= tl)) {
				if (0) {
					/* a resume entry (b NULL) runs first */
				resume:
					b = NULL;
				}
				l->nins = (uint16_t)(l->nins + nins);
				nins = 0;
				st.t2 = t2;
				st.budget = b && b->part ? 0x7FFFFFFFu : (uint32_t)((tl - t2) / 4 + 1);
				st.ix = ix;
				st.fl = fl & 3;
				st.inv = st.edge = 0;
				if (!b) {
					k = p->jres[n](&st);
					c->latch = st.latch;
				} else if (b->part) {
					st.ot = p->jot;
					st.ov = p->jov;
					st.on = 0;
					st.jmap = p->jmap;
					st.latch = 0;
					k = b->fn(&st);
				} else {
					/* a hub read or write run in the block leaves its slot here */
					st.latch = c->latch;
					k = b->fn(&st);
					c->latch = st.latch;
				}
				if (b && b->part) {
					unsigned j;
					if (st.latch) c->latch = st.latch;'''),
    (r'''				fl |= (jc || px == 511) << 2;
				ix = nix;
				if (t2 > tl) {
					if (t2 - 2 >= dis) { idled = 1; break; }''',
     r'''				fl |= (jc || px == 511) << 2;
				ix = nix;
				if (st.hiss) {
					/* the block stopped before a hub read or write that must wait: issued as exec issues it */
					c->i = ix;
					c->cond = 1;
					c->s = st.hs;
					c->d = st.hd;
					c->px = (uint16_t)pc;
					c->nix = ram[pc];
					c->latch = st.hlatch;
					hub = st.hlatch + 2;
					p->jres[n] = st.hres;
					p->jres_i[n] = ix;
					p->jres_px[n] = pc;
					p->jres_ep[n] = p->jep[n];
					break;
				}
				if (t2 > tl) {
					if (t2 - 2 >= dis) { idled = 1; break; }'''),
])
edit('src/cpu/p8x32a/p8x32ajit.cpp', [
    (r'''	unsigned op = op_of(i);
	if (outa && op <= 2) return fwr(i) && !fwc(i) && dst_of(i) < 0x1F0 && (fim(i) || src_of(i) < 0x1F0);
	if (!op_ok(op)) return false;
	if (!fim(i) && src_of(i) >= 0x1F0 && (src_of(i) > 0x1F1 || is_jump(op))) return false;''',
     r'''	unsigned op = op_of(i);
	if (outa && op <= 2) return fwr(i) && !fwc(i) && dst_of(i) < 0x1F0 && (fim(i) || src_of(i) < 0x1F0);
	// a hub read or write that ends a block (as run_local's dec jh); one that never runs is a no-op
	if (op <= 2) return !fwc(i) && (fim(i) || src_of(i) <= 0x1F0) && (!fwr(i) || dst_of(i) < 0x1F0);
	if (!op_ok(op)) return false;
	if (!fim(i) && src_of(i) >= 0x1F0 && (src_of(i) > 0x1F1 || is_jump(op))) return false;'''),
    (r'''	unsigned base;
	uint64_t tail, tail_ret;

	// k instructions ran in this block; the last one's exit state is in st
	void leave(unsigned k)''',
     r'''	unsigned base;
	uint64_t tail, tail_ret;
	bool outa;
	Label res; // the block's resume entry

	// the next instruction at st.latch + 7 after a hub read or write counted as instruction k of the run (the tail
	// adds k + 1): T2 and the budget from there
	void after_hub(unsigned k)
	{
		Label fit = a.new_label();
		a.mov(x86::rax, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, latch)));
		a.lea(x86::rsi, x86::ptr(x86::rax, 7));
		a.lea(T2, x86::ptr(x86::rax, (int)(7 - 4 * (k + 1))));
		a.mov(BUDGET, k + 1);
		a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tl)));
		a.cmp(x86::rsi, x86::rdi);
		a.ja(fit);
		a.sub(x86::rdi, x86::rsi);
		a.shr(x86::rdi, 2);
		a.lea(BUDGET, x86::ptr(x86::rdi, (int)(k + 2)));
		a.bind(fit);
	}
	// the hub read or write i at h = st.latch + 2 (rax: st.latch), s = ecx, d = edx, run as event_run runs it;
	// FL updated. A read is done here, a write through st.hubfn
	void hub_access(uint32_t i)
	{
		if (fwr(i)) {
			unsigned op = op_of(i), sz = op == 2 ? 4 : op == 1 ? 2 : 1, dst = dst_of(i);
			Label same = a.new_label(), first = a.new_label(), nochk = a.new_label();
			a.lea(x86::rsi, x86::ptr(x86::rax, 2));
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, pnow)));
			a.mov(x86::qword_ptr(x86::rdi), x86::rsi);
			a.and_(x86::ecx, 0x10000 - sz);
			// test builds: the time-order check of the read
			a.cmp(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, chkfn)), 0);
			a.je(nochk);
			a.push(x86::r8);
			a.push(x86::r9);
			a.push(x86::r10);
			a.push(x86::rcx);
			a.mov(x86::rdi, ST);
			a.mov(x86::esi, x86::ecx);
			a.mov(x86::edx, sz);
			a.sub(x86::rsp, 8);
			a.call(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, chkfn)));
			a.add(x86::rsp, 8);
			a.pop(x86::rcx);
			a.pop(x86::r10);
			a.pop(x86::r9);
			a.pop(x86::r8);
			a.bind(nochk);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hub)));
			if (op == 0) a.movzx(x86::eax, x86::byte_ptr(x86::rdi, x86::rcx));
			else if (op == 1) a.movzx(x86::eax, x86::word_ptr(x86::rdi, x86::rcx));
			else a.mov(x86::eax, x86::dword_ptr(x86::rdi, x86::rcx));
			// the write to D, as a block's instruction writes
			a.cmp(cog(dst), x86::eax);
			a.je(same);
			a.mov(x86::esi, cog(dst));
			a.mov(cog(dst), x86::eax);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
			a.mov(x86::byte_ptr(x86::rdi, (int)offsetof(p8x32a_loop, dirty)), 1);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, code)));
			a.bt(x86::dword_ptr(x86::rdi, (int)((dst >> 5) * 4)), dst & 31);
			a.jnc(same);
			a.cmp(stf(offsetof(p8x32a_jst, inv)), 0);
			a.je(first);
			a.mov(stf(offsetof(p8x32a_jst, inv)), -1);
			a.jmp(same);
			a.bind(first);
			a.mov(stf(offsetof(p8x32a_jst, inv_old)), x86::esi);
			a.mov(stf(offsetof(p8x32a_jst, inv)), dst + 1);
			a.bind(same);
			if (fwz(i)) {
				a.xor_(x86::esi, x86::esi);
				a.test(x86::eax, x86::eax);
				a.sete(x86::sil);
				a.and_(FL, ~1u);
				a.or_(FL, x86::esi);
			}
			return;
		}
		hub_call(i);
	}
	// hubfn(st, s = ecx, d = edx, i, FL) into FL; r8-r10 kept (the stack is 16-byte aligned after three pushes)
	void hub_call(uint32_t i)
	{
		a.push(x86::r8);
		a.push(x86::r9);
		a.push(x86::r10);
		a.mov(x86::rdi, ST);
		a.mov(x86::esi, x86::ecx);
		a.mov(x86::ecx, i);
		a.call(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hubfn)));
		a.pop(x86::r10);
		a.pop(x86::r9);
		a.pop(x86::r8);
		a.mov(FL, x86::eax);
	}
	// k instructions ran in this block; the last one's exit state is in st
	void leave(unsigned k)'''),
    (r'''		a.jmp(x86::rsi);
	}
	// stop before slot k: the run so far ends with a sequential fetch of slot k
	void stop_before(unsigned k, const x86::Gp &word)
	{
		a.mov(stf(offsetof(p8x32a_jst, pc)), (base + k) & 511);''',
     r'''		a.jmp(x86::rsi);
	}
	// stop before slot k: the run so far ends with a sequential fetch of slot k; back to run_local if ret, else on
	// through the tail
	void stop_before(unsigned k, const x86::Gp &word, bool ret = false)
	{
		a.mov(stf(offsetof(p8x32a_jst, pc)), (base + k) & 511);'''),
    (r'''		a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		leave(k);
	}
''',
     r'''		a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		if (!ret) {
			leave(k);
			return;
		}
		// the tail's count of a run without a jump, then its return
		a.add(TOTAL, k);
		a.lea(T2, x86::ptr(T2, (int)(4 * k)));
		a.sub(BUDGET, k);
		a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
		a.add(x86::word_ptr(x86::rdi, (int)offsetof(p8x32a_loop, nins)), k);
		a.mov(x86::rsi, tail_ret);
		a.jmp(x86::rsi);
	}
'''),
    (r'''			} else a.mov(x86::ecx, cog(src_of(i)));
			a.mov(x86::edx, cog(dst_of(i)));
		}
		switch (op) {''',
     r'''			} else a.mov(x86::ecx, cog(src_of(i)));
			a.mov(x86::edx, cog(dst_of(i)));
		}
		if (op <= 2 && !outa && cond != 0) {
			// a hub read or write ending the block: its slot as next_slot finds it
			Label sb = a.new_label(), got = a.new_label(), wait = a.new_label();
			a.lea(x86::rax, x86::ptr(T2, (int)(4 * k + 1)));
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, slot)));
			a.cmp(x86::rax, x86::rdi);
			a.jbe(sb);
			a.sub(x86::rax, x86::rdi);
			a.add(x86::rax, 15);
			a.and_(x86::rax, -16);
			a.add(x86::rax, x86::rdi);
			a.jmp(got);
			a.bind(sb);
			a.mov(x86::rax, x86::rdi);
			a.bind(got);
			// run now as event_run would: h = latch + 2 by t, its key below lim, latch + 5 before dis, gen unchanged
			a.lea(x86::rsi, x86::ptr(x86::rax, 2));
			a.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, t)));
			a.ja(wait);
			a.shl(x86::rsi, 4);
			a.mov(x86::edi, stf(offsetof(p8x32a_jst, n)));
			a.or_(x86::rsi, x86::rdi);
			a.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, lim)));
			a.jae(wait);
			a.lea(x86::rsi, x86::ptr(x86::rax, 5));
			a.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, dis)));
			a.jae(wait);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, pgen)));
			a.mov(x86::esi, x86::dword_ptr(x86::rdi));
			a.cmp(x86::esi, stf(offsetof(p8x32a_jst, gen)));
			a.jne(wait);
			a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, latch)), x86::rax);
			hub_access(i);
			after_hub(k);
			a.jmp(skip);
			a.bind(wait);
			// it waits: the block stops before it
			a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hlatch)), x86::rax);
			a.mov(stf(offsetof(p8x32a_jst, hs)), x86::ecx);
			a.mov(stf(offsetof(p8x32a_jst, hd)), x86::edx);
			a.mov(stf(offsetof(p8x32a_jst, hiss)), 1);
			a.lea(x86::rax, x86::ptr(res));
			a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hres)), x86::rax);
			a.mov(x86::esi, i);
			stop_before(k, x86::esi, true);
			a.bind(skip);
			goto end;
		}
		switch (op) {'''),
    (r'''	p8x32a_jblk *b = old;
	unsigned len = 0, k;

	if (!b) {''',
     r'''	p8x32a_jblk *b = old;
	unsigned len = 0, k;
	bool hubslot;

	if (!b) {'''),
    (r'''	b->part = outa != 0;
	b->words[0] = ix;
	// the run: translatable words below the special registers, ending at an unconditional jump or a slot whose D field may point
	// anywhere; a slot whose op, flags or condition have changed is not translated''',
     r'''	b->part = outa != 0;
	b->words[0] = ix;
	// slot 0's S and D fields may change even when it is not translated (a block of none): jit_get compares the rest
	if (var[a]) b->dyn = 1;
	// the run: translatable words below the special registers, ending at an unconditional jump or a slot whose D field may point
	// anywhere; a slot whose op, flags or condition have changed is not translated'''),
    (r'''		uint32_t w = len ? ram[a + len] : ix, v = var[a + len];
		bool dyn = v != 0;
		if ((v & ~P8X32A_JDYN) || !(op_ok(op_of(w)) || (outa && op_of(w) <= 2 && !dyn))) break;
		if (!dyn && !supported(w, outa)) break;
		b->words[len] = w;
		if (dyn) b->dyn |= 1u << len;
		len++;
		if ((is_jump(op_of(w)) && cond_of(w) == 15) || (dyn && fwr(w))) break;
	}
	// a fixed instruction must not rewrite a later fixed slot other than the next, whose word it already fetched''',
     r'''		uint32_t w = len ? ram[a + len] : ix, v = var[a + len];
		bool dyn = v != 0;
		if ((v & ~P8X32A_JDYN) || !(op_ok(op_of(w)) || (op_of(w) <= 2 && !dyn))) break;
		if (!dyn && !supported(w, outa)) break;
		b->words[len] = w;
		if (dyn) b->dyn |= 1u << len;
		len++;
		hubslot = !outa && op_of(w) <= 2 && cond_of(w) != 0;
		if ((is_jump(op_of(w)) && cond_of(w) == 15) || (dyn && fwr(w)) || hubslot) break;
	}
	// a fixed instruction must not rewrite a later fixed slot other than the next, whose word it already fetched'''),
    (r'''	as.xor_(TOTAL, TOTAL);
	as.bind(body);
	Emit e = { as, frame, a, outa ? j->ltail : j->tail, outa ? j->ltail_ret : j->tail_ret };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);''',
     r'''	as.xor_(TOTAL, TOTAL);
	as.bind(body);
	Label res = as.new_label();
	Emit e = { as, frame, a, outa ? j->ltail : j->tail, outa ? j->ltail_ret : j->tail_ret, outa != 0, res };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);'''),
    (r'''		}
		e.insn(k, b->words[k], (b->dyn >> k & 1) != 0, last, last || next_dyn);
	}
	uint32_t (*fn)(p8x32a_jst *) = NULL;''',
     r'''		}
		e.insn(k, b->words[k], (b->dyn >> k & 1) != 0, last, last || next_dyn);
	}
	// a block that ends with a hub read or write: an entry that completes it once due (run_local, EV_HUB)
	k = len - 1;
	bool hub = !outa && !(b->dyn >> k & 1) && op_of(b->words[k]) <= 2 && cond_of(b->words[k]) != 0;
	if (hub) {
		as.bind(res);
		as.emit_prolog(frame);
		as.emit_args_assignment(frame, args);
		as.mov(RAM, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, ram)));
		as.mov(FL, stf(offsetof(p8x32a_jst, fl)));
		as.xor_(TOTAL, TOTAL);
		as.mov(NEXTW, stf(offsetof(p8x32a_jst, nix)));
		as.mov(x86::ecx, stf(offsetof(p8x32a_jst, hs)));
		as.mov(x86::edx, stf(offsetof(p8x32a_jst, hd)));
		as.mov(x86::rax, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, latch)));
		e.hub_access(b->words[k]);
		// counted as one instruction of a run that goes on at the next address
		e.after_hub(0);
		as.mov(stf(offsetof(p8x32a_jst, pc)), (a + len) & 511);
		as.mov(stf(offsetof(p8x32a_jst, px)), (a + len) & 511);
		as.mov(stf(offsetof(p8x32a_jst, nix)), NEXTW);
		as.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		as.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		e.leave(1);
	}
	uint32_t (*fn)(p8x32a_jst *) = NULL;'''),
])
EOF
```

- [ ] **Step 4: The mutants are caught**

```sh
P8X32A_JIT=1 MUTANTS="hub-key32 hub-gen hub-report" nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
for o in "" -DP8X32A_JIT; do cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic $o -DP8X32A_CHECK -Isrc/cpu/p8x32a -c src/cpu/p8x32a/p8x32a.c -o /dev/null && echo "p8x32a.c ${o:-interpreted}: no warnings"; done
```

Expected (under a minute; the suites build with these warnings as errors):

```text
unmutated core (none-translated): all cases match
mutant hub-key32 caught by 89 cases (first m800001.spin at t0)
mutant hub-gen caught by 16 cases (first event_order.spin at 4096)
mutant hub-report caught by 4 cases (first jit_hubcode.spin at 4096)
p8x32a.c interpreted: no warnings
p8x32a.c -DP8X32A_JIT: no warnings
```

- [ ] **Step 5: America's Most Haunted byte-identical**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12d.log 2>&1; tail -1 build/sdl3pinmame/12d.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep -E 'bench video opt|determinism|FAIL'
```

Expected (about 3 minutes):

```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/amh.nv
bench video opt: 0.670x over 24.0 s emulated (35.8 s wall), whole run 14.5 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.417x, load 1.68
determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 6: Commit**

```sh
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32ajit.cpp tests/pinheck/p8x32a/mutate_core.sh tests/pinheck/p8x32a/chip/jit_hubcode.spin
git commit -m "p8x32a: a hub read or write ends a translated block, run there or completed by the block's resume entry"
```

### Task 5: A link table and per-exit chains

**Files:**
- Create: `tests/pinheck/p8x32a/chip/jit_chain.spin`
- Modify: `tests/pinheck/p8x32a/mutate_core.sh`, `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32ajit.cpp`

**Interfaces:**
- Consumes: Task 4's blocks and exits; `jit_written`, `jit_drop`, `jit_get`.
- Produces: `p8x32a_jlink`, `p8x32a.jlink`, `p8x32a_jst.link`, `jit_void`; `Emit::chain`, `Emit::seq_exit`, `build_edge`; the shared tail through the link table; the mutants `link-void`, `exit-k0`.

- [ ] **Step 1: The tests**

`chip/jit_chain.spin` jumps into a block whose first slot's S field sometimes names `CNT`, so the block stops before its first slot when another block's exit reaches it; two mutants keep a block's link when the block is dropped, and leave that stop without its `px`:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)

open('tests/pinheck/p8x32a/chip/jit_chain.spin', 'w').write(r'''PUB main
DAT
        long    $C0DE5EED
        org     0
' a block reached from another block's exit whose first slot no longer fits (its S field now names CNT): the run
' goes back to the interpreter at that slot; a hub read ends the loop's first block. Results at $6000
entry   mov     k, #300
lp      rdlong  h, hp
        add     acc, h
        test    k, #3 wz
  if_z  movs    lb, #$1F1
  if_nz movs    lb, #r1
        add     r1, #7
        jmp     #lb
        nop
lb      mov     acc2, 0-0
        xor     acc3, acc2
        rol     acc3, #1
        djnz    k, #lp
        wrlong  acc, r0
        wrlong  acc3, r4
        cogid   k
        cogstop k
hp      long    $6100
r0      long    $6000
r4      long    $6004
k       long    0
h       long    0
acc     long    0
acc2    long    0
acc3    long    0
r1      long    $1234
        long    $C0DEE0D0
''')
edit('tests/pinheck/p8x32a/mutate_core.sh', [
    (r'''mutant hub-report p8x32ajit.cpp '			a.bt(x86::dword_ptr(x86::rdi, (int)((dst >> 5) * 4)), dst & 31);
			a.jnc(same);' '			a.jmp(same);' 1
exit $((fail != 0))
''',
     r'''mutant hub-report p8x32ajit.cpp '			a.bt(x86::dword_ptr(x86::rdi, (int)((dst >> 5) * 4)), dst & 31);
			a.jnc(same);' '			a.jmp(same);' 1
# a block whose link outlives it; a block reached from another's exit that stops before its first slot without its
# exit state
mutant link-void p8x32a.c '	p->jlink[n][a].len = ~0u;' '' 1
mutant exit-k0 p8x32ajit.cpp '				a.mov(stf(offsetof(p8x32a_jst, px)), base);
				a.mov(stf(offsetof(p8x32a_jst, nix)), CURW);' '				a.mov(stf(offsetof(p8x32a_jst, nix)), CURW);' 1
exit $((fail != 0))
'''),
])
EOF
```

- [ ] **Step 2: Run them to see them fail**

```sh
P8X32A_JIT=1 MUTANTS="link-void exit-k0" nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
```

Expected (under a minute):

```text
unmutated core (none-translated): all cases match
MUTANT link-void: no match
MUTANT exit-k0: no match
```

- [ ] **Step 3: The links and the exits**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)

edit('src/cpu/p8x32a/p8x32a.h', [
    (r'''typedef struct p8x32a_jblk p8x32a_jblk;

typedef struct p8x32a_jst {
	uint32_t *ram;
	const uint8_t *code; /* bit s: slot s is a fixed word of a block; a write that changes it is reported in inv */
	p8x32a_jblk **tab;   /* the cog's blocks by address */
	p8x32a_loop *loop;
	uint64_t t2;''',
     r'''typedef struct p8x32a_jblk p8x32a_jblk;

/* what a block's exit needs of the block at a cog address: its entry for another block, the word it starts with
   under mask (~P8X32A_JDYN when slot 0 is read at run time), and its length (~0: none to run) */
typedef struct p8x32a_jlink {
	const void *body;
	uint32_t word, mask, len, pad;
	uint64_t pad2;
} p8x32a_jlink;

typedef struct p8x32a_jst {
	uint32_t *ram;
	const uint8_t *code; /* bit s: slot s is a fixed word of a block; a write that changes it is reported in inv */
	p8x32a_jblk **tab;   /* the cog's blocks by address */
	const p8x32a_jlink *link; /* and their links */
	p8x32a_loop *loop;
	uint64_t t2;'''),
    (r'''	void *jit;
	p8x32a_jblk *jblk[8][512];
	/* the resume entry of the block that issued cog n's hub read or write jres_i at jres_px - 1, while jep[n] (counts the
	   cog's blocks translated again, which frees their code) is jres_ep[n] */''',
     r'''	void *jit;
	p8x32a_jblk *jblk[8][512];
	p8x32a_jlink jlink[8][512];
	/* the resume entry of the block that issued cog n's hub read or write jres_i at jres_px - 1, while jep[n] (counts the
	   cog's blocks translated again, which frees their code) is jres_ep[n] */'''),
])
edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''}
static void jit_drop(p8x32a *p, int n);

''',
     r'''}
static void jit_drop(p8x32a *p, int n);

/* a block no longer runs: its link says so */
P8_INLINE void jit_void(p8x32a *p, int n, unsigned a)
{
	p->jblk[n][a]->valid = 0;
	p->jlink[n][a].len = ~0u;
}

'''),
    (r'''	for (k = (int)s; k >= 0 && k > (int)s - P8X32A_JMAX; k--) {
		p8x32a_jblk *b = p->jblk[n][k];
		if (b && b->valid && (unsigned)k + b->len > s && !(b->dyn >> (s - (unsigned)k) & 1)) b->valid = 0;
	}
	p->jcode[n][s >> 3] &= (uint8_t)~(1u << (s & 7));''',
     r'''	for (k = (int)s; k >= 0 && k > (int)s - P8X32A_JMAX; k--) {
		p8x32a_jblk *b = p->jblk[n][k];
		if (b && b->valid && (unsigned)k + b->len > s && !(b->dyn >> (s - (unsigned)k) & 1)) jit_void(p, n, (unsigned)k);
	}
	p->jcode[n][s >> 3] &= (uint8_t)~(1u << (s & 7));'''),
    (r'''	b = p->jit_build(p->jit, b, a, ix, p->cog[n].ram, var, p->lz_on && n == p->lz);
	p->jblk[n][a] = b;

	if (b && b->valid)''',
     r'''	b = p->jit_build(p->jit, b, a, ix, p->cog[n].ram, var, p->lz_on && n == p->lz);
	p->jblk[n][a] = b;
	if (b) {
		p8x32a_jlink *ln = &p->jlink[n][a];
		ln->body = b->body;
		ln->word = b->words[0];
		ln->mask = (b->dyn & 1) ? ~P8X32A_JDYN : ~0u;
		ln->len = b->valid && b->len && b->fn ? b->len : ~0u;
	}

	if (b && b->valid)'''),
    (r'''	st.code = p->jcode[n];
	st.tab = p->jblk[n];
	st.loop = l;
	st.par = (c->ptr >> 14) << 2;''',
     r'''	st.code = p->jcode[n];
	st.tab = p->jblk[n];
	st.link = p->jlink[n];
	st.loop = l;
	st.par = (c->ptr >> 14) << 2;'''),
    (r'''					unsigned j;
					for (j = 0; j < 512; j++)
						if (p->jblk[n][j]) p->jblk[n][j]->valid = 0;
					memset(p->jcode[n], 0, sizeof(p->jcode[n]));
				} else if (st.inv)''',
     r'''					unsigned j;
					for (j = 0; j < 512; j++)
						if (p->jblk[n][j]) jit_void(p, n, j);
					memset(p->jcode[n], 0, sizeof(p->jcode[n]));
				} else if (st.inv)'''),
    (r'''	unsigned j;
	for (j = 0; j < 512; j++)
		if (p->jblk[n][j]) p->jblk[n][j]->valid = 0;
	memset(p->jcode[n], 0, sizeof(p->jcode[n]));
}''',
     r'''	unsigned j;
	for (j = 0; j < 512; j++)
		if (p->jblk[n][j]) jit_void(p, n, j);
	memset(p->jcode[n], 0, sizeof(p->jcode[n]));
}'''),
    (r'''	memset(p, 0, sizeof(*p));
	p->bus = *bus;
	for (n = 0; n < 8 * 512; n++) dec_fill(&p->dec[0][0] + n, 0);
	p8x32a_reset(p, 0);
}''',
     r'''	memset(p, 0, sizeof(*p));
	p->bus = *bus;
	for (n = 0; n < 8 * 512; n++) {
		dec_fill(&p->dec[0][0] + n, 0);
		(&p->jlink[0][0] + n)->len = ~0u;
	}
	p8x32a_reset(p, 0);
}'''),
])
edit('src/cpu/p8x32a/p8x32ajit.cpp', [
    (r'''	uint64_t tail, tail_ret; // the code a block leaves through, and its return to run_local
	uint64_t ltail, ltail_ret; // the same for a lazy cog's blocks
};
''',
     r'''	uint64_t tail, tail_ret; // the code a block leaves through, and its return to run_local
	uint64_t ltail, ltail_ret; // the same for a lazy cog's blocks
	uint64_t edge; // run_local's loop_edge search step for a backward jump (called)
};
'''),
    (r'''	bool outa;
	Label res; // the block's resume entry

	// the next instruction at st.latch + 7 after a hub read or write counted as instruction k of the run (the tail''',
     r'''	bool outa;
	Label res; // the block's resume entry
	uint64_t edge;
	bool writes; // the block writes cog RAM (st.inv may be set)

	// the tail's work for a run of cnt instructions ending here, on into the block at the next address (pxc, or ecx
	// when -1) if it fits: the count, a backward jump's loop search step, the link; else to slow (accounted).
	// word: the next instruction's word (a register other than rax, rcx, rdx, rdi, rsi, or st.nix when none)
	void chain(unsigned cnt, int pxc, bool backward, const x86::Gp *word, Label slow)
	{
		a.add(TOTAL, cnt);
		a.lea(T2, x86::ptr(T2, (int)(4 * cnt)));
		a.sub(BUDGET, cnt);
		a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
		a.add(x86::word_ptr(x86::rdi, (int)offsetof(p8x32a_loop, nins)), cnt);
		if (pxc >= 0) a.mov(x86::ecx, pxc);
		if (backward) {
			Label fwd = a.new_label();
			if (pxc < 0) {
				a.cmp(x86::ecx, (base + cnt) & 511);
				a.jae(fwd);
			}
			a.mov(x86::rsi, edge);
			a.call(x86::rsi);
			a.test(x86::eax, x86::eax);
			a.jnz(slow);
			a.bind(fwd);
		}
		if (writes) {
			a.cmp(stf(offsetof(p8x32a_jst, inv)), 0);
			a.jne(slow);
		}
		if (pxc < 0) {
			a.cmp(x86::ecx, 511);
			a.je(slow);
		} else if (pxc == 511) {
			a.jmp(slow);
			return;
		}
		a.shl(x86::ecx, 5);
		a.add(x86::rcx, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, link)));
		a.mov(x86::eax, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, len)));
		a.cmp(x86::eax, BUDGET);
		a.ja(slow);
		if (word) a.mov(x86::eax, *word);
		else a.mov(x86::eax, stf(offsetof(p8x32a_jst, nix)));
		a.mov(x86::edx, x86::eax);
		a.xor_(x86::edx, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, word)));
		a.and_(x86::edx, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, mask)));
		a.jnz(slow);
		a.mov(stf(offsetof(p8x32a_jst, ix)), x86::eax);
		a.jmp(x86::qword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, body)));
	}
	// the run of cnt instructions ends with a sequential fetch of the slot at address next (word in wreg): on into the
	// next block, else back to run_local with the exit state
	void seq_exit(unsigned cnt, unsigned next, const x86::Gp &wreg)
	{
		Label slow = a.new_label();
		chain(cnt, (int)next, false, &wreg, slow);
		a.bind(slow);
		a.mov(stf(offsetof(p8x32a_jst, pc)), next);
		a.mov(stf(offsetof(p8x32a_jst, px)), next);
		a.mov(stf(offsetof(p8x32a_jst, nix)), wreg);
		a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		a.mov(x86::rsi, tail_ret);
		a.jmp(x86::rsi);
	}

	// the next instruction at st.latch + 7 after a hub read or write counted as instruction k of the run (the tail'''),
    (r'''	void stop_before(unsigned k, const x86::Gp &word, bool ret = false)
	{
		a.mov(stf(offsetof(p8x32a_jst, pc)), (base + k) & 511);
		a.mov(stf(offsetof(p8x32a_jst, px)), (base + k) & 511);''',
     r'''	void stop_before(unsigned k, const x86::Gp &word, bool ret = false)
	{
		if (!outa && !ret && k && (word == NEXTW || word == CURW)) {
			seq_exit(k, (base + k) & 511, word);
			return;
		}
		a.mov(stf(offsetof(p8x32a_jst, pc)), (base + k) & 511);
		a.mov(stf(offsetof(p8x32a_jst, px)), (base + k) & 511);'''),
    (r'''		}
		if (op >= OP_AND && op <= OP_MUXNZ && fwc(i)) parity_to_r11(a);
		if (jump) {
			// the word at the target is fetched before the write
			a.mov(stf(offsetof(p8x32a_jst, s)), x86::ecx);''',
     r'''		}
		if (op >= OP_AND && op <= OP_MUXNZ && fwc(i)) parity_to_r11(a);
		if (jump && !outa) {
			// the word at the target is fetched before the write; the rest of the exit state after it
			a.mov(x86::esi, x86::ecx);
			a.and_(x86::esi, 511);
			a.mov(x86::esi, cogr(x86::esi));
			a.mov(stf(offsetof(p8x32a_jst, nix)), x86::esi);
		} else if (jump) {
			// the word at the target is fetched before the write
			a.mov(stf(offsetof(p8x32a_jst, s)), x86::ecx);'''),
    (r'''			a.inc(stf(offsetof(p8x32a_jst, on)));
		}
		if (jump) leave(k + 1);
		a.bind(skip);
	end:''',
     r'''			a.inc(stf(offsetof(p8x32a_jst, on)));
		}
		if (jump && !outa) {
			Label slow = a.new_label(), go = a.new_label();
			unsigned pc = (base + k + 1) & 511;
			bool fixed = imm && !dyn;
			// a jump that did not jump: its exit state, through the tail (it returns: the next instruction is cancelled)
			if (op != OP_JMP) {
				if (op == OP_DJNZ) a.cmp(x86::edx, 1);
				else a.test(x86::edx, x86::edx);
				if (op == OP_TJZ) a.je(go);
				else a.jne(go);
				a.mov(stf(offsetof(p8x32a_jst, s)), x86::ecx);
				a.mov(stf(offsetof(p8x32a_jst, d)), x86::edx);
				a.mov(x86::esi, x86::ecx);
				a.and_(x86::esi, 511);
				a.mov(stf(offsetof(p8x32a_jst, px)), x86::esi);
				a.mov(stf(offsetof(p8x32a_jst, jc)), 1);
				a.mov(stf(offsetof(p8x32a_jst, jmp)), 1);
				a.mov(stf(offsetof(p8x32a_jst, pc)), pc);
				if (dyn) a.mov(stf(offsetof(p8x32a_jst, w)), CURW);
				else a.mov(stf(offsetof(p8x32a_jst, w)), i);
				leave(k + 1);
				a.bind(go);
			}
			// s and d kept for the slow path (the chain uses eax, ecx, edx, rsi, rdi)
			a.mov(stf(offsetof(p8x32a_jst, s)), x86::ecx);
			a.mov(stf(offsetof(p8x32a_jst, d)), x86::edx);
			if (!fixed) a.and_(x86::ecx, 511);
			chain(k + 1, fixed ? (int)(src_of(i) & 511) : -1, !fixed || (src_of(i) & 511) < pc, NULL, slow);
			a.bind(slow);
			a.mov(x86::esi, stf(offsetof(p8x32a_jst, s)));
			a.and_(x86::esi, 511);
			a.mov(stf(offsetof(p8x32a_jst, px)), x86::esi);
			a.mov(stf(offsetof(p8x32a_jst, pc)), pc);
			if (dyn) a.mov(stf(offsetof(p8x32a_jst, w)), CURW);
			else a.mov(stf(offsetof(p8x32a_jst, w)), i);
			a.mov(stf(offsetof(p8x32a_jst, jmp)), 1);
			a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
			a.mov(x86::rsi, tail_ret);
			a.jmp(x86::rsi);
		} else if (jump) leave(k + 1);
		a.bind(skip);
	end:'''),
    (r'''			a.bind(before);
			if (k == 0) {
				a.mov(x86::rsi, tail_ret);
				a.jmp(x86::rsi);''',
     r'''			a.bind(before);
			if (k == 0) {
				// a block reached from another's exit: the run so far ends with a sequential fetch of this slot
				a.mov(stf(offsetof(p8x32a_jst, pc)), base);
				a.mov(stf(offsetof(p8x32a_jst, px)), base);
				a.mov(stf(offsetof(p8x32a_jst, nix)), CURW);
				a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
				a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
				a.mov(x86::rsi, tail_ret);
				a.jmp(x86::rsi);'''),
    (r'''	make_frame(func, frame, code.environment());
	frame.finalize();
	Label ret = a.new_label(), no_edge = a.new_label(), diff = a.new_label(), reset = a.new_label(), full = a.new_label();
	const x86::Gp L = x86::rdi;
''',
     r'''	make_frame(func, frame, code.environment());
	frame.finalize();
	Label ret = a.new_label(), no_edge = a.new_label(), diff = a.new_label(), reset = a.new_label();
	const x86::Gp L = x86::rdi;
'''),
    (r'''	a.cmp(x86::ecx, 511);
	a.je(ret);
	a.mov(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tab)));
	a.mov(x86::rsi, x86::qword_ptr(x86::rsi, x86::rcx, 3));
	a.test(x86::rsi, x86::rsi);
	a.jz(ret);
	a.cmp(x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, valid)), 0);
	a.je(ret);
	a.mov(x86::eax, x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, len)));
	a.test(x86::eax, x86::eax);
	a.jz(ret);
	a.cmp(x86::eax, BUDGET);
	a.ja(ret);
	a.mov(x86::edx, stf(offsetof(p8x32a_jst, nix)));
	a.xor_(x86::edx, x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, words)));
	a.test(x86::byte_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, dyn)), 1);
	a.jz(full);
	a.and_(x86::edx, ~P8X32A_JDYN);
	a.bind(full);
	a.test(x86::edx, x86::edx);
	a.jnz(ret);
	a.mov(x86::eax, stf(offsetof(p8x32a_jst, nix)));
	a.mov(stf(offsetof(p8x32a_jst, ix)), x86::eax);
	a.jmp(x86::qword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, body)));
	a.bind(ret);
	a.mov(stf(offsetof(p8x32a_jst, fl)), FL);''',
     r'''	a.cmp(x86::ecx, 511);
	a.je(ret);
	a.shl(x86::ecx, 5);
	a.add(x86::rcx, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, link)));
	a.mov(x86::eax, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, len)));
	a.cmp(x86::eax, BUDGET);
	a.ja(ret);
	a.mov(x86::edx, stf(offsetof(p8x32a_jst, nix)));
	a.mov(x86::eax, x86::edx);
	a.xor_(x86::edx, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, word)));
	a.and_(x86::edx, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, mask)));
	a.jnz(ret);
	a.mov(stf(offsetof(p8x32a_jst, ix)), x86::eax);
	a.jmp(x86::qword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, body)));
	a.bind(ret);
	a.mov(stf(offsetof(p8x32a_jst, fl)), FL);'''),
    (r'''}

} // namespace
''',
     r'''}

// run_local's loop_edge search step for a backward jump to ecx with the next instruction at T2 (called from a block's
// exit): returns eax = 1 with st.edge = 1 when run_local must pass it to loop_edge; uses rax, rdx, rdi
bool build_edge(P8Jit *j)
{
	CodeHolder code;
	code.init(j->rt.environment());
	x86::Assembler a(&code);
	Label ret = a.new_label(), diff = a.new_label(), reset = a.new_label(), edge = a.new_label();
	const x86::Gp L = x86::rdi;
	a.mov(L, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
	a.movzx(x86::edx, x86::word_ptr(L, (int)offsetof(p8x32a_loop, head)));
	a.lea(x86::rax, x86::ptr(T2, -3));
	a.cmp(x86::ecx, x86::edx);
	a.jne(diff);
	a.cmp(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.jne(reset);
	a.cmp(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), P8X32A_PAT / 2);
	a.ja(reset);
	a.cmp(x86::rax, x86::qword_ptr(L, (int)offsetof(p8x32a_loop, head_t)));
	a.jbe(reset);
	a.jmp(edge);
	a.bind(diff);
	a.cmp(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.jne(reset);
	a.cmp(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), P8X32A_PAT / 2);
	a.ja(reset);
	a.cmp(x86::edx, 0xFFFF);
	a.jne(ret);
	a.bind(reset);
	a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, head)), x86::cx);
	a.mov(x86::qword_ptr(L, (int)offsetof(p8x32a_loop, head_t)), x86::rax);
	a.mov(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.mov(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, hub)), 0);
	a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), 0);
	a.bind(ret);
	a.xor_(x86::eax, x86::eax);
	a.ret();
	a.bind(edge);
	a.mov(stf(offsetof(p8x32a_jst, edge)), 1);
	a.mov(x86::eax, 1);
	a.ret();
	void *fn = NULL;
	if (j->rt.add(&fn, &code) != kErrorOk) return false;
	j->edge = (uint64_t)(uintptr_t)fn;
	return true;
}

} // namespace
'''),
    (r'''{
	P8Jit *j = new (std::nothrow) P8Jit();
	if (j && (!build_tail(j, false) || !build_tail(j, true))) { delete j; j = NULL; }
	return j;
}''',
     r'''{
	P8Jit *j = new (std::nothrow) P8Jit();
	if (j && (!build_tail(j, false) || !build_tail(j, true) || !build_edge(j))) { delete j; j = NULL; }
	return j;
}'''),
    (r'''	as.bind(body);
	Label res = as.new_label();
	Emit e = { as, frame, a, outa ? j->ltail : j->tail, outa ? j->ltail_ret : j->tail_ret, outa != 0, res };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);''',
     r'''	as.bind(body);
	Label res = as.new_label();
	bool writes = false;
	for (k = 0; k < len; k++) writes = writes || fwr(b->words[k]) || (b->dyn >> k & 1);
	Emit e = { as, frame, a, outa ? j->ltail : j->tail, outa ? j->ltail_ret : j->tail_ret, outa != 0, res, j->edge, writes };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);'''),
    (r'''		// counted as one instruction of a run that goes on at the next address
		e.after_hub(0);
		as.mov(stf(offsetof(p8x32a_jst, pc)), (a + len) & 511);
		as.mov(stf(offsetof(p8x32a_jst, px)), (a + len) & 511);
		as.mov(stf(offsetof(p8x32a_jst, nix)), NEXTW);
		as.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		as.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		e.leave(1);
	}
	uint32_t (*fn)(p8x32a_jst *) = NULL;''',
     r'''		// counted as one instruction of a run that goes on at the next address
		e.after_hub(0);
		e.seq_exit(1, (a + len) & 511, NEXTW);
	}
	uint32_t (*fn)(p8x32a_jst *) = NULL;'''),
])
EOF
```

- [ ] **Step 4: The mutants are caught**

```sh
P8X32A_JIT=1 MUTANTS="link-void exit-k0" nice -n 15 tests/pinheck/p8x32a/mutate_core.sh
for o in "" -DP8X32A_JIT; do cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic $o -DP8X32A_CHECK -Isrc/cpu/p8x32a -c src/cpu/p8x32a/p8x32a.c -o /dev/null && echo "p8x32a.c ${o:-interpreted}: no warnings"; done
```

Expected (under a minute):

```text
unmutated core (none-translated): all cases match
mutant link-void caught by 15 cases (first lazy.spin at 4096)
mutant exit-k0 caught by 2 cases (first jit_chain.spin at 4096)
p8x32a.c interpreted: no warnings
p8x32a.c -DP8X32A_JIT: no warnings
```

- [ ] **Step 5: America's Most Haunted byte-identical**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12d.log 2>&1; tail -1 build/sdl3pinmame/12d.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep -E 'bench video opt|determinism|FAIL'
```

Expected (about 3 minutes):

```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/amh.nv
bench video opt: 0.701x over 24.0 s emulated (34.2 s wall), whole run 14.1 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.431x, load 1.61
determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 6: Commit**

```sh
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32ajit.cpp tests/pinheck/p8x32a/mutate_core.sh tests/pinheck/p8x32a/chip/jit_chain.spin
git commit -m "p8x32a: blocks chain through a link table from their own exits"
```

### Task 6: run_local takes a linked block before decoding

**Files:**
- Modify: `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`

**Interfaces:**
- Consumes: Task 5's links.
- Produces: `p8x32a_jlink.{part, fn}`; `run_local`'s linked path.

- [ ] **Step 1: The linked path**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)

edit('src/cpu/p8x32a/p8x32a.h', [
    (r'''
typedef struct p8x32a_jblk p8x32a_jblk;

/* what a block's exit needs of the block at a cog address: its entry for another block, the word it starts with''',
     r'''
typedef struct p8x32a_jblk p8x32a_jblk;
struct p8x32a_jst;

/* what a block's exit needs of the block at a cog address: its entry for another block, the word it starts with'''),
    (r'''typedef struct p8x32a_jlink {
	const void *body;
	uint32_t word, mask, len, pad;
	uint64_t pad2;
} p8x32a_jlink;
''',
     r'''typedef struct p8x32a_jlink {
	const void *body;
	uint32_t word, mask, len, part; /* part: a lazy cog's block */
	uint32_t (*fn)(struct p8x32a_jst *st); /* its entry from run_local */
} p8x32a_jlink;
'''),
])
edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''		ln->mask = (b->dyn & 1) ? ~P8X32A_JDYN : ~0u;
		ln->len = b->valid && b->len && b->fn ? b->len : ~0u;
	}
''',
     r'''		ln->mask = (b->dyn & 1) ? ~P8X32A_JDYN : ~0u;
		ln->len = b->valid && b->len && b->fn ? b->len : ~0u;
		ln->part = b->part;
		ln->fn = b->fn;
	}
'''),
    (r'''	p8x32a_jst st;
	unsigned px = 0, jc = 0;

	st.ram = ram;''',
     r'''	p8x32a_jst st;
	unsigned px = 0, jc = 0;
	p8x32a_jblk *lb = NULL;

	st.ram = ram;'''),
    (r'''		px = pc;
		jc = 0;
		e = &dec[(pc - 1) & 511];
		if (e->word != ix) dec_fill(e, ix);
		/* event instructions are never translated */
		if (p->jit_build && !(fl & 4) && (e->kind != K_NL || (p->lz_on && n == p->lz ? e->fl & (F_OUTA | F_HUBRD) : e->jh)) && !(e->fl & F_INA)) {
			unsigned a = (pc - 1) & 511;
			p8x32a_jblk *b = jit_get(p, n, a, ix);
			unsigned k;
			if (b && b->len && (b->part ? t2 <= tl : t2 + 4 * (uint64_t)(b->len - 1) <= tl)) {''',
     r'''		px = pc;
		jc = 0;
		lb = NULL;
		e = &dec[(pc - 1) & 511];
		if (p->jit_build && !(fl & 4)) {
			/* a block linked at this address for this word runs without the decode and lookup below */
			const p8x32a_jlink *ln = &p->jlink[n][(pc - 1) & 511];
			if (ln->len != ~0u && !ln->part && !((ix ^ ln->word) & ln->mask) && t2 + 4 * (uint64_t)(ln->len - 1) <= tl)
				lb = p->jblk[n][(pc - 1) & 511];
		}
		if (!lb && e->word != ix) dec_fill(e, ix);
		/* event instructions are never translated */
		if (lb || (p->jit_build && !(fl & 4) && (e->kind != K_NL || (p->lz_on && n == p->lz ? e->fl & (F_OUTA | F_HUBRD) : e->jh)) && !(e->fl & F_INA))) {
			unsigned a = (pc - 1) & 511;
			p8x32a_jblk *b = lb ? lb : jit_get(p, n, a, ix);
			unsigned k;
			if (b && b->len && (b->part ? t2 <= tl : t2 + 4 * (uint64_t)(b->len - 1) <= tl)) {'''),
    (r'''			}
		}
		if (e->kind == K_NL || (e->fl & F_INA)) {
			if (!((e->cond >> fl) & 1)) {''',
     r'''			}
		}
		if (lb && e->word != ix) dec_fill(e, ix);
		if (e->kind == K_NL || (e->fl & F_INA)) {
			if (!((e->cond >> fl) & 1)) {'''),
])
EOF
```

- [ ] **Step 2: The suites pass**

```sh
flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh | tail -1
P8X32A_JIT=1 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh | grep -E 'translated|unmutated|SURVIVED|passed'
nice -n 15 tests/pinheck/display/check.sh | tail -1
```

Expected (about 20 minutes, both mutation scripts included):

```text
p8x32a: 821 passed, 0 failed
p8run: translated
unmutated core: all cases match
unmutated core (none-translated): all cases match
p8x32a: 821 passed, 0 failed
display: 0 failed
```

- [ ] **Step 3: America's Most Haunted byte-identical, with and without the translator and lazy cogs**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12d.log 2>&1; tail -1 build/sdl3pinmame/12d.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep -E 'bench video opt|determinism|FAIL'
for s in PINHECK_JIT=0 PINHECK_LAZY=0; do amh env $s REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|FAIL' | sed "s/^/$s: /"; done
```

Expected (about 10 minutes):

```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/amh.nv
bench video opt: 0.714x over 24.0 s emulated (33.6 s wall), whole run 14.0 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.484x, load 1.80
determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_JIT=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_LAZY=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 4: Commit**

```sh
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c
git commit -m "p8x32a: run_local takes a linked block before decoding"
```

### Task 7: Speed, regression, the tests bite, the documents

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

**Interfaces:**
- Consumes: everything above; `build/reference/sdl3pinmame`; Plan 9d's `pgo.sh`; Task 2's `bench.sh worker`.
- Produces: `build/pgo-use/sdl3pinmame`; the Result's measurements; spec §4's paragraph for Plan 12d; the roadmap's row.

- [ ] **Step 1: The profile-guided build**

```sh
flock /code/spooky_domino/work/heavy.lock flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/pgo.sh -DPLATFORM=linux -DARCH=x64 | grep -E '^pgo: |pgo training: bench'
```

Expected (about 10 minutes; it builds twice and trains on Domino's):

```text
pgo training: bench attract opt: 0.680x over 30.0 s emulated (44.1 s wall), whole run 14.6 G instructions and 1.7 s CPU per emulated s, worst 100 ms 0.412x, load 2.75
pgo training: bench video opt: 0.595x over 24.0 s emulated (40.3 s wall), whole run 16.0 G instructions and 1.8 s CPU per emulated s, worst 100 ms 0.393x, load 2.13
pgo: build/pgo-use/sdl3pinmame
```

- [ ] **Step 2: The worker's cycles**

The worker thread with video (two runs each) and in play, the reference against this plan's build without and with the profile:

```sh
for w in video video play; do for b in build/reference/sdl3pinmame build/sdl3pinmame/sdl3pinmame build/pgo-use/sdl3pinmame; do
	amh env SDL3PINMAME=$b flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh worker $w | grep 'bench worker' | sed "s|^|$b: |"
done; done
```

Expected (about 15 minutes):

```text
build/reference/sdl3pinmame: bench worker video: the Propeller worker 3.820 G cycles and 9.76 G instructions per emulated s, the PIC32 thread 1.786 G cycles (whole run, 40.0 s emulated)
build/sdl3pinmame/sdl3pinmame: bench worker video: the Propeller worker 3.617 G cycles and 8.59 G instructions per emulated s, the PIC32 thread 1.775 G cycles (whole run, 40.0 s emulated)
build/pgo-use/sdl3pinmame: bench worker video: the Propeller worker 3.643 G cycles and 8.43 G instructions per emulated s, the PIC32 thread 1.627 G cycles (whole run, 40.0 s emulated)
build/reference/sdl3pinmame: bench worker video: the Propeller worker 3.782 G cycles and 9.74 G instructions per emulated s, the PIC32 thread 1.793 G cycles (whole run, 40.0 s emulated)
build/sdl3pinmame/sdl3pinmame: bench worker video: the Propeller worker 3.609 G cycles and 8.60 G instructions per emulated s, the PIC32 thread 1.782 G cycles (whole run, 40.0 s emulated)
build/pgo-use/sdl3pinmame: bench worker video: the Propeller worker 3.512 G cycles and 8.46 G instructions per emulated s, the PIC32 thread 1.619 G cycles (whole run, 40.0 s emulated)
build/reference/sdl3pinmame: bench worker play: the Propeller worker 4.488 G cycles and 11.60 G instructions per emulated s, the PIC32 thread 1.852 G cycles (whole run, 80.0 s emulated)
build/sdl3pinmame/sdl3pinmame: bench worker play: the Propeller worker 4.263 G cycles and 10.36 G instructions per emulated s, the PIC32 thread 1.839 G cycles (whole run, 80.0 s emulated)
build/pgo-use/sdl3pinmame: bench worker play: the Propeller worker 4.143 G cycles and 10.14 G instructions per emulated s, the PIC32 thread 1.682 G cycles (whole run, 80.0 s emulated)
```

- [ ] **Step 3: Every game against the reference, without and with the profile, with the worker thread**

```sh
for b in build/sdl3pinmame/sdl3pinmame build/pgo-use/sdl3pinmame; do
	for g in amh jet rz; do $g env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=$b flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video play | sed "s|^|$b $g: |"; done
	REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=$b flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video | sed "s|^|$b dominos: |"
done
```

Expected: every `determinism` line `identical` (America's Most Haunted's with `dmd.bin`), no `FAIL`; the speeds are the Result's table (about 25 minutes; the reference's runs are cached after the first pass, from America's Most Haunted's on one thread in Task 1 and the others with the worker). For example:

```text
build/sdl3pinmame/sdl3pinmame amh: determinism first boot: identical nvram.base/amh.nv
build/sdl3pinmame/sdl3pinmame amh: bench attract opt: 1.252x over 30.0 s emulated (23.9 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 1.122x, load 1.66
build/sdl3pinmame/sdl3pinmame amh: bench attract ref: 1.162x over 30.0 s emulated (25.8 s wall), whole run 13.8 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.786x, load 1.61
build/sdl3pinmame/sdl3pinmame amh: determinism attract: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
build/sdl3pinmame/sdl3pinmame amh: bench video opt: 0.952x over 24.0 s emulated (25.2 s wall), whole run 14.5 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.641x, load 1.56
build/sdl3pinmame/sdl3pinmame amh: bench video ref: 0.683x over 24.0 s emulated (35.1 s wall), whole run 15.3 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.544x, load 1.22
build/sdl3pinmame/sdl3pinmame amh: determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
build/sdl3pinmame/sdl3pinmame amh: bench play opt: 0.871x over 55.0 s emulated (63.1 s wall), whole run 16.4 G instructions and 1.5 s CPU per emulated s, worst 100 ms 0.291x, load 1.41
…
build/pgo-use/sdl3pinmame jet: bench play opt: 1.120x over 55.0 s emulated (49.1 s wall), whole run 15.2 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.753x, load 1.31
build/pgo-use/sdl3pinmame jet: bench play ref: 0.916x over 55.0 s emulated (60.0 s wall), whole run 16.9 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.457x, load 1.60
build/pgo-use/sdl3pinmame jet: determinism play: identical uart.log frames.bin snd.wav nvram/jetsons.nv
```

- [ ] **Step 4: On one thread; the switches give the same output**

```sh
for g in amh jet; do $g env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh video play | grep -E 'opt:|determinism|FAIL' | sed "s/^/$g one thread: /"; done
for s in PINHECK_JOURNAL=0 PINHECK_RF13=0; do amh env $s REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|FAIL' | sed "s/^/$s: /"; done
jet env PINHECK_JIT=0 REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|FAIL' | sed "s/^/PINHECK_JIT=0: /"
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh contention | tail -1
```

Expected (about 15 minutes):

```text
amh one thread: determinism first boot: identical nvram.base/amh.nv
amh one thread: bench video opt: 0.709x over 24.0 s emulated (33.8 s wall), whole run 14.0 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.406x, load 1.23
amh one thread: determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
amh one thread: bench play opt: 0.665x over 55.0 s emulated (82.7 s wall), whole run 16.0 G instructions and 1.4 s CPU per emulated s, worst 100 ms 0.285x, load 1.10
amh one thread: determinism play: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
jet one thread: determinism first boot: identical nvram.base/jetsons.nv
jet one thread: bench video opt: 0.799x over 24.0 s emulated (30.0 s wall), whole run 14.5 G instructions and 1.0 s CPU per emulated s, worst 100 ms 0.595x, load 1.22
jet one thread: determinism video: identical uart.log frames.bin snd.wav nvram/jetsons.nv
jet one thread: bench play opt: 0.790x over 55.0 s emulated (69.6 s wall), whole run 16.1 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.519x, load 1.11
jet one thread: determinism play: identical uart.log frames.bin snd.wav nvram/jetsons.nv
PINHECK_JOURNAL=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_RF13=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_JIT=0: determinism short: identical uart.log frames.bin snd.wav nvram/jetsons.nv
contention: ok
```

- [ ] **Step 5: Regression**

The machine checks of all four games, Domino's scripted game, libpinmame, the look, and the unit suites (the storage suite writes about 7 GB of images; delete them after it passes):

```sh
for g in amh env jet rz; do
	$g env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh | tail -1
	$g env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh | tail -1
	$g env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh | tail -1
	$g env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh | tail -1
done
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/romset_check.sh | tail -1
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/game/pinmame_game.sh | tail -2
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/audio/pinmame_audio.sh | tail -1
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_look.sh | tail -1
mkdir -p build/libpinmame-src && ln -sfn ../../cmake build/libpinmame-src/cmake && ln -sfn ../../ext build/libpinmame-src/ext && ln -sfn ../../src build/libpinmame-src/src
cp cmake/libpinmame/CMakeLists.txt build/libpinmame-src/CMakeLists.txt
cmake -S build/libpinmame-src -B build/libpinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DBUILD_STATIC=OFF > build/libpinmame-cfg.log 2>&1
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
for g in env amh jet; do $g env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | tail -1; done
for s in mips32 pic32mx link audio board display storage; do flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh 2>&1 | tail -1; done
D=tests/pinheck/storage/build; rm -rf "${D:?}"
```

Expected (about 40 minutes):

```text
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
romset: ok
game: 44 checks, 0 failures
pinmame game: ok
pinmame audio: ok
pinmame look: ok
[100%] Built target pinmame_test
pinmame vpx: ok
pinmame vpx: ok (amh: the raw DMD)
pinmame vpx: ok (jetsons: display and sound)
mips32: 208 passed, 0 failed
pic32mx: 0 failed
link: 0 failed
audio: 0 failed
board: 0 failed
display: 0 failed
storage: 0 failed
```

- [ ] **Step 6: The machine-level test bites**

A block reached from another's exit that stops before its first slot without its `px` (the `exit-k0` mutant) must also fail America's Most Haunted's determinism (the firmware's traces differ within 22 million cycles):

```sh
f=src/cpu/p8x32a/p8x32ajit.cpp
python3 -c "import sys; p=sys.argv[1]; s=open(p).read(); a,b=sys.argv[2],sys.argv[3]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" $f '				a.mov(stf(offsetof(p8x32a_jst, px)), base);
				a.mov(stf(offsetof(p8x32a_jst, nix)), CURW);' '				a.mov(stf(offsetof(p8x32a_jst, nix)), CURW);'
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12d.log 2>&1; tail -1 build/sdl3pinmame/12d.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short 2>&1 | grep -m3 FAIL
git checkout $f
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12d.log 2>&1; tail -1 build/sdl3pinmame/12d.log
git status --short | grep -v '^??'
```

Expected (about 5 minutes; `git status` prints nothing):

```text
[100%] Built target sdl3pinmame
DETERMINISM FAIL first boot: nvram.base/amh.nv differs from the reference build's
DETERMINISM FAIL short: uart.log differs from the reference build's
DETERMINISM FAIL short: frames.bin differs from the reference build's
Updated 1 path from the index
[100%] Built target sdl3pinmame
```

- [ ] **Step 7: The documents**

Spec §4 gains what this plan established; the roadmap gains its row and what it leaves:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md', [
    (r'''- **Proof for every stage:**''',
     r'''- **Established by Plan 12d (a cheaper dispatch):**
  - Where a dispatch's time went (America's Most Haunted with video, the worker thread's cycles from a per-thread recording, `bench.sh worker`): `run_local` 30%, the translated blocks 21%, the scheduler 12.5%, the translator's shared tail 12%, `hub_rw` 6%, `flush` 4%, the devices and the lazy scan cog the rest; about 630 cycles per dispatch at 5.8 million dispatches per emulated second. Host instructions mislead here: a version with 6.6% fewer of them took 3% more cycles (twice the instruction-cache misses), and the Propeller run alone (`p8run`) overstated every gain, so each change was judged by the worker's cycles.
  - Taken, each exact: the scheduler keeps its keys in order; a hub read or write ends a translated block, decided there as `event_run` decides it, a read run in the block, a write through `hub_rw`, and one that waits completed by the block's resume entry when due (one call into translated code per dispatch); blocks chain through a link table from their own exits; `run_local` takes a linked block before decoding. The worker: 3.80 → 3.61 G cycles per emulated second with video (−5%), 4.49 → 4.26 G in play; on one thread 15.3 → 14.0 G host instructions with video.
  - Tests: `p8run -t0` starts the chip at cycle 2^36 and every RTL case runs there too (a 32-bit key in the prototype's hub slot ran operations ahead of other cogs only past 2^28 cycles, invisible to every earlier case and to the firmware traces), two chip tests (a hub read into a later block's slot; a chained block whose first slot no longer fits), five mutants in `mutate_core.sh` and its `MUTANTS` filter.
  - Rejected after measurement: each exit's chain inlined whole (instruction-cache misses doubled), the exits as calls to shared stubs, the translator state kept in `p8x32a`, the shared tail with the link table, the link path for a lazy cog's blocks, a table of resume entries by address.
  - Every output byte-identical to Plan 12c's build for the four games, on one thread, with `PINHECK_JIT=0`, `PINHECK_LAZY=0`, `PINHECK_RF13=0`, `PINHECK_JOURNAL=0` and for the profile-guided build. Speed with the worker (loads 1.2–1.9), without / with the profile: America's Most Haunted 1.25×/1.27× attract, 0.95×/1.00× video, 0.87×/0.92× play; The Jetsons 1.07×/1.24×, 0.99×/1.13×, 0.99×/1.12×; Rob Zombie 1.69×/1.85×, 1.42×/1.56×, 1.25×/1.40×; Domino's 1.46×/1.58×, 1.25×/1.37×. Real time is reached for America's Most Haunted with video only with the profile-guided build (1.00×), not in play.
- **Proof for every stage:**'''),
])

edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    (r'''| release | release:''',
     r'''| `2026-10-03-pinheck-12d-dispatch.md` | 12d: a cheaper dispatch for America's Most Haunted: the scheduler's keys in order; hub reads and writes at the ends of translated blocks with resume entries; a link table and per-exit chains; `run_local`'s linked path; `p8run -t0`, every RTL case from cycle 2^36, `bench.sh worker` | Plan 12c | none | every output byte-identical to Plan 12c's build for the four games, every switch and the profile-guided build; the RTL, spinsim and QEMU suites green with five new mutants and two chip tests; speeds in the plan's Result | written |
| release | release:'''),
    (r'''- After Plan 12c: with the worker thread,''',
     r'''- After Plan 12d: with the worker thread, America's Most Haunted runs 1.25× / 0.95× / 0.87× (attract, video, play) without a profile and 1.27× / 1.00× / 0.92× with the profile-guided build; The Jetsons 1.07× / 0.99× / 0.99× and 1.24× / 1.13× / 1.12×. What remains for America's Most Haunted in play: the worker needs 4.26 G cycles per emulated second (4.14 G with the profile), more than the core gives at the loads measured; `run_local` (the SD driver's pin and special-register events, interpreted slots) and the scheduler are still 40% of it, the translated code 38%.
- After Plan 12c: with the worker thread,'''),
])
EOF
```

- [ ] **Step 8: Commit**

```sh
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "docs: what Plan 12d established (sorted keys, hub operations at block ends, links and per-exit chains, run_local's linked path) and its roadmap row"
```

## Windows checklist

For the Windows session (VS2022 BuildTools / MSVC 14.44, `C:\code\dominos`), after Plan 12d is pushed, with the combined four-game recheck. Results come back by message; a Windows-only fix comes back as a diff with a test, applied on Linux. Never push. Muted or the silent device for every item.

1. **Build.** MSVC x64 and Win32 standalone PinMAME, VPinMAME64 and libpinmame, exactly as CI does. Expected: all build; no warning in `p8x32a.c`, `p8x32ajit.cpp`.
2. **Determinism across compilers.** The scripted Domino's game (`tests\pinheck\game`, as for Plan 9d): the score 32176700 and the four logs as on Linux.
3. **Speed.** `PinMAME.exe amh`, unthrottled (`-nothrottle`), 60 s of attract mode and the video workload's commands with `PINHECK_TIME_LOG=time.log`: the last line's emulated over host seconds, against Plan 12c's build on the same machine.

## Result

Measured in the replay of this plan's text: a fresh worktree of `863dc6e9` (branch `pinheck-12d`), every step run as written, every Expected line checked, on the Ryzen 7 7730U while other work shared it (one emulator at a time, `emu.lock`); the load average is printed with every speed.

- **Task 1:** the reference on one thread: America's Most Haunted 15.3 G host instructions per emulated second with video, 17.2 G in play.
- **Task 2:** every RTL case of `mutate_core.sh` (and in Task 6 of the whole suite) matches the RTL from cycle 2^36 on the present core; the reference's worker spends 3.80 G cycles per emulated second with video.
- **Task 3:** the `keys-ran` mutant on the new code is caught by 365 cases; one thread with video 15.1 G, byte-identical.
- **Task 4:** the three mutants had no code to change, then are caught: `hub-key32` by 89 cases, all of them runs from cycle 2^36; `hub-gen` by 16; `hub-report` by 4 (`chip/jit_hubcode.spin` first); `p8x32a.c` builds without warnings under the suites' flags. One thread with video 14.5 G, byte-identical.
- **Task 5:** `link-void` caught by 15 cases, `exit-k0` by 2 (`chip/jit_chain.spin`); one thread with video 14.1 G, byte-identical.
- **Task 6:** the Propeller suite: 821 passed, 0 failed, interpreted and translated (`mutate_lazy.sh` and `mutate_core.sh` inside it, every mutant caught); the display suite passes. One thread with video 14.0 G (15.3 G for the reference), byte-identical; with `PINHECK_JIT=0` and `PINHECK_LAZY=0` byte-identical too.
- **Task 7, the worker's cycles** (per emulated second, two runs with video, one in play): reference 3.820 / 3.782 G with video and 4.488 G in play; this plan's build 3.617 / 3.609 G and 4.263 G (−5%); with the profile 3.643 / 3.512 G and 4.143 G. Host instructions in the worker 9.75 → 8.60 G with video (−12%).
- **Task 7, speed with the worker thread** (each workload against the reference in the same session, the profile-guided build against the same reference runs; all 30 determinism lines identical, no `FAIL`):

| Game | Attract | Video | Play | With the profile: attract, video, play | Reference: attract, video, play |
|---|---|---|---|---|---|
| America's Most Haunted | 1.252× (12.6 G) | 0.952× (14.5 G) | 0.871× (16.4 G) | 1.269×, 1.003×, 0.922× (12.2, 14.0, 15.9 G) | 1.162×, 0.683×*, 0.851× (13.8, 15.3, 17.6 G) |
| The Jetsons | 1.072× (14.1 G) | 0.989× (14.5 G) | 0.994× (16.2 G) | 1.241×, 1.125×, 1.120× (13.2, 13.6, 15.2 G) | 1.056×, 0.965×, 0.916× (14.7, 15.1, 16.9 G) |
| Rob Zombie | 1.686× | 1.418× | 1.252× | 1.848×, 1.556×, 1.404× | 1.574×, 1.319×, 1.201× |
| Domino's | 1.456× | 1.248× | — | 1.582×, 1.369× | 1.340×, 1.184× |

  Loads 1.2–1.9 (*the reference's video run was cached from Task 1, on one thread; its instruction count is the comparable figure). Instruction counts with the worker include its spinning. On one thread: America's Most Haunted with video 14.0 G and in play 16.0 G (15.3 G and 17.2 G for the reference), The Jetsons 14.5 G and 16.1 G, byte-identical; `PINHECK_JOURNAL=0`, `PINHECK_RF13=0` (America's Most Haunted) and `PINHECK_JIT=0` (The Jetsons) give the short run byte-identical; `bench.sh contention` passes.
- **Task 7, regression:** the boot, display, service-test and simulator checks of all four games, the romset check, Domino's scripted game (44 checks, 0 failures), the audio and look checks, libpinmame for Domino's, America's Most Haunted (the raw DMD) and The Jetsons, and the MIPS32 (208 passed), PIC32MX, link, audio, board, display and storage suites pass; a block reached from another's exit that stops before its first slot without its `px` (`exit-k0`) fails America's Most Haunted's determinism from the first boot, and the tree is clean afterwards.
- **Gain per change** (the worker's cycles with video in the prototype, three runs each, the reference 152.5 G over the run): the keys in order 150.2 G (−1.5%); with hub operations at the ends of blocks 155.0 G (+1.6%: their code in every block, still with the shared tail); with the link table and per-exit chains 146.9 G (−3.7%); with `run_local`'s linked path 143.8 G (−5.7%). One thread, host instructions with video in the replay: 15.3 → 15.1 → 14.5 → 14.1 → 14.0 G.

**What remains.** America's Most Haunted in play (0.87×, 0.92× with the profile) is below real time, and with video it reaches real time only with the profile-guided build and no margin; everything else runs at real time. The worker is the bound: in play it needs 4.26 G cycles per emulated second (4.14 G with the profile). After this plan its cycles are about 38% translated code (the PASM work and the per-exit chains), 29% `run_local` (the SD driver's `INA`, `OUTA` and counter events through `event_run`, the interpreted slots the Spin interpreters rewrite, the lazy scan cog's blocks), 10% the scheduler, 4% `flush` and 19% the pin devices, `hub_rw` for writes and the rest; level-1 data misses (the cogs' state, decoded words, links) and branch mispredictions at the cog switches account for much of the C share. The levers left: the SD driver's events translated as hub operations now are (decided in the block, issued from its exit), which Plan 9c rejected when a declined event cost a call; smaller translated code for the interpreters (the exits are a third of it); and the PIC32 thread's share where the worker waits for it at its synchronisation points. The Windows recheck of the four games follows this plan.
