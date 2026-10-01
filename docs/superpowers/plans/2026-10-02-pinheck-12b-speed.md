# Plan 12b: America's Most Haunted and The Jetsons after Plan 12 (the levers measured; a hub-write journal for the lazy cog; profile-guided builds for users) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure the five levers Plan 12 left for America's Most Haunted (`amh`) and The Jetsons (`jetsons`), take those that pay and stay exact, and state the speeds per game with and without a profile-guided build. Every output stays byte-identical to Plan 12's build.

**Architecture:** One core change and one build change, both exact. (1) *A hub-write journal* (`p8x32a.c`): the lazy cog (Plan 12) no longer catches up before every other cog's hub write that changes a byte; the write's old bytes go into a journal of 64 entries, the lazy cog's hub reads take the bytes as they were at the read's time (the earliest entry after it), and the journal empties at each catch-up (the end of `p8x32a_run_until`, an `INA` read of its pins, a `CLKSET`, its exit) or, full, by a catch-up before the write as before. Translated lazy blocks stop before a hub read of a long with an entry, and the interpreter reads it. The host reads hub RAM as the scan cog saw it (`p8x32a_hub_at`) for the raw DMD's frame log and row model. (2) *Profile-guided builds for users:* Plan 9d's `pgo.sh`, trained on Domino's as it is (training on all four games measured slower for America's Most Haunted and The Jetsons), re-measured for the four games, and `docs/build.md` gives the optional build. Levers 1–3 of Plan 12's list (the SD card and The Jetsons' display link as devices bound to their cogs; hub reads ahead of other cogs' events) were built exact and measured slower; their findings are rulings, the prototype a kept patch.

**Tech Stack:** C (C89-syntax-clean cores and driver), C++17 (asmjit translator), Spin/PASM test programs (openspin, the P1 Verilog RTL), Python 3 (no third-party modules), POSIX sh, CMake (GCC profile-guided builds).

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §4 (Milestone 9: at least 1.0× on the reference machine, aim 1.2×; byte-identical determinism; every suite green including the RTL, spinsim and QEMU differentials), applied to the games of Plans 11b and 11c. Measured reasons: Plan 12's Result and "what remains" (America's Most Haunted 1.08×, 0.82×, 0.76× and The Jetsons 0.98×, 0.89×, 0.83× in attract mode, with video and in play, without a profile).

## Prerequisites

- `pinheck` at `f69cea15` (Plan 12 merged with its review fixes and the C99 `P8_INLINE`).
- Before starting, from the repository root, export what Plan 12's checks use (`TOOLS`, `P8X32A_ROM`, `PINHECK_UPDATE_DIR`, `PINHECK_ZIP`, `DOMINOS_PRP`, `DOMINOS_ZIP`, `RZSPOOK_ZIP`, `RZSPOOK_UPDATE_DIR`, `JETSONS_ZIP`, `JETSONS_UPDATE_DIR`, `AMH_ZIP`, `AMH_UPDATE_DIR`, `AMH_HEX`, `SDL_AUDIO_DRIVER=dummy`) and define the game functions once:
  ```sh
  amh() { PINHECK_GAME=amh PINHECK_ZIP=$AMH_ZIP PINHECK_UPDATE_DIR=$AMH_UPDATE_DIR "$@"; }
  jet() { PINHECK_GAME=jetsons PINHECK_ZIP=$JETSONS_ZIP PINHECK_UPDATE_DIR=$JETSONS_UPDATE_DIR "$@"; }
  rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
  ```
  A Domino's run is the command without a function. Run the steps with `bash` (the functions are used with `env`).
- The machine is shared: only one emulator runs at a time machine-wide, so every step that runs PinMAME, libpinmame or a firmware program goes through `flock /code/spooky_domino/work/emu.lock` (on another machine drop the `flock`); builds and heavy suites run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6`. Every PinMAME launch has a `timeout -k 30`, a private `-nvram_directory` and `-cfg_directory`, and no sound device (`SDL_AUDIO_DRIVER=dummy`); `bench.sh` and the machine checks do this themselves.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit. The scripts were generated from the prototype and replayed in this order on a fresh worktree of `f69cea15`.
- Speeds are wall-clock on the Ryzen 7 7730U while other agents' builds and emulators share it; the load average is printed with every speed. Instruction counts on one thread (`PINHECK_THREADS=0`) do not depend on the load; with the worker thread they include the threads' spinning.

## Global Constraints

- **Byte-identical:** every game's UART1 log, display frames (with a raw DMD its 16-subframe cycles and its subframes as the core gets them), sound and NVRAM equal those of the reference build (Task 1: `pinheck` at `f69cea15`) in attract mode, with video and in play, with and without the worker thread, with the journal off (`PINHECK_JOURNAL=0`), and for the profile-guided build; the scripted Domino's game's four logs are byte-identical.
- **All suites green:** `tests/pinheck/{mips32,pic32mx,storage,link,p8x32a,display,audio,board}/check.sh` (the `p8x32a` suite with and without the translator, `P8X32A_JIT=1`), `tests/pinheck/p8x32a/mutate_lazy.sh`, and the machine checks of Plans 5–12 for all four games.
- `PINHECK_THREADS=0`, `PINHECK_JIT=0`, `PINHECK_LAZY=0`, `PINHECK_RF13=0` keep working; `PINHECK_JOURNAL=0` turns the journal off; every combination gives the same output.
- Code comments short and factual; the cores and the driver C89-syntax-clean; no `__inline__` or `__inline` (`P8_INLINE`/`P8_COLD`).
- Never commit the mask ROM, firmware, SD cards, romsets or profiles.

## Rulings

Decisions taken on measurements of the prototype (branch `pinheck-12b-proto`, all five levers of Plan 12's list measured before choosing). Unless they say otherwise, figures are per emulated second, host instructions are G (10⁹) on one thread (`PINHECK_THREADS=0`, independent of the load), and "the reference" is Plan 12's code.

1. **Where the Propeller thread's time goes after Plan 12.** Counters in a measurement build, with the worker thread: in America's Most Haunted with video the worker is busy 1.17 s per emulated second at 0.81×; the scheduler dispatches 1.85, 2.27 and 1.90 million events of the three Spin interpreters (cogs 0, 1, 5) and 1.09 million of the SD driver (cog 3, which runs another 2.9 million inline); the lazy scan cog catches up 1.10 million times, 0.89 million of them before another cog's hub write that changes a byte; the SD card sees 2.7 million pin changes (3.3 million in play). In The Jetsons (attract mode, video and play alike) the SD driver takes 1.7–2.1 million dispatches and 5.6–6.7 million inline events, the display link's sender 1.2–1.4 million and 4.0 million (2.0 million `PHSB`, 0.5 million `FRQA` and 0.25 million `PHSA` writes), each Spin interpreter 1.5 million; `pins_out` runs 8.7–9.7 million times and `flush` 6.7–7.4 million.
2. **The ceilings.** Measurement only (the run's output is not exact): letting every hub read run ahead of the other cogs' events takes The Jetsons' attract mode from 16.5 G to 15.3 G and America's Most Haunted's video from 17.0 G to 15.2 G; letting the SD and display cogs' pin events run ahead as well takes The Jetsons to 11.6 G (1.145× on one thread) and America's Most Haunted to 15.1 G. The pin events matter for The Jetsons, the hub reads for America's Most Haunted's interpreters.
3. **Lever 3 (hub reads that skip the scheduler) is rejected.** Three exact forms were built, each byte-identical and checked against the RTL (a 3-cog random generator with shared hub bytes, 100 cases, 5 of 5 mutations caught): a hub operation runs before other cogs' earlier events when none of them can reach the hub first, judged from their next events' times and slots (The Jetsons 16.7 G, America's Most Haunted 17.1 G against 16.5 G and 17.0 G), from those events' addresses too (16.8 G, 17.4 G), and from a lookahead over the other cogs' code paths (The Jetsons' short run with the worker 17.2 G against 13.6 G). The cogs the interpreters wait for are within a few instructions of them, so the checks cost more than the dispatches they save.
4. **Levers 1 and 2 (the SD card and The Jetsons' display link as devices bound to their cogs) are rejected.** Built exact: a *bound cog* drives a pin group only a host device watches (SD: P1–P3 with its answer on P0; display: P20–P22, the device also watching P17), its counter, `OUTA`, `DIRA` and `INA` events go to the device on their own path without pending points or `flush`, and they run ahead of other cogs' events while a lookahead shows no other cog can observe or drive the group before them. The RTL agrees (a chip test with three bindings and 3,637 early events, the SD boot of Domino's to cycle 48,100,000 with 86% of the SD cog's pin events early; 6 of 8 mutations caught, an online check of the delivery order included). But without running ahead the bound path is slower than the pending points (The Jetsons' attract mode 18.5 G against 16.6 G), with the lookahead much slower (The Jetsons' short run 25.5 G against 13.6 G, the lookahead 19% of the samples), and with lever 3 as well the display link loses bits once the SD cog binds (`frames.bin` differs; the cause was not found). The prototype is kept as `/code/spooky_domino/work/m12b/proto-bind-la-journal.patch` (with `bound.spin` and `gen_hub.py` beside it).
5. **Lever 4 is taken as a journal, exact by construction.** Plan 12 rejected a journal because it would move the raw DMD's frame log off its definition. It need not: the lazy cog lags only across writes it has journaled, its reads take each byte from the earliest entry after the read (the value at the read's time), and the host reads hub RAM through the same journal (`p8x32a_hub_at`) where it reads it for the scan cog's time (the frame log's hub check at the cycle's last row clock, the `PINHECK_DMD_PROOF` row model). Catch-ups still come at the end of `p8x32a_run_until` (so each delivery stays within the same PIC32 call, and the frame log's stamp with it), at an `INA` read of the scan pins, at a `CLKSET`, at the exit, and before a write when the 64 entries are full; every catch-up passes every journaled write, so the journal empties. Translated lazy blocks test a long's bit in a map of journaled longs before a hub read and stop there; the interpreter reads through the journal. A new chip test (`chip/lzjournal.spin`: 400 rounds of byte, word and long writes to the bytes the scan cog shows) makes 1,621 entries and fills the journal 23 times, interpreted and translated; `mutate_lazy.sh`'s journal mutants (the read ignoring entries, the latest entry winning, no catch-up when full, the new bytes kept as old) are each caught, and a translator without the map test fails 8 RTL cases. America's Most Haunted with video: 16.6 G against 16.9 G with `PINHECK_JOURNAL=0` (−1.8%), byte-identical with `dmd.bin`.
6. **Lever 5: profile-guided builds, re-measured on all four games, trained on Domino's as before, recommended to users as optional.** Trained on Domino's attract mode and video (Plan 9d's `pgo.sh` as it is), in the replay with the worker thread against the same reference runs: America's Most Haunted 1.096×, 0.858×, 0.809× (attract, video, play; 14.4, 16.2, 18.1 G against 15.1, 17.0, 19.1 G without the profile), The Jetsons 1.041×, 0.966×, 0.951× (15.1, 15.5, 17.3 G against 16.2, 16.7, 18.5 G), Rob Zombie 1.691×, 1.427×, 1.281×, Domino's 1.456× and 1.265×: 4–8% faster where the wall clock resolves it, 5–8% fewer host instructions everywhere, every output byte-identical. Training on all four games (attract mode and video of each) was measured slower for the games it was meant to help: on one thread America's Most Haunted with video 16.6 G against 16.0 G with Domino's training (16.8 G without a profile), The Jetsons' attract mode 15.5 G against 15.0 G (16.2 G); so `pgo.sh` stays as it is. *Recommendation:* the profile-guided build is worth it for users who build sdl3pinmame for America's Most Haunted or The Jetsons, so `docs/build.md` gives it as an optional section (the romset and the mask ROM are the user's, the profile stays local); it is not made the default (CI has no firmware to train on), and libpinmame and the Windows builds keep Plan 9d's option without a training harness. Cost if wrong: users who skip the section run 4–8% slower.
7. **Real time.** With the worker thread and without a profile (the replay, loads 1.2–2.2): America's Most Haunted 1.14× in attract mode, 0.86× with video, 0.80× in play; The Jetsons 1.00×, 0.90×, 0.88×. With the profile-guided build: America's Most Haunted 1.10×, 0.86×, 0.81×; The Jetsons 1.04×, 0.97×, 0.95×. Real time is reached in attract mode for both games (The Jetsons' only with the profile, with a margin of 4%), and not with video or in play for either, with or without the profile. The levers that could close the gap are the run-ahead ones (Ruling 2's ceilings) and they need a lookahead that costs less than the dispatch it saves ("what remains").

## Review Focus

- **The journal's reads at the scan cog's time:** every path that reads hub RAM for the lazy cog (`hub_rw` through `jn_rd32`, the translated read through the map, the host's `p8x32a_hub_at` for the frame log and the row model) and every path that moves it on (`lazy_run` → `jn_drop`); an entry made while the lazy cog is already past the write (`h - 1 <= lz_at`), the full journal's catch-up, the reset.
- **Order of the journal:** entries are in time order because hub writes are (the scheduler's order; there is no early hub write), and each byte read takes the earliest entry after the read; `jn_drop` keeps only entries after the catch-up, which by the same argument are none.
- **The profile-guided build** is documented, not made the default: CI has no firmware to train on, and the profile stays out of the repository.

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/p8x32a/p8x32a.[ch]` | the journal: entries at other cogs' hub writes (`lazy_write`), reads at the lazy cog's time (`jn_rd32`, `p8x32a_hub_at`), entries done with at a catch-up (`jn_drop`), `jn_off`, `jn_writes`, `jn_full` (modified) |
| `src/cpu/p8x32a/p8x32ajit.cpp` | a translated lazy hub read of a journaled long stops the block (modified) |
| `src/wpc/pinheck/dmd.[ch]`, `src/wpc/pinheck.c` | the raw DMD's frame log and row model read hub RAM as at the scan cog's time; `PINHECK_JOURNAL=0` (modified) |
| `tests/pinheck/p8x32a/{run.c,check.sh,mutate_lazy.sh,chip/lzjournal.spin}` | `p8run -lazies` prints the journal's entries; `' EXPECT-JOURNAL`; the journal's chip test and mutants |
| `docs/build.md` | the optional profile-guided build for the pinHeck games (modified) |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | what Plan 12b established; its roadmap row (modified) |

---
### Task 1: Reference build

**Files:** none (a build).

**Interfaces:**
- Consumes: `pinheck` at `f69cea15`, Plan 9's `bench.sh`.
- Produces: `build/reference/sdl3pinmame`, which every later task compares with.

- [ ] **Step 1: Build the reference**

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12b.log 2>&1; tail -1 build/sdl3pinmame/12b.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```
Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: The reference on one thread**

America's Most Haunted with video, where the journal applies, as the instruction count later steps compare with:

```sh
amh env SDL3PINMAME=build/reference/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep bench
```
Expected (about 3 minutes with the first boot; the instruction count on one thread does not depend on the load):
```text
bench video opt: 0.613x over 24.0 s emulated (39.1 s wall), whole run 16.8 G instructions and 1.4 s CPU per emulated s, worst 100 ms 0.495x, load 1.30
```

### Task 2: A hub-write journal for the lazy cog

**Files:**
- Create: `tests/pinheck/p8x32a/chip/lzjournal.spin`
- Modify: `tests/pinheck/p8x32a/run.c`, `tests/pinheck/p8x32a/check.sh`, `tests/pinheck/p8x32a/mutate_lazy.sh` (the tests)
- Modify: `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32ajit.cpp`, `src/wpc/pinheck/dmd.h`, `src/wpc/pinheck/dmd.c`, `src/wpc/pinheck.c`

**Interfaces:**
- Consumes: Plan 12's lazy cogs (`lazy_write`, `lazy_run`, `lazy_catch`, the translated lazy blocks' hub reads), the raw DMD's frame log (`pinheck_dmd_frame`) and row model (`PINHECK_DMD_PROOF`).
- Produces: in `p8x32a`: `uint8_t jn, jn_off` (entries; `jn_off` 1 turns the journal off), `jn_sz[]`, `jn_old[][4]`, `jn_a[]`, `jn_t[]`, `uint8_t jmap[2048]`, `uint64_t jn_full, jn_writes`; `P8X32A_JN` (64); `uint8_t p8x32a_hub_at(const p8x32a *p, uint32_t a, uint64_t t)`; `p8x32a_jst.jmap`; in `pinheck_dmd`: `uint8_t (*hub_at)(void *ctx, uint32_t a, uint64_t t)`; `PINHECK_JOURNAL=0`; `p8run -lazies` also prints `p8run: E journal entries, F catch-ups with it full`; check.sh: `' EXPECT-JOURNAL: E F`.

- [ ] **Step 1: The tests**

`chip/lzjournal.spin`: cog 1 shows 8 hub bytes at `$6200` on P8–P15 in turn (P16 toggling each pass) and goes lazy; cog 0 then rewrites those bytes 400 times (bytes, words and every eighth round a long, the same bytes again and again), reads `INA` and stops cog 1. It expects one lazy cog, 1,621 journal entries and 23 catch-ups with the journal full. `mutate_lazy.sh` replaces its mutant of the catch-up at a write by four of the journal:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/run.c', [
    (r'''	if (lazies) fprintf(stderr, "p8run: %llu lazy cogs\n", (unsigned long long)chip.lazies);
	if (dump) {
''',
     r'''	if (lazies) fprintf(stderr, "p8run: %llu lazy cogs\n", (unsigned long long)chip.lazies);
	if (lazies) fprintf(stderr, "p8run: %llu journal entries, %llu catch-ups with it full\n", (unsigned long long)chip.jn_writes, (unsigned long long)chip.jn_full);
	if (dump) {
'''),
])
edit('tests/pinheck/p8x32a/check.sh', [
    (r'''	lazies=$(sed -n "s/^' EXPECT-LAZY: //p" "$1")
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 ${sleeps:+-sleeps} ${lazies:+-lazies} -dump "$o.ourhub" > "$o.our" 2> "$o.log"
''',
     r'''	lazies=$(sed -n "s/^' EXPECT-LAZY: //p" "$1")
	journal=$(sed -n "s/^' EXPECT-JOURNAL: //p" "$1")
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 ${sleeps:+-sleeps} ${lazies:+-lazies} -dump "$o.ourhub" > "$o.our" 2> "$o.log"
'''),
    (r'''	if [ -n "$lazies" ] && ! grep -qxF "p8run: $lazies lazy cogs" "$o.log"; then echo "LAZY $1: $(grep -F 'lazy cogs' "$o.log"), expected $lazies"; fail=$((fail + 1)); fi
	if grep -q "p8x32a: lazy\|p8run: lazy" "$o.log"; then echo "LAZY $1: $(grep "p8x32a: lazy\|p8run: lazy" "$o.log" | head -3)"; fail=$((fail + 1)); fi
''',
     r'''	if [ -n "$lazies" ] && ! grep -qxF "p8run: $lazies lazy cogs" "$o.log"; then echo "LAZY $1: $(grep -F 'lazy cogs' "$o.log"), expected $lazies"; fail=$((fail + 1)); fi
	# EXPECT-JOURNAL: e f = the lazy cog's journal took e entries and was full f times
	if [ -n "$journal" ] && ! grep -qxF "p8run: $(echo $journal | cut -d' ' -f1) journal entries, $(echo $journal | cut -d' ' -f2) catch-ups with it full" "$o.log"; then
		echo "JOURNAL $1: $(grep -F 'journal entries' "$o.log"), expected $journal"; fail=$((fail + 1)); fi
	if grep -q "p8x32a: lazy\|p8run: lazy" "$o.log"; then echo "LAZY $1: $(grep "p8x32a: lazy\|p8run: lazy" "$o.log" | head -3)"; fail=$((fail + 1)); fi
'''),
])
edit('tests/pinheck/p8x32a/mutate_lazy.sh', [
    (r'''mutant none '' ''
mutant hub-write-catch-up 'if (p->hub[ha + k] != (uint8_t)(v >> (8 * k))) { lazy_catch(p, h - 1); return; }' \
	'if (p->hub[ha + k] != (uint8_t)(v >> (8 * k))) return;'
mutant ina-lazy 'if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);' ';'
''',
     r'''mutant none '' ''
mutant journal-read 'if (p->jn_t[k] > t && (p->jn_a[k] & ~3u) == a)' 'if (0)'
mutant journal-earliest 'for (k = p->jn - 1; k >= 0; k--)
		if (p->jn_t[k] > t' 'for (k = 0; k < p->jn; k++)
		if (p->jn_t[k] > t'
mutant journal-full 'if (!p->jn_off) p->jn_full++;
		lazy_catch(p, h - 1);' 'if (!p->jn_off) p->jn_full++;'
mutant journal-old 'p->jn_old[p->jn][k] = p->hub[ha + k];' 'p->jn_old[p->jn][k] = (uint8_t)(v >> (8 * k));'
mutant ina-lazy 'if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);' ';'
'''),
])
EOF
```

```sh
cat > tests/pinheck/p8x32a/chip/lzjournal.spin <<'EOF'
' EXPECT-LAZY: 1
' EXPECT-JOURNAL: 1621 23
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0: starts the scan cog (cog 1) on 8 bytes at $6200, then rewrites them 400 times (bytes, words and longs, the
' same bytes again and again), reads INA, and stops the scan cog; results at $6000
entry   mov     x, wk
        shl     x, #2
        or      x, parf
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d20000
        waitcnt t, #0
        mov     k, d400
:burst  add     v, k
        wrbyte  v, b3
        rol     v, #3
        wrbyte  v, b5
        add     v, #77
        wrword  v, b2
        test    k, #7 wz
  if_z  wrlong  v, b4
        xor     v, k
        wrbyte  v, b3
        djnz    k, #:burst
        mov     r, ina
        wrlong  r, res0
        mov     t, cnt
        add     t, d3000
        waitcnt t, #0
        cogstop one
        mov     r, ina
        wrlong  r, res1
        cogid   r
        cogstop r
one     long    1
d400    long    400
d3000   long    3000
d20000  long    20000
parf    long    $62000000
b2      long    $6202
b3      long    $6203
b4      long    $6204
b5      long    $6205
res0    long    $6000
res1    long    $6004
r       long    0
k       long    0
t       long    0
v       long    $1234567
x       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
' the scan cog: each of the 8 bytes in turn on P8-P15, with P16 toggling
worker  mov     dira, pins
:frame  mov     p2, par
        mov     n, #8
:byte   rdbyte  v2, p2
        shl     v2, #8
        xor     v2, tbit
        mov     outa, v2
        add     p2, #1
        djnz    n, #:byte
        xor     tbit, tbit2
        jmp     #:frame
pins    long    $0001FF00
tbit    long    0
tbit2   long    $00010000
p2      long    0
n       long    0
v2      long    0
EOF
```

- [ ] **Step 2: Run them to see them fail**

```sh
flock /code/spooky_domino/work/emu.lock nice -n 15 tests/pinheck/p8x32a/check.sh 2>&1 | grep -m2 -E 'error|FAIL'
```
Expected: `p8run` does not build (no journal yet), the check exits 2:
```text
run.c:343:123: error: ‘p8x32a’ has no member named ‘jn_writes’
run.c:343:159: error: ‘p8x32a’ has no member named ‘jn_full’
```

- [ ] **Step 3: The journal**

`lazy_write` keeps a changed write's old bytes instead of catching the lazy cog up (unless the journal is full or off, or the lazy cog is past the write); `jn_rd32` gives a long as at a time; the lazy cog's `hub_rw` reads through it; `lazy_run` drops the entries it passed; a translated lazy hub read tests the long's bit in `jmap` and stops the block; the host reads the frame log's and the row model's bytes with `p8x32a_hub_at`:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.h', [
    (r'''	const uint8_t *hub;
} p8x32a_jst;
#define P8X32A_JOUT 256 /* the OUTA writes a run of blocks may leave */
#ifndef P8X32A_LZH
''',
     r'''	const uint8_t *hub;
	const uint8_t *jmap;   /* hub longs with a journal entry (p8x32a.jmap): a block stops before reading one */
} p8x32a_jst;
#define P8X32A_JOUT 256 /* the OUTA writes a run of blocks may leave */
#define P8X32A_JN 64    /* journal entries */
#ifndef P8X32A_LZH
'''),
    (r'''	uint64_t lazies;            /* lazy cogs entered */
	unsigned sched_gen; /* counts changes one cog makes to another cog's next event */
''',
     r'''	uint64_t lazies;            /* lazy cogs entered */
	/* the journal: other cogs' hub writes the lazy cog has not reached, with the bytes they replaced, in time order */
	uint8_t jn, jn_off;         /* entries; 1: none (the lazy cog catches up before each such write) */
	uint8_t jn_sz[P8X32A_JN], jn_old[P8X32A_JN][4];
	uint16_t jn_a[P8X32A_JN];
	uint64_t jn_t[P8X32A_JN];
	uint8_t jmap[2048];         /* bit a / 4: hub long a has an entry */
	uint64_t jn_full, jn_writes; /* catch-ups at a write because the journal was full; entries made */
	unsigned sched_gen; /* counts changes one cog makes to another cog's next event */
'''),
    (r'''uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir);
unsigned p8x32a_dasm(char *buf, uint32_t op);
''',
     r'''uint32_t p8x32a_pins(p8x32a *p, uint64_t t, uint32_t *dir);
uint8_t p8x32a_hub_at(const p8x32a *p, uint32_t a, uint64_t t); /* hub RAM as the lazy cog reads it at t */
unsigned p8x32a_dasm(char *buf, uint32_t op);
'''),
])
edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''
/* a hub write at h that changes a byte: the lazy cog reads hub RAM as it was before it */
P8_COLD void lazy_write(p8x32a *p, unsigned ha, unsigned sz, uint32_t v, uint64_t h)
''',
     r'''
/* a hub write at h that changes a byte: the lazy cog reads hub RAM as it was before it, the bytes it replaces kept
   in the journal (or, the journal full, by catching up first) */
P8_COLD void lazy_write(p8x32a *p, unsigned ha, unsigned sz, uint32_t v, uint64_t h)
'''),
    (r'''	for (k = 0; k < sz; k++)
		if (p->hub[ha + k] != (uint8_t)(v >> (8 * k))) { lazy_catch(p, h - 1); return; }
}
''',
     r'''	for (k = 0; k < sz; k++)
		if (p->hub[ha + k] != (uint8_t)(v >> (8 * k))) break;
	if (k == sz || h - 1 <= p->lz_at) return;
	if (p->jn_off || p->jn == P8X32A_JN) {
		if (!p->jn_off) p->jn_full++;
		lazy_catch(p, h - 1);
		return;
	}
	p->jn_t[p->jn] = h;
	p->jn_a[p->jn] = (uint16_t)ha;
	p->jn_sz[p->jn] = (uint8_t)sz;
	for (k = 0; k < sz; k++) p->jn_old[p->jn][k] = p->hub[ha + k];
	p->jmap[ha >> 5] |= (uint8_t)(1u << ((ha >> 2) & 7));
	p->jn++;
	p->jn_writes++;
}

/* the hub long at a (aligned) as it was at t: the bytes of the earliest entry after t that wrote them */
static uint32_t jn_rd32(const p8x32a *p, uint32_t a, uint64_t t)
{
	uint8_t b[4];
	int k, j;
	memcpy(b, p->hub + a, 4);
	for (k = p->jn - 1; k >= 0; k--)
		if (p->jn_t[k] > t && (p->jn_a[k] & ~3u) == a)
			for (j = 0; j < p->jn_sz[k]; j++) b[(p->jn_a[k] & 3) + j] = p->jn_old[k][j];
	return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

uint8_t p8x32a_hub_at(const p8x32a *p, uint32_t a, uint64_t t)
{
	a &= 0xFFFF;
	if (!p->jn || !(p->jmap[a >> 5] >> ((a >> 2) & 7) & 1)) return p->hub[a];
	return (uint8_t)(jn_rd32(p, a & ~3u, t) >> (8 * (a & 3)));
}

/* the lazy cog has run to t: the entries up to t are done with */
static void jn_drop(p8x32a *p, uint64_t t)
{
	int k, j = 0;
	for (k = 0; k < p->jn; k++) p->jmap[p->jn_a[k] >> 5] = 0;
	for (k = 0; k < p->jn; k++)
		if (p->jn_t[k] > t) {
			p->jn_t[j] = p->jn_t[k];
			p->jn_a[j] = p->jn_a[k];
			p->jn_sz[j] = p->jn_sz[k];
			memcpy(p->jn_old[j], p->jn_old[k], 4);
			p->jmap[p->jn_a[j] >> 5] |= (uint8_t)(1u << ((p->jn_a[j] >> 2) & 7));
			j++;
		}
	p->jn = (uint8_t)j;
}
'''),
    (r'''	p8x32a_loop *l = &p->loop[n];
	uint32_t *ram = p->cog[n].ram, a = s & 0xFFFF, w = rd32(p, a), r;
	unsigned op = OP(i), dst = DST(i);
''',
     r'''	p8x32a_loop *l = &p->loop[n];
	uint32_t *ram = p->cog[n].ram, a = s & 0xFFFF, w = p->jn && p->lz_on && n == p->lz ? jn_rd32(p, a & 0xFFFC, h) : rd32(p, a), r;
	unsigned op = OP(i), dst = DST(i);
'''),
    (r'''					st.hub = p->hub;
					st.latch = 0;
''',
     r'''					st.hub = p->hub;
					st.jmap = p->jmap;
					st.latch = 0;
'''),
    (r'''	p->lz_at = t;
}
''',
     r'''	p->lz_at = t;
	if (p->jn) jn_drop(p, t);
}
'''),
    (r'''	p->lz_on = p->lz_nh = 0;
	memset(p->lz_try, 0, sizeof(p->lz_try));
''',
     r'''	p->lz_on = p->lz_nh = 0;
	p->jn = 0;
	memset(p->jmap, 0, sizeof(p->jmap));
	memset(p->lz_try, 0, sizeof(p->lz_try));
'''),
])
edit('src/cpu/p8x32a/p8x32ajit.cpp', [
    (r'''			a.bind(on);
			// the next instruction starts at latch + 7
''',
     r'''			a.bind(on);
			// a long with a journal entry: the interpreter reads it
			a.mov(x86::esi, x86::ecx);
			a.and_(x86::esi, 0xFFFF);
			a.shr(x86::esi, 2);
			a.mov(x86::r11, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, jmap)));
			a.bt(x86::dword_ptr(x86::r11), x86::esi);
			a.jc(late);
			// the next instruction starts at latch + 7
'''),
])
edit('src/wpc/pinheck/dmd.h', [
    (r'''	const uint8_t *hub;  /* row model: hub RAM */
	uint32_t buf;        /* row model: hub address of the 4 bpp frame (two dots a byte, the left one high) */
''',
     r'''	const uint8_t *hub;  /* row model: hub RAM */
	uint8_t (*hub_at)(void *ctx, uint32_t a, uint64_t t); /* row model: hub RAM as at t, if set (else hub) */
	uint32_t buf;        /* row model: hub address of the 4 bpp frame (two dots a byte, the left one high) */
'''),
])
edit('src/wpc/pinheck/dmd.c', [
    (r'''/* P18 rising: the row's dots go to the output latches; P20 marks a subframe's first row */
static void latch(pinheck_dmd *d, uint32_t now)
{
''',
     r'''/* P18 rising: the row's dots go to the output latches; P20 marks a subframe's first row */
static void latch(pinheck_dmd *d, uint32_t now, uint64_t t)
{
'''),
    (r'''		for (x = 0; x < DMD_W; x += 2) {
			const uint8_t v = d->hub[(d->buf + (uint32_t)d->rows * (DMD_W / 2) + (uint32_t)(x >> 1)) & 0xFFFFu];
			if ((v >> 4) > level) d->latched[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
''',
     r'''		for (x = 0; x < DMD_W; x += 2) {
			const uint32_t a = (d->buf + (uint32_t)d->rows * (DMD_W / 2) + (uint32_t)(x >> 1)) & 0xFFFFu;
			const uint8_t v = d->hub_at ? d->hub_at(d->ctx, a, t) : d->hub[a];
			if ((v >> 4) > level) d->latched[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
'''),
    (r'''	}
	if (rise & DMD_P18) latch(d, now);
	if (rise & DMD_P19) row_clock(d, now, t);
''',
     r'''	}
	if (rise & DMD_P18) latch(d, now, t);
	if (rise & DMD_P19) row_clock(d, now, t);
'''),
])
edit('src/wpc/pinheck.c', [
    (r'''	for (k = 0; k < DMD_W * DMD_H && at != 0xFFFFFFFFu; k += 2)
		if (prop.chip.hub[(at + k / 2) & 0xFFFF] != (shades[k] << 4 | shades[k + 1])) at = 0xFFFFFFFFu;
	for (k = 0; k < 8; k++) stamp[k] = (uint8_t)(t >> (8 * k));
''',
     r'''	for (k = 0; k < DMD_W * DMD_H && at != 0xFFFFFFFFu; k += 2)
		if (p8x32a_hub_at(&prop.chip, (at + k / 2) & 0xFFFF, t) != (shades[k] << 4 | shades[k + 1])) at = 0xFFFFFFFFu;
	for (k = 0; k < 8; k++) stamp[k] = (uint8_t)(t >> (8 * k));
'''),
    (r'''	ser.edge = t;
}

static void pinheck_dmd_pins_cb(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
''',
     r'''	ser.edge = t;
}

/* hub RAM as the scan cog read it at t (it runs behind, the writes since in its journal) */
static uint8_t pinheck_dmd_hub_at(void *ctx, uint32_t a, uint64_t t) { (void)ctx; return p8x32a_hub_at(&prop.chip, a, t); }

static void pinheck_dmd_pins_cb(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
'''),
    (r'''		pinheck_dmd_init(&dmd_row, 0, prop.chip.hub, (uint32_t)pinheck_game()->dmdHub, NULL, pinheck_dmd_row_sub, NULL);
		dmd_head = dmd_tail = 0;
''',
     r'''		pinheck_dmd_init(&dmd_row, 0, prop.chip.hub, (uint32_t)pinheck_game()->dmdHub, NULL, pinheck_dmd_row_sub, NULL);
		dmd_row.hub_at = pinheck_dmd_hub_at;
		dmd_head = dmd_tail = 0;
'''),
    (r'''	prop_init(&prop, memory_region(PINHECK_BIOSREGION), propmem);
#ifdef PINMAME_JIT_ASMJIT
''',
     r'''	prop_init(&prop, memory_region(PINHECK_BIOSREGION), propmem);
	prop.chip.jn_off = getenv("PINHECK_JOURNAL") && atoi(getenv("PINHECK_JOURNAL")) == 0;
#ifdef PINMAME_JIT_ASMJIT
'''),
])
EOF
```

- [ ] **Step 4: The suites pass**

```sh
flock /code/spooky_domino/work/emu.lock nice -n 15 tests/pinheck/p8x32a/check.sh | tail -1
P8X32A_JIT=1 flock /code/spooky_domino/work/emu.lock nice -n 15 tests/pinheck/p8x32a/check.sh | grep -E 'translated|passed'
nice -n 15 tests/pinheck/display/check.sh | tail -1
```
Expected (about 45 minutes, the RTL references of a fresh worktree and the suite's `mutate_lazy.sh` included):
```text
p8x32a: 241 passed, 0 failed
p8run: translated
p8x32a: 241 passed, 0 failed
display: 0 failed
```

- [ ] **Step 5: America's Most Haunted byte-identical, and faster**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12b.log 2>&1; tail -1 build/sdl3pinmame/12b.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh video | grep -E 'bench video opt|determinism|FAIL'
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 PINHECK_JOURNAL=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|FAIL'
```
Expected (about 6 minutes):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/amh.nv
bench video opt: 0.650x over 24.0 s emulated (36.9 s wall), whole run 16.6 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.417x, load 2.12
determinism video: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32ajit.cpp src/wpc/pinheck/dmd.h src/wpc/pinheck/dmd.c src/wpc/pinheck.c tests/pinheck/p8x32a/run.c tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/mutate_lazy.sh tests/pinheck/p8x32a/chip/lzjournal.spin
git commit -m "p8x32a: a journal of other cogs' hub writes, so the lazy cog catches up only where its time is observed"
```

### Task 3: The profile-guided build, documented for users

**Files:**
- Modify: `docs/build.md`

**Interfaces:**
- Consumes: Plan 9d's `pgo.sh` and `cmake/pgo.cmake`.
- Produces: `build/pgo-use/sdl3pinmame` (trained on Domino's attract mode and video, as Plan 9d); `docs/build.md`'s section for the pinHeck games.

- [ ] **Step 1: The documentation**

```sh
python3 - <<'EOF'
F = '`' * 3
p = 'docs/build.md'
s = open(p).read()
a = r'''while the game runs to break into it. F1 lists the debugger keys.
'''
b = a + '\n' + r'''### A faster build for the pinHeck games (optional, GCC or Clang)

The Spooky Pinball pinHeck games (`dominos`, `rzspook`, `jetsons`, `amh`) emulate both of the board's processors
instruction by instruction, and America's Most Haunted and The Jetsons need all of a recent laptop's speed. A
profile-guided build runs them about 5–10% faster. It needs the Propeller's 32 KB mask ROM (`p8x32a.rom`) and a
romset to train on (Domino's, or another pinHeck game with `PINHECK_GAME=amh`, `jetsons` or `rzspook`); the
training plays it for about two minutes:

@@F@@shell
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
P8X32A_ROM=/path/to/p8x32a.rom PINHECK_ZIP=~/.pinmame/roms/dominos.zip tests/pinheck/perf/pgo.sh -DPLATFORM=linux -DARCH=x64
./build/pgo-use/sdl3pinmame -rompath ~/.pinmame/roms amh
@@F@@

The profile stays in `build/pgo-profile`; the emulation's output is the same as without it.
'''.replace('@@F@@', F)
assert s.count(a) == 1, (p, a[:70])
open(p, 'w').write(s.replace(a, b, 1))
EOF
```

- [ ] **Step 2: The profile-guided build**

```sh
flock /code/spooky_domino/work/heavy.lock flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/pgo.sh -DPLATFORM=linux -DARCH=x64 | grep -E '^pgo: |pgo training: bench'
```
Expected (about 25 minutes; it builds twice and trains on Domino's):
```text
pgo training: bench attract opt: 0.668x over 30.0 s emulated (44.9 s wall), whole run 15.6 G instructions and 1.7 s CPU per emulated s, worst 100 ms 0.402x, load 2.43
pgo training: bench video opt: 0.580x over 24.0 s emulated (41.4 s wall), whole run 17.0 G instructions and 1.9 s CPU per emulated s, worst 100 ms 0.406x, load 1.70
pgo: build/pgo-use/sdl3pinmame
```

- [ ] **Step 3: Commit**

```bash
git add docs/build.md
git commit -m "docs: the profile-guided build for the pinHeck games"
```

### Task 4: Speed, regression, the tests bite, the documents

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

**Interfaces:**
- Consumes: everything above; `build/reference/sdl3pinmame`, `build/pgo-use/sdl3pinmame`.
- Produces: the Result's measurements; spec §4's paragraph for Plan 12b; the roadmap's row.

- [ ] **Step 1: Every game against the reference, without and with the profile, with the worker thread**

```sh
for b in build/sdl3pinmame/sdl3pinmame build/pgo-use/sdl3pinmame; do
	for g in amh jet rz; do $g env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=$b flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video play | sed "s|^|$b $g: |"; done
	REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=$b flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video | sed "s|^|$b dominos: |"
done
```
Expected: every `determinism` line `identical` (America's Most Haunted's with `dmd.bin`), no `FAIL`; the speeds are the Result's table (about 80 minutes; the reference's runs are cached after the first pass, so the second pass compares the profile with the same reference runs). For example:
```text
build/sdl3pinmame/sdl3pinmame amh: determinism first boot: identical nvram.base/amh.nv
build/sdl3pinmame/sdl3pinmame amh: bench attract opt: 1.136x over 30.0 s emulated (26.4 s wall), whole run 15.1 G instructions and 1.2 s CPU per emulated s, worst 100 ms 1.038x, load 2.19
build/sdl3pinmame/sdl3pinmame amh: bench attract ref: 1.064x over 30.0 s emulated (28.2 s wall), whole run 15.2 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.803x, load 1.88
build/sdl3pinmame/sdl3pinmame amh: determinism attract: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
…
build/pgo-use/sdl3pinmame jet: bench play opt: 0.951x over 55.0 s emulated (57.8 s wall), whole run 17.3 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.584x, load 1.15
build/pgo-use/sdl3pinmame jet: bench play ref: 0.865x over 55.0 s emulated (63.6 s wall), whole run 18.5 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.532x, load 1.28
build/pgo-use/sdl3pinmame jet: determinism play: identical uart.log frames.bin snd.wav nvram/jetsons.nv
```

- [ ] **Step 2: On one thread; the switches give the same output**

```sh
for g in amh jet; do $g env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract | grep -E 'opt:|determinism attract|FAIL' | sed 's/^/one thread: /'; done
for s in PINHECK_JOURNAL=0 PINHECK_LAZY=0 PINHECK_JIT=0; do amh env $s REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|FAIL' | sed "s/^/$s: /"; done
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh contention | tail -1
```
Expected (about 10 minutes):
```text
one thread: bench attract opt: 0.754x over 30.0 s emulated (39.8 s wall), whole run 14.7 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.570x, load 1.25
one thread: determinism attract: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
one thread: bench attract opt: 0.766x over 30.0 s emulated (39.1 s wall), whole run 16.2 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.532x, load 1.49
one thread: determinism attract: identical uart.log frames.bin snd.wav nvram/jetsons.nv
PINHECK_JOURNAL=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_LAZY=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_JIT=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
contention: ok
```

- [ ] **Step 3: Regression**

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
Expected (about 60 minutes):
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

- [ ] **Step 4: The machine-level test bites**

A journal mutant the RTL tests catch must also fail America's Most Haunted's determinism:

```sh
f=src/cpu/p8x32a/p8x32a.c
python3 -c "import sys; p=sys.argv[1]; s=open(p).read(); a,b=sys.argv[2],sys.argv[3]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" $f '		if (p->jn_t[k] > t && (p->jn_a[k] & ~3u) == a)' '		if (0)'
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12b.log 2>&1; tail -1 build/sdl3pinmame/12b.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short 2>&1 | grep -m2 FAIL
git checkout $f
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12b.log 2>&1; tail -1 build/sdl3pinmame/12b.log
git status --short | grep -v '^??'
```
Expected (about 5 minutes; `git status` prints nothing):
```text
[100%] Built target sdl3pinmame
DETERMINISM FAIL short: frames.bin differs from the reference build's
DETERMINISM FAIL short: dmd.bin differs from the reference build's
Updated 1 path from the index
[100%] Built target sdl3pinmame
```

- [ ] **Step 5: The documents**

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
     r'''- **Established by Plan 12b (the levers after Plan 12):**
  - Measured (worker thread): America's Most Haunted's Propeller thread with video is busy 1.17 s per emulated second; the scheduler dispatches about 6 million events of its three Spin interpreters and 1.1 million of the SD driver, and the lazy scan cog catches up 1.1 million times (0.9 million before other cogs' hub writes). The Jetsons' SD driver and display link take 3–3.5 million dispatches and 10 million inline events, with 9 million calls of the pin devices.
  - Rejected after exact prototypes (byte-identical, RTL-checked): hub reads that run before other cogs' events when those cannot reach the hub first (no gain: the checks cost what the dispatches cost), and the SD card and display link as devices bound to their cogs, whose pin events run ahead of other cogs while a lookahead over those cogs' code shows they cannot observe or drive the pins (slower: the bound path alone costs more than the pending points, the lookahead more than the dispatches it saves; with both levers the display link lost bits, cause not found). Measurement-only ceilings: 30% fewer host instructions for The Jetsons, 11% for America's Most Haunted, if every such event could run ahead.
  - *A hub-write journal* (`p8x32a.c`): the lazy cog no longer catches up before another cog's hub write; the replaced bytes go into a journal of 64 entries, its reads take each byte as it was at the read, translated lazy reads stop at a journaled long, and the host reads hub RAM for the scan cog's time through the journal (`p8x32a_hub_at`: the raw DMD's frame log and row model). A chip test (1,621 entries, 23 times full) and four mutants in `mutate_lazy.sh`. America's Most Haunted with video: −1.8% host instructions on one thread.
  - *Profile-guided builds:* `docs/build.md` gives the optional build for users. Trained on Domino's (training on all four games was slower): 5–8% fewer host instructions, 4–8% faster.
  - Every output byte-identical to Plan 12's build (UART1, frames and the raw DMD's subframes, sound, NVRAM) for the four games with the worker, without it, with `PINHECK_JOURNAL=0`, without lazy cogs and without the translator, and for the profile-guided build. Speed with the worker (loads 1.2–2.2), without / with the profile: America's Most Haunted 1.14×/1.10× attract, 0.86×/0.86× video, 0.80×/0.81× play; The Jetsons 1.00×/1.04×, 0.90×/0.97×, 0.88×/0.95×; Rob Zombie 1.62×/1.69×, 1.36×/1.43×, 1.22×/1.28×; Domino's 1.38×/1.46×, 1.12×/1.27×. Real time is reached in attract mode only (The Jetsons' with the profile); not with video or in play.
- **Proof for every stage:**'''),
])

edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    (r'''| release | release:''',
     r'''| `2026-10-02-pinheck-12b-speed.md` | 12b: the levers Plan 12 left, measured: hub reads ahead of other cogs' events and the SD card and display link bound to their cogs prototyped exact and rejected (slower); a hub-write journal for the lazy cog; profile-guided builds re-measured on the four games and documented for users | Plan 12 | none | every output byte-identical to Plan 12's build for the four games, every switch and the profile-guided build; the RTL, spinsim and QEMU suites green with a journal chip test and its mutants; speeds with and without the profile in the plan's Result | written |
| release | release:'''),
    (r'''- After Plan 12: with the worker thread, without a profile,''',
     r'''- After Plan 12b: with the worker thread, America's Most Haunted runs 1.14× / 0.86× / 0.80× (attract, video, play) without a profile and 1.10× / 0.86× / 0.81× with the profile-guided build, The Jetsons 1.00× / 0.90× / 0.88× and 1.04× / 0.97× / 0.95×. What remains for real time with video and in play: letting the SD driver's and the display link's pin events and the interpreters' hub reads run ahead of the other cogs (measurement-only ceilings: −30% host instructions for The Jetsons, −11% for America's Most Haunted), which needs a lookahead over the other cogs' code that costs less than the dispatch it saves (per-address distances to the next instruction that could interfere, kept valid by the translator's tracking of code writes), and the cause of the display link's lost bits when the bound SD cog and early hub reads run together.
- After Plan 12: with the worker thread, without a profile,'''),
])
EOF
```

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "docs: what Plan 12b established (the levers measured, the journal, profile-guided builds for users) and its roadmap row"
```

## Windows checklist

For the Windows session (VS2022 BuildTools / MSVC 14.44, `C:\code\dominos`), after Plan 12b is pushed. Results come back by message; a Windows-only fix comes back as a diff with a test, applied on Linux. Never push. Muted or the silent device for every item.

1. **Build.** MSVC x64 and Win32 standalone PinMAME, VPinMAME64 and libpinmame, exactly as CI does. Expected: all build; no warning in `p8x32a.c`, `p8x32ajit.cpp`, `dmd.c`, `pinheck.c`.
2. **Determinism across compilers.** The scripted Domino's game (`tests\pinheck\game`, as for Plan 9d): the score 32176700 and the four logs as on Linux.
3. **Speed with and without the journal.** `PinMAME.exe amh`, unthrottled (`-nothrottle`), 60 s of attract mode with `PINHECK_TIME_LOG=time.log`, then the same with `PINHECK_JOURNAL=0`: the last line's emulated over host seconds.
4. **The MSVC profile-guided build** (Plan 9d's item, now trained on America's Most Haunted and The Jetsons too): `PINMAME_PGO=GEN`, 60 s of attract mode of each game, `PINMAME_PGO=USE`; the speed of `amh` and `jetsons` against the build without it, and the scripted game's logs unchanged.

## Result

Measured in the replay of this plan's text: a fresh worktree of `f69cea15` (branch `pinheck-12b`), every step run as written, every Expected line checked, on the Ryzen 7 7730U while other agents' work shared it (one emulator at a time, `emu.lock`); the load average is printed with every speed.

- **Task 1:** the reference on one thread, America's Most Haunted with video: 16.8 G host instructions per emulated second.
- **Task 2:** the tests failed first (`p8run` does not build without the journal), then the Propeller suite: 241 passed, 0 failed, interpreted and translated, `mutate_lazy.sh` inside it with the four journal mutants caught (19, 3, 1 and 19 cases); the display suite passes. America's Most Haunted with video on one thread: 16.6 G (16.8 G for the reference), byte-identical with `dmd.bin`; with `PINHECK_JOURNAL=0` byte-identical too.
- **Task 3:** `docs/build.md` gains the optional profile-guided build; `pgo.sh` built and trained it on Domino's.
- **Task 4, speed with the worker thread** (each workload against the reference in the same session, the profile-guided build against the same reference runs; all 30 determinism lines identical, no `FAIL`):

| Game | Attract | Video | Play | With the profile: attract, video, play | Reference: attract, video, play |
|---|---|---|---|---|---|
| America's Most Haunted | 1.136× (15.1 G) | 0.860× (17.0 G) | 0.795× (19.1 G) | 1.096×, 0.858×, 0.809× (14.4, 16.2, 18.1 G) | 1.064×, 0.603×*, 0.755× (15.2, 16.8, 19.4 G) |
| The Jetsons | 1.002× (16.2 G) | 0.897× (16.7 G) | 0.883× (18.5 G) | 1.041×, 0.966×, 0.951× (15.1, 15.5, 17.3 G) | 0.950×, 0.874×, 0.865× (16.2, 16.6, 18.5 G) |
| Rob Zombie | 1.617× | 1.363× | 1.219× | 1.691×, 1.427×, 1.281× | 1.593×, 1.342×, 1.196× |
| Domino's | 1.384× | 1.121× | — | 1.456×, 1.265× | 1.359×, 1.183× |

  Loads 1.2–2.2 (*the reference's America's Most Haunted video ran at load 1.45 in a slow interval; its instruction count is the comparable figure). Instruction counts with the worker include its spinning. On one thread: America's Most Haunted's attract mode 0.754× at 14.7 G, The Jetsons' 0.766× at 16.2 G, both byte-identical; `PINHECK_JOURNAL=0`, `PINHECK_LAZY=0` and `PINHECK_JIT=0` give America's Most Haunted's short run byte-identical; `bench.sh contention` passes.
- **Task 4, regression:** the boot, display, service-test and simulator checks of all four games, the romset check, Domino's scripted game (44 checks, 0 failures), the audio and look checks, libpinmame for Domino's, America's Most Haunted (the raw DMD) and The Jetsons, and the MIPS32 (208 passed), PIC32MX, link, audio, board, display and storage suites pass; the journal mutant that ignores entries fails America's Most Haunted's determinism (`frames.bin` and `dmd.bin`), and the tree is clean afterwards.
- **Gain per lever** (the brief's list): (1) and (2), the SD card and the display link bound to their cogs: built exact, slower, rejected (Ruling 4); (3) hub reads ahead of the other cogs' events: three exact forms, no gain, rejected (Ruling 3); (4) the journal: −1.2% to −1.8% host instructions for America's Most Haunted (16.6 G against 16.8 G with video, 19.1 G against 19.4 G in play, 15.1 G against 15.2 G in attract mode), none for the other games (no lazy cog); (5) the profile-guided build: 5–8% fewer host instructions for every game and 4–8% faster where the wall clock resolves it, trained on Domino's (Ruling 6).

**What remains.** America's Most Haunted with video (0.86×) and in play (0.80–0.81×) and The Jetsons with video (0.90×, 0.97× with the profile) and in play (0.88×, 0.95×) are below real time; attract mode is at 1.14× (America's Most Haunted) and 1.00–1.04× (The Jetsons). Every one of them is bound by the Propeller thread. Ruling 2's ceilings say where the time is: letting the SD driver's and the display link's pin events and the interpreters' hub reads run ahead of the other cogs would take 30% of The Jetsons' host instructions and 11% of America's Most Haunted's. Doing that exactly needs a lookahead over the other cogs' code that costs less than the dispatch it saves: per-address distances to the next instruction that could interfere, kept valid by the translator's tracking of code writes, instead of Ruling 4's per-event scan; and before that the cause of the display link's lost bits when a bound SD cog and early hub reads run together must be found. The prototype of both (bound cogs, the lookahead, early hub reads, their RTL tests `bound.spin` and `gen_hub.py`) is kept in `/code/spooky_domino/work/m12b/`.
