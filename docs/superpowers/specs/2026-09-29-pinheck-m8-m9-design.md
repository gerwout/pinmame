# pinHeck / Domino's: board I/O and playfield (Milestone 8), performance (Milestone 9), display look, release — design addendum

Status: approved design, pre-implementation
Date: 2026-09-29
Parent spec: `2026-09-28-pinheck-dominos-design.md` (binding where this addendum is silent). Sibling: `2026-09-29-pinheck-m6-m7-display-audio-design.md`.

## 1. Goal

Make Domino's playable and verifiable in standalone `sdl3pinmame`, at real time on the reference machine:

- every lamp, coil, switch, GI/flasher, RGB and servo line works;
- a full game can be played by hand from the keyboard;
- a scripted headless game proves the whole machine end to end.

Then apply the display settings, and prepare the branch for release.

## 2. Existing infrastructure reused

All of it is already in `pinheck`.

- **Scripted input:** `-key_script` (`src/keyscript.c`), frame-based `DOWN`/`UP`/`TAP`/`MARK`/`QUIT` of input codes.
- **Headless runs:** runs through `remote_debug` that terminate and accept scripted input.
- **Frame dumps:** `-dmd_dump_dir`, the DMD frame dump.
- **Playfield simulator:** `src/wpc/sim.[ch]` and the per-game simulators in `src/wpc/sims/`, which cover ball states, solenoid-driven transitions and key-to-switch tables.
- **JIT assembler:** `ext/asmjit`, vendored and part of the cmake build.

## 3. Milestone 8: board I/O, playfield simulator, scripted game

### 3.1 Board logic

`src/wpc/pinheck/board.[ch]` is pure C and C89-syntax-clean. It turns timestamped PIC32 GPIO edges into PinMAME outputs, numbered as in parent spec §4.5.

- **Lamp matrix:** columns `PIC_LIGHT_C_0..7` drive TIP107s and rows `PIC_LIGHT_R_0..7` drive TIP102s. Each lamp's on/off intervals feed PinMAME's PWM integration, so the firmware's 8 brightness levels come out of its own strobing.
- **Solenoids:** `PIC_SOL_0..23` are modulated outputs 1–24.
- **Switch matrix:** the driven and sensed lines, and the scan order, are established from `DOM_V006.PRG`. Sensed reads come from `coreGlobals.swMatrix` for the currently driven line.
- **Cabinet switches:** the 74HC165 chain (`CAB_LAT`, `CAB_CLK`, `CAB_SWITCH_IN`) carries 16 cabinet inputs in the chain order of parent spec §2.2.
- **GI and flashers:** the 74HC595 chain (`CAB_GI_OUT`, `CAB_CLK`, `!DATA_LAT`) drives GI/flasher 0–15. The GEN-gated handling of PinMAME solenoids 37–44 in `core.c` is resolved so these outputs appear.
- **RGB:**
  - The two on-board WS2801s on `RGB_DATA`/`RGB_CLK` give six channels, custom solenoids 51–56.
  - The external WS2801 chain on `EXT_RGB_DATA`/`EXT_RGB_CLK` gets its length from the firmware (parent spec open item 3), three custom outputs per LED after 61.
- **Servos:** pulse widths on the 5 servo pins give positions, custom solenoids 57–61, normalised over 1.0–2.0 ms.
- **Start button lamp:** `ST_LI_GATE`.

### 3.2 Items carried into Milestone 8

- **PinMAME time inside board callbacks:** `pic32cpu_ICount` is kept current around every board callback and `activecpu_abort_timeslice` is honoured. A test shows two GPIO edges in one slice receive distinct `timer_get_time()` values.
- **PORTB inputs:** they read according to `AD1PCFG`.
- **Byte and halfword writes:** such writes to `PORTx` and `TMRx` merge with LAT and the timer registers, not with separate storage.
- **I²C collisions:** an I²C command bit written while another command runs is cleared and sets `IWCOL`.
- **libpinmame:** its `PINMAME_HARDWARE_GEN` gains `PINHECK`.

### 3.3 Playfield simulator

`src/wpc/sims/pinheck/dominos.c` is built on the existing framework and models only the mechanics the rules depend on:

- the ball trough and its switches, with solenoid 17 loading the shooter lane;
- the autolauncher (16);
- the left and right scoops (8, 12);
- the up-post (6) and magnet (5);
- the Noid and target-bank servos;
- the outlanes and drain back into the trough.

Measured by Plan 8a, for the simulator: the firmware times coil pulses in units of its main-loop counter (`[MXXzzz]` clamps 15 × zzz to 255 units, 42.4 ms emulated for any zzz ≥ 17; the solenoid test pulses the knocker 5.5 ms and the pops 10 ms), so the simulator reacts to solenoid edges, not to pulse lengths. The Noid is servo 0 driven as a continuous-rotation servo (544 µs one way, 2,400 µs the other, pulses stopped = stop); the board reports the stop as a detach event.

Playfield switches get keyboard keys. The cabinet keys follow PinMAME's usual layout: coin, start, both flippers, tilt, and Enter/Back/Menu/User.

Established by Plan 8b:

- **Numbers.** The coil numbers above are the documents' 0-based ones: PinMAME solenoids 18 (trough), 17 (autolauncher), 9 and 13 (scoops), 7 (up-post) and 6 (magnet). Domino's game definition lives in the simulator file, as for PinMAME's other full simulators.
- **Coils as edges.** The simulator reads a coil as on when it was on at any time during the last frame (`coreGlobals.solenoids`, built by the board block), so pulses of 3–20 ms move the ball exactly once.
- **Mechanics.** The shooter lane has a manual plunger; the firmware autolaunches only ball saves and multiball balls. The optos are the Center Ramp (95) and the Oven Ramp (96), whose handler energises the magnet. The orbit handlers raise the up-post: a ball meeting it comes back down its orbit. The firmware homes the Noid (servo 0, continuous rotation) by Noid Home (58); the target bank (servo 1) is up at 646 µs and down at 1,621 µs, and its switches register in both positions.
- **Framework.** `sim_tSimData` gained an opt-in `autoBall`: while the selected ball is off the playfield, the keys move a ball on it, which multiball needs.

### 3.4 Verification

- **Unit level:** `board` is tested with synthetic GPIO edges: lamp strobing at each brightness level, both shift-register chains bit by bit, WS2801 frames, servo pulses, and a switch-matrix scan in the firmware's order.
- **Service tests (parent spec check 5):** driven by `-key_script` in `sdl3pinmame dominos -headless`.
  - The switch test reports every switch the simulator closes.
  - The lamp and solenoid tests activate exactly the documented output.
  - The servo and RGB tests move the right outputs.
  - `[MXXzzz]` and `[LXXzzz]` on UART1 check every coil and lamp number against `docs/Dominos-Switch-Matrix.pdf`, `Dominos-Lamp-Matrix.pdf` and `Spooky_Pinball_Domino's_Solenoid_List.pdf`.
- **Scripted game:** a headless `-key_script` run inserts a coin, starts a game, plays a switch sequence, drains every ball, and completes match and high-score entry.
  - It asserts on the firmware's own UART1 debug output (`PLAYER:%d BALL:%d SCORE: %luK`, `[E11000]` mode dumps), on decoded display frames (Milestone 6), and on lamp and solenoid activity.
  - It stays in the suite as the regression test for every later change.
  - Established by Plan 8b: `DOM_V006` formats `PLAYER:%d BALL:%d …` into the text it sends to the Propeller and `[E11000]` prints only an empty line, so the game's own record is the PIC32-to-Propeller link: `PINHECK_LINK_LOG` logs every packet (score, status, scrolling text, clip, high-score table). UART1 carries the firmware's mode prints (`pizzaDispatchModeStart[player]`, `COLLECTING JP??`, `MODES ARE ENDING`) and the high scores sent after the game.
  - `PINHECK_RTC` seeds the DS1340 instead of host time; the firmware's random choices follow the clock, and with it fixed two runs of the scripted game give byte-identical link, board, UART1 and frame logs. The game (`tests/pinheck/game/pinmame_game.sh`: three balls, a multiball, `ACE` at the top of the high-score table, the match) plays 315 s of emulated time, about 50 minutes at today's speed.

## 4. Milestone 9: performance

- **Target:** emulated/real time of at least 1.0 on the reference machine, an AMD Ryzen 7 7730U (8 cores, 16 threads), with the sound stream never under-running. It is measured by a headless benchmark over three workloads:
  - attract mode;
  - the scripted game;
  - video with four audio channels.
- **Baseline:** 0.15–0.36× today. The interpreters run at `mips32` ~110 M instructions/s (~80 needed) and `p8x32a` ~50 M cycles/s with one busy cog (104 needed).
- **Stages**, taken only as far as needed:
  1. `perf` profile.
  2. Interpreter fast paths: pre-decoded instructions, direct RAM/flash fetch that bypasses bus callbacks for memory, cheaper hub-slot and idle-cog handling.
  3. A JIT through the vendored `asmjit`, only for the core the profile still blames.
  4. Multi-threading only if the result stays deterministic.
- **Established by Plan 9 (stages 1 and 2):**
  - `tests/pinheck/perf/bench.sh` times attract mode (10–40 s) and a video with music and three effects (16–40 s) headless, with the DS1340 seeded from a fixed clock (without it UART1 and the frames differ between runs), and requires byte-identical UART1, frame, sound and NVRAM output against a reference build; `bench.sh profile` groups a `perf` profile of a `-g` build by component.
  - The reference build (`3d9971c3`) executed 111 G host instructions per emulated second in attract mode and 115 G with video and four channels (0.07–0.13× real time on the shared machine); 85% of its time was in the Propeller core and 10% in the PIC32.
  - Stage 2 is exact: the RTL, spinsim and QEMU suites and every machine check pass, and both workloads stay byte-identical to the reference. Host instructions per emulated second, attract / video: Propeller fast paths 43 / 45 G, idle loops 29 / 31 G, MIPS32 fast paths 22 / 24 G, about 5× fewer than the reference; 0.45–0.53× (attract) and 0.40–0.47× (video) real time at load averages 3–5.
  - After stage 2 the Propeller core takes about 60% of the time and the PIC32 about 33%: the two cogs running the ROM Spin interpreter (about 16 M instructions/s each) and the MIPS32 interpreter. Real time needs the machine below one CPU second per emulated second, more than 2× less than now: stage 3 must bring the Propeller core from about 1.2 s to about 0.45 s and the PIC32 from about 0.7 s to about 0.35 s per emulated second (a JIT for the Spin-interpreter cogs and for the MIPS32 code in flash); stage 4 (the Propeller and the PIC32 on two threads) would bound the total by the larger of the two.
- **Established by Plan 9b (stages 3 and 4):**
  - After Plan 9 the Propeller's cost was in its events and pin changes as much as in its instructions: per emulated second in attract mode about 42 M local cog instructions (about 100 host instructions each), 8 M scheduler events and 5.6 M pin change points, nearly all NCO toggles of the SD card and display link clocks. Stage 3 made the pin and counter paths, the local loop and the hub operations cheaper and exact (decoded instructions are checked by their whole word, so self-modifying code needs no invalidation), gave the MIPS32 a fast loop that hands every other instruction to the unchanged step, and translates runs of local cog instructions to x86-64 with the vendored asmjit, block by block, with writes to translated slots dropping their blocks and slots whose S or D field keeps changing read at run time. The translator is optional: CMake builds with asmjit on x86-64 use it, every other build interprets with the same results.
  - Stage 4 runs the Propeller's calls on a worker thread in their sequential order; the PIC32 side waits only where it reads Propeller state (RF13, the sound stream, the video update, NVRAM, reset, the bootloader window). Every workload stays byte-identical to the reference with the thread and the translator on and off; `PINHECK_THREADS=0` and `PINHECK_JIT=0` switch them off.
  - Host instructions per emulated second, attract / video: reference 22.4 / 24.2 G, stage 3 on one thread without the translator 16.7 / 18.2 G, with the thread and the translator 15.4 / 16.7 G. Speed on the shared machine (niced): 0.75–0.85× in attract mode and 0.58–0.72× with video and four channels, from 0.47× and 0.44× (loads 3.4–6.5).
  - Real time is not reached: the Propeller thread is the bound (71% of the work on one thread; the PIC32 thread waits for it about half its time). It must get 15–28% cheaper for 1.0× and 30–40% for 1.2×: its events (27% of the whole), pin changes (17%) and local code (26%, of which 14.5% translated). The next stage would translate hub operations and `OUTA`/`INA`/`WAITCNT` into the blocks, step the NCO clock toggles in a loop that calls only their device, link blocks with fixed targets, and let the worker run ahead of the PIC32's calls with a rule for the end of a session and a reset.
- **Established by Plan 9c (stage 3 continued):**
  - After Plan 9b each Propeller event went through the scheduler, `exec` or `do_hub`, `complete` and a new entry into the local loop and a translated block: per emulated second in attract mode about 8.8 M events (4.3 M hub operations, mostly the two Spin interpreters; 1.6 M `INA` reads by the SD card driver; 2.0 M special-register writes by the display link and the SD driver) and 5.7 M pin change points, most of them NCO toggles of the SD clock and the display clock.
  - Stage 3 continued: a cog's hub read or write, `INA` read or special-register write runs inside the local loop whenever the scheduler would take that event next (its key is below the other cogs' earliest event and no other cog's event has moved), a waiting hub operation is issued there, and the scheduler hands a running cog's due instruction or hub access back to it; NCO toggles are stepped in one loop, a pending point re-evaluates only the registers that change there, counter writes keep the pin and toggle caches of the state that lasts until they take effect, and the display is called only for its own pins. Translating event instructions into the blocks and running the worker ahead of the PIC32's calls were measured and rejected: the first costs more than it saves, the second changes the frame log's stamps and the pins' delivery points.
  - Host instructions per emulated second on one thread, attract / video: reference (Plan 9b) 15.2 / 16.5 G, events in the local loop 13.2 / 14.3 G, pins between change points 12.5 / 13.5 G. The scripted game: 6.06 T instructions for the reference, 4.94 T for this stage, its four logs byte-identical. Speed on the shared machine with the worker thread (niced): reference 0.85× attract and 0.87× video (loads 4.8 and 3.8); after this stage 1.227× attract and 1.040× video (loads 2.7 and 2.6; the plan measured 0.970× video at load 3.1), and the scripted game's 335 s in 295.9 s wall including its checks, 1.13× (load 2.2; the reference took 358.9 s at load 2.2).
  - Real time is reached on average over each run for all three workloads on the shared machine at loads 2.2–2.7, with a thin margin for video (1.04×, and below 1.0× at load 3.1). The Propeller thread is still the bound (about 70% of the work on one thread); the worker idles about 9% of the time after the PIC32's `RF13` reads (about 27,000 a second, one per link byte), where the two threads run in lock step. The aim of 1.2× for the video workload needs the Propeller thread about 10% (15–20% at load 3.5) cheaper at the loads measured.
- **After the review of Plans 9b and 9c:**
  - `bench.sh` also reports the slowest 100 ms of each timed window, from the emulated and host time at every vblank (`PINHECK_TIME_LOG`). With the thread and the translator: attract 1.275× on average, slowest 100 ms 0.764× (2.6% of the 100 ms windows and 1.2% of the 1 s windows below 1.0×, load 1.6); video 1.077×, slowest 100 ms 0.720× (4.9% of the 100 ms windows and 4.8% of the 1 s windows below 1.0×, slowest 1 s 0.883×, load 1.8). The scripted game: 335 s in 292.7 s wall, 1.14× (load 2.0), its logs byte-identical. Without the thread: 1.037× and 0.912× (slowest 100 ms 0.826× and 0.521×); without the translator: 1.105× and 0.955× (0.943× and 0.536×). Short stretches run below real time; the sound buffer and the throttle absorb them.
  - Only the thread that started the worker waits for it: a host thread reading NVRAM (VPinMAME `NVRAM`/`ChangedNVRAM`, libpinmame `PinmameGetNVRAM`) reads the Propeller EEPROM image as it is at that moment. A wait longer than 20,000 pause instructions blocks on a condition variable (an event on Windows). The Windows worker starts through `_beginthreadex` at the priority of the thread that starts it.
  - Both threads spin up to 20,000 pause instructions before they block, so while the emulation runs the process uses 1.2–1.4 CPU seconds per emulated second (up to two CPUs unthrottled). Plan 9d bounds the spin by time instead.
- **Established by Plan 9d (tuning):**
  - Measured after Plan 9c (Linux, one CCX): the PIC32 thread waits for the worker at about 28,200 syncs per emulated second, 27,074 of them `RF13` reads, and spends 460–550 ms per emulated second waiting; the worker is the bound (about 700 ms busy and 90 ms idle). Padding the queue's fields, answering `RF13` on the worker, running the worker ahead of the PIC32's calls (every output stays byte-identical under extra cut points once the frame log's stamp comes from the frame's own time, but the extra catch-ups cost the worker what they save) and thread placement (Windows already keeps both threads in one CCX) were measured without gain and not adopted.
  - The handoff collapses where CPUs are scarce: 0.044–0.073× with one allowed CPU and 0.040–0.055× on two CPUs shared with a busy process, against 0.83–0.88× inline. A wait now spins at most 50 µs by the clock and then blocks (the same speed, 1.2 instead of 1.4 CPU seconds per emulated second); the worker is not started when the process may use only one CPU; and the driver switches the worker off for as long as the inline path is faster, judged over seconds of host time. Both paths give the same output, so switching changes only the speed. `bench.sh contention` checks the three cases and requires their outputs byte-identical.
  - Profile-guided builds (`cmake/pgo.cmake`: `PINMAME_PGO=GEN|USE` for sdl3pinmame, libpinmame and the Windows standalone build; `tests/pinheck/perf/pgo.sh`) run 8–10% faster on Linux (GCC 15, trained on attract mode and video): 1.39× attract and 1.22× video with the worker (loads 2.4–2.8), the scripted game in 4.62 T instead of 4.90 T host instructions, every output byte-identical. The profile needs the firmware and stays local, so binaries built without it keep Plan 9c's speed.
- **Established by Plan 12 (America's Most Haunted and The Jetsons):**
  - America's Most Haunted's raw DMD is scanned by a cog that never idles (3.2 million dots a second, three `OUTA` writes each); on one thread the game took 22.2 G host instructions per emulated second (Domino's 11.5 G), and with the worker thread it was no faster than without it: the PIC32 read `PORTF` 197,000 times a second (99.4% the cabinet switch routine, which uses `RF0`) and each read waited for the Propeller.
  - *Lazy cogs* (`p8x32a.c`): a cog whose reachable code only reads hub RAM, `PAR` and `CNT`, computes in cog RAM, jumps to fixed addresses and writes `OUTA` on pins no other cog or counter drives and only the host's lazy path watches leaves the schedule and runs behind the others: it catches up before another cog's hub write that changes a byte, before an `INA` read whose mask includes its pins, and at the end of `p8x32a_run_until`; its pin changes go to `bus.lazy_pins` in time order; a wait or idle-loop sleep on its pins, another cog driving them or a cog operation on it ends it. Its blocks are translated with their `OUTA` writes and hub reads. The P1 RTL matches, interpreted and translated: a chip test with three entries and exits (`chip/lazy.spin`), one case per exit path read within cycles of the exit (`lzexit`: `WAITPEQ`; `lzdira`, `lzstop`: another cog's `DIRA`, `COGSTOP`, `INA` read every cycle around it), a hub read's `C` (`lzsysc`: `RDBYTE WC` keeps a cog from being lazy), a `CLKSET` with a pin change not caught up (`lzclk`), and the random programs with a scan-cog-shaped cog 1 (30% of the RTL set, most of them lazy). The other chip, ISA and random tests run with lazy cogs allowed, but most have no cog that qualifies. `mutate_lazy.sh` checks that seven mutations of the lazy core are each caught. America's Most Haunted on one thread: 14.8 G (from 22.2 G).
  - *Uncertain `RF13` reads* (`mips32.c`, `pic32mx.c`, `prop.c`): with the worker thread, a `PORTF` read takes `RF13` from the last value known, marks the bit uncertain and leaves the true `P24` to the worker in queue order (`CMD_SAMPLE`, the catch-up `prop_p24` made); the MIPS32 core settles the bit before any instruction that reads the register, except an `AND`/`ANDI` that clears it, and drops it when the register is written. A differential test of 220,000 random programs (loads of every size, `LWL`/`LWR`, `LL`/`SC`, ALU, shift, `HI`/`LO`, SPECIAL2/3 and branch instructions, `JR`/`JALR`, interrupts into a shadow set, traps and overflows) matches runs with the true value, and `mutate.sh` checks that wrong register-use entries (`SEB`/`SEH`/`WSBH`, `MULT`/`DIV`, `SRA` reading nothing) are each caught. A reset and the debugger's register view settle first; a sample's token reaches the board whole (`pic32mx.c` passes its low 30 bits with the byte offset and rebuilds it from the latest token). The PIC32 now waits about 600 times a second in attract mode, from 197,000.
  - Every output byte-identical to Plan 11c's build (UART1, frames and the raw DMD's subframes, sound, NVRAM) for the four games with the worker, without it, without lazy cogs (`PINHECK_LAZY=0`), without uncertain reads (`PINHECK_RF13=0`) and without the translator. Speed with the worker (loads 1.3–2.0): America's Most Haunted 1.08× attract, 0.82× video, 0.76× play (from 0.556×, 0.472×, 0.440×); The Jetsons 0.98×, 0.89×, 0.83× (the reference in the same runs 0.94×, 0.88×, 0.84×); Domino's 1.37× and 1.20×; Rob Zombie 1.60×, 1.33×, 1.21×. Real time is reached for America's Most Haunted in attract mode only, with a thin margin; not with video, not in play, and not for The Jetsons (whose display link, a cog that writes hub RAM and drives its pins with counters, cannot be lazy). What bounds them is the Propeller thread's ordinary work: three Spin interpreters' hub operations and the SD driver's events in America's Most Haunted, the display link's counter and `PHS` writes in The Jetsons.
- **Established by Plan 12b (the levers after Plan 12):**
  - Measured (worker thread): America's Most Haunted's Propeller thread with video is busy 1.17 s per emulated second; the scheduler dispatches about 6 million events of its three Spin interpreters and 1.1 million of the SD driver, and the lazy scan cog catches up 1.1 million times (0.9 million before other cogs' hub writes). The Jetsons' SD driver and display link take 3–3.5 million dispatches and 10 million inline events, with 9 million calls of the pin devices.
  - Rejected after exact prototypes (byte-identical, RTL-checked): hub reads that run before other cogs' events when those cannot reach the hub first (no gain: the checks cost what the dispatches cost), and the SD card and display link as devices bound to their cogs, whose pin events run ahead of other cogs while a lookahead over those cogs' code shows they cannot observe or drive the pins (slower: the bound path alone costs more than the pending points, the lookahead more than the dispatches it saves; with both levers the display link lost bits, cause not found). Measurement-only ceilings: 30% fewer host instructions for The Jetsons, 11% for America's Most Haunted, if every such event could run ahead.
  - *A hub-write journal* (`p8x32a.c`): the lazy cog no longer catches up before another cog's hub write; the replaced bytes go into a journal of 64 entries, its reads take each byte as it was at the read, translated lazy reads stop at a journaled long, and the host reads hub RAM for the scan cog's time through the journal (`p8x32a_hub_at`: the raw DMD's frame log and row model). A chip test (1,621 entries, 23 times full) and four mutants in `mutate_lazy.sh`. America's Most Haunted with video: −1.8% host instructions on one thread.
  - *Profile-guided builds:* `docs/build.md` gives the optional build for users. Trained on Domino's (training on all four games was slower): 5–8% fewer host instructions, 4–8% faster.
  - Every output byte-identical to Plan 12's build (UART1, frames and the raw DMD's subframes, sound, NVRAM) for the four games with the worker, without it, with `PINHECK_JOURNAL=0`, without lazy cogs and without the translator, and for the profile-guided build. Speed with the worker (loads 1.2–2.2), without / with the profile: America's Most Haunted 1.14×/1.10× attract, 0.86×/0.86× video, 0.80×/0.81× play; The Jetsons 1.00×/1.04×, 0.90×/0.97×, 0.88×/0.95×; Rob Zombie 1.62×/1.69×, 1.36×/1.43×, 1.22×/1.28×; Domino's 1.38×/1.46×, 1.12×/1.27×. Real time is reached in attract mode only (The Jetsons' with the profile); not with video or in play.
- **Established by Plan 12c (after Plan 12b):**
  - Plan 12b's lost display-link bits: its bound SD cog's early `INA` read with a mask of 0 took the ordinary path, which sent every pin up to the early event's time ahead of the other cogs, so their later pin writes landed behind the sent pins. The merged code has no early events; a time-order check in the test builds (`P8X32A_CHECK`: a pending point behind the sent pins, a hub access before an already-run write of its bytes, a hub write before an already-run read of them, or a cog or lock state access before an already-run one, stops the run) now makes that class of fault fail the RTL cases.
  - Taken, each exact: a cog started again on new code is translated again (its translator state was kept across reloads; after The Jetsons' Propeller reboot cog 0's Spin interpreter ran untranslated: −8% host instructions for The Jetsons); the scheduler keeps each cog's key until that cog or `sched_gen` changes; PAR and CNT sources translated (America's Most Haunted's polling cog: −5% to −8%). `mutate_core.sh` with ten mutants (among them each `sched_gen` increment), three-cog random programs (`gen.py --cogs`: woken cogs seen by cog 0), two chip tests; every RTL and mutant case also runs in a single `run_until` call (`-quantum 400000`), where a stale scheduling key lasts the whole run.
  - Rejected after an exact prototype: hub operations before other cogs' earlier events, judged by per-address lower bounds of the cycles to each cog's next hub write, hub access or pin change (kept per address until a word they read changes) and by sleepers' earliest wake. Byte-identical, and the early operations cut the dispatches by 31%, but the cheapest checks cost more than the dispatches saved (one thread with video: The Jetsons 24.0 G and America's Most Haunted 29.5 G against 15.1 G and 16.2 G without).
  - Every output byte-identical to Plan 12b's build for the four games, on one thread, with `PINHECK_JIT=0`, `PINHECK_LAZY=0`, `PINHECK_RF13=0`, `PINHECK_JOURNAL=0` and for the profile-guided build. Speed with the worker (loads 1.1–3.5), without / with the profile: America's Most Haunted 1.18×/1.22× attract, 0.91×/0.96× video, 0.86×/0.87× play; The Jetsons 1.07×/1.21×, 0.96×/1.09×, 0.96×/1.08×; Rob Zombie 1.60×/1.77×, 1.35×/1.49×, 1.22×/1.35×; Domino's 1.38×/1.52×, 1.20×/1.31×. Real time is reached for The Jetsons with the profile-guided build in every workload, not for America's Most Haunted with video or in play.
- **Proof for every stage:**
  - all existing suites pass, including the RTL and QEMU differential suites;
  - a determinism check: the optimised build reproduces the reference build's UART1 log, display frames and audio byte for byte over a fixed firmware run. The scripted game under `PINHECK_RTC` is that run: its link, board, UART1 and frame logs are already byte-identical between runs of one build.

## 5. Display look

This comes after Milestone 6 runs in game.

- **Deriving the encoding:** capture the config packet for each value of `PIXEL SHAPE`, `BRIGHTNESS`, `POSITION` and `BAR BRIGHT`, by stepping the emulated service menu with `-key_script`, and derive each field.
- **Rendering:** render the look (dot shape, brightness, offset) in the `CORE_VIDEO` renderer.
- **Tests:** unit tests for the packet decoding, and golden images per setting.
- **Caveat:** the real display module's pixel rendering is unknown, so the shape rendering is labelled an approximation. At 2×2 window pixels per dot, the dot shapes, the light between dots and the scale of `POSITION` are rulings of the display-look plan, not measurements of the module.

Established by the display-look plan, from `DOM_V006.PRG` and a sweep of every value in the emulated service menu (`CHANGE: MAIN SETTINGS`):

- **Packet.** Seven 16-bit words, most significant byte first:

| Bytes | Word | Meaning | Values |
|---|---|---|---|
| 0–1 | 0 | not a menu setting | 177 in the Propeller's start-up packet, 250 in every packet the PIC32 has sent |
| 2–3 | 1 | `POSITION` | 300–500 in steps of 1, 500 wraps to 300; start-up 340 |
| 4–5 | 2 | `PIXEL SHAPE` | 0 `ROUND`, 1 `SQUARE`, 2 `HIGHREZ`; 0 on a blank EEPROM |
| 6–7 | 3 | `BRIGHTNESS` | 175–255 in steps of 5, 255 wraps to 175; start-up 255 |
| 8–9 | 4 | width | 128 |
| 10–11 | 5 | height | 32 |
| 12–13 | 6 | `BAR BRIGHT` | 0–62 in steps of 2, 62 wraps to 0; start-up 62 |

- **When it is sent.** The Propeller sends its own packet at start-up. Each Enter on one of the four items steps the value and makes the PIC32 send the seven words (little-endian, plus a flag byte: 0, or 0x80 when Back leaves the menu) as link command 0x11; the Propeller passes the words on to the module at once, without the flag. After the menu is left with Back, a restart's start-up packet carries the stored block (the Propeller keeps the seven words and the marker `0xBAFA` in its EEPROM), with word 0 = 177: the new `POSITION`, `BRIGHTNESS` and `BAR BRIGHT`, and a `PIXEL SHAPE` that was kept (`SQUARE` stored) in runs without the playfield simulator and not kept (0 stored) with it, although the PIC32's Back packet carried the new shape and the save flag; the cause is not established. The emulator draws whatever the packet says.
- **Rendering** (`display.c`; the `sdl3pinmame` and VPinMAME window, 256×64; libpinmame gets the frame as sent). `SQUARE` fills each dot's 2×2 cell: the exact pixels of Milestone 6. `ROUND` draws each dot as the top-left pixel of its cell and lights the three gap pixels with the mean of the dots around them times `BAR BRIGHT`/124. `HIGHREZ` is Scale2x. `BRIGHTNESS` b scales every colour by b/255. `POSITION` p moves the picture down by ⌊(p − 340)/4⌋ pixels inside the window, black where it leaves. A packet of another length, or whose words 4–5 are not 128 and 32, keeps the exact look.

## 6. Release

The destination is decided once the game runs: a PR to upstream `vpinball/pinmame`, or the user's fork. The design of this phase happens then. It covers:

- upstream conventions and CI for all build systems;
- which scaffolding ships (`tests/pinheck`, `docs/superpowers`, the driver's test hooks);
- romset and ROM instructions (update zip → `dominos.zip`, user-supplied GPL `p8x32a.rom`);
- closing or ruling on every deferred minor.

Until then the branch stays local and is not pushed.

## 7. Order

1. Milestones 6 and 7 in parallel, after Plan 5.
2. Milestone 8.
3. The display look and Milestone 9 in parallel. Milestone 9 uses the scripted game as its workload and regression test.
4. Release.

Each gets its own plan, written once its inputs exist and proven before it is committed.
