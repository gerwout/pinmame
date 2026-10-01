# Plan 11b: The Jetsons (128×64 display) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The pinHeck driver runs a third game, The Jetsons (`jetsons`, `JET_V004.PRG` and `PRP_V002.BIN`), from its first-boot update through the restart to attract mode, the service tests, its 128×64 display pixel-exact in PinMAME's window and in libpinmame, and keyboard play through a playfield simulator; Plan 11a's review minors are folded in first; Domino's and Rob Zombie stay byte-identical.

**Architecture:** The display decoder (`display.c`) gets the frame size from the game's `pinheck_tGameData` (`width`, `height`): `pinheck_display_size()` accepts the 128×32 and the 128×64 module and nothing else. The driver keeps a frame of up to 8,192 bytes, and its video draws a 128×64 frame 2×2 per dot as sent; the 128×32 module keeps its look. A game's `CORE_VIDEO` layout gives the window (256×128 for The Jetsons) and libpinmame's display size (128×64, because `PINHECK_VIDEO_SCALE` is 1 there). `src/wpc/sims/pinheck/jetsons.c` holds the game, as `rzspook.c` does: ROMs, input ports, the game data (128×64, servo range 544–2,400 µs) and a simulator for the 3-ball trough with its optos, the autolauncher, the scoop, the kickout hole, the orbits with the up-post, and the flippers' end-of-stroke switches. The machine checks take the game from `tests/pinheck/games.sh`, which gains each game's Propeller image, how its update ends, its Propeller reboots before the first sync, its look and its trough switch; the frame check, check 5, the libpinmame check and `bench.sh` read the module size or the game's menu from `PINHECK_GAME`; the simulator check is new (`jetsim.py`). `pinheck_names.h` gains a `jetsons` section.

**Tech Stack:** C (C89-syntax-clean driver; `-std=c99 -pedantic -Wall -Werror` for the simulator file as Plan 8b), Python 3 (no third-party modules), POSIX sh.

**Spec:** the parent spec `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§4.5 numbering, §6 romsets, §7 checks) and its addenda (`2026-09-29-pinheck-m6-m7-display-audio-design.md`: the display link and exact pixels; `2026-09-29-pinheck-m8-m9-design.md` §3.3 simulator; `2026-09-29-pinheck-m10-vpx-design.md`: libpinmame export, names), applied to a third game, and Plan 11a (`docs/superpowers/plans/2026-10-01-pinheck-11a-rzspook.md`: `pinheck_tGameData`, `PINHECK_GAME`). The research behind this plan: `work/games/jetsons/research.md` (firmware, display, update path, numbering, romset, mechanics). The review of Plan 11a asked for four fixes, which are Task 1.

## Prerequisites

- Plan 11a executed and merged: `pinheck` at the merge of `pinheck-11a` (`504d810f`); this plan was proven on `504d810f`, whose code the merge keeps.
- This plan edits the driver (`pinheck.[ch]`, `pinheck/display.[ch]`, `pinheck/bootldr.[ch]`, `pinheckgames.c`, `driver.c`), Domino's and Rob Zombie's game files (comments, Domino's data macro, an unused array), `pinheck_names.h`, and the checks of Plans 5–11a (`games.sh`; `pic32mx/pinmame_check.sh`, `romset_check.sh`; `sim/pinmame_sim.sh`; `board/check5.py`; `display/display_test.c`, `frames.py`, `render.py`, `pinmame_display.sh`, `pinmame_look.sh`; `vpx/names.py`, `vpx.py`, `check.sh`, `pinmame_vpx.sh`; `perf/bench.sh`; `link/register_build.py`), and adds `sims/pinheck/jetsons.c` and `tests/pinheck/sim/jetsim.py`. The CPU cores, the SoC, `prop.c`, the board, audio, storage and link devices are untouched.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export what Plan 11a's checks use (`TOOLS`, `P8X32A_ROM`, `PINHECK_UPDATE_DIR`, `PINHECK_ZIP`, `DOMINOS_PRP`, `DOMINOS_ZIP`, `RZSPOOK_ZIP`, `RZSPOOK_UPDATE_DIR`, `SDL_AUDIO_DRIVER=dummy`) and `JETSONS_ZIP` = `Jetsons_Code.zip` (351,227,479 bytes, SHA1 `ebb1741b2a61cbf6ec521d067918183dbe078c0a`) under the name `jetsons.zip` (a symlink will do), `JETSONS_UPDATE_DIR` = its `Jetsons/` folder unzipped (`JET_V004.PRG`, `PRP_V002.BIN`, `DMD/`, `SFX/`; 671 MB). Nothing from any update goes into the repository. A Jetsons or Rob Zombie run is the Domino's command with the game's variables in front; the steps define them once as shell functions:
  ```sh
  jet() { PINHECK_GAME=jetsons PINHECK_ZIP=$JETSONS_ZIP PINHECK_UPDATE_DIR=$JETSONS_UPDATE_DIR "$@"; }
  rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
  ```
- The machine is shared: only one emulator runs at a time machine-wide, so every step that runs PinMAME, libpinmame or a firmware program goes through `flock /code/spooky_domino/work/emu.lock` (on another machine drop the `flock` prefix); builds and heavy suites run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6`. Every PinMAME launch has a `timeout -k 30`, a private `-nvram_directory` and `-cfg_directory`, and no sound device (`SDL_AUDIO_DRIVER=dummy`). The emulation runs at 1.1–1.4× real time with `-nothrottle`; the times below are wall-clock with other agents' work beside them.

## Global Constraints

- Domino's and Rob Zombie are byte-identical: `bench.sh` with `REFERENCE` (the build of `504d810f`) requires their UART1 log, display frames, sound and NVRAM in attract mode and with video equal byte for byte (Tasks 2 and 8); every Domino's and Rob Zombie check passes with its earlier output.
- `pinheck_tGameData` stays the only per-game mechanism; Domino's values (`PINHECK_DOMINOS_DATA`) are for Domino's and the system set only, and every other game spells out its own.
- Numbering is Plan 8a's (spec §4.5): document switch *n* (0–63) = PinMAME `(n/8+1)*10 + n%8+1`; cabinet switch *n* (9–15) = *n* + 82; document coil *n* = PinMAME solenoid *n*+1; servo *n* = custom solenoid 57+*n*.
- The simulator follows Plan 8b's conventions: PinMAME's framework, coils read as "on during the last frame" (`coreGlobals.solenoids`), `autoBall`, keys tapped for one frame, every public name outside the file `pinheck_`-prefixed.
- The checks select the game with `PINHECK_GAME` (default `dominos`); with it unset every check runs Domino's exactly as before.
- Code comments short and factual; the driver C89-syntax-clean.

## Rulings

Decisions the research and the review left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it). Times are emulated seconds of PIC32 time; addresses are `JET_V004.PRG` flash addresses. The firmware is the authority for behaviour, the documents for names.

1. **The driver supports exactly two modules: 128×32 and 128×64.** `pinheck_display_size()` accepts `DISPLAY_SIZE_OK(w, h)` (width 128, height 32 or 64) and keeps 128×32 for anything else; `pinheck_unsupported()` names such a size, and an inverted WS2801 (`rgbInverted`), on stderr and in the error log at machine start (review minor 2). The decoder's buffer is 8,192 bytes; a transfer of any other length than the module's frame is discarded as before. Rejected: a general `w × h` (no third module exists; a size the driver has not drawn would pass silently). Cost if wrong: a later module needs one more size in the macro.
2. **The 128×64 module is drawn as sent, 2×2 per dot; the look stays the 128×32 module's.** JET's config packet is 12 bytes (`00 01 00 37 00 00 00 ff 00 80 00 40`: word 0 = 1, `POSITION` 55, shape 0, brightness 255, 128, 64; the cog sends three longs, so `BAR BRIGHT` never leaves), which `pinheck_display_look` does not decode (`display: unknown config packet, exact pixels`). Its menu offers only `POSITION` (10–120); what the module does with it is not known, so the frame is drawn unshifted. `aligned` is 55, the factory `POSITION`, for the record; the driver reads it only for a decoded look. `games.sh` says `LOOK=none`: the look check does not apply, and the display check asserts 12-byte packets and the exact-pixels line instead. Cost if wrong: if the module honours `POSITION` or a dot shape, PinMAME shows it unshifted and square.
3. **The window and libpinmame follow the game's layout.** `jetsons_disp` is `PINHECK_VIDEO_H64 × PINHECK_VIDEO_W`: 256×128 in PinMAME and VPinMAME (the core panel below it; the visible area stays 256×256) and 128×64 for libpinmame hosts (`PINHECK_VIDEO_SCALE` 1), whose display list announces `128x64 depth 16`. Domino's and Rob Zombie keep 256×64 and 128×32.
4. **Two discarded transfers at start are the firmware's, not the driver's.** The display cog's first latch follows a lone 1,024-byte burst (its burst counter starts at the last of the eight), and the Propeller's reboot before the sync (Ruling 6) cuts one frame after 7,168 bytes; the driver discards both. The rebooted cog starts with a lone 1,024-byte burst again, so a normal start discards 1,024, 7,168 and 1,024 bytes. The next frame follows 33 ms later. In 55 s of attract and service menu no other transfer was discarded. The decoder logs each discarded frame (`display: frame of 1024 bytes discarded`) up to `DISPLAY_LOG_FRAMES` (16) per machine start, then `display: further discarded frames not logged`. The display check asserts exactly 1,024, 7,168 and 1,024 in each launch for a game without a look, and no discarded frame for the 128×32 module.
5. **No 128×64 frame is whole in hub RAM; the frame check is the clip.** JET sends each frame from two 1,024-byte pages (`$5A10`, `$5E10`), so the frame log's hub address is `$FFFFFFFF` for every frame and `frames.py` asserts that for the 128×64 module. The check against the firmware is the `.VID` clip: `[V00TL1]` (`_DT/TL1.VID`, 45 frames; with `TL2` the only clips without two equal consecutive frames or a uniform frame) must show frames 0..44 pixel-exact, contiguous and in order.
6. **JET's update ends with the PIC32 held, and the boot check sees it at the restart.** V002 never sends `LEAVE_PROGMODE`: after `UPDATE COMPLETE` / `PLEASE RESTART` (first shown at 100.5 s) the stand-in bootloader keeps the PIC32 in programming mode until the machine restarts. `boot_stop()` logs `boot: stopped in programming mode, 197632 bytes programmed` when the machine stops in that state (`MACHINE_STOP`, which a restart passes through); Domino's and Rob Zombie leave first and log nothing new. The boot check restarts at 104 s (`UPDATED_AT`); after the restart the PIC32 syncs, stores its defaults on the blank U13 and goes on to its banner and attract mode in the same launch (Domino's stops once more there), and the second launch reaches attract mode with the banner `Game: JET - JETSONS`, the stored word `400BAFA` and `Version: ` with nothing after it.
7. **The Propeller reboots once before every first sync, and the boot converges.** V002's main loop reboots the chip (`CLKSET $80`, which `prop.c` turns into a restart) after 10,000 iterations without a sync. With the stand-in's 3 s hold the PIC32's application starts at 3.0 s and sends its first SYNC at 4.05 s; the watchdog fires at 3.64 s, frames stop until 5.30 s, and the rebooted Propeller answers the PIC32's SYNC packets (sent every 0.5 s, the eleventh at 9.05 s), so `PROPELLER SYNC CHECK........OK` follows, about 1.7 s later than without the reboot. The boot check asserts exactly one `prop: CLKSET 80` in a normal start (`REBOOTS`, 0 for Domino's and Rob Zombie). The checks that use the service menu or the console start later for The Jetsons (check 5's menu at 16 s, the simulator's commands from 12 s, the libpinmame clip at 14 s). `REBOOTS=1` depends on the stand-in's 3 s hold: the real bootloader's timeout is not known; any hold under 2.59 s avoids the reboot, and Domino's needs a hold over 1.7 s. The hold stays 3 s. Cost if wrong: on the real machine the bootloader's hold may be shorter than the watchdog, and attract mode would come 1.7 s sooner.
8. **Where the switch chart and the firmware disagree, the number is the firmware's and the name the chart's.**
   - The kickout hole is switch 7 (PinMAME 18): in a game JET fired the saucer coil (19, solenoid 20) 1.7 s after switch 7 closed and never for switch 6 (the chart's number) held for 3 s.
   - The trough's eject position is Trough Opto 1 (cabinet 10, PinMAME 92): `countballs()` (0x9D02FAA8) counts switches 34 and 35 and cabinet 10, and never reads switch 33 (the chart's Ball Trough 1).
   - The scoop is its opto (cabinet 14, 96): in attract mode JET ejects the scoop (coil 8, solenoid 9) 1 s after the opto closes, and its dispatch handles the opto as switch 8 (0x9D01338C). Switch 8 alone, the chart's Scoop, did nothing in attract mode; in a game the scoop coil fired 3 s after it closed, so the firmware reads it somewhere too. The simulator closes the opto only.
   - Switches 14, 15, 22, 23, 30, 31, 38 and 39 are in JET's dispatch (39, 30 and 22 share one handler) and lamps 45–47 are driven, but no chart names them: they get no names and no simulator keys; the switch test and the column/row keys still reach them.
   Cost if wrong: a table built from the chart's numbers for the kickout or the scoop would wait for an eject the firmware does not give.
9. **The simulator's geometry is inferred and marked as such** (research §7): the 3-ball trough on 54, 53 and the opto 92; the Ball Load coil (4, solenoid 5) moves trough 1 to the shooter lane (51) unless the lane is full; JET itself fires the Ball Launch coil (5, solenoid 6) as soon as a ball reaches the lane (V003's `WHATSNEW`: "Ball is kicked off lane as soon as machine is turned on"), and the Launch Button (cabinet 2, key 9) does the same in a game; there is no manual plunger. The scoop and the kickout hole hold the ball until their coils fire. The up-post (coil 16, solenoid 17, "BALL STOP") sends an orbit shot back down the orbit it came up if it rose within 9 frames, as Domino's. The flippers' end-of-stroke switches (26, 55) close while a coil of their flipper was on in the last frame (the firmware ends the power pulse on them). The Elroy loop, captive ball, Extra Ball target, top lanes, pops, slings and the six GEORGE targets are one switch each. Cost if wrong: a shot's switch order differs from the machine's; the firmware's rules see the same switches.
10. **The servo range is 544–2,400 µs, and the check is attract mode.** JET has no servo test (its menu has nine items: SWITCH EDGE, AUDIO/MUSIC, SOLENOID, LAMP, RGB LIGHTING, COIL SETTINGS, MAIN SETTINGS, GAME SETTINGS, GAME AUDITS) and its `[S…]` console command only prints to a dead stream. In attract mode both servos pulse 1,476 µs (90°), which the game's range draws as level 128 (Domino's range gives 121–122). Check 5 and the simulator check assert 1,476 µs and level 128 on 57 and 58; `getMech(0)` is the Orbitty's angle in degrees.
11. **Check 5 for The Jetsons.** The console part is Domino's (the same 64 lamps, 24 coils and GI chain). The keys start later (menu at 16 s, console from 17 s, keys from 44.5 s; Ruling 7). The solenoid test runs KNOCKER to SOL23 as Domino's; there is no servo test, so LAMP follows SOLENOID; the lamp and RGB tests are Domino's (no external LED: 62–64 stay 0, as JET never writes them); RGB LIGHTING is the fifth item, so four Left Shifts lead back to SWITCH EDGE. The switch test draws the cabinet columns at the left (x 0–7) and the matrix at the right (x 8–39); Domino's has them the other way round. At rest it shows the coin door (1) and trough opto 1 (10) closed and no matrix switch: the console's Ball Load and the solenoid test's LOAD COIL each put a ball in the shooter lane, which the firmware launched. Attract mode: the start lamp is lit throughout (JET sets RA3 in `setup()` and every `loop()`), not blinking. The switch test's first frame is drawn before the inputs (all open); check 5 ignores the test's first frame only when it shows nothing closed.
12. **Speed** (`bench.sh`, a build without a profile, at the loads printed): The Jetsons' frames stream continuously at 30 fps, 8,192 bytes each. Measured once in attract mode, with video and in play (Task 8); no optimisation is in this plan.
13. **Plan 11a's review minors** (Task 1): `PINHECK_GAME_DEFAULTS` becomes `PINHECK_DOMINOS_DATA` and `INIT_PINHECK` takes the data as an argument, with a check that only the system set and Domino's use Domino's values; `pinheck_unsupported()` (Ruling 1); Rob Zombie's unused `locals.since[]` goes; the `autoBall` comment says what `sim.c` does (Up/Down still select among free balls); a game's VBScript names its own file in its first line (`rzspook.vbs`, `jetsons.vbs`), Domino's `pinheck.vbs` unchanged. Rob Zombie's lock overflow stays deferred under Plan 11a's Ruling 7.
14. **Names.** A `jetsons` section in `pinheck_names.h`, ids equal to `jetsons.c`'s constants as `names.py check` requires; `names.py vbs src jetsons` writes `jetsons.vbs`.

## Review Focus

- **Domino's and Rob Zombie unchanged** by the frame size: `disp.frame` is 4,096 for both, the video takes the look branch for a 32-row module exactly as before, and libpinmame's loop draws the same 128×32. `bench.sh` with `REFERENCE` for both games (Tasks 2 and 8) and their display checks (Task 6).
- **The 128×64 frame path end to end**: decoder (8,192-byte frames, 4,096 discarded), driver copy and frame log (8,212-byte records), window (256×128 at 2×2), libpinmame (128×64 RGB565). Unit test (Task 2), the clip check and the screenshot (Task 6), the libpinmame frames (Task 6).
- **The parameterised checks** run Domino's and Rob Zombie as before with their own `games.sh` values: the boot check's `UPDATE_END` and `REBOOTS`, the simulator check's trough switch, check 5's plan (compared byte for byte), the display check's allowed discards (only for `LOOK=none`).
- **The update that ends without `LEAVE_PROGMODE`**: `boot_stop()` logs only when the machine stops with the PIC32 held for programming; the first launch must restart, as the user does (Ruling 6).
- **The Propeller's reboot before the first sync** (Ruling 7): exactly one per normal start; a check that sends console commands or keys before 13 s would lose them.

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck.h`, `src/wpc/pinheckgames.c`, `src/wpc/sims/pinheck/{dominos,rzspook}.c` | Domino's data only for Domino's and the system set; review minors (modified) |
| `src/wpc/pinheck/display.[ch]` | frame size per module (`pinheck_display_size`, `DISPLAY_SIZE_OK`) (modified) |
| `src/wpc/pinheck.c` | unsupported per-game data named at start; 8,192-byte frames; 128×64 drawn as sent; `boot_stop` at machine stop (modified) |
| `src/wpc/pinheck/bootldr.[ch]` | `boot_stop`: the update that ends without leaving (modified) |
| `src/wpc/sims/pinheck/jetsons.c` | The Jetsons' game definition and playfield simulator |
| `src/wpc/driver.c`, `tests/pinheck/link/register_build.py`, build files | `jetsons` in the driver list and every build (modified / regenerated) |
| `src/wpc/pinheck_names.h`, `tests/pinheck/vpx/{names.py,check.sh}` | The Jetsons' names; the game-data and VBScript checks (modified) |
| `tests/pinheck/games.sh` | per-game facts: Propeller image, update end, reboots, look, trough switch (modified) |
| `tests/pinheck/pic32mx/{pinmame_check.sh,romset_check.sh}` | the boot check by game (modified) |
| `tests/pinheck/sim/{pinmame_sim.sh,jetsim.py}` | the simulator check by game; The Jetsons' |
| `tests/pinheck/board/check5.py` | check 5 for The Jetsons (modified) |
| `tests/pinheck/display/{display_test.c,frames.py,render.py,pinmame_display.sh,pinmame_look.sh}` | the 128×64 module in the unit and machine checks (modified) |
| `tests/pinheck/vpx/{vpx.py,pinmame_vpx.sh}` | libpinmame's 128×64 display and sound (modified) |
| `tests/pinheck/perf/bench.sh` | The Jetsons' workloads (modified) |
| `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | this plan's row and what it carries (modified) |

---
### Task 1: Reference build and Plan 11a's review minors

**Files:**
- Modify: `tests/pinheck/vpx/check.sh`, `src/wpc/pinheck.h`, `src/wpc/pinheckgames.c`, `src/wpc/sims/pinheck/dominos.c`, `src/wpc/sims/pinheck/rzspook.c`, `src/wpc/pinheck.c`, `tests/pinheck/vpx/names.py`

**Interfaces:**
- Consumes: Plan 11a's `pinheck_tGameData`, `INIT_PINHECK`, `PINHECK_GAME_DEFAULTS`, `pinheck_game()`, `names.py vbs SRC [GAME]`.
- Produces: `PINHECK_DOMINOS_DATA` (`128, 32, 340, 1000, 2000, 0, 1`); `INIT_PINHECK(name, balls, version, data)`; `static void pinheck_unsupported(void)` in `pinheck.c`, called by `MACHINE_INIT` for every game; the `gamedata:` and `vbs:` lines of `vpx/check.sh`; `build/reference/sdl3pinmame`.

- [ ] **Step 1: Build the reference**

Build `pinheck` as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`) and keep the binary; Domino's and Rob Zombie's outputs are compared with it in Tasks 2 and 8:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11b.log 2>&1; tail -1 build/sdl3pinmame/11b.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```
Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: The checks first**

Domino's script as it is, for Step 3: `python3 tests/pinheck/vpx/names.py vbs src > build/pinheck-before.vbs`

`vpx/check.sh` checks that only the system set and Domino's use Domino's values and that `INIT_PINHECK` takes the data, and that Rob Zombie's script names its file (review minors 1 and 4):

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/vpx/check.sh', [
    (r'''srcs() { grep -o''',
     r'''# per-game data: Domino's values only for the system set and Domino's; every other game spells out its own
dd=$(grep -l 'PINHECK_DOMINOS_DATA' $S/wpc/*.c $S/wpc/sims/pinheck/*.c | sed 's|.*/src/||' | tr '\n' ' ')
if [ "$dd" = "wpc/pinheckgames.c wpc/sims/pinheck/dominos.c " ] && grep -q 'define INIT_PINHECK(name, balls, version, data)' $S/wpc/pinheckgames.c &&
	! grep -q 'PINHECK_GAME_DEFAULTS' $S/wpc/*.[ch] $S/wpc/sims/pinheck/*.c; then
	echo "gamedata: Domino's values only in the system set and dominos.c ($(grep -l 'pinheck_tGameData' $S/wpc/sims/pinheck/*.c | wc -l) games defined)"
else
	echo "GAMEDATA FAIL: Domino's values (PINHECK_DOMINOS_DATA) in '$dd', or INIT_PINHECK without a data argument"; fail=$((fail + 1))
fi
python3 names.py vbs $S rzspook | head -1 | grep -q "rzspook.vbs" && echo "vbs: rzspook's script names its file" || { echo "VBS FAIL: rzspook's script does not name rzspook.vbs"; fail=$((fail + 1)); }
srcs() { grep -o'''),
])
EOF
```

Run: `nice -n 15 tests/pinheck/vpx/check.sh | grep FAIL`
Expected:
```text
GAMEDATA FAIL: Domino's values (PINHECK_DOMINOS_DATA) in '', or INIT_PINHECK without a data argument
VBS FAIL: rzspook's script does not name rzspook.vbs
```

- [ ] **Step 3: The fixes**

Domino's values get Domino's name and `INIT_PINHECK` an explicit data argument; `pinheck_unsupported()` names per-game data the driver does not support (here: an inverted WS2801, a display other than 128×32; Task 2 adds 128×64); Rob Zombie loses its unused `locals.since[]`; the `autoBall` comment says what `sim.c` does; a game's script other than Domino's names its file in its first line:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


AUTOBALL = r'''  TRUE                  /* the keys move a ball on the playfield: no Up/Down in multiball */
'''
AUTOBALL_NEW = r'''  TRUE                  /* autoBall: the keys move the selected ball, or the first free one; Up/Down select among free balls */
'''
edit('src/wpc/pinheck.h', [
    (r'''/* Domino's values: 128 x 32, POSITION 340, servo levels 1.0-2.0 ms, WS2801 as sent, PINHECK_INSERVICE applies */
#define PINHECK_GAME_DEFAULTS 128, 32, 340, 1000, 2000, 0, 1
''',
     r'''/* Domino's values: 128 x 32, POSITION 340, servo levels 1.0-2.0 ms, WS2801 as sent, PINHECK_INSERVICE applies.
   Only Domino's and the system set use them; every other game spells out its own */
#define PINHECK_DOMINOS_DATA 128, 32, 340, 1000, 2000, 0, 1
'''),
])
edit('src/wpc/pinheckgames.c', [
    (r'''#define INIT_PINHECK(name, balls, version) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
static pinheck_tGameData name##GameData = { { GEN_PINHECK, pinheck_disp, {FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, version, 0, pinheck_getsol} }, PINHECK_GAME_DEFAULTS }; \
''',
     r'''/* data: the game's width, height, aligned, servoMin, servoMax, rgbInverted, inService (pinheck_tGameData) */
#define INIT_PINHECK(name, balls, version, data) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
static pinheck_tGameData name##GameData = { { GEN_PINHECK, pinheck_disp, {FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, version, 0, pinheck_getsol} }, data }; \
'''),
    (r'''INIT_PINHECK(pinheck, 3, 0)
''',
     r'''INIT_PINHECK(pinheck, 3, 0, PINHECK_DOMINOS_DATA)
'''),
])
edit('src/wpc/sims/pinheck/dominos.c', [
    (r'''  PINHECK_GAME_DEFAULTS
''',
     r'''  PINHECK_DOMINOS_DATA
'''),
    (AUTOBALL, AUTOBALL_NEW),
])
edit('src/wpc/sims/pinheck/rzspook.c', [
    (r'''  int since[25];  /* frames since each coil was last on */
  int drop;''',
     r'''  int drop;'''),
    (r'''  int i;
  for (i = 1; i <= 24; i++)
    locals.since[i] = sol(i) ? 0 : locals.since[i] < 10000 ? locals.since[i] + 1 : 10000;
  if (pinheck_servo(0)) locals.gateUs''',
     r'''  int i;
  if (pinheck_servo(0)) locals.gateUs'''),
    (r'''  memset(&locals, 0, sizeof(locals));
  for (i = 0; i <= 24; i++) locals.since[i] = 10000;
  locals.drop = -1;''',
     r'''  memset(&locals, 0, sizeof(locals));
  locals.drop = -1;'''),
    (AUTOBALL, AUTOBALL_NEW),
])
edit('src/wpc/pinheck.c', [
    (r'''static uint8_t pinheck_brd_swcol(void *ctx, int col) { (void)ctx; return coreGlobals.swMatrix[col + 1]; }
''',
     r'''/* per-game data the driver does not support is named at start, not dropped silently */
static void pinheck_unsupported(void)
{
	const pinheck_tGameData *g = pinheck_game();
	if (g->rgbInverted) {
		fprintf(stderr, "pinheck: %s: inverted WS2801 lines are not supported, the RGB outputs are as sent\n", Machine->gamedrv->name);
		logerror("pinheck: %s: inverted WS2801 lines are not supported\n", Machine->gamedrv->name);
	}
	if (g->width != DISPLAY_W || g->height != DISPLAY_H) {
		fprintf(stderr, "pinheck: %s: a %dx%d display is not supported, frames are taken as 128x32\n", Machine->gamedrv->name, g->width, g->height);
		logerror("pinheck: %s: a %dx%d display is not supported\n", Machine->gamedrv->name, g->width, g->height);
	}
}

static uint8_t pinheck_brd_swcol(void *ctx, int col) { (void)ctx; return coreGlobals.swMatrix[col + 1]; }
'''),
    (r'''		logerror("pinheck: '%s' is the pinHeck system set, not a game\n", Machine->gamedrv->name);
		return;
	}
''',
     r'''		logerror("pinheck: '%s' is the pinHeck system set, not a game\n", Machine->gamedrv->name);
		return;
	}
	pinheck_unsupported();
'''),
])
edit('tests/pinheck/vpx/names.py', [
    (r'''  names.py vbs SRC [GAME]     the system script a table loads with LoadVPM (to stdout): pinheck.vbs for dominos
GAME is dominos (the default) or rzspook."""
''',
     r'''  names.py vbs SRC [GAME]     the system script a table loads with LoadVPM (to stdout): pinheck.vbs for dominos,
                              GAME.vbs (rzspook.vbs, ...) for the others, which say so in their first line
GAME is dominos (the default) or rzspook."""
'''),
    (r'''    out = [HEAD.lstrip('\n') % TITLE[game]]
''',
     r'''    out = [HEAD.lstrip('\n') % TITLE[game]]
    if game != 'dominos':
        out.insert(0, "' %s.vbs: save under this name (pinheck.vbs is Domino's); its table loads it with LoadVPM\n" % game)
'''),
])
EOF
```

Run: `python3 tests/pinheck/vpx/names.py vbs src | cmp - build/pinheck-before.vbs && echo "pinheck.vbs: unchanged"; nice -n 15 tests/pinheck/vpx/check.sh | grep -v selftest`
Expected:
```text
pinheck.vbs: unchanged
names dominos: 46 switches, 48 lamps, 47 solenoid outputs; 39 sim constants agree
names rzspook: 53 switches, 49 lamps, 50 solenoid outputs; 53 sim constants agree
mingw: strcasecmp kept
gamedata: Domino's values only in the system set and dominos.c (2 games defined)
vbs: rzspook's script names its file
builds: 10 build lists carry the same 34 pinHeck sources
scale: 2x2 dots in PinMAME and VPinMAME windows, the 128x32 panel as sent to libpinmame hosts
vpx: LIBPINMAME or PINMAME_ROMS not set: libpinmame's display list and getters not run
vpx: 0 failed
```

- [ ] **Step 4: Build**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11b.log 2>&1; tail -1 build/sdl3pinmame/11b.log
grep -i warning build/sdl3pinmame/11b.log | grep -cE 'pinheck|dominos|rzspook'
```
Expected: `[100%] Built target sdl3pinmame` and `0`. Domino's and Rob Zombie are compared with the reference after Task 2, which touches the same driver; the review minors change no output (Domino's data is the same seven numbers; `locals.since[]` was written and never read).

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck.h src/wpc/pinheck.c src/wpc/pinheckgames.c src/wpc/sims/pinheck/dominos.c src/wpc/sims/pinheck/rzspook.c tests/pinheck/vpx/check.sh tests/pinheck/vpx/names.py
git commit -m "pinheck: Domino's data only for Domino's (PINHECK_DOMINOS_DATA), unsupported game data named at start, Plan 11a review minors"
```

### Task 2: The 128×64 frame in the decoder and the driver

**Files:**
- Modify: `tests/pinheck/display/display_test.c`, `src/wpc/pinheck/display.h`, `src/wpc/pinheck/display.c`, `src/wpc/pinheck.h`, `src/wpc/pinheck.c`

**Interfaces:**
- Consumes: Task 1's `pinheck_unsupported()`; Plan 6's decoder (`pinheck_display_init`, `pinheck_display_pins`, the `on_frame` callback) and the driver's `pinheck_disp_frame`, `pinheck_disp_reset` and `pinheck_video`.
- Produces: `DISPLAY_FRAME_MAX` (8,192), `DISPLAY_SIZE_OK(w, h)`, `display.frame` (bytes per frame), `int pinheck_display_size(display *d, int w, int h)` (1 for 128×32 and 128×64, else 0 and the size unchanged); `PINHECK_VIDEO_H64` (64 × `PINHECK_VIDEO_SCALE`); frame log records of 20 + `disp.frame` bytes.

- [ ] **Step 1: The failing unit test**

A 128×64 module takes 8,192-byte frames and discards a 128×32 frame, an 8,193-byte transfer and a lone 1,024-byte burst; any other size is refused:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/display_test.c', [
    (r'''static uint8_t last_frame[DISPLAY_FRAME], last_cfg[DISPLAY_CFG_MAX];
''',
     r'''static uint8_t last_frame[DISPLAY_FRAME_MAX], last_cfg[DISPLAY_CFG_MAX];
'''),
    (r'''static void on_frame(void *ctx, const uint8_t *f, uint64_t t) { (void)ctx; (void)t; memcpy(last_frame, f, DISPLAY_FRAME); frames++; }
''',
     r'''static void on_frame(void *ctx, const uint8_t *f, uint64_t t) { (void)ctx; (void)t; memcpy(last_frame, f, (size_t)d.frame); frames++; }
'''),
    (r'''static void config_packet(void)
''',
     r'''/* the 128x64 module: 8192-byte frames; a 128x32 frame or a 1024-byte burst on its own is discarded */
static void frame_128x64(void)
{
	int i;
	reset();
	CHECK(pinheck_display_size(&d, 128, 48) == 0 && pinheck_display_size(&d, 256, 32) == 0 && d.frame == DISPLAY_FRAME);
	CHECK(pinheck_display_size(&d, 128, 64) == 1 && d.frame == 8192);
	for (i = 0; i < 8192; i++) byte((unsigned)(i * 5 + 3) & 0xFF);
	strobe();
	CHECK(frames == 1 && logs == 0);
	for (i = 0; i < 8192 && last_frame[i] == ((i * 5 + 3) & 0xFF); i++) ;
	CHECK(i == 8192);
	for (i = 0; i < DISPLAY_FRAME; i++) byte(0x11);
	strobe();
	CHECK(frames == 1 && strcmp(last_log, "display: frame of 4096 bytes discarded") == 0);
	for (i = 0; i < 8193; i++) byte(0x22);
	strobe();
	CHECK(frames == 1);
	for (i = 0; i < 1024; i++) byte(0x33);
	strobe();
	for (i = 0; i < 8192; i++) byte(0x44);
	strobe();
	CHECK(frames == 2 && last_frame[0] == 0x44 && last_frame[8191] == 0x44);
	CHECK(pinheck_display_size(&d, 128, 32) == 1 && d.frame == DISPLAY_FRAME);
}

static void config_packet(void)
'''),
    (r'''	frame_and_bit_order();
	config_packet();
''',
     r'''	frame_and_bit_order();
	frame_128x64();
	config_packet();
'''),
])
EOF
```

Run: `PINHECK_SKIP_FIRMWARE=1 nice -n 15 tests/pinheck/display/check.sh 2>&1 | grep -m1 error`
Expected: `display_test.c:8:27: error: ‘DISPLAY_FRAME_MAX’ undeclared here (not in a function); did you mean ‘DISPLAY_FRAME’?` (GCC's wording; another compiler's differs).

- [ ] **Step 2: The frame size, the driver's frame and the window**

The decoder takes its frame size from `pinheck_display_size`; the driver sets it from the game data at each reset, keeps up to 8,192 bytes, logs `disp.frame` bytes per record, and draws a 128×64 frame 2×2 per dot as sent (the look stays the 128×32 module's; libpinmame keeps the frame as sent for both):

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck/display.h', [
    (r'''#define DISPLAY_FRAME   (DISPLAY_W * DISPLAY_H)
''',
     r'''#define DISPLAY_FRAME   (DISPLAY_W * DISPLAY_H)
#define DISPLAY_FRAME_MAX (DISPLAY_W * 2 * DISPLAY_H) /* the 128 x 64 module */
#define DISPLAY_SIZE_OK(w, h) ((w) == DISPLAY_W && ((h) == DISPLAY_H || (h) == 2 * DISPLAY_H))
'''),
    (r'''typedef void (*display_frame_fn)(void *ctx, const uint8_t *frame4096, uint64_t t);
''',
     r'''typedef void (*display_frame_fn)(void *ctx, const uint8_t *frame, uint64_t t); /* frame: d->frame bytes */
'''),
    (r'''	uint8_t buf[DISPLAY_FRAME];
	long nbits;
''',
     r'''	uint8_t buf[DISPLAY_FRAME_MAX];
	long frame;     /* bytes in a frame: DISPLAY_FRAME, or as pinheck_display_size set it */
	long nbits;
'''),
    (r'''void pinheck_display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir);
''',
     r'''int pinheck_display_size(display *d, int w, int h);
void pinheck_display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir);
'''),
])
edit('src/wpc/pinheck/display.c', [
    (r'''	} else if (n != DISPLAY_FRAME) {
''',
     r'''	} else if (n != d->frame) {
'''),
    (r'''	d->log = log;
}
''',
     r'''	d->log = log;
	d->frame = DISPLAY_FRAME;
}

/* the 128 x 32 or the 128 x 64 module: frames of w * h bytes; 0 (frames stay 128 x 32) for any other size */
int pinheck_display_size(display *d, int w, int h)
{
	if (!DISPLAY_SIZE_OK(w, h)) return 0;
	d->frame = (long)w * h;
	return 1;
}
'''),
    (r'''		if (i < DISPLAY_FRAME) {
''',
     r'''		if (i < d->frame) {
'''),
])
edit('src/wpc/pinheck.h', [
    (r'''/* the 128x32 panel is drawn 2x2 per dot in the PinMAME and VPinMAME windows; libpinmame hosts
   draw their own dots, so they get the panel as sent */
''',
     r'''/* the panel is drawn 2x2 per dot in the PinMAME and VPinMAME windows; libpinmame hosts
   draw their own dots, so they get the panel as sent */
'''),
    (r'''#define PINHECK_VIDEO_H (32 * PINHECK_VIDEO_SCALE)
''',
     r'''#define PINHECK_VIDEO_H (32 * PINHECK_VIDEO_SCALE)
#define PINHECK_VIDEO_H64 (64 * PINHECK_VIDEO_SCALE) /* the 128x64 module */
'''),
    (r'''  int width, height;      /* display module in dots; the driver supports 128 x 32 */
''',
     r'''  int width, height;      /* display module in dots: 128 x 32 (drawn in its look) or 128 x 64 (as sent) */
'''),
])
edit('src/wpc/pinheck.c', [
    (r'''	if (g->width != DISPLAY_W || g->height != DISPLAY_H) {
''',
     r'''	if (!DISPLAY_SIZE_OK(g->width, g->height)) {
'''),
    (r'''static uint8_t disp_shown[DISPLAY_FRAME], disp_cfg[DISPLAY_CFG_MAX];
''',
     r'''static uint8_t disp_shown[DISPLAY_FRAME_MAX], disp_cfg[DISPLAY_CFG_MAX];
'''),
    (r'''	uint32_t at = 0xFFFFFFFFu, a;
	int k;
	(void)ctx;
	memcpy(disp_shown, frame, DISPLAY_FRAME);
	disp_dirty = 1;
	if (!disp_log) return;
	for (a = 0; a + DISPLAY_FRAME <= 0x8000; a++)
		if (prop.chip.hub[a] == frame[0] && !memcmp(prop.chip.hub + a, frame, DISPLAY_FRAME)) { at = a; break; }
''',
     r'''	uint32_t at = 0xFFFFFFFFu, a;
	const size_t n = (size_t)disp.frame;
	int k;
	(void)ctx;
	memcpy(disp_shown, frame, n);
	disp_dirty = 1;
	if (!disp_log) return;
	for (a = 0; a + n <= 0x8000; a++)
		if (prop.chip.hub[a] == frame[0] && !memcmp(prop.chip.hub + a, frame, n)) { at = a; break; }
'''),
    (r'''	fwrite(frame, 1, DISPLAY_FRAME, disp_log);
''',
     r'''	fwrite(frame, 1, n, disp_log);
'''),
    (r'''	pinheck_display_init(&disp, NULL, pinheck_disp_frame, pinheck_disp_config, pinheck_prop_log);
''',
     r'''	pinheck_display_init(&disp, NULL, pinheck_disp_frame, pinheck_disp_config, pinheck_prop_log);
	pinheck_display_size(&disp, pinheck_game()->width, pinheck_game()->height); /* any other size is named at start */
'''),
    (r'''	const int x0 = layout->left, y0 = layout->top;
	int x, y;
	prop_sync(&prop);
	(void)cliprect;
#if !defined(LIBPINMAME) && PINHECK_VIDEO_SCALE == 2
	/* the module's look (dot shape, brightness, position); libpinmame hosts get the frame as sent */
	if (disp_dirty) pinheck_display_render(&disp_look, disp_shown, disp_img);
	disp_dirty = 0;
	for (y = 0; y < DISPLAY_LOOK_H && y0 + y < bitmap->height; y++)
		for (x = 0; x < DISPLAY_LOOK_W && x0 + x < bitmap->width; x++) {
			const uint8_t *p = disp_img + (y * DISPLAY_LOOK_W + x) * 3;
			if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y0 + y])[x0 + x] = MAKE_RGB(p[0], p[1], p[2]);
			else ((UINT16 *)bitmap->line[y0 + y])[x0 + x] = (UINT16)(((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
		}
#else
	for (y = 0; y < DISPLAY_H * PINHECK_VIDEO_SCALE && y0 + y < bitmap->height; y++)
		for (x = 0; x < DISPLAY_W * PINHECK_VIDEO_SCALE && x0 + x < bitmap->width; x++) {
			const uint8_t v = disp_shown[(y / PINHECK_VIDEO_SCALE) * DISPLAY_W + x / PINHECK_VIDEO_SCALE];
			if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y0 + y])[x0 + x] = disp_rgb32[v];
			else ((UINT16 *)bitmap->line[y0 + y])[x0 + x] = disp_rgb15[v];
		}
#endif
}
''',
     r'''	const int x0 = layout->left, y0 = layout->top, h = (int)(disp.frame / DISPLAY_W);
	int x, y;
	prop_sync(&prop);
	(void)cliprect;
#if !defined(LIBPINMAME) && PINHECK_VIDEO_SCALE == 2
	/* the 128x32 module's look (dot shape, brightness, position); the 128x64 module and libpinmame hosts get the frame as sent */
	if (h == DISPLAY_H) {
		if (disp_dirty) pinheck_display_render(&disp_look, disp_shown, disp_img);
		disp_dirty = 0;
		for (y = 0; y < DISPLAY_LOOK_H && y0 + y < bitmap->height; y++)
			for (x = 0; x < DISPLAY_LOOK_W && x0 + x < bitmap->width; x++) {
				const uint8_t *p = disp_img + (y * DISPLAY_LOOK_W + x) * 3;
				if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y0 + y])[x0 + x] = MAKE_RGB(p[0], p[1], p[2]);
				else ((UINT16 *)bitmap->line[y0 + y])[x0 + x] = (UINT16)(((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
			}
		return;
	}
#endif
	for (y = 0; y < h * PINHECK_VIDEO_SCALE && y0 + y < bitmap->height; y++)
		for (x = 0; x < DISPLAY_W * PINHECK_VIDEO_SCALE && x0 + x < bitmap->width; x++) {
			const uint8_t v = disp_shown[(y / PINHECK_VIDEO_SCALE) * DISPLAY_W + x / PINHECK_VIDEO_SCALE];
			if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y0 + y])[x0 + x] = disp_rgb32[v];
			else ((UINT16 *)bitmap->line[y0 + y])[x0 + x] = disp_rgb15[v];
		}
}
'''),
])
EOF
```

- [ ] **Step 3: The display suite**

Run: `PINHECK_SKIP_FIRMWARE=1 nice -n 15 tests/pinheck/display/check.sh | tail -4`
Expected:
```text
display: ok
look: display.c equals look.py in 8 looks
look: sdl3pinmame renders the look, libpinmame gets the frame as sent
display: 0 failed
```

- [ ] **Step 4: Build, and Domino's and Rob Zombie are byte-identical**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11b.log 2>&1; tail -1 build/sdl3pinmame/11b.log
grep -i warning build/sdl3pinmame/11b.log | grep -cE 'pinheck|dominos|rzspook|display\.c'
REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video
rz env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video
```
Expected (about 3 minutes each; speeds vary with the load):
```text
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.283x over 30.0 s emulated (23.4 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.999x, load 3.30
bench attract ref: 1.277x over 30.0 s emulated (23.5 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.995x, load 2.54
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.121x over 24.0 s emulated (21.4 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.787x, load 2.46
bench video ref: 1.117x over 24.0 s emulated (21.5 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.890x, load 2.14
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
determinism first boot: identical nvram.base/rzspook.nv
bench attract opt: 1.454x over 30.0 s emulated (20.6 s wall), whole run 11.4 G instructions and 1.0 s CPU per emulated s, worst 100 ms 1.311x, load 2.30
bench attract ref: 1.433x over 30.0 s emulated (20.9 s wall), whole run 11.4 G instructions and 1.0 s CPU per emulated s, worst 100 ms 1.281x, load 2.20
determinism attract: identical uart.log frames.bin snd.wav nvram/rzspook.nv
bench video opt: 1.225x over 24.0 s emulated (19.6 s wall), whole run 12.4 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.488x, load 2.12
bench video ref: 1.230x over 24.0 s emulated (19.5 s wall), whole run 12.4 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.478x, load 1.87
determinism video: identical uart.log frames.bin snd.wav nvram/rzspook.nv
```

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck/display.h src/wpc/pinheck/display.c src/wpc/pinheck.h src/wpc/pinheck.c tests/pinheck/display/display_test.c
git commit -m "pinheck display: the 128x64 module (8192-byte frames, drawn 2x2 as sent); 128x32 unchanged"
```

### Task 3: The `jetsons` game and its boot check

**Files:**
- Create: `src/wpc/sims/pinheck/jetsons.c`
- Modify: `tests/pinheck/games.sh`, `src/wpc/pinheck/bootldr.h`, `src/wpc/pinheck/bootldr.c`, `src/wpc/pinheck.c`, `tests/pinheck/pic32mx/pinmame_check.sh`, `tests/pinheck/pic32mx/romset_check.sh`, `src/wpc/pinheckgames.c`, `src/wpc/driver.c`, `tests/pinheck/link/register_build.py`; regenerated: `src/pinmame.mak`, `cmake/*/CMakeLists*.txt`, `vcproj/*.vcxproj(.filters)`

**Interfaces:**
- Consumes: Task 2's `PINHECK_VIDEO_H64` and frame size; Plan 11a's `games.sh`, `pinheck_tGameData`, `pinheck_servo()`; Plan 8b's simulator framework.
- Produces: the `jetsons` game (keys in the file's header; `HandleMechanics` bit 0 the flippers' end-of-stroke switches; `getMech(0)` the Orbitty's angle in degrees, −1 before its first pulse); `void boot_stop(pic32_boot *b)`; `games.sh` variables `PRP`, `UPDATE_END`, `REBOOTS` for every game, and The Jetsons' line.

- [ ] **Step 1: The checks' game table and the boot check by game**

`games.sh` gains each game's Propeller image, how its update ends and how often its Propeller reboots before the first sync, and The Jetsons' line (its `CLIP` is Task 6's); the bootloader stand-in logs an update that ends with the PIC32 held (Ruling 6); the boot check and the romset check take them:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/games.sh', [
    (r'''#   PRG         the PIC32 image in the update
#   PROGRAMMED  bytes the first-boot update flashes
''',
     r'''#   PRG         the PIC32 image in the update
#   PRP         the Propeller image in the update
#   PROGRAMMED  bytes the first-boot update flashes
#   UPDATE_END  how the update hands the PIC32 back: leave (STK500v2 LEAVE_PROGMODE), or stopped in
#               programming mode (the Propeller asks for a restart and the PIC32 waits for it)
#   REBOOTS     Propeller reboots (CLKSET $80) before the first PIC32 sync of a normal start
'''),
    (r'''dominos) PRG=DOM_V006.PRG PROGRAMMED=204288 UPDATED_AT=138 STORED=600BAFA CLIP=LT5 SIM=sim.py
''',
     r'''dominos) PRG=DOM_V006.PRG PRP=PRP_V008.BIN PROGRAMMED=204288 UPDATE_END=leave REBOOTS=0 UPDATED_AT=138 STORED=600BAFA CLIP=LT5 SIM=sim.py
'''),
    (r'''rzspook) PRG=RZO_V026.PRG PROGRAMMED=309248 UPDATED_AT=184 STORED=1A00BAFA CLIP=DMB SIM=rzsim.py
	BANNER='pinHeck System 2011-2016|Game: RZO - SPOOK SHOW' ;;
''',
     r'''rzspook) PRG=RZO_V026.PRG PRP=PRP_V008.BIN PROGRAMMED=309248 UPDATE_END=leave REBOOTS=0 UPDATED_AT=184 STORED=1A00BAFA CLIP=DMB SIM=rzsim.py
	BANNER='pinHeck System 2011-2016|Game: RZO - SPOOK SHOW' ;;
jetsons) PRG=JET_V004.PRG PRP=PRP_V002.BIN PROGRAMMED=197632 UPDATE_END='stopped in programming mode' REBOOTS=1 UPDATED_AT=104 STORED=400BAFA CLIP=TL1 SIM=jetsim.py
	BANNER='pinHeck System 2011-2016|Game: JET - JETSONS' ;;
'''),
])
edit('src/wpc/pinheck/bootldr.c', [
    (r'''uint64_t boot_hold(const pic32_boot *b, uint64_t now)
''',
     r'''/* the machine stops (a restart or the end of a run) with the PIC32 held for programming */
void boot_stop(pic32_boot *b)
{
	if (b->state == BOOT_HOST && !b->app_at) logf1(b, "boot: stopped in programming mode, %lu bytes programmed", b->programmed);
}

uint64_t boot_hold(const pic32_boot *b, uint64_t now)
'''),
])
edit('src/wpc/pinheck/bootldr.h', [
    (r'''void boot_advance(pic32_boot *b, uint64_t pic_cycle);
''',
     r'''void boot_advance(pic32_boot *b, uint64_t pic_cycle);
void boot_stop(pic32_boot *b);
'''),
])
edit('src/wpc/pinheck.c', [
    (r'''static MACHINE_STOP(pinheck)
{
	prop_stop_thread(&prop);
''',
     r'''static MACHINE_STOP(pinheck)
{
	if (!locals.idle) boot_stop(&boot);
	prop_stop_thread(&prop);
'''),
])
edit('tests/pinheck/pic32mx/pinmame_check.sh', [
    (r'''	(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/$GAME.zip" $PRG PRP_V008.BIN DMD SFX) || exit 2
''',
     r'''	(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/$GAME.zip" $PRG $PRP DMD SFX) || exit 2
'''),
    (r'''for want in "boot: sign-on 0" "boot: leave, $PROGRAMMED bytes programmed" "prop: CLKSET 6f"; do
''',
     r'''for want in "boot: sign-on 0" "boot: $UPDATE_END, $PROGRAMMED bytes programmed" "prop: CLKSET 6f"; do
'''),
    (r'''grep -q "boot: sign-on" $B/prop2.log && { echo "PINMAME FAIL: launch 2 ran the update again"; fail=1; }
''',
     r'''grep -q "boot: sign-on" $B/prop2.log && { echo "PINMAME FAIL: launch 2 ran the update again"; fail=1; }
n=$(grep -c "prop: CLKSET 80" $B/prop2.log)
[ "$n" = "$REBOOTS" ] || { echo "PINMAME FAIL: the Propeller rebooted $n times before the sync of launch 2, expected $REBOOTS"; fail=1; }
'''),
])
edit('tests/pinheck/pic32mx/romset_check.sh', [
    (r'''(cd "$PINHECK_UPDATE_DIR" && zip -q -0 "$OLDPWD/$B/nomedia/$GAME.zip" $PRG PRP_V008.BIN) || exit 2
''',
     r'''(cd "$PINHECK_UPDATE_DIR" && zip -q -0 "$OLDPWD/$B/nomedia/$GAME.zip" $PRG $PRP) || exit 2
'''),
])
EOF
```

- [ ] **Step 2: Confirm The Jetsons' boot check fails before the game exists**

```sh
jet() { PINHECK_GAME=jetsons PINHECK_ZIP=$JETSONS_ZIP PINHECK_UPDATE_DIR=$JETSONS_UPDATE_DIR "$@"; }
rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
```
Expected (exit 1):
```text
PINMAME FAIL: launch 1 exited 1
```
followed by PinMAME's own lines for a set it does not know.

- [ ] **Step 3: The game and its simulator**

`src/wpc/sims/pinheck/jetsons.c`:

```c
// license:BSD-3-Clause

/*******************************************************************************
 The Jetsons (Spooky Pinball, 2017): game definition and playfield simulator.

 The simulator models the mechanics the rules depend on: the 3-ball trough with
 its optos, the shooter lane and the autolauncher (no manual plunger), the left
 scoop and its opto, the kickout hole, the orbits with the up-post, the Elroy
 loop, the flippers' end-of-stroke switches, pops, slings and the drain. The
 Orbitty topper (servo 0) is tracked from its servo pulses.
 Coils are read as "on at some time during the last frame" (coreGlobals.solenoids,
 set by the board block), so a pulse of a few milliseconds is never missed.
 ******************************************************************************/

/*------------------------------------------------------------------------------
  Keys (L/R Ctrl selects the left or right one of a pair):
    +-  L/R Slingshot        +I  L/R Inlane         +O  L/R Outlane (drain)
    +R  L/R Orbit            +B  L/R Pop Bumper      B  Lower Pop Bumper
    +L  L/R Lane              L  Middle Lane         S  Scoop
     K  Kickout Hole          E  Elroy Loop          C  Captive Ball
     X  Extra Ball            G/H/J  Center Targets G, E, O
     T/Y/U  Right Targets R, G, E                    Q  Drain between the flippers
  The Launch Button (9) fires the autolauncher.
  The keys move a ball on the playfield (autoBall); Up/Down still select one.
------------------------------------------------------------------------------*/

#include "driver.h"
#include "core.h"
#include "sim.h"
#include "sndbrd.h"
#include "pinheck.h"

PINHECK_INPUT_PORTS_START(jetsons, 3)
  PORT_START /* 0 */
    COREPORT_BIT(0x0001, "Left Qualifier",   KEYCODE_LCONTROL)
    COREPORT_BIT(0x0002, "Right Qualifier",  KEYCODE_RCONTROL)
    COREPORT_BIT(0x0004, "L/R Slingshot",    KEYCODE_MINUS)
    COREPORT_BIT(0x0008, "L/R Inlane",       KEYCODE_I)
    COREPORT_BIT(0x0010, "L/R Outlane",      KEYCODE_O)
    COREPORT_BIT(0x0020, "L/R Orbit",        KEYCODE_R)
    COREPORT_BIT(0x0040, "L/R/Lower Pop",    KEYCODE_B)
    COREPORT_BIT(0x0080, "L/R/Middle Lane",  KEYCODE_L)
    COREPORT_BIT(0x0100, "Scoop",            KEYCODE_S)
    COREPORT_BIT(0x0200, "Kickout Hole",     KEYCODE_K)
    COREPORT_BIT(0x0400, "Elroy Loop",       KEYCODE_E)
    COREPORT_BIT(0x0800, "Captive Ball",     KEYCODE_C)
    COREPORT_BIT(0x1000, "Extra Ball",       KEYCODE_X)
    COREPORT_BIT(0x2000, "Drain",            KEYCODE_Q)
  PORT_START /* 1 */
    COREPORT_BIT(0x0001, "Center Target G",  KEYCODE_G)
    COREPORT_BIT(0x0002, "Center Target E",  KEYCODE_H)
    COREPORT_BIT(0x0004, "Center Target O",  KEYCODE_J)
    COREPORT_BIT(0x0008, "Right Target R",   KEYCODE_T)
    COREPORT_BIT(0x0010, "Right Target G",   KEYCODE_Y)
    COREPORT_BIT(0x0020, "Right Target E",   KEYCODE_U)
PINHECK_INPUT_PORTS_END

/* switches: document switch n (0-63) is (n/8+1)*10 + n%8+1; the optos are cabinet inputs 10, 11 and 14 */
#define swROrbit    11
#define swCaptive   12
#define swRLane     13
#define swMLane     14
#define swLLane     15
#define swElroy     16
#define swKickout   18
#define swExtraBall 22
#define swLSling    23
#define swLInlane   24
#define swLOutlane  25
#define swLFlipEOS  26
#define swCTargetG  31
#define swCTargetE  32
#define swCTargetO  33
#define swLowerPop  34
#define swLPop      35
#define swRPop      36
#define swRTargetR  41
#define swRTargetG  42
#define swRTargetE  43
#define swRSling    44
#define swRInlane   45
#define swROutlane  46
#define swShooter   51
#define swTrough2   53
#define swTrough3   54
#define swRFlipEOS  55
#define swLOrbit    56
#define swTrough1   92
#define swScoop     96

/* solenoids: document coil n is PinMAME solenoid n+1 */
#define sRFlipLow   3
#define sRFlipHigh  4
#define sLoad       5
#define sLaunch     6
#define sLFlipHigh  7
#define sLFlipLow   8
#define sScoop      9
#define sPost       17
#define sKickout    20

static struct {
  int since[25];  /* frames since each coil was last on */
  int orbittyUs;  /* servo 0 (the Orbitty topper): last pulse width in us, 0 = none yet */
  int eos[2];     /* left and right flipper end-of-stroke switches as last set, -1 = not yet */
} locals;

static int sol(int n) { return (coreGlobals.solenoids >> (n - 1)) & 1; }

enum { stTrough3 = SIM_FIRSTSTATE, stTrough2, stTrough1, stShooter, stLaunched, stDrain,
       stLOutlane, stROutlane, stLInlane, stRInlane, stLSling, stRSling,
       stScoop, stKickout, stLOrbit, stROrbit, stLOrbitOut, stROrbitOut, stLOrbitBack, stROrbitBack,
       stElroy, stCaptive, stExtraBall, stLLane, stMLane, stRLane, stLPop, stRPop, stLowerPop,
       stCTargetG, stCTargetE, stCTargetO, stRTargetR, stRTargetG, stRTargetE };

static sim_tState jetsons_stateDef[] = {
  {"Not Installed", 0, 0,           0, stDrain,  0, 0, 0, SIM_STNOTEXCL},
  {"Moving"},
  {"Playfield",     0, 0,           0, 0,        0, 0, 0, SIM_STNOTEXCL},

  {"Trough 3",      1, swTrough3,   0, stTrough2, 3},
  {"Trough 2",      1, swTrough2,   0, stTrough1, 3},
  {"Trough 1",      1, swTrough1,   0, 0,         0},
  {"Shooter Lane",  1, swShooter,   0, 0,         0},
  {"Launched",      1, 0,           0, stFree,   10},
  {"Drain",         1, 0,           0, stTrough3, 1, 0, 0, SIM_STNOTEXCL},

  {"Left Outlane",  1, swLOutlane,  0, stDrain,  10},
  {"Right Outlane", 1, swROutlane,  0, stDrain,  10},
  {"Left Inlane",   1, swLInlane,   0, stFree,    5},
  {"Right Inlane",  1, swRInlane,   0, stFree,    5},
  {"Left Sling",    1, swLSling,    0, stFree,    2},
  {"Right Sling",   1, swRSling,    0, stFree,    2},

  {"Scoop",         1, swScoop,     0, 0,         0},
  {"Kickout Hole",  1, swKickout,   0, 0,         0},
  {"Left Orbit",    1, swLOrbit,    0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"Right Orbit",   1, swROrbit,    0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"L Orbit Exit",  1, swROrbit,    0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"R Orbit Exit",  1, swLOrbit,    0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"L Orbit Post",  1, swLOrbit,    0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"R Orbit Post",  1, swROrbit,    0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Elroy Loop",    1, swElroy,     0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Captive Ball",  1, swCaptive,   0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Extra Ball",    1, swExtraBall, 0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Left Lane",     1, swLLane,     0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Middle Lane",   1, swMLane,     0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Right Lane",    1, swRLane,     0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Left Pop",      1, swLPop,      0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Right Pop",     1, swRPop,      0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Lower Pop",     1, swLowerPop,  0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Center G",      1, swCTargetG,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Center E",      1, swCTargetE,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Center O",      1, swCTargetO,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Right R",       1, swRTargetR,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Right G",       1, swRTargetG,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Right E",       1, swRTargetE,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {0}
};

/* frames: the ball meets the up-post 9 frames after the orbit switch and is stopped if the post rose since */
#define ORBIT_POST 9

static int jetsons_handleBallState(sim_tBallStatus *ball, int *inports) {
  (void)inports;
  switch (ball->state) {
    case stTrough1:  if (sol(sLoad) && !core_getSw(swShooter)) return setState(stShooter, 5); break; /* a full lane loses the pulse */
    case stShooter:  if (sol(sLaunch)) return setState(stLaunched, 2); break;
    case stScoop:    if (sol(sScoop)) return setState(stFree, 5); break;
    case stKickout:  if (sol(sKickout)) return setState(stFree, 5); break;
    case stLOrbit:   return setState(locals.since[sPost] < ORBIT_POST ? stLOrbitBack : stLOrbitOut, 3);
    case stROrbit:   return setState(locals.since[sPost] < ORBIT_POST ? stROrbitBack : stROrbitOut, 3);
  }
  return 0;
}

/* HandleMechanics bit 0: the flippers' end-of-stroke switches, closed while a coil of their flipper is on.
   The Orbitty's position is tracked from servo 0's pulses. */
static void jetsons_handleMech(int mech) {
  static const int coils[2][3] = { { sLFlipHigh, sLFlipLow, swLFlipEOS }, { sRFlipHigh, sRFlipLow, swRFlipEOS } };
  int i;
  for (i = 1; i <= 24; i++)
    locals.since[i] = sol(i) ? 0 : locals.since[i] < 10000 ? locals.since[i] + 1 : 10000;
  if (pinheck_servo(0)) locals.orbittyUs = pinheck_servo(0);
  if (mech & 0x01)
    for (i = 0; i < 2; i++) {
      const int on = sol(coils[i][0]) || sol(coils[i][1]);
      if (on != locals.eos[i]) core_setSw(coils[i][2], locals.eos[i] = on); /* only on change: the switch test may toggle it */
    }
}

/* getMech(0): the Orbitty's angle in degrees (544-2,400 us = 0-180), -1 before its first pulse */
static int jetsons_getMech(int mechNo) {
  if (mechNo == 0) return locals.orbittyUs ? (locals.orbittyUs - 544) * 180 / (2400 - 544) : -1;
  return 0;
}

static sim_tInportData jetsons_inportData[] = {
  {0, 0x0005, stLSling},   {0, 0x0006, stRSling},
  {0, 0x0009, stLInlane},  {0, 0x000a, stRInlane},
  {0, 0x0011, stLOutlane}, {0, 0x0012, stROutlane},
  {0, 0x0021, stLOrbit},   {0, 0x0022, stROrbit},
  {0, 0x0041, stLPop},     {0, 0x0042, stRPop},     {0, 0x0040, stLowerPop},
  {0, 0x0081, stLLane},    {0, 0x0082, stRLane},    {0, 0x0080, stMLane},
  {0, 0x0100, stScoop},    {0, 0x0200, stKickout},  {0, 0x0400, stElroy},
  {0, 0x0800, stCaptive},  {0, 0x1000, stExtraBall},
  {0, 0x2000, stDrain},
  {1, 0x0001, stCTargetG}, {1, 0x0002, stCTargetE}, {1, 0x0004, stCTargetO},
  {1, 0x0008, stRTargetR}, {1, 0x0010, stRTargetG}, {1, 0x0020, stRTargetE},
  {0}
};

static sim_tSimData jetsonsSimData = {
  2,                    /* 2 game specific input ports */
  jetsons_stateDef,
  jetsons_inportData,
  { stTrough1, stTrough2, stTrough3, stDrain, stDrain, stDrain, stDrain },
  NULL,                 /* no init */
  jetsons_handleBallState,
  NULL,                 /* no static drawing */
  FALSE,                /* no manual plunger: the Launch Button fires the autolauncher */
  NULL,                 /* no custom key conditions */
  TRUE                  /* autoBall: the keys move the selected ball, or the first free one; Up/Down select among free balls */
};

static core_tLCDLayout jetsons_disp[] = {
  {0, 0, PINHECK_VIDEO_H64, PINHECK_VIDEO_W, CORE_VIDEO, (genf *)pinheck_video, NULL}, {0}
};

/* the 128x64 module, factory POSITION 55; servo levels 0-255 = 0-180 degrees (0.544-2.4 ms) */
static pinheck_tGameData jetsonsGameData = {
  { GEN_PINHECK, jetsons_disp,
    { FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, 4, 0,
      pinheck_getsol, jetsons_handleMech, jetsons_getMech },
    &jetsonsSimData },
  128, 64, 55, 544, 2400, 0, 1
};

static void init_jetsons(void) {
  int i;
  core_gameData = &jetsonsGameData.core;
  memset(&locals, 0, sizeof(locals));
  for (i = 0; i <= 24; i++) locals.since[i] = 10000;
  for (i = 0; i < 2; i++) locals.eos[i] = -1;
}

PINHECK_ROMSTART(jetsons, "JET_V004.PRG", 0x2FF60, CRC(c778cb10) SHA1(a70445cae2e013ac8c14dba7507657e6cf1583ca),
                 "PRP_V002.BIN", CRC(91725a9a) SHA1(c9d4335c0c6872e7d019a58fcdf000a08c93123d))
PINHECK_ROMEND
CORE_CLONEDEFNV(jetsons, pinheck, "The Jetsons", 2017, "Spooky Pinball", gl_mPINHECK, GAME_NOT_WORKING)
```

The game in the games file's list, the driver list and every build:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheckgames.c', [
    (r'''/* Rob Zombie's Spookshow International: sims/pinheck/rzspook.c */
''',
     r'''/* Rob Zombie's Spookshow International: sims/pinheck/rzspook.c */
/* The Jetsons: sims/pinheck/jetsons.c */
'''),
])
edit('src/wpc/driver.c', [
    (r'''DRIVERNV(rzspook)       //pinHeck 2016 Rob Zombie's Spookshow International (V26)
''',
     r'''DRIVERNV(rzspook)       //pinHeck 2016 Rob Zombie's Spookshow International (V26)
DRIVERNV(jetsons)       //pinHeck 2017 The Jetsons (V4)
'''),
])
edit('tests/pinheck/link/register_build.py', [
    (r'''    ('src/wpc/sims/pinheck/rzspook.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/rzspook.o\n'),
]
''',
     r'''    ('src/wpc/sims/pinheck/rzspook.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/rzspook.o\n'),
    ('src/wpc/sims/pinheck/jetsons.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/jetsons.o\n'),
]
'''),
])
EOF
```

Then: `python3 tests/pinheck/link/register_build.py`
Expected: 19 `updated …` lines (`cmake/*` ×10, `src/pinmame.mak`, `vcproj/*` ×8); a second run prints nothing.

- [ ] **Step 4: Build**

The build reads the root `CMakeLists.txt`, a copy that predates the registration, so copy it again first:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11b.log 2>&1; tail -1 build/sdl3pinmame/11b.log
grep -i warning build/sdl3pinmame/11b.log | grep -cE 'jetsons|rzspook|pinheck|dominos|bootldr'
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 5: The three games boot**

The Jetsons' first launch flashes the update, shows `PLEASE RESTART` at 100.5 s and restarts at 104 s (`boot: stopped in programming mode, 197632 bytes programmed`); after the restart its Propeller reboots once and the PIC32 syncs; the second launch prints the banner and takes `[E97000]`; then the romset check runs the system set alone and `jetsons` without media. Domino's and Rob Zombie run the same check as before:

```sh
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
```
Expected (about 2 minutes each):
```text
romset: ok
pinmame: ok
romset: ok
pinmame: ok
romset: ok
pinmame: ok
```

- [ ] **Step 6: The unit suite, the launch lint, the symbols and the Windows projects**

Run: `flock /code/spooky_domino/work/emu.lock nice -n 15 tests/pinheck/pic32mx/check.sh | tail -6`
Expected:
```text
logical 16 -> RF5  COMM_OUT, PIC32 -> Propeller data
pins: ok (87 logical pins resolved)
makefile link: ok
symbols: ok
vcxproj: ok
pic32mx: 0 failed
```

- [ ] **Step 7: Commit**

```bash
git add src/wpc/sims/pinheck/jetsons.c src/wpc/pinheckgames.c src/wpc/driver.c src/wpc/pinheck.c src/wpc/pinheck/bootldr.h src/wpc/pinheck/bootldr.c tests/pinheck/games.sh tests/pinheck/pic32mx tests/pinheck/link/register_build.py src/pinmame.mak cmake vcproj
git commit -m "pinheck: The Jetsons (jetsons): game, simulator, boot check by game (update without LEAVE_PROGMODE, Propeller reboot before the sync)"
```

### Task 4: The simulator check

**Files:**
- Create: `tests/pinheck/sim/jetsim.py`
- Modify: `tests/pinheck/games.sh`, `tests/pinheck/sim/pinmame_sim.sh`

**Interfaces:**
- Consumes: Task 3's simulator; Plan 8b's `PINHECK_OUT_LOG` `W`, `S`, `V` and `P` lines and the console's `[MXXzzz]`.
- Produces: `jetsim.py plan DIR` / `verify DIR`, as `sim.py`; `games.sh` `TROUGH1`.

The check runs as Plan 8b's: a first boot in service, then attract mode with ball search off, where nothing moves a ball unless the check does. Both servos must sit at 1,476 µs and level 128 (Ruling 10). One ball goes from the trough to the shooter lane, from which the firmware launches it, into the scoop, which the firmware ejects, into the kickout hole until coil 19 fires, round the left orbit to the right orbit, then up the left orbit against the raised post and back; the four flipper coils close and open their end-of-stroke switches; the ball drains; a second ball is loaded, launched and drained.

- [ ] **Step 1: The check**

`tests/pinheck/sim/jetsim.py`:

```python
#!/usr/bin/env python3
"""The Jetsons simulator check: in attract mode the firmware's console coil commands ([MXXzzz]) and the
simulator keys move the balls; the switches the simulator closes (PINHECK_OUT_LOG 'W' lines) must follow
the coils, and the servo outputs follow the game's servo range.
  jetsim.py plan DIR     write DIR/send, send_at, send_gap, keys.txt, frames
  jetsim.py verify DIR   check DIR/out2.log against the plan"""
import sys

FPS = 60
SEND_AT, GAP = 12.0, 0.05
# (time, console command) and (time, keys, hold frames): a ball's trip round the playfield, then two drains.
# [MXX002] pulses a coil for about 5 ms, shorter than a frame; [M16250] holds the up-post up for longer.
# The firmware itself launches a ball that reaches the shooter lane and ejects the scoop in attract mode
SEND = [(12.0, '[E97000]'), (14.0, '[M04002]'), (20.5, '[M19002]'),
        (23.55, '[M16250]'), (25.0, '[M02002]'), (25.5, '[M03002]'), (26.0, '[M06002]'), (26.5, '[M07002]'),
        (29.0, '[M04002]')]
KEYS = [(17.0, 'S', 1), (19.5, 'K', 1), (22.0, 'LCONTROL R', 1), (23.5, 'LCONTROL R', 1), (28.0, 'Q', 1), (31.0, 'Q', 1)]
END = 33.0
TROUGH = [92, 53, 54]                   # trough 1 (the eject position: Trough Opto 1), 2, 3
SERVO_US = (544, 2400)                  # the game's servo levels 0 and 255


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

    rest = [s for s in [51] + TROUGH if state(s, 11.0)]
    check(rest == TROUGH, 'at rest closed %s; expected the trough %s' % (rest, TROUGH))
    for n, us in ((0, 1476), (1, 1476)):
        w = sorted(set(round(u) for t, s, u in servo if s == n and 13.0 <= t < 14.0))
        lv = round((us - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
        check(w and all(abs(u - us) < 10 for u in w) and abs((level('S%d' % (57 + n), 14.0) or -9) - lv) <= 1,
              'servo %d (%s) in attract: %s us and output %d at %s; expected %d us (90 degrees) and %d' %
              (n, 'the Orbitty' if n == 0 else 'servo 1', w, 57 + n, level('S%d' % (57 + n), 14.0), us, lv))
    load_ = coil_on(5, 14.0, 15.0)
    check(load_ and edges(92, load_[0], load_[0] + 0.1, 0) and edges(51, load_[0], load_[0] + 1.0, 1),
          'trough: [M04002] at 14.0 (coil 5 %s) did not move trough 1 (92) to the shooter lane (51)' % load_[:1])
    check([state(s, 15.4) for s in TROUGH] == [1, 1, 0], 'trough: the two balls did not roll down to 92 and 53: %s' % [state(s, 15.4) for s in TROUGH])
    lane = edges(51, 14.0, 15.0, 1)
    launch = coil_on(6, lane[0], lane[0] + 0.3) if lane else []
    check(launch and edges(51, launch[0], launch[0] + 0.1, 0), 'autolauncher: the firmware did not launch the ball from the shooter lane (51 %s, coil 6 %s)' % (lane[:1], launch[:1]))
    into = edges(96, 17.0, 17.2, 1)
    scoop = coil_on(9, into[0], into[0] + 2.0) if into else []
    check(scoop and edges(96, scoop[0], scoop[0] + 0.1, 0) and not edges(96, into[0], scoop[0], 0),
          'scoop: opto 96 closed %s, the firmware fired coil 9 %s, released %s' % (into[:1], scoop[:1], edges(96, 17.0, 19.0, 0)))
    kick = coil_on(20, 20.4, 21.0)
    check(state(18, 20.4) == 1 and kick and edges(18, kick[0], kick[0] + 0.1, 0),
          'kickout hole: switch 18 held %d, coil 20 %s, released %s' % (state(18, 20.4), kick[:1], edges(18, 20.4, 21.0, 0)))
    check(edges(56, 22.0, 22.2, 1) and edges(11, 22.0, 22.6, 1), 'orbit: a left orbit shot without the post did not reach the right orbit (11)')
    post = coil_on(17, 23.5, 23.7)
    check(post and len(edges(56, 23.5, 24.2, 1)) == 2 and not edges(11, 23.5, 24.6),
          'up-post: coil 17 at %s, left orbit closed %s, right orbit %s: the ball must come back down the left orbit' %
          (post[:1], edges(56, 23.5, 24.2, 1), edges(11, 23.5, 24.6)))
    for c, s, t in ((3, 55, 25.0), (4, 55, 25.5), (7, 26, 26.0), (8, 26, 26.5)):
        on = coil_on(c, t, t + 0.4)
        check(on and edges(s, on[0], on[0] + 0.05, 1) and edges(s, on[0], on[0] + 0.1, 0),
              'flipper coil %d (%s) did not close and open its end-of-stroke switch %d' % (c, on[:1], s))
    check([state(s, 28.95) for s in [51] + TROUGH] == [0, 1, 1, 1], 'drain: the ball did not return to the trough (51, %s: %s)' % (TROUGH, [state(s, 28.95) for s in [51] + TROUGH]))
    check(len(edges(51, 29.0, 30.5, 1)) == 1 and len(edges(51, 29.0, 30.5, 0)) == 1, 'second ball: the load and the firmware\'s launch did not each move it')
    check([state(s, 32.5) for s in [51] + TROUGH] == [0, 1, 1, 1], 'second ball: Q did not drain it to the trough (51, %s: %s)' % (TROUGH, [state(s, 32.5) for s in [51] + TROUGH]))
    for f in fails:
        print('SIM FAIL: ' + f)
    print('sim: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'verify'):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2]))
```

`chmod +x tests/pinheck/sim/jetsim.py`

`pinmame_sim.sh` waits for the ball in the game's trough 1:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/games.sh', [
    (r'''#   SIM         the simulator check (tests/pinheck/sim)
''',
     r'''#   SIM         the simulator check (tests/pinheck/sim)
#   TROUGH1     the switch a ball in trough 1 (the eject position) closes
'''),
    (r'''CLIP=LT5 SIM=sim.py
''',
     r'''CLIP=LT5 SIM=sim.py TROUGH1=12
'''),
    (r'''CLIP=DMB SIM=rzsim.py
''',
     r'''CLIP=DMB SIM=rzsim.py TROUGH1=12
'''),
    (r'''CLIP=TL1 SIM=jetsim.py
''',
     r'''CLIP=TL1 SIM=jetsim.py TROUGH1=92
'''),
])
edit('tests/pinheck/sim/pinmame_sim.sh', [
    (r'''grep -q ' W .*12=1' $B/out1.log || { echo "SIM FAIL: launch 1 logged no ball in the trough (PINHECK_OUT_LOG 'W' lines)"; exit 1; }
''',
     r'''grep -qE " W (.* )?$TROUGH1=1" $B/out1.log || { echo "SIM FAIL: launch 1 logged no ball in the trough (PINHECK_OUT_LOG 'W' lines)"; exit 1; }
'''),
])
EOF
```

- [ ] **Step 2: The three simulator checks pass**

```sh
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh
```
Expected (under a minute each):
```text
sim: 17 checks, 0 failures
pinmame sim: ok
sim: 19 checks, 0 failures
pinmame sim: ok
sim: 24 checks, 0 failures
pinmame sim: ok
```

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/sim tests/pinheck/games.sh
git commit -m "pinheck: The Jetsons' simulator check (trough and optos, firmware launch, scoop, kickout, up-post, end-of-stroke switches, servos)"
```

### Task 5: The service tests (check 5)

**Files:**
- Modify: `tests/pinheck/board/check5.py`

**Interfaces:**
- Consumes: Plan 8a's check 5 and Plan 11a's per-game parts; the switch, lamp and coil lists (research §4).
- Produces: check 5 for `PINHECK_GAME=jetsons`; `check5.py`'s `MX`, `CX` (the switch test's matrix and cabinet columns) and `CAB_REST`.

For The Jetsons the console commands and the lamp and RGB tests are Domino's; the menu, the switch-test screen, the attract mode and the resting balls differ (Ruling 11). Check 5 reads the frame log at the module's size (8,192-byte frames for The Jetsons); its switch test sits in the top 32 rows of both modules.

- [ ] **Step 1: Check 5 for The Jetsons**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/board/check5.py', [
    (r'''PINHECK_GAME (dominos, rzspook) selects the game's servo and RGB tests and its resting balls."""
''',
     r'''PINHECK_GAME (dominos, rzspook, jetsons) selects the game's servo and RGB tests, its menu, its switch-test
screen and its resting balls."""
'''),
    (r'''FRAME = 4096
''',
     r'''FRAME = 128 * (64 if os.environ.get('PINHECK_GAME') == 'jetsons' else 32)   # The Jetsons' module is 128x64
'''),
    (r'''MENU_AT = 10.5                                      # attract mode runs light shows: test from the service menu
SEND_AT, GAP = 11.5, 0.25
KEYS_AT = 39.0
''',
     r'''GAME = os.environ.get('PINHECK_GAME', 'dominos')
MENU_AT = 10.5                                      # attract mode runs light shows: test from the service menu
SEND_AT, GAP = 11.5, 0.25
KEYS_AT = 39.0
if GAME == 'jetsons':                               # its Propeller reboots once before the sync (games.sh REBOOTS)
    MENU_AT, SEND_AT, KEYS_AT = 16.0, 17.0, 44.5
'''),
    (r'''GAME = os.environ.get('PINHECK_GAME', 'dominos')
if GAME == 'rzspook':
''',
     r'''if GAME == 'rzspook':
'''),
    (r'''    tap('7', 1.0)                                   # back to SOLENOID
    tap('RSHIFT', 1.0)                              # SERVO
    if GAME == 'rzspook':
''',
     r'''    tap('7', 1.0)                                   # back to SOLENOID
    if GAME != 'jetsons':                           # The Jetsons has no servo test
        tap('RSHIFT', 1.0)                          # SERVO
    if GAME == 'jetsons':
        pass
    elif GAME == 'rzspook':
'''),
    (r'''        tap('0', 1.5, ('servo', 0, 255))            # servo 0 runs at 180 deg
    tap('7', 1.0)                                   # back to SERVO
    tap('RSHIFT', 1.0)                              # LAMP
''',
     r'''        tap('0', 1.5, ('servo', 0, 255))            # servo 0 runs at 180 deg
    if GAME != 'jetsons':
        tap('7', 1.0)                               # back to SERVO
    tap('RSHIFT', 1.0)                              # LAMP
'''),
    (r'''    tap('7', 1.0)                                   # back to RGB LIGHTING
    for i in range(5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
''',
     r'''    tap('7', 1.0)                                   # back to RGB LIGHTING
    for i in range(4 if GAME == 'jetsons' else 5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
'''),
    (r'''    for n in range(64):
        k = 'KEYCODE_%s KEYCODE_%s' % (COLS[n // 8], ROWS[n % 8])
        tap(k, 16 / FPS, ('switch', (n,), (1,)), hold=4)
        tap(k, 16 / FPS, ('switch', (), (1,)), hold=4)
    for key, cab in (('1', 12), ('5', 7), ('INSERT', 8), ('9', 2), ('0', 6)):
        tap(key, 16 / FPS, ('switch', (), (1, cab)), hold=12)
        tap(None, 16 / FPS, ('switch', (), (1,)))
    tap('END', 32 / FPS, ('switch', (), ()), hold=4)
    tap('END', 32 / FPS, ('switch', (), (1,)), hold=4)
    for key, cab in (('LSHIFT', 4), ('RSHIFT', 3)):
        tap(key, 16 / FPS, ('switch', (), (1, cab)), hold=12)
        tap(None, 16 / FPS, ('switch', (), (1,)))
''',
     r'''    for n in range(64):
        k = 'KEYCODE_%s KEYCODE_%s' % (COLS[n // 8], ROWS[n % 8])
        tap(k, 16 / FPS, ('switch', (n,), CAB_REST), hold=4)
        tap(k, 16 / FPS, ('switch', (), CAB_REST), hold=4)
    for key, cab in (('1', 12), ('5', 7), ('INSERT', 8), ('9', 2), ('0', 6)):
        tap(key, 16 / FPS, ('switch', (), tuple(sorted(CAB_REST + (cab,)))), hold=12)
        tap(None, 16 / FPS, ('switch', (), CAB_REST))
    tap('END', 32 / FPS, ('switch', (), CAB_REST[1:]), hold=4)
    tap('END', 32 / FPS, ('switch', (), CAB_REST), hold=4)
    for key, cab in (('LSHIFT', 4), ('RSHIFT', 3)):
        tap(key, 16 / FPS, ('switch', (), tuple(sorted(CAB_REST + (cab,)))), hold=12)
        tap(None, 16 / FPS, ('switch', (), CAB_REST))
'''),
    (r'''def grid(f):
    """switch test screen: closed matrix switches (column 0 drawn on the right) and cabinet inputs (0-7 right, 8-15 left)"""
    px = lambda x, y: f[y * 128 + x]
    g, c2 = px(0, 0), px(32, 0)
    if not g or not c2:
        return None
    for y in range(32):
        for x in range(40):
            if (x % 4 in (0, 3) or y % 4 in (0, 3)) and px(x, y) != (g if x < 32 else c2):
                return None
    if any(px(x, y) == g for x in range(1, 31, 4) for y in range(1, 31, 4)):
        return None
    sw = tuple(c * 8 + r for c in range(8) for r in range(8) if px((7 - c) * 4 + 1, r * 4 + 1))
    cab = tuple(k for k in range(16) if px(37 if k < 8 else 33, (k % 8) * 4 + 1))
    return sw, cab
''',
     r'''def grid(f):
    """switch test screen: closed matrix switches (column 0 drawn on the right) and cabinet inputs (0-7 right, 8-15 left);
    the matrix at x = MX, the cabinet columns at x = CX"""
    px = lambda x, y: f[y * 128 + x]
    g, c2 = px(MX, 0), px(CX, 0)
    if not g or not c2:
        return None
    for y in range(32):
        for x in range(40):
            if (x % 4 in (0, 3) or y % 4 in (0, 3)) and px(x, y) != (g if MX <= x < MX + 32 else c2):
                return None
    if any(px(x, y) == g for x in range(MX + 1, MX + 31, 4) for y in range(1, 31, 4)):
        return None
    sw = tuple(c * 8 + r for c in range(8) for r in range(8) if px(MX + (7 - c) * 4 + 1, r * 4 + 1))
    cab = tuple(k for k in range(16) if px(CX + (5 if k < 8 else 1), (k % 8) * 4 + 1))
    return sw, cab
'''),
    (r'''SERVO_US = (544, 2400) if GAME == 'rzspook' else (1000, 2000)   # the game's servo levels 0 and 255
''',
     r'''SERVO_US = (544, 2400) if GAME in ('rzspook', 'jetsons') else (1000, 2000)   # the game's servo levels 0 and 255
# the switch test: the matrix's and the cabinet columns' x; cabinet inputs closed at rest (the coin door, and
# The Jetsons' trough opto under the ball the tests leave in the trough)
MX, CX = (8, 0) if GAME == 'jetsons' else (0, 32)
CAB_REST = (1, 10) if GAME == 'jetsons' else (1,)
'''),
    (r'''    check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
    check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [255] * 3, 'external LED 0 is not white on 62-64')
    if GAME == 'rzspook':
''',
     r'''    if GAME == 'jetsons':
        # the start lamp stays lit; the external LED is never written; servos 0 (the Orbitty) and 1 at 90 degrees,
        # 1,476 us, in the game's range
        check(o.at('L91', MENU_AT) >= 128, 'start lamp 91 is not lit in attract')
        check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [0] * 3, 'external LED 0 on 62-64 is lit')
        for n, us in ((0, 1476), (1, 1476)):
            w = sorted(set(round(u) for tt, u in o.servo.get(n, []) if MENU_AT - 1.0 <= tt < MENU_AT))
            level = round((us - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
            check(w and all(abs(u - us) < 10 for u in w), 'attract servo %d pulses %s us, expected %d' % (n, w, us))
            check(abs(o.at('S%d' % (57 + n), MENU_AT) - level) <= 1, 'attract servo output %d is %d, expected %d' % (57 + n, o.at('S%d' % (57 + n), MENU_AT), level))
    else:
        check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
        check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [255] * 3, 'external LED 0 is not white on 62-64')
    if GAME == 'jetsons':
        pass
    elif GAME == 'rzspook':
'''),
    (r'''    want = {0, 1, 2, 3, 4, 5} if GAME == 'rzspook' else {0, 1}
    check(rest and base - {39} == want and rest[-1][1] == (1,), 'switch test at rest shows %s, expected switches %s, maybe Noid Home (39), and the closed coin door' % (rest[-1:], sorted(want)))
''',
     r'''    # The Jetsons: the console's load and launch and the solenoid test's LOAD COIL and PLUNGER put two balls on the
    # playfield; the third rests on trough opto 1 (cabinet 10)
    want = {0, 1, 2, 3, 4, 5} if GAME == 'rzspook' else set() if GAME == 'jetsons' else {0, 1}
    check(rest and base - {39} == want and rest[-1][1] == CAB_REST, 'switch test at rest shows %s, expected switches %s, maybe Noid Home (39), and cabinet inputs %s' % (rest[-1:], sorted(want), CAB_REST))
'''),
    (r'''    shown = set(g for g in (grid(f) for tt, f in frames if tt >= KEYS_AT) if g)
''',
     r'''    shown = [g for g in (grid(f) for tt, f in frames if tt >= KEYS_AT) if g]
    shown = set(shown[1:])                          # the test's first frame is drawn before the inputs are read
'''),
])
EOF
```

- [ ] **Step 2: Domino's and Rob Zombie's plans are unchanged**

```sh
mkdir -p build/c5a build/c5b && git show HEAD:tests/pinheck/board/check5.py > build/check5-before.py
for g in dominos rzspook; do PINHECK_GAME=$g python3 build/check5-before.py plan build/c5a && PINHECK_GAME=$g python3 tests/pinheck/board/check5.py plan build/c5b
	for f in send send_at send_gap keys.txt frames; do cmp -s build/c5a/$f build/c5b/$f || echo "differs: $g $f"; done; done; echo "check 5 plans compared"
rm -rf tests/pinheck/board/__pycache__
```
Expected: `check 5 plans compared` and no `differs` line.

- [ ] **Step 3: The three games' check 5**

```sh
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh
```
Expected (about 2–3 minutes each; the brightness levels may differ in the third decimal):
```text
timing: 39902 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.580 0.706 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 264 service-test steps, 0 failures
pinmame board: ok
timing: 74937 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.580 0.722 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 266 service-test steps, 0 failures
pinmame board: ok
timing: 74878 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.588 0.706 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 272 service-test steps, 0 failures
pinmame board: ok
```

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/board/check5.py
git commit -m "pinheck: check 5 for The Jetsons (no servo test, switch-test screen, attract servos and start lamp)"
```

### Task 6: The 128×64 frames in the display check and in libpinmame

**Files:**
- Modify: `tests/pinheck/display/frames.py`, `tests/pinheck/display/render.py`, `tests/pinheck/games.sh`, `tests/pinheck/display/pinmame_display.sh`, `tests/pinheck/display/pinmame_look.sh`, `tests/pinheck/vpx/vpx.py`, `tests/pinheck/vpx/pinmame_vpx.sh`

**Interfaces:**
- Consumes: Task 2's frame log (20 + `disp.frame` bytes per record) and window; Plan 6's display check (`[V00<clip>]` pixel-exact against the `.VID`, a screenshot against the decoded frames); Plan 10's libpinmame host (`host`, `vpx.py displays|media`).
- Produces: `frames.py`'s `W`, `H`, `FRAME` from `PINHECK_GAME` (128×64 for `jetsons`); `games.sh` `LOOK` (`menu` or `none`); `pinmame_vpx.sh` by game (`jetsons`: display and sound only).

- [ ] **Step 1: The checks by module**

`frames.py` reads 8,212-byte records for the 128×64 module and asserts that none of its frames is whole in hub RAM (Ruling 5); `render.py` compares the 128×64 frame 2×2 as sent; the display check asserts 12-byte config packets and allows the two start-up discards for a module without a look (Rulings 2 and 4); the look check does not apply to it; libpinmame's media check takes the module's size:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/frames.py', [
    (r'''import argparse
import struct
import sys

FRAME = 4096
''',
     r'''import argparse
import os
import struct
import sys

# the module PINHECK_GAME has: 128x32, or The Jetsons' 128x64, whose firmware keeps no whole frame in hub RAM
W, H = 128, 64 if os.environ.get('PINHECK_GAME') == 'jetsons' else 32
FRAME = W * H
'''),
    (r'''    return '\n'.join(''.join('#' if f[y * 128 + x] else '.' for x in range(128)) for y in range(32))
''',
     r'''    return '\n'.join(''.join('#' if f[y * W + x] else '.' for x in range(W)) for y in range(H))
'''),
    (r'''    hub = sorted(set(at for _, _, at, f in recs if f.count(f[0]) != FRAME))
    if not hub:
        sys.exit('frames: FAIL, every frame is uniform')
    if hub != [hub[0]] or hub[0] == 0xFFFFFFFF:
''',
     r'''    hub = sorted(set(at for _, _, at, f in recs if f.count(f[0]) != FRAME))
    if not hub:
        sys.exit('frames: FAIL, every frame is uniform')
    if H == 64:
        if hub != [0xFFFFFFFF]:
            sys.exit('frames: FAIL, a 128x64 frame is whole in hub RAM at %s' % ['$%04x' % h for h in hub if h != 0xFFFFFFFF])
        print('frames: no 128x64 frame is whole in hub RAM (the firmware sends it from two 1024-byte pages)')
    elif hub != [hub[0]] or hub[0] == 0xFFFFFFFF:
'''),
    (r'''    print('frames: every non-uniform frame equals the firmware framebuffer at hub $%04x when latched' % hub[0])
''',
     r'''    else:
        print('frames: every non-uniform frame equals the firmware framebuffer at hub $%04x when latched' % hub[0])
'''),
])
edit('tests/pinheck/display/render.py', [
    (r'''"""Check a PinMAME screen snapshot of a pinHeck game against the decoded frame log:
the visible area is SCALE x the 128x32 frame wide and the full screen high, the frame is
''',
     r'''"""Check a PinMAME screen snapshot of a pinHeck game against the decoded frame log:
the visible area is SCALE x the 128-dot frame wide and the full screen high, the frame (128x32 in
the module's look, 128x64 as sent) is
'''),
    (r'''from frames import FRAME, read_log  # noqa: E402
from look import parse as parse_look, render as render_look  # noqa: E402

W, H = 128, 32
SCALE = 2
''',
     r'''from frames import FRAME, W, H, read_log  # noqa: E402
from look import parse as parse_look, render as render_look  # noqa: E402

SCALE = 2
'''),
    (r'''    """the frame region shows f in the module's look (look.py; the exact look draws each dot SCALE x SCALE)"""
    img = render_look(look, f)
''',
     r'''    """the frame region shows f in the module's look (look.py; the exact look draws each dot SCALE x SCALE);
    the 128x64 module has no look"""
    if H != 32:
        return all(near(rows[y][x], rgb332(f[(y // SCALE) * W + x // SCALE])) for y in range(H * SCALE) for x in range(W * SCALE))
    img = render_look(look, f)
'''),
])
edit('tests/pinheck/games.sh', [
    (r'''#   CLIP        a .VID clip the display check plays ([V00<CLIP>])
''',
     r'''#   CLIP        a .VID clip the display check plays ([V00<CLIP>])
#   LOOK        the module's look: menu (the 128x32 module's 14-byte config packets), or none (The Jetsons'
#               128x64 module: 12-byte packets, drawn as sent)
'''),
    (r'''STORED=600BAFA CLIP=LT5''',
     r'''STORED=600BAFA LOOK=menu CLIP=LT5'''),
    (r'''STORED=1A00BAFA CLIP=DMB''',
     r'''STORED=1A00BAFA LOOK=menu CLIP=DMB'''),
    (r'''STORED=400BAFA CLIP=TL1''',
     r'''STORED=400BAFA LOOK=none CLIP=TL1'''),
])
edit('tests/pinheck/display/pinmame_display.sh', [
    (r'''grep -q "^display: config" $B/prop1.log || { echo "PINMAME FAIL: no display config packet"; fail=1; }
''',
     r'''grep -q "^display: config" $B/prop1.log || { echo "PINMAME FAIL: no display config packet"; fail=1; }
if [ $LOOK = none ]; then
	grep -q "^display: config\( ..\)\{12\}$" $B/prop1.log && grep -q "^display: unknown config packet, exact pixels" $B/prop1.log &&
		! grep "^display: config" $B/prop1.log | grep -qv "^display: config\( ..\)\{12\}$" ||
		{ echo "PINMAME FAIL: $GAME's module must get 12-byte config packets only, drawn as sent"; fail=1; }
fi
'''),
    (r'''grep "^display: \(frame\|latch\|mode\)" $B/prop1.log $B/prop2.log && { echo "PINMAME FAIL: malformed display transfers"; fail=1; }
''',
     r'''# The Jetsons' display cog starts with a lone 1024-byte burst, and its reboot before the sync cuts a frame short
skip='^$'
[ $LOOK = none ] && skip='display: frame of \(1024\|7168\) bytes discarded$'
grep "^display: \(frame\|latch\|mode\)" $B/prop1.log $B/prop2.log | grep -v "$skip" && { echo "PINMAME FAIL: malformed display transfers"; fail=1; }
'''),
])
edit('tests/pinheck/display/pinmame_look.sh', [
    (r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
[ $LOOK = none ] && { echo "pinmame look: $GAME has no look (its module is drawn as sent: pinmame_display.sh)"; exit 0; }
'''),
])
EOF
```

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/vpx/vpx.py', [
    (r'''FPS = 60
W, H = 128, 32
''',
     r'''FPS = 60
W, H = 128, 64 if os.environ.get('PINHECK_GAME') == 'jetsons' else 32    # the module PINHECK_GAME has
'''),
    (r'''    if avail != ['avail 0 1 type 15 128x32 depth 16 length 0']:
        fails.append('display announced as %s, expected one 128x32 VIDEO display of depth 16' % avail)
''',
     r'''    if avail != ['avail 0 1 type 15 %dx%d depth 16 length 0' % (W, H)]:
        fails.append('display announced as %s, expected one %dx%d VIDEO display of depth 16' % (avail, W, H))
'''),
])
edit('tests/pinheck/vpx/pinmame_vpx.sh', [
    (r'''# Domino's through libpinmame (spec M10 4.1): display, sound, outputs, switches and mechanics as a host receives them
''',
     r'''# Domino's through libpinmame (spec M10 4.1): display, sound, outputs, switches and mechanics as a host receives them;
# with PINHECK_GAME=jetsons The Jetsons' 128x64 display and its sound only
'''),
    (r''': "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
''',
     r''': "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
'''),
    (r''': "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
''',
     r''': "${PINHECK_ZIP:?set PINHECK_ZIP to the game's romset zip}"
'''),
    (r'''cd "$(dirname "$0")" || exit 2
S=../../../src
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
S=../../../src
'''),
    (r'''ln -s "$PINHECK_ZIP" $B/boot/roms/dominos.zip || exit 2
''',
     r'''ln -s "$PINHECK_ZIP" $B/boot/roms/$GAME.zip || exit 2
'''),
    (r'''launch boot dominos 1200 .
python3 vpx.py displays $B/boot || fail=1
python3 vpx.py media $B/boot $B/boot/capture.wav || fail=1
''',
     r'''launch boot $GAME 1200 .
python3 vpx.py displays $B/boot || fail=1
python3 vpx.py media $B/boot $B/boot/capture.wav || fail=1
if [ $GAME = jetsons ]; then
	# a video clip through the plugin message API, after The Jetsons' later sync
	PINHECK_UART1_SEND_AT=14 PINHECK_UART1_SEND="[V00$CLIP]" launch media -p $GAME 1500 .
	grep -aq "Playing Video" $B/media/uart.log || { echo "VPX FAIL: [V00$CLIP] not acknowledged"; fail=1; }
	python3 vpx.py media $B/media $B/media/capture.wav || fail=1
	[ $fail -eq 0 ] || exit 1
	echo "pinmame vpx: ok ($GAME: display and sound)"
	exit 0
fi
'''),
])
EOF
```

- [ ] **Step 2: The display checks**

```sh
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_look.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh
```
Expected (about a minute each):
```text
frames: 501 frames from 1.70s to 20.00s, 29.2 per second
frames: no 128x64 frame is whole in hub RAM (the firmware sends it from two 1024-byte pages)
render: frame region equals decoded frame 2 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (19 pens, none a frame colour)
frames: 93 frames from 12.00s to 15.01s, 30.6 per second
frames: no 128x64 frame is whole in hub RAM (the firmware sends it from two 1024-byte pages)
frames: TL1.VID: frames 0..44 of 45 shown pixel-exact, contiguous and in order (log frames 29..73)
render: frame region equals decoded frame 1 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (13 pens, none a frame colour)
render: the core's solenoid log shows (104 pixels)
pinmame display: ok
pinmame look: jetsons has no look (its module is drawn as sent: pinmame_display.sh)
frames: 394 frames from 1.68s to 20.02s, 21.4 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $5870 when latched
render: frame region equals decoded frame 7 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (4 pens, none a frame colour)
frames: 75 frames from 12.02s to 14.99s, 24.9 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $5870 when latched
frames: LT5.VID: frames 1..33 of 34 shown pixel-exact, contiguous and in order (log frames 1..33)
render: frame region equals decoded frame 7 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (13 pens, none a frame colour)
render: the core's solenoid log shows (110 pixels)
pinmame display: ok
```

- [ ] **Step 3: libpinmame exports 128×64**

Build libpinmame on Linux as Plan 10 did, then run The Jetsons through it: the display list must announce one `128x64` VIDEO display of depth 16, and every frame on the panel for a vblank or more must reach the host by the callback and by the plugin message API, with the sound equal to the driver's capture:

```sh
mkdir -p build/libpinmame-src && ln -sfn ../../cmake build/libpinmame-src/cmake && ln -sfn ../../ext build/libpinmame-src/ext && ln -sfn ../../src build/libpinmame-src/src
cp cmake/libpinmame/CMakeLists.txt build/libpinmame-src/CMakeLists.txt
cmake -S build/libpinmame-src -B build/libpinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DBUILD_STATIC=OFF > build/libpinmame-cfg.log 2>&1
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
jet env LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh
```
Expected (about 2 minutes after the build):
```text
displays: boot announces 1 display(s), indices [0], updates [1200]
media: callback path: 144 frames, each a decoded frame, in order; the frame log has 144 distinct frames
media: callback path: all 143 frames on the panel for a vblank or more; 1 shorter ones, 1 of them shown
media: 1764000 API samples, 1764002 captured, largest difference 1, 100019 loud samples
media: callback path: 312 frames, each a decoded frame, in order; the frame log has 313 distinct frames
media: callback path: all 310 frames on the panel for a vblank or more; 3 shorter ones, 2 of them shown
media: plugin path: 312 frames, each a decoded frame, in order; the frame log has 313 distinct frames
media: plugin path: all 310 frames on the panel for a vblank or more; 3 shorter ones, 2 of them shown
media: 2205000 API samples, 2205002 captured, largest difference 1, 185341 loud samples
pinmame vpx: ok (jetsons: display and sound)
```
(the clip run's frame counts can differ by one between runs: a frame shorter than a vblank may or may not reach the host)

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/display tests/pinheck/vpx/vpx.py tests/pinheck/vpx/pinmame_vpx.sh tests/pinheck/games.sh
git commit -m "pinheck: the 128x64 module in the display check (TL1.VID pixel-exact, drawn 2x2 as sent) and in libpinmame (128x64)"
```

### Task 7: Names and the VBScript constants

**Files:**
- Modify: `src/wpc/pinheck_names.h`, `tests/pinheck/vpx/names.py`, `tests/pinheck/vpx/check.sh`

**Interfaces:**
- Consumes: Plan 10's `pinheck_name_t` and `names.py`; Task 1's `vbs:` check.
- Produces: `pinheck_jetsons_switch_names[]`, `pinheck_jetsons_lamp_names[]`, `pinheck_jetsons_solenoid_names[]`; `names.py check|vbs SRC jetsons`.

- [ ] **Step 1: Domino's script before the change**

Run: `python3 tests/pinheck/vpx/names.py vbs src > build/pinheck-before.vbs`

- [ ] **Step 2: The names**

Switch and lamp names are the charts', coils, GI and optos the wire-to-board sheet's, numbered where the firmware reads them (Ruling 8); ids are `jetsons.c`'s constants:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck_names.h', [
    (r'''   its coil, bit 1 closes the flippers' end-of-stroke switches while their coils are on. */
''',
     r'''   its coil, bit 1 closes the flippers' end-of-stroke switches while their coils are on.

   The Jetsons (pinheck_jetsons_*).
   Sources: Jetsons_Switch_Matrix_Production.pdf, Jetsons_Light_Matrix_Production.pdf and JETSONS-WIRE-TO-BOARD.pdf
   (coils, GI, optos); numbering as above. A device is numbered where JET_V004 reads it: the kickout hole on
   switch 7 (18; the switch chart says 6), the trough's eject position and the scoop on their optos (92, 96;
   the chart's switches 33 and 8 are never read). Not listed: switches 14, 15, 22, 23, 30, 31, 38, 39 and lamps
   45-47, which the firmware uses but no chart names. Servo levels 0-255 are 0-180 degrees (0.544-2.4 ms); servo 0
   is the Orbitty topper. The firmware never writes the external RGB LED (62-64). HandleMechanics bit 0 closes
   the flippers' end-of-stroke switches while their coils are on. */
'''),
    (r'''  { 64, "sLDGG",       "LDG light green" },
  { 0 }
};
''',
     r'''  { 64, "sLDGG",       "LDG light green" },
  { 0 }
};

static const pinheck_name_t pinheck_jetsons_switch_names[] = {
  {   1, "swCoinDoor",  "Coin Door (closed)" },
  {   2, "swUser",      "Launch Button" },
  {   3, "swRFlip",     "Right Flipper" },
  {   4, "swLFlip",     "Left Flipper" },
  {   5, "swBack",      "Back" },
  {   6, "swEnter",     "Enter (menu)" },
  {   7, "swCoin",      "Coin Mech" },
  {   8, "swTilt",      "Tilt" },
  {  11, "swROrbit",    "Right Orbit" },
  {  12, "swCaptive",   "Captive Ball" },
  {  13, "swRLane",     "Right Lane" },
  {  14, "swMLane",     "Middle Lane" },
  {  15, "swLLane",     "Left Lane" },
  {  16, "swElroy",     "Elroy Loop" },
  {  18, "swKickout",   "Kickout Hole" },
  {  22, "swExtraBall", "Extra Ball" },
  {  23, "swLSling",    "Left Sling" },
  {  24, "swLInlane",   "Left Inlane" },
  {  25, "swLOutlane",  "Left Outlane" },
  {  26, "swLFlipEOS",  "Left Flipper EOS" },
  {  31, "swCTargetG",  "Center Target G" },
  {  32, "swCTargetE",  "Center Target E" },
  {  33, "swCTargetO",  "Center Target O" },
  {  34, "swLowerPop",  "Lower Pop" },
  {  35, "swLPop",      "Left Pop" },
  {  36, "swRPop",      "Right Pop" },
  {  41, "swRTargetR",  "Right Target R" },
  {  42, "swRTargetG",  "Right Target G" },
  {  43, "swRTargetE",  "Right Target E" },
  {  44, "swRSling",    "Right Sling" },
  {  45, "swRInlane",   "Right Inlane" },
  {  46, "swROutlane",  "Right Outlane" },
  {  51, "swShooter",   "Shooter Lane" },
  {  53, "swTrough2",   "Ball Trough 2" },
  {  54, "swTrough3",   "Ball Trough 3" },
  {  55, "swRFlipEOS",  "Right Flipper EOS" },
  {  56, "swLOrbit",    "Left Orbit" },
  {  92, "swTrough1",   "Trough Opto 1 (ball trough 1)" },
  {  93, "swTroughJam", "Trough Jam Opto" },
  {  94, "swStart",     "Start Button" },
  {  96, "swScoop",     "Scoop Opto" },
  { 112, "swLRFlip",    "Right Flipper button (reaches switch 3)" },
  { 114, "swLLFlip",    "Left Flipper button (reaches switch 4)" },
  { 0 }
};

static const pinheck_name_t pinheck_jetsons_lamp_names[] = {
  {  11, "lJ",                 "J" },
  {  12, "lA",                 "A" },
  {  13, "lExtraBall",         "Extra Ball" },
  {  14, "lLScoopArrow",       "Left Scoop Arrow" },
  {  15, "lLScoopS",           "Left Scoop S" },
  {  16, "lRosie",             "Rosie" },
  {  17, "lAstro",             "Astro" },
  {  18, "lJudy",              "Judy" },
  {  21, "lE",                 "E" },
  {  22, "lN",                 "N" },
  {  23, "lRTargetE",          "Right Target E" },
  {  24, "lRTargetG",          "Right Target G" },
  {  25, "lRTargetR",          "Right Target R" },
  {  26, "lOrbity",            "Orbity" },
  {  27, "lElroy",             "Elroy" },
  {  28, "lJane",              "Jane" },
  {  31, "lGeorge",            "George" },
  {  32, "lJetScreamer",       "Jet Screamer" },
  {  33, "lOrbitCityMB",       "Orbit City MB" },
  {  34, "lSpacelyCogswellMB", "Spacely vs Cogswell MB" },
  {  35, "lJetAgain",          "Jet Again" },
  {  41, "lAstroJ",            "Astro J" },
  {  42, "lLOrbitArrow",       "Left Orbit Arrow" },
  {  43, "lJudyJ",             "Judy J" },
  {  44, "lRampArrow",         "Ramp Arrow" },
  {  45, "lElroyJ",            "Elroy J" },
  {  46, "lElroyLoopArrow",    "Elroy Loop Arrow" },
  {  47, "lCTargetG",          "Center Target G" },
  {  48, "lCTargetE",          "Center Target E" },
  {  51, "lCTargetO",          "Center Target O" },
  {  52, "lJackpot",           "Jackpot" },
  {  53, "lKickoutC",          "Kickout C" },
  {  54, "lKickoutArrow",      "Kickout Arrow" },
  {  55, "lOrbityJ",           "Orbity J" },
  {  56, "lROrbitArrow",       "Right Orbit Arrow" },
  {  91, "lStart",             "Start Button" },
  { 0 }
};

static const pinheck_name_t pinheck_jetsons_solenoid_names[] = {
  {  1, "sKnocker",    "Knocker (option)" },
  {  2, "sShaker",     "Shaker (option)" },
  {  3, "sRFlipLow",   "Right Flipper Low" },
  {  4, "sRFlipHigh",  "Right Flipper High" },
  {  5, "sLoad",       "Ball Load (trough)" },
  {  6, "sLaunch",     "Ball Launch (autolauncher)" },
  {  7, "sLFlipHigh",  "Left Flipper High" },
  {  8, "sLFlipLow",   "Left Flipper Low" },
  {  9, "sScoop",      "Scoop" },
  { 10, "sLSling",     "Left Sling" },
  { 11, "sRSling",     "Right Sling" },
  { 17, "sPost",       "Up Post (ball stop)" },
  { 18, "sLowerPop",   "Lower Pop" },
  { 19, "sLPop",       "Left Pop" },
  { 20, "sKickout",    "Saucer Kick Out" },
  { 21, "sRPop",       "Right Pop" },
  { 25, "sGI0",        "GI_0 (no wire on the chart)" },
  { 26, "sGI1",        "GI_1 (no wire on the chart)" },
  { 27, "sGI2",        "GI_2 (no wire on the chart)" },
  { 28, "sGI3",        "GI_3 (no wire on the chart)" },
  { 29, "sGI4",        "GI_4 (no wire on the chart)" },
  { 30, "sGI5",        "GI_5 (no wire on the chart)" },
  { 31, "sGI6",        "GI_6 (no wire on the chart)" },
  { 32, "sGI7",        "GI_7 (no wire on the chart)" },
  { 37, "sGI8",        "GI_8 ramp flasher" },
  { 38, "sGI9",        "GI_9 scoop flasher" },
  { 39, "sGI10",       "GI_10 left GI" },
  { 40, "sGI11",       "GI_11 right GI" },
  { 41, "sGI12",       "GI_12 (no wire on the chart)" },
  { 42, "sGI13",       "GI_13 (no wire on the chart)" },
  { 43, "sGI14",       "GI_14 (no wire on the chart)" },
  { 44, "sGI15",       "GI_15 (no wire on the chart)" },
  { 51, "sRGB1R",      "RGB1 red" },
  { 52, "sRGB1G",      "RGB1 green" },
  { 53, "sRGB1B",      "RGB1 blue" },
  { 54, "sRGB2R",      "RGB2 red (follows the Orbitty)" },
  { 55, "sRGB2G",      "RGB2 green (follows the Orbitty)" },
  { 56, "sRGB2B",      "RGB2 blue (follows the Orbitty)" },
  { 57, "sOrbitty",    "Servo 0: Orbitty topper" },
  { 58, "sServo1",     "Servo 1 (held at 90 degrees)" },
  { 59, "sServo2",     "Servo 2" },
  { 60, "sServo3",     "Servo 3" },
  { 61, "sServo4",     "Servo 4" },
  { 62, "sExtR",       "External RGB red (not written)" },
  { 63, "sExtG",       "External RGB green (not written)" },
  { 64, "sExtB",       "External RGB blue (not written)" },
  { 0 }
};
'''),
])
edit('tests/pinheck/vpx/names.py', [
    (r'''GAME is dominos (the default) or rzspook."""
''',
     r'''GAME is dominos (the default), rzspook or jetsons."""
'''),
    (r'''TITLE = {'dominos': "Domino's Spectacular Pinball Adventure", 'rzspook': "Rob Zombie's Spookshow International"}
''',
     r'''TITLE = {'dominos': "Domino's Spectacular Pinball Adventure", 'rzspook': "Rob Zombie's Spookshow International",
         'jetsons': 'The Jetsons'}
'''),
    (r'''ROLES = {'dominos': ('sNoid', 'sExtR'), 'rzspook': ('sGate', 'sLDGR')}
''',
     r'''ROLES = {'dominos': ('sNoid', 'sExtR'), 'rzspook': ('sGate', 'sLDGR'), 'jetsons': ('sOrbitty', 'sExtR')}
'''),
])
edit('tests/pinheck/vpx/check.sh', [
    (r'''python3 names.py check $S rzspook || fail=$((fail + 1))
''',
     r'''python3 names.py check $S rzspook || fail=$((fail + 1))
python3 names.py check $S jetsons || fail=$((fail + 1))
'''),
    (r'''int main(void) { return pinheck_dominos_switch_names[0].num != 1 || pinheck_rzspook_switch_names[0].num != 1; }\n''',
     r'''int main(void) { return pinheck_dominos_switch_names[0].num != 1 || pinheck_rzspook_switch_names[0].num != 1 || pinheck_jetsons_switch_names[0].num != 1; }\n'''),
    (r'''python3 names.py vbs $S rzspook | head -1 | grep -q "rzspook.vbs" && echo "vbs: rzspook's script names its file" || { echo "VBS FAIL: rzspook's script does not name rzspook.vbs"; fail=$((fail + 1)); }
''',
     r'''for g in rzspook jetsons; do
	python3 names.py vbs $S $g | head -1 | grep -q "$g.vbs" && echo "vbs: the $g script names $g.vbs" || { echo "VBS FAIL: the $g script does not name $g.vbs"; fail=$((fail + 1)); }
done
'''),
    (r'''	echo "scale: 2x2 dots in PinMAME and VPinMAME windows, the 128x32 panel as sent to libpinmame hosts"
''',
     r'''	echo "scale: 2x2 dots in PinMAME and VPinMAME windows, the panel (128x32, The Jetsons' 128x64) as sent to libpinmame hosts"
'''),
])
EOF
```

- [ ] **Step 3: The checks**

```sh
python3 tests/pinheck/vpx/names.py vbs src | cmp - build/pinheck-before.vbs && echo "pinheck.vbs: unchanged"
python3 tests/pinheck/vpx/names.py vbs src jetsons | grep -c '^Const'
nice -n 15 tests/pinheck/vpx/check.sh | grep -v selftest
```
Expected:
```text
pinheck.vbs: unchanged
125
names dominos: 46 switches, 48 lamps, 47 solenoid outputs; 39 sim constants agree
names rzspook: 53 switches, 49 lamps, 50 solenoid outputs; 53 sim constants agree
names jetsons: 43 switches, 36 lamps, 46 solenoid outputs; 40 sim constants agree
mingw: strcasecmp kept
gamedata: Domino's values only in the system set and dominos.c (3 games defined)
vbs: the rzspook script names rzspook.vbs
vbs: the jetsons script names jetsons.vbs
builds: 10 build lists carry the same 35 pinHeck sources
scale: 2x2 dots in PinMAME and VPinMAME windows, the panel (128x32, The Jetsons' 128x64) as sent to libpinmame hosts
vpx: LIBPINMAME or PINMAME_ROMS not set: libpinmame's display list and getters not run
vpx: 0 failed
```
(the `mingw:` line only where `x86_64-w64-mingw32-gcc` is installed)

- [ ] **Step 4: Commit**

```bash
git add src/wpc/pinheck_names.h tests/pinheck/vpx
git commit -m "pinheck_names.h: The Jetsons' switch, lamp and output names; names.py jetsons"
```

### Task 8: Speed

**Files:**
- Modify: `tests/pinheck/perf/bench.sh`

**Interfaces:**
- Consumes: Plan 11a's `bench.sh` by game.
- Produces: `bench.sh` workloads for `jetsons`: `attract`, `video` (`[V00WZA]`, a 125 s clip, and six of its sounds, from 13 s) and `play` (coin, start, the Launch Button, a shot every 1.5 s from 25 s, timed from 30 s for 55 s), its runs under `tests/pinheck/perf/build/jetsons`.

- [ ] **Step 1: The Jetsons' workloads**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/perf/bench.sh', [
    (r'''# PINHECK_GAME selects the game (tests/pinheck/games.sh); rzspook has attract, video and play (a game started and
# played from the simulator's keys), each in build/rzspook
''',
     r'''# PINHECK_GAME selects the game (tests/pinheck/games.sh); rzspook and jetsons have attract, video and play (a game
# started and played from the simulator's keys), each in build/GAME
'''),
    (r'''# workload: frames to run, frame the timed window starts at, UART1 commands, keys
''',
     r'''# The Jetsons' play: coin and start once attract mode runs (its sync is later), the Launch Button (9), then a shot every
# 1.5 s from 25 s on (none to the scoop or the kickout hole, which hold the ball); the ball never drains
jet_play_keys() {
	printf '900 tap 6 KEYCODE_5\n960 tap 6 KEYCODE_1\n1200 tap 6 KEYCODE_9\n'
	i=0
	for k in B 'LCONTROL R' 'RCONTROL R' 'LCONTROL MINUS' 'RCONTROL MINUS' E C X G H J T Y U 'LCONTROL B' 'RCONTROL B' \
		'LCONTROL L' 'RCONTROL L' L B 'LCONTROL R' 'RCONTROL R' 'LCONTROL MINUS' 'RCONTROL MINUS' E C X G H J T Y U \
		'LCONTROL B' 'RCONTROL B' 'LCONTROL L' 'RCONTROL L' L; do
		printf '%d tap 1 KEYCODE_%s\n' $((1500 + 90 * i)) "$(echo "$k" | sed 's/ / KEYCODE_/')"
		i=$((i + 1))
	done
}

# workload: frames to run, frame the timed window starts at, UART1 commands, keys
'''),
    (r'''	rzspook/play) FRAMES=4800 MARK=1500 SEND_AT=10 SEND='[E97000]' KEYS=$(play_keys) ;;
''',
     r'''	rzspook/play) FRAMES=4800 MARK=1500 SEND_AT=10 SEND='[E97000]' KEYS=$(play_keys) ;;
	jetsons/video) FRAMES=2400 MARK=960 SEND_AT=13 SEND='[E96000]~[V00WZA]~[F00ZM0]~[F00G00]~[F00J00]~[F00NAA]~~~~~~~~~~[F00SAU]~~~~[F00H00]' ;;
	jetsons/play) FRAMES=5100 MARK=1800 SEND_AT=13 SEND='[E97000]' KEYS=$(jet_play_keys) ;;
'''),
])
EOF
```

- [ ] **Step 2: The Jetsons' speed, and Domino's and Rob Zombie still identical**

```sh
jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video play
REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video
rz env REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract
```
Expected (about 3 minutes each; speeds vary with the load):
```text
bench attract opt: 0.937x over 30.0 s emulated (32.0 s wall), whole run 15.8 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.632x, load 1.95
bench video opt: 0.858x over 24.0 s emulated (27.9 s wall), whole run 16.2 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.621x, load 1.84
bench play opt: 0.841x over 55.0 s emulated (65.4 s wall), whole run 18.1 G instructions and 1.5 s CPU per emulated s, worst 100 ms 0.591x, load 1.97
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.266x over 30.0 s emulated (23.7 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.799x, load 1.76
bench attract ref: 1.277x over 30.0 s emulated (23.5 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.995x, load 2.54
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.118x over 24.0 s emulated (21.4 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.975x, load 1.77
bench video ref: 1.117x over 24.0 s emulated (21.5 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.890x, load 2.14
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
determinism first boot: identical nvram.base/rzspook.nv
bench attract opt: 1.425x over 30.0 s emulated (21.0 s wall), whole run 11.4 G instructions and 1.0 s CPU per emulated s, worst 100 ms 1.249x, load 1.79
bench attract ref: 1.433x over 30.0 s emulated (20.9 s wall), whole run 11.4 G instructions and 1.0 s CPU per emulated s, worst 100 ms 1.281x, load 2.20
determinism attract: identical uart.log frames.bin snd.wav nvram/rzspook.nv
```

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/perf/bench.sh
git commit -m "pinheck bench: The Jetsons' attract, video and play"
```

### Task 9: Regression, the tests bite, the roadmap

**Files:**
- Modify: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

- [ ] **Step 1: The remaining suites**

Domino's scripted game, the libpinmame check of Domino's, Rob Zombie's display and look checks, Domino's look check, and the link suite:

```sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/game/pinmame_game.sh | tail -2
LIBPINMAME=build/libpinmame/libpinmame.so flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/vpx/pinmame_vpx.sh | tail -1
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh | tail -1
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_look.sh | tail -1
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_look.sh | tail -1
PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 tests/pinheck/link/check.sh | tail -1
```
Expected (about 12 minutes):
```text
game: 44 checks, 0 failures
pinmame game: ok
pinmame vpx: ok
pinmame display: ok
pinmame look: ok
pinmame look: ok
link: 0 failed
```

- [ ] **Step 2: The tests bite**

Apply each mutation, rebuild (about a minute), run the named check with `jet`, confirm the named failure line (other failure lines may follow), then restore the file. Rebuild once more at the end.

```sh
mut() { f=$1; python3 -c "import sys; p=sys.argv[1]; s=open(p).read(); a,b=sys.argv[2],sys.argv[3]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" "$f" "$2" "$3" || return
	flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11b.log 2>&1; tail -1 build/sdl3pinmame/11b.log
	shift 3; jet env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 "$@" 2>&1 | grep -m3 FAIL; git checkout $f; }
J=src/wpc/sims/pinheck/jetsons.c
```

```sh
mut $J '  128, 64, 55, 544, 2400, 0, 1' '  128, 32, 55, 544, 2400, 0, 1' tests/pinheck/display/pinmame_display.sh
mut $J '  128, 64, 55, 544, 2400, 0, 1' '  128, 64, 55, 1000, 2000, 0, 1' tests/pinheck/sim/pinmame_sim.sh
mut src/wpc/pinheck.c '	if (h == DISPLAY_H) {' '	if (h) {' tests/pinheck/display/pinmame_display.sh
mut $J '  128, 64, 55, 544, 2400, 0, 1' '  128, 64, 55, 544, 2400, 1, 1' tests/pinheck/pic32mx/romset_check.sh; grep -h 'not supported' tests/pinheck/pic32mx/build/romset/nomedia.out
```

| Mutation | Change | Caught by |
|---|---|---|
| `height`: the game data says 128×32 | `jetsons.c` `128, 64, 55, …` → `128, 32, 55, …` | `render: FAIL, the frame region shows none of the last 0 decoded frames` (every 8,192-byte frame discarded) |
| `servo`: Domino's servo range | `jetsons.c` `544, 2400` → `1000, 2000` | `SIM FAIL: servo 0 (the Orbitty) in attract: [1476, 1477, 1479] us and output 57 at 122; expected 1476 us (90 degrees) and 128` |
| `look`: the 128×64 frame through the 128×32 look | `pinheck.c` `if (h == DISPLAY_H) {` → `if (h) {` | `render: FAIL, the frame region shows none of the last 8 decoded frames` |
| `rgb`: an inverted WS2801 | `jetsons.c` `…, 0, 1` → `…, 1, 1` | the romset check passes and its launch prints `pinheck: jetsons: inverted WS2801 lines are not supported, the RGB outputs are as sent` |

Then rebuild once more; `git status --short | grep -v '^??'` prints nothing.

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
     r'''| `2026-10-01-pinheck-11b-jetsons.md` | 11b: Plan 11a's review minors (`PINHECK_DOMINOS_DATA`, unsupported game data named at start); the 128×64 module (8,192-byte frames, drawn 2×2 as sent, 128×64 to libpinmame hosts); The Jetsons (`jetsons`): game, simulator, names; the update that ends without `LEAVE_PROGMODE` and the Propeller's reboot before the first sync in the boot check | Plan 11a | none | `jetsons` passes the boot (first-boot update and restart), simulator, service-test, display (`TL1.VID` pixel-exact) and libpinmame checks; Domino's and Rob Zombie byte-identical (`bench.sh` with `REFERENCE`, the scripted game's score 32176700) | written |
| release | release:'''),
    (r'''- After Plan 11a: `rzspook` runs''',
     r'''- After Plan 11b: `jetsons` runs below real time without a profile: about 0.94× in attract mode, 0.86× with video and 0.84× in play (`bench.sh`, load 1.6–2.0), and needs about 25% more host instructions per emulated second than Domino's (15.8 G against 12.6 G in attract mode). Its display cog re-sends every 8,192-byte frame at 30 fps; the profile is the Propeller's (`run_local`, the pin-edge queue's `flush`) and the PIC32 waiting for it (`wait_head`, `do_catch_up`), not the frame decoder (0.5%). A profile-guided build and Plan 9d's remaining Propeller items are the levers; Plan 11b does not take them.
- After Plan 11a: `rzspook` runs'''),
    (r'''- `pinheck_tGameData` (`src/wpc/pinheck.h`) carries what the Jetsons and America's Most Haunted need, with Domino's values in `PINHECK_GAME_DEFAULTS`:''',
     r'''- After Plan 11b: Domino's values are `PINHECK_DOMINOS_DATA` (Domino's and the system set only); the driver reads `width`/`height` (128×32 or 128×64; `pinheck_display_size`) and names an unsupported size or `rgbInverted` ≠ 0 at start (`pinheck_unsupported`); `games.sh` also carries `PRP`, `UPDATE_END`, `REBOOTS`, `LOOK` and `TROUGH1`. For Plan 11c: AMH's display is a raw DMD, not this link (`LOOK=none` and the frame checks by module size are where it plugs in); `rgbInverted` and `inService` 0 are still to be read.
- `pinheck_tGameData` (`src/wpc/pinheck.h`) carries what the Jetsons and America's Most Haunted need, with Domino's values in `PINHECK_GAME_DEFAULTS`:'''),
])
EOF
```

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "docs: roadmap row for Plan 11b and what it carries into 11c"
```

## Windows checklist

For the Windows session (VS2022 BuildTools / MSVC 14.44, `C:\code\dominos`), after Plan 11b is pushed. Results come back by message; a Windows-only fix comes back as a diff with a test, applied on Linux. Never push. Never play sound on a device the user is listening to unless the user asks; muted or the silent device is fine for every item but 6.

Setup: `roms\jetsons.zip` = `C:\code\spooky\Jetsons\Jetsons_Code.zip` copied under that name (351,227,479 bytes; not renamed in place), `roms\pinheck.zip` as before. A fresh `nvram\` and `cfg\` directory for item 2 (PinMAME on Windows does not create them).

1. **Build.** MSVC x64 standalone PinMAME, VPinMAME64 and libpinmame at the Plan 11b commit, exactly as CI does. Expected: all build; no warning in `jetsons.c`, `pinheck.c`, `display.c`, `bootldr.c`, `pinheckgames.c`; `PinMAME.exe -listfull jetsons` lists `jetsons` as "The Jetsons".
2. **First boot.** `PinMAME.exe jetsons` with the empty `nvram\`. Expected: `CODE UPDATE FOUND` / `FLASHING TO: JET V004`, the sector count to 384 and `VERIFY FLASH SECTOR … OF 384` for about 100 s of emulated time, then `UPDATE COMPLETE` / `PLEASE RESTART`, blinking; close and start again: the `JET ▮ JETSONS` / `CODE VERSION: 4` / `A/V VERSION: 2` screen, `MCU:SYNC` dots for a few seconds (the Propeller restarts itself once, Ruling 7), then attract mode with The Jetsons' clips. Unlike Domino's, no second restart is needed. With `PINHECK_UART1_LOG=uart.log` the second launch logs `400BAFA` and `Game: JET - JETSONS` (between EEPROM trace lines) and `Version: ` with nothing after it.
3. **The display.** The 128×64 picture fills a 256×128 area of the window, 2×2 per dot, crisp, from the top (no look settings: the module is drawn as sent). In the service menu (`0`, Right Shift to `MAIN SETTINGS`, `0`) `POSITION` (factory 55) changes nothing on screen (Ruling 2). Screenshot of attract mode and of the switch test.
4. **Service tests.** The main menu has nine items and no SERVO; `SOLENOID TEST` opens at `KNOCKER` and `0` fires each coil (the core's solenoid row shows it); `LAMP TEST` steps through the GI and all 64 lamp positions; `RGB TEST` steps RGB1 and RGB2 red, green, blue, white; `SWITCH TEST` after `Del` (simulator keys off) shows the cabinet columns at the left, trough opto 1 (cabinet 10) and the coin door closed, and reacts to the column/row keys (`Q`–`I` × `A`–`K`). Back with `7`.
5. **Keyboard play.** `5` coin, `1` start, `9` (Launch Button): the ball leaves the shooter lane; `B`, `Ctrl`+`R`, `E`, `C`, `G`/`H`/`J`, `T`/`Y`/`U` score; `S` (scoop) and `K` (kickout hole) hold the ball until the firmware kicks it out; `Q` drains; balls 2 and 3 are served. Report anything the firmware complains about on the display (`MISSING BALLS`, ball search).
6. **Sound** (unmuted, if the user agrees): music and call-outs in attract mode and in the game (The Jetsons' factory volumes are 25 on every channel, lower than Domino's); no drift over a minute.
7. **VPinMAME smoke test** (Route A of Milestone 10, `[Plugin.PinMAME] Enable = 0`): the Domino's test table copied to `jetsons_test.vpx` with `Const cGameName = "jetsons"` in its script, `roms\jetsons.zip` in VPinMAME's ROM folder, `nvram\` there emptied first. Expected: the two launches of item 2, then the VPinMAME DMD window shows the 128×64 display crisp at 2×2 per dot (DmdWidth/Height 256×128), sound plays, the lamp indicators follow the attract lamp show; `table.log` has `route com`. For the names, `python tests\pinheck\vpx\names.py vbs src jetsons > jetsons.vbs`.
8. **libpinmame**: `pinmame_test.exe` (or the M10 host) on `jetsons`: one display, type 15 VIDEO, 128×64, RGB565.

## Result

Measured in the replay on the Ryzen 7 7730U while other agents' emulators and builds shared it (one emulator at a time, `emu.lock`); builds without a profile; the load average is printed with every speed.

| | attract (load) | video (load) | play (load) | G host instructions per emulated s, attract / video / play |
|---|---|---|---|---|
| `jetsons` | 0.937×, slowest 100 ms 0.632× (1.95) | 0.858×, slowest 100 ms 0.621× (1.84) | 0.841×, slowest 100 ms 0.591× (1.97) | 15.8 / 16.2 / 18.1 |
| `dominos`, same session | 1.266×, slowest 100 ms 0.799× (1.76) | 1.118×, slowest 100 ms 0.975× (1.77) | — | 12.6 / 13.6 |
| `dominos`, the reference (`504d810f`) | 1.277× (2.54) | 1.117× (2.14) | — | 12.6 / 13.6 |
| `rzspook`, same session (video: Task 2) | 1.425×, slowest 100 ms 1.249× (1.79) | 1.225×, slowest 100 ms 0.488× (2.12) | — | 11.4 / 12.4 |

Domino's and Rob Zombie are byte-identical to the reference in UART1, frames, sound and NVRAM (attract mode and video, Tasks 2 and 8), and Domino's scripted game ends at 32176700 as before (Task 9). The Jetsons runs below real time on this machine: its display cog re-sends every 8,192-byte frame at 30 fps, attract mode included, and it needs about a quarter more host instructions per emulated second than Domino's. A profile of its attract mode (`bench.sh profile attract`, prototype) puts the time in the Propeller (`run_local` 23%, the pin-edge queue's `flush` 10%, counters and hub 7%) and in the PIC32 waiting for it (`wait_head` 11%, `do_catch_up` 6%); the frame decoder is 0.5% and the window 0.2%. No cheap and exact change was found; a profile-guided build (Plan 9d: about 10% for Domino's) and Plan 9d's remaining Propeller items are the levers (roadmap).

The Jetsons' first boot: the update flashes 197,632 bytes and shows `PLEASE RESTART` at 100.5 s; after the restart (104 s in the check) the Propeller reboots once before the sync, the PIC32 stores its defaults and goes on to attract mode in the same launch. In the boot check's second launch the sync passes at 9.05 s, about 1.7 s later than without the reboot.

Exploratory findings carried forward: JET launches a ball that reaches the shooter lane at once and ejects the scoop 1 s after its opto closes, in attract mode too; it leaves a ball in the kickout hole in attract mode and ejects it 1.7 s after switch 7 closes in a game; in a game it also fired the scoop coil 3 s after switch 8 (the chart's Scoop) closed alone (Ruling 8); its skill shot asks to hold the Launch Button for power, which the simulator's single tap of `9` does not model.

## Self-Review

**Scope coverage** (the brief of this plan):

| Requirement | Where |
|---|---|
| Plan 11a's review minors, each with a check that fails first where feasible | Task 1 (`gamedata:` and `vbs:` fail first; `pinheck_unsupported` by mutation in Task 9; the dead array and the comment have no check), Ruling 13 |
| 128×64: `display.c`'s frame size, by game | Task 2 (`pinheck_display_size`, unit test), Ruling 1 |
| the `CORE_VIDEO` layout and renderer, 2×2 dots, 256×128 | Task 2 (`pinheck_video`), Task 3 (`jetsons_disp`), Task 6 (screenshot), Ruling 3 |
| the look for 128×32 only, exact for 128×64 | Task 2, Task 6 (`LOOK=none`), Ruling 2; mutation `look` |
| libpinmame/VPX export at 128×64 | Task 6 Step 3 (`128x64 depth 16`, callback and plugin frames, sound), Ruling 3 |
| the display tests by game; Domino's and RZ byte-identical | Tasks 2, 6, 8 (`bench.sh` with `REFERENCE`), Task 9 (their display and look checks) |
| the `jetsons` game: ROMs, CRCs, game data, servo range, every build list | Task 3, Rulings 6 and 10; `register_build.py`, `pic32mx/check.sh`, `vpx/check.sh` |
| a simulator for the playfield mechanics | Task 3 (`jetsons.c`), Task 4 (`jetsim.py`), Rulings 8–9; mutation `servo` |
| names, checks by game, boot check (first boot with update, the restart, attract), check 5, simulator check, frame check against `.VID` clips at 128×64 | Tasks 7, 3, 5, 4, 6; Rulings 5–7, 11 |
| speed: attract, video, play, once each | Task 8, the Result, Ruling 12 |
| Windows checklist | the section above |

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay (speeds as measured, with the load).

**Type consistency:** `pinheck_display_size(display *, int, int)` and `DISPLAY_SIZE_OK` are defined in Task 2 and used by `pinheck_disp_reset` and `pinheck_unsupported` (Task 1 adds the latter with a 128×32 test, Task 2 widens it); `display.frame` is read by `pinheck_disp_frame`, `pinheck_video` and the unit test; `boot_stop` is declared in `bootldr.h` and called by `MACHINE_STOP`; `jetsons.c`'s constants are the ids `names.py check src jetsons` compares; `games.sh` sets `PRP`, `UPDATE_END`, `REBOOTS` (Task 3), `TROUGH1` (Task 4) and `LOOK` (Task 6) for every game, read by `pinmame_check.sh`, `romset_check.sh`, `pinmame_sim.sh`, `pinmame_display.sh` and `pinmame_look.sh`; `frames.py`'s `W`, `H`, `FRAME`, `check5.py`'s `FRAME`, `MX`, `CX`, `CAB_REST` and `vpx.py`'s `W`, `H` all key on `PINHECK_GAME == 'jetsons'`.

**Review Focus:** five items, each pinned by the named checks and the mutations of Task 9.

**Proof:** every file and edit this plan writes was extracted from this document (its edit scripts and the two new files) into a fresh worktree at `504d810f`, task by task, and the tree it produced is the proven prototype's (with the roadmap edit); every Expected output above was reproduced there on a machine shared with other agents' work. Each mutation of Task 9 was caught with the named line, and the tree was clean afterwards.
