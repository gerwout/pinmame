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

Playfield switches get keyboard keys. The cabinet keys follow PinMAME's usual layout: coin, start, both flippers, tilt, and Enter/Back/Menu/User.

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
- **Proof for every stage:**
  - all existing suites pass, including the RTL and QEMU differential suites;
  - a determinism check: the optimised build reproduces the reference build's UART1 log, display frames and audio byte for byte over a fixed firmware run.

## 5. Display look

This comes after Milestone 6 runs in game.

- **Deriving the encoding:** capture the config packet for each value of `PIXEL SHAPE`, `BRIGHTNESS`, `POSITION` and `BAR BRIGHT`, by stepping the emulated service menu with `-key_script`, and derive each field.
- **Rendering:** render the look (dot shape, brightness, offset) in the `CORE_VIDEO` renderer.
- **Tests:** unit tests for the packet decoding, and golden images per setting.
- **Caveat:** the real display module's pixel rendering is unknown, so the shape rendering is labelled an approximation.

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
