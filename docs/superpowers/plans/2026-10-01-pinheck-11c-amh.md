# Plan 11c: America's Most Haunted (raw DMD, Intel HEX firmware) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The pinHeck driver runs a fourth game, America's Most Haunted (`amh`, `AMH_V023.hex` and `DMD/PROP_023.BIN`), from a blank NVRAM to attract mode, the service tests and keyboard play through a playfield simulator, with its raw 128×32 dot-matrix display decoded from the Propeller's scan pins, integrated to 16 shades by PinMAME's DMD PWM path and checked pixel-exact against the game's `.VID` clips, in PinMAME's window and in libpinmame; Domino's, Rob Zombie and The Jetsons stay byte-identical.

**Architecture:** The romset is the unmodified `AMH_SD_V023.zip` with `AMH_V023.hex` added at its root. The game's driver init converts the Intel HEX into program flash (`hexload.c`, records checked, flash range only); a HEX that does not convert stops the machine with a message. The display is a raw DMD scanned by a Propeller cog: `dmd.c` decodes P16 (data) on P17 (dot clock), P18 (latch), P19 (row clock) and P20 (first row) into 1-bit subframes of 128×32; the Propeller thread queues them, and the emulation thread hands each vblank's subframes to the core (`core_dmd_submit_frame`) after the Propeller caught up. A new core PWM filter adds the last 16 subframes with equal weights: each subframe shows one level of the cog's 16, so a still picture integrates to its 4-bit values exactly. The game uses a `CORE_DMD` layout and the machine driver `PINHECKDMD` (palette video, the core's DMD renderer); the 16-shade lists of the VPinMAME, dmddevice and libpinmame DMD paths include `GEN_PINHECK`. `pinheck_tGameData` gains `dmdHub` (the cog's frame buffer in hub RAM, which marks a raw-DMD game) and `bootHold` (the stand-in bootloader's hold, 5 s for AMH so that its first commands reach the Propeller's main loop). `src/wpc/sims/pinheck/amh.c` holds the game and a simulator for the 4-ball trough with its drain kicker, the shooter lane, the scoop, the Spooky Door and the VUK behind it, the Hellevator car, the ghost loop with its magnet and the flippers' end-of-stroke switches. The checks select the game with `PINHECK_GAME=amh`: the boot check has a branch for a game without an update, the display check one for the raw DMD (`LOOK=dmd`), check 5, the simulator check (`amhsim.py`), the libpinmame check and `bench.sh` gain AMH's plans.

**Tech Stack:** C (C89-syntax-clean driver; `-std=c99 -pedantic -Wall -Werror` for the simulator file), C++ (core.c and libpinmame's generic files, as they are), Python 3 (no third-party modules), POSIX sh.

**Spec:** the parent spec `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§4.5 numbering, §6 romsets, §7 checks) and its addenda (`2026-09-29-pinheck-m6-m7-display-audio-design.md`: exact pixels; `2026-09-29-pinheck-m8-m9-design.md` §3.3 simulator; `2026-09-29-pinheck-m10-vpx-design.md`: libpinmame export, names), applied to a fourth game, and Plans 11a and 11b (`pinheck_tGameData`, `PINHECK_GAME`, `games.sh`). The research behind this plan: `work/games/amh/research.md` (firmware, HEX, display cog, link, board revision, numbering, mechanics); where this plan's measurements contradict it, the Rulings say so.

## Prerequisites

- Plan 11b executed and merged, and Plan 9d's spin follow-ups: `pinheck` at `40f6f4df` (`PINHECK_SPIN_US`, a 1 ms spin on Windows; `7bad62ec` is the merge of `pinheck-11b`). This plan was replayed on `7bad62ec` and its commits rebased onto `40f6f4df` without conflict (it does not touch `prop.c`).
- This plan edits the driver (`pinheck.[ch]`, `pinheck/bootldr.[ch]`, `pinheckgames.c`, `driver.c`), the core's DMD PWM code (`core.[ch]`: one filter, one combiner, one 16-shade list), libpinmame's and VPinMAME's 16-shade lists (`libpinmame.cpp`, `ControllerDmdDevice.cpp`), the game data lines of Rob Zombie and The Jetsons, `pinheck_names.h`, and the checks of Plans 5–11b, and adds `pinheck/hexload.[ch]`, `pinheck/dmd.[ch]`, `sims/pinheck/amh.c`, `tests/pinheck/pic32mx/hex_test.c`, `tests/pinheck/display/dmd_test.c` and `tests/pinheck/sim/amhsim.py`. The CPU cores, the SoC, `prop.c`, the board, audio, storage, link and display-link devices are untouched.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- **The romset** (the user builds it once; the zip's other files stay as they are): copy `AMH_SD_V023.zip` (874,445,206 bytes) to `amh.zip` and add `AMH_V023.hex` (705,004 bytes, CRC32 `d5147386`) at its root, on Linux `cp AMH_SD_V023.zip amh.zip && zip -j amh.zip AMH_V023.hex`, on Windows with 7-Zip `7z a amh.zip AMH_V023.hex` (or PowerShell `Compress-Archive -Path AMH_V023.hex -Update -DestinationPath amh.zip`). PinMAME finds `DMD/PROP_023.BIN` by its name; the SD card shows the HEX at its root, which the Propeller ignores. `pinheck.zip` (the mask ROM) is needed as for every pinHeck game.
- Before starting, from the repository root, export what Plan 11b's checks use (`TOOLS`, `P8X32A_ROM`, `PINHECK_UPDATE_DIR`, `PINHECK_ZIP`, `DOMINOS_PRP`, `DOMINOS_ZIP`, `RZSPOOK_ZIP`, `RZSPOOK_UPDATE_DIR`, `JETSONS_ZIP`, `JETSONS_UPDATE_DIR`, `SDL_AUDIO_DRIVER=dummy`) and `AMH_ZIP` = the `amh.zip` above, `AMH_UPDATE_DIR` = a folder holding `AMH_V023.hex` and the zip's `DMD/` folder unzipped (`unzip AMH_SD_V023.zip 'DMD/*'`, 89 MB; the checks read `DMD/PROP_023.BIN` and the `.VID` clips there), `AMH_HEX=$AMH_UPDATE_DIR/AMH_V023.hex`. Nothing from the firmware or the SD card goes into the repository. A game's run is the Domino's command with the game's variables in front; the steps define them once as shell functions:
  ```sh
  amh() { PINHECK_GAME=amh PINHECK_ZIP=$AMH_ZIP PINHECK_UPDATE_DIR=$AMH_UPDATE_DIR "$@"; }
  jet() { PINHECK_GAME=jetsons PINHECK_ZIP=$JETSONS_ZIP PINHECK_UPDATE_DIR=$JETSONS_UPDATE_DIR "$@"; }
  rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
  ```
- The machine is shared: only one emulator runs at a time machine-wide, so every step that runs PinMAME, libpinmame or a firmware program goes through `flock /code/spooky_domino/work/emu.lock` (on another machine drop the `flock` prefix); builds and heavy suites run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6`. Every PinMAME launch has a `timeout -k 30`, a private `-nvram_directory` and `-cfg_directory`, and no sound device (`SDL_AUDIO_DRIVER=dummy`). America's Most Haunted runs at about 0.45–0.55× real time (Ruling 14); the times below are wall-clock with other agents' work beside them.

## Global Constraints

- Domino's, Rob Zombie and The Jetsons are byte-identical: `bench.sh` with `REFERENCE` (Task 1's build of `pinheck` as it stands) requires their UART1 log, display frames, sound and NVRAM in attract mode and with video equal byte for byte (Tasks 4 and 12); every one of their checks passes with its earlier output.
- `pinheck_tGameData` stays the only per-game mechanism; every game spells out its values, the two new fields included.
- Numbering is Plan 8a's (spec §4.5): document switch *n* (0–63) = PinMAME `(n/8+1)*10 + n%8+1`; cabinet switch *n* (9–15) = *n* + 82; document coil *n* = PinMAME solenoid *n*+1; servo *n* = custom solenoid 57+*n*; on-board RGB LED 2 (America's Most Haunted's ghost) = 62–64.
- The simulator follows Plan 8b's conventions: PinMAME's framework, coils read as "on during the last frame" (`coreGlobals.solenoids`), `autoBall`, keys tapped for one frame, every public name outside the file `pinheck_`-prefixed.
- The checks select the game with `PINHECK_GAME` (default `dominos`); with it unset every check runs Domino's exactly as before.
- Code comments short and factual; the driver C89-syntax-clean.

## Rulings

Decisions the research and the decisions handed to this plan left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it). Times are emulated seconds of PIC32 time unless they say otherwise; `PROP_023.BIN` addresses are hub addresses, `AMH_V023` addresses flash addresses. The firmware is the authority for behaviour, the documents for names.

1. **The HEX becomes program flash in the game's driver init, once per launch.** `pinheck_hex_flash()` accepts record types 00 (data), 01 (end), 04 (upper address) and 05 (start address, ignored), checks every record's checksum, requires every data byte inside 0x1D000000–0x1D07FFFF and an end record with nothing but white space (or a ^Z) after it; `AMH_V023.hex` gives 243,952 bytes, an image whose first 246,232 bytes have CRC32 `09d9072f` (entry `lui k0,0x9d00` at 0x9D001000), 0xFF elsewhere. A restart keeps flash, as power-cycling the real board does. A HEX that does not convert stops the machine like the system set (on-screen message, error exit after 5 s). Rejected: a converted `.bin` in the romset (the user would need a converter; the romset would not be the official files). Cost if wrong: none for the shipped V023; an older HEX converts the same way.
2. **The display is decoded from the shifted bits, not from hub RAM.** The cog's loop (hub $336C) is: per subframe (level 0–15) and row, 64 `rdbyte`s, each nibble `cmp level,nibble wc` → `muxc outa,P16`, then `muxz`/`muxnz outa,P17` (one rising edge per dot after the data); at the row's end P20 = first row, a P18 pulse, a P19 pulse. The decision handed to this plan was a row model (watch P18–P20 only, take the row from the 4 bpp frame at $5B0C at its latch) if proven equivalent. It is not: run beside the pin decoder on the real firmware (test hook `PINHECK_DMD_PROOF`), it differs in 36 of 14,640 subframes in the first 20 s and 135 of 30,323 in 40 s with a clip, always in exactly one row — the row the firmware's frame copy (`bytemove(@buf+$800, @buf, $800)`, hub $17A7) overtakes while the cog shifts it. Watching P16/P17 as well costs about 4–5% (40 s of attract mode: 0.61–0.62× with the row model alone, 0.58–0.59× with both decoders; the decoder itself is 1.3% of the profile). The driver decodes the pins; the row model stays as the test hook, and the display check asserts that it differs only by single rows in under 1% of subframes. Cost if wrong: about 4% of speed.
3. **The cog's quirks are the hardware's.** P19 is already high when the cog starts, so the first subframe's first row gets no row clock: the decoder's first subframe is level 1, and a whole frame (16 subframes from level 0) is first logged 16 subframes later. The second row of that first subframe gets 127 dot clocks (P17 stays high from the first row); its first dot is the shift register's oldest bit, as on the module. A cleared DIRA (a Propeller reboot) restarts the decoder. Level k is the k-th subframe since the cog started (mod 16), counted from P18 with P20 high.
4. **16 shades through PinMAME's DMD PWM path, exactly.** A new filter `CORE_DMD_PWM_FILTER_PINHECK_16` adds the last 16 subframes with weight 1 each; any 16 consecutive subframes hold each level once and the level-15 subframe is always dark, so a still picture shows value/16 (0–15/16) without flicker, and a change mixes old and new values dot by dot. The combiner `CORE_DMD_PWM_COMBINER_SUM_16` (the raw frame for colorizers) is the same sum, 0–15. The subframes (about 785 a second, 49 cycles of 16) reach the core at each vblank after `prop_sync`, from a ring of 64 the Propeller thread fills: no sharing with the renderer. The machine driver `PINHECKDMD` imports `PINHECK` with palette video, so the core draws the `CORE_DMD` layout with its DMD pens (`PINHECK`'s `VIDEO_RGB_DIRECT` would show the pen numbers as colours). `GEN_PINHECK` joins the 16-shade lists of core.c's VPinMAME path, the dmddevice path, libpinmame's layout depth (4) and its plugin identify format (BITPLANE4); the pinHeck games with the display link never reach those DMD paths. Cost if wrong: none in the emulation; a colorization made for 4-level raw frames would not apply.
5. **The frame log of a raw DMD** (`PINHECK_FRAME_LOG`) records each whole cycle of 16 subframes, one byte per dot (0–15), with Plan 6's 20-byte stamp; its hub address is `dmdHub` ($5B0C) when the 4 bpp buffer holds that frame at the cycle's end, else `$FFFFFFFF` (a cycle the frame copy crossed: 10–30% of cycles while a clip plays). The frame checks use the whole cycles; `.VID` frames are 2,048 bytes, unpacked high nibble first.
6. **`PINHECK_INSERVICE` does not apply** (`inService` 0): on a blank NVRAM the firmware prints `New code detected, storing new game defaults... done!`, writes its defaults over the link and stores `(23 << 24) | $BAFA` (`1700BAFA`) at EEPROM $8000, and goes on to attract mode without a restart.
7. **America's Most Haunted's application starts 5 s after a reset (`bootHold`); the others keep 3 s.** The research's boot did not reach attract mode: with the stand-in's 3 s hold the PIC32 sends its attract start (link command $13 with $FF, `GR` = 255) once at 3.05 s, then retries EEPROM reads ($21/$20/$22) until the Propeller answers; the Propeller's main loop starts only at 3.85 s (the version screen, method 52 at $2642, ends with `pause 2000`), so the 32-entry command ring has been overwritten and the display stays on the version screen (`GR:10` in the Propeller's serial status) until the first game or service-menu exit. With the application at 5 s (or 7 s) the command is taken and attract mode plays. The real chipKIT bootloader's timeout is not known; its PIC32 sends this command once, which a design that always loses it would have shown. The boot check asserts that the display leaves `GR:10` (test hook `PINHECK_PROP_SERIAL`, the Propeller's watchdog line on P30). Cost if wrong: if the real board starts the application sooner, it shows the version screen at power-on until a game, and the emulation reaches attract mode 2 s later than the real machine.
8. **The on-board RGB chain is not inverted; the ghost is its third LED.** The research inferred inverted lines from `AMH_V023` 0x9D03A8DC, reading `sw …,0x6124` as LATESET and `0x6128` as LATECLR; on the PIC32MX they are LATECLR and LATESET. The routine is RE2 low, RE1 = bit, RE2 high, as Domino's; the ghost's (0x9D038B48) is `digitalWrite(RE2,0)`, RE1 = bit, `digitalWrite(RE2,1)`; the frame ends with RE2 low. 72 bits, three LEDs. The firmware's RGB test shows pure colours as read: GHOST=RED gives 255,0,0 on 62–64, RGB1=GREEN 0,255,0 on 51–53 (check 5). With the inverted reading every test colour came out complemented. `rgbInverted` stays 0 for every game and unsupported, as Plan 11b left it; on-board LED 2 goes to 62–64 (`PINHECK_ONB_LEDS` 3; no pinHeck board drives both it and the external chain's LED 0). Rejected: implementing the inversion handed to this plan (its premise is the misread). Cost if wrong: none for AMH; a future inverted board needs the flag implemented.
9. **The GI is four playfield circuits, 37–40 (GI_8–11).** The attract show and the lamp test's PLAYFIELD GI steps light only these; the backbox GI (25–32) stays dark in attract mode. The wiring chart names four colours (lower red, mid yellow, upper purple, scoop purple/black) but not their bits, so the names are "playfield GI 1–4 of 4" without a colour. Coils 6 and 7 (solenoids 7, 8) are both PROTO BG 1 in the solenoid test (`sProtoBG1`, `sProtoBG2`); coils 1–5, 12 and 23 are UNUSED there and get no names.
10. **Servo levels are 0–180° over 544–2,400 µs** (Arduino `Servo`): the servo test's factory positions pulse 593 µs (DOOR OPEN, TARGET UP, 5°), 647 µs (HELL DOWN, GHOST LEFT, 10°), 1,476 µs (DOOR CLOSE, GHOST MIDDLE, 90°), 2,200 µs (TARGET DOWN, HELL UP, 160°) and 2,305 µs (GHOST RIGHT, 170°). Attract mode rests at HELL DOWN, DOOR OPEN, GHOST MIDDLE and TARGET DOWN.
11. **The service menu opens at MAIN SETTINGS** (CHANGE:), three Right Shifts before SWITCH EDGE; its eleven items are SWITCH EDGE, AUDIO/MUSIC, SOLENOID, SERVO, SERVO DEFAULT, LAMP, RGB LIGHTING, COIL SETTINGS, MAIN SETTINGS, GAME SETTINGS, GAME AUDITS. The solenoid test starts at RFLIP HIGH (coil 16) and runs 16–23, 0–15; the servo test has nine items (Ruling 10); the lamp test is Domino's, but its ALL ON steps leave lamp 56 (Spook Again, 81) off; the RGB test is GHOST, RGB1 and RGB2 red, green, blue (one LED lit at a time), then GHOST TYPE. The switch test is Domino's layout (matrix at x 0–31, cabinet columns at 32–39) with its grid in two shades; check 5 takes any lit dot of a grid line and skips the cycles the frame copy crossed. Start lamp 91 stays off in attract mode.
12. **The simulator's geometry is inferred and marked as such** (research §7 and the firmware's reactions): a 4-ball trough on 84 (the eject position) to 87; a drained ball rests on the drain switch 88 until the drain kicker (coil 21, solenoid 22), which the firmware fires; the ball loader (solenoid 21) moves trough 1 to the shooter lane (82); in attract mode the firmware fires the autoplunger (solenoid 23) for a ball in the lane, in a game the player plunges (Space). The scoop (37) and the VUK behind the Spooky Door (38) hold the ball until their coils fire (the firmware ejects both after 0.4 s in attract mode). The door is open while servo 1 pulses under 1,000 µs: a door shot then goes to the VUK, else it closes and opens the door opto (96). The Hellevator car (64) takes a ball while servo 0 is under 1,400 µs (HELL DOWN); 30 frames after the car rose (HELL UP) the ball rolls out on top. The ghost loop shot crosses its opto (95) and is held under the ghost if the loop magnet (solenoid 1) was on within 12 frames, until it has been off for 6; in a game the firmware holds the magnet with 8 ms pulses, so a loop shot stays under the ghost. Switch 66 (the chart's Pop Bumper 2) fires coil 13 (POP BUMP 0) in a game; the names follow the chart. Psychic is key M: MAME's pause key P would stop a key script. Cost if wrong: a shot's switch order differs from the machine's; the firmware's rules see the same switches.
13. **The checks for a game without an update** (`UPDATE_END=none` in `games.sh`): the boot check runs two 15 s launches — the first on a blank NVRAM (the conversion's `hex:` line, the defaults stored, the banner, the version word in the NVRAM's EEPROM image), the second with the stored NVRAM (no defaults stored again, `Ball Search: DISABLED`, attract mode) — and the romset check adds a HEX with one digit changed, which must stop with the conversion error. The display check (`LOOK=dmd`): launch 1 runs the row-model proof and snapshots the service menu (a still screen) for the core's DMD rendering (one pen per shade, brighter for every higher shade); launch 2 turns video attract mode off (`[E96000]`: with it on, the Propeller draws text over the clips) and plays `AB1.VID`, whose 60 frames must show pixel-exact, contiguous and in order. libpinmame's DMD is checked exactly: every frame a host gets (callback bytes, 255 × sum / 16; plugin floats, sum / 16) is the sum of the last 16 subframes the driver handed the core at some vblank, in order (test hook `PINHECK_DMD_LOG`).
14. **Speed: America's Most Haunted runs at about half real time, and this plan does not change that.** Without a profile: 0.56× in attract mode, 0.47× with video, 0.44× in play (22.9, 25.0 and 27.4 G host instructions per emulated second, against Domino's 12.6 G); with a profile-guided build trained on its attract and video workloads: 21.1, 23.2 and 25.3 G instructions (8% fewer); 0.61×, 0.54× and 0.49× at loads 2.0–2.8 (10–13% faster), 0.55×, 0.47× and 0.44× in a second run at loads 2.2–3.5 (no gain): about half real time either way. The display cog never idles: 3.2 million dots a second, three OUTA writes each, which the Propeller core turns into pin change points. The profile (attract, `perf`): the PIC32 thread waits for the Propeller (`wait_head` 40% of all samples); the Propeller threads' `run_local` 14%, the worker loop 9%, translated cog code 8%, `do_catch_up` 6%, the pin-change `flush` 4.5%, `hub_rw` 1.8%, `special_write`/`add_pending` 2%; the DMD decoder 1.3%. No cheap exact lever appeared; the candidates are a high-level model of the scan cog (its OUTA writes replaced by the rows it shifts, proven against the pin decoder), a translated fast path for OUTA-only cog loops, and the Propeller items Plan 9d left. Cost if wrong: AMH plays at half speed until such a plan.
15. **Names.** An `amh` section in `pinheck_names.h` (ids equal to `amh.c`'s constants, as `names.py check` requires; the servo ids `sHell`, `sDoor`, `sGhost`, `sTarget`; the ghost light `sGhostR/G/B` on 62–64); `names.py vbs src amh` writes `amh.vbs`.

## Review Focus

- **Domino's, Rob Zombie and The Jetsons unchanged:** their game data spells out `dmdHub` 0 and `bootHold` 3000 (`BOOT_WINDOW`), so the display link, the boot window and the outputs are as before; `bench.sh` with `REFERENCE` (Tasks 4 and 12), their boot, display and check-5 runs (Task 12).
- **The pin decoder** (`dmd.c`): the dot order (the first shifted dot is the leftmost), the level count from cog start, the cog-start quirk (Ruling 3), the restart on a cleared DIRA; the unit test models the cog's loop instruction by instruction, and the clip check (Task 8) and the exact libpinmame check (Task 9) cover the real firmware.
- **Threading of the subframe ring:** the Propeller thread writes it inside a catch-up, the emulation thread reads it at vblank after `prop_sync` (Plan 9b's rule for Propeller state); a full ring drops the oldest and is logged at stop.
- **The generic changes:** the new PWM filter and combiner in `core.c`, and `GEN_PINHECK` in four 16-shade lists (Ruling 4); no other game reaches them.
- **The HEX loader's validation** (unit test with every error kind, and the corrupt-HEX romset case) and the refusal path (the core's DMD state is set up before the refusal, so the core can draw).
- **The boot hold** (Ruling 7): only America's Most Haunted's is longer; the boot check asserts attract mode through the Propeller's serial status.

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck/hexload.[ch]` | Intel HEX into a flash image, validated |
| `src/wpc/pinheck/dmd.[ch]` | the raw DMD: subframes from the scan pins (and the row model for the proof) |
| `src/wpc/pinheck.h`, `src/wpc/pinheck.c` | `PINHECK_HEX_ROMSTART`, `pinheck_flash_hex`; game data `dmdHub`, `bootHold`; the DMD path (ring, vblank hand-off, frame log, test hooks), `PINHECKDMD`; on-board LED 2 on 62–64 (modified) |
| `src/wpc/pinheck/bootldr.[ch]` | `boot_set_window`: a game's hold (modified) |
| `src/wpc/core.[ch]` | `CORE_DMD_PWM_FILTER_PINHECK_16`, `CORE_DMD_PWM_COMBINER_SUM_16`; `GEN_PINHECK` 16 shades in the VPinMAME path (modified) |
| `src/libpinmame/libpinmame.cpp`, `src/win32com/ControllerDmdDevice.cpp` | `GEN_PINHECK` 16 shades (modified) |
| `src/wpc/sims/pinheck/amh.c` | America's Most Haunted's game definition and playfield simulator |
| `src/wpc/sims/pinheck/{jetsons,rzspook}.c`, `src/wpc/pinheckgames.c`, `src/wpc/driver.c` | the new game data fields; `amh` in the driver list (modified) |
| `tests/pinheck/link/register_build.py`, build files | `hexload.c`, `dmd.c`, `amh.c` in every build (modified / regenerated) |
| `src/wpc/pinheck_names.h`, `tests/pinheck/vpx/{names.py,check.sh}` | America's Most Haunted's names (modified) |
| `tests/pinheck/pic32mx/{hex_test.c,check.sh,pinmame_check.sh,romset_check.sh}`, `tests/pinheck/games.sh` | the HEX unit test; the boot and romset checks for a game without an update |
| `tests/pinheck/link/bootldr_test.c` | a game's boot hold (modified) |
| `tests/pinheck/display/{dmd_test.c,check.sh,frames.py,render.py,pinmame_display.sh}` | the decoder's unit test; the raw DMD in the frame, snapshot and display checks |
| `tests/pinheck/sim/amhsim.py` | the simulator check |
| `tests/pinheck/board/check5.py` | check 5 for America's Most Haunted (modified) |
| `tests/pinheck/vpx/{host.cpp,vpx.py,pinmame_vpx.sh}` | libpinmame's DMD frames, checked exactly (modified) |
| `tests/pinheck/perf/bench.sh` | America's Most Haunted's workloads (modified) |
| `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | this plan's row and what it carries (modified) |

---
### Task 1: Reference build

**Files:** none (a build).

- [ ] **Step 1: Build the reference**

Build `pinheck` as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`) and keep the binary; Domino's, Rob Zombie's and The Jetsons' outputs are compared with it in Tasks 4 and 12:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11c.log 2>&1; tail -1 build/sdl3pinmame/11c.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```
Expected: `[100%] Built target sdl3pinmame`.

### Task 2: The Intel HEX loader

**Files:**
- Create: `src/wpc/pinheck/hexload.h`, `src/wpc/pinheck/hexload.c`, `tests/pinheck/pic32mx/hex_test.c`
- Modify: `tests/pinheck/pic32mx/check.sh`, `src/wpc/pinheck.h`, `src/wpc/pinheck.c`, `tests/pinheck/link/register_build.py`; regenerated build files

**Interfaces:**
- Produces: `long pinheck_hex_flash(const uint8_t *hex, size_t n, uint8_t *flash, uint32_t size, uint32_t base, char *err)`; `PINHECK_HEXREGION` (`REGION_USER3`); `PINHECK_HEX_ROMSTART(name, hex, hexsize, hexhash, prp, prphash)`; `void pinheck_flash_hex(void)` (a game's driver init calls it); the Propeller log line `hex: N bytes of program flash` at every machine start of such a game.

- [ ] **Step 1: The failing unit test**

The test: records with hand-made checksums for every error kind; with `AMH_HEX`, the real file.

`tests/pinheck/pic32mx/hex_test.c`:

```c
#include "hexload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0x1D000000u
#define SIZE 0x80000u

static uint8_t flash[SIZE];
static int fails;

static uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	int k;
	while (n--) {
		c ^= *p++;
		for (k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

/* converts text; want: bytes written, or -1 with err starting with why */
static void expect(const char *name, const char *text, long want, const char *why)
{
	char err[128] = "";
	long got;
	memset(flash, 0xFF, SIZE);
	got = pinheck_hex_flash((const uint8_t *)text, strlen(text), flash, SIZE, BASE, err);
	if (got != want || (want < 0 && strncmp(err, why, strlen(why)))) {
		printf("HEX FAIL %s: got %ld '%s', want %ld '%s'\n", name, got, err, want, why);
		fails++;
	}
}

int main(void)
{
	const char *path = getenv("AMH_HEX");
	const char *good = ":020000041D00DD\n:0400F8000000009D67\n:00000001FF\n";
	expect("records", good, 4, "");
	if (memcmp(flash + 0xF8, "\x00\x00\x00\x9d", 4) || flash[0xF7] != 0xFF || flash[0xFC] != 0xFF) { printf("HEX FAIL records: bytes\n"); fails++; }
	expect("crlf and lower case", ":020000041d00dd\r\n:0400f8000000009d67\r\n:00000001ff\r\n", 4, "");
	expect("start address", ":020000041D00DD\n:040000059D0010004A\n:00000001FF\n", 0, "");
	expect("checksum", ":020000041D00DD\n:0400F8000000009D66\n:00000001FF\n", -1, "line 2: checksum");
	expect("no end", ":020000041D00DD\n:0400F8000000009D67\n", -1, "no end record");
	expect("after end", ":00000001FF\n:00000001FF\n", -1, "line 2: data after");
	expect("below flash", ":0400F8000000009D67\n:00000001FF\n", -1, "line 1: address 000000f8 outside");
	expect("past flash", ":020000041D07D6\n:04FFFE0001020304F5\n:00000001FF\n", -1, "line 2: address 1d07fffe outside");
	expect("boot flash", ":020000041FC01B\n:0400000000000000FC\n:00000001FF\n", -1, "line 2: address 1fc00000 outside");
	expect("segment record", ":020000021000EC\n:00000001FF\n", -1, "line 1: record type 02");
	expect("short", ":0400F8000000\n", -1, "line 1: short record");
	expect("digit", ":0400F80000G0009D67\n:00000001FF\n", -1, "line 1: not a hex digit");
	expect("mark", "0400F8000000009D67\n", -1, "line 1: no record mark");
	if (path) {
		static uint8_t text[1 << 20];
		char err[128] = "";
		FILE *f = fopen(path, "rb");
		size_t n = f ? fread(text, 1, sizeof(text), f) : 0;
		long got;
		if (f) fclose(f);
		memset(flash, 0xFF, SIZE);
		got = pinheck_hex_flash(text, n, flash, SIZE, BASE, err);
		if (n != 705004 || got != 243952 || crc32(flash, 0x3C1D8) != 0x09D9072Fu) {
			printf("HEX FAIL AMH_V023.hex: %lu bytes read, %ld written (%s), image crc %08x\n", (unsigned long)n, got, err, (unsigned)crc32(flash, 0x3C1D8));
			fails++;
		} else {
			size_t k;
			for (k = 0x3C1D8; k < SIZE && flash[k] == 0xFF; k++) ;
			if (k != SIZE) { printf("HEX FAIL AMH_V023.hex: byte %lx past the image is set\n", (unsigned long)k); fails++; }
			else printf("hex: AMH_V023.hex -> %ld bytes, image 0x9D000000-0x9D03C1D7 crc 09d9072f, entry %02x%02x%02x%02x\n", got, flash[0x1003], flash[0x1002], flash[0x1001], flash[0x1000]);
		}
	}
	printf("hex: %d failed\n", fails);
	return fails != 0;
}
```

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/pic32mx/check.sh', [
    (r'''cc $CF -o $B/soc_test soc_test.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c || exit 2
./$B/soc_test || fail=$((fail + 1))
if [ -f boot.c ]; then
	if [ -n "$PINHECK_UPDATE_DIR" ]; then
''',
     r'''cc $CF -o $B/soc_test soc_test.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c || exit 2
./$B/soc_test || fail=$((fail + 1))
# Intel HEX into program flash; AMH_HEX (America's Most Haunted's AMH_V023.hex) adds the real file
cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/hexload.c || { echo "C89 FAIL hexload.c"; fail=$((fail + 1)); }
cc $CF -fsanitize=address,undefined -fno-sanitize-recover=all -I$S/wpc/pinheck -o $B/hex_test hex_test.c $S/wpc/pinheck/hexload.c || exit 2
./$B/hex_test || fail=$((fail + 1))
if [ -f boot.c ]; then
	if [ -n "$PINHECK_UPDATE_DIR" ]; then
'''),
])
EOF
```

Run: `PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 tests/pinheck/pic32mx/check.sh 2>&1 | grep -m1 -o 'hexload.c: No such file'`
Expected: `hexload.c: No such file`.

- [ ] **Step 2: The loader and the driver's HEX region**

`src/wpc/pinheck/hexload.h`:

```c
#ifndef PINHECK_HEXLOAD_H
#define PINHECK_HEXLOAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Intel HEX (records 00 data, 01 end, 04 upper address, 05 start address) into a flash image of size bytes at
   physical address base. Every record's checksum is checked, every byte must fall inside the image and the file must
   end with record 01. Returns the data bytes written, or -1 with the reason in err (at least 80 bytes). */
long pinheck_hex_flash(const uint8_t *hex, size_t n, uint8_t *flash, uint32_t size, uint32_t base, char *err);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/hexload.c`:

```c
#include "hexload.h"
#include <stdio.h>

static int nibble(uint8_t c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static int byte_at(const uint8_t *p)
{
	int hi = nibble(p[0]), lo = nibble(p[1]);
	return hi < 0 || lo < 0 ? -1 : hi << 4 | lo;
}

long pinheck_hex_flash(const uint8_t *hex, size_t n, uint8_t *flash, uint32_t size, uint32_t base, char *err)
{
	uint8_t rec[5 + 255];
	uint32_t upper = 0;
	size_t i = 0;
	long line = 0, written = 0;
	int k;

	while (i < n) {
		int len, sum = 0, type;
		uint32_t a;
		if (hex[i] == '\r' || hex[i] == '\n' || hex[i] == ' ' || hex[i] == '\t') { i++; continue; }
		line++;
		if (hex[i] != ':') { sprintf(err, "line %ld: no record mark", line); return -1; }
		if (i + 11 > n || (len = byte_at(hex + i + 1)) < 0) { sprintf(err, "line %ld: short record", line); return -1; }
		if (i + 11 + 2 * (size_t)len > n) { sprintf(err, "line %ld: short record", line); return -1; }
		for (k = 0; k < len + 5; k++) {
			int b = byte_at(hex + i + 1 + 2 * k);
			if (b < 0) { sprintf(err, "line %ld: not a hex digit", line); return -1; }
			rec[k] = (uint8_t)b;
			sum += b;
		}
		if (sum & 0xFF) { sprintf(err, "line %ld: checksum", line); return -1; }
		i += 11 + 2 * (size_t)len;
		type = rec[3];
		a = upper + ((uint32_t)rec[1] << 8 | rec[2]);
		if (type == 0) {
			if (a < base || a - base > size || (uint32_t)len > size - (a - base)) { sprintf(err, "line %ld: address %08lx outside flash", line, (unsigned long)a); return -1; }
			for (k = 0; k < len; k++) flash[a - base + k] = rec[4 + k];
			written += len;
		} else if (type == 1) {
			while (i < n && (hex[i] == '\r' || hex[i] == '\n' || hex[i] == ' ' || hex[i] == '\t' || hex[i] == 0x1A)) i++;
			if (i < n) { sprintf(err, "line %ld: data after the end record", line + 1); return -1; }
			return written;
		} else if (type == 4 && len == 2) {
			upper = ((uint32_t)rec[4] << 8 | rec[5]) << 16;
		} else if (type != 5 || len != 4) {
			sprintf(err, "line %ld: record type %02x not supported", line, (unsigned)type);
			return -1;
		}
	}
	sprintf(err, "no end record");
	return -1;
}
```

The driver: a HEX region in the romset macros, the conversion at driver init, the refusal, the log line; the build group:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
    (r'''#include "pinheck/board.h"
#include "pinheck.h"''',
     r'''#include "pinheck/board.h"
#include "pinheck/hexload.h"
#include "pinheck.h"'''),
    (r'''{
	return !memory_region(PINHECK_CPUREGION) || !memory_region(PINHECK_PROPREGION);
}

''',
     r'''{
	return !memory_region(PINHECK_CPUREGION) || !memory_region(PINHECK_PROPREGION);
}

static int hex_bad;   /* the game's Intel HEX did not convert: the machine refuses to run */
static long hex_bytes; /* data bytes the Intel HEX programmed */

/* driver init of a game whose PIC32 image is Intel HEX (PINHECK_HEXREGION): program flash from it, once a launch */
void pinheck_flash_hex(void)
{
	char err[96], msg[200];
	long n;
	hex_bad = 0;
	hex_bytes = 0;
	if (!memory_region(PINHECK_HEXREGION) || !memory_region(PINHECK_CPUREGION)) return;
	n = pinheck_hex_flash(memory_region(PINHECK_HEXREGION), memory_region_length(PINHECK_HEXREGION), memory_region(PINHECK_CPUREGION),
	                      memory_region_length(PINHECK_CPUREGION), 0x1D000000u, err);
	if (n >= 0) {
		hex_bytes = n;
		return;
	}
	hex_bad = 1;
	sprintf(msg, "pinheck: %.16s: the PIC32 image (Intel HEX) does not convert: %.80s", Machine->gamedrv->name, err);
	pinheck_warn(msg);
}

'''),
    (r'''		usrintf_showmessage_secs(PINHECK_REFUSE_SECS, "'%.16s' is the pinHeck system set, not a game. Run a game such as dominos.", Machine->gamedrv->name);
		logerror("pinheck: '%s' is the pinHeck system set, not a game\n", Machine->gamedrv->name);
		return;
	}
''',
     r'''		usrintf_showmessage_secs(PINHECK_REFUSE_SECS, "'%.16s' is the pinHeck system set, not a game. Run a game such as dominos.", Machine->gamedrv->name);
		logerror("pinheck: '%s' is the pinHeck system set, not a game\n", Machine->gamedrv->name);
		return;
	}
	if (hex_bad && memory_region(PINHECK_HEXREGION)) {
		locals.idle = 1;
		usrintf_showmessage_secs(PINHECK_REFUSE_SECS, "%.16s: the PIC32 image (Intel HEX) does not convert.", Machine->gamedrv->name);
		return;
	}
'''),
    (r'''	boot_set_log(&boot, pinheck_prop_log, NULL);
''',
     r'''	boot_set_log(&boot, pinheck_prop_log, NULL);
	if (hex_bytes) {
		char msg[64];
		sprintf(msg, "hex: %ld bytes of program flash", hex_bytes);
		pinheck_prop_log(NULL, msg);
	}
'''),
])
edit('src/wpc/pinheck.h', [
    (r'''#define PINHECK_PROPREGION REGION_USER1
#define PINHECK_BIOSREGION REGION_USER2

/* the panel is drawn 2x2 per dot in the PinMAME and VPinMAME windows; libpinmame hosts
''',
     r'''#define PINHECK_PROPREGION REGION_USER1
#define PINHECK_BIOSREGION REGION_USER2
#define PINHECK_HEXREGION  REGION_USER3 /* a PIC32 image as Intel HEX, converted into PINHECK_CPUREGION */

/* the panel is drawn 2x2 per dot in the PinMAME and VPinMAME windows; libpinmame hosts
'''),
    (r'''      ROM_LOAD(prp, 0x0000, 0x8000, prphash)

#define PINHECK_ROMEND ROM_END

''',
     r'''      ROM_LOAD(prp, 0x0000, 0x8000, prphash)

/* a game whose PIC32 image is Intel HEX: the driver programs flash from it at start (pinheck_flash_hex) */
#define PINHECK_HEX_ROMSTART(name, hex, hexsize, hexhash, prp, prphash) \
  PINHECK_BIOS_ROMSTART(name) \
    ROM_REGION(0x80000, PINHECK_CPUREGION, ROMREGION_ERASEFF) \
    ROM_REGION(hexsize, PINHECK_HEXREGION, 0) \
      ROM_LOAD(hex, 0x0000, hexsize, hexhash) \
    ROM_REGION(0x8000, PINHECK_PROPREGION, 0) \
      ROM_LOAD(prp, 0x0000, 0x8000, prphash)

#define PINHECK_ROMEND ROM_END

'''),
    (r'''extern int pinheck_servo(int servo);
''',
     r'''extern int pinheck_servo(int servo);
extern void pinheck_flash_hex(void);
'''),
])
edit('tests/pinheck/link/register_build.py', [
    (r'''     'DRVLIBS += $(PINOBJ)/pinheck/board.o\n'),
]''',
     r'''     'DRVLIBS += $(PINOBJ)/pinheck/board.o\n'),
    (['src/wpc/pinheck/hexload.c', 'src/wpc/pinheck/hexload.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/hexload.o\n'),
]'''),
])
EOF
```

Then: `python3 tests/pinheck/link/register_build.py`
Expected: 19 `updated …` lines (`cmake/*` ×10, `src/pinmame.mak`, `vcproj/*` ×8); a second run prints nothing.

- [ ] **Step 3: The unit test passes**

Run: `AMH_HEX=$AMH_HEX PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 tests/pinheck/pic32mx/check.sh | tail -6` (without the update the Domino's boot test is skipped; Task 5 runs it)
Expected:
```text
hex: AMH_V023.hex -> 243952 bytes, image 0x9D000000-0x9D03C1D7 crc 09d9072f, entry 3c1a9d00
hex: 0 failed
makefile link: ok
symbols: ok
vcxproj: ok
pic32mx: 0 failed
```

- [ ] **Step 4: Build**

The build reads the root `CMakeLists.txt`, a copy that predates the registration, so copy it again first:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11c.log 2>&1; tail -1 build/sdl3pinmame/11c.log
grep -i warning build/sdl3pinmame/11c.log | grep -cE 'pinheck|hexload'
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck/hexload.h src/wpc/pinheck/hexload.c src/wpc/pinheck.h src/wpc/pinheck.c tests/pinheck/pic32mx/hex_test.c tests/pinheck/pic32mx/check.sh tests/pinheck/link/register_build.py src/pinmame.mak cmake vcproj
git commit -m "pinheck: PIC32 firmware from an Intel HEX in the romset (validated, converted at driver init)"
```

### Task 3: Game data `dmdHub` and `bootHold`, a game's boot hold

**Files:**
- Modify: `tests/pinheck/link/bootldr_test.c`, `src/wpc/pinheck/bootldr.h`, `src/wpc/pinheck/bootldr.c`, `src/wpc/pinheck.h`, `src/wpc/pinheck.c`, `src/wpc/pinheckgames.c`, `src/wpc/sims/pinheck/jetsons.c`, `src/wpc/sims/pinheck/rzspook.c`

**Interfaces:**
- Consumes: Plan 11b's `pinheck_tGameData`, `PINHECK_DOMINOS_DATA`.
- Produces: `pinheck_tGameData.dmdHub` (hub address of a raw DMD's 4 bpp frame; 0: the display link) and `.bootHold` (ms); `PINHECK_DOMINOS_DATA` = `128, 32, 340, 1000, 2000, 0, 1, 0, 3000`; `void boot_set_window(pic32_boot *b, uint64_t pic_cycles)` (from the next reset on; `BOOT_WINDOW` until set).

- [ ] **Step 1: The failing test**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/link/bootldr_test.c', [
    (r'''	CHECK(b.state == BOOT_APP && boot_hold(&b, now) == 0);
	CHECK(tx_l[ntx - 1] == 0 && tx_t[ntx - 1] == 1000 + BOOT_WINDOW);
}

''',
     r'''	CHECK(b.state == BOOT_APP && boot_hold(&b, now) == 0);
	CHECK(tx_l[ntx - 1] == 0 && tx_t[ntx - 1] == 1000 + BOOT_WINDOW);
}

/* a game's own hold (America's Most Haunted: 5 s) applies from the next reset on */
static void window_set(void)
{
	start();
	boot_set_window(&b, 400000000u);
	boot_reset(&b, now);
	run_until(1000 + 400000000u - 1);
	CHECK(b.state == BOOT_WAIT && boot_hold(&b, now) > 0);
	run_until(1000 + 400000000u);
	CHECK(b.state == BOOT_APP && boot_hold(&b, now) == 0 && tx_t[ntx - 1] == 1000 + 400000000u);
}

'''),
    (r'''	reply_timing_independent_of_step();
	window_without_host();
	sign_on_holds();
	program_verify_leave();
''',
     r'''	reply_timing_independent_of_step();
	window_without_host();
	window_set();
	sign_on_holds();
	program_verify_leave();
'''),
])
EOF
```

Run: `PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 tests/pinheck/link/check.sh 2>&1 | grep -m1 -o "error: implicit declaration of function"`
Expected: `error: implicit declaration of function` (`boot_set_window`).

- [ ] **Step 2: The window and the game data**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.h', [
    (r'''  int inService;          /* PINHECK_INSERVICE seeds the update record (version: core.hw.gameSpecific1) */
} pinheck_tGameData;

/* Domino's values: 128 x 32, POSITION 340, servo levels 1.0-2.0 ms, WS2801 as sent, PINHECK_INSERVICE applies.
   Only Domino's and the system set use them; every other game spells out its own */
#define PINHECK_DOMINOS_DATA 128, 32, 340, 1000, 2000, 0, 1
''',
     r'''  int inService;          /* PINHECK_INSERVICE seeds the update record (version: core.hw.gameSpecific1) */
  int dmdHub;             /* a raw 128 x 32 DMD scanned by a Propeller cog: hub address of its 4 bpp frame; 0: the display link */
  int bootHold;           /* ms the bootloader stand-in holds the PIC32 after a reset without a sign-on */
} pinheck_tGameData;

/* Domino's values: 128 x 32, POSITION 340, servo levels 1.0-2.0 ms, WS2801 as sent, PINHECK_INSERVICE applies,
   the display link, a 3 s boot hold. Only Domino's and the system set use them; every other game spells out its own */
#define PINHECK_DOMINOS_DATA 128, 32, 340, 1000, 2000, 0, 1, 0, 3000
'''),
])
edit('src/wpc/pinheckgames.c', [
    (r'''};

/* data: the game's width, height, aligned, servoMin, servoMax, rgbInverted, inService (pinheck_tGameData) */
#define INIT_PINHECK(name, balls, version, data) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
''',
     r'''};

/* data: the game's width, height, aligned, servoMin, servoMax, rgbInverted, inService, dmdHub, bootHold (pinheck_tGameData) */
#define INIT_PINHECK(name, balls, version, data) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
'''),
])
edit('src/wpc/sims/pinheck/jetsons.c', [
    (r'''      pinheck_getsol, jetsons_handleMech, jetsons_getMech },
    &jetsonsSimData },
  128, 64, 55, 544, 2400, 0, 1
};

''',
     r'''      pinheck_getsol, jetsons_handleMech, jetsons_getMech },
    &jetsonsSimData },
  128, 64, 55, 544, 2400, 0, 1, 0, 3000
};

'''),
])
edit('src/wpc/sims/pinheck/rzspook.c', [
    (r'''      pinheck_getsol, rzspook_handleMech, rzspook_getMech },
    &rzspookSimData },
  128, 32, 460, 544, 2400, 0, 1
};

''',
     r'''      pinheck_getsol, rzspook_handleMech, rzspook_getMech },
    &rzspookSimData },
  128, 32, 460, 544, 2400, 0, 1, 0, 3000
};

'''),
])
edit('src/wpc/pinheck/bootldr.h', [
    (r'''	uint32_t flash_size;
	int state;
	uint64_t window_end, app_at, tx_free;
	int line;
	uint64_t edge_t[BOOT_EDGES];
''',
     r'''	uint32_t flash_size;
	int state;
	uint64_t window, window_end, app_at, tx_free; /* window: the hold after a reset, BOOT_WINDOW unless set */
	int line;
	uint64_t edge_t[BOOT_EDGES];
'''),
    (r'''void boot_init(pic32_boot *b, uint8_t *flash, uint32_t flash_size, boot_tx_fn tx, void *tx_ctx);
void boot_set_log(pic32_boot *b, boot_log_fn fn, void *ctx);
void boot_reset(pic32_boot *b, uint64_t pic_cycle);
void boot_rx(pic32_boot *b, uint64_t pic_cycle, int level);
''',
     r'''void boot_init(pic32_boot *b, uint8_t *flash, uint32_t flash_size, boot_tx_fn tx, void *tx_ctx);
void boot_set_log(pic32_boot *b, boot_log_fn fn, void *ctx);
void boot_set_window(pic32_boot *b, uint64_t pic_cycles); /* from the next reset on */
void boot_reset(pic32_boot *b, uint64_t pic_cycle);
void boot_rx(pic32_boot *b, uint64_t pic_cycle, int level);
'''),
])
edit('src/wpc/pinheck/bootldr.c', [
    (r'''	b->tx = tx;
	b->tx_ctx = tx_ctx;
	boot_reset(b, 0);
}

''',
     r'''	b->tx = tx;
	b->tx_ctx = tx_ctx;
	b->window = BOOT_WINDOW;
	boot_reset(b, 0);
}

void boot_set_window(pic32_boot *b, uint64_t pic_cycles)
{
	b->window = pic_cycles;
}

'''),
    (r'''{
	b->state = BOOT_WAIT;
	b->window_end = t + BOOT_WINDOW;
	b->app_at = 0;
	b->tx_free = t;
''',
     r'''{
	b->state = BOOT_WAIT;
	b->window_end = t + b->window;
	b->app_at = 0;
	b->tx_free = t;
'''),
])
edit('src/wpc/pinheck.c', [
    (r'''		pinheck_prop_log(NULL, msg);
	}
	pinheck_open_card();''',
     r'''		pinheck_prop_log(NULL, msg);
	}
	boot_set_window(&boot, (uint64_t)pinheck_game()->bootHold * (PINHECK_CLOCK / 1000));
	pinheck_open_card();'''),
])
EOF
```

- [ ] **Step 3: The test passes, build**

```sh
PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 tests/pinheck/link/check.sh | tail -1
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11c.log 2>&1; tail -1 build/sdl3pinmame/11c.log
grep -i warning build/sdl3pinmame/11c.log | grep -cE 'pinheck|bootldr|jetsons|rzspook|dominos'
```
Expected: `link: 0 failed`, `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/link/bootldr_test.c src/wpc/pinheck/bootldr.h src/wpc/pinheck/bootldr.c src/wpc/pinheck.h src/wpc/pinheck.c src/wpc/pinheckgames.c src/wpc/sims/pinheck/jetsons.c src/wpc/sims/pinheck/rzspook.c
git commit -m "pinheck: game data dmdHub and bootHold (the stand-in bootloader's hold per game, 3 s for the games so far)"
```

### Task 4: The raw DMD

**Files:**
- Create: `src/wpc/pinheck/dmd.h`, `src/wpc/pinheck/dmd.c`, `tests/pinheck/display/dmd_test.c`
- Modify: `tests/pinheck/display/check.sh`, `src/wpc/core.h`, `src/wpc/core.c`, `src/wpc/pinheck.h`, `src/wpc/pinheck.c`, `tests/pinheck/link/register_build.py`; regenerated build files

**Interfaces:**
- Consumes: Task 3's `dmdHub`; the core's `core_dmd_pwm_init`, `core_dmd_submit_frame`; `prop_set_pins`, `prop_set_pins_mask`, `prop_sync`.
- Produces: `pinheck_dmd_init(d, full, hub, buf, ctx, on_sub, on_frame)`, `pinheck_dmd_pins(d, t, out, dir)`, `DMD_W`, `DMD_H`, `DMD_ROW`, `DMD_SUB`, `DMD_ROW_PINS`, `DMD_ALL_PINS`; `CORE_DMD_PWM_FILTER_PINHECK_16`, `CORE_DMD_PWM_COMBINER_SUM_16`; `MACHINE_DRIVER_EXTERN(PINHECKDMD)`, `gl_mPINHECKDMD`; test hooks `PINHECK_DMD_PROOF` (the row model beside the decoder: `dmd: proof: N subframes, the row model differs in M (R rows)` in the Propeller log at stop), `PINHECK_DMD_LOG` (each vblank: a uint32 count, then the subframes handed to the core), `PINHECK_PROP_SERIAL` (a raw-DMD game's Propeller serial lines, `secs text`); `PINHECK_FRAME_LOG` of a raw DMD (Ruling 5).

- [ ] **Step 1: The failing unit test**

The test: the cog's loop as pin writes, into both decoders.

`tests/pinheck/display/dmd_test.c`:

```c
#include "dmd.h"
#include <stdio.h>
#include <string.h>

/* the cog's scan loop (PROP_023.BIN, hub $336C) as pin writes: masks P16-P21, outa = P19|P21 at start, flags clear;
   per subframe level 0-15, per row 64 bytes high nibble first: data = value > level (muxc), then muxz/muxnz on P17;
   at the row's end P20 = first row (muxz), Z = 1, then P18 and P19 pulsed (muxz, muxnz) */
#define P(n) (1u << (n))

static uint8_t hub[65536];
static pinheck_dmd row, full;
static uint8_t subs[2][64][DMD_SUB];
static int levels[2][64], nsub[2];
static uint8_t frames[2][8][DMD_W * DMD_H];
static int nframe[2];
static uint32_t out, dir;
static uint64_t t;
static int fails;

static void on_sub(void *ctx, const uint8_t *s, int level, uint64_t when)
{
	int k = *(int *)ctx;
	(void)when;
	if (nsub[k] < 64) { memcpy(subs[k][nsub[k]], s, DMD_SUB); levels[k][nsub[k]] = level; }
	nsub[k]++;
}

static void on_frame(void *ctx, const uint8_t *f, uint64_t when)
{
	int k = *(int *)ctx;
	(void)when;
	if (nframe[k] < 8) memcpy(frames[k][nframe[k]], f, DMD_W * DMD_H);
	nframe[k]++;
}

static void put(void)
{
	t += 4;
	pinheck_dmd_pins(&row, t, out, dir);
	pinheck_dmd_pins(&full, t, out, dir);
}

static void mux(uint32_t pin, int v) { out = v ? out | pin : out & ~pin; put(); }

/* the cog runs subframes from its start: z is the Z flag, clear at cog start */
static void scan(uint32_t buf, int nsubs)
{
	int s, r, b, h, z = 0;
	for (s = 0; s < 6; s++) { dir |= P(16 + s); put(); }
	out = P(19); put();
	out |= P(21); put();
	for (s = 0; s < nsubs; s++) {
		const int level = s % 16;
		for (r = 0; r < 32; r++) {
			for (b = 0; b < 64; b++) {
				const uint8_t v = hub[buf + 64 * r + b];
				t += 8;
				for (h = 0; h < 2; h++) {
					const int n = h ? v & 15 : v >> 4;
					mux(P(16), level < n);
					mux(P(17), z);
					mux(P(17), !z);
				}
			}
			mux(P(20), r == 0);
			z = 1;
			mux(P(18), z);
			mux(P(18), !z);
			mux(P(19), z);
			mux(P(19), !z);
		}
	}
}

static void check(const char *name, int ok)
{
	if (!ok) { printf("DMD FAIL %s\n", name); fails++; }
}

static void reset(void)
{
	static int ids[2] = { 0, 1 };
	pinheck_dmd_init(&row, 0, hub, 0x5B0C, &ids[0], on_sub, on_frame);
	pinheck_dmd_init(&full, 1, NULL, 0, &ids[1], on_sub, on_frame);
	memset(nsub, 0, sizeof(nsub));
	memset(nframe, 0, sizeof(nframe));
	out = dir = 0;
}

int main(void)
{
	int x, y, k, ok;
	for (y = 0; y < 32; y++)
		for (x = 0; x < 128; x += 2) hub[0x5B0C + 64 * y + x / 2] = (uint8_t)(((x + y) % 16) << 4 | (x * 7 + y) % 16);
	reset();
	scan(0x5B0C, 40);
	/* the first subframe's first row has no row clock (P19 starts high): subframes from level 1 on */
	check("subframes", nsub[0] == 39 && nsub[1] == 39 && levels[0][0] == 1 && levels[0][15] == 0 && levels[1][15] == 0);
	check("row model = shifted bits", !memcmp(subs[0], subs[1], sizeof(subs[0])) && !memcmp(levels[0], levels[1], sizeof(levels[0])));
	/* subframe of level L: a dot is lit while its value is above L */
	for (ok = 1, y = 0; y < 32; y++)
		for (x = 0; x < 128; x++) {
			const uint8_t v = hub[0x5B0C + 64 * y + x / 2], n = x & 1 ? v & 15 : v >> 4;
			const int lit = subs[1][3][16 * y + x / 8] >> (7 - x % 8) & 1;
			if (lit != (n > levels[1][3])) ok = 0;
		}
	check("dots lit above the level", ok);
	/* whole cycles from level 0: one frame of 16 subframes = the dot values */
	check("frames", nframe[0] == 1 && nframe[1] == 1 && !memcmp(frames[0][0], frames[1][0], DMD_W * DMD_H));
	for (ok = nframe[0] == 1, y = 0; ok && y < 32; y++)
		for (x = 0; x < 128; x++) {
			const uint8_t v = hub[0x5B0C + 64 * y + x / 2], n = x & 1 ? v & 15 : v >> 4;
			if (frames[0][0][128 * y + x] != n) ok = 0;
		}
	check("frame = 4 bpp buffer", ok);
	/* the cog stops (DIRA cleared, a Propeller reboot) mid-subframe and starts again: nothing stale is kept */
	for (k = 0; k < 6; k++) { dir &= ~P(16 + k); put(); }
	out = 0; put();
	memset(nsub, 0, sizeof(nsub));
	memset(nframe, 0, sizeof(nframe));
	scan(0x5B0C, 33);
	check("restart", nsub[0] == 32 && nsub[1] == 32 && levels[0][0] == 1 && nframe[0] == 1 && !memcmp(subs[0], subs[1], sizeof(subs[0])));
	printf("dmd: %d failed\n", fails);
	return fails != 0;
}
```

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/check.sh', [
    (r'''cc $CF $SAN -I$S/wpc/pinheck -o $B/display_test display_test.c $S/wpc/pinheck/display.c || exit 2
./$B/display_test || fail=$((fail + 1))
cc $CF $SAN -I$S/wpc/pinheck -o $B/lookdump lookdump.c $S/wpc/pinheck/display.c || exit 2
python3 look.py crosscheck ./$B/lookdump || fail=$((fail + 1))
''',
     r'''cc $CF $SAN -I$S/wpc/pinheck -o $B/display_test display_test.c $S/wpc/pinheck/display.c || exit 2
./$B/display_test || fail=$((fail + 1))
cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/dmd.c || fail=$((fail + 1))
cc $CF $SAN -I$S/wpc/pinheck -o $B/dmd_test dmd_test.c $S/wpc/pinheck/dmd.c || exit 2
./$B/dmd_test || fail=$((fail + 1))
cc $CF $SAN -I$S/wpc/pinheck -o $B/lookdump lookdump.c $S/wpc/pinheck/display.c || exit 2
python3 look.py crosscheck ./$B/lookdump || fail=$((fail + 1))
'''),
])
EOF
```

Run: `PINHECK_SKIP_FIRMWARE=1 nice -n 15 tests/pinheck/display/check.sh 2>&1 | grep -m1 -o 'dmd.c: No such file'`
Expected: `dmd.c: No such file`.

- [ ] **Step 2: The decoder**

`src/wpc/pinheck/dmd.h`:

```c
#ifndef PINHECK_DMD_H
#define PINHECK_DMD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A raw 128 x 32 dot matrix scanned by a Propeller cog: per row 128 dots shifted on P16 (data) and P17 (clock,
   rising), P18 latches them (rising), P19 clocks the row driver (rising) with P20 high for the first row. A
   subframe is 32 rows, 1 bit per dot, 16 bytes per row, the first dot shifted in the top bit of byte 0. */
#define DMD_W      128
#define DMD_H      32
#define DMD_ROW    (DMD_W / 8)
#define DMD_SUB    (DMD_ROW * DMD_H)
#define DMD_LEVELS 16
#define DMD_P16 (1u << 16)
#define DMD_P17 (1u << 17)
#define DMD_P18 (1u << 18)
#define DMD_P19 (1u << 19)
#define DMD_P20 (1u << 20)
#define DMD_ROW_PINS (DMD_P18 | DMD_P19 | DMD_P20)
#define DMD_ALL_PINS (DMD_P16 | DMD_P17 | DMD_ROW_PINS)

typedef void (*dmd_sub_fn)(void *ctx, const uint8_t *sub, int level, uint64_t t); /* level 0-15, -1 unknown */
typedef void (*dmd_frame_fn)(void *ctx, const uint8_t *shades, uint64_t t);       /* DMD_W * DMD_H dots, 0-15 */

typedef struct pinheck_dmd {
	void *ctx;
	dmd_sub_fn on_sub;
	dmd_frame_fn on_frame;
	const uint8_t *hub;  /* row model: hub RAM */
	uint32_t buf;        /* row model: hub address of the 4 bpp frame (two dots a byte, the left one high) */
	uint32_t level;      /* pins as last seen */
	int full;            /* 1: dots from the shifted bits (P16/P17), 0: from hub RAM at the latch */
	int on;              /* the scan pins are outputs */
	int rows;            /* latches since the first row, -1 before one */
	int row;             /* the row driver's position, -1 before its first row */
	int count;           /* subframes started since the pins became outputs */
	int sublevel;        /* level of the subframe being drawn, -1 unknown */
	uint32_t nbits;      /* bits shifted */
	uint8_t shift[DMD_ROW], latched[DMD_ROW];
	uint8_t sub[DMD_SUB];
	uint8_t sum[DMD_W * DMD_H];
	int nsum;            /* subframes in sum, from level 0 on; -1 until one starts */
} pinheck_dmd;

/* full: 1 decodes P16/P17 (mask DMD_ALL_PINS), 0 takes the dots from hub (mask DMD_ROW_PINS) */
void pinheck_dmd_init(pinheck_dmd *d, int full, const uint8_t *hub, uint32_t buf, void *ctx, dmd_sub_fn on_sub, dmd_frame_fn on_frame);
void pinheck_dmd_pins(pinheck_dmd *d, uint64_t t, uint32_t out, uint32_t dir);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/dmd.c`:

```c
#include "dmd.h"
#include <string.h>

static void restart(pinheck_dmd *d)
{
	d->rows = d->row = d->nsum = d->sublevel = -1;
	d->count = 0;
	d->nbits = 0;
	memset(d->shift, 0, sizeof(d->shift));
	memset(d->latched, 0, sizeof(d->latched));
	memset(d->sub, 0, sizeof(d->sub));
}

void pinheck_dmd_init(pinheck_dmd *d, int full, const uint8_t *hub, uint32_t buf, void *ctx, dmd_sub_fn on_sub, dmd_frame_fn on_frame)
{
	memset(d, 0, sizeof(*d));
	d->full = full;
	d->hub = hub;
	d->buf = buf;
	d->ctx = ctx;
	d->on_sub = on_sub;
	d->on_frame = on_frame;
	restart(d);
}

/* P18 rising: the row's dots go to the output latches; P20 marks a subframe's first row */
static void latch(pinheck_dmd *d, uint32_t now)
{
	int x;
	if (now & DMD_P20) {
		d->rows = 0;
		d->count++;
	} else
		d->rows = d->rows >= 0 && d->rows < DMD_H - 1 ? d->rows + 1 : -1;
	if (d->full) {
		const uint32_t start = d->nbits % DMD_W; /* the oldest of the last 128 bits */
		if (!start) memcpy(d->latched, d->shift, DMD_ROW);
		else {
			memset(d->latched, 0, DMD_ROW);
			for (x = 0; x < DMD_W; x++) {
				const uint32_t b = (start + (uint32_t)x) % DMD_W;
				if (d->shift[b >> 3] & (0x80 >> (b & 7))) d->latched[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
			}
		}
	} else {
		/* the cog shows subframe k after its start at level k mod 16: a dot is lit while its value is above the level */
		const int level = (d->count - 1) % DMD_LEVELS;
		memset(d->latched, 0, DMD_ROW);
		if (d->rows < 0 || !d->count) return;
		for (x = 0; x < DMD_W; x += 2) {
			const uint8_t v = d->hub[(d->buf + (uint32_t)d->rows * (DMD_W / 2) + (uint32_t)(x >> 1)) & 0xFFFFu];
			if ((v >> 4) > level) d->latched[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
			if ((v & 15) > level) d->latched[x >> 3] |= (uint8_t)(0x40 >> (x & 7));
		}
	}
}

static void subframe(pinheck_dmd *d, uint64_t t)
{
	int i, k;
	if (d->on_sub) d->on_sub(d->ctx, d->sub, d->sublevel, t);
	if (!d->on_frame) return;
	if (d->sublevel == 0) {
		memset(d->sum, 0, sizeof(d->sum));
		d->nsum = 0;
	}
	if (d->nsum < 0 || d->sublevel != d->nsum) { d->nsum = -1; return; }
	for (i = 0; i < DMD_SUB; i++)
		if (d->sub[i])
			for (k = 0; k < 8; k++) d->sum[8 * i + k] += (uint8_t)(d->sub[i] >> (7 - k) & 1);
	if (++d->nsum == DMD_LEVELS) {
		d->on_frame(d->ctx, d->sum, t);
		d->nsum = -1;
	}
}

/* P19 rising: the row driver shifts, starting over at the first row while P20 is high */
static void row_clock(pinheck_dmd *d, uint32_t now, uint64_t t)
{
	if (now & DMD_P20) {
		d->row = 0;
		d->sublevel = d->count > 0 ? (d->count - 1) % DMD_LEVELS : -1;
	} else
		d->row = d->row >= 0 && d->row < DMD_H - 1 ? d->row + 1 : -1;
	if (d->row < 0) return;
	memcpy(d->sub + d->row * DMD_ROW, d->latched, DMD_ROW);
	if (d->row == DMD_H - 1) subframe(d, t);
}

void pinheck_dmd_pins(pinheck_dmd *d, uint64_t t, uint32_t out, uint32_t dir)
{
	const uint32_t now = out & dir & (d->full ? DMD_ALL_PINS : DMD_ROW_PINS), rise = now & ~d->level;
	const int on = (dir & DMD_ROW_PINS) == DMD_ROW_PINS;

	d->level = now;
	if (on != d->on) {
		d->on = on;
		restart(d);
	}
	if (!on) return;
	if (rise & DMD_P17) {
		const uint32_t b = d->nbits % DMD_W;
		if (now & DMD_P16) d->shift[b >> 3] |= (uint8_t)(0x80 >> (b & 7));
		else d->shift[b >> 3] &= (uint8_t)~(0x80 >> (b & 7));
		d->nbits++;
	}
	if (rise & DMD_P18) latch(d, now);
	if (rise & DMD_P19) row_clock(d, now, t);
}
```

Run: `PINHECK_SKIP_FIRMWARE=1 nice -n 15 tests/pinheck/display/check.sh | tail -4`
Expected:
```text
dmd: 0 failed
look: display.c equals look.py in 8 looks
look: sdl3pinmame renders the look, libpinmame gets the frame as sent
display: 0 failed
```

- [ ] **Step 3: The core's filter, the driver's DMD path**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
    (r'''#include "pinheck/board.h"
#include "pinheck/hexload.h"''',
     r'''#include "pinheck/board.h"
#include "pinheck/dmd.h"
#include "pinheck/hexload.h"'''),
    (r'''		logerror("pinheck: %s: inverted WS2801 lines are not supported\n", Machine->gamedrv->name);
	}
	if (!DISPLAY_SIZE_OK(g->width, g->height)) {
		fprintf(stderr, "pinheck: %s: a %dx%d display is not supported, frames are taken as 128x32\n", Machine->gamedrv->name, g->width, g->height);
		logerror("pinheck: %s: a %dx%d display is not supported\n", Machine->gamedrv->name, g->width, g->height);
''',
     r'''		logerror("pinheck: %s: inverted WS2801 lines are not supported\n", Machine->gamedrv->name);
	}
	if (g->dmdHub && (g->width != DMD_W || g->height != DMD_H)) {
		fprintf(stderr, "pinheck: %s: a %dx%d raw DMD is not supported, it is taken as 128x32\n", Machine->gamedrv->name, g->width, g->height);
		logerror("pinheck: %s: a %dx%d raw DMD is not supported\n", Machine->gamedrv->name, g->width, g->height);
	} else if (!g->dmdHub && !DISPLAY_SIZE_OK(g->width, g->height)) {
		fprintf(stderr, "pinheck: %s: a %dx%d display is not supported, frames are taken as 128x32\n", Machine->gamedrv->name, g->width, g->height);
		logerror("pinheck: %s: a %dx%d display is not supported\n", Machine->gamedrv->name, g->width, g->height);
'''),
    (r'''}

static void pinheck_disp_init(void)
{
''',
     r'''}

/* The raw DMD (game data dmdHub): the Propeller thread decodes subframes from the scan pins (P16-P20) into a ring,
   which the emulation thread hands to the core's PWM integration at each vblank, after the Propeller has caught up.
   Test hook PINHECK_DMD_PROOF also runs the row model (P18-P20 only, a row's dots from hub RAM at its latch) and
   counts the subframes in which it differs. */
#define PINHECK_DMD_RING 64
static pinheck_dmd dmd, dmd_row;
static int dmd_on, dmd_proof;
static uint8_t dmd_ring[PINHECK_DMD_RING][DMD_SUB];
static unsigned dmd_head, dmd_tail, dmd_dropped;
static unsigned long dmd_subs, dmd_differ;
static uint64_t dmd_first;

static void pinheck_dmd_sub(void *ctx, const uint8_t *sub, int level, uint64_t t)
{
	(void)ctx; (void)level; (void)t;
	if (dmd_head - dmd_tail == PINHECK_DMD_RING) { dmd_tail++; dmd_dropped++; }
	memcpy(dmd_ring[dmd_head % PINHECK_DMD_RING], sub, DMD_SUB);
	dmd_head++;
}

/* called after the decoder's: its subframe for the same row clock is complete in dmd.sub; dmd_rows counts the rows */
static unsigned long dmd_rows;

static void pinheck_dmd_row_sub(void *ctx, const uint8_t *sub, int level, uint64_t t)
{
	int r, n = 0;
	(void)ctx; (void)level;
	dmd_subs++;
	for (r = 0; r < DMD_H; r++) n += memcmp(sub + r * DMD_ROW, dmd.sub + r * DMD_ROW, DMD_ROW) != 0;
	if (!n) return;
	if (!dmd_differ++) dmd_first = t;
	dmd_rows += (unsigned long)n;
}

/* test log (PINHECK_FRAME_LOG): each complete cycle of 16 subframes, one byte per dot (0-15); hub address = dmdHub
   when the frame buffer holds that frame at its end */
static void pinheck_dmd_frame(void *ctx, const uint8_t *shades, uint64_t t)
{
	uint8_t stamp[20];
	uint64_t pic = prop_stamp(&prop);
	uint32_t at = (uint32_t)pinheck_game()->dmdHub;
	int k;
	(void)ctx;
	for (k = 0; k < DMD_W * DMD_H && at != 0xFFFFFFFFu; k += 2)
		if (prop.chip.hub[(at + k / 2) & 0xFFFF] != (shades[k] << 4 | shades[k + 1])) at = 0xFFFFFFFFu;
	for (k = 0; k < 8; k++) stamp[k] = (uint8_t)(t >> (8 * k));
	for (k = 0; k < 8; k++) stamp[8 + k] = (uint8_t)(pic >> (8 * k));
	for (k = 0; k < 4; k++) stamp[16 + k] = (uint8_t)(at >> (8 * k));
	fwrite(stamp, 1, 20, disp_log);
	fwrite(shades, 1, DMD_W * DMD_H, disp_log);
}

/* test hook PINHECK_PROP_SERIAL: the raw DMD game's Propeller serial output on P30 (57,600 baud, 8N1, at its clock of
   104 MHz), one line per text line: its watchdog's status */
#define PINHECK_SER_PIN (1u << 30)
static FILE *ser_log;
static int ser_opened;
static struct { int level, bit; uint64_t edge, start; uint8_t byte; char line[256]; int n; } ser;

static void pinheck_ser_pins(uint64_t t, uint32_t out, uint32_t dir)
{
	const uint64_t bit = 104000000u / 57600u;
	int level = !(dir & PINHECK_SER_PIN) || (out & PINHECK_SER_PIN);
	/* the data bits whose centres passed since the last edge had the old level */
	while (ser.bit >= 0 && ser.bit < 8 && ser.start + bit * (uint64_t)(2 * ser.bit + 3) / 2 < t) {
		if (ser.level) ser.byte |= (uint8_t)(1u << ser.bit);
		if (++ser.bit == 8) {
			if (ser.byte == '\n' || ser.byte == '\r' || ser.n == (int)sizeof(ser.line) - 1) {
				if (ser.n) { ser.line[ser.n] = 0; fprintf(ser_log, "%.6f %s\n", t / 104e6, ser.line); }
				ser.n = 0;
			} else if (ser.byte >= 32 && ser.byte < 127) ser.line[ser.n++] = (char)ser.byte;
			ser.bit = -1;
		}
	}
	if (level == ser.level) return;
	if (!level && ser.bit < 0 && t - ser.edge >= bit) { ser.start = t; ser.bit = 0; ser.byte = 0; }
	ser.level = level;
	ser.edge = t;
}

static void pinheck_dmd_pins_cb(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx;
	if (ser_log) pinheck_ser_pins(t, out, dir);
	pinheck_dmd_pins(&dmd, t, out, dir);
	if (dmd_proof) pinheck_dmd_pins(&dmd_row, t, out, dir);
}

/* test hook PINHECK_DMD_LOG: at each vblank, the number of subframes the decoder queued since the last (uint32) and
   those subframes, as the core is to get them */
static FILE *dmd_log;
static int dmd_log_opened;

/* at each vblank: the subframes the Propeller drew since the last one */
static void pinheck_dmd_vblank(void)
{
	uint32_t n;
	unsigned k;
	prop_sync(&prop);
	n = dmd_head - dmd_tail;
	if (dmd_log) {
		fwrite(&n, 4, 1, dmd_log);
		for (k = dmd_tail; k != dmd_head; k++) fwrite(dmd_ring[k % PINHECK_DMD_RING], 1, DMD_SUB, dmd_log);
	}
	for (; dmd_tail != dmd_head; dmd_tail++) core_dmd_submit_frame(core_gameData->lcdLayout, dmd_ring[dmd_tail % PINHECK_DMD_RING], 1);
}

static void pinheck_dmd_stop(void)
{
	char msg[160];
	if (dmd_proof) {
		sprintf(msg, "dmd: proof: %lu subframes, the row model differs in %lu (%lu rows)", dmd_subs, dmd_differ, dmd_rows);
		if (dmd_differ) sprintf(msg + strlen(msg), ", the first at Propeller cycle %llu", (unsigned long long)dmd_first);
		pinheck_prop_log(NULL, msg);
	}
	if (dmd_dropped) {
		sprintf(msg, "dmd: %u subframes dropped between vblanks", dmd_dropped);
		pinheck_prop_log(NULL, msg);
	}
	dmd_subs = dmd_differ = dmd_rows = 0;
	dmd_dropped = 0;
	if (dmd_log) fclose(dmd_log);
	if (ser_log) fclose(ser_log);
	dmd_log = ser_log = NULL;
}

static void pinheck_disp_init(void)
{
'''),
    (r'''	disp_log = path ? fopen(path, disp_opened ? "ab" : "wb") : NULL;
	disp_opened = 1;
	prop_set_pins(&prop, pinheck_disp_pins, NULL);
	prop_set_pins_mask(&prop, DISPLAY_P17 | DISPLAY_P20 | DISPLAY_P21 | DISPLAY_P22);
''',
     r'''	disp_log = path ? fopen(path, disp_opened ? "ab" : "wb") : NULL;
	disp_opened = 1;
	dmd_on = pinheck_game()->dmdHub != 0;
	if (dmd_on) {
		dmd_proof = getenv("PINHECK_DMD_PROOF") != NULL;
		if (!dmd_log && getenv("PINHECK_DMD_LOG")) dmd_log = fopen(getenv("PINHECK_DMD_LOG"), dmd_log_opened++ ? "ab" : "wb");
		prop_set_pins(&prop, pinheck_dmd_pins_cb, NULL);
		if (!ser_log && getenv("PINHECK_PROP_SERIAL")) ser_log = fopen(getenv("PINHECK_PROP_SERIAL"), ser_opened++ ? "a" : "w");
		memset(&ser, 0, sizeof(ser));
		ser.level = 1;
		ser.bit = -1;
		prop_set_pins_mask(&prop, DMD_ALL_PINS | (ser_log ? PINHECK_SER_PIN : 0));
		return;
	}
	prop_set_pins(&prop, pinheck_disp_pins, NULL);
	prop_set_pins_mask(&prop, DISPLAY_P17 | DISPLAY_P20 | DISPLAY_P21 | DISPLAY_P22);
'''),
    (r'''static void pinheck_disp_reset(void)
{
	pinheck_display_init(&disp, NULL, pinheck_disp_frame, pinheck_disp_config, pinheck_prop_log);
	pinheck_display_size(&disp, pinheck_game()->width, pinheck_game()->height); /* any other size is named at start */
''',
     r'''static void pinheck_disp_reset(void)
{
	if (dmd_on) {
		pinheck_dmd_init(&dmd, 1, NULL, 0, NULL, pinheck_dmd_sub, disp_log ? pinheck_dmd_frame : NULL);
		pinheck_dmd_init(&dmd_row, 0, prop.chip.hub, (uint32_t)pinheck_game()->dmdHub, NULL, pinheck_dmd_row_sub, NULL);
		dmd_head = dmd_tail = 0;
		return;
	}
	pinheck_display_init(&disp, NULL, pinheck_disp_frame, pinheck_disp_config, pinheck_prop_log);
	pinheck_display_size(&disp, pinheck_game()->width, pinheck_game()->height); /* any other size is named at start */
'''),
    (r'''	if (locals.idle && timer_get_time() >= PINHECK_REFUSE_SECS) mame_schedule_error_exit();
	if (!locals.idle) pinheck_brd_vblank();
	core_updateSw(0);
}
''',
     r'''	if (locals.idle && timer_get_time() >= PINHECK_REFUSE_SECS) mame_schedule_error_exit();
	if (!locals.idle) pinheck_brd_vblank();
	if (!locals.idle && dmd_on) pinheck_dmd_vblank();
	core_updateSw(0);
}
'''),
    (r'''	opened = 1;
	locals.reset_at = !reset_done && getenv("PINHECK_RESET_AT") ? atof(getenv("PINHECK_RESET_AT")) : 0.0;
	if (pinheck_system_only()) {
		locals.idle = 1;
''',
     r'''	opened = 1;
	locals.reset_at = !reset_done && getenv("PINHECK_RESET_AT") ? atof(getenv("PINHECK_RESET_AT")) : 0.0;
	/* the core draws a CORE_DMD layout from its PWM integration, also while the machine refuses to run */
	if (pinheck_game()->dmdHub) core_dmd_pwm_init(core_gameData->lcdLayout, CORE_DMD_PWM_FILTER_PINHECK_16, CORE_DMD_PWM_COMBINER_SUM_16, 0);
	if (pinheck_system_only()) {
		locals.idle = 1;
'''),
    (r'''	prop.chip.jit_build = NULL;
#endif
	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
''',
     r'''	prop.chip.jit_build = NULL;
#endif
	if (!locals.idle && dmd_on) pinheck_dmd_stop();
	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
'''),
    (r'''	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_RGB_DIRECT)
MACHINE_DRIVER_END
''',
     r'''	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_RGB_DIRECT)
MACHINE_DRIVER_END

/* a raw DMD (game data dmdHub): the core draws the CORE_DMD layout with its palette pens */
MACHINE_DRIVER_START(PINHECKDMD)
	MDRV_IMPORT_FROM(PINHECK)
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
MACHINE_DRIVER_END
'''),
])
edit('src/wpc/pinheck.h', [
    (r'''extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK''',
     r'''extern MACHINE_DRIVER_EXTERN(PINHECK);
extern MACHINE_DRIVER_EXTERN(PINHECKDMD);
#define gl_mPINHECK PINHECK
#define gl_mPINHECKDMD PINHECKDMD'''),
])
edit('src/wpc/core.c', [
    (r'''    }
    break;
  default:
    assert(0); // Unsupported filter
''',
     r'''    }
    break;
  case CORE_DMD_PWM_FILTER_PINHECK_16: // pinHeck raw DMD: 16 subframes of one level each (a dot of value v is lit in v of them), about 785 Hz
    {
      static const UINT32 fir_box_16[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }; // any 16 consecutive subframes hold each level once
      dmd_state->fir_weights = fir_box_16;
      dmd_state->fir_size = dmd_state->nFrames = sizeof(fir_box_16) / sizeof(UINT32);
    }
    break;
  default:
    assert(0); // Unsupported filter
'''),
    (r'''    }
    break;
  default:
    assert(0); // Unsupported combiner
''',
     r'''    }
    break;
  case CORE_DMD_PWM_COMBINER_SUM_16: // Sum of the last 16 frames (pinHeck raw DMD: one subframe per level, the level-15 one always dark, so 0-15)
    {
      assert((dmd_state->width & 7) == 0 && dmd_state->nFrames >= 16);
      memset(dmd_state->tempRawFrame, 0, dmd_state->frameSize);
      for (int i = 1; i <= 16; i++) {
        UINT8* rawData = dmd_state->tempRawFrame;
        const UINT8* frameData = dmd_state->rawFrames + ((nf + (dmd_state->nFrames - i)) % dmd_state->nFrames) * dmd_state->rawFrameSize;
        for (int kk = 0; kk < dmd_state->rawFrameSize; kk++)
          for (UINT8 ll = 0, data = *frameData++; ll < 8; ll++, data <<= 1)
            (*rawData++) += (data >> 7);
      }
      for (int kk = 0; kk < dmd_state->frameSize; kk++)
        if (dmd_state->tempRawFrame[kk] > 15) dmd_state->tempRawFrame[kk] = 15;
    }
    break;
  default:
    assert(0); // Unsupported combiner
'''),
])
edit('src/wpc/core.h', [
    (r'''#define CORE_DMD_PWM_FILTER_CAPCOM_128x32        8
#define CORE_DMD_PWM_FILTER_CAPCOM_256x64        9

// note that the following are only used to support the pre-PWM legacy path (e.g. for old colorizations)
''',
     r'''#define CORE_DMD_PWM_FILTER_CAPCOM_128x32        8
#define CORE_DMD_PWM_FILTER_CAPCOM_256x64        9
#define CORE_DMD_PWM_FILTER_PINHECK_16           10

// note that the following are only used to support the pre-PWM legacy path (e.g. for old colorizations)
'''),
    (r'''#define CORE_DMD_PWM_COMBINER_SUM_1_2_1          8
#define CORE_DMD_PWM_COMBINER_1                  9

#define CORE_DMD_PWM_PREINTEGRATED_LINEAR_4  0x100
''',
     r'''#define CORE_DMD_PWM_COMBINER_SUM_1_2_1          8
#define CORE_DMD_PWM_COMBINER_1                  9
#define CORE_DMD_PWM_COMBINER_SUM_16             10

#define CORE_DMD_PWM_PREINTEGRATED_LINEAR_4  0x100
'''),
])
edit('tests/pinheck/link/register_build.py', [
    (r'''     'DRVLIBS += $(PINOBJ)/pinheck/hexload.o\n'),
]''',
     r'''     'DRVLIBS += $(PINOBJ)/pinheck/hexload.o\n'),
    (['src/wpc/pinheck/dmd.c', 'src/wpc/pinheck/dmd.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/dmd.o\n'),
]'''),
])
EOF
```

Then: `python3 tests/pinheck/link/register_build.py`
Expected: 19 `updated …` lines; a second run prints nothing.

- [ ] **Step 4: Build, and Domino's, Rob Zombie and The Jetsons are byte-identical**

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11c.log 2>&1; tail -1 build/sdl3pinmame/11c.log
grep -i warning build/sdl3pinmame/11c.log | grep -cE 'pinheck|dmd\.c|core\.c'
REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video | grep determinism
rz env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract | grep determinism
jet env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract | grep determinism
```
Expected (about 15 minutes):
```text
[100%] Built target sdl3pinmame
0
determinism first boot: identical nvram.base/dominos.nv
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
determinism first boot: identical nvram.base/rzspook.nv
determinism attract: identical uart.log frames.bin snd.wav nvram/rzspook.nv
determinism first boot: identical nvram.base/jetsons.nv
determinism attract: identical uart.log frames.bin snd.wav nvram/jetsons.nv
```

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck/dmd.h src/wpc/pinheck/dmd.c src/wpc/core.h src/wpc/core.c src/wpc/pinheck.h src/wpc/pinheck.c tests/pinheck/display/dmd_test.c tests/pinheck/display/check.sh tests/pinheck/link/register_build.py src/pinmame.mak cmake vcproj
git commit -m "pinheck: the raw DMD scanned by a Propeller cog (subframes from the pins, 16 shades through the core's PWM path)"
```

### Task 5: The `amh` game and its boot check

**Files:**
- Create: `src/wpc/sims/pinheck/amh.c`
- Modify: `tests/pinheck/games.sh`, `tests/pinheck/pic32mx/pinmame_check.sh`, `tests/pinheck/pic32mx/romset_check.sh`, `src/wpc/pinheck.c`, `src/wpc/pinheckgames.c`, `src/wpc/driver.c`, `tests/pinheck/link/register_build.py`; regenerated build files

**Interfaces:**
- Consumes: Tasks 2–4.
- Produces: the game `amh` (clone of `pinheck`; `amhGameData` = `128, 32, 0, 544, 2400, 0, 0, 0x5B0C, 5000`; `gameSpecific1` 23); `games.sh` `amh` and `UPDATE_END=none`; on-board RGB LED 2 on solenoids 62–64.

- [ ] **Step 1: The checks' game table and the boot check for a game without an update**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/games.sh', [
    (r'''# The game a machine check runs, sourced by the checks: PINHECK_GAME (default dominos), whose romset is
# PINHECK_ZIP and whose unzipped update is PINHECK_UPDATE_DIR.
#   PRG         the PIC32 image in the update
#   PRP         the Propeller image in the update
#   PROGRAMMED  bytes the first-boot update flashes
#   UPDATE_END  how the update hands the PIC32 back: leave (STK500v2 LEAVE_PROGMODE), or stopped in
#               programming mode (the Propeller asks for a restart and the PIC32 waits for it)
#   REBOOTS     Propeller reboots (CLKSET $80) before the first PIC32 sync of a normal start. The Jetsons' 1
#               follows from the stand-in bootloader's 3 s hold: any hold under 2.59 s gives 0, Domino's needs
#               over 1.7 s, and the real bootloader's timeout is not known
#   UPDATED_AT  emulated seconds by which the Propeller shows PLEASE RESTART on a first boot
#   STORED      the version word the firmware prints after the sync check (version << 24 | $BAFA, hex)
#   BANNER      the UART1 banner lines checked (| separated)
#   CLIP        a .VID clip the display check plays ([V00<CLIP>])
#   LOOK        the module's look: menu (the 128x32 module's 14-byte config packets), or none (The Jetsons'
#               128x64 module: 12-byte packets, drawn as sent)
#   SIM         the simulator check (tests/pinheck/sim)
#   TROUGH1     the switch a ball in trough 1 (the eject position) closes
''',
     r'''# The game a machine check runs, sourced by the checks: PINHECK_GAME (default dominos), whose romset is
# PINHECK_ZIP and whose unzipped update is PINHECK_UPDATE_DIR.
#   PRG         the PIC32 image in the update (America's Most Haunted: the Intel HEX next to the SD card's files)
#   PRP         the Propeller image in the update
#   PROGRAMMED  bytes the first-boot update flashes (none: the romset's Intel HEX is converted at start)
#   UPDATE_END  how the update hands the PIC32 back: leave (STK500v2 LEAVE_PROGMODE), or stopped in
#               programming mode (the Propeller asks for a restart and the PIC32 waits for it); none: no update
#   REBOOTS     Propeller reboots (CLKSET $80) before the first PIC32 sync of a normal start. The Jetsons' 1
#               follows from the stand-in bootloader's 3 s hold: any hold under 2.59 s gives 0, Domino's needs
#               over 1.7 s, and the real bootloader's timeout is not known
#   UPDATED_AT  emulated seconds by which the Propeller shows PLEASE RESTART on a first boot
#   STORED      the version word the firmware prints after the sync check (version << 24 | $BAFA, hex); with no
#               update, the word the firmware stores at EEPROM $8000 on a blank NVRAM
#   BANNER      the UART1 banner lines checked (| separated)
#   CLIP        a .VID clip the display check plays ([V00<CLIP>])
#   LOOK        the module's look: menu (the 128x32 module's 14-byte config packets), none (The Jetsons'
#               128x64 module: 12-byte packets, drawn as sent), or dmd (a raw DMD scanned by a Propeller cog)
#   SIM         the simulator check (tests/pinheck/sim)
#   TROUGH1     the switch a ball in trough 1 (the eject position) closes
'''),
    (r'''jetsons) PRG=JET_V004.PRG PRP=PRP_V002.BIN PROGRAMMED=197632 UPDATE_END='stopped in programming mode' REBOOTS=1 UPDATED_AT=104 STORED=400BAFA LOOK=none CLIP=TL1 SIM=jetsim.py TROUGH1=92
	BANNER='pinHeck System 2011-2016|Game: JET - JETSONS' ;;
*) echo "PINHECK_GAME: no machine checks for '$GAME'"; exit 2 ;;
esac
''',
     r'''jetsons) PRG=JET_V004.PRG PRP=PRP_V002.BIN PROGRAMMED=197632 UPDATE_END='stopped in programming mode' REBOOTS=1 UPDATED_AT=104 STORED=400BAFA LOOK=none CLIP=TL1 SIM=jetsim.py TROUGH1=92
	BANNER='pinHeck System 2011-2016|Game: JET - JETSONS' ;;
amh) PRG=AMH_V023.hex PRP=DMD/PROP_023.BIN PROGRAMMED=0 UPDATE_END=none REBOOTS=0 UPDATED_AT=0 STORED=1700BAFA LOOK=dmd CLIP=AB1 SIM=amhsim.py TROUGH1=84
	BANNER='pinHeck System 2011-2016|Game:AMH - SPOOKY PIN|Version:023' ;;
*) echo "PINHECK_GAME: no machine checks for '$GAME'"; exit 2 ;;
esac
'''),
])
edit('tests/pinheck/pic32mx/pinmame_check.sh', [
    (r'''		-headless -frames_to_run $2 -skip_gamewarnings -nothrottle > run$1.out 2>&1) || { echo "PINMAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
# the first boot flashes the update; the Propeller then asks for a restart, which comes 12 s later
PINHECK_RESET_AT=$UPDATED_AT launch 1 $(((UPDATED_AT + 12) * 60))
PINHECK_UART1_SEND_AT=11.5 PINHECK_UART1_SEND='[E97000]' launch 2 900
fail=0
for want in "boot: sign-on 0" "boot: $UPDATE_END, $PROGRAMMED bytes programmed" "prop: CLKSET 6f"; do
	grep -qF "$want" $B/prop1.log || { echo "PINMAME FAIL: launch 1 lacks '$want'"; fail=1; }
''',
     r'''		-headless -frames_to_run $2 -skip_gamewarnings -nothrottle > run$1.out 2>&1) || { echo "PINMAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
fail=0
if [ "$UPDATE_END" = none ]; then
	# no update: the driver programs flash from the romset's Intel HEX at every start; on the blank NVRAM the firmware
	# stores its defaults and the version word itself, and the next start keeps them
	launch 1 900
	PINHECK_PROP_SERIAL=$PWD/$B/ser2.log PINHECK_UART1_SEND_AT=11.5 PINHECK_UART1_SEND='[E97000]' launch 2 900
	for n in 1 2; do
		grep -q "^hex: [1-9][0-9]* bytes of program flash$" $B/prop$n.log || { echo "PINMAME FAIL: launch $n did not program flash from $PRG"; fail=1; }
		grep -q "boot: sign-on" $B/prop$n.log && { echo "PINMAME FAIL: launch $n ran an update"; fail=1; }
		k=$(grep -c "prop: CLKSET 80" $B/prop$n.log)
		[ "$k" = "$REBOOTS" ] || { echo "PINMAME FAIL: the Propeller rebooted $k times in launch $n, expected $REBOOTS"; fail=1; }
	done
	grep -aq "New code detected, storing new game defaults... done!" $B/uart1.log || { echo "PINMAME FAIL: launch 1 did not store the defaults"; fail=1; }
	grep -aq "New code detected" $B/uart2.log && { echo "PINMAME FAIL: launch 2 stored the defaults again"; fail=1; }
	python3 banner.py uart $B/uart1.log "$BANNER" || fail=1
	python3 banner.py uart $B/uart2.log "$BANNER|Ball Search: DISABLED" || fail=1
	# the Propeller's watchdog line (P30): its display leaves the start screen (GR 10) for attract mode only when the
	# PIC32's first commands come after the Propeller's main loop started (game data bootHold)
	grep -q "GR:" $B/ser2.log && grep "GR:" $B/ser2.log | grep -qv "GR:10D" || { echo "PINMAME FAIL: the Propeller's display never left its start screen (GR:10)"; fail=1; }
	# the NVRAM holds U13 (128 KB, then the core's mech and DIP bytes) and the Propeller EEPROM from $8000, whose
	# first long is the version word
	python3 -c "import struct, sys; d = open(sys.argv[1], 'rb').read(); i = d.find(struct.pack('<I', int(sys.argv[2], 16)), 131072); sys.exit(not 131072 < i < 131200)" \
		$B/nvram/$GAME.nv $STORED || { echo "PINMAME FAIL: the NVRAM's Propeller EEPROM does not start with the version word $STORED"; fail=1; }
	if [ $fail -ne 0 ]; then cat -v $B/uart2.log | tail -20; exit 1; fi
	./romset_check.sh || exit 1
	echo "pinmame: ok"
	exit 0
fi
# the first boot flashes the update; the Propeller then asks for a restart, which comes 12 s later
PINHECK_RESET_AT=$UPDATED_AT launch 1 $(((UPDATED_AT + 12) * 60))
PINHECK_UART1_SEND_AT=11.5 PINHECK_UART1_SEND='[E97000]' launch 2 900
for want in "boot: sign-on 0" "boot: $UPDATE_END, $PROGRAMMED bytes programmed" "prop: CLKSET 6f"; do
	grep -qF "$want" $B/prop1.log || { echo "PINMAME FAIL: launch 1 lacks '$want'"; fail=1; }
'''),
])
edit('tests/pinheck/pic32mx/romset_check.sh', [
    (r'''rm -rf $B && mkdir -p $B/bios $B/nomedia $B/nvram $B/cfg || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j bios/pinheck.zip p8x32a.rom && cp bios/pinheck.zip nomedia/ && rm p8x32a.rom) || exit 2
(cd "$PINHECK_UPDATE_DIR" && zip -q -0 "$OLDPWD/$B/nomedia/$GAME.zip" $PRG $PRP) || exit 2
fail=0
run() {
''',
     r'''rm -rf $B && mkdir -p $B/bios $B/nomedia $B/nvram $B/cfg || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j bios/pinheck.zip p8x32a.rom && cp bios/pinheck.zip nomedia/ && rm p8x32a.rom) || exit 2
(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -j "$OLDPWD/$B/nomedia/$GAME.zip" $PRG $PRP) || exit 2
fail=0
run() {
'''),
    (r'''	echo "ROMSET FAIL: $GAME without media exited $rc"; tail -3 $B/nomedia.out; fail=1
fi
# the driver supports everything the game's data asks for
if grep -F "not supported" $B/nomedia.out; then
''',
     r'''	echo "ROMSET FAIL: $GAME without media exited $rc"; tail -3 $B/nomedia.out; fail=1
fi
# an Intel HEX that does not convert (one data digit changed: the record's checksum fails) stops the game
case $PRG in *.hex)
	mkdir -p $B/badhex && cp $B/bios/pinheck.zip $B/badhex/ &&
		sed '2s/^\(:..........\)./\1x/' "$PINHECK_UPDATE_DIR/$PRG" | tr x 7 > $B/$PRG &&
		(cd $B && zip -q -0 -j badhex/$GAME.zip $PRG "$PINHECK_UPDATE_DIR/$PRP" && rm $PRG) || exit 2
	rc=$(run $GAME badhex badhex)
	if [ "$rc" -eq 0 ] || [ "$rc" -ge 124 ] || ! grep -qF "pinheck: $GAME: the PIC32 image (Intel HEX) does not convert: line 2: checksum" $B/badhex.out; then
		echo "ROMSET FAIL: $GAME with a corrupt Intel HEX exited $rc, want the conversion error and an error exit"; tail -3 $B/badhex.out; fail=1
	fi ;;
esac
# the driver supports everything the game's data asks for
if grep -F "not supported" $B/nomedia.out; then
'''),
])
EOF
```

Run: `amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh | head -1`
Expected: `PINMAME FAIL: launch 1 exited 1` (no `amh` yet).

- [ ] **Step 2: The game and its simulator**

`src/wpc/sims/pinheck/amh.c`:

```c
// license:BSD-3-Clause

/*******************************************************************************
 America's Most Haunted (Spooky Pinball, 2014): game definition and playfield
 simulator.

 The PIC32 image is AMH_V023.hex (Intel HEX, outside the SD card); the driver
 programs flash from it at start. The display is a raw 128x32 DMD scanned by a
 Propeller cog (game data dmdHub).
 The simulator models the mechanics the rules depend on: the 4-ball trough with
 the drain kicker and the ball loader, the shooter lane (autoplunger or manual
 plunger), the basement scoop, the Spooky Door (servo 1) with the VUK behind it,
 the Hellevator car (servo 0), the ghost loop with its opto and magnet, and the
 flippers' end-of-stroke switches. Coils are read as "on at some time during the
 last frame" (coreGlobals.solenoids), so a pulse of a few milliseconds is never
 missed.
 ******************************************************************************/

/*------------------------------------------------------------------------------
  Keys (L/R Ctrl selects the left or right one of a pair):
    +-  L/R Slingshot        +I  L/R Inlane         +O  L/R Outlane (drain)
    +B  L/R Pop Bumper (0/1)  B  Pop Bumper 2       +R  Upper/Lower Left Orbit
    +N  Basement Upper/Lower  S  Basement Scoop      D  Spooky Door (the VUK while the door is open)
     E  Hellevator car        G  Ghost Loop          H  Hotel Path
     V  Elevator Call Button  J  Balcony Jump        F  Balcony Jump that falls short (Pop Path)
     W/K/M  Wiki, Tech, Psychic
     Z/X/C  Ghost Targets 1-3                         T/Y/U  "O", "R", "B" rollovers
     Q  Drain between the flippers                    Space  Plunger (hold, release)
  The keys move a ball on the playfield (autoBall); Up/Down still select one.
------------------------------------------------------------------------------*/

#include "driver.h"
#include "core.h"
#include "sim.h"
#include "sndbrd.h"
#include "pinheck.h"

PINHECK_INPUT_PORTS_START(amh, 4)
  PORT_START /* 0 */
    COREPORT_BIT(0x0001, "Left Qualifier",   KEYCODE_LCONTROL)
    COREPORT_BIT(0x0002, "Right Qualifier",  KEYCODE_RCONTROL)
    COREPORT_BIT(0x0004, "L/R Slingshot",    KEYCODE_MINUS)
    COREPORT_BIT(0x0008, "L/R Inlane",       KEYCODE_I)
    COREPORT_BIT(0x0010, "L/R Outlane",      KEYCODE_O)
    COREPORT_BIT(0x0020, "U/L Left Orbit",   KEYCODE_R)
    COREPORT_BIT(0x0040, "Pop Bumpers",      KEYCODE_B)
    COREPORT_BIT(0x0080, "Basement Scoop",   KEYCODE_S)
    COREPORT_BIT(0x0100, "Spooky Door",      KEYCODE_D)
    COREPORT_BIT(0x0200, "Hellevator",       KEYCODE_E)
    COREPORT_BIT(0x0400, "Ghost Loop",       KEYCODE_G)
    COREPORT_BIT(0x0800, "Hotel Path",       KEYCODE_H)
    COREPORT_BIT(0x1000, "Elevator Call",    KEYCODE_V)
    COREPORT_BIT(0x2000, "Drain",            KEYCODE_Q)
    COREPORT_BIT(0x4000, "U/L Basement",     KEYCODE_N)
  PORT_START /* 1 */
    COREPORT_BIT(0x0001, "Balcony Jump",     KEYCODE_J)
    COREPORT_BIT(0x0002, "Balcony Fail",     KEYCODE_F)
    COREPORT_BIT(0x0008, "Wiki",             KEYCODE_W)
    COREPORT_BIT(0x0010, "Tech",             KEYCODE_K)
    COREPORT_BIT(0x0020, "Psychic",          KEYCODE_M)
    COREPORT_BIT(0x0040, "Ghost Target 1",   KEYCODE_Z)
    COREPORT_BIT(0x0080, "Ghost Target 2",   KEYCODE_X)
    COREPORT_BIT(0x0100, "Ghost Target 3",   KEYCODE_C)
    COREPORT_BIT(0x0200, "O Rollover",       KEYCODE_T)
    COREPORT_BIT(0x0400, "R Rollover",       KEYCODE_Y)
    COREPORT_BIT(0x0800, "B Rollover",       KEYCODE_U)
PINHECK_INPUT_PORTS_END

/* switches: document switch n (0-63) is (n/8+1)*10 + n%8+1; the optos are cabinet inputs 13 and 14 */
#define swWiki       31
#define swTech       32
#define swGhost1     33
#define swGhost2     34
#define swGhost3     35
#define swScoop      37
#define swVUK        38
#define swHotel      45
#define swCall       46
#define swPsychic    47
#define swJumpMade   51
#define swJumpApp    52
#define swPopPath    53
#define swBaseUpper  54
#define swBaseLower  55
#define swPop0       56
#define swULOrbit    57
#define swLLOrbit    58
#define swRollO      61
#define swRollR      62
#define swRollB      63
#define swCar        64
#define swPop2       66
#define swPop1       67
#define swLOutlane   71
#define swLInlane    72
#define swLSling     73
#define swLFlipEOS   74
#define swRFlipEOS   75
#define swRSling     76
#define swRInlane    77
#define swROutlane   78
#define swShooter    82
#define swTrough1    84
#define swTrough2    85
#define swTrough3    86
#define swTrough4    87
#define swDrain      88
#define swLoopOpto   95
#define swDoorOpto   96

/* solenoids: document coil n is PinMAME solenoid n+1 */
#define sMagnet      1
#define sScoop       11
#define sVUK         12
#define sRFlipHigh   17
#define sRFlipHold   18
#define sLFlipHigh   19
#define sLFlipHold   20
#define sLoad        21
#define sDrainKick   22
#define sLaunch      23

/* servos (pulse widths in us as the firmware's servo test sends them): see amh_handleMech */
#define DOOR_OPEN_US  1000  /* DOOR OPEN 5 degrees (593 us), DOOR CLOSE 90 (1,476 us) */
#define CAR_DOWN_US   1400  /* HELL DOWN 10 degrees (647 us), HELL UP 160 (2,200 us) */

static struct {
  int since[25];  /* frames since each coil was last on */
  int us[5];      /* each servo's last pulse width in us, 0 = none yet */
  int ride;       /* frames the Hellevator car has been up with a ball in it */
  int eos[2];     /* left and right flipper end-of-stroke switches as last set, -1 = not yet */
} locals;

static int sol(int n) { return (coreGlobals.solenoids >> (n - 1)) & 1; }
static int doorOpen(void) { return locals.us[1] && locals.us[1] < DOOR_OPEN_US; }
static int carDown(void) { return !locals.us[0] || locals.us[0] < CAR_DOWN_US; }

enum { stTrough4 = SIM_FIRSTSTATE, stTrough3, stTrough2, stTrough1, stShooter, stLaunched, stDrain, stDrainHole,
       stLOutlane, stROutlane, stLInlane, stRInlane, stLSling, stRSling,
       stScoop, stDoor, stDoorHit, stVUK, stCar, stLoop, stMagnet,
       stULOrbit, stLLOrbit, stHotel, stCall, stJumpApp, stJumpMade, stJumpShort, stJumpFail, stBaseUpper, stBaseLower,
       stPop0, stPop1, stPop2, stWiki, stTech, stPsychic, stGhost1, stGhost2, stGhost3, stRollO, stRollR, stRollB };

static sim_tState amh_stateDef[] = {
  {"Not Installed", 0, 0,           0, stDrain,  0, 0, 0, SIM_STNOTEXCL},
  {"Moving"},
  {"Playfield",     0, 0,           0, 0,        0, 0, 0, SIM_STNOTEXCL},

  {"Trough 4",      1, swTrough4,   0, stTrough3, 3},
  {"Trough 3",      1, swTrough3,   0, stTrough2, 3},
  {"Trough 2",      1, swTrough2,   0, stTrough1, 3},
  {"Trough 1",      1, swTrough1,   0, 0,         0},
  {"Shooter Lane",  1, swShooter,   0, 0,         0},
  {"Launched",      1, 0,           0, stFree,   10},
  {"Drain",         1, 0,           0, stDrainHole, 1, 0, 0, SIM_STNOTEXCL},
  {"Drain Hole",    1, swDrain,     0, 0,         0},

  {"Left Outlane",  1, swLOutlane,  0, stDrain,  10},
  {"Right Outlane", 1, swROutlane,  0, stDrain,  10},
  {"Left Inlane",   1, swLInlane,   0, stFree,    5},
  {"Right Inlane",  1, swRInlane,   0, stFree,    5},
  {"Left Sling",    1, swLSling,    0, stFree,    2},
  {"Right Sling",   1, swRSling,    0, stFree,    2},

  {"Basement Scoop",1, swScoop,     0, 0,         0},
  {"Spooky Door",   1, 0,           0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"Door Hit",      1, swDoorOpto,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"VUK",           1, swVUK,       0, 0,         0},
  {"Hellevator",    1, swCar,       0, 0,         0},
  {"Ghost Loop",    1, swLoopOpto,  0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"Magnet",        1, 0,           0, 0,         0},
  {"Upper L Orbit", 1, swULOrbit,   0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Lower L Orbit", 1, swLLOrbit,   0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Hotel Path",    1, swHotel,     0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Elevator Call", 1, swCall,      0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Jump Approach", 1, swJumpApp,   0, stJumpMade, 5, 0, 0, SIM_STNOTEXCL},
  {"Jump Made",     1, swJumpMade,  0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Jump Short",    1, swJumpApp,   0, stJumpFail, 5, 0, 0, SIM_STNOTEXCL},
  {"Jump Fail",     1, swPopPath,   0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Basement Upper",1, swBaseUpper, 0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Basement Lower",1, swBaseLower, 0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Pop Bumper 0",  1, swPop0,      0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Pop Bumper 1",  1, swPop1,      0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Pop Bumper 2",  1, swPop2,      0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Wiki",          1, swWiki,      0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Tech",          1, swTech,      0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Psychic",       1, swPsychic,   0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Ghost Target 1",1, swGhost1,    0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Ghost Target 2",1, swGhost2,    0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Ghost Target 3",1, swGhost3,    0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"O Rollover",    1, swRollO,     0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"R Rollover",    1, swRollR,     0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"B Rollover",    1, swRollB,     0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {0}
};

/* frames: the ball reaches the magnet 6 frames after the loop opto and is caught if the magnet came on within 12 */
#define MAGNET_CATCH 12
#define MAGNET_DROP  6
#define CAR_RIDE     30  /* frames the car takes from the bottom to the top */

static int amh_handleBallState(sim_tBallStatus *ball, int *inports) {
  (void)inports;
  switch (ball->state) {
    case stTrough1:  if (sol(sLoad) && !core_getSw(swShooter)) return setState(stShooter, 5); break; /* a full lane loses the pulse */
    case stShooter:  if (sol(sLaunch) || sim_getSol(sShooterRel)) return setState(stLaunched, 2); break;
    case stDrainHole: if (sol(sDrainKick)) return setState(stTrough4, 3); break;
    case stScoop:    if (sol(sScoop)) return setState(stFree, 5); break;
    case stDoor:     return setState(doorOpen() ? stVUK : stDoorHit, 3);
    case stVUK:      if (sol(sVUK)) return setState(stFree, 10); break;
    case stCar:      /* the ball rides up in the car and rolls out at the top */
      if (carDown()) locals.ride = 0;
      else if (++locals.ride >= CAR_RIDE) return setState(stFree, 5);
      break;
    case stLoop:     return setState(locals.since[sMagnet] < MAGNET_CATCH ? stMagnet : stFree, 5);
    case stMagnet:   if (locals.since[sMagnet] > MAGNET_DROP) return setState(stFree, 5); break;
  }
  return 0;
}

/* HandleMechanics bit 0: the flippers' end-of-stroke switches, closed while a coil of their flipper is on.
   The servos are tracked from their pulses. */
static void amh_handleMech(int mech) {
  static const int coils[2][3] = { { sLFlipHigh, sLFlipHold, swLFlipEOS }, { sRFlipHigh, sRFlipHold, swRFlipEOS } };
  int i;
  for (i = 1; i <= 24; i++)
    locals.since[i] = sol(i) ? 0 : locals.since[i] < 10000 ? locals.since[i] + 1 : 10000;
  for (i = 0; i < 5; i++)
    if (pinheck_servo(i)) locals.us[i] = pinheck_servo(i);
  if (mech & 0x01)
    for (i = 0; i < 2; i++) {
      const int on = sol(coils[i][0]) || sol(coils[i][1]);
      if (on != locals.eos[i]) core_setSw(coils[i][2], locals.eos[i] = on); /* only on change: the switch test may toggle it */
    }
}

/* getMech(n): servo n's last pulse width in us, 0 before its first pulse */
static int amh_getMech(int mechNo) {
  return mechNo >= 0 && mechNo < 5 ? locals.us[mechNo] : 0;
}

static sim_tInportData amh_inportData[] = {
  {0, 0x0005, stLSling},   {0, 0x0006, stRSling},
  {0, 0x0009, stLInlane},  {0, 0x000a, stRInlane},
  {0, 0x0011, stLOutlane}, {0, 0x0012, stROutlane},
  {0, 0x0021, stULOrbit},  {0, 0x0022, stLLOrbit},
  {0, 0x0041, stPop0},     {0, 0x0042, stPop1},     {0, 0x0040, stPop2},
  {0, 0x0080, stScoop},    {0, 0x0100, stDoor},     {0, 0x0200, stCar},
  {0, 0x0400, stLoop},     {0, 0x0800, stHotel},    {0, 0x1000, stCall},
  {0, 0x2000, stDrain},
  {0, 0x4001, stBaseUpper}, {0, 0x4002, stBaseLower},
  {1, 0x0001, stJumpApp},  {1, 0x0002, stJumpShort},
  {1, 0x0008, stWiki},     {1, 0x0010, stTech},     {1, 0x0020, stPsychic},
  {1, 0x0040, stGhost1},   {1, 0x0080, stGhost2},   {1, 0x0100, stGhost3},
  {1, 0x0200, stRollO},    {1, 0x0400, stRollR},    {1, 0x0800, stRollB},
  {0}
};

static sim_tSimData amhSimData = {
  2,                    /* 2 game specific input ports */
  amh_stateDef,
  amh_inportData,
  { stTrough1, stTrough2, stTrough3, stTrough4, stDrain, stDrain, stDrain },
  NULL,                 /* no init */
  amh_handleBallState,
  NULL,                 /* no static drawing */
  TRUE,                 /* manual plunger (Space) next to the autoplunger */
  NULL,                 /* no custom key conditions */
  TRUE                  /* autoBall: the keys move the selected ball, or the first free one; Up/Down select among free balls */
};

static core_tLCDLayout amh_disp[] = {
  {0, 0, 32, 128, CORE_DMD, NULL, NULL}, {0}
};

/* the raw 128x32 DMD (frame at hub $5B0C), servo levels 0-255 = 0.544-2.4 ms, WS2801 as sent (the ghost is on-board LED 2),
   no in-service record (the firmware stores its defaults itself), the application 5 s after a reset (its first
   commands need the Propeller's main loop, which starts 3.9 s after power-on) */
static pinheck_tGameData amhGameData = {
  { GEN_PINHECK, amh_disp,
    { FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, 23, 0,
      pinheck_getsol, amh_handleMech, amh_getMech },
    &amhSimData },
  128, 32, 0, 544, 2400, 0, 0, 0x5B0C, 5000
};

static void init_amh(void) {
  int i;
  core_gameData = &amhGameData.core;
  memset(&locals, 0, sizeof(locals));
  for (i = 0; i <= 24; i++) locals.since[i] = 10000;
  for (i = 0; i < 2; i++) locals.eos[i] = -1;
  pinheck_flash_hex();
}

PINHECK_HEX_ROMSTART(amh, "AMH_V023.hex", 705004, CRC(d5147386) SHA1(adbf469841e4aa5063fe7d7e8fb1d756d932d64d),
                     "PROP_023.BIN", CRC(bd5a99e8) SHA1(763e1e01c663cc8eace3dbe0689da894dc4cadec))
PINHECK_ROMEND
CORE_CLONEDEFNV(amh, pinheck, "America's Most Haunted", 2014, "Spooky Pinball", gl_mPINHECKDMD, GAME_NOT_WORKING)
```

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
    (r'''#define PINHECK_SOL_RGB 50  /* on-board RGB left R,G,B, right R,G,B: 51-56 */
#define PINHECK_SOL_SRV 56  /* servos 0-4: 57-61 */
#define PINHECK_SOL_EXT 61  /* external WS2801 LED 0 R,G,B: 62-64 */
#define PINHECK_EXT_LEDS 1  /* the firmware drives one external LED */
#define PINHECK_NSOLS   64
#define PINHECK_LAMP_ST 64  /* start button lamp: lamp 91 */
''',
     r'''#define PINHECK_SOL_RGB 50  /* on-board RGB left R,G,B, right R,G,B: 51-56 */
#define PINHECK_SOL_SRV 56  /* servos 0-4: 57-61 */
#define PINHECK_SOL_EXT 61  /* external WS2801 LED 0 R,G,B, or on-board LED 2 R,G,B: 62-64 */
#define PINHECK_EXT_LEDS 1  /* the firmware drives one external LED */
#define PINHECK_ONB_LEDS 3  /* on-board LEDs 0 and 1; a third (America's Most Haunted's ghost) on 62-64 */
#define PINHECK_NSOLS   64
#define PINHECK_LAMP_ST 64  /* start button lamp: lamp 91 */
'''),
    (r'''	(void)ctx;
	if (brd_log) fprintf(brd_log, "%.9f R %d %d %02x%02x%02x %llu\n", timer_get_time(), chain, led, r, g, b, (unsigned long long)t);
	if ((chain == BOARD_RGB_ONBOARD && led < 2) || (chain == BOARD_RGB_EXTERNAL && led < PINHECK_EXT_LEDS)) {
		int idx = chain == BOARD_RGB_ONBOARD ? PINHECK_SOL_RGB + 3 * led : PINHECK_SOL_EXT + 3 * led;
		pinheck_brd_level(idx, r);
		pinheck_brd_level(idx + 1, g);
''',
     r'''	(void)ctx;
	if (brd_log) fprintf(brd_log, "%.9f R %d %d %02x%02x%02x %llu\n", timer_get_time(), chain, led, r, g, b, (unsigned long long)t);
	if ((chain == BOARD_RGB_ONBOARD && led < PINHECK_ONB_LEDS) || (chain == BOARD_RGB_EXTERNAL && led < PINHECK_EXT_LEDS)) {
		int idx = chain == BOARD_RGB_ONBOARD && led < 2 ? PINHECK_SOL_RGB + 3 * led : PINHECK_SOL_EXT + 3 * (chain == BOARD_RGB_ONBOARD ? led - 2 : led);
		pinheck_brd_level(idx, r);
		pinheck_brd_level(idx + 1, g);
'''),
])
edit('src/wpc/pinheckgames.c', [
    (r'''/* Rob Zombie's Spookshow International: sims/pinheck/rzspook.c */
/* The Jetsons: sims/pinheck/jetsons.c */
''',
     r'''/* Rob Zombie's Spookshow International: sims/pinheck/rzspook.c */
/* The Jetsons: sims/pinheck/jetsons.c */
/* America's Most Haunted: sims/pinheck/amh.c */
'''),
])
edit('src/wpc/driver.c', [
    (r'''DRIVERNV(rzspook)       //pinHeck 2016 Rob Zombie's Spookshow International (V26)
DRIVERNV(jetsons)       //pinHeck 2017 The Jetsons (V4)

#endif /* DRIVER_RECURSIVE */
''',
     r'''DRIVERNV(rzspook)       //pinHeck 2016 Rob Zombie's Spookshow International (V26)
DRIVERNV(jetsons)       //pinHeck 2017 The Jetsons (V4)
DRIVERNV(amh)           //pinHeck 2014 America's Most Haunted (V23)

#endif /* DRIVER_RECURSIVE */
'''),
])
edit('tests/pinheck/link/register_build.py', [
    (r'''    ('src/wpc/sims/pinheck/rzspook.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/rzspook.o\n'),
    ('src/wpc/sims/pinheck/jetsons.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/jetsons.o\n'),
]
changed = []
''',
     r'''    ('src/wpc/sims/pinheck/rzspook.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/rzspook.o\n'),
    ('src/wpc/sims/pinheck/jetsons.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/jetsons.o\n'),
    ('src/wpc/sims/pinheck/amh.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/amh.o\n'),
]
changed = []
'''),
])
EOF
```

Then: `python3 tests/pinheck/link/register_build.py`
Expected: 19 `updated …` lines; a second run prints nothing.

- [ ] **Step 3: Build**

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11c.log 2>&1; tail -1 build/sdl3pinmame/11c.log
grep -i warning build/sdl3pinmame/11c.log | grep -cE 'amh|pinheck'
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 4: The four games boot**

America's Most Haunted's first launch converts the HEX, stores its defaults and reaches attract mode; the second keeps them, takes `[E97000]` and leaves the version screen; then the romset check runs the system set alone, `amh` without media and `amh` with a corrupt HEX. The other games run the same check as before:

```sh
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
```
Expected (America's Most Haunted about 2 minutes, the others 2–5 minutes each):
```text
romset: ok
pinmame: ok
romset: ok
pinmame: ok
romset: ok
pinmame: ok
romset: ok
pinmame: ok
```

- [ ] **Step 5: The unit suite, the launch lint, the symbols and the Windows projects**

Run: `AMH_HEX=$AMH_HEX flock /code/spooky_domino/work/emu.lock nice -n 15 tests/pinheck/pic32mx/check.sh | tail -5`
Expected:
```text
pins: ok (87 logical pins resolved)
makefile link: ok
symbols: ok
vcxproj: ok
pic32mx: 0 failed
```

- [ ] **Step 6: Commit**

```bash
git add src/wpc/sims/pinheck/amh.c src/wpc/pinheck.c src/wpc/pinheckgames.c src/wpc/driver.c tests/pinheck/games.sh tests/pinheck/pic32mx tests/pinheck/link/register_build.py src/pinmame.mak cmake vcproj
git commit -m "pinheck: America's Most Haunted (amh): game, simulator, boot check for a game without an update"
```

### Task 6: The simulator check

**Files:**
- Create: `tests/pinheck/sim/amhsim.py`

**Interfaces:**
- Consumes: `pinmame_sim.sh` and `games.sh`'s `SIM`, `TROUGH1`.
- Produces: `amhsim.py plan DIR`, `amhsim.py verify DIR`.

- [ ] **Step 1: The check**

`tests/pinheck/sim/amhsim.py`:

```python
#!/usr/bin/env python3
"""America's Most Haunted simulator check: in attract mode the firmware's console coil and servo commands
([MXXzzz], [SXXzzz]) and the simulator keys move the balls; the switches the simulator closes (PINHECK_OUT_LOG
'W' lines) must follow the coils and servos, and the servo outputs follow the game's servo range.
  amhsim.py plan DIR     write DIR/send, send_at, send_gap, keys.txt, frames
  amhsim.py verify DIR   check DIR/out2.log against the plan"""
import sys

FPS = 60
SEND_AT, GAP = 12.0, 0.05
# (time, console command) and (time, keys, hold frames): a ball's trip round the playfield, then the drain.
# The firmware itself launches a ball that reaches the shooter lane and ejects the scoop and the VUK in attract
# mode; the door is open (servo 1 at 5 degrees) and the Hellevator car down (servo 0 at 10 degrees)
SEND = [(12.0, '[E97000]'), (13.0, '[M20020]'), (22.0, '[S01090]'), (24.0, '[S01005]'), (26.0, '[S00160]'),
        (28.0, '[S00010]'), (29.0, '[M00250]'), (32.0, '[M16020]'), (32.5, '[M17020]'), (33.0, '[M18020]'),
        (33.5, '[M19020]')]
KEYS = [(17.0, 'S', 1), (20.0, 'D', 1), (23.0, 'D', 1), (25.0, 'E', 1), (29.05, 'G', 1), (31.0, 'Q', 1)]
END = 35.0
TROUGH = [84, 85, 86, 87]               # trough 1 (the eject position) to 4
SERVO_US = (544, 2400)                  # the game's servo levels 0 and 255
SHOOTER, SCOOP, VUK, CAR, DRAIN, LOOP, DOOR = 82, 37, 38, 64, 88, 95, 96


def plan(d):
    slots = {round((t - SEND_AT) / GAP): c for t, c in SEND}
    send = ''.join(slots.get(k, '') + '~' for k in range(max(slots) + 1))
    keys = ''.join('%d tap %d %s\n' % (round(t * FPS), h, ' '.join('KEYCODE_' + k for k in ks.split())) for t, ks, h in KEYS)
    for name, text in (('send', send), ('send_at', '%g' % SEND_AT), ('send_gap', '%g' % GAP),
                       ('keys.txt', keys), ('frames', '%d' % round(END * FPS))):
        open('%s/%s' % (d, name), 'w').write(text + ('\n' if name != 'keys.txt' else ''))


def load(path):
    sw, coils, servo, out = [], [], [], {}
    for line in open(path):
        f = line.split()
        if len(f) < 3:
            continue
        t = float(f[0])
        if f[1] == 'W':
            sw += [(t, int(a), int(b)) for a, b in (x.split('=') for x in f[2:])]
        elif f[1] == 'S':
            coils.append((t, int(f[2], 16)))
        elif f[1] == 'V':
            servo.append((t, int(f[2]), float(f[3])))
        elif f[1] == 'P':
            for x in f[2:]:
                k, v = x.split('=')
                out.setdefault(k, []).append((t, int(v)))
    return sw, coils, servo, out


def verify(d):
    sw, coils, servo, out = load(d + '/out2.log')
    fails, checks = [], [0]

    def check(ok, what):
        checks[0] += 1
        if not ok:
            fails.append(what)

    def state(n, t):
        v = 0
        for tt, s, x in sw:
            if tt > t:
                break
            if s == n:
                v = x
        return v

    def edges(n, t0, t1, val=None):
        return [tt for tt, s, x in sw if s == n and t0 <= tt < t1 and (val is None or x == val)]

    def coil_on(c, t0, t1):
        return [t for t, w in coils if t0 <= t < t1 and w >> (c - 1) & 1]

    def level(name, t):
        v = None
        for tt, x in out.get(name, []):
            if tt > t:
                break
            v = x
        return v

    def us(n, t0, t1):
        return sorted(set(round(u) for t, s, u in servo if s == n and t0 <= t < t1))

    rest = [s for s in [SHOOTER] + TROUGH if state(s, 11.0)]
    check(rest == TROUGH, 'at rest closed %s; expected the trough %s' % (rest, TROUGH))
    # attract mode: Hellevator down (10 degrees), door open (5), ghost middle (90), target down (160)
    for n, want in ((0, 647), (1, 593), (2, 1476), (3, 2200)):
        w = us(n, 11.0, 12.0)
        lv = round((want - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
        check(w and all(abs(u - want) < 10 for u in w) and abs((level('S%d' % (57 + n), 12.0) or -9) - lv) <= 1,
              'servo %d in attract: %s us and output %d at %s; expected %d us and %d' % (n, w, 57 + n, level('S%d' % (57 + n), 12.0), want, lv))
    load_ = coil_on(21, 13.0, 13.5)
    check(load_ and edges(84, load_[0], load_[0] + 0.1, 0) and edges(SHOOTER, load_[0], load_[0] + 0.5, 1),
          'trough: [M20020] at 13.0 (coil 21 %s) did not move trough 1 (84) to the shooter lane (82)' % load_[:1])
    check([state(s, 14.0) for s in TROUGH] == [1, 1, 1, 0], 'trough: the three balls did not roll down to 84-86: %s' % [state(s, 14.0) for s in TROUGH])
    lane = edges(SHOOTER, 13.0, 14.0, 1)
    launch = coil_on(23, lane[0], lane[0] + 1.0) if lane else []
    check(launch and edges(SHOOTER, launch[0], launch[0] + 0.1, 0), 'autoplunger: the firmware did not launch the ball from the shooter lane (82 %s, coil 23 %s)' % (lane[:1], launch[:1]))
    for name, sw_, coil, t in (('scoop', SCOOP, 11, 17.0), ('VUK behind the open door', VUK, 12, 20.0)):
        into = edges(sw_, t, t + 0.4, 1)
        kick = coil_on(coil, into[0], into[0] + 2.0) if into else []
        check(kick and edges(sw_, kick[0], kick[0] + 0.1, 0) and not edges(sw_, into[0], kick[0], 0),
              '%s: switch %d closed %s, the firmware fired coil %d %s, released %s' % (name, sw_, into[:1], coil, kick[:1], edges(sw_, t, t + 3.0, 0)))
    check(us(1, 22.6, 23.0) and all(abs(u - 1476) < 10 for u in us(1, 22.6, 23.0)), 'door: [S01090] did not close it (servo 1 %s)' % us(1, 22.6, 23.0))
    check(len(edges(DOOR, 23.0, 23.6, 1)) == 1 and len(edges(DOOR, 23.0, 23.6, 0)) == 1 and not edges(VUK, 23.0, 24.0),
          'door: a shot at the closed door did not close and open the door opto (96) alone')
    into = edges(CAR, 25.0, 25.2, 1)
    up = [t for t, s, u in servo if s == 0 and t >= 26.0 and u > 1400]
    out_ = edges(CAR, 25.2, 28.0, 0)
    check(into and up and out_ and out_[0] > up[0], 'Hellevator: the car switch (64) closed %s, the car rose %s, the ball left %s: it must ride up first' % (into[:1], up[:1], out_[:1]))
    mag = coil_on(1, 29.0, 29.1)
    opto = edges(LOOP, 29.0, 29.2, 1)
    check(mag and opto, 'ghost loop: magnet coil 1 %s and loop opto 95 %s' % (mag[:1], opto[:1]))
    check([state(s, 30.5) for s in [SHOOTER] + TROUGH] == [0, 1, 1, 1, 0], 'magnet: the trough changed while the ball was held')
    drain = edges(DRAIN, 31.0, 31.3, 1)
    kick = coil_on(22, drain[0], drain[0] + 2.0) if drain else []
    check(kick and edges(DRAIN, kick[0], kick[0] + 0.1, 0), 'drain: switch 88 closed %s, the firmware fired the drain kicker (22) %s' % (drain[:1], kick[:1]))
    check([state(s, 31.9) for s in [SHOOTER] + TROUGH] == [0, 1, 1, 1, 1], 'drain: the ball did not return to the trough (82, %s: %s)' % (TROUGH, [state(s, 31.9) for s in [SHOOTER] + TROUGH]))
    for c, s, t in ((17, 75, 32.0), (18, 75, 32.5), (19, 74, 33.0), (20, 74, 33.5)):
        on = coil_on(c, t, t + 0.4)
        check(on and edges(s, on[0], on[0] + 0.05, 1) and edges(s, on[0], on[0] + 0.2, 0),
              'flipper coil %d (%s) did not close and open its end-of-stroke switch %d' % (c, on[:1], s))
    for f in fails:
        print('SIM FAIL: ' + f)
    print('sim: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'verify'):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2]))
```

Then: `chmod +x tests/pinheck/sim/amhsim.py`

- [ ] **Step 2: The simulator checks pass**

```sh
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh | tail -2
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh | tail -1
```
Expected (about 3 minutes each):
```text
sim: 21 checks, 0 failures
pinmame sim: ok
pinmame sim: ok
```

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/sim/amhsim.py
git commit -m "pinheck: America's Most Haunted's simulator check (trough and drain kicker, autoplunger, scoop, door and VUK, Hellevator, ghost loop, end-of-stroke switches, servos)"
```

### Task 7: The service tests (check 5)

**Files:**
- Modify: `tests/pinheck/board/check5.py`

**Interfaces:**
- Consumes: Plan 11b's `check5.py` (`grid`, `shown_states`).
- Produces: check 5's plan and checks for `amh` (Ruling 11); Domino's, Rob Zombie's and The Jetsons' plans unchanged.

- [ ] **Step 1: Check 5 for America's Most Haunted**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/board/check5.py', [
    (r'''  check5.py verify DIR    check DIR/out2.log and DIR/frames2.bin against the plan
  check5.py selftest      the verifier's helpers
PINHECK_GAME (dominos, rzspook, jetsons) selects the game's servo and RGB tests, its menu, its switch-test
screen and its resting balls."""
import os
''',
     r'''  check5.py verify DIR    check DIR/out2.log and DIR/frames2.bin against the plan
  check5.py selftest      the verifier's helpers
PINHECK_GAME (dominos, rzspook, jetsons, amh) selects the game's servo and RGB tests, its menu, its switch-test
screen and its resting balls."""
import os
'''),
    (r'''GI = ['S%d' % s for s in list(range(25, 33)) + list(range(37, 45))]
RGB = ['S%d' % s for s in range(51, 57)]
if GAME == 'rzspook':
    RGB += ['S62', 'S63', 'S64']                    # the LDG light, the external WS2801 LED
SERVO_US = (544, 2400) if GAME in ('rzspook', 'jetsons') else (1000, 2000)   # the game's servo levels 0 and 255
# the switch test: the matrix's and the cabinet columns' x; cabinet inputs closed at rest (the coin door, and
# The Jetsons' trough opto under the ball the tests leave in the trough)
''',
     r'''GI = ['S%d' % s for s in list(range(25, 33)) + list(range(37, 45))]
RGB = ['S%d' % s for s in range(51, 57)]
if GAME in ('rzspook', 'amh'):
    RGB += ['S62', 'S63', 'S64']                    # the LDG light, the external WS2801 LED; America's Most Haunted's ghost
SERVO_US = (544, 2400) if GAME in ('rzspook', 'jetsons', 'amh') else (1000, 2000)   # the game's servo levels 0 and 255
# America's Most Haunted's ALL ON steps leave lamp 56 (Spook Again, 81) off
ALL_LAMPS = tuple(l for l in LAMPS if not (GAME == 'amh' and l == 'L81'))
# the switch test: the matrix's and the cabinet columns' x; cabinet inputs closed at rest (the coin door, and
# The Jetsons' trough opto under the ball the tests leave in the trough)
'''),
    (r'''def key_plan():
    """(time, keys, hold frames, expectation for the window after it)"""
    ev, t = [(MENU_AT, '0', 6, None)], KEYS_AT     # main menu: SWITCH EDGE

    def tap(keys, dt, exp=None, hold=6):
''',
     r'''def key_plan():
    """(time, keys, hold frames, expectation for the window after it)"""
    ev, t = [(MENU_AT, '0', 6, None)], KEYS_AT     # main menu: SWITCH EDGE (America's Most Haunted: MAIN SETTINGS)

    def tap(keys, dt, exp=None, hold=6):
'''),
    (r'''        ev.append((t, keys, hold, exp))
        t += dt
    tap('RSHIFT', 1.0)
    tap('RSHIFT', 1.0)                              # SOLENOID
    tap('0', 1.0)                                   # SOLENOID TEST: KNOCKER (Rob Zombie: AUTOPLUNGER)
    for c in (list(range(16, 24)) + list(range(16)) if GAME == 'rzspook' else range(24)):
        tap('0', 1.5 if c == 1 else 0.5, ('fire', c))   # the shaker test ignores keys for a second
        tap('RSHIFT', 0.5)
''',
     r'''        ev.append((t, keys, hold, exp))
        t += dt
    if GAME == 'amh':                               # MAIN SETTINGS, GAME SETTINGS, GAME AUDITS, then SWITCH EDGE
        for i in range(3):
            tap('RSHIFT', 1.0)
    tap('RSHIFT', 1.0)
    tap('RSHIFT', 1.0)                              # SOLENOID
    tap('0', 1.0)                                   # SOLENOID TEST: KNOCKER (Rob Zombie: AUTOPLUNGER, AMH: RFLIP HIGH)
    for c in (list(range(16, 24)) + list(range(16)) if GAME in ('rzspook', 'amh') else range(24)):
        tap('0', 1.5 if c == 1 else 0.5, ('fire', c))   # the shaker test ignores keys for a second
        tap('RSHIFT', 0.5)
'''),
    (r'''    if GAME == 'jetsons':
        pass
    elif GAME == 'rzspook':
        tap('0', 1.0)                               # SERVO TEST: GATE OPEN
''',
     r'''    if GAME == 'jetsons':
        pass
    elif GAME == 'amh':
        # SERVO TEST: DOOR OPEN, DOOR CLOSE, TARGET UP, TARGET DOWN, GHOST LEFT, MIDDLE, RIGHT, HELL UP, HELL DOWN
        # (the factory angles 5, 90, 5, 160, 10, 90, 170, 160, 10 degrees on servos 1, 3, 2 and 0)
        tap('0', 1.0)
        for i, (s, us) in enumerate(((1, 593), (1, 1476), (3, 593), (3, 2200), (2, 647), (2, 1476), (2, 2305), (0, 2200), (0, 647))):
            if i:
                tap('RSHIFT', 1.0)
            tap('0', 1.5, ('position', s, us))
    elif GAME == 'rzspook':
        tap('0', 1.0)                               # SERVO TEST: GATE OPEN
'''),
    (r'''    if GAME != 'jetsons':
        tap('7', 1.0)                               # back to SERVO
    tap('RSHIFT', 1.0)                              # LAMP
    tap('0', 0.5, ('gi', ()))                       # LAMP TEST: ALL OFF
''',
     r'''    if GAME != 'jetsons':
        tap('7', 1.0)                               # back to SERVO
    if GAME == 'amh':
        tap('RSHIFT', 1.0)                          # SERVO DEFAULT
    tap('RSHIFT', 1.0)                              # LAMP
    tap('0', 0.5, ('gi', ()))                       # LAMP TEST: ALL OFF
'''),
    (r'''    rgb = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    ext = (255, 255, 255) if GAME == 'rzspook' else ()          # the LDG light stays white from attract
    tap('0', 1.0, ('rgb', rgb[0] + (0, 0, 0) + ext))    # RGB1 RED
    for v in rgb[1:]:
        tap('RSHIFT', 1.0, ('rgb', v + (0, 0, 0) + ext))
    for v in rgb:
        tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) + v + ext))
    if GAME == 'rzspook':
        # LDG RED, GREEN, BLUE, WHITE: the LDG light's lines are red, blue, green (SWAP G <-> B: NO)
''',
     r'''    rgb = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    ext = (255, 255, 255) if GAME == 'rzspook' else ()          # the LDG light stays white from attract
    if GAME == 'amh':
        # GHOST=RED, GREEN, BLUE, then RGB1 and RGB2 RED, GREEN, BLUE: one LED lit at a time
        steps = [(0, 0, 0) * 2 + v for v in rgb[:3]] + [v + (0, 0, 0) * 2 for v in rgb[:3]] + [(0, 0, 0) + v + (0, 0, 0) for v in rgb[:3]]
        tap('0', 1.0, ('rgb', steps[0]))
        for v in steps[1:]:
            tap('RSHIFT', 1.0, ('rgb', v))
    else:
        tap('0', 1.0, ('rgb', rgb[0] + (0, 0, 0) + ext))    # RGB1 RED
        for v in rgb[1:]:
            tap('RSHIFT', 1.0, ('rgb', v + (0, 0, 0) + ext))
        for v in rgb:
            tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) + v + ext))
    if GAME == 'rzspook':
        # LDG RED, GREEN, BLUE, WHITE: the LDG light's lines are red, blue, green (SWAP G <-> B: NO)
'''),
    (r'''            tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) * 2 + v))
    tap('7', 1.0)                                   # back to RGB LIGHTING
    for i in range(4 if GAME == 'jetsons' else 5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
    tap('DEL', 0.5)                                 # simulator keys off: column/row keys reach the matrix
''',
     r'''            tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) * 2 + v))
    tap('7', 1.0)                                   # back to RGB LIGHTING
    for i in range(4 if GAME == 'jetsons' else 6 if GAME == 'amh' else 5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
    tap('DEL', 0.5)                                 # simulator keys off: column/row keys reach the matrix
'''),
    (r'''    if not g or not c2:
        return None
    for y in range(32):
        for x in range(40):
            if (x % 4 in (0, 3) or y % 4 in (0, 3)) and px(x, y) != (g if MX <= x < MX + 32 else c2):
                return None
    if any(px(x, y) == g for x in range(MX + 1, MX + 31, 4) for y in range(1, 31, 4)):
''',
     r'''    if not g or not c2:
        return None
    # America's Most Haunted draws the grid in two shades: any lit dot is a grid line
    line = (lambda x, y: px(x, y) != 0) if GAME == 'amh' else (lambda x, y: px(x, y) == (g if MX <= x < MX + 32 else c2))
    for y in range(32):
        for x in range(40):
            if (x % 4 in (0, 3) or y % 4 in (0, 3)) and not line(x, y):
                return None
    if any(px(x, y) == g for x in range(MX + 1, MX + 31, 4) for y in range(1, 31, 4)):
'''),
    (r'''    raw = open(d + '/frames2.bin', 'rb').read()
    rec = 20 + FRAME
    frames = [(struct.unpack_from('<Q', raw, i + 8)[0] / 80e6, raw[i + 20:i + rec]) for i in range(0, len(raw) - rec + 1, rec)]
    fails = []

''',
     r'''    raw = open(d + '/frames2.bin', 'rb').read()
    rec = 20 + FRAME
    frames = [(struct.unpack_from('<Q', raw, i + 8)[0] / 80e6, raw[i + 20:i + rec]) for i in range(0, len(raw) - rec + 1, rec)
              if GAME != 'amh' or struct.unpack_from('<I', raw, i + 16)[0] != 0xFFFFFFFF]   # a raw DMD cycle the frame copy crossed
    fails = []

'''),
    (r'''            check(w and all(abs(u - us) < 10 for u in w), 'attract servo %d pulses %s us, expected %d' % (n, w, us))
            check(abs(o.at('S%d' % (57 + n), MENU_AT) - level) <= 1, 'attract servo output %d is %d, expected %d' % (57 + n, o.at('S%d' % (57 + n), MENU_AT), level))
    else:
        check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
''',
     r'''            check(w and all(abs(u - us) < 10 for u in w), 'attract servo %d pulses %s us, expected %d' % (n, w, us))
            check(abs(o.at('S%d' % (57 + n), MENU_AT) - level) <= 1, 'attract servo output %d is %d, expected %d' % (57 + n, o.at('S%d' % (57 + n), MENU_AT), level))
    elif GAME == 'amh':
        # the start lamp stays off; the ghost (on-board LED 2, 62-64) stays dark while RGB1 and RGB2 fade; the servos
        # rest at HELL DOWN, DOOR OPEN, GHOST MIDDLE and TARGET DOWN
        check(not o.rises('L91', 5, MENU_AT), 'start lamp 91 lit in attract')
        check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [0] * 3, 'the ghost on 62-64 is lit in attract')
        check(len(set(v for t, v in o.ev.get('S51', []) if 5 <= t < MENU_AT)) > 3, 'RGB1 (51) does not fade in attract')
        for n, us in ((0, 647), (1, 593), (2, 1476), (3, 2200)):
            w = sorted(set(round(u) for tt, u in o.servo.get(n, []) if MENU_AT - 1.0 <= tt < MENU_AT))
            level = round((us - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
            check(w and all(abs(u - us) < 10 for u in w), 'attract servo %d pulses %s us, expected %d' % (n, w, us))
            check(abs(o.at('S%d' % (57 + n), MENU_AT) - level) <= 1, 'attract servo output %d is %d, expected %d' % (57 + n, o.at('S%d' % (57 + n), MENU_AT), level))
    else:
        check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
'''),
    (r'''    if GAME == 'jetsons':
        pass
    elif GAME == 'rzspook':
        gi = [o.at('S%d' % s, MENU_AT) for s in range(37, 45)]
''',
     r'''    if GAME == 'jetsons':
        pass
    elif GAME == 'amh':
        # the attract show uses the four playfield GI circuits (GI_8-11: 37-40) and no other GI
        check(all(o.rises('S%d' % s, 5, MENU_AT) for s in range(37, 41)), 'attract playfield GI 37-40 never all lit')
        check(not any(o.rises('S%d' % s, 5, MENU_AT) for s in list(range(25, 33)) + list(range(41, 45))), 'attract lit GI other than 37-40')
    elif GAME == 'rzspook':
        gi = [o.at('S%d' % s, MENU_AT) for s in range(37, 45)]
'''),
    (r'''    # The Jetsons: the console's load and launch and the solenoid test's LOAD COIL and PLUNGER put two balls on the
    # playfield; the third rests on trough opto 1 (cabinet 10)
    want = {0, 1, 2, 3, 4, 5} if GAME == 'rzspook' else set() if GAME == 'jetsons' else {0, 1}
    check(rest and base - {39} == want and rest[-1][1] == CAB_REST, 'switch test at rest shows %s, expected switches %s, maybe Noid Home (39), and cabinet inputs %s' % (rest[-1:], sorted(want), CAB_REST))
    for i, (t, k, hold, e) in enumerate(ev):
''',
     r'''    # The Jetsons: the console's load and launch and the solenoid test's LOAD COIL and PLUNGER put two balls on the
    # playfield; the third rests on trough opto 1 (cabinet 10)
    # America's Most Haunted: the console's ball load and the solenoid test's BALL LOAD each put a ball in the shooter
    # lane, which the firmware's autoplunger launched; the other two rest in trough 1 and 2 (59, 60)
    want = {0, 1, 2, 3, 4, 5} if GAME == 'rzspook' else set() if GAME == 'jetsons' else {59, 60} if GAME == 'amh' else {0, 1}
    check(rest and base - {39} == want and rest[-1][1] == CAB_REST, 'switch test at rest shows %s, expected switches %s, maybe Noid Home (39), and cabinet inputs %s' % (rest[-1:], sorted(want), CAB_REST))
    for i, (t, k, hold, e) in enumerate(ev):
'''),
    (r'''            if e[0] == 'gi+lamps':
                want = tuple(GI) if e[1] == 'all' else ()
                check(o.on(LAMPS, t1 - 0.05) == (tuple(LAMPS) if e[1] == 'all' else ()), 'GI AND LAMPS %s: lamps %s' % (e[1], o.on(LAMPS, t1 - 0.05)))
            got = o.on(GI, t1 - 0.05)
            check(sorted(got) == sorted(want), 'lamp test GI step at %.1f: %s, expected %s' % (t, got, want))
''',
     r'''            if e[0] == 'gi+lamps':
                want = tuple(GI) if e[1] == 'all' else ()
                check(o.on(LAMPS, t1 - 0.05) == (ALL_LAMPS if e[1] == 'all' else ()), 'GI AND LAMPS %s: lamps %s' % (e[1], o.on(LAMPS, t1 - 0.05)))
            got = o.on(GI, t1 - 0.05)
            check(sorted(got) == sorted(want), 'lamp test GI step at %.1f: %s, expected %s' % (t, got, want))
'''),
    (r'''        elif e[0] == 'lamps':
            got = o.on(LAMPS, t1 - 0.05)
            check(got == (tuple(LAMPS) if e[1] == 'all' else ()), 'lamp test ALL %s: %d lamps' % (e[1], len(got)))
        elif e[0] == 'rgb':
            got = tuple(o.at(s, t1 - 0.05) for s in RGB)
''',
     r'''        elif e[0] == 'lamps':
            got = o.on(LAMPS, t1 - 0.05)
            check(got == (ALL_LAMPS if e[1] == 'all' else ()), 'lamp test ALL %s: %d lamps' % (e[1], len(got)))
        elif e[0] == 'rgb':
            got = tuple(o.at(s, t1 - 0.05) for s in RGB)
'''),
])
EOF
```

- [ ] **Step 2: The other games' plans are unchanged**

```sh
mkdir -p build/c5a build/c5b && git show HEAD:tests/pinheck/board/check5.py > build/check5-before.py
for g in dominos rzspook jetsons; do PINHECK_GAME=$g python3 build/check5-before.py plan build/c5a && PINHECK_GAME=$g python3 tests/pinheck/board/check5.py plan build/c5b
	for f in send send_at send_gap keys.txt frames; do cmp -s build/c5a/$f build/c5b/$f || echo "differs: $g $f"; done; done; echo "check 5 plans compared"
python3 tests/pinheck/board/check5.py selftest
rm -rf tests/pinheck/board/__pycache__
```
Expected: `check 5 plans compared` alone, then `check5 selftest: ok`.

- [ ] **Step 3: Check 5**

```sh
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh | tail -3
```
Expected (about 6 minutes):
```text
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.580 0.706 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 274 service-test steps, 0 failures
pinmame board: ok
```

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/board/check5.py
git commit -m "pinheck: check 5 for America's Most Haunted (menu from MAIN SETTINGS, servo test, ghost light, attract servos and GI)"
```

### Task 8: The raw DMD in the display check

**Files:**
- Modify: `tests/pinheck/display/frames.py`, `tests/pinheck/display/render.py`, `tests/pinheck/display/pinmame_display.sh`

**Interfaces:**
- Consumes: Task 4's frame log and `PINHECK_DMD_PROOF`; `games.sh` `LOOK=dmd`, `CLIP=AB1`.
- Produces: `frames.py` for a raw DMD (with `PINHECK_GAME=amh`: 4,096-byte cycles of shades, cycles the frame copy crossed left out, 4 bpp `.VID` frames); `render.py --dmd` (the core's DMD rendering, palette snapshots); the display check's `LOOK=dmd` branch (Ruling 13).

- [ ] **Step 1: The checks**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/frames.py', [
    (r'''import sys

# the module PINHECK_GAME has: 128x32, or The Jetsons' 128x64, whose firmware keeps no whole frame in hub RAM
W, H = 128, 64 if os.environ.get('PINHECK_GAME') == 'jetsons' else 32
FRAME = W * H
PROP_HZ = 104e6
PIC_HZ = 80e6
''',
     r'''import sys

# the module PINHECK_GAME has: 128x32, or The Jetsons' 128x64, whose firmware keeps no whole frame in hub RAM;
# America's Most Haunted's raw DMD logs each cycle of 16 subframes, one byte per dot (0-15), and its .VID frames
# are 4 bpp (two dots a byte, the left one high)
W, H = 128, 64 if os.environ.get('PINHECK_GAME') == 'jetsons' else 32
FRAME = W * H
DMD = os.environ.get('PINHECK_GAME') == 'amh'
TORN = 0xFFFFFFFF  # a raw DMD cycle the firmware's frame copy crossed: no whole frame
PROP_HZ = 104e6
PIC_HZ = 80e6
'''),
    (r'''def read_vid(path):
    d = open(path, 'rb').read()
    if len(d) < 512 or (len(d) - 512) % FRAME:
        sys.exit('frames: %s is not a .VID file' % path)
    return [d[i:i + FRAME] for i in range(512, len(d), FRAME)]


''',
     r'''def read_vid(path):
    d = open(path, 'rb').read()
    n = FRAME // 2 if DMD else FRAME
    if len(d) < 512 or (len(d) - 512) % n:
        sys.exit('frames: %s is not a .VID file' % path)
    if DMD:
        return [bytes(x for b in d[i:i + n] for x in (b >> 4, b & 15)) for i in range(512, len(d), n)]
    return [d[i:i + n] for i in range(512, len(d), n)]


'''),
    (r'''    if not hub:
        sys.exit('frames: FAIL, every frame is uniform')
    if H == 64:
        if hub != [0xFFFFFFFF]:
            sys.exit('frames: FAIL, a 128x64 frame is whole in hub RAM at %s' % ['$%04x' % h for h in hub if h != 0xFFFFFFFF])
''',
     r'''    if not hub:
        sys.exit('frames: FAIL, every frame is uniform')
    if DMD:
        whole = [at for _, _, at, _ in recs if at != TORN]
        if hub != [hub[0], TORN][:len(hub)] or hub[0] == TORN or len(whole) < len(recs) // 2:
            sys.exit('frames: FAIL, raw DMD cycles at hub addresses %s, %d of %d whole' % (['none' if h == TORN else '$%04x' % h for h in hub], len(whole), len(recs)))
        print('frames: %d of %d cycles equal the firmware framebuffer at hub $%04x at their end, the others the frame copy crossed' % (len(whole), len(recs), hub[0]))
        recs = [r for r in recs if r[2] != TORN]
        log = [(t, f) for t, _, _, f in recs]
    elif H == 64:
        if hub != [0xFFFFFFFF]:
            sys.exit('frames: FAIL, a 128x64 frame is whole in hub RAM at %s' % ['$%04x' % h for h in hub if h != 0xFFFFFFFF])
'''),
])
edit('tests/pinheck/display/render.py', [
    (r'''    if d[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('render: FAIL, %s is not a PNG' % path)
    i, idat = 8, b''
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i + 8])
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', d[i + 8:i + 18])
        elif t == b'IDAT':
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    if (depth, ctype) != (8, 2):
        sys.exit('render: FAIL, %s is not 8 bit RGB' % path)
    raw, bpp, stride = zlib.decompress(idat), 3, 3 * w
    rows, prev = [], bytearray(stride)
    for y in range(h):
''',
     r'''    if d[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('render: FAIL, %s is not a PNG' % path)
    i, idat, plte = 8, b'', b''
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i + 8])
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', d[i + 8:i + 18])
        elif t == b'PLTE':
            plte = d[i + 8:i + 8 + n]
        elif t == b'IDAT':
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    if (depth, ctype) not in ((8, 2), (8, 3)):
        sys.exit('render: FAIL, %s is not 8 bit RGB or palette' % path)
    bpp = 3 if ctype == 2 else 1  # a palette snapshot: the core's pens (a raw DMD game)
    raw, stride = zlib.decompress(idat), bpp * w
    rows, prev = [], bytearray(stride)
    for y in range(h):
'''),
    (r'''                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[3 * x:3 * x + 3]) for x in range(w)])
        prev = line
    return w, h, rows
''',
     r'''                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        if bpp == 3:
            rows.append([tuple(line[3 * x:3 * x + 3]) for x in range(w)])
        else:
            rows.append([tuple(plte[3 * p:3 * p + 3]) for p in line])
        prev = line
    return w, h, rows
'''),
    (r'''

def main():
    ap = argparse.ArgumentParser()
''',
     r'''

def dmd_check(rows, frames):
    """the core's DMD rendering (2x2 per dot, its colour at the dot's top left pixel): one colour per shade of the
    frame shown, brighter for every higher shade; returns the matched frame's index or None"""
    for k in range(len(frames) - 1, -1, -1):
        f, pens = frames[k], {}
        for y in range(H):
            for x in range(W):
                pens.setdefault(f[y * W + x], set()).add(rows[2 * y][2 * x])
        if any(len(p) != 1 for p in pens.values()):
            continue
        lum = [sum(next(iter(pens[v]))) for v in sorted(pens)]
        if all(a < b for a, b in zip(lum, lum[1:])):
            return k
    return None


def main():
    ap = argparse.ArgumentParser()
'''),
    (r'''    ap.add_argument('--config', help='the last config packet (hex bytes) before the snapshot; none = the exact look')
    ap.add_argument('--sol-log', action='store_true', help='the core\'s solenoid log must show')
    a = ap.parse_args()
    look, _ = parse_look(bytes.fromhex(a.config) if a.config else None)
''',
     r'''    ap.add_argument('--config', help='the last config packet (hex bytes) before the snapshot; none = the exact look')
    ap.add_argument('--sol-log', action='store_true', help='the core\'s solenoid log must show')
    ap.add_argument('--dmd', action='store_true', help='a raw DMD (frames of shades 0-15) in the core\'s DMD rendering')
    a = ap.parse_args()
    look, _ = parse_look(bytes.fromhex(a.config) if a.config else None)
'''),
    (r'''        sys.exit('render: FAIL, the visible area cannot hold the scaled frame')
    frames = [f for _, _, _, f in read_log(a.log)][-a.last:]
    hit = next((k for k in range(len(frames) - 1, -1, -1) if frame_matches(rows, frames[k], look)), None)
    if hit is None:
        print('render: FAIL, the frame region shows none of the last %d decoded frames' % len(frames))
        fail = 1
    elif frames[hit].count(frames[hit][0]) == FRAME:
        print('render: FAIL, the matched frame is uniform, nothing was checked')
        fail = 1
    else:
        print('render: frame region equals decoded frame %d of the last %d, %dx%d per pixel' % (hit, len(frames), SCALE, SCALE))
    panel = H * SCALE + 3
    bad = [(x, y) for y in range(min(h, panel)) for x in range(w)
''',
     r'''        sys.exit('render: FAIL, the visible area cannot hold the scaled frame')
    frames = [f for _, _, _, f in read_log(a.log)][-a.last:]
    if a.dmd:
        hit = dmd_check(rows, frames)
        shades = sorted(set(frames[hit])) if hit is not None else []
        if hit is None or len(shades) < 2:
            print('render: FAIL, the DMD region shows none of the last %d decoded frames with one rising colour per shade' % len(frames))
            fail = 1
        else:
            print('render: DMD region equals decoded frame %d of the last %d: shades %s, one colour each, rising' % (hit, len(frames), shades))
    else:
        hit = next((k for k in range(len(frames) - 1, -1, -1) if frame_matches(rows, frames[k], look)), None)
        if hit is None:
            print('render: FAIL, the frame region shows none of the last %d decoded frames' % len(frames))
            fail = 1
        elif frames[hit].count(frames[hit][0]) == FRAME:
            print('render: FAIL, the matched frame is uniform, nothing was checked')
            fail = 1
        else:
            print('render: frame region equals decoded frame %d of the last %d, %dx%d per pixel' % (hit, len(frames), SCALE, SCALE))
    panel = H * SCALE + 3
    bad = [(x, y) for y in range(min(h, panel)) for x in range(w)
'''),
    (r'''    pens = set(rows[y][x] for y in range(panel, h) for x in range(min(w, SIM_X))) - {(0, 0, 0)}
    shown = [tuple((c >> 3) << 3 | c >> 5 for c in rgb332(v)) for v in range(1, 256)]  # as a 15 bpp screen shows them
    frame_pens = [p for p in pens if any(all(abs(a - b) <= 1 for a, b in zip(p, q)) for q in shown)]
    if bad:
        x, y = bad[0]
''',
     r'''    pens = set(rows[y][x] for y in range(panel, h) for x in range(min(w, SIM_X))) - {(0, 0, 0)}
    shown = [tuple((c >> 3) << 3 | c >> 5 for c in rgb332(v)) for v in range(1, 256)]  # as a 15 bpp screen shows them
    frame_pens = [] if a.dmd else [p for p in pens if any(all(abs(a - b) <= 1 for a, b in zip(p, q)) for q in shown)]
    if bad:
        x, y = bad[0]
'''),
])
edit('tests/pinheck/display/pinmame_display.sh', [
    (r'''		|| { echo "PINMAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
# F12 just before the end of launch 1: a screen snapshot for render.py
echo "1190 tap 2 KEYCODE_F12" > $B/snap.ks
''',
     r'''		|| { echo "PINMAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
if [ $LOOK = dmd ]; then
	# the raw DMD: launch 1 compares the decoder with the row model (PINHECK_DMD_PROOF) and takes a snapshot of the
	# service menu, a still screen; launch 2 turns video attract mode off and plays the clip, then takes one more
	printf '600 tap 6 KEYCODE_0\n1190 tap 2 KEYCODE_F12\n' > $B/snap.ks
	PINHECK_DMD_PROOF=1 launch 1 1200 "-key_script $PWD/$B/snap.ks -snapshot_directory $PWD/$B/snap"
	echo "1190 tap 2 KEYCODE_F12" > $B/snap2.ks
	PINHECK_UART1_SEND_AT=$((SEND_AT - 2)) PINHECK_UART1_SEND="[E96000]~~[V00$CLIP]" launch 2 1200 "-key_script $PWD/$B/snap2.ks -snapshot_directory $PWD/$B/snap2"
	fail=0
	grep -aq "Playing Video" $B/uart2.log || { echo "PINMAME FAIL: [V00$CLIP] not acknowledged"; fail=1; }
	grep "^display:" $B/prop1.log $B/prop2.log && { echo "PINMAME FAIL: the display link decoder ran for a raw DMD"; fail=1; }
	# the row model differs only where the firmware's frame copy overtook the row being shifted: one row in under 1%
	grep "^dmd: proof:" $B/prop1.log
	python3 -c "import re, sys; m = re.search(r'^dmd: proof: (\d+) subframes, the row model differs in (\d+) \((\d+) rows\)', open(sys.argv[1]).read(), re.M); sys.exit(not m or int(m[1]) < 10000 or int(m[2]) != int(m[3]) or 100 * int(m[2]) >= int(m[1]))" $B/prop1.log ||
		{ echo "PINMAME FAIL: no proof line, under 10000 subframes, or the row model differs by more than one row in 1% of them"; fail=1; }
	python3 frames.py $B/frames1.bin || fail=1
	python3 render.py $B/snap/$GAME.png $B/frames1.bin --dmd || fail=1
	dir=$(echo "$CLIP" | cut -c1)
	python3 frames.py $B/frames2.bin --after "$SEND_AT" --vid "$PINHECK_UPDATE_DIR/DMD/_D$dir/$CLIP.VID" || fail=1
	python3 render.py $B/snap2/$GAME.png $B/frames2.bin --dmd || fail=1
	[ $fail -eq 0 ] || exit 1
	echo "pinmame display: ok"
	exit 0
fi
# F12 just before the end of launch 1: a screen snapshot for render.py
echo "1190 tap 2 KEYCODE_F12" > $B/snap.ks
'''),
])
EOF
```

- [ ] **Step 2: The display checks**

```sh
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh | tail -1
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh | tail -1
```
Expected (America's Most Haunted about 2 minutes, the others about 4 each):
```text
dmd: proof: 14640 subframes, the row model differs in 36 (36 rows), the first at Propeller cycle 65761576
frames: 914 frames from 1.39s to 20.01s, 49.0 per second
frames: 836 of 914 cycles equal the firmware framebuffer at hub $5b0c at their end, the others the frame copy crossed
render: DMD region equals decoded frame 7 of the last 8: shades [0, 4, 6, 9, 11, 13, 15], one colour each, rising
render: 256x256 visible, black outside the frame apart from the core panel (8 pens, none a frame colour)
frames: 393 frames from 12.02s to 20.01s, 49.0 per second
frames: 293 of 393 cycles equal the firmware framebuffer at hub $5b0c at their end, the others the frame copy crossed
frames: AB1.VID: frames 0..59 of 60 shown pixel-exact, contiguous and in order (log frames 0..170)
render: DMD region equals decoded frame 3 of the last 8: shades [0, 11, 12, 15], one colour each, rising
render: 256x256 visible, black outside the frame apart from the core panel (22 pens, none a frame colour)
pinmame display: ok
pinmame display: ok
pinmame display: ok
```

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/display/frames.py tests/pinheck/display/render.py tests/pinheck/display/pinmame_display.sh
git commit -m "pinheck: the raw DMD in the display check (row-model proof, AB1.VID pixel-exact, the core's DMD rendering)"
```

### Task 9: The raw DMD through libpinmame and VPinMAME

**Files:**
- Modify: `src/wpc/core.c`, `src/libpinmame/libpinmame.cpp`, `src/win32com/ControllerDmdDevice.cpp`, `tests/pinheck/vpx/host.cpp`, `tests/pinheck/vpx/vpx.py`, `tests/pinheck/vpx/pinmame_vpx.sh`

**Interfaces:**
- Consumes: Task 4's `PINHECK_DMD_LOG`.
- Produces: 16 shades for `GEN_PINHECK` DMDs (Ruling 4); `host` writes DMD frames (one byte per dot) to `frames.bin`; `vpx.py dmd DIR`; `pinmame_vpx.sh` for `LOOK=dmd`.

- [ ] **Step 1: The 16-shade lists and the check**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/core.c', [
    (r'''  const float perc66 = hasPercents ? (float)pmoptions.dmd_perc66 : 67.f;

  if ((core_gameData->gen & (GEN_SAM | GEN_SPA | GEN_ALVG_DMD2)) || (strncasecmp(Machine->gamedrv->name, "smb", 3) == 0) || (strncasecmp(Machine->gamedrv->name, "cueball", 7) == 0)) {
    // Backward compatibility: 16 shades mode has no colorization and fixed lighting levels ranging from 0 to 100
    //static const UINT8 levelgts3[16] = {0/*5*/, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100}; // GTS3 and AlvinG brightness seems okay
''',
     r'''  const float perc66 = hasPercents ? (float)pmoptions.dmd_perc66 : 67.f;

  if ((core_gameData->gen & (GEN_SAM | GEN_SPA | GEN_ALVG_DMD2 | GEN_PINHECK)) || (strncasecmp(Machine->gamedrv->name, "smb", 3) == 0) || (strncasecmp(Machine->gamedrv->name, "cueball", 7) == 0)) { // pinHeck: the raw DMD's 16 levels
    // Backward compatibility: 16 shades mode has no colorization and fixed lighting levels ranging from 0 to 100
    //static const UINT8 levelgts3[16] = {0/*5*/, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100}; // GTS3 and AlvinG brightness seems okay
'''),
])
edit('src/libpinmame/libpinmame.cpp', [
    (r'''				pDisplay->layout.width = layout->length;
				pDisplay->layout.height = layout->start;
				const int shade_16_enabled = (core_gameData->gen & (GEN_SAM | GEN_SPA | GEN_ALVG | GEN_ALVG_DMD2 | GEN_GTS3)) != 0;
				pDisplay->layout.depth = shade_16_enabled ? 4 : 2;
				pDisplay->size = pDisplay->layout.width * pDisplay->layout.height;
''',
     r'''				pDisplay->layout.width = layout->length;
				pDisplay->layout.height = layout->start;
				const int shade_16_enabled = (core_gameData->gen & (GEN_SAM | GEN_SPA | GEN_ALVG | GEN_ALVG_DMD2 | GEN_GTS3 | GEN_PINHECK)) != 0; // pinHeck: the raw DMD's 16 levels
				pDisplay->layout.depth = shade_16_enabled ? 4 : 2;
				pDisplay->size = pDisplay->layout.width * pDisplay->layout.height;
'''),
    (r'''         if ((layout->type & CORE_SEGMASK) != CORE_VIDEO)
         {
            def.srcId.identifyFormat = ((core_gameData->gen & (GEN_SAM | GEN_SPA | GEN_ALVG_DMD2)) || (strncasecmp(Machine->gamedrv->name, "smb", 3) == 0) || (strncasecmp(Machine->gamedrv->name, "cueball", 7) == 0)) ? CTLPI_DISPLAY_ID_FORMAT_BITPLANE4 : CTLPI_DISPLAY_ID_FORMAT_BITPLANE2;
            def.srcId.GetIdentifyFrame = &GetDisplayIdFrame;
         }
''',
     r'''         if ((layout->type & CORE_SEGMASK) != CORE_VIDEO)
         {
            def.srcId.identifyFormat = ((core_gameData->gen & (GEN_SAM | GEN_SPA | GEN_ALVG_DMD2 | GEN_PINHECK)) || (strncasecmp(Machine->gamedrv->name, "smb", 3) == 0) || (strncasecmp(Machine->gamedrv->name, "cueball", 7) == 0)) ? CTLPI_DISPLAY_ID_FORMAT_BITPLANE4 : CTLPI_DISPLAY_ID_FORMAT_BITPLANE2; // pinHeck: the raw DMD's 16 levels
            def.srcId.GetIdentifyFrame = &GetDisplayIdFrame;
         }
'''),
])
edit('src/win32com/ControllerDmdDevice.cpp', [
    (r'''extern "C" void dmddeviceRenderDMDFrame(const int width, const int height, float* dmdDotLum, UINT8* dmdDotRaw, UINT32 noOfRawFrames, UINT8* rawbuffer, const int isDMD2) {
	// 16 shades based on hardware generation and extended to some GTS3 games using long PWM pattern (SMB, SMBMW and CBW)
	const int is16Shades = (core_gameData->gen & (GEN_SAM | GEN_SPA | GEN_ALVG_DMD2)) || (strncasecmp(Machine->gamedrv->name, "smb", 3) == 0) || (strncasecmp(Machine->gamedrv->name, "cueball", 7) == 0);
	dmd_width = width; // store for DeInit
	dmd_height = height;
''',
     r'''extern "C" void dmddeviceRenderDMDFrame(const int width, const int height, float* dmdDotLum, UINT8* dmdDotRaw, UINT32 noOfRawFrames, UINT8* rawbuffer, const int isDMD2) {
	// 16 shades based on hardware generation and extended to some GTS3 games using long PWM pattern (SMB, SMBMW and CBW)
	const int is16Shades = (core_gameData->gen & (GEN_SAM | GEN_SPA | GEN_ALVG_DMD2 | GEN_PINHECK)) || (strncasecmp(Machine->gamedrv->name, "smb", 3) == 0) || (strncasecmp(Machine->gamedrv->name, "cueball", 7) == 0); // pinHeck: the raw DMD's 16 levels
	dmd_width = width; // store for DeInit
	dmd_height = height;
'''),
])
edit('tests/pinheck/vpx/host.cpp', [
    (r'''     -R  stop after FRAMES, copy $PINHECK_LINK_LOG to DIR/link1.log, run FRAMES more in a new session
   host -T: messages broadcast from a second thread while this one subscribes (for a ThreadSanitizer build)
   DIR gets api.log, frames.bin (VIDEO frames: uint32 frame, then the pixels) and audio.raw (int16 stereo). */
#include "libpinmame.h"
#include "plugins/ControllerPlugin.h"
''',
     r'''     -R  stop after FRAMES, copy $PINHECK_LINK_LOG to DIR/link1.log, run FRAMES more in a new session
   host -T: messages broadcast from a second thread while this one subscribes (for a ThreadSanitizer build)
   DIR gets api.log, frames.bin (VIDEO and DMD frames: uint32 frame, then the pixels; the plugin's with the top bit
   set) and audio.raw (int16 stereo). */
#include "libpinmame.h"
#include "plugins/ControllerPlugin.h"
'''),
    (r'''		fwrite(&tag, 4, 1, framef);
		fwrite(data, 1, (size_t)l->width * l->height * (l->depth == 16 ? 2 : 3), framef);
	}
	auto it = switches.find(f);
''',
     r'''		fwrite(&tag, 4, 1, framef);
		fwrite(data, 1, (size_t)l->width * l->height * (l->depth == 16 ? 2 : 3), framef);
	} else if (data && (l->type & PINMAME_DISPLAY_TYPE_SEGMASK) == PINMAME_DISPLAY_TYPE_DMD) {
		uint32_t tag = (uint32_t)f;                  /* one byte per dot: luminance 0-255 */
		fwrite(&tag, 4, 1, framef);
		fwrite(data, 1, (size_t)l->width * l->height, framef);
	}
	auto it = switches.find(f);
'''),
])
edit('tests/pinheck/vpx/vpx.py', [
    (r'''  vpx.py media DIR REF.wav      display frames and sound through the API equal the driver's frame log and capture;
                                every frame on the panel for a vblank (1/60 s) or more reaches the host
  vpx.py mech ON_DIR OFF_DIR    HandleMechanics bit 0 turns the simulated Noid on and off
  vpx.py restart DIR            a second session in the same process: the link log is complete at the first
''',
     r'''  vpx.py media DIR REF.wav      display frames and sound through the API equal the driver's frame log and capture;
                                every frame on the panel for a vblank (1/60 s) or more reaches the host
  vpx.py dmd DIR                America's Most Haunted's raw DMD through the API: 16 shades, the driver's decoded frames
  vpx.py mech ON_DIR OFF_DIR    HandleMechanics bit 0 turns the simulated Noid on and off
  vpx.py restart DIR            a second session in the same process: the link log is complete at the first
'''),
    (r'''

def mech(on, off):
    fails = []
''',
     r'''

def dmd(d):
    """America's Most Haunted's raw DMD as a libpinmame host gets it: one 128x32 DMD of 16 shades (depth 4). The
    driver hands the core each vblank's subframes (PINHECK_DMD_LOG); the core shows the sum of the last 16, so each
    frame of the callback path (bytes, 255 * sum / 16) and of the plugin path (floats, sum / 16) must equal that sum
    at a vblank, the frames in the order of their vblanks (libpinmame passes a frame on when it changed)"""
    fails = []
    _, other = api_log(d)
    avail = [l for l in other if l.startswith('avail ')]
    if avail != ['avail 0 1 type 14 128x32 depth 4 length 0']:
        fails.append('display announced as %s, expected one 128x32 DMD of depth 4' % avail)
    raw, i, subs, cum = open(os.path.join(d, 'dmd.log'), 'rb').read(), 0, [], []
    while i + 4 <= len(raw):
        n = struct.unpack_from('<I', raw, i)[0]
        subs += [raw[i + 4 + 512 * k:i + 4 + 512 * (k + 1)] for k in range(n)]
        cum.append(len(subs))
        i += 4 + 512 * n
    dots = [bytes((v >> (7 - b)) & 1 for b in range(8)) for v in range(256)]   # a subframe byte as 8 dots of 0/1
    expanded, sums = {}, {}

    def window(k):
        """the dots' sums (one byte each) over the last 16 subframes the core had after vblank k"""
        if k not in sums:
            total = 0
            for j in range(max(0, cum[k] - 16), cum[k]):
                if j not in expanded:
                    expanded[j] = int.from_bytes(b''.join(dots[v] for v in subs[j]), 'big')
                total += expanded[j]                     # no carries: a byte sums at most 16 dots
            sums[k] = total.to_bytes(W * H, 'big')
        return sums[k]
    host = {False: [], True: []}
    raw, i = open(os.path.join(d, 'frames.bin'), 'rb').read(), 0
    while i + 4 <= len(raw):
        plugin = struct.unpack_from('<I', raw, i)[0] >= 0x80000000
        n = W * H * (4 if plugin else 1)
        host[plugin].append(raw[i + 4:i + 4 + n])
        i += 4 + n
    lum8 = bytes(min(255, int(255.0 * v / 16.0)) for v in range(256))   # sums 0-16
    for plugin in (False, True):
        frames = host[plugin]
        if plugin and not frames:
            continue
        path = 'plugin' if plugin else 'callback'
        conv = (lambda w: struct.pack('<%df' % (W * H), *(v / 16.0 for v in w))) if plugin else (lambda w: w.translate(lum8))
        k, good, lit = 0, 0, 0
        for x in frames:
            lit += any(x)
            hit = next((j for j in range(k, len(cum)) if conv(window(j)) == x), None)
            if hit is None:
                continue
            good += 1
            k = hit
        print('dmd: %s path: %d of %d frames (%d not blank) equal the sum of the last 16 subframes at a vblank, in order' % (path, good, len(frames), lit))
        if not frames or good != len(frames) or lit < len(frames) // 2:
            fails.append('%s path: %d of %d frames are not the sum of the last 16 subframes at a vblank' % (path, len(frames) - good, len(frames)))
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


def mech(on, off):
    fails = []
'''),
    (r'''    if a[:1] == ['media'] and len(a) == 3:
        sys.exit(media(a[1], a[2]))
    if a[:1] == ['mech'] and len(a) == 3:
        sys.exit(mech(a[1], a[2]))
''',
     r'''    if a[:1] == ['media'] and len(a) == 3:
        sys.exit(media(a[1], a[2]))
    if a[:1] == ['dmd'] and len(a) == 2:
        sys.exit(dmd(a[1]))
    if a[:1] == ['mech'] and len(a) == 3:
        sys.exit(mech(a[1], a[2]))
'''),
])
edit('tests/pinheck/vpx/pinmame_vpx.sh', [
    (r'''#!/bin/sh
# Domino's through libpinmame (spec M10 4.1): display, sound, outputs, switches and mechanics as a host receives them;
# with PINHECK_GAME=jetsons The Jetsons' 128x64 display and its sound only
: "${LIBPINMAME:?set LIBPINMAME to the built libpinmame.so}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
''',
     r'''#!/bin/sh
# Domino's through libpinmame (spec M10 4.1): display, sound, outputs, switches and mechanics as a host receives them;
# with PINHECK_GAME=jetsons The Jetsons' 128x64 display and its sound only; with amh America's Most Haunted's raw DMD
: "${LIBPINMAME:?set LIBPINMAME to the built libpinmame.so}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
'''),
    (r'''	rm -f $B/$name/*.log $B/$name/*.bin $B/$name/*.raw $B/$name/*.wav
	(cd $B/$name && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out.log PINHECK_FRAME_LOG=$PWD/frames.log PINHECK_WAV=$PWD/capture.wav \
		PINHECK_UART1_LOG=$PWD/uart.log timeout -k 30 3000 ../host "$@" > run.out 2>&1) || { echo "VPX FAIL: $name exited $?"; tail -5 $B/$name/run.out; exit 1; }
}
fail=0
''',
     r'''	rm -f $B/$name/*.log $B/$name/*.bin $B/$name/*.raw $B/$name/*.wav
	(cd $B/$name && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out.log PINHECK_FRAME_LOG=$PWD/frames.log PINHECK_WAV=$PWD/capture.wav \
		PINHECK_DMD_LOG=$PWD/dmd.log PINHECK_UART1_LOG=$PWD/uart.log timeout -k 30 3000 ../host "$@" > run.out 2>&1) || { echo "VPX FAIL: $name exited $?"; tail -5 $B/$name/run.out; exit 1; }
}
fail=0
'''),
    (r'''launch boot $GAME 1200 .
python3 vpx.py displays $B/boot || fail=1
python3 vpx.py media $B/boot $B/boot/capture.wav || fail=1
if [ $GAME = jetsons ]; then
''',
     r'''launch boot $GAME 1200 .
python3 vpx.py displays $B/boot || fail=1
if [ $LOOK = dmd ]; then
	# the DMD's frames through the callback and the plugin path, with a clip playing
	python3 vpx.py dmd $B/boot || fail=1
	PINHECK_UART1_SEND_AT=10 PINHECK_UART1_SEND="[E96000]~~[V00$CLIP]" launch media -p $GAME 1200 .
	grep -aq "Playing Video" $B/media/uart.log || { echo "VPX FAIL: [V00$CLIP] not acknowledged"; fail=1; }
	python3 vpx.py dmd $B/media || fail=1
	[ $fail -eq 0 ] || exit 1
	echo "pinmame vpx: ok ($GAME: the raw DMD)"
	exit 0
fi
python3 vpx.py media $B/boot $B/boot/capture.wav || fail=1
if [ $GAME = jetsons ]; then
'''),
])
EOF
```

- [ ] **Step 2: Build libpinmame, the check**

```sh
mkdir -p build/libpinmame-src && ln -sfn ../../cmake build/libpinmame-src/cmake && ln -sfn ../../ext build/libpinmame-src/ext && ln -sfn ../../src build/libpinmame-src/src
cp cmake/libpinmame/CMakeLists.txt build/libpinmame-src/CMakeLists.txt
cmake -S build/libpinmame-src -B build/libpinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DBUILD_STATIC=OFF > build/libpinmame-cfg.log 2>&1
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
amh env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh
```
Expected (about 6 minutes: each launch runs until the host got 1,200 changed frames, about 90 s emulated):
```text
[100%] Built target pinmame_test
displays: boot announces 1 display(s), indices [0], updates [1200]
dmd: callback path: 1200 of 1200 frames (1200 not blank) equal the sum of the last 16 subframes at a vblank, in order
dmd: callback path: 1200 of 1200 frames (1200 not blank) equal the sum of the last 16 subframes at a vblank, in order
dmd: plugin path: 1200 of 1200 frames (1200 not blank) equal the sum of the last 16 subframes at a vblank, in order
pinmame vpx: ok (amh: the raw DMD)
```

- [ ] **Step 3: The standalone build still builds**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11c.log 2>&1; tail -1 build/sdl3pinmame/11c.log
```
Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 4: Commit**

```bash
git add src/wpc/core.c src/libpinmame/libpinmame.cpp src/win32com/ControllerDmdDevice.cpp tests/pinheck/vpx/host.cpp tests/pinheck/vpx/vpx.py tests/pinheck/vpx/pinmame_vpx.sh
git commit -m "pinheck: the raw DMD's 16 shades for VPinMAME, dmddevice and libpinmame; libpinmame's DMD frames checked against the subframes"
```

### Task 10: Names and the VBScript constants

**Files:**
- Modify: `src/wpc/pinheck_names.h`, `tests/pinheck/vpx/names.py`, `tests/pinheck/vpx/check.sh`

**Interfaces:**
- Consumes: `amh.c`'s constants.
- Produces: `pinheck_amh_switch_names`, `pinheck_amh_lamp_names`, `pinheck_amh_solenoid_names`; `names.py check SRC amh`, `names.py vbs SRC amh` (`amh.vbs`).

- [ ] **Step 1: Domino's script before the change**

Run: `python3 tests/pinheck/vpx/names.py vbs src > build/pinheck-before.vbs`

- [ ] **Step 2: The names**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck_names.h', [
    (r'''   lamps 66-68, which the firmware uses but no chart names. Servo levels 0-255 are 0-180 degrees (0.544-2.4 ms); servo 0
   is the Orbitty topper. The firmware never writes the external RGB LED (62-64). HandleMechanics bit 0 closes
   the flippers' end-of-stroke switches while their coils are on. */
#ifndef PINHECK_NAMES_H
#define PINHECK_NAMES_H
''',
     r'''   lamps 66-68, which the firmware uses but no chart names. Servo levels 0-255 are 0-180 degrees (0.544-2.4 ms); servo 0
   is the Orbitty topper. The firmware never writes the external RGB LED (62-64). HandleMechanics bit 0 closes
   the flippers' end-of-stroke switches while their coils are on.

   America's Most Haunted (pinheck_amh_*).
   Sources: AMH_Switch_Matrix_Production.pdf, AMH_Light_Matrix_Production.pdf and AMH-WIRE-TO-BOARD.pdf (coils, GI,
   servos, optos); the coil names are the firmware's solenoid test (AMH_V023). Numbering as above. The ghost light is
   on-board RGB LED 2 on 62-64 (the board has no external chain). The playfield GI is four circuits, 37-40, whose
   order the wiring chart does not give. Coils 2-6, 13 and 24 are UNUSED in the solenoid test; 7 and 8 are both its
   PROTO BG 1. Servo levels 0-255 are 0-180 degrees (0.544-2.4 ms). The switch chart leaves matrix switches 0-15, 21,
   24-27, 31, 44, 47, 56 and 58 unlabelled. HandleMechanics bit 0 closes the flippers' end-of-stroke switches
   while their coils are on. */
#ifndef PINHECK_NAMES_H
#define PINHECK_NAMES_H
'''),
    (r'''};

#endif
''',
     r'''};

static const pinheck_name_t pinheck_amh_switch_names[] = {
  {   1, "swCoinDoor",   "Coin Door (closed)" },
  {   2, "swUser",       "User 0" },
  {   3, "swRFlip",      "Right Flipper" },
  {   4, "swLFlip",      "Left Flipper" },
  {   5, "swBack",       "Back (red service button)" },
  {   6, "swEnter",      "Enter (green service button)" },
  {   7, "swCoin",       "Coin Mech" },
  {   8, "swTilt",       "Tilt" },
  {  31, "swWiki",       "Wiki" },
  {  32, "swTech",       "Tech" },
  {  33, "swGhost1",     "Ghost Target 1 (left)" },
  {  34, "swGhost2",     "Ghost Target 2 (middle)" },
  {  35, "swGhost3",     "Ghost Target 3 (right)" },
  {  37, "swScoop",      "Basement Right Scoop" },
  {  38, "swVUK",        "VUK Left Behind Door" },
  {  45, "swHotel",      "Hotel Path" },
  {  46, "swCall",       "Elevator Call Button" },
  {  47, "swPsychic",    "Psychic" },
  {  51, "swJumpMade",   "Balcony Jump Success" },
  {  52, "swJumpApp",    "Balcony Jump Approach" },
  {  53, "swPopPath",    "Pop Path / Jump Fail" },
  {  54, "swBaseUpper",  "Basement Upper" },
  {  55, "swBaseLower",  "Basement Lower" },
  {  56, "swPop0",       "Pop Bumper 0" },
  {  57, "swULOrbit",    "Upper Left Orbit" },
  {  58, "swLLOrbit",    "Lower Left Orbit" },
  {  61, "swRollO",      "O Rollover" },
  {  62, "swRollR",      "R Rollover" },
  {  63, "swRollB",      "B Rollover" },
  {  64, "swCar",        "Ball in Elevator Car" },
  {  66, "swPop2",       "Pop Bumper 2" },
  {  67, "swPop1",       "Pop Bumper 1" },
  {  71, "swLOutlane",   "Left Outlane" },
  {  72, "swLInlane",    "Left Inlane" },
  {  73, "swLSling",     "Left Sling" },
  {  74, "swLFlipEOS",   "Left Flipper EOS" },
  {  75, "swRFlipEOS",   "Right Flipper EOS" },
  {  76, "swRSling",     "Right Sling" },
  {  77, "swRInlane",    "Right Inlane" },
  {  78, "swROutlane",   "Right Outlane" },
  {  82, "swShooter",    "Shooter Lane" },
  {  84, "swTrough1",    "Trough Ball 1 (eject position)" },
  {  85, "swTrough2",    "Trough Ball 2" },
  {  86, "swTrough3",    "Trough Ball 3" },
  {  87, "swTrough4",    "Trough Ball 4" },
  {  88, "swDrain",      "Drain" },
  {  94, "swStart",      "Start Button" },
  {  95, "swLoopOpto",   "Ghost Loop Opto" },
  {  96, "swDoorOpto",   "Spooky Door Opto" },
  { 112, "swLRFlip",     "Right Flipper button (reaches switch 3)" },
  { 114, "swLLFlip",     "Left Flipper button (reaches switch 4)" },
  { 0 }
};

static const pinheck_name_t pinheck_amh_lamp_names[] = {
  { 11, "lWiki",           "Wiki" },
  { 12, "lTech",           "Tech" },
  { 13, "lMinionMaster",   "Minion Master" },
  { 14, "lPrison1",        "Prison 1" },
  { 15, "lPrison2",        "Prison 2" },
  { 16, "lPrison3",        "Prison 3" },
  { 17, "lPrisonLock",     "Prison Lock" },
  { 18, "lLOrbitCamera",   "Left Orbit Camera" },
  { 21, "lDoor1",          "Door 1" },
  { 22, "lDoor2",          "Door 2" },
  { 23, "lDoor3",          "Door 3" },
  { 24, "lDoctorGhost",    "Doctor Ghost" },
  { 25, "lTheaterGhost",   "Theater Ghost" },
  { 26, "lFightDemon",     "Fight Demon" },
  { 27, "lDoorCamera",     "Door Camera" },
  { 28, "lExtraBall",      "Extra Ball" },
  { 31, "lJackpot",        "Jackpot" },
  { 32, "lGhostTarget1",   "Ghost Target 1" },
  { 33, "lGhostTarget2",   "Ghost Target 2" },
  { 34, "lGhostTarget3",   "Ghost Target 3" },
  { 35, "lEVP",            "EVP" },
  { 36, "lAdvanceFort",    "Advance Fort" },
  { 37, "lAdvanceBar",     "Advance Bar" },
  { 38, "lPopCamera",      "Pop Camera" },
  { 41, "lElevatorUp",     "Elevator Up" },
  { 42, "lElevatorDown",   "Elevator Down" },
  { 43, "lHotel1",         "Hotel 1" },
  { 44, "lHotel2",         "Hotel 2" },
  { 45, "lHotel3",         "Hotel 3" },
  { 46, "lHotelGhost",     "Hotel Ghost" },
  { 47, "lLock",           "Lock" },
  { 48, "lHotelCamera",    "Hotel Camera" },
  { 51, "lO",              "O" },
  { 52, "lR",              "R" },
  { 53, "lB",              "B" },
  { 55, "lTheater1",       "Theater 1" },
  { 56, "lTheater2",       "Theater 2" },
  { 57, "lTheater3",       "Theater 3" },
  { 58, "lTheaterCamera",  "Theater Camera" },
  { 61, "lULFlasher",      "Upper Left Flasher" },
  { 62, "lHellFlasher",    "Hell Flasher" },
  { 63, "lScoopFlasher",   "Scoop Flasher" },
  { 64, "lPhotoHunt",      "Photo Hunt" },
  { 65, "lArmyGhost",      "Army Ghost" },
  { 66, "lBarGhost",       "Bar Ghost" },
  { 67, "lSpiritGuide",    "Spirit Guide" },
  { 68, "lBasementCamera", "Basement Camera" },
  { 71, "lBossesBeaten",   "Bosses Beaten" },
  { 72, "lMultiballDone",  "Multiball Complete" },
  { 73, "lPhotoAce",       "Photo Ace" },
  { 74, "lPsychic",        "Psychic" },
  { 75, "lG",              "G" },
  { 76, "lL",              "L" },
  { 77, "lI",              "I" },
  { 78, "lGlirR",          "R (GLIR)" },
  { 81, "lSpookAgain",     "Spook Again (not lit by the lamp test's ALL ON)" },
  { 82, "lHospitalMode",   "Hospital Mode" },
  { 83, "lTheaterMode",    "Theater Mode" },
  { 84, "lFortMode",       "Fort Mode" },
  { 85, "lBarMode",        "Bar Mode" },
  { 86, "lHotelMode",      "Hotel Mode" },
  { 87, "lPrisonMode",     "Prison Mode" },
  { 88, "lDemonBattle",    "Demon Battle" },
  { 91, "lStart",          "Start Button" },
  { 0 }
};

static const pinheck_name_t pinheck_amh_solenoid_names[] = {
  {  1, "sMagnet",    "Loop Magnet (under the ghost)" },
  {  7, "sProtoBG1",  "Proto BG 1 (backglass prototype output)" },
  {  8, "sProtoBG2",  "Proto BG 1 (second output of the same test item)" },
  {  9, "sLSling",    "Left Sling" },
  { 10, "sRSling",    "Right Sling" },
  { 11, "sScoop",     "Scoop Kick" },
  { 12, "sVUK",       "VUK (behind the Spooky Door)" },
  { 14, "sPop0",      "Pop Bumper 0" },
  { 15, "sPop1",      "Pop Bumper 1" },
  { 16, "sPop2",      "Pop Bumper 2" },
  { 17, "sRFlipHigh", "Right Flipper High" },
  { 18, "sRFlipHold", "Right Flipper Hold" },
  { 19, "sLFlipHigh", "Left Flipper High" },
  { 20, "sLFlipHold", "Left Flipper Hold" },
  { 21, "sLoad",      "Ball Load (trough)" },
  { 22, "sDrainKick", "Drain Kicker" },
  { 23, "sLaunch",    "Autoplunger" },
  { 25, "sGI0",       "GI_0 backbox (dark in attract mode)" },
  { 26, "sGI1",       "GI_1 backbox (dark in attract mode)" },
  { 27, "sGI2",       "GI_2 backbox (dark in attract mode)" },
  { 28, "sGI3",       "GI_3 backbox (dark in attract mode)" },
  { 29, "sGI4",       "GI_4 backbox (dark in attract mode)" },
  { 30, "sGI5",       "GI_5 backbox (dark in attract mode)" },
  { 31, "sGI6",       "GI_6 backbox (dark in attract mode)" },
  { 32, "sGI7",       "GI_7 backbox (dark in attract mode)" },
  { 37, "sGI8",       "GI_8 playfield GI 1 of 4 (lower red, mid yellow, upper purple, scoop: not assigned)" },
  { 38, "sGI9",       "GI_9 playfield GI 2 of 4 (lower red, mid yellow, upper purple, scoop: not assigned)" },
  { 39, "sGI10",      "GI_10 playfield GI 3 of 4 (lower red, mid yellow, upper purple, scoop: not assigned)" },
  { 40, "sGI11",      "GI_11 playfield GI 4 of 4 (lower red, mid yellow, upper purple, scoop: not assigned)" },
  { 41, "sGI12",      "GI_12 (no wire on the chart)" },
  { 42, "sGI13",      "GI_13 (no wire on the chart)" },
  { 43, "sGI14",      "GI_14 (no wire on the chart)" },
  { 44, "sGI15",      "GI_15 (no wire on the chart)" },
  { 51, "sRGB1R",     "RGB1 red" },
  { 52, "sRGB1G",     "RGB1 green" },
  { 53, "sRGB1B",     "RGB1 blue" },
  { 54, "sRGB2R",     "RGB2 red" },
  { 55, "sRGB2G",     "RGB2 green" },
  { 56, "sRGB2B",     "RGB2 blue" },
  { 57, "sHell",      "Servo 0: Hellevator (car down 10, up 160 degrees)" },
  { 58, "sDoor",      "Servo 1: Spooky Door (open 5, closed 90 degrees)" },
  { 59, "sGhost",     "Servo 2: Ghost (left 10, middle 90, right 170 degrees)" },
  { 60, "sTarget",    "Servo 3: Target (up 5, down 160 degrees)" },
  { 61, "sServo4",    "Servo 4" },
  { 62, "sGhostR",    "Ghost light red (on-board RGB LED 2)" },
  { 63, "sGhostG",    "Ghost light green (on-board RGB LED 2)" },
  { 64, "sGhostB",    "Ghost light blue (on-board RGB LED 2)" },
  { 0 }
};

#endif
'''),
])
edit('tests/pinheck/vpx/names.py', [
    (r'''  names.py vbs SRC [GAME]     the system script a table loads with LoadVPM (to stdout): pinheck.vbs for dominos,
                              GAME.vbs (rzspook.vbs, ...) for the others, which say so in their first line
GAME is dominos (the default), rzspook or jetsons."""
import re
import sys
''',
     r'''  names.py vbs SRC [GAME]     the system script a table loads with LoadVPM (to stdout): pinheck.vbs for dominos,
                              GAME.vbs (rzspook.vbs, ...) for the others, which say so in their first line
GAME is dominos (the default), rzspook, jetsons or amh."""
import re
import sys
'''),
    (r'''TABLES = ('switch', 'lamp', 'solenoid')
TITLE = {'dominos': "Domino's Spectacular Pinball Adventure", 'rzspook': "Rob Zombie's Spookshow International",
         'jetsons': 'The Jetsons'}
# the ids of servo 0 and the external LED's first channel
ROLES = {'dominos': ('sNoid', 'sExtR'), 'rzspook': ('sGate', 'sLDGR'), 'jetsons': ('sOrbitty', 'sExtR')}


''',
     r'''TABLES = ('switch', 'lamp', 'solenoid')
TITLE = {'dominos': "Domino's Spectacular Pinball Adventure", 'rzspook': "Rob Zombie's Spookshow International",
         'jetsons': 'The Jetsons', 'amh': "America's Most Haunted"}
# the ids of servo 0 and the external LED's first channel
ROLES = {'dominos': ('sNoid', 'sExtR'), 'rzspook': ('sGate', 'sLDGR'), 'jetsons': ('sOrbitty', 'sExtR'), 'amh': ('sHell', 'sGhostR')}


'''),
])
edit('tests/pinheck/vpx/check.sh', [
    (r'''python3 names.py check $S rzspook || fail=$((fail + 1))
python3 names.py check $S jetsons || fail=$((fail + 1))
if command -v x86_64-w64-mingw32-gcc > /dev/null; then python3 mingw_check.py $S/libpinmame/libpinmame.cpp x86_64-w64-mingw32-gcc || fail=$((fail + 1)); else echo "vpx: MinGW compiler missing, strcasecmp check skipped"; fi
printf '#include "pinheck_names.h"\nint main(void) { return pinheck_dominos_switch_names[0].num != 1 || pinheck_rzspook_switch_names[0].num != 1 || pinheck_jetsons_switch_names[0].num != 1; }\n' > $B/names.c
cc -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$S/wpc $B/names.c -o $B/names && ./$B/names || { echo "NAMES FAIL: pinheck_names.h"; fail=$((fail + 1)); }
# per-game data: Domino's values only for the system set and Domino's; every other game spells out its own
''',
     r'''python3 names.py check $S rzspook || fail=$((fail + 1))
python3 names.py check $S jetsons || fail=$((fail + 1))
python3 names.py check $S amh || fail=$((fail + 1))
if command -v x86_64-w64-mingw32-gcc > /dev/null; then python3 mingw_check.py $S/libpinmame/libpinmame.cpp x86_64-w64-mingw32-gcc || fail=$((fail + 1)); else echo "vpx: MinGW compiler missing, strcasecmp check skipped"; fi
printf '#include "pinheck_names.h"\nint main(void) { return pinheck_dominos_switch_names[0].num != 1 || pinheck_rzspook_switch_names[0].num != 1 || pinheck_jetsons_switch_names[0].num != 1 || pinheck_amh_switch_names[0].num != 1; }\n' > $B/names.c
cc -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$S/wpc $B/names.c -o $B/names && ./$B/names || { echo "NAMES FAIL: pinheck_names.h"; fail=$((fail + 1)); }
# per-game data: Domino's values only for the system set and Domino's; every other game spells out its own
'''),
    (r'''	echo "GAMEDATA FAIL: Domino's values (PINHECK_DOMINOS_DATA) in '$dd', or INIT_PINHECK without a data argument"; fail=$((fail + 1))
fi
for g in rzspook jetsons; do
	python3 names.py vbs $S $g | head -1 | grep -q "$g.vbs" && echo "vbs: the $g script names $g.vbs" || { echo "VBS FAIL: the $g script does not name $g.vbs"; fail=$((fail + 1)); }
done
''',
     r'''	echo "GAMEDATA FAIL: Domino's values (PINHECK_DOMINOS_DATA) in '$dd', or INIT_PINHECK without a data argument"; fail=$((fail + 1))
fi
for g in rzspook jetsons amh; do
	python3 names.py vbs $S $g | head -1 | grep -q "$g.vbs" && echo "vbs: the $g script names $g.vbs" || { echo "VBS FAIL: the $g script does not name $g.vbs"; fail=$((fail + 1)); }
done
'''),
])
EOF
```

- [ ] **Step 3: The checks**

```sh
python3 tests/pinheck/vpx/names.py vbs src | cmp - build/pinheck-before.vbs && echo "pinheck.vbs: unchanged"
python3 tests/pinheck/vpx/names.py vbs src amh | grep -c '^Const'
nice -n 15 tests/pinheck/vpx/check.sh | grep -v selftest
```
Expected:
```text
pinheck.vbs: unchanged
162
names dominos: 46 switches, 48 lamps, 47 solenoid outputs; 39 sim constants agree
names rzspook: 53 switches, 49 lamps, 50 solenoid outputs; 53 sim constants agree
names jetsons: 44 switches, 36 lamps, 46 solenoid outputs; 40 sim constants agree
names amh: 51 switches, 64 lamps, 47 solenoid outputs; 50 sim constants agree
mingw: strcasecmp kept
gamedata: Domino's values only in the system set and dominos.c (4 games defined)
vbs: the rzspook script names rzspook.vbs
vbs: the jetsons script names jetsons.vbs
vbs: the amh script names amh.vbs
builds: 10 build lists carry the same 40 pinHeck sources
scale: 2x2 dots in PinMAME and VPinMAME windows, the panel (128x32, The Jetsons' 128x64) as sent to libpinmame hosts
vpx: LIBPINMAME or PINMAME_ROMS not set: libpinmame's display list and getters not run
vpx: 0 failed
```

- [ ] **Step 4: Commit**

```bash
git add src/wpc/pinheck_names.h tests/pinheck/vpx/names.py tests/pinheck/vpx/check.sh
git commit -m "pinheck_names.h: America's Most Haunted's switch, lamp and output names; names.py amh"
```

### Task 11: Speed

**Files:**
- Modify: `tests/pinheck/perf/bench.sh`

**Interfaces:**
- Consumes: Plan 11a's `bench.sh` by game; Plan 9d's `pgo.sh` steps.
- Produces: `bench.sh` workloads for `amh`: `attract`, `video` (`[E96000]`, `[V00D01]`, a 34 s clip, music and five sounds, from 10 s) and `play` (coin, start, the plunger, a shot every 1.5 s from 15 s, timed from 25 s for 55 s), its runs under `tests/pinheck/perf/build/amh`.

- [ ] **Step 1: America's Most Haunted's workloads**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/perf/bench.sh', [
    (r'''#                                  0.8x, - its speed. stall: throttled, the process stopped 0.3 s at 10 s emulated,
#                                  and the worker must stay on
# PINHECK_GAME selects the game (tests/pinheck/games.sh); rzspook and jetsons have attract, video and play (a game
# started and played from the simulator's keys), each in build/GAME
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
''',
     r'''#                                  0.8x, - its speed. stall: throttled, the process stopped 0.3 s at 10 s emulated,
#                                  and the worker must stay on
# PINHECK_GAME selects the game (tests/pinheck/games.sh); rzspook, jetsons and amh have attract, video and play (a game
# started and played from the simulator's keys), each in build/GAME
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
'''),
    (r'''}

# workload: frames to run, frame the timed window starts at, UART1 commands, keys
spec() {
''',
     r'''}

# America's Most Haunted's play: coin and start, the plunger, then a shot every 1.5 s from 15 s on (none to the
# Hellevator or the ghost loop, whose car and magnet hold the ball); the ball never drains
amh_play_keys() {
	printf '480 tap 6 KEYCODE_5\n540 tap 6 KEYCODE_1\n660 tap 40 KEYCODE_SPACE\n'
	i=0
	for k in B 'LCONTROL R' 'RCONTROL R' 'LCONTROL MINUS' 'RCONTROL MINUS' H V J W K M Z X C T Y U 'LCONTROL B' 'RCONTROL B' \
		'LCONTROL N' 'RCONTROL N' F S D B 'LCONTROL R' 'RCONTROL R' 'LCONTROL MINUS' 'RCONTROL MINUS' H V J W K M Z X C T Y U \
		'LCONTROL B' 'RCONTROL B' 'LCONTROL N' 'RCONTROL N' F S D; do
		printf '%d tap 1 KEYCODE_%s\n' $((900 + 90 * i)) "$(echo "$k" | sed 's/ / KEYCODE_/')"
		i=$((i + 1))
	done
}

# workload: frames to run, frame the timed window starts at, UART1 commands, keys
spec() {
'''),
    (r'''	jetsons/video) FRAMES=2400 MARK=960 SEND_AT=13 SEND='[E96000]~[V00WZA]~[F00ZM0]~[F00G00]~[F00J00]~[F00NAA]~~~~~~~~~~[F00SAU]~~~~[F00H00]' ;;
	jetsons/play) FRAMES=5100 MARK=1800 SEND_AT=13 SEND='[E97000]' KEYS=$(jet_play_keys) ;;
	*) echo "bench: no workload $1 for $GAME"; exit 2 ;;
	esac
''',
     r'''	jetsons/video) FRAMES=2400 MARK=960 SEND_AT=13 SEND='[E96000]~[V00WZA]~[F00ZM0]~[F00G00]~[F00J00]~[F00NAA]~~~~~~~~~~[F00SAU]~~~~[F00H00]' ;;
	jetsons/play) FRAMES=5100 MARK=1800 SEND_AT=13 SEND='[E97000]' KEYS=$(jet_play_keys) ;;
	amh/video) FRAMES=2400 MARK=960 SEND_AT=10 SEND='[E96000]~[V00D01]~[F00ZB1]~[F00AD2]~[F00B0O]~[F00CBG]~~~~~~~~~~[F00D1A]~~~~[F00EVG]' ;;
	amh/play) FRAMES=4800 MARK=1500 SEND_AT=7 SEND='[E97000]' KEYS=$(amh_play_keys) ;;
	*) echo "bench: no workload $1 for $GAME"; exit 2 ;;
	esac
'''),
])
EOF
```

- [ ] **Step 2: The speed without a profile, and the profile**

```sh
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video play
amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh profile attract > /dev/null
perf report -i tests/pinheck/perf/build/amh/prof/attract/perf.data --stdio -s symbol 2> /dev/null | grep -v '^#' | grep -v '^$' | head -8 | awk '{print $1, $3}'
```
Expected (about 15 minutes; speeds vary with the load):
```text
bench attract opt: 0.556x over 30.0 s emulated (53.9 s wall), whole run 22.9 G instructions and 3.1 s CPU per emulated s, worst 100 ms 0.436x, load 2.29
bench video opt: 0.472x over 24.0 s emulated (50.8 s wall), whole run 25.0 G instructions and 3.5 s CPU per emulated s, worst 100 ms 0.368x, load 2.06
bench play opt: 0.440x over 55.0 s emulated (125.1 s wall), whole run 27.4 G instructions and 3.9 s CPU per emulated s, worst 100 ms 0.229x, load 1.88
39.99% wait_head.lto_priv.0
13.92% run_local
8.57% worker
8.07% pic32cpu_execute
5.59% do_catch_up.lto_priv.0
4.60% flush.lto_priv.0
1.83% hub_rw
1.13% special_write.lto_priv.0
```

- [ ] **Step 3: The speed with a profile-guided build**

`pgo.sh`'s steps with the machine's locks (`pgo.sh` itself would run its training outside `emu.lock`): an instrumented build, America's Most Haunted's attract and video workloads as training, the build with the profile, then the three workloads:

```sh
P=$PWD/build/pgo-profile; rm -rf "${P:?}"
for m in gen use; do
	M=$(echo $m | tr a-z A-Z); c=; [ $m = use ] && c=--clean-first
	cmake -S . -B build/pgo-$m -DCMAKE_BUILD_TYPE=Release -DPINMAME_PGO=$M -DPINMAME_PGO_DIR="$P" -DPLATFORM=linux -DARCH=x64 > build/pgo-$m.cmake.log 2>&1
	flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 3600 cmake --build build/pgo-$m -j6 $c > build/pgo-$m.log 2>&1; tail -1 build/pgo-$m.log
	[ $m = gen ] && amh env SDL3PINMAME=build/pgo-gen/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video > build/pgo-train.log 2>&1
done
amh env SDL3PINMAME=build/pgo-use/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video play
```
Expected (about 20 minutes):
```text
[100%] Built target sdl3pinmame
[100%] Built target sdl3pinmame
bench attract opt: 0.610x over 30.0 s emulated (49.2 s wall), whole run 21.1 G instructions and 2.8 s CPU per emulated s, worst 100 ms 0.439x, load 2.83
bench video opt: 0.535x over 24.0 s emulated (44.8 s wall), whole run 23.2 G instructions and 3.1 s CPU per emulated s, worst 100 ms 0.424x, load 2.27
bench play opt: 0.491x over 55.0 s emulated (112.1 s wall), whole run 25.3 G instructions and 2.9 s CPU per emulated s, worst 100 ms 0.244x, load 2.00
```

The speed depends on the load: a second run at loads 2.2–3.5 gave 0.554×, 0.469× and 0.436× with the same instruction counts.

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/perf/bench.sh
git commit -m "pinheck bench: America's Most Haunted's attract, video and play"
```

### Task 12: Regression, the tests bite, the roadmap

**Files:**
- Modify: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

- [ ] **Step 1: The other games**

Domino's, Rob Zombie and The Jetsons byte-identical with video, their service tests and simulators, Domino's scripted game and libpinmame check, the look checks, the link suite:

```sh
REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video | grep determinism
rz env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video | grep determinism
jet env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video | grep determinism
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh | tail -1
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh | tail -1
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh | tail -1
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh | tail -1
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh | tail -1
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/game/pinmame_game.sh | tail -2
LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | tail -1
jet env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | tail -1
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_look.sh | tail -1
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh | tail -1
PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 tests/pinheck/link/check.sh | tail -1
PINHECK_SKIP_FIRMWARE=1 nice -n 15 tests/pinheck/board/check.sh | tail -1
```
Expected (about 30 minutes):
```text
determinism first boot: identical nvram.base/dominos.nv
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
determinism first boot: identical nvram.base/rzspook.nv
determinism attract: identical uart.log frames.bin snd.wav nvram/rzspook.nv
determinism video: identical uart.log frames.bin snd.wav nvram/rzspook.nv
determinism first boot: identical nvram.base/jetsons.nv
determinism attract: identical uart.log frames.bin snd.wav nvram/jetsons.nv
determinism video: identical uart.log frames.bin snd.wav nvram/jetsons.nv
pinmame board: ok
pinmame board: ok
pinmame board: ok
pinmame sim: ok
pinmame sim: ok
game: 44 checks, 0 failures
pinmame game: ok
pinmame vpx: ok
pinmame vpx: ok (jetsons: display and sound)
pinmame look: ok
pinmame display: ok
link: 0 failed
board: 0 failed
```

- [ ] **Step 2: The tests bite**

Apply each mutation, rebuild (about a minute), run the named check with `amh`, confirm the named failure line (other failure lines may follow), then restore the file. Rebuild once more at the end.

```sh
mut() { f=$1; python3 -c "import sys; p=sys.argv[1]; s=open(p).read(); a,b=sys.argv[2],sys.argv[3]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" "$f" "$2" "$3" || return
	flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11c.log 2>&1; tail -1 build/sdl3pinmame/11c.log
	shift 3; amh env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 "$@" 2>&1 | grep -m3 FAIL; git checkout $f; }
A=src/wpc/sims/pinheck/amh.c
```

```sh
mut $A '  128, 32, 0, 544, 2400, 0, 0, 0x5B0C, 5000' '  128, 32, 0, 544, 2400, 0, 0, 0x5B0C, 3000' tests/pinheck/pic32mx/pinmame_check.sh
mut src/wpc/pinheck/dmd.c '		const uint32_t start = d->nbits % DMD_W; /* the oldest of the last 128 bits */' '		const uint32_t start = (d->nbits + 1) % DMD_W;' tests/pinheck/display/pinmame_display.sh
mut $A '  128, 32, 0, 544, 2400, 0, 0, 0x5B0C, 5000' '  128, 32, 0, 1000, 2000, 0, 0, 0x5B0C, 5000' tests/pinheck/sim/pinmame_sim.sh
mut src/wpc/pinheck.c '#define PINHECK_ONB_LEDS 3' '#define PINHECK_ONB_LEDS 2' tests/pinheck/board/pinmame_board.sh
```

The subframe hand-off is checked through libpinmame, so that mutation rebuilds libpinmame:

```sh
mutl() { f=$1; python3 -c "import sys; p=sys.argv[1]; s=open(p).read(); a,b=sys.argv[2],sys.argv[3]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" "$f" "$2" "$3" || return
	flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
	shift 3; amh env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 "$@" 2>&1 | grep -m3 FAIL; git checkout $f; }
mutl src/wpc/pinheck.c '	for (; dmd_tail != dmd_head; dmd_tail++) core_dmd_submit_frame(' '	for (dmd_tail += dmd_tail != dmd_head; dmd_tail != dmd_head; dmd_tail++) core_dmd_submit_frame(' tests/pinheck/vpx/pinmame_vpx.sh
```

| Mutation | Change | Caught by |
|---|---|---|
| `bootHold`: 3 s, as the other games | `amh.c` `…, 0x5B0C, 5000` → `…, 0x5B0C, 3000` | `PINMAME FAIL: the Propeller's display never left its start screen (GR:10)` |
| dot order: the shift register read one bit off | `dmd.c` `start = d->nbits % DMD_W` → `(d->nbits + 1) % DMD_W` | `PINMAME FAIL: no proof line, under 10000 subframes, or the row model differs by more than one row in 1% of them`, then `frames: FAIL, raw DMD cycles at hub addresses ['none'], 22 of 914 whole` |
| servo range: Domino's | `amh.c` `544, 2400` → `1000, 2000` | `SIM FAIL: servo 0 in attract: [644, 646, 648] us and output 57 at 0; expected 647 us and 14` |
| ghost light: two on-board LEDs | `pinheck.c` `PINHECK_ONB_LEDS 3` → `2` | `BOARD FAIL: RGB test at 142.9: (0, 0, 0, 0, 0, 0, 0, 0, 0), expected (0, 0, 0, 0, 0, 0, 255, 0, 0)` |
| hand-off: one subframe a vblank lost | `pinheck.c` `for (; dmd_tail != dmd_head; …` → `for (dmd_tail += dmd_tail != dmd_head; …` | `VPX FAIL: callback path: 1013 of 1200 frames are not the sum of the last 16 subframes at a vblank` (the display check's still screens hide it: every shade keeps one pen) |

Then rebuild both once more; `git status --short | grep -v '^??'` prints nothing.

- [ ] **Step 3: The roadmap**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    (r'''| release | release:''',
     r'''| `2026-10-01-pinheck-11c-amh.md` | 11c: America's Most Haunted (`amh`): PIC32 firmware from the romset's Intel HEX (`hexload.c`, converted at driver init); the raw 128×32 DMD scanned by a Propeller cog, decoded from its pins (`dmd.c`) and integrated to 16 shades by a new core PWM filter (`PINHECKDMD`, palette video; `GEN_PINHECK` in the 16-shade lists of VPinMAME, dmddevice and libpinmame); game data `dmdHub` and `bootHold` (AMH's application 5 s after a reset); the ghost light as on-board RGB LED 2 (62–64; no inverted lines); game, simulator, names; the checks for a game without an update and for a raw DMD | Plan 11b | none | `amh` passes the boot, romset (corrupt HEX), simulator, service-test, display (`AB1.VID` pixel-exact; the row model differs in single rows only) and libpinmame (every frame the sum of the last 16 subframes) checks; Domino's, Rob Zombie and The Jetsons byte-identical (`bench.sh` with `REFERENCE`, the scripted game's score 32176700) | written |
| release | release:'''),
    (r'''- After Plan 11b: `jetsons` runs below real time without a profile:''',
     r'''- After Plan 11c: `amh` runs at about half real time: 0.56× in attract mode, 0.47× with video and 0.44× in play without a profile, 0.49–0.61× with a profile-guided build trained on its attract and video workloads (8% fewer instructions; `bench.sh`, loads 1.9–3.5), with 22.9 G host instructions per emulated second in attract mode against Domino's 12.6 G. Its display cog never idles (3.2 million dots a second, three OUTA writes each); the PIC32 thread waits for the Propeller (`wait_head` 40% of the profile), whose threads spend it in `run_local`, translated cog code, `do_catch_up` and the pin change points; the DMD decoder is 1.3%. The candidates (Plan 11c Ruling 14): a high-level model of the scan cog proven against the pin decoder, a translated fast path for OUTA-only cog loops, Plan 9d's remaining Propeller items. America's Most Haunted's scripted game is open, as Rob Zombie's.
- After Plan 11b: `jetsons` runs below real time without a profile:'''),
    (r'''- The machine checks take `PINHECK_GAME` (`tests/pinheck/games.sh`,''',
     r'''- After Plan 11c: every field of `pinheck_tGameData` is read (`rgbInverted` stays 0 and unsupported: AMH's chain is not inverted, Plan 11c Ruling 8), with two more, `dmdHub` and `bootHold`; `games.sh` gains `UPDATE_END=none` (a game converted from its Intel HEX) and `LOOK=dmd`.
- The machine checks take `PINHECK_GAME` (`tests/pinheck/games.sh`,'''),
])
EOF
```

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "docs: roadmap row for Plan 11c and what it carries"
```

## Windows checklist

For the Windows session (VS2022 BuildTools / MSVC 14.44, `C:\code\dominos`), after Plan 11c is pushed. Results come back by message; a Windows-only fix comes back as a diff with a test, applied on Linux. Never push. Never play sound on a device the user is listening to unless the user asks; muted or the silent device is fine for every item but 6.

Setup: `roms\amh.zip` = `C:\code\spooky\America's Most Haunted\AMH_SD_V023.zip` copied under that name with `AMH_V023.hex` from the same folder added at its root (7-Zip: `7z a roms\amh.zip "C:\code\spooky\America's Most Haunted\AMH_V023.hex"`; 874,644,371 bytes afterwards), `roms\pinheck.zip` as before. A fresh `nvram\` and `cfg\` directory for item 2.

1. **Build.** MSVC x64 standalone PinMAME, VPinMAME64 and libpinmame at the Plan 11c commit, exactly as CI does. Expected: all build; no warning in `amh.c`, `pinheck.c`, `dmd.c`, `hexload.c`, `bootldr.c`, `pinheckgames.c`; `core.c`'s and `libpinmame.cpp`'s changes compile; `PinMAME.exe -listfull amh` lists `amh` as "America's Most Haunted".
2. **First boot.** `PinMAME.exe amh` with the empty `nvram\`. Expected: no update and no restart: for about 5 s the DMD shows `AMH - SPOOKY PIN` / `CODE VERSION: 23` / `A/V VERSION: 23` (the version screen), then attract mode with clips, high scores and text. With `PINHECK_UART1_LOG=uart.log`: `New code detected, storing new game defaults... done!`, the banner `Game:AMH - SPOOKY PIN` / `Version:023`. Close and start again: the same without `New code detected`.
3. **The display.** A 128×32 DMD in PinMAME's DMD colours, 16 shades, no flicker, crisp at the window's DMD size; the clips move smoothly (15 frames a second). In the service menu (`0`, it opens at MAIN SETTINGS): a still menu screen. Screenshot of attract mode and of the menu.
4. **Service tests.** `0`, three Right Shifts to SWITCH EDGE, two more to SOLENOID: the test opens at RFLIP HIGH and `0` fires each coil (the core's solenoid row shows it); SERVO TEST moves the outputs 57–60 (DOOR OPEN … HELL DOWN); LAMP TEST steps through the GI and all lamps; RGB TEST lights GHOST (62–64), RGB1 (51–53) and RGB2 (54–56) red, green, blue in turn; SWITCH TEST after `Del` (simulator keys off) reacts to the column/row keys (`Q`–`I` × `A`–`K`) and shows the trough switches of the balls at rest closed (59–62; the solenoid test's BALL LOAD and AUTOPLUNGER launch one). Back with `7`.
5. **Keyboard play.** `5` coin, `1` start, hold `Space` (the plunger): the ball leaves the shooter lane; `B`, `Ctrl`+`R`, `Ctrl`+`-`, `H`, `J`, `W`, `K`, `M`, `Z`/`X`/`C`, `T`/`Y`/`U` score; `S` (scoop) and `D` (Spooky Door) are kicked out by the firmware; `Q` drains, the drain kicker returns the ball to the trough and the next ball is served. Report anything the firmware complains about on the display.
6. **Sound** (unmuted, if the user agrees): music and call-outs in attract mode and in the game; no drift over a minute.
7. **Speed.** `PinMAME.exe amh` unthrottled (`-nothrottle`) for 60 s of attract mode with `PINHECK_TIME_LOG=time.log`: the last line's emulated over host seconds. Linux gives about 0.55×; the Windows box was 0.67–0.72× of Linux for Domino's.
8. **VPinMAME smoke test** (Route A of Milestone 10): the Domino's test table copied to `amh_test.vpx` with `Const cGameName = "amh"`, `roms\amh.zip` in VPinMAME's ROM folder. Expected: the VPinMAME DMD window shows the 128×32 DMD with 16 shades (not 4), sound plays, the lamp indicators follow the attract show; `table.log` has `route com`. For the names, `python tests\pinheck\vpx\names.py vbs src amh > amh.vbs`.
9. **libpinmame**: `pinmame_test.exe` (or the M10 host) on `amh`: one display, type 14 DMD, 128×32, depth 4.

## Result

Measured in the replay of this plan's text (a fresh worktree of `7bad62ec`, every step run as written; the machine rebooted during Task 9, which was resumed from its Step 2 after the libpinmame build's objects truncated by the reboot were rebuilt; the eleven commits then rebased onto `40f6f4df` without conflict, where the AMH boot check and the link suite pass) on the Ryzen 7 7730U while other agents' emulators and builds shared it (one emulator at a time, `emu.lock`); builds without a profile unless named; the load average is printed with every speed.

- **Task 2:** `AMH_V023.hex` converts to 243,952 bytes of program flash, an image of CRC32 `09d9072f` with the entry at 0x9D001000; each malformed record (checksum, digit, mark, short, type 02, outside flash, boot flash, no end, data after the end) is refused with its line.
- **Task 4:** Domino's (attract and video), Rob Zombie and The Jetsons (attract) byte-identical to `7bad62ec`: UART1, frames, sound and NVRAM.
- **Task 5:** `amh` boots from a blank NVRAM to attract mode without a restart (defaults stored, `1700BAFA` at EEPROM $8000), the second launch keeps them, and the Propeller's display leaves its version screen; a corrupt HEX stops the machine with `line 2: checksum`. Domino's, Rob Zombie and The Jetsons boot as before.
- **Task 6:** the simulator check: 21 checks, 0 failures (trough and drain kicker, the firmware's autoplunger, scoop and VUK ejects, the door, the Hellevator ride, the loop magnet, end-of-stroke switches, attract servos).
- **Task 7:** check 5: 106 console commands and 274 service-test steps, 0 failures.
- **Task 8:** the row model differs from the pin decoder in 36 of 14,640 subframes, one row each; `AB1.VID`'s 60 frames show pixel-exact, contiguous and in order; both snapshots show one DMD pen per shade, rising.
- **Task 9:** libpinmame: one 128×32 DMD of depth 4; 1,200 of 1,200 frames on the callback and on the plugin path equal the sum of the last 16 subframes the driver handed the core.
- **Task 11:** America's Most Haunted without a profile 0.556× in attract mode, 0.472× with video, 0.440× in play (22.9, 25.0, 27.4 G instructions per emulated second, loads 1.9–2.3; slowest 100 ms 0.44×, 0.37×, 0.23×); with the profile-guided build 0.610×, 0.535×, 0.491× (21.1, 23.2, 25.3 G, loads 2.0–2.8), and 0.554×, 0.469×, 0.436× in the replay after the machine's reboot (loads 2.2–3.5). Without a profile in that replay: 0.573×, 0.483×, 0.443×. The profile: `wait_head` 40%, `run_local` 14%, the worker loop 8.6%, the PIC32 8.1%, `do_catch_up` 5.6%, `flush` 4.6%, `hub_rw` 1.8%, `special_write` 1.1%.
- **Task 12:** Domino's, Rob Zombie and The Jetsons byte-identical in attract mode and with video; their check 5, simulators (Domino's in Task 6), Domino's scripted game (44 checks, 0 failures), the libpinmame checks, the look and display checks and the link and board suites pass. Each of the five mutations is caught by the check named.
