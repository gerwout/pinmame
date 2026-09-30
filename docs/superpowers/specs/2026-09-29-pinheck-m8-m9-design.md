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
  - Host instructions per emulated second on one thread, attract / video: reference (Plan 9b) 15.2 / 16.5 G, events in the local loop 13.2 / 14.3 G, pins between change points 12.5 / 13.5 G. The scripted game: 6.06 T instructions for the reference, 4.94 T for this stage, its four logs byte-identical. Speed on the shared machine with the worker thread (niced): reference 0.85× attract and 0.87× video (loads 4.8 and 3.8); after this stage 1.24× attract and 1.08× video (loads 2.8 and 2.6), and the scripted game's 335 s in 333 s wall including its checks (load 2.8; the reference took 417 s at load 5.2).
  - Real time is reached for all three workloads on the shared machine. The Propeller thread is still the bound (about 70% of the work on one thread); the worker idles about 9% of the time after the PIC32's `RF13` reads (about 27,000 a second, one per link byte), where the two threads run in lock step. The aim of 1.2× for the video workload needs the Propeller thread about 10% (15–20% at load 3.5) cheaper at the loads measured.
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
