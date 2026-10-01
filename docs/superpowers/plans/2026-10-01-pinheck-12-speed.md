# Plan 12: America's Most Haunted and The Jetsons at real time (lazy output cogs, uncertain RF13 reads) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** America's Most Haunted (`amh`) and The Jetsons (`jetsons`) run faster on the reference machine, every output byte-identical to Plan 11c's build: the raw DMD's scan cog runs as a *lazy cog* outside the Propeller's schedule, and the PIC32 thread no longer waits for the Propeller at a `PORTF` read whose `RF13` bit the code does not use; Plan 11c's four review minors are folded in.

**Architecture:** Two exact mechanisms, each proven against the existing differential suites and by byte-identical machine runs. (1) *Lazy cogs* (`p8x32a.c`): a cog whose code from its next instruction on only reads hub RAM, `PAR` and `CNT`, computes in cog RAM, jumps to fixed addresses and writes `OUTA` on pins that no other cog or counter drives and that only the host's lazy path watches, leaves the schedule and runs behind the other cogs; it catches up before another cog's hub write that changes a byte, before an `INA` read whose mask includes its pins, and at the end of `p8x32a_run_until`; its pin changes go to `bus.lazy_pins` in time order instead of becoming pending points; a wait or idle-loop sleep on its pins, another cog driving them, or a cog operation on it makes it an ordinary cog again. Its blocks are translated with their `OUTA` writes and hub reads, and stop at the catch-up's time limit. (2) *Uncertain port bits* (`mips32.c`, `pic32mx.c`, `prop.c`): with the worker thread, a `PORTF` read returns `RF13` from the last value known and marks that bit uncertain; the worker reads the true `P24` in the queued command order (`CMD_SAMPLE`, the same catch-up `prop_p24` made); the MIPS32 core steps carefully while a register holds an uncertain bit and waits for the true bit only when an instruction reads the register (an `AND`/`ANDI` that clears the bit does not need it); a write that does not read it makes it known.

**Tech Stack:** C (C89-syntax-clean cores and driver), C++17 (asmjit translator), Spin/PASM test programs (openspin, the P1 Verilog RTL), Python 3 (no third-party modules), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §4 (Milestone 9: at least 1.0× on the reference machine, aim 1.2×; byte-identical determinism; every suite green including the RTL, spinsim and QEMU differentials), applied to the games of Plans 11b and 11c. Measured reasons: Plan 11c's Ruling 14 and Result (America's Most Haunted at 0.44–0.56×), Plan 11b's Result (The Jetsons at 0.84–0.94×).

## Prerequisites

- `pinheck` at `8fa0a5e5` (Plan 11c merged; its code is identical to the `pinheck-11c` branch this plan was prototyped on).
- Before starting, from the repository root, export what Plan 11c's checks use (`TOOLS`, `P8X32A_ROM`, `PINHECK_UPDATE_DIR`, `PINHECK_ZIP`, `DOMINOS_PRP`, `DOMINOS_ZIP`, `RZSPOOK_ZIP`, `RZSPOOK_UPDATE_DIR`, `JETSONS_ZIP`, `JETSONS_UPDATE_DIR`, `AMH_ZIP`, `AMH_UPDATE_DIR`, `AMH_HEX`, `SDL_AUDIO_DRIVER=dummy`) and define the game functions once:
  ```sh
  amh() { PINHECK_GAME=amh PINHECK_ZIP=$AMH_ZIP PINHECK_UPDATE_DIR=$AMH_UPDATE_DIR "$@"; }
  jet() { PINHECK_GAME=jetsons PINHECK_ZIP=$JETSONS_ZIP PINHECK_UPDATE_DIR=$JETSONS_UPDATE_DIR "$@"; }
  rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
  ```
  A Domino's run is the command without a function. Run the steps with `bash` (the functions are used with `env`).
- The machine is shared: only one emulator runs at a time machine-wide, so every step that runs PinMAME, libpinmame or a firmware program goes through `flock /code/spooky_domino/work/emu.lock` (on another machine drop the `flock`); builds and heavy suites run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6`. Every PinMAME launch has a `timeout -k 30`, a private `-nvram_directory` and `-cfg_directory`, and no sound device (`SDL_AUDIO_DRIVER=dummy`); `bench.sh` and the machine checks do this themselves.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit. The scripts of Tasks 3 and 4 were generated from the prototype and replayed in this order on a fresh worktree of `8fa0a5e5`.
- Speeds are wall-clock on the Ryzen 7 7730U while other agents' builds and emulators share it; the load average is printed with every speed. Instruction counts on one thread (`PINHECK_THREADS=0`) do not depend on the load; with the worker thread they include the threads' spinning.

## Global Constraints

- **Byte-identical:** every game's UART1 log, display frames (a raw DMD's 16-subframe cycles and, new in `bench.sh`, its subframes as the core gets them: `PINHECK_DMD_LOG`), sound and NVRAM equal those of the reference build (Task 1: `pinheck` at `8fa0a5e5`) in attract mode, with video and in play; the scripted Domino's game's four logs are byte-identical.
- **All suites green:** `tests/pinheck/{mips32,pic32mx,storage,link,p8x32a,display,audio,board}/check.sh` (the `p8x32a` suite, which also runs the EEPROM test, with and without the translator, `P8X32A_JIT=1`), and the machine checks of Plans 5–11c for all four games.
- `PINHECK_THREADS=0`, `PINHECK_JIT=0` keep working; `PINHECK_LAZY=0` turns lazy cogs off, `PINHECK_RF13=0` turns the uncertain `RF13` reads off; every combination gives the same output.
- Code comments short and factual; the cores and the driver C89-syntax-clean.
- Never commit the mask ROM, firmware, SD cards or romsets.

## Rulings

Decisions taken on measurements of the prototype (branch `pinheck-12-proto`, the four candidates of the brief measured before choosing). Unless they say otherwise, figures are per emulated second of attract mode without a profile-guided build; "one thread" is `PINHECK_THREADS=0`.

1. **Where America's Most Haunted's time went.** On one thread the reference spends 22.2 G host instructions (0.56×), Domino's 11.5 G. With the scan cog never started (a measurement only: the firmware runs on, the display stays dark) it is 11.1 G (0.97×): the scan cog cost about 10 G, of which 4.1 G was the pin machinery its 9.6 million `OUTA` writes a second set off (pending points, `flush`, `pins_out`) and the rest its 5.7 million scheduler passes and the passes it took from the other cogs. With the worker thread the reference is no faster than on one thread (0.556× against 0.560×): the PIC32 reads `PORTF` 197,000 times a second, 99.4% of them from the cabinet switch routine (`digitalRead(45)` = `RF0`, return address `9D023034`), and each read waited for the Propeller. With the scan cog absent and those reads not waiting, attract mode runs at 1.47×; with the scan cog absent alone at 1.04×.
2. **Candidate 1 is taken as lazy cogs, not as an eager fast path.** Making the scan cog's `OUTA` writes cheap while keeping them events (pin machinery bypassed, a measurement only) gave 16.7 G; the cost that remains is the cog being in the schedule at all. A lazy cog leaves the schedule. It is generic: no knowledge of the scan cog's program, only a reachability check of its code from its next instruction (system ops, waits, hub writes, `INA`/`PHS` sources, special destinations other than `OUTA`, register jumps and writes into reachable code exclude a cog) and dynamic conditions (no other cog's `DIRA` or NCO counter on its pins, no sleeper or `WAITPxx` on them, its counters off, its pins all in the host's `lazy_ok`). It is exact by construction: it reads hub RAM exactly as it was at each read (it catches up before every other cog's hub write that changes a byte), its pins are exact wherever the emulation can observe them (`INA` with a mask on them, `p8x32a_pins`, the end of `p8x32a_run_until`), and every event that would make its lateness observable makes it an ordinary cog first. The P1 RTL decides: a new chip test (`chip/lazy.spin`: a scan cog reading a hub buffer another cog rewrites, masked and unmasked `INA`, `WAITPEQ` on its pins, `DIRA` on its pins, `COGSTOP`) enters lazy mode three times and matches the RTL trace and hub RAM; every existing chip, ISA and random test now runs with lazy cogs allowed (`run.c` merges the lazy path into its trace) and still matches, interpreted and translated. Rejected: a high-level model of the scan cog (candidate 2: it would need the cog's program recognised word by word and a second proof for the frame sender); a hub-write journal that lets the lazy cog lag across writes (measured cost of the catch-ups at writes: 0.7 G; the journal would move the frame log's hub check and stamp off their present definitions).
3. **The lazy cog's code is translated with its `OUTA` writes and hub reads.** In the interpreter it cost 17.0 G; translated `OUTA` writes (left in a list the block's caller hands to `bus.lazy_pins`), blocks that stop where the catch-up's time limit falls (the lazy cog's catch-ups land anywhere in its loop), translated hub reads without `WC` (slot timing as `next_slot`), `event_run`/`jit_get` kept inlined and the lazy paths out of line: 13.3 G. Mask-aware `INA` (Ruling 4) took video from 17.6 G to 17.0 G. The non-lazy paths cost about 3% more instructions on one thread (The Jetsons 13.2 → 13.6 G): accepted, the worker-thread speed of every game went up (Result).
4. **An `INA` read sees the lazy cog's pins only where it can use them.** `AND`/`ANDN`/`TEST`/`TESTN` with `INA` as source use only `INA & D`; a wait uses its mask; an idle loop's inputs their masks. Only those masks that include the lazy pins make the lazy cog catch up; other reads get the lazy pins as 0, which nothing reads. The SD card driver's `test DO, ina` reads (2.4 million a second with video) no longer catch the scan cog up.
5. **The frame log of a raw DMD reads hub RAM when its cycle's last row clock reaches the decoder.** With the scan cog lazy that is exactly the time of that row clock (the lazy cog never passes another cog's hub write); in Plan 11c's build it was the moment `flush` delivered that pin change, a few cycles later. The two differ only if a hub write into the frame buffer falls in those cycles: in every run of this plan's checks the frame logs are byte-identical. `PINHECK_DMD_PROOF`'s row model reads hub RAM the same way; its counts may differ from Plan 11c's in such cycles, and the display check's bound (single rows in under 1% of subframes) stays.
6. **Candidate 3 is taken as uncertain port bits.** The PIC32 cannot run past a read whose value it needs, and exactness forbids guessing `P24` (any cog could drive it). But the cabinet routine's `digitalRead(45)` loads all of `PORTF` into `$v1`, keeps `$v1 & mask(RF0)`, and reloads `$v1` ten instructions later. With the worker thread, `pinheck_port_read` returns `RF13` from the last value settled and marks it uncertain (`pic32mx_uncertain` → `mips32_uncertain`); the worker reads the true `P24` in queue order (`CMD_SAMPLE`: `do_catch_up` and `p8x32a_pins`, the catch-up and read `prop_p24` made, so the Propeller's cut points do not change); the MIPS32 core runs the slow step while a register holds an uncertain bit, settles it (waiting for that sample if needed) before any instruction that reads the register, except an `AND`/`ANDI` whose other operand clears the bit, and drops it when an instruction writes the register without reading it. Exceptions are taken with the register uncertain; the handler's instructions read it like any other (the fetch window is reset at an exception, so the first one settles). A differential test (`uncertain_test.c`: 20,000 random programs with device loads of every size, `AND`/`ANDI`, `RDPGPR`, stores, branches, interrupts into shadow set 1, the true bits known at once or up to 40 instructions later) finds no difference from runs that read the true value, and four mutations of the careful step are each caught. Link reads (`digitalRead(14)`, the bit is used) settle at the next instruction, as they waited before. With the worker off (one CPU, the governor, `PINHECK_THREADS=0`) or `PINHECK_RF13=0`, `prop_p24` reads exactly as before. After it the PIC32 waits about 460 times a second in attract mode (settles) and 120 times (`prop_sync`), from 197,000.
7. **Measured and rejected:** advancing the Propeller at each PIC32 pin write (the queued edges are known up to there): 0.545× against 0.556× (with the scan cog absent, 0.990× against 1.041×): the pin writes come just before the read, so the worker gains no overlap and pays more catch-ups. Candidate 1 for The Jetsons' frame sender: its cog writes hub RAM (its handshake flags) and drives its pins with counters, so it cannot be lazy; making its counter and `OUTA` writes local while keeping it in the schedule ("private pins") is estimated at 5–6% for The Jetsons and is left (what remains).
8. **Plan 11c's review minors.** (a) `hex_bytes` is cleared at machine stop: a second session of another game in one libpinmame process logged a false `hex:` line (`pinmame_vpx.sh` runs `amh` then `dominos` in one host process). (b) The decoder's latch: after the rotating path the register is stored in shifting order and the bit count reset, so each further latch copies (America's Most Haunted's 127-clock second row made every latch rotate). (c) On-board LED 2 on 62–64 only where the game data says so (`onbLed2`, 1 for America's Most Haunted, 0 for every other game): no other game's board has it, and 62–64 belong to the external chain's LED 0 there; no shipped firmware of the other games writes a third on-board LED, so no check fails first. (d) The raw combiner (`CORE_DMD_PWM_COMBINER_SUM_16`) is checked through libpinmame's `PINMAME_DMD_MODE_RAW` (`host -D`, `vpx.py dmd DIR raw`: each raw frame is the sum of the last 16 subframes at a vblank, held to 15); the comment on the DMD ring says "after the queued Propeller work has run".
9. **Speed is reported, not promised.** The brief's aim is 1.2× for every workload; the Result gives each game's three workloads with the load, and "what remains" names the next levers where a workload stays under 1.0×.

## Review Focus

- **A lazy cog observed while late.** Any path that reads pins, waits on pins or touches the lazy cog's state without catching it up (`ina` with a full mask, `p8x32a_pins`, `wait_pins`, `loop_post`, `sys`, `special_write` of another cog's `DIRA`/counters, `p8x32a_reset`); `chip/lazy.spin` and the random tests cover the ones a program can reach; review the rest by reading.
- **The lazy path's order and timing:** held changes (`lz_ht`) delivered when the catch-up reaches them, the change at entry that is not yet in effect, the exit turning held changes into pending points, prop.c substituting the lazy pins on the normal path so a device never sees them twice or out of order.
- **Translated lazy blocks:** the time-limit check before every slot, the hub read's slot arithmetic (`next_slot`), `T2` after a hub read, the `OUTA` list and the separate tail; blocks dropped on entry and exit (`jit_drop`) so an eager cog never runs a block that writes `OUTA` locally.
- **The careful MIPS32 step:** which instructions read and write which registers (`gpr_use`), the safe `AND`/`ANDI`, shadow sets (`RDPGPR`/`WRPGPR`, `MTC0`, `ERET` settle), loads of every size, `LWL`/`LWR` (settled at the load), host access to the registers (`pic32cpu_get_reg`/`set_reg` settle).
- **The sample ring:** `prop_sample_get` after the governor stops the worker, a token older than `PROP_SAMPS` (a register stays uncertain at most 256 instructions, so it cannot be).

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/p8x32a/p8x32a.[ch]` | lazy cogs: entry (`lazy_try`, `lazy_code`), catch-up (`lazy_run`), exit, the lazy path (`bus.lazy`, `bus.lazy_pins`), mask-aware `INA` (modified) |
| `src/cpu/p8x32a/p8x32ajit.[ch]`, `tests/pinheck/p8x32a/jit_oom_test.cpp` | translated lazy blocks: `OUTA` writes, hub reads, time limit, their own tail (modified) |
| `tests/pinheck/p8x32a/{run.c,check.sh,chip/lazy.spin}` | the lazy path in the RTL comparison; `EXPECT-LAZY`; the lazy-cog chip test |
| `src/cpu/mips32/mips32.[ch]` | uncertain registers and the careful step (modified) |
| `src/cpu/pic32mx/pic32mx.[ch]`, `pic32mxcpu.c` | uncertain port bits to the core, their settling (modified) |
| `tests/pinheck/mips32/{uncertain_test.c,check.sh}` | the differential test of uncertain loads |
| `tests/pinheck/{mips32/unit_test.c,mips32/run.c,pic32mx/boot.c,pic32mx/soc_test.c,link/machine.c}` | the new bus and board callbacks in their initialisers (modified) |
| `src/wpc/pinheck/prop.[ch]` | the lazy path to the pins device (`prop_set_pins_lazy`), `CMD_SAMPLE` and its ring (modified) |
| `src/wpc/pinheck.[ch]`, `src/wpc/pinheck/dmd.c`, `src/wpc/sims/pinheck/{amh,jetsons,rzspook}.c` | the scan cog's lazy path, uncertain `RF13`, review minors (modified) |
| `tests/pinheck/vpx/{host.cpp,vpx.py,pinmame_vpx.sh}` | raw DMD frames, two games in one process (modified) |
| `tests/pinheck/perf/bench.sh` | a raw DMD's subframes in the determinism check; `BENCH_PERF_EV` (modified) |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | what Plan 12 established; its roadmap row (modified) |

---
### Task 1: Reference build; a raw DMD's subframes in the determinism check

**Files:**
- Modify: `tests/pinheck/perf/bench.sh`

**Interfaces:**
- Consumes: Plan 9's `bench.sh` (`SDL3PINMAME`, `REFERENCE`, `bench.sh profile`), Plan 11c's `PINHECK_DMD_LOG` (per vblank: a `uint32` count, then that many 512-byte subframes).
- Produces: `build/reference/sdl3pinmame` (`pinheck` at `8fa0a5e5`), which every later task compares with; `bench.sh` compares `dmd.bin` (`PINHECK_DMD_LOG`) where either build wrote one; `BENCH_PERF_EV` sets `bench.sh profile`'s `perf record` event options (default `-F 999`; `-e instructions:u -c 200000` samples by instructions).

- [ ] **Step 1: Build the reference**

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12.log 2>&1; tail -1 build/sdl3pinmame/12.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```
Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: `bench.sh` compares a raw DMD's subframes**

The cache key of the reference's runs gains `dmd.bin`, so runs cached before this change are made again.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/perf/bench.sh', [
    (r'''#   bench.sh [attract] [video]     time each workload with $SDL3PINMAME; with $REFERENCE set, run it too
#                                  and require byte-identical UART1, frame, sound and NVRAM output; each timing
#                                  also gives the slowest 100 ms of host time (PINHECK_TIME_LOG)
#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py
#   bench.sh contention [CASE...]  10 s of attract mode where CPUs are scarce (Linux, taskset; BENCH_CPU_A and BENCH_CPU_B,
''',
     r'''#   bench.sh [attract] [video]     time each workload with $SDL3PINMAME; with $REFERENCE set, run it too
#                                  and require byte-identical UART1, frame (a raw DMD's subframes too), sound and
#                                  NVRAM output; each timing
#                                  also gives the slowest 100 ms of host time (PINHECK_TIME_LOG)
#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py (BENCH_PERF_EV: perf
#                                  record's event options, default -F 999)
#   bench.sh contention [CASE...]  10 s of attract mode where CPUs are scarce (Linux, taskset; BENCH_CPU_A and BENCH_CPU_B,
'''),
    (r'''	# the reference's runs are kept while its machine and the workload are the same
	key="$FRAMES $MARK $SEND_AT $SEND $KEYS"
	if [ $label = ref ] && [ -s $D/bench.txt ] && [ "$(cat $D/spec.txt 2> /dev/null)" = "$key" ]; then cat $D/bench.txt; return; fi
''',
     r'''	# the reference's runs are kept while its machine and the workload are the same
	key="$FRAMES $MARK $SEND_AT $SEND $KEYS dmd.bin"
	if [ $label = ref ] && [ -s $D/bench.txt ] && [ "$(cat $D/spec.txt 2> /dev/null)" = "$key" ]; then cat $D/bench.txt; return; fi
'''),
    (r'''	[ -n "$PERF" ] && wrap="$PERF stat -e task-clock,instructions:u,cycles:u -o $D/perf.txt"
	[ -n "$PERF" ] && [ "$4" = record ] && wrap="$PERF record -F 999 -o $D/perf.data --"
	{ echo "$MARK mark window"; [ -z "$KEYS" ] || echo "$KEYS"; } > $D/keys.txt
''',
     r'''	[ -n "$PERF" ] && wrap="$PERF stat -e task-clock,instructions:u,cycles:u -o $D/perf.txt"
	[ -n "$PERF" ] && [ "$4" = record ] && wrap="$PERF record ${BENCH_PERF_EV:--F 999} -o $D/perf.data --"
	{ echo "$MARK mark window"; [ -z "$KEYS" ] || echo "$KEYS"; } > $D/keys.txt
'''),
    (r'''	(cd $D && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 PINHECK_UART1_LOG=$D/uart.log PINHECK_PROP_LOG=$D/prop.log \
		PINHECK_FRAME_LOG=$D/frames.bin PINHECK_TIME_LOG=$D/time.log PINHECK_WAV=$D/snd.wav PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND_GAP=1 PINHECK_UART1_SEND="$SEND" \
		timeout -k 30 5400 $wrap "$bin" $GAME -rompath $B/$ml/roms -nvram_directory nvram -cfg_directory $B/$ml/cfg -headless \
''',
     r'''	(cd $D && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 PINHECK_UART1_LOG=$D/uart.log PINHECK_PROP_LOG=$D/prop.log \
		PINHECK_FRAME_LOG=$D/frames.bin PINHECK_DMD_LOG=$D/dmd.bin PINHECK_TIME_LOG=$D/time.log PINHECK_WAV=$D/snd.wav PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND_GAP=1 PINHECK_UART1_SEND="$SEND" \
		timeout -k 30 5400 $wrap "$bin" $GAME -rompath $B/$ml/roms -nvram_directory nvram -cfg_directory $B/$ml/cfg -headless \
'''),
    (r'''	same=
	for f in uart.log frames.bin snd.wav nvram/$GAME.nv; do
		if cmp -s $B/opt/$w/$f $B/ref/$w/$f; then same="$same $f"; else echo "DETERMINISM FAIL $w: $f differs from the reference build's"; fail=1; fi
''',
     r'''	same=
	dmd=
	[ -e $B/opt/$w/dmd.bin ] || [ -e $B/ref/$w/dmd.bin ] && dmd=dmd.bin
	for f in uart.log frames.bin $dmd snd.wav nvram/$GAME.nv; do
		if cmp -s $B/opt/$w/$f $B/ref/$w/$f; then same="$same $f"; else echo "DETERMINISM FAIL $w: $f differs from the reference build's"; fail=1; fi
'''),
])
EOF
```

- [ ] **Step 3: The reference against itself**

```sh
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/reference/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short
```
Expected (about 2 minutes; the speeds vary with the load, the instruction counts on one thread do not):
```text
determinism first boot: identical nvram.base/amh.nv
bench short opt: 0.561x over 10.0 s emulated (17.8 s wall), whole run 20.2 G instructions and 1.5 s CPU per emulated s, worst 100 ms 0.492x, load 1.87
bench short ref: 0.549x over 10.0 s emulated (18.2 s wall), whole run 20.2 G instructions and 1.5 s CPU per emulated s, worst 100 ms 0.384x, load 1.52
determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/perf/bench.sh
git commit -m "pinheck bench: a raw DMD's subframes in the determinism check; BENCH_PERF_EV"
```

### Task 2: Plan 11c's review minors

**Files:**
- Modify: `tests/pinheck/vpx/host.cpp`, `tests/pinheck/vpx/vpx.py`, `tests/pinheck/vpx/pinmame_vpx.sh` (the tests)
- Modify: `src/wpc/pinheck.c`, `src/wpc/pinheck.h`, `src/wpc/sims/pinheck/{amh,jetsons,rzspook}.c`, `src/wpc/pinheck/dmd.c`

**Interfaces:**
- Consumes: Plan 11c's libpinmame host (`host.cpp`), `vpx.py dmd`, `pinmame_vpx.sh` with `PINHECK_GAME=amh`.
- Produces: `host -n GAME2` (with `-R`, the second session runs `GAME2`), `host -D` (`PINMAME_DMD_MODE_RAW`), `vpx.py dmd DIR raw`; game data field `onbLed2` (last field of `pinheck_tGameData`; `PINHECK_DOMINOS_DATA` ends `…, 3000, 0`); `hex_bytes` cleared at machine stop.

- [ ] **Step 1: The tests**

`pinmame_vpx.sh` for America's Most Haunted gains two launches: raw frames (`-D`), and a second session of Domino's in the same process (`-R -n dominos`, with `DOMINOS_ZIP`), whose prop log must hold America's Most Haunted's `hex:` line once:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/vpx/host.cpp', [
    (r'''   message API VPX standalone uses, and logs what a host receives.
   host [-p] [-o] [-x] [-P] [-R] [-m mech] [-s switches] GAME FRAMES DIR
     -p  plugin message API (a minimal MsgPluginAPI host); -o sample lamps 1-98 and solenoids 1-64 each frame;
''',
     r'''   message API VPX standalone uses, and logs what a host receives.
   host [-p] [-o] [-x] [-P] [-R] [-n GAME2] [-D] [-m mech] [-s switches] GAME FRAMES DIR
     -p  plugin message API (a minimal MsgPluginAPI host); -o sample lamps 1-98 and solenoids 1-64 each frame;
'''),
    (r'''     -s  file of "frame switch state" lines applied with PinmameSetSwitch; -P  physical outputs (SolMask(2) = 2);
     -R  stop after FRAMES, copy $PINHECK_LINK_LOG to DIR/link1.log, run FRAMES more in a new session
   host -T: messages broadcast from a second thread while this one subscribes (for a ThreadSanitizer build)
''',
     r'''     -s  file of "frame switch state" lines applied with PinmameSetSwitch; -P  physical outputs (SolMask(2) = 2);
     -R  stop after FRAMES, copy $PINHECK_LINK_LOG to DIR/link1.log, run FRAMES more in a new session (of GAME2 with -n)
     -D  DMD frames raw (PINMAME_DMD_MODE_RAW)
   host -T: messages broadcast from a second thread while this one subscribes (for a ThreadSanitizer build)
'''),
    (r'''	if (argc == 2 && !strcmp(argv[1], "-T")) return thread_check();
	bool plugin = false, restart = false;
	int mech = 0, a = 1;
''',
     r'''	if (argc == 2 && !strcmp(argv[1], "-T")) return thread_check();
	bool plugin = false, restart = false, raw = false;
	const char *game2 = nullptr;
	int mech = 0, a = 1;
'''),
    (r'''		else if (!strcmp(argv[a], "-R")) restart = true;
		else if (!strcmp(argv[a], "-m") && a + 1 < argc) mech = atoi(argv[++a]);
''',
     r'''		else if (!strcmp(argv[a], "-R")) restart = true;
		else if (!strcmp(argv[a], "-D")) raw = true;
		else if (!strcmp(argv[a], "-n") && a + 1 < argc) game2 = argv[++a];
		else if (!strcmp(argv[a], "-m") && a + 1 < argc) mech = atoi(argv[++a]);
'''),
    (r'''	if (argc - a != 3) {
		fprintf(stderr, "usage: host [-p] [-o] [-x] [-P] [-R] [-m mech] [-s switches] GAME FRAMES DIR\n");
		return 2;
''',
     r'''	if (argc - a != 3) {
		fprintf(stderr, "usage: host [-p] [-o] [-x] [-P] [-R] [-n GAME2] [-D] [-m mech] [-s switches] GAME FRAMES DIR\n");
		return 2;
'''),
    (r'''	if (physout) PinmameSetSolenoidMask(2, 2);
	if (plugin) {
''',
     r'''	if (physout) PinmameSetSolenoidMask(2, 2);
	if (raw) PinmameSetDmdMode(PINMAME_DMD_MODE_RAW);
	if (plugin) {
'''),
    (r'''		}
		if (PinmameRun(game) != PINMAME_STATUS_OK) { logl("run failed\n"); return 1; }
		while (!done) {
''',
     r'''		}
		if (PinmameRun(session && game2 ? game2 : game) != PINMAME_STATUS_OK) { logl("run failed\n"); return 1; }
		while (!done) {
'''),
])
edit('tests/pinheck/vpx/vpx.py', [
    (r'''
def dmd(d):
    """America's Most Haunted's raw DMD as a libpinmame host gets it: one 128x32 DMD of 16 shades (depth 4). The
    driver hands the core each vblank's subframes (PINHECK_DMD_LOG); the core shows the sum of the last 16, so each
    frame of the callback path (bytes, 255 * sum / 16) and of the plugin path (floats, sum / 16) must equal that sum
    at a vblank, the frames in the order of their vblanks (libpinmame passes a frame on when it changed)"""
    fails = []
''',
     r'''
def dmd(d, rawmode=False):
    """America's Most Haunted's raw DMD as a libpinmame host gets it: one 128x32 DMD of 16 shades (depth 4). The
    driver hands the core each vblank's subframes (PINHECK_DMD_LOG); the core shows the sum of the last 16, so each
    frame of the callback path (bytes, 255 * sum / 16; raw: the sum) and of the plugin path (floats, sum / 16) must
    equal that sum at a vblank, the frames in the order of their vblanks (libpinmame passes a frame on when it changed)"""
    fails = []
'''),
    (r'''        i += 4 + n
    lum8 = bytes(min(255, int(255.0 * v / 16.0)) for v in range(256))   # sums 0-16
    for plugin in (False, True):
''',
     r'''        i += 4 + n
    # sums 0-16; raw (PINMAME_DMD_MODE_RAW): the core's raw frame, the sum held to 15
    lum8 = bytes(min(15, v) for v in range(256)) if rawmode else bytes(min(255, int(255.0 * v / 16.0)) for v in range(256))
    for plugin in (False, True):
'''),
    (r'''        sys.exit(dmd(a[1]))
    if a[:1] == ['mech'] and len(a) == 3:
''',
     r'''        sys.exit(dmd(a[1]))
    if a[:1] == ['dmd'] and len(a) == 3 and a[2] == 'raw':
        sys.exit(dmd(a[1], True))
    if a[:1] == ['mech'] and len(a) == 3:
'''),
])
edit('tests/pinheck/vpx/pinmame_vpx.sh', [
    (r'''	rm -f $B/$name/*.log $B/$name/*.bin $B/$name/*.raw $B/$name/*.wav
	(cd $B/$name && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out.log PINHECK_FRAME_LOG=$PWD/frames.log PINHECK_WAV=$PWD/capture.wav \
		PINHECK_DMD_LOG=$PWD/dmd.log PINHECK_UART1_LOG=$PWD/uart.log timeout -k 30 3000 ../host "$@" > run.out 2>&1) || { echo "VPX FAIL: $name exited $?"; tail -5 $B/$name/run.out; exit 1; }
''',
     r'''	rm -f $B/$name/*.log $B/$name/*.bin $B/$name/*.raw $B/$name/*.wav
	(cd $B/$name && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out.log PINHECK_FRAME_LOG=$PWD/frames.log PINHECK_WAV=$PWD/capture.wav PINHECK_PROP_LOG=$PWD/prop.log \
		PINHECK_DMD_LOG=$PWD/dmd.log PINHECK_UART1_LOG=$PWD/uart.log timeout -k 30 3000 ../host "$@" > run.out 2>&1) || { echo "VPX FAIL: $name exited $?"; tail -5 $B/$name/run.out; exit 1; }
'''),
    (r'''	python3 vpx.py dmd $B/media || fail=1
	[ $fail -eq 0 ] || exit 1
''',
     r'''	python3 vpx.py dmd $B/media || fail=1
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
'''),
])
EOF
```

- [ ] **Step 2: Run them to see the two-game case fail**

libpinmame as Plan 11c built it:

```sh
mkdir -p build/libpinmame-src && ln -sfn ../../cmake build/libpinmame-src/cmake && ln -sfn ../../ext build/libpinmame-src/ext && ln -sfn ../../src build/libpinmame-src/src
cp cmake/libpinmame/CMakeLists.txt build/libpinmame-src/CMakeLists.txt
cmake -S build/libpinmame-src -B build/libpinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DBUILD_STATIC=OFF > build/libpinmame-cfg.log 2>&1
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
amh env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | grep -E 'raw|hex|sessions|vpx'
```
Expected (about 15 minutes; the build, then the one failure):
```text
[100%] Built target pinmame_test
VPX FAIL: 2 hex lines over amh and dominos
```
The raw path's line (`dmd: callback path: … frames … equal the sum of the last 16 subframes at a vblank, in order`) passes already: the combiner exists, it was not checked.

- [ ] **Step 3: The fixes**

`hex_bytes` per session; the decoder's latch stores the rotated register; on-board LED 2 by game data; the DMD ring's comment:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
    (r'''#define PINHECK_EXT_LEDS 1  /* the firmware drives one external LED */
#define PINHECK_ONB_LEDS 3  /* on-board LEDs 0 and 1; a third (America's Most Haunted's ghost) on 62-64 */
#define PINHECK_NSOLS   64
''',
     r'''#define PINHECK_EXT_LEDS 1  /* the firmware drives one external LED */
#define PINHECK_ONB_LEDS 3  /* on-board LEDs 0 and 1; a third (game data onbLed2: America's Most Haunted's ghost) on 62-64 */
#define PINHECK_NSOLS   64
'''),
    (r'''	if (brd_log) fprintf(brd_log, "%.9f R %d %d %02x%02x%02x %llu\n", timer_get_time(), chain, led, r, g, b, (unsigned long long)t);
	if ((chain == BOARD_RGB_ONBOARD && led < PINHECK_ONB_LEDS) || (chain == BOARD_RGB_EXTERNAL && led < PINHECK_EXT_LEDS)) {
		int idx = chain == BOARD_RGB_ONBOARD && led < 2 ? PINHECK_SOL_RGB + 3 * led : PINHECK_SOL_EXT + 3 * (chain == BOARD_RGB_ONBOARD ? led - 2 : led);
''',
     r'''	if (brd_log) fprintf(brd_log, "%.9f R %d %d %02x%02x%02x %llu\n", timer_get_time(), chain, led, r, g, b, (unsigned long long)t);
	if ((chain == BOARD_RGB_ONBOARD && led < (pinheck_game()->onbLed2 ? PINHECK_ONB_LEDS : 2)) || (chain == BOARD_RGB_EXTERNAL && led < PINHECK_EXT_LEDS)) {
		int idx = chain == BOARD_RGB_ONBOARD && led < 2 ? PINHECK_SOL_RGB + 3 * led : PINHECK_SOL_EXT + 3 * (chain == BOARD_RGB_ONBOARD ? led - 2 : led);
'''),
    (r'''/* The raw DMD (game data dmdHub): the Propeller thread decodes subframes from the scan pins (P16-P20) into a ring,
   which the emulation thread hands to the core's PWM integration at each vblank, after the Propeller has caught up.
   Test hook PINHECK_DMD_PROOF also runs the row model (P18-P20 only, a row's dots from hub RAM at its latch) and
''',
     r'''/* The raw DMD (game data dmdHub): the Propeller thread decodes subframes from the scan pins (P16-P20) into a ring,
   which the emulation thread hands to the core's PWM integration at each vblank, after the queued Propeller work has run.
   Test hook PINHECK_DMD_PROOF also runs the row model (P18-P20 only, a row's dots from hub RAM at its latch) and
'''),
    (r'''	locals.uart1 = locals.proplog = NULL;
	if (locals.have_vol) vfat_free(&vol);
''',
     r'''	locals.uart1 = locals.proplog = NULL;
	hex_bytes = 0; /* the next session's game may have no HEX */
	if (locals.have_vol) vfat_free(&vol);
'''),
])
edit('src/wpc/pinheck.h', [
    (r'''  int bootHold;           /* ms the bootloader stand-in holds the PIC32 after a reset without a sign-on */
} pinheck_tGameData;
''',
     r'''  int bootHold;           /* ms the bootloader stand-in holds the PIC32 after a reset without a sign-on */
  int onbLed2;            /* 1: a third on-board WS2801 LED, on outputs 62-64 (the external chain's LED 0 then has none) */
} pinheck_tGameData;
'''),
    (r'''/* Domino's values: 128 x 32, POSITION 340, servo levels 1.0-2.0 ms, WS2801 as sent, PINHECK_INSERVICE applies,
   the display link, a 3 s boot hold. Only Domino's and the system set use them; every other game spells out its own */
#define PINHECK_DOMINOS_DATA 128, 32, 340, 1000, 2000, 0, 1, 0, 3000

''',
     r'''/* Domino's values: 128 x 32, POSITION 340, servo levels 1.0-2.0 ms, WS2801 as sent, PINHECK_INSERVICE applies,
   the display link, a 3 s boot hold, two on-board LEDs. Only Domino's and the system set use them; every other game
   spells out its own */
#define PINHECK_DOMINOS_DATA 128, 32, 340, 1000, 2000, 0, 1, 0, 3000, 0

'''),
])
edit('src/wpc/sims/pinheck/amh.c', [
    (r'''    &amhSimData },
  128, 32, 0, 544, 2400, 0, 0, 0x5B0C, 5000
};
''',
     r'''    &amhSimData },
  128, 32, 0, 544, 2400, 0, 0, 0x5B0C, 5000, 1
};
'''),
])
edit('src/wpc/sims/pinheck/jetsons.c', [
    (r'''    &jetsonsSimData },
  128, 64, 55, 544, 2400, 0, 1, 0, 3000
};
''',
     r'''    &jetsonsSimData },
  128, 64, 55, 544, 2400, 0, 1, 0, 3000, 0
};
'''),
])
edit('src/wpc/sims/pinheck/rzspook.c', [
    (r'''    &rzspookSimData },
  128, 32, 460, 544, 2400, 0, 1, 0, 3000
};
''',
     r'''    &rzspookSimData },
  128, 32, 460, 544, 2400, 0, 1, 0, 3000, 0
};
'''),
])
edit('src/wpc/pinheck/dmd.c', [
    (r'''			}
		}
''',
     r'''			}
			/* the register in shifting order from bit 0: the next latches take the copy */
			memcpy(d->shift, d->latched, DMD_ROW);
			d->nbits = 0;
		}
'''),
])
EOF
```

- [ ] **Step 4: The checks pass**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12.log 2>&1; tail -1 build/sdl3pinmame/12.log
amh env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | grep -E 'callback|sessions|FAIL|vpx:'
nice -n 15 tests/pinheck/display/check.sh | tail -1
tests/pinheck/vpx/check.sh | tail -1
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh | tail -1
```
Expected (about 15 minutes):
```text
[100%] Built target pinmame_test
[100%] Built target sdl3pinmame
dmd: callback path: 1200 of 1200 frames (1200 not blank) equal the sum of the last 16 subframes at a vblank, in order
dmd: callback path: 1200 of 1200 frames (1200 not blank) equal the sum of the last 16 subframes at a vblank, in order
dmd: callback path: 600 of 600 frames (600 not blank) equal the sum of the last 16 subframes at a vblank, in order
sessions: the Intel HEX logged once over amh and dominos
pinmame vpx: ok (amh: the raw DMD)
display: 0 failed
vpx: 0 failed
pinmame board: ok
```
(About 15 minutes; the frame counts are the replay's.)

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck.c src/wpc/pinheck.h src/wpc/sims/pinheck/amh.c src/wpc/sims/pinheck/jetsons.c src/wpc/sims/pinheck/rzspook.c src/wpc/pinheck/dmd.c tests/pinheck/vpx/host.cpp tests/pinheck/vpx/vpx.py tests/pinheck/vpx/pinmame_vpx.sh
git commit -m "pinheck: Plan 11c's review minors (hex_bytes per session, the latch's register, on-board LED 2 by game data, raw DMD frames checked)"
```

### Task 3: Lazy cogs

**Files:**
- Create: `tests/pinheck/p8x32a/chip/lazy.spin`
- Modify: `tests/pinheck/p8x32a/run.c`, `tests/pinheck/p8x32a/check.sh` (the tests)
- Modify: `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`, `src/cpu/p8x32a/p8x32ajit.h`, `src/cpu/p8x32a/p8x32ajit.cpp`, `tests/pinheck/p8x32a/jit_oom_test.cpp`, `src/wpc/pinheck/prop.h`, `src/wpc/pinheck/prop.c`, `src/wpc/pinheck.c`

**Interfaces:**
- Consumes: the core's scheduler (`p8x32a_run_until`, `run_local`, `event_run`, `hub_rw`, `do_hub`, `special_write`, `flush`), Plan 9b's translator (`p8x32a_jit_fn`), prop.c's devices.
- Produces: in `p8x32a_bus` (after `pure_in`): `void (*lazy)(void *ctx, uint64_t t, uint32_t mask, uint32_t out, uint32_t dir)` (from `t` a lazy cog has pins `mask`, 0: none) and `void (*lazy_pins)(void *ctx, uint64_t t, uint32_t out, uint32_t dir)` (a change of only those pins, in time order); in `p8x32a`: `uint32_t lazy_ok` (pins a lazy cog may drive; 0, the default, turns lazy cogs off) and `uint64_t lazies` (entries); `p8x32a_jit_fn` and `p8x32a_jit_build` gain a last argument `int outa`; `p8x32a_jblk.part`; `p8x32a_jst` gains `on`, `ot`, `ov`, `tl`, `slot`, `latch`, `hub`. prop.c: `void prop_set_pins_lazy(pinheck_prop *p, prop_pins_fn fn, void *ctx, uint32_t mask)`. pinheck.c: the scan cog's pins go to `pinheck_dmd_lazy_cb`; `PINHECK_LAZY=0` turns it off. run.c: `-nolazy`, `-lazies` (prints `p8run: N lazy cogs`); check.sh: `' EXPECT-LAZY: N` and a failure on any `p8x32a: lazy …` log line.

- [ ] **Step 1: The RTL test and the trace with a lazy path**

`run.c` allows lazy cogs on every pin no device of its own watches and merges the core's two pin streams (`N`: the core's pins outside a lazy cog's, `L`: the lazy cog's, `M`: from when it has which) into the `P` lines the RTL writes, one per cycle that changes them. `chip/lazy.spin`: cog 1 scans a 16-byte hub buffer (two bits a byte on P16 with a clock on P17, P18 toggling every frame, P19 toggling where `CNT` bit 4 is 0); cog 0 rewrites the buffer, reads `INA` whole, masked (`test` with a mask on and off the scan pins) and eight times 777 cycles apart, waits for a rising edge of P18 (the cog leaves lazy mode), later drives P19 high (again), and stops cog 1 (again): three entries.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/p8x32a/run.c', [
    (r'''static uint8_t eemem[0x20000];
static int have_ee, have_sd, notrace, sleeps;
static sd_card sd;
''',
     r'''static uint8_t eemem[0x20000];
static int have_ee, have_sd, notrace, sleeps, nolazy, lazies;
static sd_card sd;
'''),
    (r'''
static int rank(const struct ev *e) { return e->line[0] == 'P' ? 0 : e->line[0] == 'K' ? 1 : 2; }

''',
     r'''
/* N, L, M: the core's pin changes outside a lazy cog's pins, that cog's pin changes, its pins from then on; merged into P */
static int rank(const struct ev *e) { return strchr("PNLM", e->line[0]) ? 0 : e->line[0] == 'K' ? 1 : 2; }

'''),
    (r'''		char b[48];
		sprintf(b, "P %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
		emit(t, b);
	}
}
''',
     r'''		char b[48];
		sprintf(b, "N %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
		emit(t, b);
	}
}

static void lazy(void *ctx, uint64_t t, uint32_t mask, uint32_t out, uint32_t dir)
{
	char b[64];
	(void)ctx;
	if (notrace) return;
	sprintf(b, "M %llu %08x %08x %08x", (unsigned long long)t, (unsigned)mask, (unsigned)out, (unsigned)dir);
	emit(t, b);
}

static void lazy_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	char b[48];
	(void)ctx;
	if (notrace) return;
	sprintf(b, "L %llu %08x %08x", (unsigned long long)t, (unsigned)out, (unsigned)dir);
	emit(t, b);
}
'''),
    (r'''{
	p8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg, ctr_state, ~0x30000001u };
	const char *rom = NULL, *ram = NULL, *eep = NULL, *dump = NULL;
''',
     r'''{
	p8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg, ctr_state, ~0x30000001u, lazy, lazy_pins };
	const char *rom = NULL, *ram = NULL, *eep = NULL, *dump = NULL;
'''),
    (r'''		else if (!strcmp(argv[i], "-notrace")) notrace = 1;
		else if (!strcmp(argv[i], "-sleeps")) sleeps = 1;
		else if (!strcmp(argv[i], "-ctrlog") && i + 1 < argc) { if (!(ctrlog = fopen(argv[++i], "w"))) { perror(argv[i]); return 2; } }
''',
     r'''		else if (!strcmp(argv[i], "-notrace")) notrace = 1;
		else if (!strcmp(argv[i], "-nolazy")) nolazy = 1;
		else if (!strcmp(argv[i], "-sleeps")) sleeps = 1;
		else if (!strcmp(argv[i], "-lazies")) lazies = 1;
		else if (!strcmp(argv[i], "-ctrlog") && i + 1 < argc) { if (!(ctrlog = fopen(argv[++i], "w"))) { perror(argv[i]); return 2; } }
'''),
    (r'''	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "p8run: rom must be 32768 bytes\n"); return 2; }
	if (ram && !load(ram, chip.hub, 0x8000, &n)) return 2;
''',
     r'''	if (!load(rom, chip.hub + 0x8000, 0x8000, &n) || n != 0x8000) { fprintf(stderr, "p8run: rom must be 32768 bytes\n"); return 2; }
	/* a cog may run lazily on any pin no device here watches */
	chip.lazy_ok = nolazy ? 0 : ~((have_ee ? 0x30000000u : 0) | (have_sd ? 0xFu : 0));
	if (ram && !load(ram, chip.hub, 0x8000, &n)) return 2;
'''),
    (r'''		size_t k;
		qsort(evs, nev, sizeof(*evs), evcmp);
		for (k = 0; k < nev; k++)
			if (evs[k].t < end) printf("%s\n", evs[k].line);
	}
''',
     r'''		size_t k;
		uint32_t no = 0, nd = 0, lm = 0, lo = 0, ld = 0, po = 0, pd = 0;
		qsort(evs, nev, sizeof(*evs), evcmp);
		for (k = 0; k < nev; k++) {
			const char *l = evs[k].line;
			unsigned long long t;
			unsigned a, b, c;
			if (evs[k].t >= end) continue;
			if (!strchr("NLM", l[0])) { printf("%s\n", l); continue; }
			if (l[0] == 'N') { sscanf(l + 2, "%llu %x %x", &t, &a, &b); no = a; nd = b; }
			else if (l[0] == 'L') { sscanf(l + 2, "%llu %x %x", &t, &a, &b); lo = a; ld = b; }
			else {
				/* a lazy cog's pins go back to the core's trace as they last were */
				sscanf(l + 2, "%llu %x %x %x", &t, &a, &b, &c);
				no = (no & ~lm) | lo;
				nd = (nd & ~lm) | ld;
				lm = a;
				lo = b;
				ld = c;
			}
			/* one P line for the pins at the end of each cycle that changes them */
			if (k + 1 < nev && evs[k + 1].t == evs[k].t && strchr("NLM", evs[k + 1].line[0])) continue;
			{
				uint32_t o = (no & ~lm) | lo, d = (nd & ~lm) | ld;
				if (o != po || d != pd) printf("P %llu %08x %08x\n", (unsigned long long)evs[k].t, (unsigned)o, (unsigned)d);
				po = o;
				pd = d;
			}
		}
	}
'''),
    (r'''	if (sleeps) fprintf(stderr, "p8run: %llu idle-loop sleeps\n", (unsigned long long)chip.sleeps);
	if (dump) {
''',
     r'''	if (sleeps) fprintf(stderr, "p8run: %llu idle-loop sleeps\n", (unsigned long long)chip.sleeps);
	if (lazies) fprintf(stderr, "p8run: %llu lazy cogs\n", (unsigned long long)chip.lazies);
	if (dump) {
'''),
])
edit('tests/pinheck/p8x32a/check.sh', [
    (r'''	sleeps=$(sed -n "s/^' EXPECT-SLEEPS: //p" "$1")
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 ${sleeps:+-sleeps} -dump "$o.ourhub" > "$o.our" 2> "$o.log"
	if cmp -s "$o.rtl" "$o.our" && cmp -s "$o.rtlhub" "$o.ourhub"; then pass=$((pass + 1))
''',
     r'''	sleeps=$(sed -n "s/^' EXPECT-SLEEPS: //p" "$1")
	lazies=$(sed -n "s/^' EXPECT-LAZY: //p" "$1")
	./$B/p8run -rom "$o.rom" -ram "$o.ram" -halt -cycles 400000 $args $3 ${sleeps:+-sleeps} ${lazies:+-lazies} -dump "$o.ourhub" > "$o.our" 2> "$o.log"
	if cmp -s "$o.rtl" "$o.our" && cmp -s "$o.rtlhub" "$o.ourhub"; then pass=$((pass + 1))
'''),
    (r'''	if [ -n "$sleeps" ] && ! grep -qxF "p8run: $sleeps idle-loop sleeps" "$o.log"; then echo "SLEEPS $1: $(grep -F idle-loop "$o.log"), expected $sleeps"; fail=$((fail + 1)); fi
	# EXPECT-CLKSHIFT: d v = with CLKSET moving queued edges d cycles earlier, the long at $6000 is v
''',
     r'''	if [ -n "$sleeps" ] && ! grep -qxF "p8run: $sleeps idle-loop sleeps" "$o.log"; then echo "SLEEPS $1: $(grep -F idle-loop "$o.log"), expected $sleeps"; fail=$((fail + 1)); fi
	if [ -n "$lazies" ] && ! grep -qxF "p8run: $lazies lazy cogs" "$o.log"; then echo "LAZY $1: $(grep -F 'lazy cogs' "$o.log"), expected $lazies"; fail=$((fail + 1)); fi
	if grep -q "p8x32a: lazy" "$o.log"; then echo "LAZY $1: $(grep "p8x32a: lazy" "$o.log")"; fail=$((fail + 1)); fi
	# EXPECT-CLKSHIFT: d v = with CLKSET moving queued edges d cycles earlier, the long at $6000 is v
'''),
])
EOF
```

```sh
cat > tests/pinheck/p8x32a/chip/lazy.spin <<'EOF'
' EXPECT-LAZY: 3
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0: starts the scan cog (cog 1) on a buffer at $6200, then writes the buffer, reads INA whole and masked,
' reads INA eight times 777 cycles apart, waits for a rising edge of a scan pin, drives one high, and stops the scan cog; results at $6000
entry   mov     x, wk
        shl     x, #2
        or      x, parf
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d40000
        waitcnt t, #0
        wrbyte  b5, buf3
        wrbyte  b5, buf3
        mov     r, ina
        wrlong  r, res0
        mov     t, cnt
        add     t, d3000
        waitcnt t, #0
        test    dmask, ina wc
        muxc    r2, #1
        test    omask, ina wz
        muxz    r2, #2
        wrlong  r2, res1
        mov     k, #8
        mov     a, res7
:rd     mov     t, cnt
        add     t, d777
        waitcnt t, #0
        mov     r, ina
        wrlong  r, a
        add     a, #4
        djnz    k, #:rd
        wrbyte  b9, buf7
        mov     t, cnt
        add     t, d3000
        waitcnt t, #0
        waitpne lbit, lbit
        waitpeq lbit, lbit
        mov     r, cnt
        wrlong  r, res2
        mov     t, cnt
        add     t, d90000
        waitcnt t, #0
        mov     r, ina
        wrlong  r, res3
        or      outa, zbit
        or      dira, zbit
        mov     t, cnt
        add     t, d3000
        waitcnt t, #0
        mov     r, ina
        wrlong  r, res4
        andn    dira, zbit
        andn    outa, zbit
        mov     t, cnt
        add     t, d90000
        waitcnt t, #0
        wrbyte  b9, buf3
        mov     r, ina
        wrlong  r, res5
        cogstop one
        mov     r, ina
        wrlong  r, res6
        cogid   r
        cogstop r
one     long    1
d3000   long    3000
d777    long    777
d40000  long    40000
d90000  long    90000
b5      long    $A5
b9      long    $5A
parf    long    $62000000
buf3    long    $6203
buf7    long    $6207
res0    long    $6000
res1    long    $6004
res2    long    $6008
res3    long    $600C
res4    long    $6010
res5    long    $6014
res6    long    $6018
res7    long    $6020
dmask   long    $00010000
omask   long    $00000F00
lbit    long    $00040000
zbit    long    $00080000
r       long    0
a       long    0
k       long    0
r2      long    0
t       long    0
x       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
' the scan cog: per byte two bits on P16 with a clock on P17, then P18 toggles, and P19 where CNT bit 4 is 0
worker  mov     dira, pins
:frame  mov     p2, par
        mov     n, #16
:byte   rdbyte  v, p2
        test    v, #1 wc
        muxc    outa, dbit
        or      outa, cbit
        andn    outa, cbit
        test    v, #2 wc
        muxc    outa, dbit
        or      outa, cbit
        andn    outa, cbit
        add     p2, #1
        djnz    n, #:byte
        xor     outa, lbit2
        mov     q, cnt
        test    q, #$10 wz
  if_z  xor     outa, zbit2
        jmp     #:frame
pins    long    $000F0000
dbit    long    $00010000
cbit    long    $00020000
lbit2   long    $00040000
zbit2   long    $00080000
p2      long    0
n       long    0
v       long    0
q       long    0
        long    $C0DEE0D0
EOF
```

- [ ] **Step 2: Run it to see it fail**

```sh
nice -n 15 tests/pinheck/p8x32a/check.sh 2>&1 | grep -m3 -E 'error|FAIL'
```
Expected: the build of `p8run` fails, the check exits 2:
```text
run.c:231:116: error: excess elements in struct initializer [-Werror]
run.c:231:122: error: excess elements in struct initializer [-Werror]
run.c:287:13: error: ‘p8x32a’ has no member named ‘lazy_ok’
```

- [ ] **Step 3: Lazy cogs in the core, the translator, prop.c and the driver**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.h', [
    (r'''	uint32_t pure_in; /* input pins that change only at pins_next edges, never inside pins_out; 0 = none */
} p8x32a_bus;
''',
     r'''	uint32_t pure_in; /* input pins that change only at pins_next edges, never inside pins_out; 0 = none */
	void (*lazy)(void *ctx, uint64_t t, uint32_t mask, uint32_t out, uint32_t dir); /* from t a lazy cog has pins mask (0: none) */
	void (*lazy_pins)(void *ctx, uint64_t t, uint32_t out, uint32_t dir);   /* a change of the lazy cog's pins (only those) */
} p8x32a_bus;
'''),
    (r'''	uint32_t inv, inv_old; /* 0, or the slot + 1 and its old word; ~0: more than one */
} p8x32a_jst;

''',
     r'''	uint32_t inv, inv_old; /* 0, or the slot + 1 and its old word; ~0: more than one */
	uint32_t on;           /* OUTA writes of a lazy cog's block: their times and values, in order */
	uint64_t *ot;
	uint32_t *ov;
	uint64_t tl, slot;     /* a lazy cog's blocks: the last time an instruction or hub read may start; its first hub slot */
	uint64_t latch;        /* and the slot of the last hub read they ran, 0 if none */
	const uint8_t *hub;
} p8x32a_jst;
#define P8X32A_JOUT 256 /* the OUTA writes a run of blocks may leave */

'''),
    (r'''	const void *body; /* entry for a block reached from another */
	unsigned len, valid;
	uint32_t words[P8X32A_JMAX], dyn; /* dyn: bit k set, slot k is read at run time */
''',
     r'''	const void *body; /* entry for a block reached from another */
	unsigned len, valid, part; /* part: stops before the slot its budget does not reach (a lazy cog's block) */
	uint32_t words[P8X32A_JMAX], dyn; /* dyn: bit k set, slot k is read at run time */
'''),
    (r'''/* translate the run at cog address a: ix, then the words after it in ram; var[k] holds the bits in which slot k
   has changed (0: fixed, only S/D: read at run time, else not translated); old is the block this one replaces */
typedef p8x32a_jblk *(*p8x32a_jit_fn)(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var);

''',
     r'''/* translate the run at cog address a: ix, then the words after it in ram; var[k] holds the bits in which slot k
   has changed (0: fixed, only S/D: read at run time, else not translated); old is the block this one replaces;
   outa: OUTA writes are translated too, each left in st's ot/ov (a lazy cog) */
typedef p8x32a_jblk *(*p8x32a_jit_fn)(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var, int outa);

'''),
    (r'''	uint8_t sleepers;
	unsigned sched_gen; /* counts changes one cog makes to another cog's next event */
''',
     r'''	uint8_t sleepers;
	uint8_t lz_on, lz, lz_nh;   /* the lazy cog; its pin changes waiting for their time */
	uint32_t lazy_ok;           /* pins a lazy cog may drive (host: only bus.lazy_pins watches them); 0 = none */
	uint32_t lz_pins, lz_out, lz_hout[4];
	p8x32a_reg lz_reg;          /* the lazy cog's OUTA */
	uint64_t lz_at, lz_to, lz_evt, lz_ht[4], lz_try[8]; /* lz_evt: its next event (its ev_t is out of the schedule) */
	uint32_t lz_wait[8];
	uint64_t lazies;            /* lazy cogs entered */
	unsigned sched_gen; /* counts changes one cog makes to another cog's next event */
'''),
    (r'''	uint8_t jcode[8][64];
} p8x32a;
''',
     r'''	uint8_t jcode[8][64];
	uint64_t jot[P8X32A_JOUT];
	uint32_t jov[P8X32A_JOUT];
} p8x32a;
'''),
])
edit('src/cpu/p8x32a/p8x32a.c', [
    (r'''enum { EV_NONE, EV_HUB, EV_EXEC, EV_DONE, EV_WAITPIN, EV_RESTART, EV_SLEEP };
enum { LOOP_SEARCH, LOOP_RECORD, LOOP_SLEEP };
''',
     r'''enum { EV_NONE, EV_HUB, EV_EXEC, EV_DONE, EV_WAITPIN, EV_RESTART, EV_SLEEP };

/* run_local's helpers stay in its loop */
#if defined(__GNUC__)
#define P8_INLINE static __inline__ __attribute__((always_inline))
#define P8_COLD static __attribute__((noinline))
#elif defined(_MSC_VER)
#define P8_INLINE static __forceinline
#define P8_COLD static __declspec(noinline)
#else
#define P8_INLINE static
#define P8_COLD static
#endif
enum { LOOP_SEARCH, LOOP_RECORD, LOOP_SLEEP };
'''),
    (r'''enum {
	LOG_WAITVID = 1, LOG_CTR_MODE = 2, LOG_CTR_OUT = 4, LOG_REBOOT = 8
};
''',
     r'''enum {
	LOG_WAITVID = 1, LOG_CTR_MODE = 2, LOG_CTR_OUT = 4, LOG_REBOOT = 8, LOG_LAZY = 16
};
'''),
    (r'''static void loop_notify(p8x32a *p, uint64_t t, uint32_t pins);
static void jit_write(p8x32a *p, int n, unsigned s, uint32_t old);

''',
     r'''static void loop_notify(p8x32a *p, uint64_t t, uint32_t pins);
static void jit_written(p8x32a *p, int n, unsigned s, uint32_t old);
P8_INLINE void jit_write(p8x32a *p, int n, unsigned s, uint32_t old)
{
	if (p->jit_build && (p->jcode[n][s >> 3] >> (s & 7) & 1)) jit_written(p, n, s, old);
}

P8_COLD void lazy_run(p8x32a *p, uint64_t t);
static void lazy_exit(p8x32a *p);
/* the lazy cog catches up to t */
#define lazy_catch(p, t) do { if ((p)->lz_on && (t) > (p)->lz_at) lazy_run((p), (t)); } while (0)

/* a hub write at h that changes a byte: the lazy cog reads hub RAM as it was before it */
P8_COLD void lazy_write(p8x32a *p, unsigned ha, unsigned sz, uint32_t v, uint64_t h)
{
	unsigned k;
	for (k = 0; k < sz; k++)
		if (p->hub[ha + k] != (uint8_t)(v >> (8 * k))) { lazy_catch(p, h - 1); return; }
}
static void jit_drop(p8x32a *p, int n);


'''),
    (r'''	uint32_t o = 0, d = 0;
	int n;
	for (n = 0; n < 8; n++) {
		p8x32a_cog *c = &p->cog[n];
''',
     r'''	uint32_t o = 0, d = 0;
	int n;
	lazy_catch(p, t);
	if (p->lz_on) o = regval(&p->lz_reg, t) & p->lz_pins;
	for (n = 0; n < 8; n++) {
		p8x32a_cog *c = &p->cog[n];
'''),
    (r'''/* after flush(t) the pins at t are the last flushed state */
static uint32_t ina(p8x32a *p, uint64_t t)
{
	uint32_t ext;
	flush(p, t);
''',
     r'''/* after flush(t) the pins at t are the last flushed state */
/* INA with a lazy cog: its pins from its own register */
P8_COLD uint32_t ina_lazy(p8x32a *p, uint64_t t)
{
	uint32_t ext;
	lazy_catch(p, t);
	flush(p, t);
	ext = p->bus.pins_in ? p->bus.pins_in(p->bus.ctx, t) : 0;
	return (((p->last_dir & p->last_out) | (~p->last_dir & ext)) & ~p->lz_pins) | (regval(&p->lz_reg, t) & p->lz_pins);
}

/* INA for a use that sees only the pins in m (AND/ANDN with D = m, a wait's mask): the lazy cog's pins only if m has them */
static uint32_t ina(p8x32a *p, uint64_t t, uint32_t m)
{
	uint32_t ext;
	if (p->lz_on && (m & p->lz_pins)) return ina_lazy(p, t);
	flush(p, t);
'''),
    (r'''
static void special_write(p8x32a *p, int n, unsigned a, uint32_t v, uint64_t m3)
''',
     r'''
/* the lazy cog's OUTA write taking effect at e: its pins go to bus.lazy_pins once the catch-up reaches e */
static void lazy_outa(p8x32a *p, uint32_t v, uint64_t e)
{
	p8x32a_reg *r = &p->lz_reg;
	uint32_t g = v & p->lz_pins;
	if (e > r->at) r->prev = r->cur;
	r->cur = v;
	r->at = e;
	if (g == (p->lz_nh ? p->lz_hout[p->lz_nh - 1] : p->lz_out)) return;
	if (e > p->lz_to || p->lz_nh) {
		if (p->lz_nh == 4) { log_once(p, LOG_LAZY, "p8x32a: lazy cog's pin changes overflow"); return; }
		p->lz_ht[p->lz_nh] = e;
		p->lz_hout[p->lz_nh++] = g;
		return;
	}
	p->lz_out = g;
	if (p->bus.lazy_pins) p->bus.lazy_pins(p->bus.ctx, e, g, p->lz_pins);
}

/* the waiting changes of the lazy cog's pins up to t */
static void lazy_due(p8x32a *p, uint64_t t)
{
	int k = 0, j;
	while (k < p->lz_nh && p->lz_ht[k] <= t) {
		p->lz_out = p->lz_hout[k];
		if (p->bus.lazy_pins) p->bus.lazy_pins(p->bus.ctx, p->lz_ht[k], p->lz_out, p->lz_pins);
		k++;
	}
	for (j = k; j < p->lz_nh; j++) { p->lz_ht[j - k] = p->lz_ht[j]; p->lz_hout[j - k] = p->lz_hout[j]; }
	p->lz_nh = (uint8_t)(p->lz_nh - k);
}

static void special_write(p8x32a *p, int n, unsigned a, uint32_t v, uint64_t m3)
'''),
    (r'''	int k = a & 1;
	switch (a) {
''',
     r'''	int k = a & 1;
	if (p->lz_on) {
		if (n == p->lz && a == 0x1F4) { lazy_outa(p, v, e); return; }
		/* another cog drives the lazy cog's pins: it runs as the others from here */
		if ((a == 0x1F6 && (v & p->lz_pins)) || ((a == 0x1F8 || a == 0x1F9) && (nco_pins(v) & p->lz_pins))) lazy_exit(p);
	}
	switch (a) {
'''),
    (r'''	case 0x1F1: return cnt(p, t);
	case 0x1F2: return ina(p, t);
	case 0x1FC: return phs_at(c, 0, t);
''',
     r'''	case 0x1F1: return cnt(p, t);
	case 0x1F2: return ina(p, t, 0xFFFFFFFFu);
	case 0x1FC: return phs_at(c, 0, t);
'''),
    (r'''	if (l->nin) {
		uint32_t v = ina(p, t);
		for (k = 0; k < l->nin; k++)
''',
     r'''	if (l->nin) {
		uint32_t v, m = 0;
		for (k = 0; k < l->nin; k++) m |= l->in_mask[k];
		v = ina(p, t, m);
		for (k = 0; k < l->nin; k++)
'''),
    (r'''	}
	if (head == l->head && !l->dirty && l->nins <= P8X32A_PAT / 2 && t > l->head_t) {
		l->state = LOOP_RECORD;
''',
     r'''	}
	if (head == l->head && !l->dirty && l->nins <= P8X32A_PAT / 2 && t > l->head_t && !(p->lz_on && n == p->lz)) {
		l->state = LOOP_RECORD;
'''),
    (r'''	l->wake = (m & ~p->bus.pure_in) ? 0xFFFFFFFFu : m;
	l->t0 = c->ev_t;
''',
     r'''	l->wake = (m & ~p->bus.pure_in) ? 0xFFFFFFFFu : m;
	/* the lazy cog's pin changes are no pending points */
	if (p->lz_on && (l->wake & p->lz_pins)) { loop_reset(l); return; }
	l->t0 = c->ev_t;
'''),
    (r'''	bit = (uint8_t)(1u << num);
	if (op == 2 || op == 3) {
''',
     r'''	bit = (uint8_t)(1u << num);
	if (p->lz_on && (op == 2 || op == 3) && num == p->lz) lazy_exit(p);
	if (op == 2 || op == 3) {
'''),
    (r'''			if (p->bus.cog_start) p->bus.cog_start(p->bus.ctx, h, (int)num, t->ptr);
			stop_cog(p, (int)num, h + 1);
''',
     r'''			if (p->bus.cog_start) p->bus.cog_start(p->bus.ctx, h, (int)num, t->ptr);
			p->lz_try[num] = p->lz_wait[num] = 0;
			stop_cog(p, (int)num, h + 1);
'''),
    (r'''			int changed = 0;
			for (k = 0; k < sz; k++) {
''',
     r'''			int changed = 0;
			if (p->lz_on) lazy_write(p, ha, sz, v, h);
			for (k = 0; k < sz; k++) {
'''),
    (r'''	if (t + 2 >= c->disable_at) { idle(p, n); return; }
	match = ((ina(p, t) & c->s) == c->d) ^ (OP(c->i) == 0x3D);
	if (match) { c->ev = EV_DONE; c->ev_t = t + 2; return; }
''',
     r'''	if (t + 2 >= c->disable_at) { idle(p, n); return; }
	if (p->lz_on && (c->s & p->lz_pins)) lazy_exit(p);
	match = ((ina(p, t, c->s) & c->s) == c->d) ^ (OP(c->i) == 0x3D);
	if (match) { c->ev = EV_DONE; c->ev_t = t + 2; return; }
'''),
    (r'''	for (k = 0; k < 8; k++)
		if (k != n && p->cog[k].ev != EV_NONE && p->cog[k].ev_t + 1 < nt) nt = p->cog[k].ev_t + 1;
	if (p->bus.pins_next) {
''',
     r'''	for (k = 0; k < 8; k++)
		if (k != n && p->cog[k].ev != EV_NONE && !(p->lz_on && k == p->lz) && p->cog[k].ev_t + 1 < nt) nt = p->cog[k].ev_t + 1;
	if (p->bus.pins_next) {
'''),
    (r'''	c->cond = (uint8_t)(((COND(i) >> ((c->c << 1) | c->z)) & 1) && !c->cancel);
	c->s = FIM(i) ? SRC(i) : sread(p, n, SRC(i), t2);
	c->d = c->ram[DST(i)];
''',
     r'''	c->cond = (uint8_t)(((COND(i) >> ((c->c << 1) | c->z)) & 1) && !c->cancel);
	c->s = FIM(i) ? SRC(i) : SRC(i) == 0x1F2 ? ina(p, t2, (op == 0x18 || op == 0x19) ? c->ram[DST(i)] : 0xFFFFFFFFu) : sread(p, n, SRC(i), t2);
	c->d = c->ram[DST(i)];
'''),
    (r'''enum { K_NL, K_GEN, K_JMP, K_DJNZ, K_TJ, K_AND, K_ANDN, K_OR, K_XOR, K_ADD, K_SUB, K_MOV, K_SHL, K_SHR, K_MOVS, K_MOVD, K_MOVI };
enum { F_IMM = 1, F_SPEC = 2, F_WR = 4, F_WC = 8, F_WZ = 16, F_INA = 32 };

''',
     r'''enum { K_NL, K_GEN, K_JMP, K_DJNZ, K_TJ, K_AND, K_ANDN, K_OR, K_XOR, K_ADD, K_SUB, K_MOV, K_SHL, K_SHR, K_MOVS, K_MOVD, K_MOVI };
enum { F_IMM = 1, F_SPEC = 2, F_WR = 4, F_WC = 8, F_WZ = 16, F_INA = 32, F_OUTA = 64, F_HUBRD = 128 };

'''),
    (r'''	if (FWR(i) && DST(i) >= 0x1F0) e->kind = K_NL;
}
''',
     r'''	if (FWR(i) && DST(i) >= 0x1F0) e->kind = K_NL;
	if (FWR(i) && DST(i) == 0x1F4) e->fl |= F_OUTA;
	if (op <= 2 && FWR(i) && !FWC(i)) e->fl |= F_HUBRD;
}
'''),
    (r'''		int changed = 0;
		for (k = 0; k < sz; k++) {
''',
     r'''		int changed = 0;
		if (p->lz_on) lazy_write(p, ha, sz, d, h);
		for (k = 0; k < sz; k++) {
'''),
    (r'''   (the word fetched for the next instruction). */
static int event_run(p8x32a *p, int n, const p8x32a_dec *e, uint32_t ix, unsigned pc, unsigned *fl, uint64_t *t2, uint32_t *nix, uint64_t t, uint64_t lim, unsigned gen)
{
''',
     r'''   (the word fetched for the next instruction). */
P8_INLINE int event_run(p8x32a *p, int n, const p8x32a_dec *e, uint32_t ix, unsigned pc, unsigned *fl, uint64_t *t2, uint32_t *nix, uint64_t t, uint64_t lim, unsigned gen)
{
'''),
    (r'''		if (e->fl & F_IMM) s = e->src;
		else if (e->fl & F_INA) s = ina(p, now);
		else if (!(e->fl & F_SPEC)) s = ram[e->src];
''',
     r'''		if (e->fl & F_IMM) s = e->src;
		else if (e->fl & F_INA) s = ina(p, now, (op == 0x18 || op == 0x19) ? ram[e->dst] : 0xFFFFFFFFu);
		else if (!(e->fl & F_SPEC)) s = ram[e->src];
'''),
    (r'''
static void jit_write(p8x32a *p, int n, unsigned s, uint32_t old)
{
	if (p->jit_build && (p->jcode[n][s >> 3] >> (s & 7) & 1)) jit_written(p, n, s, old);
}

/* the translated block for ix at cog address a */
static p8x32a_jblk *jit_get(p8x32a *p, int n, unsigned a, uint32_t ix)
{
''',
     r'''

/* the translated block for ix at cog address a */
P8_INLINE p8x32a_jblk *jit_get(p8x32a *p, int n, unsigned a, uint32_t ix)
{
'''),
    (r'''	}
	b = p->jit_build(p->jit, b, a, ix, p->cog[n].ram, var);
	p->jblk[n][a] = b;
''',
     r'''	}
	b = p->jit_build(p->jit, b, a, ix, p->cog[n].ram, var, p->lz_on && n == p->lz);
	p->jblk[n][a] = b;
'''),
    (r'''		/* event instructions are never translated */
		if (p->jit_build && !(fl & 4) && e->kind != K_NL && !(e->fl & F_INA)) {
			unsigned a = (pc - 1) & 511;
''',
     r'''		/* event instructions are never translated */
		if (p->jit_build && !(fl & 4) && (e->kind != K_NL || ((e->fl & (F_OUTA | F_HUBRD)) && p->lz_on && n == p->lz)) && !(e->fl & F_INA)) {
			unsigned a = (pc - 1) & 511;
'''),
    (r'''			unsigned k;
			if (b && b->len && t2 + 4 * (uint64_t)(b->len - 1) <= tl) {
				l->nins = (uint16_t)(l->nins + nins);
''',
     r'''			unsigned k;
			if (b && b->len && (b->part ? t2 <= tl : t2 + 4 * (uint64_t)(b->len - 1) <= tl)) {
				l->nins = (uint16_t)(l->nins + nins);
'''),
    (r'''				st.t2 = t2;
				st.budget = (uint32_t)((tl - t2) / 4 + 1);
				st.ix = ix;
''',
     r'''				st.t2 = t2;
				st.budget = b->part ? 0x7FFFFFFFu : (uint32_t)((tl - t2) / 4 + 1);
				st.ix = ix;
'''),
    (r'''				st.inv = st.edge = 0;
				k = b->fn(&st);
			} else
''',
     r'''				st.inv = st.edge = 0;
				if (b->part) {
					st.ot = p->jot;
					st.ov = p->jov;
					st.on = 0;
					st.tl = tl;
					st.slot = p->slot_base + 3 + 2 * (uint64_t)n;
					st.hub = p->hub;
					st.latch = 0;
				}
				k = b->fn(&st);
				if (b->part) {
					unsigned j;
					if (st.latch) c->latch = st.latch;
					for (j = 0; j < st.on; j++) lazy_outa(p, st.ov[j], st.ot[j]);
					st.on = 0;
				}
			} else
'''),
    (r'''
void p8x32a_run_until(p8x32a *p, uint64_t t)
''',
     r'''
/* Lazy cogs. A cog whose code from its next instruction on reads hub RAM, PAR and CNT only, computes in cog RAM,
   jumps to fixed addresses and writes OUTA, on pins no other cog or counter drives and that only the host's
   bus.lazy_pins watches (lazy_ok), leaves the schedule and runs behind the others: it catches up before another
   cog's hub write that changes a byte (so it reads hub RAM as it was), before an INA read and at the end of
   run_until, and its pin changes go to bus.lazy_pins in time order instead of the pending points. A wait or a sleep
   on its pins, another cog driving them, or a cog operation on it makes it run as the others again. */

/* the cog's code from its next instruction: every reachable word passes, and none of it is written */
static int lazy_code(const p8x32a *p, int n)
{
	const p8x32a_cog *c = &p->cog[n];
	uint8_t seen[64], wr[64];
	uint16_t todo[512];
	int nt = 0, k;
	memset(seen, 0, sizeof(seen));
	memset(wr, 0, sizeof(wr));
	todo[nt++] = (uint16_t)((c->p - 1) & 511);
	seen[todo[0] >> 3] |= (uint8_t)(1u << (todo[0] & 7));
	while (nt) {
		unsigned a = todo[--nt], op, nx[2], nn = 0, j;
		uint32_t w = c->ram[a];
		if (a >= 0x1F0) return 0;
		op = OP(w);
		if (COND(w) == 0) nx[nn++] = (a + 1) & 511;
		else {
			if (op == 3 || op >= 0x3C) return 0;                           /* system ops, waits */
			if (!FIM(w) && SRC(w) >= 0x1F0 && SRC(w) != 0x1F0 && SRC(w) != 0x1F1 && SRC(w) != 0x1F4) return 0; /* INA, PHS, ... */
			if (op <= 2) {
				if (!FWR(w) || DST(w) >= 0x1F0) return 0;                  /* hub writes */
			} else if (FWR(w) && DST(w) >= 0x1F0 && DST(w) != 0x1F4) return 0; /* special registers but OUTA */
			if (FWR(w) && DST(w) < 0x1F0) wr[DST(w) >> 3] |= (uint8_t)(1u << (DST(w) & 7));
			if (op == 0x17 || (op >= 0x39 && op <= 0x3B)) {
				if (!FIM(w)) return 0;                                       /* jumps to an address from a register */
				nx[nn++] = SRC(w);
				if (COND(w) != 15 || op != 0x17) nx[nn++] = (a + 1) & 511;
			} else nx[nn++] = (a + 1) & 511;
		}
		for (j = 0; j < nn; j++)
			if (!(seen[nx[j] >> 3] >> (nx[j] & 7) & 1)) {
				seen[nx[j] >> 3] |= (uint8_t)(1u << (nx[j] & 7));
				todo[nt++] = (uint16_t)nx[j];
			}
	}
	for (k = 0; k < 64; k++)
		if (seen[k] & wr[k]) return 0;                                          /* code it writes */
	return 1;
}

P8_COLD void lazy_run(p8x32a *p, uint64_t t)
{
	p8x32a_cog *c;
	uint64_t now;
	c = &p->cog[p->lz];
	now = p->now;
	p->lz_to = t;
	lazy_due(p, t);
	c->ev_t = p->lz_evt;
	while (c->ev_t <= t && (c->ev == EV_EXEC || c->ev == EV_HUB)) {
		p->now = c->ev_t;
		if (!run_local(p, p->lz, t, P8X32A_NEVER, p->sched_gen)) { log_once(p, LOG_LAZY, "p8x32a: lazy cog stopped"); break; }
	}
	lazy_due(p, t);
	p->lz_evt = c->ev_t;
	c->ev_t = P8X32A_NEVER; /* out of the schedule */
	p->now = now;
	p->lz_at = t;
}

/* the cog's translated blocks are built again: with or without its OUTA writes */
static void jit_drop(p8x32a *p, int n)
{
	unsigned j;
	for (j = 0; j < 512; j++)
		if (p->jblk[n][j]) p->jblk[n][j]->valid = 0;
	memset(p->jcode[n], 0, sizeof(p->jcode[n]));
}

/* at p->now: the lazy cog runs as the others from here */
static void lazy_exit(p8x32a *p)
{
	p8x32a_cog *c = &p->cog[p->lz];
	int k;
	lazy_catch(p, p->now);
	flush(p, p->now);
	for (k = 0; k < p->lz_nh; k++) add_pending(p, p->lz_ht[k], p->lz_pins, P8X32A_PEND_COG(p->lz));
	p->lz_nh = 0;
	p->lz_on = 0;
	c->ev_t = p->lz_evt;
	c->outa = p->lz_reg;
	jit_drop(p, p->lz);
	p->pins_ok = 0;
	p->sched_gen++;
	p->lz_try[p->lz] = p->now + (1u << 16);
	if (p->bus.lazy) p->bus.lazy(p->bus.ctx, p->now, 0, 0, 0);
}

/* at the end of run_until(t), with the pins flushed to t: a cog that can run lazily from its next instruction */
static void lazy_try(p8x32a *p, uint64_t t)
{
	int n, k, j;
	if (p->stop) return;
	for (n = 0; n < 8; n++) {
		p8x32a_cog *c = &p->cog[n];
		uint32_t g = c->dira.cur, other = 0;
		if (c->ev != EV_EXEC || t < p->lz_try[n]) continue;
		/* a cog that does not qualify is looked at again after 2^14 cycles, doubling to 2^22 */
		p->lz_wait[n] = p->lz_wait[n] ? (p->lz_wait[n] < (1u << 22) ? p->lz_wait[n] * 2 : p->lz_wait[n]) : 1u << 14;
		p->lz_try[n] = t + p->lz_wait[n];
		if (!c->run || (p->sleepers >> n & 1) || p->loop[n].state != LOOP_SEARCH) continue;
		if (c->disable_at != P8X32A_NEVER || c->restart_at != P8X32A_NEVER || c->ix != c->ram[(c->p - 1) & 511]) continue;
		if (c->ctr[0] || c->ctr[1] || c->ctr_old[0] || c->ctr_old[1] || c->ctr_at[0] > t || c->ctr_at[1] > t) continue;
		if (!g || (g & ~p->lazy_ok) || c->dira.at > t) continue;
		for (k = 0; k < 8; k++) {
			const p8x32a_cog *o = &p->cog[k];
			for (j = 0; j < 2; j++) other |= nco_pins(o->ctr[j]) | nco_pins(o->ctr_old[j]);
			if (k == n) continue;
			other |= regval(&o->dira, t) | o->dira.cur;
			if ((p->sleepers >> k & 1) && (p->loop[k].wake & g)) other |= g;
			if (o->ev == EV_WAITPIN) other |= o->s;
		}
		if ((other & g) || !lazy_code(p, n)) continue;
		p->lz_on = 1;
		p->lz = (uint8_t)n;
		p->lz_pins = g;
		p->lz_at = p->lz_to = t;
		p->lz_nh = 0;
		/* an OUTA write not in effect yet goes on the lazy path */
		p->lz_out = regval(&c->outa, t) & g;
		if (c->outa.at > t && ((c->outa.cur ^ c->outa.prev) & g)) {
			p->lz_ht[0] = c->outa.at;
			p->lz_hout[0] = c->outa.cur & g;
			p->lz_nh = 1;
		}
		p->pins_ok = 0;
		p->sched_gen++;
		p->lazies++;
		p->lz_evt = c->ev_t;
		c->ev_t = P8X32A_NEVER;
		/* its OUTA is kept in lz_reg: the pins' register part leaves it out */
		p->lz_reg = c->outa;
		c->outa.prev = c->outa.cur = 0;
		jit_drop(p, n);
		if (p->bus.lazy) p->bus.lazy(p->bus.ctx, t, g, p->lz_out, g);
		return;
	}
}

void p8x32a_run_until(p8x32a *p, uint64_t t)
'''),
    (r'''	}
	flush(p, t);
	p->now = t;
}
''',
     r'''	}
	lazy_catch(p, t);
	flush(p, t);
	p->now = t;
	if (!p->lz_on && p->lazy_ok) lazy_try(p, t);
}
'''),
    (r'''	for (n = 0; n < 8; n++) loop_reset(&p->loop[n]);
	p->flushed = t;
''',
     r'''	for (n = 0; n < 8; n++) loop_reset(&p->loop[n]);
	if (p->lz_on) {
		jit_drop(p, p->lz);
		if (p->bus.lazy) p->bus.lazy(p->bus.ctx, t, 0, 0, 0);
	}
	p->lz_on = p->lz_nh = 0;
	memset(p->lz_try, 0, sizeof(p->lz_try));
	memset(p->lz_wait, 0, sizeof(p->lz_wait));
	p->flushed = t;
'''),
])
edit('src/cpu/p8x32a/p8x32ajit.h', [
    (r'''void p8x32a_jit_free(void *jit);
p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var);

''',
     r'''void p8x32a_jit_free(void *jit);
p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var, int outa);

'''),
])
edit('src/cpu/p8x32a/p8x32ajit.cpp', [
    (r'''	uint64_t tail, tail_ret; // the code a block leaves through, and its return to run_local
};
''',
     r'''	uint64_t tail, tail_ret; // the code a block leaves through, and its return to run_local
	uint64_t ltail, ltail_ret; // the same for a lazy cog's blocks
};
'''),
    (r'''
// a fixed word run_local() would run as a local instruction
bool supported(uint32_t i)
{
	if (!op_ok(op_of(i))) return false;
	if (!fim(i) && src_of(i) >= 0x1F0) return false;
	if (fwr(i) && dst_of(i) >= 0x1F0) return false;
	return true;
''',
     r'''
// a fixed word run_local() would run as a local instruction; with outa, also a write to OUTA or a hub read without
// WC (a lazy cog's)
bool supported(uint32_t i, int outa)
{
	unsigned op = op_of(i);
	if (outa && op <= 2) return fwr(i) && !fwc(i) && dst_of(i) < 0x1F0 && (fim(i) || src_of(i) < 0x1F0);
	if (!op_ok(op)) return false;
	if (!fim(i) && src_of(i) >= 0x1F0) return false;
	if (fwr(i) && dst_of(i) >= 0x1F0)
		return outa && dst_of(i) == 0x1F4 && ((op >= OP_ROR && op <= OP_SAR) || (op >= OP_MOVS && op <= OP_MOVI) || (op >= OP_AND && op <= OP_SUB) || op == OP_MOV);
	return true;
'''),
    (r'''		switch (op) {
		case OP_ROR: a.mov(x86::eax, x86::edx); a.ror(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.and_(x86::r11d, 1); break;
''',
     r'''		switch (op) {
		case 0: case 1: case 2: {
			// a lazy cog's hub read: it waits for the cog's slot (next_slot), which must come by st.tl
			Label late = a.new_label(), sb = a.new_label(), got = a.new_label(), on = a.new_label();
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
			a.lea(x86::rdi, x86::ptr(x86::rax, 2));
			a.cmp(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tl)));
			a.jbe(on);
			a.bind(late);
			a.mov(x86::esi, i);
			stop_before(k, x86::esi);
			a.bind(on);
			// the next instruction starts at latch + 7
			a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, latch)), x86::rax);
			a.lea(T2, x86::ptr(x86::rax, (int)(7 - 4 * (k + 1))));
			a.and_(x86::ecx, 0xFFFF);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hub)));
			if (op == 0) a.movzx(x86::eax, x86::byte_ptr(x86::rdi, x86::rcx));
			else if (op == 1) { a.and_(x86::ecx, 0xFFFE); a.movzx(x86::eax, x86::word_ptr(x86::rdi, x86::rcx)); }
			else { a.and_(x86::ecx, 0xFFFC); a.mov(x86::eax, x86::dword_ptr(x86::rdi, x86::rcx)); }
			break;
		}
		case OP_ROR: a.mov(x86::eax, x86::edx); a.ror(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.and_(x86::r11d, 1); break;
'''),
    (r'''		}
		if (jump) leave(k + 1);
''',
     r'''		}
		if (!dyn && fwr(i) && dst_of(i) == 0x1F4) {
			// a lazy cog's OUTA write: its time (this instruction's + 2) and value go to st's list
			a.mov(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, ot)));
			a.mov(x86::edi, stf(offsetof(p8x32a_jst, on)));
			a.lea(x86::r11, x86::ptr(T2, (int)(4 * k + 2)));
			a.mov(x86::qword_ptr(x86::rsi, x86::rdi, 3), x86::r11);
			a.mov(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, ov)));
			a.mov(x86::dword_ptr(x86::rsi, x86::rdi, 2), x86::eax);
			a.inc(stf(offsetof(p8x32a_jst, on)));
		}
		if (jump) leave(k + 1);
'''),
    (r'''// jump, and continue in the block at the next address when nothing needs run_local.
bool build_tail(P8Jit *j)
{
''',
     r'''// jump, and continue in the block at the next address when nothing needs run_local.
bool build_tail(P8Jit *j, bool lazy)
{
'''),
    (r'''	a.sub(BUDGET, x86::eax);
	a.mov(L, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
''',
     r'''	a.sub(BUDGET, x86::eax);
	if (lazy) {
		// a block that stopped before its first slot, at its time limit
		a.test(x86::eax, x86::eax);
		a.jz(ret);
	}
	a.mov(L, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
'''),
    (r'''	a.jne(ret);
	a.mov(x86::ecx, stf(offsetof(p8x32a_jst, px)));
''',
     r'''	a.jne(ret);
	if (lazy) {
		a.cmp(stf(offsetof(p8x32a_jst, on)), P8X32A_JOUT - P8X32A_JMAX);
		a.jae(ret);
	}
	a.mov(x86::ecx, stf(offsetof(p8x32a_jst, px)));
'''),
    (r'''	if (j->rt.add(&fn, &code) != kErrorOk) return false;
	j->tail = (uint64_t)(uintptr_t)fn;
	j->tail_ret = j->tail + code.label_offset(ret);
	return true;
''',
     r'''	if (j->rt.add(&fn, &code) != kErrorOk) return false;
	if (lazy) {
		j->ltail = (uint64_t)(uintptr_t)fn;
		j->ltail_ret = j->ltail + code.label_offset(ret);
	} else {
		j->tail = (uint64_t)(uintptr_t)fn;
		j->tail_ret = j->tail + code.label_offset(ret);
	}
	return true;
'''),
    (r'''	P8Jit *j = new (std::nothrow) P8Jit();
	if (j && !build_tail(j)) { delete j; j = NULL; }
	return j;
''',
     r'''	P8Jit *j = new (std::nothrow) P8Jit();
	if (j && (!build_tail(j, false) || !build_tail(j, true))) { delete j; j = NULL; }
	return j;
'''),
    (r'''}

extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var)
{
	P8Jit *j = (P8Jit *)jit;
''',
     r'''}

extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var, int outa)
{
	P8Jit *j = (P8Jit *)jit;
'''),
    (r'''	b->dyn = 0;
	b->words[0] = ix;
''',
     r'''	b->dyn = 0;
	b->part = outa != 0;
	b->words[0] = ix;
'''),
    (r'''		bool dyn = v != 0;
		if ((v & ~P8X32A_JDYN) || !op_ok(op_of(w))) break;
		if (!dyn && !supported(w)) break;
		b->words[len] = w;
''',
     r'''		bool dyn = v != 0;
		if ((v & ~P8X32A_JDYN) || !(op_ok(op_of(w)) || (outa && op_of(w) <= 2 && !dyn))) break;
		if (!dyn && !supported(w, outa)) break;
		b->words[len] = w;
'''),
    (r'''	as.bind(body);
	Emit e = { as, frame, a, j->tail, j->tail_ret };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);
		e.insn(k, b->words[k], (b->dyn >> k & 1) != 0, last, last || next_dyn);
''',
     r'''	as.bind(body);
	Emit e = { as, frame, a, outa ? j->ltail : j->tail, outa ? j->ltail_ret : j->tail_ret };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);
		if (outa) {
			// a lazy cog's block runs while its instructions start by st.tl
			Label go = as.new_label();
			as.lea(x86::rsi, x86::ptr(T2, (int)(4 * k)));
			as.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tl)));
			as.jbe(go);
			if (b->dyn >> k & 1) e.stop_before(k, NEXTW);
			else {
				as.mov(x86::esi, b->words[k]);
				e.stop_before(k, x86::esi);
			}
			as.bind(go);
		}
		e.insn(k, b->words[k], (b->dyn >> k & 1) != 0, last, last || next_dyn);
'''),
    (r'''extern "C" void p8x32a_jit_free(void *jit) { (void)jit; }
extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var)
{
	(void)jit; (void)old; (void)a; (void)ix; (void)ram; (void)var;
	return NULL;
''',
     r'''extern "C" void p8x32a_jit_free(void *jit) { (void)jit; }
extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var, int outa)
{
	(void)jit; (void)old; (void)a; (void)ix; (void)ram; (void)var; (void)outa;
	return NULL;
'''),
])
edit('tests/pinheck/p8x32a/jit_oom_test.cpp', [
    (r'''	try {
		b = p8x32a_jit_build(jit, NULL, 0, 0xA0BC0000u, ram, var);
	} catch (...) {
''',
     r'''	try {
		b = p8x32a_jit_build(jit, NULL, 0, 0xA0BC0000u, ram, var, 0);
	} catch (...) {
'''),
])
edit('src/wpc/pinheck/prop.h', [
    (r'''	uint32_t pins_mask; /* pins calls fn only on a change of these (default all) */
	prop_clock_fn clock; /* the PIC32 cycle now */
''',
     r'''	uint32_t pins_mask; /* pins calls fn only on a change of these (default all) */
	prop_pins_fn pins_lazy; /* a lazy cog's pins (p8x32a.h), if they are all in lazy_mask */
	void *pins_lazy_ctx;
	uint32_t lazy_mask;
	uint32_t lz_mask, lz_out, lz_dir; /* the lazy cog's pins and their last state */
	prop_clock_fn clock; /* the PIC32 cycle now */
'''),
    (r'''void prop_set_pins_mask(pinheck_prop *p, uint32_t mask);
void prop_reset(pinheck_prop *p, uint64_t pic_cycle);
''',
     r'''void prop_set_pins_mask(pinheck_prop *p, uint32_t mask);
void prop_set_pins_lazy(pinheck_prop *p, prop_pins_fn fn, void *ctx, uint32_t mask);
void prop_reset(pinheck_prop *p, uint64_t pic_cycle);
'''),
])
edit('src/wpc/pinheck/prop.c', [
    (r'''	pinheck_prop *p = (pinheck_prop *)ctx;
	uint32_t ch = p->po_ok ? (out ^ p->po_out) | (dir ^ p->po_dir) : 0xFFFFFFFFu;
	p->po_out = out;
''',
     r'''	pinheck_prop *p = (pinheck_prop *)ctx;
	uint32_t ch;
	/* a lazy cog's pins: as last sent on the lazy path */
	if (p->lz_mask) {
		out = (out & ~p->lz_mask) | p->lz_out;
		dir = (dir & ~p->lz_mask) | p->lz_dir;
	}
	ch = p->po_ok ? (out ^ p->po_out) | (dir ^ p->po_dir) : 0xFFFFFFFFu;
	p->po_out = out;
'''),
    (r'''	if (p->pins && (ch & p->pins_mask)) p->pins(p->pins_ctx, t, out, dir);
}
''',
     r'''	if (p->pins && (ch & p->pins_mask)) p->pins(p->pins_ctx, t, out, dir);
}

static void lazy(void *ctx, uint64_t t, uint32_t mask, uint32_t out, uint32_t dir)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	(void)t;
	if (p->log && p->chip.lazies <= 16 && mask != p->lz_mask) {
		char msg[48];
		if (mask) sprintf(msg, "prop: a cog runs lazily on pins %08x", (unsigned)mask);
		else strcpy(msg, "prop: the lazy cog runs as the others");
		p->log(p->log_ctx, msg);
	}
	p->lz_mask = mask;
	p->lz_out = out & mask;
	p->lz_dir = dir & mask;
}

static void lazy_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	pinheck_prop *p = (pinheck_prop *)ctx;
	p->lz_out = out;
	p->lz_dir = dir;
	if (p->pins_lazy) p->pins_lazy(p->pins_lazy_ctx, t, out, dir);
}

/* a cog may run lazily on pins that no device but pins_lazy watches */
static void lazy_update(pinheck_prop *p)
{
	const uint32_t dev = PIN_DO | PIN_SCLK | PIN_DI | PIN_CS | PIN_SND | PIN_P24 | PIN_P25 | (1u << 26) | PIN_SCL | PIN_SDA;
	p->chip.lazy_ok = p->pins_lazy ? ~(dev | (p->pins_mask & ~p->lazy_mask)) : 0;
}
'''),
    (r'''	memset(p, 0, sizeof(*p));
	bus.ctx = p;
''',
     r'''	memset(p, 0, sizeof(*p));
	memset(&bus, 0, sizeof(bus));
	bus.ctx = p;
'''),
    (r'''	bus.pure_in = ~(PIN_DO | PIN_SDA); /* SD DO and EEPROM SDA answer inside pins_out */
	p8x32a_init(&p->chip, &bus);
''',
     r'''	bus.pure_in = ~(PIN_DO | PIN_SDA); /* SD DO and EEPROM SDA answer inside pins_out */
	bus.lazy = lazy;
	bus.lazy_pins = lazy_pins;
	p8x32a_init(&p->chip, &bus);
'''),
    (r'''	p->pins_mask = 0xFFFFFFFFu;
}
''',
     r'''	p->pins_mask = 0xFFFFFFFFu;
	lazy_update(p);
}
'''),
    (r'''	p->pins_mask = mask;
	p->po_ok = 0;
}

''',
     r'''	p->pins_mask = mask;
	p->po_ok = 0;
	lazy_update(p);
}

/* set before the Propeller runs: fn gets the changes of a lazy cog's pins, which must all be in mask; NULL: none */
void prop_set_pins_lazy(pinheck_prop *p, prop_pins_fn fn, void *ctx, uint32_t mask)
{
	p->pins_lazy = fn;
	p->pins_lazy_ctx = ctx;
	p->lazy_mask = mask;
	lazy_update(p);
}

'''),
])
edit('src/wpc/pinheck.c', [
    (r'''
/* test hook PINHECK_DMD_LOG: at each vblank, the number of subframes the decoder queued since the last (uint32) and
''',
     r'''
/* the scan cog running lazily (p8x32a.h): the changes of its pins */
static void pinheck_dmd_lazy_cb(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx;
	pinheck_dmd_pins(&dmd, t, out, dir);
	if (dmd_proof) pinheck_dmd_pins(&dmd_row, t, out, dir);
}

/* test hook PINHECK_DMD_LOG: at each vblank, the number of subframes the decoder queued since the last (uint32) and
'''),
    (r'''		prop_set_pins_mask(&prop, DMD_ALL_PINS | (ser_log ? PINHECK_SER_PIN : 0));
		return;
	}
	prop_set_pins(&prop, pinheck_disp_pins, NULL);
''',
     r'''		prop_set_pins_mask(&prop, DMD_ALL_PINS | (ser_log ? PINHECK_SER_PIN : 0));
		if (!getenv("PINHECK_LAZY") || atoi(getenv("PINHECK_LAZY")) != 0) prop_set_pins_lazy(&prop, pinheck_dmd_lazy_cb, NULL, DMD_ALL_PINS);
		return;
	}
	prop_set_pins_lazy(&prop, NULL, NULL, 0);
	prop_set_pins(&prop, pinheck_disp_pins, NULL);
'''),
])
EOF
```

- [ ] **Step 4: The Propeller suite, interpreted and translated**

```sh
nice -n 15 tests/pinheck/p8x32a/check.sh | tail -1
P8X32A_JIT=1 nice -n 15 tests/pinheck/p8x32a/check.sh | grep -E 'translated|passed'
```
Expected (about 10 minutes; the first run also builds the boot and SD-boot RTL references):
```text
p8x32a: 234 passed, 0 failed
p8run: translated
p8x32a: 234 passed, 0 failed
```

- [ ] **Step 5: America's Most Haunted with its scan cog lazy, on one thread**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12.log 2>&1; tail -1 build/sdl3pinmame/12.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -v ' ref:'
grep lazily tests/pinheck/perf/build/amh/opt/short/prop.log
```
Expected (about 3 minutes):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/amh.nv
bench short opt: 0.763x over 10.0 s emulated (13.1 s wall), whole run 13.1 G instructions and 1.0 s CPU per emulated s, worst 100 ms 0.731x, load 1.85
determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
prop: a cog runs lazily on pins 003f0000
```

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c src/cpu/p8x32a/p8x32ajit.h src/cpu/p8x32a/p8x32ajit.cpp src/wpc/pinheck/prop.h src/wpc/pinheck/prop.c src/wpc/pinheck.c tests/pinheck/p8x32a/run.c tests/pinheck/p8x32a/check.sh tests/pinheck/p8x32a/jit_oom_test.cpp tests/pinheck/p8x32a/chip/lazy.spin
git commit -m "p8x32a: lazy cogs (an output-only cog runs behind the others; its pins on their own path), translated with their OUTA writes and hub reads"
```

### Task 4: Uncertain RF13 reads

**Files:**
- Create: `tests/pinheck/mips32/uncertain_test.c`
- Modify: `tests/pinheck/mips32/check.sh`, `tests/pinheck/mips32/unit_test.c`, `tests/pinheck/mips32/run.c`, `tests/pinheck/pic32mx/boot.c`, `tests/pinheck/pic32mx/soc_test.c`, `tests/pinheck/link/machine.c` (the tests and the new callbacks in their initialisers)
- Modify: `src/cpu/mips32/mips32.h`, `src/cpu/mips32/mips32.c`, `src/cpu/pic32mx/pic32mx.h`, `src/cpu/pic32mx/pic32mx.c`, `src/cpu/pic32mx/pic32mxcpu.c`, `src/wpc/pinheck/prop.h`, `src/wpc/pinheck/prop.c`, `src/wpc/pinheck.c`

**Interfaces:**
- Consumes: Plan 9b's worker queue (`post`, `wait_head`, `run_cmd`), `prop_p24`.
- Produces: `mips32_bus.settle` (`int (*)(void *ctx, uint32_t token, int wait, uint32_t *bits)`: the true bits in the read's value, 1 when known, always with `wait`), `void mips32_uncertain(mips32_state *, uint32_t mask, uint32_t token)` (from `bus.read`), `void mips32_settle(mips32_state *)`; `pic32mx_board.port_settle` (same signature, in `port_read`'s value), `void pic32mx_uncertain(pic32mx *, uint32_t mask, uint32_t token)` (from `port_read`); prop.c: `uint32_t prop_sample(pinheck_prop *, uint64_t pic_cycle)` (with the worker: `P24` as `prop_p24` reads it, read by the worker; returns a token) and `int prop_sample_get(pinheck_prop *, uint32_t token, int wait)` (0/1, -1 not yet); `PINHECK_RF13=0` reads `RF13` exactly at the read, as before.

- [ ] **Step 1: The differential test**

```sh
cat > tests/pinheck/mips32/uncertain_test.c <<'EOF'
/* Uncertain reads (mips32_uncertain): random programs load a device word whose bits in a mask are known only later;
   each runs once with the true word and once with wrong bits there, settled through bus.settle (at once or after
   some instructions), with a timer-like interrupt whose handler stores every register. Registers, HI/LO, PC and
   memory must end the same. */
#include "mips32.h"
#include <stdio.h>
#include <string.h>

#define DEV 0xF000u   /* the device word, physical */
#define CLR 0xFFF4u   /* a store here drops the interrupt request */
#define PROG 0x1000u  /* program, physical (kseg0 0x80001000) */
#define NPROG 64

static uint8_t kmem[0x10000];
static mips32_state cpu;
static uint32_t dev_true, dev_mask, dev_guess, rng;
static int uncertain, settle_after, asked, shadow;

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static uint32_t rd(void *ctx, uint32_t pa, int size, int fetch, int *err)
{
	uint32_t v = 0;
	int i;
	(void)ctx; (void)fetch;
	if (pa >= DEV && pa < DEV + 4) {
		unsigned sh = (pa & 3) * 8;
		uint32_t sm = size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1;
		uint32_t w = uncertain ? (dev_true & ~dev_mask) | (dev_guess & dev_mask) : dev_true;
		if (uncertain && ((dev_mask >> sh) & sm)) { mips32_uncertain(&cpu, (dev_mask >> sh) & sm, sh); asked = 0; }
		return (w >> sh) & sm;
	}
	if (pa + (unsigned)size > sizeof(kmem)) { *err = 1; return 0; }
	for (i = 0; i < size; i++) v |= (uint32_t)kmem[pa + i] << (8 * i);
	return v;
}

static void wr(void *ctx, uint32_t pa, uint32_t v, int size, int *err)
{
	int i;
	(void)ctx;
	if (pa == CLR) { mips32_set_eic(&cpu, 0, 0, 0); return; }
	if (pa >= DEV && pa < DEV + 4) return;
	if (pa + (unsigned)size > sizeof(kmem)) { *err = 1; return; }
	for (i = 0; i < size; i++) kmem[pa + i] = (uint8_t)(v >> (8 * i));
}

/* the true bits, in the read's value (token: its shift); unknown for settle_after asks after each uncertain read */
static int settle(void *ctx, uint32_t token, int wait, uint32_t *bits)
{
	(void)ctx;
	if (!wait && asked++ < settle_after) return 0;
	*bits = dev_true >> token;
	return 1;
}

static void put(uint32_t pa, uint32_t w) { kmem[pa] = (uint8_t)w; kmem[pa + 1] = (uint8_t)(w >> 8); kmem[pa + 2] = (uint8_t)(w >> 16); kmem[pa + 3] = (uint8_t)(w >> 24); }

#define R(op, rs, rt, rd, sa, fn) ((uint32_t)(op) << 26 | (uint32_t)(rs) << 21 | (uint32_t)(rt) << 16 | (uint32_t)(rd) << 11 | (uint32_t)(sa) << 6 | (fn))
#define I(op, rs, rt, imm) ((uint32_t)(op) << 26 | (uint32_t)(rs) << 21 | (uint32_t)(rt) << 16 | ((imm) & 0xFFFFu))

static unsigned reg(void) { return 1 + rnd() % 12; }

/* one random instruction at slot k of NPROG; base registers: 24 = the device (kseg0), 25 = scratch RAM */
static uint32_t insn(int k)
{
	static const uint8_t fn[] = { 0x21, 0x23, 0x24, 0x24, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B, 0x0A, 0x0B, 0x04, 0x06 };
	static const uint8_t ld[] = { 0x20, 0x21, 0x23, 0x24, 0x25 };
	switch (rnd() % 13) {
	case 12: return 0x41400000u | reg() << 16 | reg() << 11;                    /* rdpgpr (previous set = this one) */
	case 0: case 1: return I(ld[rnd() % 5], 24, reg(), 0) | ((rnd() % 4) & 0);
	case 2: { unsigned o = ld[rnd() % 5], off = o == 0x23 ? 0 : o == 0x21 || o == 0x25 ? (rnd() % 2) * 2 : rnd() % 4; return I(o, 24, reg(), off); }
	case 3: case 4: case 5: return R(0, reg(), reg(), reg(), 0, fn[rnd() % sizeof(fn)]);
	case 6: return I(0x0C, reg(), reg(), rnd() & 0xFFFF);                        /* andi */
	case 7: return I(0x09 + rnd() % 6, reg(), reg(), rnd() & 0xFFFF);            /* addiu slti sltiu andi ori xori */
	case 8: return R(0, 0, reg(), reg(), rnd() % 32, (rnd() % 2) ? 0x00 : 0x02);  /* sll, srl */
	case 9: return I((rnd() % 2) ? 0x2B : 0x28, 25, reg(), (rnd() % 16) * 4);   /* sw, sb */
	case 10: return I(0x23, 25, reg(), (rnd() % 16) * 4);                        /* lw scratch */
	default: {
		int fwd = 2 + (int)(rnd() % 6);
		if (k + fwd + 1 >= NPROG) return 0;
		return I(0x04 + rnd() % 2, reg(), reg(), fwd);                              /* beq, bne */
	}
	}
}

static void load_program(void)
{
	int k;
	memset(kmem, 0, sizeof(kmem));
	/* the handler at 0x180: store registers 1-12 (base 26), drop the request (base 27), return; in shadow set 1
	   it reads them from the interrupted set with RDPGPR */
	for (k = 1; k <= 12; k++) {
		put(0x180 + 8 * (uint32_t)(k - 1), shadow ? 0x41400000u | (uint32_t)k << 16 | 13u << 11 : 0);
		put(0x184 + 8 * (uint32_t)(k - 1), I(0x2B, 26, shadow ? 13 : k, 4 * k));
	}
	put(0x180 + 96, I(0x2B, 27, 0, 0));
	put(0x180 + 100, 0x42000018u);
	for (k = 0; k < NPROG; k++) put(PROG + 4 * (uint32_t)k, insn(k));
	put(PROG + 4 * NPROG, 0x1000FFFFu); /* b . */
	put(PROG + 4 * NPROG + 4, 0);
}

static void reset_cpu(void)
{
	mips32_bus bus;
	int k;
	memset(&bus, 0, sizeof(bus));
	bus.read = rd;
	bus.write = wr;
	bus.settle = settle;
	mips32_init(&cpu, &bus, 2, 0x00018700u);
	mips32_direct(&cpu, 0, 0, DEV, kmem, kmem); /* code and RAM; the device and CLR go through the bus */
	cpu.status = 0x00000001u;
	cpu.pc = 0x80000000u + PROG;
	cpu.npc = cpu.pc + 4;
	for (k = 1; k < 24; k++) mips32_regs(&cpu)[k] = rnd();
	mips32_regs(&cpu)[24] = 0x80000000u + DEV;
	mips32_regs(&cpu)[25] = 0x80000000u + 0x8000u;
	cpu.gpr[0][26] = cpu.gpr[1][26] = 0x80007000u;
	cpu.gpr[0][27] = cpu.gpr[1][27] = 0x80000000u + CLR;
	asked = 0;
}

int main(void)
{
	static uint8_t mem_a[sizeof(kmem)];
	uint32_t regs_a[32], seed;
	int t, fails = 0, ints = 0;
	for (t = 0; t < 20000; t++) {
		uint32_t s0, irq_at, k;
		int pass, slices;
		uint64_t cyc_a = 0;
		uint32_t pc_a = 0, hi_a = 0, lo_a = 0;
		seed = 0x9E3779B9u * (uint32_t)(t + 1);
		rng = seed;
		dev_true = rnd();
		dev_mask = (rnd() % 3) ? 1u << (rnd() % 32) : rnd();
		dev_guess = ~dev_true;
		settle_after = (int)(rnd() % 40);
		shadow = (int)(rnd() % 2);
		irq_at = rnd() % 80;
		s0 = rnd();
		for (pass = 0; pass < 2; pass++) {
			rng = s0;
			load_program();
			reset_cpu();
			uncertain = pass;
			for (slices = 0; slices < 100 && cpu.cycles < 200; slices++) {
				if (cpu.cycles >= irq_at && cpu.cycles < irq_at + 4) { mips32_set_eic(&cpu, 1, 0, shadow); if (pass) ints++; }
				mips32_run(&cpu, 1 + (int)((cpu.cycles * 7) % 5));
			}
			mips32_settle(&cpu);
			if (!pass) {
				memcpy(mem_a, kmem, sizeof(kmem));
				memcpy(regs_a, mips32_regs(&cpu), sizeof(regs_a));
				pc_a = cpu.pc; hi_a = cpu.hi; lo_a = cpu.lo; cyc_a = cpu.cycles;
				continue;
			}
			if (memcmp(mem_a, kmem, sizeof(kmem)) || memcmp(regs_a, mips32_regs(&cpu), sizeof(regs_a)) || pc_a != cpu.pc ||
			    hi_a != cpu.hi || lo_a != cpu.lo || cyc_a != cpu.cycles) {
				if (fails++ < 5) {
					printf("UNCERTAIN FAIL case %d (seed %08x, mask %08x):", t, (unsigned)seed, (unsigned)dev_mask);
					for (k = 1; k < 32; k++) if (regs_a[k] != mips32_regs(&cpu)[k]) printf(" r%u %08x/%08x", (unsigned)k, (unsigned)regs_a[k], (unsigned)mips32_regs(&cpu)[k]);
					printf(" pc %08x/%08x\n", (unsigned)pc_a, (unsigned)cpu.pc);
				}
			}
		}
	}
	printf("uncertain: 20000 programs, %d interrupt raises, %d differ\n", ints, fails);
	return fails != 0;
}
EOF
```

The suite runs it, and the test programs' bus and board initialisers name the new callbacks:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/mips32/check.sh', [
    (r'''[ -x $B/unit_test ] && { ./$B/unit_test || fail=$((fail + 1)); }
for f in diff/*.S; do [ -e "$f" ] && run_diff "$f" $B/diff; done
''',
     r'''[ -x $B/unit_test ] && { ./$B/unit_test || fail=$((fail + 1)); }
cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/mips32 -o $B/uncertain_test uncertain_test.c ../../../src/cpu/mips32/mips32.c || exit 2
./$B/uncertain_test || fail=$((fail + 1))
for f in diff/*.S; do [ -e "$f" ] && run_diff "$f" $B/diff; done
'''),
])
edit('tests/pinheck/mips32/unit_test.c', [
    (r'''{
	mips32_bus bus = { NULL, rd, wr, NULL, NULL };
	memset(kmem, 0, sizeof(kmem));
''',
     r'''{
	mips32_bus bus = { NULL, rd, wr, NULL, NULL, NULL };
	memset(kmem, 0, sizeof(kmem));
'''),
])
edit('tests/pinheck/mips32/run.c', [
    (r'''{
	mips32_bus bus = { NULL, bus_read, bus_write, exc_hook, NULL };
	uint64_t limit = 100000000;
''',
     r'''{
	mips32_bus bus = { NULL, bus_read, bus_write, exc_hook, NULL, NULL };
	uint64_t limit = 100000000;
'''),
])
edit('tests/pinheck/pic32mx/boot.c', [
    (r'''{
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, unmapped, exception, NULL };
	unsigned long long cycles = 800000000ull;
''',
     r'''{
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, unmapped, exception, NULL, NULL };
	unsigned long long cycles = 800000000ull;
'''),
])
edit('tests/pinheck/pic32mx/soc_test.c', [
    (r'''{
	pic32mx_board board = { NULL, b_port_write, b_port_read, b_uart_tx, NULL, b_unmapped, b_exception, NULL };
	memset(&rec, 0, sizeof(rec));
''',
     r'''{
	pic32mx_board board = { NULL, b_port_write, b_port_read, b_uart_tx, NULL, b_unmapped, b_exception, NULL, NULL };
	memset(&rec, 0, sizeof(rec));
'''),
])
edit('tests/pinheck/link/machine.c', [
    (r'''{
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, NULL, exception, hold };
	unsigned long long cycles = 800000000ull, first = 0, send_at = 0;
''',
     r'''{
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, NULL, exception, hold, NULL };
	unsigned long long cycles = 800000000ull, first = 0, send_at = 0;
'''),
])
EOF
```

- [ ] **Step 2: Run it to see it fail**

```sh
tests/pinheck/mips32/check.sh 2>&1 | grep -m2 -E 'error'
```
Expected: the suite's first build fails on the new callback in an initialiser (the check exits 2):
```text
run.c:121:71: error: excess elements in struct initializer [-Werror]
```

- [ ] **Step 3: Uncertain registers, port bits and samples**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/mips32/mips32.h', [
    (r'''	void (*irq_taken)(void *ctx, int vector);
} mips32_bus;
''',
     r'''	void (*irq_taken)(void *ctx, int vector);
	/* the true bits of an uncertain read (mips32_uncertain), in the read's value; 1 when known (always with wait) */
	int (*settle)(void *ctx, uint32_t token, int wait, uint32_t *bits);
} mips32_bus;
'''),
    (r'''	int dslot;                    /* direct region of the last fast load */
};
''',
     r'''	int dslot;                    /* direct region of the last fast load */
	unsigned exc_seq;             /* exceptions taken */
	/* a register loaded from a read whose bits prov_mask are not known yet (bus.settle gives them): every
	   instruction that reads it, but an AND that clears those bits, waits for them first */
	int prov, prov_kind;
	unsigned prov_set, prov_reg, prov_gen, prov_n;
	uint32_t prov_v, prov_mask, prov_tok, unc, unc_tok;
};
'''),
    (r'''uint32_t *mips32_regs(mips32_state *s);
void mips32_set_eic(mips32_state *s, int ripl, int vector, int srs);
''',
     r'''uint32_t *mips32_regs(mips32_state *s);
void mips32_uncertain(mips32_state *s, uint32_t mask, uint32_t token); /* from bus.read: bits of this read not known yet */
void mips32_settle(mips32_state *s);                                  /* a register not known yet gets its bits */
void mips32_set_eic(mips32_state *s, int ripl, int vector, int srs);
'''),
])
edit('src/cpu/mips32/mips32.c', [
    (r'''
	if (s->bus.exc_hook) {
''',
     r'''
	s->exc_seq++;
	if (s->bus.exc_hook) {
'''),
    (r'''
static void exec_mem(mips32_state *s, uint32_t op)
''',
     r'''
enum { PK_W, PK_B, PK_H, PK_BU, PK_HU };

static uint32_t prov_value(int kind, uint32_t v)
{
	switch (kind) {
	case PK_B: return (uint32_t)(int32_t)(int8_t)v;
	case PK_H: return (uint32_t)(int32_t)(int16_t)v;
	case PK_BU: return v & 0xFFu;
	case PK_HU: return v & 0xFFFFu;
	}
	return v;
}

/* the uncertain bits as they are in the register */
static uint32_t prov_regmask(const mips32_state *s)
{
	uint32_t m = s->prov_mask;
	switch (s->prov_kind) {
	case PK_B: return (m & 0x80u) ? m | 0xFFFFFF00u : m & 0xFFu;
	case PK_H: return (m & 0x8000u) ? m | 0xFFFF0000u : m & 0xFFFFu;
	case PK_BU: return m & 0xFFu;
	case PK_HU: return m & 0xFFFFu;
	}
	return m;
}

static void prov_apply(mips32_state *s, uint32_t bits)
{
	uint32_t v = (s->prov_v & ~s->prov_mask) | (bits & s->prov_mask);
	s->gpr[s->prov_set][s->prov_reg] = prov_value(s->prov_kind, v);
	s->prov = 0;
}

void mips32_settle(mips32_state *s)
{
	uint32_t bits = 0;
	if (!s->prov) return;
	if (s->bus.settle) s->bus.settle(s->bus.ctx, s->prov_tok, 1, &bits);
	prov_apply(s, bits);
}

void mips32_uncertain(mips32_state *s, uint32_t mask, uint32_t token)
{
	s->unc = mask;
	s->unc_tok = token;
}

/* register reg (current set) was loaded with v, whose bits s->unc are not known yet */
static void prov_new(mips32_state *s, unsigned reg, int kind, uint32_t v)
{
	uint32_t mask = s->unc;
	s->unc = 0;
	if (!reg) return;
	if (s->prov && !(s->prov_reg == reg && s->prov_set == (s->srsctl & 7))) mips32_settle(s);
	s->prov = 1;
	s->prov_kind = kind;
	s->prov_set = s->srsctl & 7;
	s->prov_reg = reg;
	s->prov_v = v;
	s->prov_mask = mask;
	s->prov_tok = s->unc_tok;
	s->prov_gen++;
	s->prov_n = 0;
}

static void exec_mem(mips32_state *s, uint32_t op)
'''),
    (r'''
	switch (op >> 26) {
	case 0x20: if (load(s, ea, 1, &v)) SET(RT(op), (uint32_t)(int32_t)(int8_t)v); break;
	case 0x21: if (load(s, ea, 2, &v)) SET(RT(op), (uint32_t)(int32_t)(int16_t)v); break;
	case 0x22:
		if (load(s, ea & ~3u, 4, &v)) {
			sh = (3 - b) * 8;
''',
     r'''
	s->unc = 0;
	switch (op >> 26) {
	case 0x20: if (load(s, ea, 1, &v)) { SET(RT(op), (uint32_t)(int32_t)(int8_t)v); if (s->unc) prov_new(s, RT(op), PK_B, v); } break;
	case 0x21: if (load(s, ea, 2, &v)) { SET(RT(op), (uint32_t)(int32_t)(int16_t)v); if (s->unc) prov_new(s, RT(op), PK_H, v); } break;
	case 0x22:
		if (load(s, ea & ~3u, 4, &v)) {
			if (s->unc) { uint32_t bits = 0; if (s->bus.settle) s->bus.settle(s->bus.ctx, s->unc_tok, 1, &bits); v = (v & ~s->unc) | (bits & s->unc); s->unc = 0; }
			sh = (3 - b) * 8;
'''),
    (r'''		break;
	case 0x23: if (load(s, ea, 4, &v)) SET(RT(op), v); break;
	case 0x24: if (load(s, ea, 1, &v)) SET(RT(op), v & 0xFFu); break;
	case 0x25: if (load(s, ea, 2, &v)) SET(RT(op), v & 0xFFFFu); break;
	case 0x26:
		if (load(s, ea & ~3u, 4, &v)) {
			sh = b * 8;
''',
     r'''		break;
	case 0x23: if (load(s, ea, 4, &v)) { SET(RT(op), v); if (s->unc) prov_new(s, RT(op), PK_W, v); } break;
	case 0x24: if (load(s, ea, 1, &v)) { SET(RT(op), v & 0xFFu); if (s->unc) prov_new(s, RT(op), PK_BU, v); } break;
	case 0x25: if (load(s, ea, 2, &v)) { SET(RT(op), v & 0xFFFFu); if (s->unc) prov_new(s, RT(op), PK_HU, v); } break;
	case 0x26:
		if (load(s, ea & ~3u, 4, &v)) {
			if (s->unc) { uint32_t bits = 0; if (s->bus.settle) s->bus.settle(s->bus.ctx, s->unc_tok, 1, &bits); v = (v & ~s->unc) | (bits & s->unc); s->unc = 0; }
			sh = b * 8;
'''),
    (r'''		break;
	case 0x30: if (load(s, ea, 4, &v)) { SET(RT(op), v); s->llbit = 1; } break;
	case 0x38:
''',
     r'''		break;
	case 0x30: if (load(s, ea, 4, &v)) { SET(RT(op), v); s->llbit = 1; if (s->unc) prov_new(s, RT(op), PK_W, v); } break;
	case 0x38:
'''),
    (r'''
static int irq_pending(const mips32_state *s)
''',
     r'''
/* The registers an instruction reads (a mask) and the one it writes when it completes (-1: none or not always);
   -2: it touches other register sets or changes the current one. */
#define R1(n) (1u << (n))
static int gpr_use(uint32_t op, uint32_t *rd)
{
	unsigned rs = RS(op), rt = RT(op), d = RD(op);
	*rd = R1(rs) | R1(rt);
	switch (op >> 26) {
	case 0x00:
		switch (FUNCT(op)) {
		case 0x00: case 0x02: case 0x03: *rd = R1(rt); return (int)d;
		case 0x04: case 0x06: case 0x07: return (int)d;
		case 0x08: *rd = R1(rs); return -1;
		case 0x09: *rd = R1(rs); return (int)d;
		case 0x0A: case 0x0B: *rd |= R1(d); return -1;
		case 0x0C: case 0x0D: case 0x0F: *rd = 0; return -1;
		case 0x10: case 0x12: *rd = 0; return (int)d;
		case 0x11: case 0x13: *rd = R1(rs); return -1;
		case 0x18: case 0x19: case 0x1A: case 0x1B: return -1;
		case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x2A: case 0x2B: return (int)d;
		case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x36: return -1;
		}
		return -2;
	case 0x01:
		*rd = R1(rs);
		if (rt >= 0x10 && rt <= 0x13) return 31;
		return -1;
	case 0x02: *rd = 0; return -1;
	case 0x03: *rd = 0; return 31;
	case 0x04: case 0x05: case 0x14: case 0x15: return -1;
	case 0x06: case 0x07: case 0x16: case 0x17: *rd = R1(rs); return -1;
	case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: *rd = R1(rs); return (int)rt;
	case 0x0F: *rd = 0; return (int)rt;
	case 0x10:
		if (op & (1u << 25)) { *rd = 0; return FUNCT(op) == 0x20 ? -1 : -2; }
		if (rs == 0x00 || rs == 0x0B) { *rd = 0; return (int)rt; }
		return -2;
	case 0x1C:
		switch (FUNCT(op)) {
		case 0x00: case 0x01: case 0x04: case 0x05: return -1;
		case 0x02: return (int)d;
		case 0x20: case 0x21: *rd = R1(rs); return (int)d;
		}
		return -2;
	case 0x1F:
		switch (FUNCT(op)) {
		case 0x00: *rd = R1(rs); return (int)rt;
		case 0x04: return (int)rt;
		case 0x20: *rd = R1(rt); return (int)d;
		case 0x3B: *rd = 0; return (int)rt;
		}
		return -2;
	case 0x20: case 0x21: case 0x23: case 0x24: case 0x25: case 0x30: *rd = R1(rs); return (int)rt;
	case 0x22: case 0x26: return (int)rt;
	case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2E: case 0x2F: case 0x33: return -1;
	case 0x38: return -1;
	}
	return -2;
}

/* before one instruction while a register is not known: it waits for its bits where it reads them (an AND that
   clears them does not); the register is known again when an instruction writes it without reading it. Returns
   the register the instruction writes for careful_post, or -1 */
typedef struct careful { unsigned gen, seq; int wr; } careful;

#if defined(__GNUC__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
static void careful_pre(mips32_state *s, careful *k)
{
	uint32_t bits = 0, op, rd, m, other;
	unsigned off = s->pc - s->fva, r = s->prov_reg;
	int wr, safe = 0;

	k->wr = -1;
	if (s->bus.settle && s->bus.settle(s->bus.ctx, s->prov_tok, 0, &bits)) { prov_apply(s, bits); return; }
	if (off >= s->fsize || (off & 3) || ++s->prov_n > 256) { mips32_settle(s); return; }
	op = le32(s->fptr + off);
	wr = gpr_use(op, &rd);
	if (wr == -2) { mips32_settle(s); return; }
	if ((s->srsctl & 7) != s->prov_set) return;
	if (rd >> r & 1) {
		m = prov_regmask(s);
		if ((op >> 26) == 0x00 && FUNCT(op) == 0x24 && RS(op) != RT(op)) {
			other = REGS(s)[RS(op) == r ? RT(op) : RS(op)];
			safe = !(other & m);
		} else if ((op >> 26) == 0x0C)
			safe = !(UIMM(op) & m);
		if (!safe) { mips32_settle(s); return; }
	}
	if (wr == (int)r) {
		k->wr = wr;
		k->gen = s->prov_gen;
		k->seq = s->exc_seq;
	}
}

static int irq_pending(const mips32_state *s)
'''),
    (r'''		}
		if (!s->waiting && fast_run(s, end < s->ti_at ? end : s->ti_at)) {
		} else if (s->waiting) {
''',
     r'''		}
		if (!s->waiting && !s->prov && fast_run(s, end < s->ti_at ? end : s->ti_at)) {
		} else if (s->waiting) {
'''),
    (r'''			s->cycles += burn;
		} else
			step(s);
		if (s->cycles >= s->ti_at) {
''',
     r'''			s->cycles += burn;
		} else {
			careful k;
			k.wr = -1;
			if (s->prov) careful_pre(s, &k);
			step(s);
			/* written whole without being read: known */
			if (k.wr >= 0 && s->prov && s->prov_gen == k.gen && s->exc_seq == k.seq) s->prov = 0;
		}
		if (s->cycles >= s->ti_at) {
'''),
    (r'''{
	s->pc = 0xBFC00000u;
''',
     r'''{
	s->prov = 0;
	s->unc = 0;
	s->pc = 0xBFC00000u;
'''),
])
edit('src/cpu/pic32mx/pic32mx.h', [
    (r'''	uint64_t (*hold)(void *ctx, uint64_t cycle); /* cycles the core must stay held, 0 = run */
} pic32mx_board;
''',
     r'''	uint64_t (*hold)(void *ctx, uint64_t cycle); /* cycles the core must stay held, 0 = run */
	/* the true bits of a port read that pic32mx_uncertain marked, in port_read's value; 1 when known (always with wait) */
	int (*port_settle)(void *ctx, uint32_t token, int wait, uint32_t *bits);
} pic32mx_board;
'''),
    (r'''	uint64_t run_end; /* cycle at which the current pic32mx_run ends */
} pic32mx;
''',
     r'''	uint64_t run_end; /* cycle at which the current pic32mx_run ends */
	uint32_t unc, unc_tok; /* port_read's bits not known yet (pic32mx_uncertain) */
} pic32mx;
'''),
    (r'''void pic32mx_set_irq(pic32mx *p, int irq);
int pic32mx_irq_vector(int irq);
''',
     r'''void pic32mx_set_irq(pic32mx *p, int irq);
void pic32mx_uncertain(pic32mx *p, uint32_t mask, uint32_t token); /* from port_read: these bits are settled later */
int pic32mx_irq_vector(int irq);
'''),
])
edit('src/cpu/pic32mx/pic32mx.c', [
    (r'''		uint32_t in = 0xFFFFu;
		if (p->board.port_read) { host_enter(p); in = p->board.port_read(p->board.ctx, port, p->cpu.cycles); host_leave(p); }
		if (port == PIC32MX_PORTB) in &= SFR(p, OFF_AD1PCFG);
		return (SFR(p, OFF_TRISA + (uint32_t)port * 0x40 + 0x20) & ~tris) | (in & tris);
''',
     r'''		uint32_t in = 0xFFFFu;
		p->unc = 0;
		if (p->board.port_read) { host_enter(p); in = p->board.port_read(p->board.ctx, port, p->cpu.cycles); host_leave(p); }
		if (port == PIC32MX_PORTB) { in &= SFR(p, OFF_AD1PCFG); p->unc &= SFR(p, OFF_AD1PCFG); }
		p->unc &= tris;
		return (SFR(p, OFF_TRISA + (uint32_t)port * 0x40 + 0x20) & ~tris) | (in & tris);
'''),
    (r'''	if (pa >= PIC32MX_SFR_BASE && pa - PIC32MX_SFR_BASE < PIC32MX_SFR_SIZE) {
		uint32_t off = pa - PIC32MX_SFR_BASE;
		return (sfr_read(p, off & ~3u) >> ((off & 3) * 8)) & (size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1);
	}
''',
     r'''	if (pa >= PIC32MX_SFR_BASE && pa - PIC32MX_SFR_BASE < PIC32MX_SFR_SIZE) {
		uint32_t off = pa - PIC32MX_SFR_BASE, sm = size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1;
		v = (sfr_read(p, off & ~3u) >> ((off & 3) * 8)) & sm;
		if (p->unc) {
			uint32_t m = (p->unc >> ((off & 3) * 8)) & sm;
			p->unc = 0;
			if (m) mips32_uncertain(&p->cpu, m, p->unc_tok << 2 | (off & 3));
		}
		return v;
	}
'''),
    (r'''
void pic32mx_init(pic32mx *p, const pic32mx_board *board, const uint8_t *flash, uint32_t flash_size)
''',
     r'''
void pic32mx_uncertain(pic32mx *p, uint32_t mask, uint32_t token)
{
	p->unc = mask;
	p->unc_tok = token;
}

static int soc_settle(void *ctx, uint32_t token, int wait, uint32_t *bits)
{
	pic32mx *p = (pic32mx *)ctx;
	uint32_t in = 0;
	int r;
	if (!p->board.port_settle) return 1;
	r = p->board.port_settle(p->board.ctx, token >> 2, wait, &in);
	*bits = in >> ((token & 3) * 8);
	return r;
}

void pic32mx_init(pic32mx *p, const pic32mx_board *board, const uint8_t *flash, uint32_t flash_size)
'''),
    (r'''	memset(p, 0, sizeof(*p));
	if (board) p->board = *board;
''',
     r'''	memset(p, 0, sizeof(*p));
	memset(&bus, 0, sizeof(bus));
	if (board) p->board = *board;
'''),
    (r'''	bus.irq_taken = irq_taken;
	mips32_init(&p->cpu, &bus, 2, 0x00018700u);
''',
     r'''	bus.irq_taken = irq_taken;
	bus.settle = soc_settle;
	mips32_init(&p->cpu, &bus, 2, 0x00018700u);
'''),
])
edit('src/cpu/pic32mx/pic32mxcpu.c', [
    (r'''unsigned pic32cpu_get_reg(int regnum)
{
	switch (regnum) {
	case REG_PC: case PIC32CPU_PC: return soc.cpu.pc;
''',
     r'''unsigned pic32cpu_get_reg(int regnum)
{
	mips32_settle(&soc.cpu);
	switch (regnum) {
	case REG_PC: case PIC32CPU_PC: return soc.cpu.pc;
'''),
    (r'''void pic32cpu_set_reg(int regnum, unsigned val)
{
	switch (regnum) {
	case REG_PC: case PIC32CPU_PC: soc.cpu.pc = val; soc.cpu.npc = val + 4; soc.cpu.delay = 0; return;
''',
     r'''void pic32cpu_set_reg(int regnum, unsigned val)
{
	mips32_settle(&soc.cpu);
	switch (regnum) {
	case REG_PC: case PIC32CPU_PC: soc.cpu.pc = val; soc.cpu.npc = val + 4; soc.cpu.delay = 0; return;
'''),
])
edit('src/wpc/pinheck/prop.h', [
    (r'''#define PROP_PIC_PINS ((1u << 24) | (1u << 25) | (1u << 26))

''',
     r'''#define PROP_PIC_PINS ((1u << 24) | (1u << 25) | (1u << 26))
#define PROP_SAMPS 1024

'''),
    (r'''	void *worker; /* worker thread, NULL = calls run inline */
	uint64_t owner; /* the thread that started the worker: the only one whose prop_sync waits for it */
''',
     r'''	void *worker; /* worker thread, NULL = calls run inline */
	uint32_t samp_post, samp_cmd[PROP_SAMPS]; /* P24 samples (prop_sample): posted, and the queue position of each */
	volatile uint32_t samp_done;
	uint8_t samp_val[PROP_SAMPS];
	uint64_t owner; /* the thread that started the worker: the only one whose prop_sync waits for it */
'''),
    (r'''int prop_p24(pinheck_prop *p, uint64_t pic_cycle);
void prop_set_clock(pinheck_prop *p, prop_clock_fn fn, void *ctx);
''',
     r'''int prop_p24(pinheck_prop *p, uint64_t pic_cycle);
uint32_t prop_sample(pinheck_prop *p, uint64_t pic_cycle);      /* with the worker: prop_p24 left to it; a token */
int prop_sample_get(pinheck_prop *p, uint32_t token, int wait); /* its P24 (0, 1); -1 not yet (wait 0) */
void prop_set_clock(pinheck_prop *p, prop_clock_fn fn, void *ctx);
'''),
])
edit('src/wpc/pinheck/prop.c', [
    (r'''
enum { CMD_PINS, CMD_CATCH_UP, CMD_QUIT };

''',
     r'''
enum { CMD_PINS, CMD_CATCH_UP, CMD_SAMPLE, CMD_QUIT };

'''),
    (r'''#ifdef PINHECK_NO_THREADS
int prop_start_thread(pinheck_prop *p) { (void)p; return -1; }
''',
     r'''#ifdef PINHECK_NO_THREADS
uint32_t prop_sample(pinheck_prop *p, uint64_t pic_cycle) { (void)p; (void)pic_cycle; return 0; }
int prop_sample_get(pinheck_prop *p, uint32_t token, int wait) { (void)p; (void)token; (void)wait; return 0; }
int prop_start_thread(pinheck_prop *p) { (void)p; return -1; }
'''),
    (r'''	if (c->kind == CMD_CATCH_UP) do_catch_up(p, c->pic);
	else do_pic_pins(p, c->pic, c->pins);
}
''',
     r'''	if (c->kind == CMD_CATCH_UP) do_catch_up(p, c->pic);
	else if (c->kind == CMD_SAMPLE) {
		/* prop_p24's catch-up and read, on this thread */
		uint32_t dir, out;
		do_catch_up(p, c->pic);
		out = p8x32a_pins(&p->chip, p->chip.now, &dir);
		p->samp_val[c->pins % PROP_SAMPS] = (uint8_t)((dir & PIN_P24) ? (out & PIN_P24) != 0 : 0);
		put_rel(&p->samp_done, c->pins);
	} else do_pic_pins(p, c->pic, c->pins);
}
'''),
    (r'''	p->worker = NULL;
}
''',
     r'''	p->worker = NULL;
}

/* P24 at pic_cycle as prop_p24 reads it, read by the worker: the caller goes on and asks prop_sample_get later */
uint32_t prop_sample(pinheck_prop *p, uint64_t pic_cycle)
{
	prop_worker *w = (prop_worker *)p->worker;
	uint32_t k = ++p->samp_post;
	p->samp_cmd[k % PROP_SAMPS] = w->tail;
	post(p, CMD_SAMPLE, pic_cycle, k);
	return k;
}

int prop_sample_get(pinheck_prop *p, uint32_t token, int wait)
{
	if ((int)(get_acq(&p->samp_done) - token) < 0) {
		prop_worker *w = (prop_worker *)p->worker;
		if (!wait) return -1;
		if (w) wait_head(w, p->samp_cmd[token % PROP_SAMPS] + 1);
		get_acq(&p->samp_done);
	}
	return p->samp_val[token % PROP_SAMPS];
}
'''),
])
edit('src/wpc/pinheck.c', [
    (r'''
static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
''',
     r'''
/* RF13 (P24) with the worker thread: read by the worker, and settled only for an instruction that uses it
   (mips32_uncertain); its guess is the last value settled */
static int rf13_guess, rf13_exact = -1;

static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
'''),
    (r'''	if (port != PIC32MX_PORTF) return v;
	return (v & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}
''',
     r'''	if (port != PIC32MX_PORTF) return v;
	if (rf13_exact < 0) rf13_exact = getenv("PINHECK_RF13") && atoi(getenv("PINHECK_RF13")) == 0;
	if (prop.worker && !rf13_exact) {
		pic32mx_uncertain(pic32cpu_soc(), RF13, prop_sample(&prop, cycle));
		return (v & ~RF13) | (rf13_guess ? RF13 : 0);
	}
	return (v & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}

static int pinheck_port_settle(void *ctx, uint32_t token, int wait, uint32_t *bits)
{
	int v = prop_sample_get(&prop, token, wait);
	(void)ctx;
	if (v < 0) return 0;
	rf13_guess = v;
	*bits = v ? RF13 : 0;
	return 1;
}
'''),
    (r'''{
	pic32mx_board board = { NULL, pinheck_port_write, pinheck_port_read, pinheck_uart_tx, pinheck_i2c_pins, pinheck_unmapped, pinheck_exception, pinheck_hold };
	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");
''',
     r'''{
	pic32mx_board board = { NULL, pinheck_port_write, pinheck_port_read, pinheck_uart_tx, pinheck_i2c_pins, pinheck_unmapped, pinheck_exception, pinheck_hold, pinheck_port_settle };
	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");
'''),
])
EOF
```

- [ ] **Step 4: The suites**

```sh
nice -n 15 tests/pinheck/mips32/check.sh | tail -2
nice -n 15 tests/pinheck/pic32mx/check.sh | tail -1
PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 tests/pinheck/link/check.sh | tail -1
```
Expected:
```text
uncertain: 20000 programs, 31994 interrupt raises, 0 differ
mips32: 208 passed, 0 failed
pic32mx: 0 failed
link: 0 failed
```

- [ ] **Step 5: The test bites**

Each mutation of the careful step must make programs differ:

```sh
mutm() { cp src/cpu/mips32/mips32.c build/m.c && python3 -c "import sys; p=sys.argv[1]; s=open(p).read(); a,b=sys.argv[2],sys.argv[3]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" build/m.c "$1" "$2" &&
	cc -O2 -std=c99 -Isrc/cpu/mips32 -o build/uncm tests/pinheck/mips32/uncertain_test.c build/m.c && ./build/uncm | tail -1; }
mutm '		if (!safe) { mips32_settle(s); return; }' '		(void)safe;'
mutm '	if (wr == -2) { mips32_settle(s); return; }' '	if (wr == -2) return;'
mutm '	if (s->bus.settle && s->bus.settle(s->bus.ctx, s->prov_tok, 0, &bits)) { prov_apply(s, bits); return; }' '	if (s->bus.settle && s->bus.settle(s->bus.ctx, s->prov_tok, 0, &bits)) { s->prov = 0; return; }'
mutm '			safe = !(other & m);' '			safe = 1;'
```
Expected (each a nonzero count; the counts are those of the replay):
```text
uncertain: 20000 programs, 31994 interrupt raises, 2777 differ
uncertain: 20000 programs, 31994 interrupt raises, 770 differ
uncertain: 20000 programs, 31994 interrupt raises, 1108 differ
uncertain: 20000 programs, 31994 interrupt raises, 274 differ
```

- [ ] **Step 6: America's Most Haunted with the worker thread**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12.log 2>&1; tail -1 build/sdl3pinmame/12.log
amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -v ' ref:'
```
Expected (about 2 minutes):
```text
[100%] Built target sdl3pinmame
determinism first boot: identical nvram.base/amh.nv
bench short opt: 1.037x over 10.0 s emulated (9.6 s wall), whole run 13.5 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.898x, load 2.24
determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 7: Commit**

```bash
git add src/cpu/mips32/mips32.h src/cpu/mips32/mips32.c src/cpu/pic32mx/pic32mx.h src/cpu/pic32mx/pic32mx.c src/cpu/pic32mx/pic32mxcpu.c src/wpc/pinheck/prop.h src/wpc/pinheck/prop.c src/wpc/pinheck.c tests/pinheck/mips32/uncertain_test.c tests/pinheck/mips32/check.sh tests/pinheck/mips32/unit_test.c tests/pinheck/mips32/run.c tests/pinheck/pic32mx/boot.c tests/pinheck/pic32mx/soc_test.c tests/pinheck/link/machine.c
git commit -m "pinheck: RF13 read by the worker and settled only where the PIC32 uses it (uncertain register bits in the MIPS32 core)"
```

### Task 5: Speed, regression, the tests bite, the documents

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

**Interfaces:**
- Consumes: everything above; `build/reference/sdl3pinmame`, `build/libpinmame/libpinmame.so`.
- Produces: the Result's measurements; spec §4's paragraph for Plan 12; the roadmap's row.

- [ ] **Step 1: Every game against the reference, with the worker thread**

```sh
for g in amh jet rz; do $g env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video play; done
REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video
```
Expected: every `determinism` line `identical` (America's Most Haunted's with `dmd.bin`), no `FAIL`; the speeds are the Result's table (about 25 minutes, the reference's runs included). For example:
```text
determinism first boot: identical nvram.base/amh.nv
bench attract opt: 1.077x over 30.0 s emulated (27.8 s wall), whole run 15.2 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.752x, load 1.52
bench attract ref: 0.565x over 30.0 s emulated (53.0 s wall), whole run 22.9 G instructions and 3.1 s CPU per emulated s, worst 100 ms 0.482x, load 2.00
determinism attract: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
```

- [ ] **Step 2: On one thread; the switches give the same output**

```sh
for g in amh jet; do $g env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame PINHECK_THREADS=0 flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract | sed 's/^/one thread: /'; done
for s in PINHECK_LAZY=0 PINHECK_RF13=0 PINHECK_JIT=0; do amh env $s REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short | grep -E 'determinism short|opt:' | sed "s/^/$s: /"; done
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh contention | tail -5
```
Expected (about 8 minutes; the reference's lines come from the runs of Step 1):
```text
one thread: bench attract opt: 0.741x over 30.0 s emulated (40.5 s wall), whole run 14.8 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.521x, load 1.57
one thread: determinism attract: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
one thread: bench attract opt: 0.799x over 30.0 s emulated (37.5 s wall), whole run 16.2 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.541x, load 1.33
one thread: determinism attract: identical uart.log frames.bin snd.wav nvram/jetsons.nv
PINHECK_LAZY=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_RF13=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
PINHECK_JIT=0: determinism short: identical uart.log frames.bin dmd.bin snd.wav nvram/amh.nv
contention: ok
```
(with the `one thread: … ref:` and `first boot` lines, each `PINHECK_…=0: bench short opt:` speed, and the four `contention …` lines between them).

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
LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | tail -1
jet env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | tail -1
for s in audio board display storage; do flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh 2>&1 | tail -1; done
D=tests/pinheck/storage/build; rm -rf "${D:?}"
```
Expected (about 35 minutes):
```text
pinmame: ok
pinmame display: ok
pinmame board: ok
pinmame sim: ok
```
four times (America's Most Haunted, Domino's, The Jetsons, Rob Zombie), then
```text
romset: ok
game: 44 checks, 0 failures
pinmame game: ok
pinmame audio: ok
pinmame look: ok
pinmame vpx: ok
pinmame vpx: ok (jetsons: display and sound)
audio: 0 failed
board: 0 failed
display: 0 failed
storage: 0 failed
```

- [ ] **Step 4: The machine-level tests bite**

A lazy-cog mutation the RTL test catches must also fail America's Most Haunted's determinism, and an uncertain-bit mutation must fail it with the worker:

```sh
mut() { f=$1; python3 -c "import sys; p=sys.argv[1]; s=open(p).read(); a,b=sys.argv[2],sys.argv[3]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" "$f" "$2" "$3" || return
	flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12.log 2>&1; tail -1 build/sdl3pinmame/12.log
	amh env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3000 tests/pinheck/perf/bench.sh short 2>&1 | grep -m2 FAIL; git checkout $f; }
mut src/cpu/p8x32a/p8x32a.c '		if (p->hub[ha + k] != (uint8_t)(v >> (8 * k))) { lazy_catch(p, h - 1); return; }' '		if (p->hub[ha + k] != (uint8_t)(v >> (8 * k))) return;'
mut src/cpu/mips32/mips32.c '		if (!safe) { mips32_settle(s); return; }' '		(void)safe;'
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/12.log 2>&1; tail -1 build/sdl3pinmame/12.log
git status --short | grep -v '^??'
```
Expected (about 3 minutes; `git status` prints nothing):
```text
[100%] Built target sdl3pinmame
DETERMINISM FAIL short: frames.bin differs from the reference build's
DETERMINISM FAIL short: dmd.bin differs from the reference build's
Updated 1 path from the index
[100%] Built target sdl3pinmame
DETERMINISM FAIL first boot: nvram.base/amh.nv differs from the reference build's
DETERMINISM FAIL short: uart.log differs from the reference build's
Updated 1 path from the index
[100%] Built target sdl3pinmame
```

| Mutation | Change | Caught by |
|---|---|---|
| the lazy cog reads hub RAM after a write it should precede | `lazy_write` returns without catching up | `DETERMINISM FAIL short: frames.bin differs from the reference build's` (and `dmd.bin`) |
| an instruction reads the uncertain register with the guessed bit | careful step: no settle before a read | `DETERMINISM FAIL first boot: nvram.base/amh.nv differs from the reference build's` (and `uart.log`) |

and in the unit suites (Tasks 3 and 4) the RTL test catches four lazy-cog mutations (no catch-up at a write, no catch-up for a masked `INA`, no exit on a wait, no exit on another cog's `DIRA`), the differential test four careful-step mutations. The build is restored at the end.

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
     r'''- **Established by Plan 12 (America's Most Haunted and The Jetsons):**
  - America's Most Haunted's raw DMD is scanned by a cog that never idles (3.2 million dots a second, three `OUTA` writes each); on one thread the game took 22.2 G host instructions per emulated second (Domino's 11.5 G), and with the worker thread it was no faster than without it: the PIC32 read `PORTF` 197,000 times a second (99.4% the cabinet switch routine, which uses `RF0`) and each read waited for the Propeller.
  - *Lazy cogs* (`p8x32a.c`): a cog whose reachable code only reads hub RAM, `PAR` and `CNT`, computes in cog RAM, jumps to fixed addresses and writes `OUTA` on pins no other cog or counter drives and only the host's lazy path watches leaves the schedule and runs behind the others: it catches up before another cog's hub write that changes a byte, before an `INA` read whose mask includes its pins, and at the end of `p8x32a_run_until`; its pin changes go to `bus.lazy_pins` in time order; a wait or idle-loop sleep on its pins, another cog driving them or a cog operation on it ends it. Its blocks are translated with their `OUTA` writes and hub reads. The P1 RTL, a new chip test (three entries and exits) and every chip, ISA and random test with lazy cogs allowed match, interpreted and translated. America's Most Haunted on one thread: 14.8 G (from 22.2 G).
  - *Uncertain `RF13` reads* (`mips32.c`, `pic32mx.c`, `prop.c`): with the worker thread, a `PORTF` read takes `RF13` from the last value known, marks the bit uncertain and leaves the true `P24` to the worker in queue order (`CMD_SAMPLE`, the catch-up `prop_p24` made); the MIPS32 core settles the bit before any instruction that reads the register, except an `AND`/`ANDI` that clears it, and drops it when the register is written. A differential test of 20,000 random programs (loads of every size, interrupts into a shadow set) matches runs with the true value. The PIC32 now waits about 600 times a second in attract mode, from 197,000.
  - Every output byte-identical to Plan 11c's build (UART1, frames and the raw DMD's subframes, sound, NVRAM) for the four games with the worker, without it, without lazy cogs (`PINHECK_LAZY=0`), without uncertain reads (`PINHECK_RF13=0`) and without the translator. Speed with the worker (loads 1.3–2.0): America's Most Haunted 1.08× attract, 0.82× video, 0.76× play (from 0.556×, 0.472×, 0.440×); The Jetsons 0.98×, 0.89×, 0.83× (the reference in the same runs 0.94×, 0.88×, 0.84×); Domino's 1.37× and 1.20×; Rob Zombie 1.60×, 1.33×, 1.21×. Real time is reached for America's Most Haunted in attract mode only, with a thin margin; not with video, not in play, and not for The Jetsons (whose display link, a cog that writes hub RAM and drives its pins with counters, cannot be lazy). What bounds them is the Propeller thread's ordinary work: three Spin interpreters' hub operations and the SD driver's events in America's Most Haunted, the display link's counter and `PHS` writes in The Jetsons.
- **Proof for every stage:**'''),
])

edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    (r'''| release | release:''',
     r'''| `2026-10-01-pinheck-12-speed.md` | 12: America's Most Haunted and The Jetsons faster: lazy cogs (an output-only cog off the schedule, its pins on their own path, translated with its `OUTA` writes and hub reads; America's Most Haunted's scan cog), uncertain `RF13` reads (the PIC32 waits for `P24` only where it uses the bit), mask-aware `INA`; Plan 11c's review minors | Plan 11c | none | every output byte-identical to Plan 11c's build for the four games and every switch; the RTL, spinsim and QEMU suites green with a lazy-cog chip test and a differential test of uncertain loads; speeds in the plan's Result | written |
| release | release:'''),
    (r'''- After Plan 11b: `jetsons` runs below real time without a profile:''',
     r'''- After Plan 12: with the worker thread, without a profile, America's Most Haunted runs 1.08× / 0.82× / 0.76× (attract, video, play) and The Jetsons 0.98× / 0.89× / 0.83×. What remains for real time with margin there: the Propeller thread's ordinary events (three Spin interpreters' hub operations through the scheduler; the SD driver's `OUTA`/`INA` events, which could become local with the SD card as a device bound to its cog; The Jetsons' frame sender's counter writes as "private pins"; hub reads that commute with the other cogs' next operations), a hub-write journal so the lazy cog catches up less often, and the profile-guided build (8–10%).
- After Plan 11b: `jetsons` runs below real time without a profile:'''),
])
EOF
```

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "docs: what Plan 12 established (lazy cogs, uncertain RF13 reads) and its roadmap row"
```

## Windows checklist

For the Windows session (VS2022 BuildTools / MSVC 14.44, `C:\code\dominos`), after Plan 12 is pushed. Results come back by message; a Windows-only fix comes back as a diff with a test, applied on Linux. Never push. Muted or the silent device for every item.

1. **Build.** MSVC x64 and Win32 standalone PinMAME, VPinMAME64 and libpinmame, exactly as CI does. Expected: all build; no warning in `p8x32a.c`, `p8x32ajit.cpp`, `mips32.c`, `pic32mx.c`, `prop.c`, `pinheck.c`, `dmd.c` (`P8_INLINE`/`P8_COLD` use `__forceinline`/`__declspec(noinline)` under MSVC).
2. **Determinism across compilers.** The scripted Domino's game (`tests\pinheck\game`, as for Plan 9d): the score 32176700 and the four logs as on Linux.
3. **Speed, with and without the new mechanisms.** `PinMAME.exe amh` and `PinMAME.exe jetsons`, unthrottled (`-nothrottle`), 60 s of attract mode each with `PINHECK_TIME_LOG=time.log`: the last line's emulated over host seconds; then the same for `amh` with `PINHECK_LAZY=0`, with `PINHECK_RF13=0`, and with both. Linux gives the Result's figures; the Windows box ran at 0.7–0.8× of Linux for Domino's.
4. **Thread placement.** `start /affinity 1 PinMAME.exe amh` (one CPU): no collapse (Plan 9d's guard: no worker on one CPU), the display moves.
5. **VPinMAME smoke test for `amh`** (Plan 11c's item 8): the DMD shows 16 shades and moves; sound plays.

## Result

Measured in the replay of this plan's text: a fresh worktree of `8fa0a5e5` (branch `pinheck-12`), every step run as written, every Expected line checked, on the Ryzen 7 7730U while other agents' work shared it (one emulator at a time, `emu.lock`); builds without a profile; the load average is printed with every speed.

- **Task 1:** the reference against itself byte-identical, the raw DMD's subframes (`dmd.bin`) now compared.
- **Task 2:** the two-game session failed first (`VPX FAIL: 2 hex lines over amh and dominos`) and passes after the fix; the raw frames (`PINMAME_DMD_MODE_RAW`, 600 of 600) and both brightness paths (1,200 of 1,200 each) equal the sums of the last 16 subframes; the display, VPX and board checks pass.
- **Task 3:** the RTL test failed first (build), then the Propeller suite: 234 passed, 0 failed, interpreted and translated (the lazy chip test enters lazy mode three times; four mutations of the lazy core, each caught by the RTL comparison, were measured in the prototype). America's Most Haunted on one thread: 13.1 G host instructions per emulated second in the short run (20.2 G), byte-identical with `dmd.bin`.
- **Task 4:** the differential test failed first (build), then 20,000 programs, 0 differ, and each of four careful-step mutations makes programs differ (2,777, 770, 1,108, 274); the MIPS32 (208 passed, QEMU), PIC32MX and link suites pass. America's Most Haunted with the worker: 1.04× in the short run, byte-identical.
- **Task 5, speed with the worker thread** (each workload against the reference in the same session; every determinism line identical):

| Game | Attract | Video | Play | Reference: attract, video, play |
|---|---|---|---|---|
| America's Most Haunted | 1.077× (15.2 G) | 0.821× (17.2 G) | 0.758× (19.3 G) | 0.565×, 0.484×, 0.446× (22.9, 25.0, 27.5 G) |
| The Jetsons | 0.979× | 0.891× | 0.834× | 0.939×, 0.875×, 0.843× |
| Rob Zombie | 1.596× | 1.327× | 1.213× | 1.445×, 1.239×, 1.115× |
| Domino's | 1.366× | 1.195× | — | 1.275×, 1.107× |

  Loads 1.3–2.0; instruction counts with the worker include its spinning. On one thread: America's Most Haunted 0.741× attract at 14.8 G (the reference 22.2 G), The Jetsons 0.799× at 16.2 G. With `PINHECK_LAZY=0`, `PINHECK_RF13=0` or `PINHECK_JIT=0` America's Most Haunted's short run is byte-identical (0.67×, 0.79×, 0.88×: each mechanism pays on its own); `bench.sh contention` passes for America's Most Haunted (one CPU, a busy CPU, switching every second, a stall).
- **Task 5, regression:** the boot, display, service-test and simulator checks of all four games, the romset check, Domino's scripted game (44 checks, 0 failures), the audio, look and libpinmame checks (Domino's and The Jetsons; America's Most Haunted's in Task 2) and the audio, board, display and storage suites pass; the two machine-level mutations fail America's Most Haunted's determinism (`frames.bin` and `dmd.bin`; the first boot's NVRAM and `uart.log`).
- **Gain per candidate** (America's Most Haunted, attract): candidate 1 as lazy cogs, 22.2 → 14.8 G on one thread, 0.56× → 0.74× (it is what makes the PIC32's waits the bound); candidate 3 as uncertain `RF13` reads, 0.74× (one thread) or 0.79× (worker, `PINHECK_RF13=0`) → 1.08× with the worker; candidate 2 (a model of the scan cog) not built, its gain would be at most the lazy cog's remaining cost (13.3 G against 12.5 G with the lazy cog not run at all, a measurement only); candidate 4: mask-aware `INA` (video 17.6 → 17.0 G), the decoder's latch (Plan 11c's minor). For The Jetsons only candidate 3 applies (its link reads use `RF13`; the worker's own sampling gains 2–5% in attract mode and with video, none in play).

**What remains.** America's Most Haunted with video (0.82×) and in play (0.76×), and The Jetsons in all three workloads (0.83–0.98×), are below real time without a profile; America's Most Haunted's attract mode is at 1.08×. Every one of these is bound by the Propeller thread (the PIC32 waits about 600 times a second). The next levers, measured or estimated in the prototype: (1) the SD driver's `OUTA`/`INA` events made local by binding the SD card device to its cog (its responses depend only on that cog's pins; 2.4 million events a second with video in America's Most Haunted); (2) The Jetsons' frame sender's counter, `PHS` and `OUTA` writes as "private pins" (estimated 5–6%); (3) hub reads that commute with the other cogs' pending hub operations (three Spin interpreters interleave about 6 million hub operations a second through the scheduler); (4) a hub-write journal so the lazy cog catches up less often (its catch-ups at writes cost about 0.7 G); (5) the profile-guided build (8–10% in Plans 9d and 11c, not re-measured here).
