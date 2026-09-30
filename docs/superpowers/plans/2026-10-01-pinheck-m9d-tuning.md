# Milestone 9d: performance tuning (bounded waits, one-CPU guard, worker governor, profile-guided builds) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Domino's keeps its real-time margin where CPUs are scarce and gains speed on every machine, with every output byte-identical to Plan 9c's build: the two emulation threads wait for each other with a bounded spin measured by the clock, the worker thread is not started when the process may use only one CPU, the driver runs the Propeller inline for as long as that is faster (an oversubscribed machine), and `sdl3pinmame`, libpinmame (GCC, Clang, MSVC) and the Windows standalone `PinMAME.exe` (MSVC) can be built with profile-guided optimisation.

**Architecture:** Measurement first (Rulings 1–4): after Plan 9c the PIC32 thread waits for the Propeller worker at about 28,200 syncs per emulated second (27,074 of them `RF13` reads) and the worker idles about 12% of its time, but neither cache-line traffic nor the idle time is a lever that pays: padding the queue, answering `RF13` on the worker, running the worker ahead of the PIC32's calls and thread placement were prototyped and measured without gain on this machine, and the Windows measurements show the scheduler already keeps both threads in one CCX. What does not hold is the handoff under contention: with one allowed CPU the threaded path runs at 0.044–0.073× (inline about 0.8–1.0×), and with two CPUs shared with a busy process at 0.055×. `prop.c` now spins at most 50 µs by the clock before it blocks (Task 2) and does not start the worker when the affinity mask allows one CPU (Task 2); the machine driver switches the worker off for as long as the inline path is faster, measured over seconds of host time (Task 3); `bench.sh contention` checks all three. Profile-guided builds (Task 4) are an option of the CMake lists (`cmake/pgo.cmake`) with a training script (`pgo.sh`), measured at +8–10% on Linux (1.39× attract and 1.22× video at loads 2.4–2.8); the profile is never committed. The Propeller core, the translator, the PIC32 core and every device block are unchanged.

**Tech Stack:** C (C89-syntax-clean `prop.c` and driver, `-std=c99 -Wall -Wextra -Werror -pedantic` test builds, mingw-w64 + wine for the Windows worker), CMake (GCC 15 `-fprofile-generate`/`-fprofile-use`, Clang with `llvm-profdata`, MSVC `/GENPROFILE`/`/USEPROFILE`), `perf` 7.1, `taskset`, Python 3 (no third-party modules), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §4 (binding): real time with margin (aim ≥ 1.2× for attract and video), byte-identical determinism, all suites green including the RTL, spinsim and QEMU differentials. Parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` §5 and §7 where the addendum is silent. Plans 9, 9b and 9c (`docs/superpowers/plans/2026-09-29-pinheck-m9-performance.md`, `2026-09-29-pinheck-m9b-realtime.md`, `2026-09-30-pinheck-m9c-realtime.md`) provide the benchmark, the worker thread and the determinism method.

## Prerequisites

- Plans 9b, 9c and 10 executed, reviewed and merged, and the libpinmame MinGW fix after them: `pinheck` at `769d7dc0`. Proven and replayed on exactly that commit.
- This plan edits Plan 9b's `prop.c` and `prop_test.c`, the machine driver `pinheck.c`, Plan 9's `tests/pinheck/perf/bench.sh`, `cmake/sdl3pinmame/CMakeLists.txt`, `cmake/libpinmame/CMakeLists.txt` and `cmake/pinmame/CMakeLists_win-x64.txt`, and adds `cmake/pgo.cmake` and `tests/pinheck/perf/pgo.sh`. The `p8x32a` and `mips32` cores, the translator, the PIC32 SoC, the board, display, audio and storage blocks do not change.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools` (or the main checkout's, where the oracles are built); `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip; `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`; `SDL_AUDIO_DRIVER=dummy`. Every command below uses these.
- `perf` may count user-space events (`sudo sysctl kernel.perf_event_paranoid=1` after a reboot). `taskset` (util-linux) runs the contention checks; mingw-w64 and wine run the Windows worker's unit test (`PINHECK_WINE=1`).
- `bench.sh contention` pins its runs to CPUs 2 and 4 (`BENCH_CPU_A`, `BENCH_CPU_B` choose others) and runs a busy loop on the first for about a minute; the rest of the machine stays free.
- The reference machine is shared: other agents' emulators and builds run beside these steps, at times seven emulators at nice 5 (load averages up to 9). Wall-clock speeds were measured at the load averages printed with them; paired runs alternate the builds; the one-thread instruction counts do not depend on the load. Builds run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6` (on another machine drop the `flock` prefix). Every PinMAME run has a `timeout -k 30`.

## Global Constraints

- Exactness: nothing this plan changes alters the machine's behaviour. `bench.sh` with `REFERENCE` requires the UART1 log, the display frame log, the sound capture and the NVRAM of each workload to equal the reference build's byte for byte, with the worker thread on (the default) and off (`PINHECK_THREADS=0`), and for the profile-guided build; `bench.sh contention` requires the same outputs with and without the worker on one CPU, on two contended CPUs and with the worker switched every second. The scripted game's link, board, UART1 and frame logs equal the reference's.
- The reference build is `pinheck` at `769d7dc0`, built Release exactly as below and copied to `build/reference/sdl3pinmame` before any source changes.
- `prop.c` and the driver stay C, C89-syntax-clean (`-std=c89 -pedantic-errors -Wno-long-long`, which the link suite gates, also with `PINHECK_NO_THREADS`) and warning-free in the test builds; `prop.c` includes no PinMAME headers. No new source file joins any build.
- Test hooks stay inert unless set. One new test hook, `PINHECK_THREAD_FLIP`; no new switches.
- Every sdl3pinmame launch uses private `-nvram_directory` and `-cfg_directory` and a `timeout -k 30`.
- Every existing suite stays green: `tests/pinheck/{mips32,pic32mx,storage,link,p8x32a,display,audio,board}/check.sh`, the p8x32a suite also with `P8X32A_JIT=1`, `pic32mx/pinmame_check.sh` (with `romset_check.sh`), `display/pinmame_display.sh`, `audio/pinmame_audio.sh`, `board/pinmame_board.sh`, `display/pinmame_look.sh`, and `game/pinmame_game.sh`.
- No profile, `.gcda`, `.profraw`, `.profdata`, `.pgd` or `.pgc` file is committed; they live under the ignored `build/`.
- Code comments stay short and factual.

## Rulings

Decisions the spec left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it). Counts are per emulated second in attract mode, measured on this plan's prototype with a counting build of `prop.c` (clock reads around every wait, not part of the plan); speeds are wall-clock over `bench.sh`'s window at the load printed.

1. **Where the time goes after Plan 9c (Linux, Ryzen 7 7730U, one CCX).** With the worker thread 1.26× attract and 1.09× video (loads 2.9 and 2.4), on one thread 1.02× and 0.91×; pinned to one CPU (`taskset -c 2`) on one thread 1.04–1.05× against 1.28× threaded (loads 2.2–3.5). The worker is the bound: about 700 ms busy and 90 ms idle per emulated second in attract mode (790 and 97 ms with video). By symbol the worker spends 25% in the local loop, 21% in translated code, 15% spinning for work, 11% in `flush`, 8% in the scheduler, 5% in `hub_rw`; the PIC32 thread 53% executing and 42% in `prop_sync`. Syncs: 28,172 a second, 27,074 of them `RF13` reads (every one waits), the rest the sound, video, NVRAM and boot syncs; posts: 12,571 catch-ups from the slice starts and 726 pin changes. The PIC32 thread waits 460 ms (attract) and 550 ms (video) per emulated second: 90% of the waits end within 8 µs (the Propeller running the ~3,000 PIC32 cycles since the last read), and about 400 a second last 0.25–4 ms (the Propeller's backlog), which is most of the wait time. The worker's idle periods are 1–2 µs (24,000 a second: the PIC32 resuming after an answer and running to its next post).
2. **Thread placement is not implemented.** This laptop has one L3 (CPUs 0–15), so nothing can be placed. The Windows measurements (Threadripper 3970X, Zen 2, 8 LPs per CCX, 2 CCXs per CCD, one processor group) show the scheduler already keeping both threads in one CCX: default 0.930/0.901×, pinned to two cores of one CCX 0.921/0.930×, across CCDs 0.882/0.878×, on the two SMT siblings of one core 0.750/0.746×; video default and pinned 0.853–0.867×. Code that narrows the affinity would only help where a scheduler splits the threads across CCDs (−5%) and could hurt where other work shares the chosen CCX. Cost if wrong: about 5% on such a machine; the Windows checklist measures the default placement again.
3. **The queue's fields stay on one cache line, and `RF13` is read as before.** Padding `head`, `tail` and `sleeping` 128 bytes apart and answering `RF13` from the worker (one line instead of the cogs' registers) measured 1.284/1.274× against the reference's 1.311/1.229× (attract) and 1.113/1.119× against 1.117/1.125× (video), within the machine's noise. The thread adds about 575,000 cache-to-cache fills a second (`ls_dmnd_fills_from_sys.int_cache`, 21.9 M against 7.6 M over 25 s), about 20 per sync, a few ms a second at 50–80 ns each; a two-thread ping-pong over the same fields takes 47 ns a round trip on one line and 79 ns padded. `perf c2c` on Zen 3 (IBS) reports no HITM data. This closes the 9b/9c review's minor M6. Cost if wrong: under 1% where the threads sit in different CCXs.
4. **The worker does not run ahead of the PIC32's calls.** Measured: with the frame log's PIC32 stamp taken from the frame's own Propeller time instead of the call that delivered it, every output (UART1, frames, sound, NVRAM) stayed byte-identical under random extra cut points (1–3,000 cycles, three seeds), with the thread on and off, with queued catch-ups coalesced, and with the PIC32's slices capped at 256–2,048 cycles to give the worker more horizons. The extra horizons cut the worker's idle time from 96 to 64 (1,024-cycle slices) and 48 ms (256) per emulated second, but its busy time rose by 20–30 ms (each catch-up costs the worker about 0.45 µs: the scheduler, the final flush, sleeping cogs woken at the horizon), and the speeds did not change (1.135–1.166× against 1.164–1.233×, loads 2.2–3.2). The idle that remains is the PIC32's own run to its next post after each answer. The frame stamp therefore stays as it is. Cost if wrong: up to the 12% idle time, if a cheaper catch-up is found.
5. **A wait spins at most 50 µs by the clock, then blocks.** Plan 9b bounded the spin by 20,000 pause instructions: 390 µs on Zen 3 (19.5 ns a pause), a different time on other cores. The clock is read every 64 pauses (`clock_gettime(CLOCK_MONOTONIC)`, `QueryPerformanceCounter`). 50 µs covers about 97% of the PIC32's waits (Ruling 1). Measured in the same session (load about 3): 1.144/1.163× with 1.21–1.23 CPU seconds per emulated second against 1.165/1.162× with 1.38 for the 390 µs spin; yielding the CPU from the start was 5% slower (1.098/1.107×), a 20 µs spin followed by yields no faster than the plain bound (1.159/1.161×). Cost if wrong: waits between 50 and 390 µs (about 1,000 a second) now block, each a futex or event wake on the worker.
6. **No worker thread when the process may use only one CPU.** Two threads time-sliced on one CPU wait a full spin and a context switch at every handoff: 0.044–0.049× on Linux (`taskset -c 0`, `taskset -c 2`), 0.073× on Windows (one LP), against 0.83–1.05× inline on one CPU. `prop_start_thread` counts the allowed CPUs (`sched_getaffinity` on Linux, which covers `taskset` and cpusets; `GetProcessAffinityMask` on Windows, which covers `start /affinity`; other systems are assumed to allow two) and returns -1 below two, logging `prop: one CPU allowed, no worker thread`; the calls then run inline. The affinity mask itself is never changed.
7. **The driver keeps the worker only while it is faster.** When the machine is oversubscribed the handoffs wait for a descheduled thread: on two CPUs with a busy loop on one, 0.055× with Plan 9b's spin, 0.179× with the 50 µs bound, 0.278× and 0.385× with yields, 0.879× inline. No wait policy recovers that, so the driver measures: over each second of host time (`osd_cycles`) below 0.95× with the worker it runs a second inline and keeps the faster (5% margin), trying the other again after 10 s, doubling to 320 s; the first 5 s and any window longer than 3 s of host time (a pause) are not judged; throttled at 1.0× nothing changes. Both paths give the same output (Plan 9b), so switching changes only the speed; `PINHECK_THREAD_FLIP` switches every second and `bench.sh contention` requires those runs byte-identical. Measured: two contended CPUs 0.648× against 0.670× inline (load 7, `prop: worker thread off (0.34x with it, 0.70x without)`); with seven other emulators at nice 5 the video workload ran at 0.737× against the reference's 0.523× and 0.650× inline. Cost if wrong: a trial second in the slower mode every 10–320 s.
8. **Profile-guided builds are an option, measured on Linux and specified for Windows; the profile is never committed.** GCC 15 with `-fprofile-use` after training on `bench.sh`'s attract and video workloads, in paired runs: 1.281/1.271× against 1.171/1.152× attract and 1.111/1.107× against 1.019/1.026× video with the worker, 1.038/1.030× and 0.915/0.916× against 0.971/0.956× and 0.855/0.849× on one thread (loads 2.3–4.4); in the replay, with Tasks 2–3 and `pgo.sh`, 1.387× attract and 1.222× video against 1.272× and 1.110× without the profile (loads 1.9–2.8), 1.144× and 1.026× against 1.048× and 0.946× on one thread; host instructions per emulated second −5.5%, every output byte-identical. The scripted game, not used for training, ran in 4.62 T instead of 4.90 T host instructions (265 s instead of 290 s including its checks), its four logs identical (Task 4). The Windows session measured the same code with MSVC and with MinGW GCC 15.2 (`-O3`, no LTO) on the 3970X through libpinmame (attract, 20–50 s, medians of three): one thread without the translator 0.626× against 0.719×, one thread 0.690× against 0.685×, with the worker 0.707× against 0.817×, output identical; MSVC's deficit is in the C paths, which is what a profile helps. The profile comes from running the firmware, which CI does not have, and is toolchain-specific binary data, so it stays local: `cmake/pgo.cmake` gives `PINMAME_PGO=GEN|USE` and `PINMAME_PGO_DIR` to the sdl3pinmame, libpinmame and Windows x64 PinMAME lists (GCC's `-fprofile-prefix-path` keeps the profile valid in another build directory, Clang's is merged with `llvm-profdata`, MSVC links with `/GENPROFILE` and `/USEPROFILE`), and `tests/pinheck/perf/pgo.sh` builds and trains sdl3pinmame. The x86-64 GCC builds have no FMA, so `-ffp-contract=fast` cannot fuse differently after other inlining; an arm64 or MSVC `/fp:fast` build must pass the determinism check with its profile, which the Windows checklist includes. VPinMAME and the 32-bit lists are left without the option (no training harness drives the COM server). Cost if wrong: binaries built without a profile run about 10% slower than a local profile-guided build.
9. **No change to the Propeller core in this plan.** The candidates of Plan 9c's Result were weighed on the profile (Ruling 1): the scheduler's search is about 3.4% of the worker (keeping the three earliest keys needs every one of the ~30 places that change a cog's next event), the translated blocks' entry and shared tail about 5%, `flush` and the pin devices about 15% (Plan 9c already stepped the NCO toggles); each would need RTL-exact changes for gains below what paired runs resolve on this machine (±3%), while the profile-guided build took 5.5% of the host instructions across all of them. They are listed for release with these shares.
10. **Measuring.** Speeds are wall-clock over `bench.sh`'s window and depend on the load, printed with each; paired runs alternate the builds. The one-thread host instruction counts do not depend on the load. `bench.sh contention` compares runs of the same build made one after the other on the same CPUs, so its ratios hold at other loads; its thresholds (0.9 on one CPU, 0.8 on two contended CPUs) leave room for the governor's trial seconds.
11. **The Windows checklist** (end of this plan) is for the Windows session: builds, determinism across the worker switches and the profile-guided build, the one-LP and contended-CPU cases, speeds before and after, and the MSVC profile-guided build. Its results land in the release notes; a Windows-only fix comes back as a diff with a test.

## Review Focus

- **Switching the worker mid-run** (`prop_stop_thread` then `prop_start_thread` from the vblank callback, the same emulation thread): the queue drains before the stop, the inline calls continue from the same state, and a host thread reading NVRAM never waits (it is not the owner). `PINHECK_THREAD_FLIP` switches every second and `bench.sh contention` requires those runs byte-identical to the run without the worker (Task 3).
- **The governor's windows** (a pause, a machine reset that restarts emulated time, the first seconds of boot, a throttled run at 1.0×): only complete windows of 1–3 s of host time are judged; a mutation that never switches off fails `bench.sh contention` (Task 5).
- **The one-CPU guard on every platform:** Linux `sched_getaffinity` needs `_GNU_SOURCE` before the first include; Windows counts the process mask; other systems return 2 and keep today's behaviour. `prop_test` restricts its own affinity (Linux, and Windows under wine) and restores it (Task 2).
- **The bounded spin:** the clock is read every 64 pauses, so a wait can overrun 50 µs by one read; a blocked wait is woken exactly as in Plan 9b. `spin_bound` in `prop_test` fails if the bound grows tenfold (Task 5's mutation).
- **Profile-guided builds** change code layout and inlining, never the machine: the Linux build is byte-identical to the reference in `bench.sh` and in the scripted game's logs (Task 4); an arm64 or MSVC `/fp:fast` build is checked the same way on its platform (Windows checklist).

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck/prop.c` | a wait spins at most `PROP_SPIN_NS` by the clock, then blocks; `prop_start_thread` returns -1 when fewer than two CPUs are allowed (Task 2) (modified) |
| `tests/pinheck/link/prop_test.c` | `spin_bound`, `one_cpu`; the foreign-sync test's flag is atomic (Task 2) (modified) |
| `src/wpc/pinheck.c` | the worker governor and `PINHECK_THREAD_FLIP` (Task 3) (modified) |
| `tests/pinheck/perf/bench.sh` | `bench.sh contention`, the `short` workload, `BENCH_CPUS`, `BENCH_MACHINE`, the Propeller log of every run (Task 3) (modified) |
| `cmake/pgo.cmake` | `PINMAME_PGO`, `PINMAME_PGO_DIR`, `pinmame_enable_pgo(target)` for GCC, Clang and MSVC (Task 4) (new) |
| `cmake/sdl3pinmame/CMakeLists.txt`, `cmake/libpinmame/CMakeLists.txt`, `cmake/pinmame/CMakeLists_win-x64.txt` | include `cmake/pgo.cmake` for their targets (Task 4) (modified) |
| `tests/pinheck/perf/pgo.sh` | instrumented build, training on `bench.sh`'s workloads, profile-guided build (Task 4) (new) |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | measured results (Task 5) (modified) |

---
### Task 1: Reference build and baseline

**Files:** none changed.

**Interfaces:**
- Consumes: Plan 9's `tests/pinheck/perf/bench.sh` (`bench.sh [attract] [video]`, `bench.sh profile WORKLOAD`, `SDL3PINMAME`, `REFERENCE`) and Plan 9b's `PINHECK_THREADS`.
- Produces: `build/reference/sdl3pinmame`, the reference every later task compares with, its cached benchmark runs, and the baseline of the Result.

- [ ] **Step 1: Build the reference**

Build `pinheck` as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`) and keep the binary as the reference:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9d.log 2>&1; tail -1 build/sdl3pinmame/m9d.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```
Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: The baseline: with the thread, on one thread, and on one thread pinned to one CPU**

The pinned run is the figure the Windows session compares its own one-thread speed with (Windows checklist):

```sh
SDL3PINMAME=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video
PINHECK_THREADS=0 SDL3PINMAME=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | sed 's/^/one thread: /'
PINHECK_THREADS=0 SDL3PINMAME=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 taskset -c 2 tests/pinheck/perf/bench.sh attract | sed 's/^/one thread on CPU 2: /'
```
Expected (about 6 minutes; speeds vary with the load, the one-thread instruction counts do not):
```text
bench attract opt: 1.096x over 30.0 s emulated (27.3 s wall), whole run 12.6 G instructions and 1.4 s CPU per emulated s, worst 100 ms 0.862x, load 4.29
bench video opt: 0.833x over 24.0 s emulated (28.8 s wall), whole run 13.6 G instructions and 1.8 s CPU per emulated s, worst 100 ms 0.642x, load 4.56
one thread: bench attract opt: 0.755x over 30.0 s emulated (39.7 s wall), whole run 12.5 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.405x, load 4.33
one thread: bench video opt: 0.775x over 24.0 s emulated (31.0 s wall), whole run 13.5 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.601x, load 3.73
one thread on CPU 2: bench attract opt: 0.903x over 30.0 s emulated (33.2 s wall), whole run 12.5 G instructions and 1.0 s CPU per emulated s, worst 100 ms 0.555x, load 3.38
```

- [ ] **Step 3: Where the time goes, per thread**

A Release build with `-g` in its own directory, profiled with the worker thread; `perf report --sort pid` separates the worker (the first line) from the PIC32 thread:

```sh
cmake -S . -B build/sdl3pinmame-prof -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DCMAKE_C_FLAGS=-g -DCMAKE_CXX_FLAGS=-g > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame-prof -j6 > build/sdl3pinmame-prof/m9d.log 2>&1; tail -1 build/sdl3pinmame-prof/m9d.log
SDL3PINMAME=build/sdl3pinmame-prof/sdl3pinmame nice -n 15 ionice -c 3 timeout 5400 tests/pinheck/perf/bench.sh profile attract | sed -n 1p
P=tests/pinheck/perf/build/prof/attract/perf.data
perf report -i $P --stdio --no-children -g none --sort pid 2> /dev/null | grep '%' | head -2
for t in $(perf report -i $P --stdio --no-children -g none --sort pid 2> /dev/null | grep '%' | head -2 | awk '{print $2}' | cut -d: -f1); do
	perf report -i $P --stdio --no-children -g none --tid $t --sort dso,sym --percentage relative 2> /dev/null | grep '%' | grep -v '\[JIT\]' | head -6
	perf report -i $P --stdio --no-children -g none --tid $t --sort dso --percentage relative 2> /dev/null | grep '\[JIT\]'
done
```
Expected (percentages vary by a few points; the translated code's samples appear under the `[JIT]` pseudo-library):
```text
[100%] Built target sdl3pinmame
bench attract prof: 1.131x over 30.0 s emulated (26.5 s wall), worst 100 ms 0.940x, load 4.42
    54.25%  1238240:sdl3pinmame
    45.73%  1238238:sdl3pinmame
    24.69%  sdl3pinmame        [.] run_local.lto_priv.0
    11.91%  sdl3pinmame        [.] worker.lto_priv.0
    11.74%  sdl3pinmame        [.] flush.lto_priv.0
     8.24%  sdl3pinmame        [.] do_catch_up.lto_priv.0
     5.03%  sdl3pinmame        [.] hub_rw
     2.99%  sdl3pinmame        [.] ctr_next.lto_priv.0
    21.65%  [JIT] tid 1238238
    52.09%  sdl3pinmame           [.] prop_sync.constprop.0
    44.36%  sdl3pinmame           [.] pic32cpu_execute
     0.95%  sdl3pinmame           [.] pinheck_video
     0.23%  sdl3pinmame           [.] pinheck_port_write
     0.21%  sdl3pinmame           [.] bus_read
     0.21%  sdl3pinmame           [.] update_eic.lto_priv.0
```

### Task 2: Bounded waits and the one-CPU guard

**Files:**
- Modify: `src/wpc/pinheck/prop.c`, `tests/pinheck/link/prop_test.c`

**Interfaces:**
- Consumes: Plan 9b's `prop_start_thread`, `prop_stop_thread`, `prop_sync`, `wait_head`, `worker`, `idle_wait`, `CPU_RELAX_ANY`; `prop_test`'s `boot`, `pulse`, `count`, `cpu_s`, `wall_s`, `nap_ms`.
- Produces: `#define PROP_SPIN_NS 50000` (replaces `PROP_SPIN`); `static uint64_t now_ns(void)`, `static int cpus_allowed(void)`, `static int spin(uint64_t *t0, unsigned *k)` in the threaded part of `prop.c`; `prop_start_thread` returns -1 and logs `prop: one CPU allowed, no worker thread` when fewer than two CPUs are allowed. `prop_test` gains `spin_bound` (POSIX) and `one_cpu`.

A wait spins at most 50 µs by the clock and then blocks (Ruling 5); with one allowed CPU the worker is not started (Ruling 6).

- [ ] **Step 1: Write the tests**

`spin_bound` makes 50 syncs each wait about 2 ms (the pins callback naps on the worker) and requires the waiting thread to use under 10 ms of CPU in all; with Plan 9b's 390 µs spin it uses about 20. `one_cpu` restricts the process to one CPU (Linux `sched_setaffinity`, Windows `SetProcessAffinityMask`), requires `prop_start_thread` to refuse, runs the echo program inline, and restores the mask. The foreign-sync test's `host_go` becomes an atomic: ThreadSanitizer reported the plain `volatile` flag once the thread timing changed.

```sh
python3 - <<'PYEOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/link/prop_test.c', [
(r'''#define _POSIX_C_SOURCE 200809L
#include "../../../src/wpc/pinheck/prop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
''', r'''#define _POSIX_C_SOURCE 200809L
#ifdef __linux__
#define _GNU_SOURCE /* sched_setaffinity */
#endif
#include "../../../src/wpc/pinheck/prop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#ifdef __linux__
#include <sched.h>
#endif
'''),
(r'''static volatile int host_go;

static void *host_sync(void *arg)
{
	(void)arg;
	while (host_go) prop_sync(&p);
	return NULL;
}
''', r'''static int host_go; /* atomic */

static void *host_sync(void *arg)
{
	(void)arg;
	while (__atomic_load_n(&host_go, __ATOMIC_ACQUIRE)) prop_sync(&p);
	return NULL;
}
'''),
(r'''		host_go = 1;
''', r'''		__atomic_store_n(&host_go, 1, __ATOMIC_RELEASE);
'''),
(r'''		host_go = 0;
''', r'''		__atomic_store_n(&host_go, 0, __ATOMIC_RELEASE);
'''),
(r'''#ifdef _WIN32
/* the worker runs at the priority of the thread that started it */
''', r'''#ifndef _WIN32
static int nap_calls;

static void nap_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx; (void)t; (void)out; (void)dir;
	if (nap_calls > 0) { nap_calls--; nap_ms(2); }
}

/* waits longer than the spin bound cost the waiting thread about 50 us of CPU each, not the whole wait */
static void spin_bound(void)
{
	uint64_t pic = 200000;
	double c0, w0;
	int b;
	boot();
	prop_set_pins(&p, nap_pins, NULL);
	CHECK(prop_start_thread(&p) == 0);
	prop_catch_up(&p, pic);
	prop_sync(&p);
	nap_calls = 1000;
	c0 = cpu_s();
	w0 = wall_s();
	for (b = 0; b < 50; b++) {
		pulse(&pic, b & 1);
		prop_catch_up(&p, pic);
		prop_sync(&p);
	}
	CHECK(wall_s() - w0 > 0.1);
	CHECK(cpu_s() - c0 < 0.01);
	nap_calls = 0;
	prop_stop_thread(&p);
}
#endif

/* with one CPU allowed the worker does not start and the calls run inline */
static void one_cpu(void)
{
	uint64_t pic = 200000;
	int b;
#ifdef __linux__
	cpu_set_t all, one;
	CHECK(sched_getaffinity(0, sizeof(all), &all) == 0);
	CPU_ZERO(&one);
	for (b = 0; b < CPU_SETSIZE; b++)
		if (CPU_ISSET(b, &all)) { CPU_SET(b, &one); break; }
	CHECK(sched_setaffinity(0, sizeof(one), &one) == 0);
#elif defined(_WIN32)
	DWORD_PTR all, sys;
	CHECK(GetProcessAffinityMask(GetCurrentProcess(), &all, &sys));
	CHECK(SetProcessAffinityMask(GetCurrentProcess(), all & (0 - all)));
#endif
	boot();
#if defined(__linux__) || defined(_WIN32)
	CHECK(prop_start_thread(&p) == -1 && p.worker == NULL);
#endif
	prop_catch_up(&p, pic);
	for (b = 0; b < BITS; b++) pulse(&pic, (b * 7 + 3) % 5 < 2);
	prop_catch_up(&p, pic);
	prop_sync(&p);
	CHECK(count() == BITS);
#ifdef __linux__
	CHECK(sched_setaffinity(0, sizeof(all), &all) == 0);
	if (CPU_COUNT(&all) >= 2) {
#elif defined(_WIN32)
	CHECK(SetProcessAffinityMask(GetCurrentProcess(), all));
	if (all & (all - 1)) {
#else
	{
#endif
		CHECK(prop_start_thread(&p) == 0);
		prop_stop_thread(&p);
	}
}

#ifdef _WIN32
/* the worker runs at the priority of the thread that started it */
'''),
(r'''	blocking_sync();
#ifdef _WIN32
	priority();
#endif
''', r'''	blocking_sync();
#ifndef _WIN32
	spin_bound();
#endif
	one_cpu();
#ifdef _WIN32
	priority();
#endif
'''),
])
PYEOF
```

- [ ] **Step 2: Run the link suite to see the new tests fail**

Run: `PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 ionice -c 3 tests/pinheck/link/check.sh 2>&1 | grep 'PROP FAIL\|^prop:\|^link:'`
Expected:
```text
PROP FAIL prop_test.c:391: cpu_s() - c0 < 0.01
PROP FAIL prop_test.c:416: prop_start_thread(&p) == -1 && p.worker == NULL
prop: FAIL
PROP FAIL prop_test.c:391: cpu_s() - c0 < 0.01
PROP FAIL prop_test.c:416: prop_start_thread(&p) == -1 && p.worker == NULL
prop: FAIL
PROP FAIL prop_test.c:391: cpu_s() - c0 < 0.01
PROP FAIL prop_test.c:416: prop_start_thread(&p) == -1 && p.worker == NULL
prop: FAIL
link: 3 failed
```

- [ ] **Step 3: Bound the spin by the clock and guard the one-CPU case**

```sh
python3 - <<'PYEOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck/prop.c', [
(r'''#include "prop.h"
#include <stdio.h>
''', r'''#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* sched_getaffinity */
#endif
#include "prop.h"
#include <stdio.h>
'''),
(r'''#define PROP_Q 4096
#define PROP_SPIN 20000
''', r'''#define PROP_Q 4096
#define PROP_SPIN_NS 50000 /* a wait spins this long, then blocks */
'''),
(r'''static unsigned get_sc(volatile unsigned *v) { MemoryBarrier(); return *v; }
static void put_sc(volatile unsigned *v, unsigned x) { InterlockedExchange((volatile LONG *)v, (LONG)x); }
#else
#include <pthread.h>
''', r'''static unsigned get_sc(volatile unsigned *v) { MemoryBarrier(); return *v; }
static void put_sc(volatile unsigned *v, unsigned x) { InterlockedExchange((volatile LONG *)v, (LONG)x); }
static uint64_t now_ns(void)
{
	static LARGE_INTEGER f;
	LARGE_INTEGER c;
	if (!f.QuadPart) QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
}
static int cpus_allowed(void)
{
	DWORD_PTR pm, sm;
	int n = 0;
	if (!GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm) || !pm) return 2;
	for (; pm; pm &= pm - 1) n++;
	return n;
}
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
'''),
(r'''static unsigned get_sc(volatile unsigned *v) { return __atomic_load_n(v, __ATOMIC_SEQ_CST); }
static void put_sc(volatile unsigned *v, unsigned x) { __atomic_store_n(v, x, __ATOMIC_SEQ_CST); }
#endif
''', r'''static unsigned get_sc(volatile unsigned *v) { return __atomic_load_n(v, __ATOMIC_SEQ_CST); }
static void put_sc(volatile unsigned *v, unsigned x) { __atomic_store_n(v, x, __ATOMIC_SEQ_CST); }
static uint64_t now_ns(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
static int cpus_allowed(void)
{
#ifdef __linux__
	cpu_set_t s;
	if (sched_getaffinity(0, sizeof(s), &s)) return 2;
	return CPU_COUNT(&s);
#else
	return 2;
#endif
}
#endif

/* one step of a bounded spin; 1 when it has lasted PROP_SPIN_NS (the clock is read every 64 steps) */
static int spin(uint64_t *t0, unsigned *k)
{
	if (!(*k & 63)) {
		uint64_t n = now_ns();
		if (!*k) *t0 = n;
		else if (n - *t0 >= PROP_SPIN_NS) return 1;
	}
	++*k;
	CPU_RELAX_ANY();
	return 0;
}
'''),
(r'''static void wait_head(prop_worker *w, unsigned want)
{
	int k;
	for (k = 0; k < PROP_SPIN; k++) {
		if ((int)(get_acq(&w->head) - want) >= 0) return;
		CPU_RELAX_ANY();
	}
''', r'''static void wait_head(prop_worker *w, unsigned want)
{
	uint64_t t0 = 0;
	unsigned k = 0;
	for (;;) {
		if ((int)(get_acq(&w->head) - want) >= 0) return;
		if (spin(&t0, &k)) break;
	}
'''),
(r'''	unsigned h = w->head;
	int k = 0;
	for (;;) {
		unsigned t = get_acq(&w->tail);
		if (h == t) {
			if (++k < PROP_SPIN) CPU_RELAX_ANY();
			else { idle_wait(w, h); k = 0; }
			continue;
		}
		k = 0;
''', r'''	unsigned h = w->head, k = 0;
	uint64_t t0 = 0;
	for (;;) {
		unsigned t = get_acq(&w->tail);
		if (h == t) {
			if (spin(&t0, &k)) { idle_wait(w, h); k = 0; }
			continue;
		}
		k = 0;
'''),
(r'''	prop_worker *w;
	if (p->worker) return 0;
''', r'''	prop_worker *w;
	if (p->worker) return 0;
	/* two threads time-sliced on one CPU wait for each other at every handoff */
	if (cpus_allowed() < 2) {
		if (p->log) p->log(p->log_ctx, "prop: one CPU allowed, no worker thread");
		return -1;
	}
'''),
])
PYEOF
```

- [ ] **Step 4: The link suite passes, also for the Windows worker under wine**

Run: `PINHECK_WINE=1 nice -n 15 ionice -c 3 tests/pinheck/link/check.sh > build/m9d-link.log 2>&1; echo "link: exit $? $(tail -1 build/m9d-link.log)"; grep -a '^prop:\|FAIL' build/m9d-link.log` (about 5 minutes: the C89 gates, `prop_test` plain, under AddressSanitizer and ThreadSanitizer, the mingw builds under wine, and the machine check)
Expected:
```text
link: exit 0 link: 0 failed
prop: ok
```

- [ ] **Step 5: Rebuild and compare with the reference, with the thread and on one thread**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9d.log 2>&1; tail -1 build/sdl3pinmame/m9d.log
grep -i warning build/sdl3pinmame/m9d.log | grep -c 'prop\.c\|pinheck\.c'
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:'
PINHECK_THREADS=0 SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:\|determinism first' | sed 's/^/one thread: /'
```
Expected (speeds vary with the load; the CPU seconds per emulated second fall with the shorter spin):
```text
[100%] Built target sdl3pinmame
0
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.151x over 30.0 s emulated (26.1 s wall), whole run 12.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.955x, load 3.89
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.015x over 24.0 s emulated (23.6 s wall), whole run 13.6 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.888x, load 2.83
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread: bench attract opt: 0.949x over 30.0 s emulated (31.6 s wall), whole run 12.5 G instructions and 0.9 s CPU per emulated s, worst 100 ms 0.779x, load 2.37
one thread: determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread: bench video opt: 0.847x over 24.0 s emulated (28.3 s wall), whole run 13.5 G instructions and 1.0 s CPU per emulated s, worst 100 ms 0.712x, load 2.19
one thread: determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 6: Commit**

```bash
git add src/wpc/pinheck/prop.c tests/pinheck/link/prop_test.c
git commit -m "pinheck prop: waits spin at most 50 us by the clock, then block; no worker thread when one CPU is allowed"
```

### Task 3: The worker governor and the contention benchmark

**Files:**
- Modify: `src/wpc/pinheck.c`, `tests/pinheck/perf/bench.sh`

**Interfaces:**
- Consumes: Task 2's `prop_start_thread` (-1 with one CPU); Plan 9b's `prop_stop_thread` (drains the queue) and `prop.worker`; Plan 9c's `PINHECK_TIME_LOG` vblank hook (`osd_cycles`, `timer_get_time`); Plan 5's `PINHECK_PROP_LOG`.
- Produces: `static void pinheck_governor(void)` called at every vblank of a running game; the test hook `PINHECK_THREAD_FLIP`; log lines `prop: worker thread off (Ax with it, Bx without)`, `prop: worker thread on (...)`, `prop: worker thread switched on|off`. `bench.sh contention`, the workload `short` (10 s of attract mode after the 10 s mark), `BENCH_CPUS` (the CPUs a run is pinned to), `BENCH_MACHINE` (another label's machine directory), and `prop.log` in every run directory.

The driver keeps the worker only while it is faster (Ruling 7); `bench.sh contention` checks the one-CPU guard, the governor and switching (Ruling 10).

- [ ] **Step 1: The contention benchmark**

```sh
python3 - <<'PYEOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/perf/bench.sh', [
(r'''	attract) FRAMES=2400 MARK=600 SEND_AT=0 SEND= ;;
''', r'''	attract) FRAMES=2400 MARK=600 SEND_AT=0 SEND= ;;
	short) FRAMES=1200 MARK=600 SEND_AT=0 SEND= ;;
'''),
(r'''#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py
''', r'''#   bench.sh profile WORKLOAD      perf record one workload with $SDL3PINMAME, then components.py
#   bench.sh contention            10 s of attract mode where CPUs are scarce (Linux, taskset; BENCH_CPU_A and BENCH_CPU_B,
#                                  default 2 and 4, name two allowed CPUs): on one CPU, on two CPUs while a busy loop
#                                  shares the first, and with the worker switched every second; each against the same
#                                  run without the worker thread, byte-identical and at least 0.9x, 0.8x, - its speed
'''),
(r'''# run one workload with one build under perf stat, perf record (mode "record") or neither
run() {
	label=$1 bin=$2 w=$3
	spec $w
	machine $label $bin
	D=$B/$label/$w
''', r'''# run one workload with one build under perf stat, perf record (mode "record") or neither; BENCH_MACHINE names
# another label's machine directory, BENCH_CPUS the CPUs it runs on
run() {
	label=$1 bin=$2 w=$3
	spec $w
	ml=${BENCH_MACHINE:-$label}
	machine $ml $bin
	D=$B/$label/$w
'''),
(r'''	rm -rf $D && mkdir -p $D && cp -r $B/$label/nvram.base $D/nvram || exit 2
''', r'''	rm -rf $D && mkdir -p $D && cp -r $B/$ml/nvram.base $D/nvram || exit 2
'''),
(r'''	(cd $D && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 PINHECK_UART1_LOG=$D/uart.log \
		PINHECK_FRAME_LOG=$D/frames.bin PINHECK_TIME_LOG=$D/time.log PINHECK_WAV=$D/snd.wav PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND_GAP=1 PINHECK_UART1_SEND="$SEND" \
		timeout -k 30 5400 $wrap "$bin" dominos -rompath ../roms -nvram_directory nvram -cfg_directory ../cfg -headless \
''', r'''	[ -n "$BENCH_CPUS" ] && wrap="taskset -c $BENCH_CPUS $wrap"
	(cd $D && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 PINHECK_UART1_LOG=$D/uart.log PINHECK_PROP_LOG=$D/prop.log \
		PINHECK_FRAME_LOG=$D/frames.bin PINHECK_TIME_LOG=$D/time.log PINHECK_WAV=$D/snd.wav PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND_GAP=1 PINHECK_UART1_SEND="$SEND" \
		timeout -k 30 5400 $wrap "$bin" dominos -rompath $B/$ml/roms -nvram_directory nvram -cfg_directory $B/$ml/cfg -headless \
'''),
(r'''[ $# -eq 0 ] && set -- attract video
''', r'''if [ "$1" = contention ]; then
	command -v taskset > /dev/null || { echo "bench: taskset not found"; exit 2; }
	A=${BENCH_CPU_A:-2} C=${BENCH_CPU_B:-4} fail=0 hog=
	trap '[ -n "$hog" ] && kill $hog 2> /dev/null' EXIT
	export BENCH_MACHINE=opt
	for m in one busy flip; do
		case $m in
		one) cpus=$A need=0.9 log='one CPU allowed' ;;
		busy) cpus=$A,$C need=0.8 log='worker thread off' ;;
		flip) cpus= need=0 log='worker thread switched' ;;
		esac
		if [ $m = busy ]; then taskset -c $A sh -c 'while :; do :; done' & hog=$!; fi
		BENCH_CPUS=$cpus PINHECK_THREADS=0 run ${m}0 "$SDL3PINMAME" short > /dev/null || exit 1
		if [ $m = flip ]; then PINHECK_THREAD_FLIP=1 run $m "$SDL3PINMAME" short > /dev/null || exit 1
		else BENCH_CPUS=$cpus run $m "$SDL3PINMAME" short > /dev/null || exit 1; fi
		if [ -n "$hog" ]; then kill $hog; wait $hog 2> /dev/null; hog=; fi
		s0=$(sed 's/.*: \([0-9.]*\)x over.*/\1/' $B/${m}0/short/bench.txt) s=$(sed 's/.*: \([0-9.]*\)x over.*/\1/' $B/$m/short/bench.txt)
		n=$(grep -c "$log" $B/$m/short/prop.log)
		echo "contention $m: ${s}x with the worker thread requested, ${s0}x without; \"$log\" $n times"
		for f in uart.log frames.bin snd.wav nvram/dominos.nv; do
			cmp -s $B/$m/short/$f $B/${m}0/short/$f || { echo "CONTENTION FAIL $m: $f differs from the run without the worker thread"; fail=1; }
		done
		[ $n -gt 0 ] || { echo "CONTENTION FAIL $m: \"$log\" not logged"; fail=1; }
		awk "BEGIN { exit !($s >= $need * $s0) }" || { echo "CONTENTION FAIL $m: slower than $need of the run without the worker thread"; fail=1; }
	done
	[ $fail -eq 0 ] && echo "contention: ok"
	exit $fail
fi
[ $# -eq 0 ] && set -- attract video
'''),
])
PYEOF
```

- [ ] **Step 2: Run it with the reference to see it fail**

Run: `SDL3PINMAME=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh contention`
Expected (about 20 minutes: the reference runs at about 0.04× in the first two cases):
```text
contention one: 0.047x with the worker thread requested, 0.931x without; "one CPU allowed" 0 times
CONTENTION FAIL one: "one CPU allowed" not logged
CONTENTION FAIL one: slower than 0.9 of the run without the worker thread
contention busy: 0.043x with the worker thread requested, 0.830x without; "worker thread off" 0 times
CONTENTION FAIL busy: "worker thread off" not logged
CONTENTION FAIL busy: slower than 0.8 of the run without the worker thread
contention flip: 1.230x with the worker thread requested, 1.002x without; "worker thread switched" 0 times
CONTENTION FAIL flip: "worker thread switched" not logged
```

- [ ] **Step 3: The governor**

```sh
python3 - <<'PYEOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
(r'''/* test hook: PINHECK_TIME_LOG gets the emulated and the host time at each vblank, in seconds */
static FILE *time_log;

static INTERRUPT_GEN(pinheck_vblank)
{
	if (time_log) fprintf(time_log, "%.6f %.6f\n", timer_get_time(), (double)osd_cycles() / (double)osd_cycles_per_second());
''', r'''/* test hook: PINHECK_TIME_LOG gets the emulated and the host time at each vblank, in seconds */
static FILE *time_log;

/* The worker thread stays on while it is faster. A second of host time below 0.95x real time with it tries a second
   without it, and the faster of the two stays; the other is tried again after a pause that doubles (10 s to 320 s).
   Both give the same results, so switching changes only the speed. Test hook: PINHECK_THREAD_FLIP switches every
   second. */
enum { GOV_OFF, GOV_THREADED, GOV_TRY_INLINE, GOV_INLINE, GOV_TRY_THREADED };
static struct { int state, flip; double w0, e0, next, pause, thr, inl; } gov;

static void pinheck_gov_log(const char *what, double a, double b)
{
	char msg[96];
	sprintf(msg, "prop: worker thread %s (%.2fx with it, %.2fx without)", what, a, b);
	pinheck_prop_log(NULL, msg);
}

static void pinheck_governor(void)
{
	double w = (double)osd_cycles() / (double)osd_cycles_per_second(), e = timer_get_time(), s;
	if (gov.state == GOV_OFF) return;
	if (gov.w0 == 0.0 || w - gov.w0 > 3.0 || e < gov.e0) { gov.w0 = w; gov.e0 = e; return; } /* start, pause or reset */
	if (w - gov.w0 < 1.0) return;
	s = (e - gov.e0) / (w - gov.w0);
	gov.w0 = w;
	gov.e0 = e;
	if (gov.flip) {
		if (prop.worker) prop_stop_thread(&prop);
		else prop_start_thread(&prop);
		pinheck_prop_log(NULL, prop.worker ? "prop: worker thread switched on" : "prop: worker thread switched off");
		return;
	}
	switch (gov.state) {
	case GOV_THREADED:
		if (s >= 0.95 || w < gov.next) break;
		gov.thr = s;
		prop_stop_thread(&prop);
		gov.state = GOV_TRY_INLINE;
		break;
	case GOV_TRY_INLINE:
		gov.inl = s;
		if (s > gov.thr * 1.05) {
			pinheck_gov_log("off", gov.thr, s);
			gov.state = GOV_INLINE;
		} else {
			prop_start_thread(&prop);
			gov.state = GOV_THREADED;
		}
		gov.next = w + gov.pause;
		if (gov.pause < 320.0) gov.pause *= 2.0;
		break;
	case GOV_INLINE:
		gov.inl = s;
		if (w < gov.next || prop_start_thread(&prop)) break;
		gov.state = GOV_TRY_THREADED;
		break;
	case GOV_TRY_THREADED:
		if (s > gov.inl * 1.05 || s >= 0.95) {
			pinheck_gov_log("on", s, gov.inl);
			gov.state = GOV_THREADED;
			gov.pause = 10.0;
			break;
		}
		prop_stop_thread(&prop);
		gov.state = GOV_INLINE;
		gov.next = w + gov.pause;
		if (gov.pause < 320.0) gov.pause *= 2.0;
		break;
	}
}

static INTERRUPT_GEN(pinheck_vblank)
{
	if (time_log) fprintf(time_log, "%.6f %.6f\n", timer_get_time(), (double)osd_cycles() / (double)osd_cycles_per_second());
	if (!locals.idle) pinheck_governor();
'''),
(r'''	if (!getenv("PINHECK_THREADS") || atoi(getenv("PINHECK_THREADS")) != 0) prop_start_thread(&prop);
''', r'''	memset(&gov, 0, sizeof(gov));
	if ((!getenv("PINHECK_THREADS") || atoi(getenv("PINHECK_THREADS")) != 0) && prop_start_thread(&prop) == 0) {
		gov.state = GOV_THREADED;
		gov.next = (double)osd_cycles() / (double)osd_cycles_per_second() + 5.0;
		gov.pause = 10.0;
		gov.flip = getenv("PINHECK_THREAD_FLIP") != NULL;
	}
'''),
])
PYEOF
```

- [ ] **Step 4: Rebuild; the contention benchmark passes and the workloads stay byte-identical**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/m9d.log 2>&1; tail -1 build/sdl3pinmame/m9d.log
grep -i warning build/sdl3pinmame/m9d.log | grep -c 'prop\.c\|pinheck\.c'
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh contention
grep -h 'worker thread' tests/pinheck/perf/build/busy/short/prop.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:'
grep -c 'worker thread' tests/pinheck/perf/build/opt/attract/prop.log tests/pinheck/perf/build/opt/video/prop.log
```
Expected (about 8 minutes; speeds vary with the load; on an unloaded machine the governor never switches the workloads, as the last line shows):
```text
[100%] Built target sdl3pinmame
0
contention one: 1.016x with the worker thread requested, 1.012x without; "one CPU allowed" 1 times
contention busy: 0.863x with the worker thread requested, 0.911x without; "worker thread off" 1 times
contention flip: 1.151x with the worker thread requested, 1.029x without; "worker thread switched" 13 times
contention: ok
prop: worker thread off (0.38x with it, 0.94x without)
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.272x over 30.0 s emulated (23.6 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 1.082x, load 2.09
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.110x over 24.0 s emulated (21.6 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.925x, load 1.86
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
tests/pinheck/perf/build/opt/attract/prop.log:0
tests/pinheck/perf/build/opt/video/prop.log:0
```

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck.c tests/pinheck/perf/bench.sh
git commit -m "pinheck: the worker thread stays on only while it is faster (host-time governor, PINHECK_THREAD_FLIP); bench.sh contention"
```

### Task 4: Profile-guided builds

**Files:**
- Create: `cmake/pgo.cmake`, `tests/pinheck/perf/pgo.sh`
- Modify: `cmake/sdl3pinmame/CMakeLists.txt`, `cmake/libpinmame/CMakeLists.txt`, `cmake/pinmame/CMakeLists_win-x64.txt`

**Interfaces:**
- Consumes: Plan 9's `bench.sh attract video` as the training run; the CMake lists' `sdl3pinmame`, `pinmame_shared`, `pinmame_static` and (Windows) `pinmame` targets; `cmake/p2k.cmake`, which copies a target's compile options.
- Produces: `cmake/pgo.cmake` with the cache variables `PINMAME_PGO` (empty, `GEN` or `USE`) and `PINMAME_PGO_DIR` (default `<build dir>/pgo-profile`) and `pinmame_enable_pgo(<target>)`, called by the sdl3pinmame, libpinmame and Windows x64 PinMAME lists before the asmjit and p2k includes; `tests/pinheck/perf/pgo.sh [cmake arguments]` → `build/pgo-use/sdl3pinmame`, with the profile in `build/pgo-profile` (`PGO_JOBS`, `LLVM_PROFDATA`).

Profile-guided builds are an option; the profile stays local (Ruling 8).

- [ ] **Step 1: The options and the script**

```sh
python3 - <<'PYEOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('cmake/sdl3pinmame/CMakeLists.txt', [
(r'''# --- asmjit-based ARM7 JIT backend
# No-op on a target with no JIT backend; see cmake/asmjit.cmake
include(${CMAKE_SOURCE_DIR}/cmake/asmjit.cmake)
pinmame_enable_asmjit(sdl3pinmame)
''', r'''# --- Profile-guided optimisation (PINMAME_PGO), off by default; see cmake/pgo.cmake
include(${CMAKE_SOURCE_DIR}/cmake/pgo.cmake)
pinmame_enable_pgo(sdl3pinmame)

# --- asmjit-based ARM7 JIT backend
# No-op on a target with no JIT backend; see cmake/asmjit.cmake
include(${CMAKE_SOURCE_DIR}/cmake/asmjit.cmake)
pinmame_enable_asmjit(sdl3pinmame)
'''),
])

edit('cmake/pinmame/CMakeLists_win-x64.txt', [
(r'''# --- asmjit-based ARM7 JIT backend
include(${CMAKE_SOURCE_DIR}/cmake/asmjit.cmake)
pinmame_enable_asmjit(pinmame)
''', r'''# --- Profile-guided optimisation (PINMAME_PGO), off by default; see cmake/pgo.cmake
include(${CMAKE_SOURCE_DIR}/cmake/pgo.cmake)
pinmame_enable_pgo(pinmame)

# --- asmjit-based ARM7 JIT backend
include(${CMAKE_SOURCE_DIR}/cmake/asmjit.cmake)
pinmame_enable_asmjit(pinmame)
'''),
])

edit('cmake/libpinmame/CMakeLists.txt', [
(r'''# --- asmjit-based ARM7 JIT backend
include(${CMAKE_SOURCE_DIR}/cmake/asmjit.cmake)
if(TARGET pinmame_shared)
''', r'''# --- Profile-guided optimisation (PINMAME_PGO), off by default; see cmake/pgo.cmake
include(${CMAKE_SOURCE_DIR}/cmake/pgo.cmake)
if(TARGET pinmame_shared)
   pinmame_enable_pgo(pinmame_shared)
endif()
if(TARGET pinmame_static)
   pinmame_enable_pgo(pinmame_static)
endif()

# --- asmjit-based ARM7 JIT backend
include(${CMAKE_SOURCE_DIR}/cmake/asmjit.cmake)
if(TARGET pinmame_shared)
'''),
])
PYEOF
cat > cmake/pgo.cmake <<'EOF2'
# ---------------------------------------------------------------------------
#  Profile-guided optimisation, off by default
#
#  PINMAME_PGO=GEN builds the target instrumented: running it writes a profile
#  to PINMAME_PGO_DIR. PINMAME_PGO=USE builds it with that profile.
#    GCC, MinGW: -fprofile-generate / -fprofile-use; the profile is keyed by
#                object paths relative to the build directory, so another
#                build directory of the same tree can use it
#    Clang:      -fprofile-generate / -fprofile-use=PINMAME_PGO_DIR/default.profdata
#                (merge the .profraw files with llvm-profdata first)
#    MSVC:       /GENPROFILE / /USEPROFILE with PINMAME_PGO_DIR/<target>.pgd,
#                Release only; the instrumented binary needs pgort140.dll and
#                writes <target>!N.pgc when it exits or unloads
#  tests/pinheck/perf/pgo.sh does both sdl3pinmame builds and the training run.
#  The profile is local and never committed.
#
#  Usage, after the target is defined and before cmake/asmjit.cmake and
#  cmake/p2k.cmake (which copy the target's options):
#      include(${CMAKE_SOURCE_DIR}/cmake/pgo.cmake)
#      pinmame_enable_pgo(<target>)
# ---------------------------------------------------------------------------

set(PINMAME_PGO "" CACHE STRING "Profile-guided optimisation: empty, GEN or USE")
set(PINMAME_PGO_DIR "${CMAKE_BINARY_DIR}/pgo-profile" CACHE PATH "Directory of the PGO profile")
if(NOT PINMAME_PGO MATCHES "^(|GEN|USE)$")
   message(FATAL_ERROR "PINMAME_PGO must be empty, GEN or USE")
endif()

function(pinmame_enable_pgo target)
   if(PINMAME_PGO STREQUAL "")
      return()
   endif()
   if(MSVC)
      file(MAKE_DIRECTORY ${PINMAME_PGO_DIR})
      if(PINMAME_PGO STREQUAL "GEN")
         target_link_options(${target} PRIVATE $<$<CONFIG:RELEASE>:/GENPROFILE:PGD=${PINMAME_PGO_DIR}/${target}.pgd>)
      else()
         target_link_options(${target} PRIVATE $<$<CONFIG:RELEASE>:/USEPROFILE:PGD=${PINMAME_PGO_DIR}/${target}.pgd>)
      endif()
   elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      if(PINMAME_PGO STREQUAL "GEN")
         target_compile_options(${target} PRIVATE -fprofile-generate=${PINMAME_PGO_DIR} -fprofile-update=atomic -fprofile-prefix-path=${CMAKE_BINARY_DIR})
         target_link_options(${target} PRIVATE -fprofile-generate=${PINMAME_PGO_DIR})
      else()
         target_compile_options(${target} PRIVATE -fprofile-use=${PINMAME_PGO_DIR} -fprofile-partial-training -fprofile-prefix-path=${CMAKE_BINARY_DIR} -Wno-missing-profile)
      endif()
   else()
      if(PINMAME_PGO STREQUAL "GEN")
         target_compile_options(${target} PRIVATE -fprofile-generate=${PINMAME_PGO_DIR})
         target_link_options(${target} PRIVATE -fprofile-generate=${PINMAME_PGO_DIR})
      else()
         target_compile_options(${target} PRIVATE -fprofile-use=${PINMAME_PGO_DIR}/default.profdata -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
      endif()
   endif()
endfunction()
EOF2
cat > tests/pinheck/perf/pgo.sh <<'EOF3'
#!/bin/sh
# Profile-guided sdl3pinmame (GCC or Clang). Run from the repository root with cmake/sdl3pinmame/CMakeLists.txt
# copied to CMakeLists.txt: build/pgo-gen is instrumented and trained on bench.sh's attract and video workloads,
# then build/pgo-use is built with the profile in build/pgo-profile. Arguments pass to cmake (-DPLATFORM=linux ...).
# Needs P8X32A_ROM and PINHECK_ZIP as bench.sh does; PGO_JOBS (default 6) builds in parallel, LLVM_PROFDATA names
# Clang's llvm-profdata.
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
grep -q 'project(sdl3pinmame' CMakeLists.txt 2> /dev/null || { echo "pgo: run from the repository root with cmake/sdl3pinmame/CMakeLists.txt copied to CMakeLists.txt"; exit 2; }
P=$PWD/build/pgo-profile
J=${PGO_JOBS:-6}
mkdir -p build && rm -rf "$P"
for m in gen use; do
	M=$(echo $m | tr a-z A-Z)
	cmake -S . -B build/pgo-$m -DCMAKE_BUILD_TYPE=Release -DPINMAME_PGO=$M -DPINMAME_PGO_DIR="$P" "$@" > build/pgo-$m.cmake.log 2>&1 || { tail -20 build/pgo-$m.cmake.log; exit 1; }
	cmake --build build/pgo-$m -j$J > build/pgo-$m.log 2>&1 || { tail -20 build/pgo-$m.log; exit 1; }
	[ $m = use ] && break
	SDL3PINMAME=build/pgo-gen/sdl3pinmame tests/pinheck/perf/bench.sh attract video | sed 's/^/pgo training: /'
	if ls "$P"/*.profraw > /dev/null 2>&1; then
		${LLVM_PROFDATA:-llvm-profdata} merge -o "$P/default.profdata" "$P"/*.profraw || exit 1
	fi
	[ -n "$(ls -A "$P" 2> /dev/null)" ] || { echo "pgo: the training run wrote no profile to $P"; exit 1; }
done
echo "pgo: build/pgo-use/sdl3pinmame"
EOF3
chmod +x tests/pinheck/perf/pgo.sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
```

- [ ] **Step 2: Build with a profile**

Run: `flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 5400 tests/pinheck/perf/pgo.sh -DPLATFORM=linux -DARCH=x64; ls build/pgo-profile | wc -l`
Expected (about 25 minutes: two builds and the training run, whose speeds are the instrumented build's):
```text
pgo training: bench attract opt: 0.562x over 30.0 s emulated (53.3 s wall), whole run 15.9 G instructions and 2.2 s CPU per emulated s, worst 100 ms 0.319x, load 2.06
pgo training: bench video opt: 0.489x over 24.0 s emulated (49.0 s wall), whole run 17.3 G instructions and 2.4 s CPU per emulated s, worst 100 ms 0.371x, load 1.62
pgo: build/pgo-use/sdl3pinmame
417
```

- [ ] **Step 3: The profile-guided build against the reference**

```sh
SDL3PINMAME=build/pgo-use/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:'
PINHECK_THREADS=0 SDL3PINMAME=build/pgo-use/sdl3pinmame REFERENCE=build/reference/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | grep -v ' ref:\|determinism first' | sed 's/^/one thread: /'
PINHECK_THREADS=0 SDL3PINMAME=build/sdl3pinmame/sdl3pinmame nice -n 15 ionice -c 3 timeout 9000 tests/pinheck/perf/bench.sh attract video | sed 's/^/one thread, without the profile: /'
```
Expected (speeds vary with the load; the one-thread instruction counts do not):
```text
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.387x over 30.0 s emulated (21.6 s wall), whole run 11.9 G instructions and 1.0 s CPU per emulated s, worst 100 ms 0.964x, load 2.83
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.222x over 24.0 s emulated (19.6 s wall), whole run 12.8 G instructions and 1.1 s CPU per emulated s, worst 100 ms 1.047x, load 2.36
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread: bench attract opt: 1.144x over 30.0 s emulated (26.2 s wall), whole run 11.8 G instructions and 0.8 s CPU per emulated s, worst 100 ms 0.969x, load 1.82
one thread: determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread: bench video opt: 1.026x over 24.0 s emulated (23.4 s wall), whole run 12.7 G instructions and 0.9 s CPU per emulated s, worst 100 ms 0.893x, load 1.46
one thread: determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
one thread, without the profile: bench attract opt: 1.048x over 30.0 s emulated (28.6 s wall), whole run 12.5 G instructions and 0.9 s CPU per emulated s, worst 100 ms 0.901x, load 1.20
one thread, without the profile: bench video opt: 0.946x over 24.0 s emulated (25.4 s wall), whole run 13.5 G instructions and 0.9 s CPU per emulated s, worst 100 ms 0.806x, load 1.30
```

- [ ] **Step 4: The scripted game with and without the profile**

The game was not part of the training. Its four logs must be identical, and the profile-guided build executes fewer instructions:

```sh
for b in sdl3pinmame pgo-use; do
	perf stat -e task-clock,instructions:u -o build/game-$b.perf env SDL3PINMAME=$PWD/build/$b/sdl3pinmame nice -n 15 ionice -c 3 timeout 6000 tests/pinheck/game/pinmame_game.sh | tail -1
	echo "load $(cut -d' ' -f1 /proc/loadavg)"
	rm -rf build/game-$b && cp -r tests/pinheck/game/build/pinmame build/game-$b
done
for f in link2.log out2.log uart2.log frames2.bin; do cmp build/game-sdl3pinmame/$f build/game-pgo-use/$f && echo "$f same"; done
grep -h 'elapsed\|instructions' build/game-sdl3pinmame.perf build/game-pgo-use.perf
rm -rf build/game-sdl3pinmame build/game-pgo-use
```
Expected (about 12 minutes):
```text
pinmame game: ok
load 1.65
pinmame game: ok
load 1.51
link2.log same
out2.log same
uart2.log same
frames2.bin same
 4,899,388,168,588      instructions:u                                                        
     290.087774466 seconds time elapsed
 4,624,164,639,163      instructions:u                                                        
     265.280395997 seconds time elapsed
```

- [ ] **Step 5: Commit, without any profile**

```bash
git add cmake/pgo.cmake cmake/sdl3pinmame/CMakeLists.txt cmake/libpinmame/CMakeLists.txt cmake/pinmame/CMakeLists_win-x64.txt tests/pinheck/perf/pgo.sh
git status --short | grep -v '^??'
git commit -m "Profile-guided builds: cmake/pgo.cmake (PINMAME_PGO=GEN|USE; GCC, Clang, MSVC) for sdl3pinmame, libpinmame and Windows PinMAME x64; tests/pinheck/perf/pgo.sh"
```
Expected from `git status` before the commit (nothing else; `build/` and its profile are ignored):
```text
M  cmake/libpinmame/CMakeLists.txt
A  cmake/pgo.cmake
M  cmake/pinmame/CMakeLists_win-x64.txt
M  cmake/sdl3pinmame/CMakeLists.txt
A  tests/pinheck/perf/pgo.sh
```

### Task 5: Regression, the scripted game, results, and the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` (§4), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` (the m9d row, "Carried into release")

- [ ] **Step 1: Every suite and machine check**

```sh
export SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame
for s in mips32 pic32mx link p8x32a display audio board; do nice -n 15 ionice -c 3 tests/pinheck/$s/check.sh > build/m9d-$s.log 2>&1; echo "$s: exit $? $(tail -1 build/m9d-$s.log)"; done
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 tests/pinheck/storage/check.sh > build/m9d-storage.log 2>&1; echo "storage: exit $? $(tail -1 build/m9d-storage.log)"; rm -rf tests/pinheck/storage/build
P8X32A_JIT=1 nice -n 15 ionice -c 3 tests/pinheck/p8x32a/check.sh > build/m9d-p8x32a-jit.log 2>&1; echo "p8x32a translated: exit $? $(tail -1 build/m9d-p8x32a-jit.log)"
for c in pic32mx/pinmame_check.sh display/pinmame_display.sh audio/pinmame_audio.sh board/pinmame_board.sh display/pinmame_look.sh; do nice -n 15 ionice -c 3 timeout 7200 tests/pinheck/$c > build/m9d-$(basename $c .sh).log 2>&1; echo "$c: exit $? $(tail -1 build/m9d-$(basename $c .sh).log)"; done
```
Expected (about 20 minutes once the RTL boot references are cached; the storage suite's images, about 7 GB, are deleted after it passes):
```text
mips32: exit 0 mips32: 208 passed, 0 failed
pic32mx: exit 0 pic32mx: 0 failed
link: exit 0 link: 0 failed
p8x32a: exit 0 p8x32a: 233 passed, 0 failed
display: exit 0 display: 0 failed
audio: exit 0 audio: 0 failed
board: exit 0 board: 0 failed
storage: exit 0 storage: 0 failed
p8x32a translated: exit 0 p8x32a: 233 passed, 0 failed
pic32mx/pinmame_check.sh: exit 0 pinmame: ok
display/pinmame_display.sh: exit 0 pinmame display: ok
audio/pinmame_audio.sh: exit 0 pinmame audio: ok
board/pinmame_board.sh: exit 0 pinmame board: ok
display/pinmame_look.sh: exit 0 pinmame look: ok
```

- [ ] **Step 2: The scripted game with the reference and this build**

```sh
for b in reference sdl3pinmame; do
	perf stat -e task-clock,instructions:u -o build/game-$b.perf env SDL3PINMAME=$PWD/build/$b/sdl3pinmame nice -n 15 ionice -c 3 timeout 6000 tests/pinheck/game/pinmame_game.sh | tail -1
	echo "load $(cut -d' ' -f1 /proc/loadavg)"
	rm -rf build/game-$b && cp -r tests/pinheck/game/build/pinmame build/game-$b
done
for f in link2.log out2.log uart2.log frames2.bin; do cmp build/game-reference/$f build/game-sdl3pinmame/$f && echo "$f same"; done
grep -h 'elapsed\|instructions' build/game-reference.perf build/game-sdl3pinmame.perf
rm -rf build/game-reference build/game-sdl3pinmame
```
Expected (about 12 minutes; the second run is this build):
```text
pinmame game: ok
load 2.50
pinmame game: ok
load 1.66
link2.log same
out2.log same
uart2.log same
frames2.bin same
 4,915,647,745,915      instructions:u                                                        
     298.301684268 seconds time elapsed
 4,900,772,496,468      instructions:u                                                        
     294.158729544 seconds time elapsed
```

- [ ] **Step 3: Update the spec and the roadmap**

```sh
python3 - <<'PYEOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md', [
(r'''  - Both threads spin up to 20,000 pause instructions before they block, so while the emulation runs the process uses 1.2–1.4 CPU seconds per emulated second (up to two CPUs unthrottled).
- **Proof for every stage:**
''', r'''  - Both threads spin up to 20,000 pause instructions before they block, so while the emulation runs the process uses 1.2–1.4 CPU seconds per emulated second (up to two CPUs unthrottled). Plan 9d bounds the spin by time instead.
- **Established by Plan 9d (tuning):**
  - Measured after Plan 9c (Linux, one CCX): the PIC32 thread waits for the worker at about 28,200 syncs per emulated second, 27,074 of them `RF13` reads, and spends 460–550 ms per emulated second waiting; the worker is the bound (about 700 ms busy and 90 ms idle). Padding the queue's fields, answering `RF13` on the worker, running the worker ahead of the PIC32's calls (every output stays byte-identical under extra cut points once the frame log's stamp comes from the frame's own time, but the extra catch-ups cost the worker what they save) and thread placement (Windows already keeps both threads in one CCX) were measured without gain and not adopted.
  - The handoff collapses where CPUs are scarce: 0.044–0.073× with one allowed CPU and 0.040–0.055× on two CPUs shared with a busy process, against 0.83–0.88× inline. A wait now spins at most 50 µs by the clock and then blocks (the same speed, 1.2 instead of 1.4 CPU seconds per emulated second); the worker is not started when the process may use only one CPU; and the driver switches the worker off for as long as the inline path is faster, judged over seconds of host time. Both paths give the same output, so switching changes only the speed. `bench.sh contention` checks the three cases and requires their outputs byte-identical.
  - Profile-guided builds (`cmake/pgo.cmake`: `PINMAME_PGO=GEN|USE` for sdl3pinmame, libpinmame and the Windows standalone build; `tests/pinheck/perf/pgo.sh`) run 8–10% faster on Linux (GCC 15, trained on attract mode and video): 1.39× attract and 1.22× video with the worker (loads 2.4–2.8), the scripted game in 4.62 T instead of 4.90 T host instructions, every output byte-identical. The profile needs the firmware and stays local, so binaries built without it keep Plan 9c's speed.
- **Proof for every stage:**
'''),
])

edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
(r'''| release | release: destination decided''', r'''| `2026-10-01-pinheck-m9d-tuning.md` | 9d, tuning: waits bounded by time, no worker with one allowed CPU, the worker switched off while the inline path is faster, `bench.sh contention`, profile-guided builds (`PINMAME_PGO`, `pgo.sh`; MSVC for the Windows standalone build) | Plan 10 | none | byte-identical to the reference build (worker on, off, switched; profile-guided build; scripted game); one CPU and two contended CPUs at ≥ 0.9× and ≥ 0.8× of the inline speed (0.044× and 0.040× before); profile-guided build +8–10% on Linux (1.39× attract, 1.22× video); Windows checklist | written |
| release | release: destination decided'''),
(r'''What remains for the aim of 1.2× with video: the worker's idle time after `RF13` reads (a frame log stamp that does not depend on the PIC32's calls would let the worker run ahead), the translated blocks' entry and exit, and the pin change points of the SD and display links. `bench.sh` with `REFERENCE` and the scripted game stay the determinism checks.
''', r'''What remains for the aim of 1.2× with video: the worker's idle time after `RF13` reads (a frame log stamp that does not depend on the PIC32's calls would let the worker run ahead), the translated blocks' entry and exit, and the pin change points of the SD and display links. `bench.sh` with `REFERENCE` and the scripted game stay the determinism checks.
- After Plan 9d: a profile-guided build reaches the aim of 1.2× for attract mode and video on Linux at the loads measured (1.39× and 1.22×, loads 2.4–2.8); a release built without a profile keeps Plan 9c's speed (1.27× and 1.11× at load 2). Measured and not adopted (Plan 9d Rulings 2–4, 9): thread placement, padding the queue's fields, answering `RF13` on the worker, running the worker ahead (every output stays byte-identical under extra cut points with the frame stamp taken from the frame's time, but each extra catch-up costs the worker about 0.45 µs), and Propeller-core changes whose profile shares are the scheduler's search (3.4% of the worker), the translated blocks' entry and tail (about 5%) and the pin change points (about 15%). The Windows checklist of Plan 9d (the one-LP and contended cases, MSVC profile-guided build) is open.
'''),
])
PYEOF
```

- [ ] **Step 4: Mutations**

Apply each mutation, run the named check, confirm the named failure line (other failure lines may follow), then restore the file with `git checkout <file>`. The link suite runs as `PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= tests/pinheck/link/check.sh` (about 2 minutes); `bench.sh contention` needs the build (`cmake --build build/sdl3pinmame -j6`) and takes 10–15 minutes with a mutated governor.

| File | Replace → with | Check | Must show |
|---|---|---|---|
| `src/wpc/pinheck/prop.c` | in `prop_start_thread`, `if (cpus_allowed() < 2) {` → `if (cpus_allowed() < 1) {` | link | `PROP FAIL prop_test.c:416: prop_start_thread(&p) == -1 && p.worker == NULL` |
| `src/wpc/pinheck/prop.c` | `#define PROP_SPIN_NS 50000 ` → `#define PROP_SPIN_NS 500000 ` | link | `PROP FAIL prop_test.c:391: cpu_s() - c0 < 0.01` |
| `src/wpc/pinheck/prop.c` | in `spin`, `else if (n - *t0 >= PROP_SPIN_NS) return 1;` → `else if (0) return 1;` | link | `PROP FAIL prop_test.c:354: cpu_s() - c0 < 0.05` |
| `src/wpc/pinheck.c` | in `pinheck_governor`, `if (s >= 0.95 \|\| w < gov.next) break;` → `if (1) break;` | `bench.sh contention` | `CONTENTION FAIL busy: "worker thread off" not logged` |
| `src/wpc/pinheck.c` | in `pinheck_governor`, `if (s > gov.thr * 1.05) {` → `if (0) {` | `bench.sh contention` | `CONTENTION FAIL busy: "worker thread off" not logged` |

Then `git status --short src tests` prints nothing, and the build is rebuilt from the restored sources.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "Spec: Milestone 9d measured; bounded waits, one-CPU guard, worker governor, profile-guided builds"
```

## Windows checklist

For the Windows session (Threadripper 3970X, Windows 11, VS2022 BuildTools / MSVC 14.44, `C:\code\dominos`), after Plan 9d is pushed. Results come back by message; a Windows-only fix comes back as a diff with a test, applied on Linux. Never push.

Common setup: the romset and `pinheck.zip` in `roms\` as before; `set PINHECK_RTC=1790683200` (the benchmark's fixed clock) for every run below; a private `-nvram_directory` and `-cfg_directory` per run; `-nothrottle`, sound on the dummy/silent device or muted. A run of `N` frames for the speed windows: `PinMAME.exe dominos -nothrottle -frames_to_run N -skip_gamewarnings` with `PINHECK_TIME_LOG=time.log`, `PINHECK_UART1_LOG=uart.log`, `PINHECK_FRAME_LOG=frames.bin`, `PINHECK_WAV=snd.wav`, `PINHECK_PROP_LOG=prop.log`; the speed of the window from 10 s of emulated time on is
`python -c "t=[tuple(map(float,l.split())) for l in open('time.log')]; t=[x for x in t if x[0]>=10]; print('%.3fx' % ((t[-1][0]-t[0][0])/(t[-1][1]-t[0][1])))"`.
Before the timed runs, boot once with `PINHECK_INSERVICE=6` for 1200 frames and copy that NVRAM directory for each run, as `bench.sh` does. The video workload is the attract run with `PINHECK_UART1_SEND_AT=10 PINHECK_UART1_SEND_GAP=1 PINHECK_UART1_SEND=[E96000]~[V00AT9]~[F00ZM0]~[F00BWI]~[F00IF0]~[F00NBI]~~~~~~~~~~[F00NBI]~~~~[F00IF0]`, timed from 16 s.

1. **Build.** MSVC x64 standalone PinMAME, VPinMAME64 and libpinmame at the Plan 9d commit, exactly as CI does. Expected: all build; no warning in `prop.c`, `pinheck.c`.
2. **Determinism across the new paths.** Attract, 2400 frames, four runs from the same NVRAM copy: default; `PINHECK_THREADS=0`; `PINHECK_THREAD_FLIP=1`; `start /affinity 1` (one LP). Expected: `uart.log`, `frames.bin`, `snd.wav` and the NVRAM identical in all four; `prop.log` has `worker thread switched` lines in the third and `prop: one CPU allowed, no worker thread` in the fourth.
3. **Speed, Plan 9c's build against Plan 9d's.** Attract and video, 2400 frames, default and `PINHECK_THREADS=0`, each build twice, alternating. Report speeds and CPU time (`Measure-Command` or the process's CPU time) per emulated second. Expected: speeds as before (0.90–0.93× attract), CPU time lower with Plan 9d (the 50 µs spin); no `worker thread off` line in `prop.log` on an idle machine.
4. **One logical CPU.** `start /affinity 1 PinMAME.exe ...` attract with Plan 9d. Expected: `prop: one CPU allowed, no worker thread`, speed within 10% of the `PINHECK_THREADS=0` run on one LP (0.70×), against 0.073× before.
5. **Two contended LPs.** A busy loop on LP 0 (`start /affinity 1 powershell -NoProfile -Command "while ($true) {}"`, stopped after the run) and `start /affinity 5 PinMAME.exe ...` (LPs 0 and 2, two cores) attract, with Plan 9c's and Plan 9d's build and with `PINHECK_THREADS=0`. Expected: Plan 9d logs `prop: worker thread off (...)` and runs at least 0.8× the `PINHECK_THREADS=0` speed; report Plan 9c's speed in the same setting.
6. **Placement unchanged.** Plan 9d's default attract speed against the table of 2026-09-30 (default 0.930/0.901×, same CCX 0.921/0.930×). Expected: the same within noise (Ruling 2).
7. **Profile-guided builds (MSVC).** Measured against the same-box medians of 2026-09-30 (libpinmame at `04b2ec55`, attract from 20 to 50 s, `PINHECK_RTC` fixed, medians of three): MSVC 0.626× (one thread, no translator), 0.690× (one thread), 0.707× (with the worker); MinGW GCC 15.2 0.719×, 0.685×, 0.817×. From a VS2022 x64 developer prompt (it puts `pgort140.dll` on the `PATH`), for libpinmame and then for PinMAME.exe:
   ```bat
   copy cmake\libpinmame\CMakeLists.txt CMakeLists.txt
   cmake -G "Visual Studio 17 2022" -A x64 -DPLATFORM=win -DARCH=x64 -DPINMAME_PGO=GEN -DPINMAME_PGO_DIR=C:/code/dominos/pgo-lib -B build-pgo-lib
   cmake --build build-pgo-lib --config Release
   ```
   Train with the instrumented library the way the medians were taken (attract, and the video workload once; it runs slower), ending each run normally so that the DLL unloads and writes `pinmame_shared!N.pgc` (move any `.pgc` that appears beside the DLL into `C:\code\dominos\pgo-lib`). Then:
   ```bat
   cmake -DPINMAME_PGO=USE build-pgo-lib
   cmake --build build-pgo-lib --config Release
   ```
   and the same for the standalone build with `cmake\pinmame\CMakeLists_win-x64.txt`, `-DPINMAME_PGO_DIR=C:/code/dominos/pgo-exe`, `-B build-pgo-exe` (its profile is `pinmame.pgd`, its counts `pinmame!N.pgc`), trained on item 3's attract and video runs. Expected: the second link of each reports the profile in use (`Merging ...pgc`, "N of M functions ... optimized for speed"); the three median configurations again with the profile-guided libpinmame; for PinMAME.exe attract and video against the build without a profile (alternating runs); the outputs of item 2's default run and of the libpinmame attract run identical with and without the profile (the MSVC builds use `/fp:fast`, so this is the check that the profile changed no floating-point result); the scripted game still ends with score 32176700 and its logs equal the build's without a profile. If a second link reports no profile data, merge the counts by hand (`pgomgr /merge <file>.pgc <dir>\\<target>.pgd`) and link again. Do not commit or send the `.pgd`/`.pgc` files.
8. **VPinMAME with the test table** (Route A, as for Milestone 10): play a minute with VPX. Expected: nothing new in the log on a machine with free cores; on a contended machine a `worker thread off` line may appear in the Propeller log and the game keeps running.

## Result

Measured in the replay on the Ryzen 7 7730U while other agents' emulators and builds shared it (at times seven emulators at nice 5); the load average is printed with every speed, and the speeds of one row were not all taken at the same load. The one-thread host instructions per emulated second do not depend on the load.

| Build | attract with the worker (load) | video with the worker (load) | CPU s per emulated s, attract / video | attract / video on one thread (load) | G host instructions per emulated s, one thread |
|---|---|---|---|---|---|
| reference (Plan 9c, `769d7dc0`) | 1.096× (4.3); prototype session 1.262× (2.9) | 0.833× (4.6); prototype session 1.090× (2.4) | 1.4 / 1.8 | 0.755× / 0.775× (4.3 / 3.7); 0.903× pinned to CPU 2 (3.4) | 12.5 / 13.5 |
| Task 2: bounded waits, one-CPU guard | 1.151× (3.9) | 1.015× (2.8) | 1.2 / 1.3 | 0.949× / 0.847× (2.4 / 2.2) | 12.5 / 13.5 |
| Task 3: worker governor | 1.272× (2.1) | 1.110× (1.9) | 1.1 / 1.2 | 1.048× / 0.946× (1.2 / 1.3) | 12.5 / 13.5 |
| Task 4: profile-guided build | 1.387× (2.8) | 1.222× (2.4) | 1.0 / 1.1 | 1.144× / 1.026× (1.8 / 1.5) | 11.8 / 12.7 |

Every row is byte-identical to the reference in UART1, frames, sound and NVRAM, with the worker on and off. `bench.sh contention` (10 s of attract mode, each case against the same build without the worker, one after the other):

| Case | reference | this plan |
|---|---|---|
| one CPU (`taskset -c 2`) | 0.047× against 0.931× | 1.016× against 1.012×, `prop: one CPU allowed, no worker thread` |
| two CPUs, a busy loop on one | 0.043× against 0.830× | 0.863× against 0.911×, `prop: worker thread off (0.38x with it, 0.94x without)` |
| worker switched every second | (no hook) | 1.151× against 1.029×, 13 switches, outputs identical |

The scripted game (335 s of play in two launches with its checks, logs byte-identical in every pair): reference 4.92 T host instructions in 298 s (load 2.5), this plan's build 4.90 T in 294 s (load 1.7); without and with the profile 4.90 T in 290 s and 4.62 T in 265 s (loads 1.7 and 1.5), 1.26× including its launches and checks.

Where the time goes (Task 1 Step 3, the reference with the worker): the worker 54% of the samples, of which the local loop 25%, translated code 22%, its idle spin 12%, `flush` 12%, the scheduler 8%, `hub_rw` 5%, `ctr_next` 3%; the PIC32 thread 46%, of which `prop_sync` 52% and the MIPS32 and SoC 44%. The Propeller thread stays the bound; the waits it causes are the lock step at `RF13` reads (Ruling 1), which the measured alternatives did not shorten (Rulings 3, 4).

What real time with margin needs now: on this machine at the loads measured, a profile-guided build runs attract mode and video above the aim of 1.2× (1.39× and 1.22×); a build without a profile runs them at 1.27× and 1.11× (load 2), as Plan 9c did, using 1.1–1.2 CPU seconds per emulated second instead of 1.4–1.8 (12% less at equal load, Ruling 5). Where CPUs are scarce the speed now follows the inline path (0.86–1.02× in the contended cases against 0.04× before). On the Windows 3970X the gap to Linux is per-core speed and MSVC's code for the C paths (Ruling 8); the checklist's profile-guided build is the lever measured here, and its result decides whether a Windows release ships with a profile. What remains on the Propeller thread (Ruling 9) and the run-ahead finding (Ruling 4) are carried into the release list.

## Self-Review

**Spec coverage** (addendum §4 and the brief of this stage):

| Requirement | Where |
|---|---|
| real time with margin, aim ≥ 1.2× attract and video | Task 4 (profile-guided build: 1.2× attract and about 1.1× video at the loads measured); Tasks 2–3 keep the margin where CPUs are scarce; the Result |
| byte-identical determinism | `bench.sh` with `REFERENCE` after Tasks 2, 3 and 4 (worker on and off, profile-guided build); `bench.sh contention` (one CPU, contended CPUs, switching); the scripted game against the reference (Task 5) and with and without the profile (Task 4) |
| all suites green, including the RTL, spinsim and QEMU differentials | Task 2 Step 4 (link suite with wine), Task 5 Step 1 (every suite, the p8x32a suite also translated, every machine check) |
| measure first (profile, sync counts, spin and blocking, false sharing, idle) | Task 1 Steps 2–3; Rulings 1–4 (the counting build's figures, `perf c2c`, cache-to-cache fills) |
| thread placement for any topology, respecting the affinity mask, opt-out | measured, not implemented (Ruling 2); the one-CPU guard reads the affinity mask and never changes it (Ruling 6) |
| handoff cost: padding, spin-then-block per platform, fewer syncs | padding and fewer syncs measured and rejected (Rulings 3, 4); the spin bounded by time on every platform (Ruling 5) |
| the Propeller thread | profile shares weighed, no core change (Ruling 9) |
| profile-guided optimisation, Linux measured, Windows steps, profiles not committed | Task 4, Ruling 8, Windows checklist item 7 |
| the single-LP collapse (controller) | Task 2 (guard), Task 3 (governor), `bench.sh contention`, Windows checklist items 4–5 |

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay (speeds as measured, with the load).

**Type consistency:** `static int spin(uint64_t *t0, unsigned *k)`, `static uint64_t now_ns(void)` and `static int cpus_allowed(void)` are defined once per platform branch of `prop.c` and used by `wait_head`, `worker` and `prop_start_thread`; `PROP_SPIN_NS` replaces `PROP_SPIN` everywhere it was used. `static void pinheck_governor(void)` and `static void pinheck_gov_log(const char *, double, double)` are defined before `pinheck_vblank`, which calls the governor; `gov` is reset in `MACHINE_INIT` before `prop_start_thread`. `pinmame_enable_pgo(target)` is defined in `cmake/pgo.cmake` and called after each target is defined and before `pinmame_enable_asmjit` and `pinmame_enable_p2k`. `bench.sh`'s `run` takes the same arguments as before; `BENCH_MACHINE` and `BENCH_CPUS` are read only inside it.

**Review Focus:** five items, each pinned by the named tests in Tasks 2–5 and the mutations of Task 5.

**Proof:** every file and edit this plan writes was extracted from this document (its edit scripts, `cmake/pgo.cmake` and `tests/pinheck/perf/pgo.sh`) into a fresh worktree of `pinheck` at `769d7dc0`, task by task, and the files it produced are byte-identical to the proven prototype's; every Expected output above was reproduced there on a machine shared with other agents' emulators and builds (the load averages are printed with the speeds). Each mutation of Task 5 was caught with the named line, and the tree was clean afterwards.
