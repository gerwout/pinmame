# Plan 11a: per-game data in the driver and Rob Zombie's Spookshow International Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The pinHeck driver runs a second game, Rob Zombie's Spookshow International (`rzspook`, the update V26), from its first-boot update to attract mode, the service tests, the display and its look, and keyboard play through a playfield simulator; the driver gains per-game data that also carries what the Jetsons and America's Most Haunted will need; Domino's stays byte-identical.

**Architecture:** Every pinHeck game's `core_gameData` points at the `core` member of a `pinheck_tGameData` (`pinheck.h`), which adds the game's display size, the `POSITION` its look draws unshifted, its servo pulse range, the polarity of the WS2801 lines and whether `PINHECK_INSERVICE` applies; `core.hw.gameSpecific1` stays the firmware version. The driver reads the aligned `POSITION` (it moves the decoded `POSITION` by `DISPLAY_ALIGNED − aligned`, so `display.c` keeps drawing 340 unshifted), the servo range (custom solenoid levels 57–61) and the `PINHECK_INSERVICE` switch; Domino's gets its own values (`PINHECK_GAME_DEFAULTS`), so every computation for it is the one it had. `src/wpc/sims/pinheck/rzspook.c` holds the game, as `dominos.c` does: ROMs, input ports, game data and a simulator for the 7-ball trough, the shooter lane, the VUK, the drop target and the lock on the right rail with its stop post, the Spaulding gate (servo 0) and the upper playfield's exit, the robot (servo 1), the flippers' end-of-stroke switches, pops and slings. The machine checks take `PINHECK_GAME` (`tests/pinheck/games.sh` for the shell scripts, a small per-game part in `check5.py`, `look.py`, `bench.sh` and `names.py`), so Domino's checks run unchanged and the same checks run for `rzspook`; the simulator check is new (`rzsim.py`). `pinheck_names.h` gains a `rzspook` section and `names.py` writes its VBScript constants.

**Tech Stack:** C (C89-syntax-clean driver; `-std=c99 -pedantic -Wall -Werror` for the simulator file as Plan 8b), Python 3 (no third-party modules), POSIX sh.

**Spec:** the parent spec `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§4.5 numbering, §6 romsets, §7 checks) and the addenda (`2026-09-29-pinheck-m8-m9-design.md` §3.3 simulator, `2026-09-29-pinheck-m6-m7-display-audio-design.md` addendum 5 the look, `2026-09-29-pinheck-m10-vpx-design.md` names), applied to a second game. The research behind this plan: `work/games/rz/research.md` (firmware, board use, numbering, settings, romset, mechanics), and for the per-game data `work/games/jetsons/research.md` and `work/games/amh/research.md`. Decisions the user took (the recommended options): the game is `rzspook`, a clone of the `pinheck` system set, its romset the update zip renamed to `rzspook.zip`, `gameSpecific1` = 26; the look's aligned `POSITION` is the game's factory value (Domino's 340, Rob Zombie 460), a per-game value, with Domino's byte-identical.

## Prerequisites

- Plan 9d executed and merged: `pinheck` at `964122ec`. Proven and replayed on exactly that commit.
- This plan edits the driver (`pinheck.[ch]`, `pinheckgames.c`, `pinheck/display.[ch]`, `driver.c`), Domino's game data (`sims/pinheck/dominos.c`), `pinheck_names.h`, and the checks of Plans 5–10 (`pic32mx/pinmame_check.sh`, `romset_check.sh`, `banner.py`, `check.sh`; `board/pinmame_board.sh`, `check5.py`; `display/pinmame_display.sh`, `pinmame_look.sh`, `look.py`; `sim/pinmame_sim.sh`; `perf/bench.sh`; `vpx/names.py`, `check.sh`; `link/register_build.py`), and adds `sims/pinheck/rzspook.c`, `tests/pinheck/games.sh` and `tests/pinheck/sim/rzsim.py`. The CPU cores, the SoC, `prop.c`, the board, audio, storage and link devices are untouched.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools` (or the main checkout's); `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = Domino's romset zip; `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`; `RZSPOOK_ZIP` = `rzupdate_V26.zip` (526,373,675 bytes) under the name `rzspook.zip` (a symlink will do); `RZSPOOK_UPDATE_DIR` = that zip unzipped (`RZO_V026.PRG`, `PRP_V008.BIN`, `DMD/`, `SFX/`; 897 MB); `SDL_AUDIO_DRIVER=dummy`. Nothing from either update goes into the repository. A Rob Zombie run is the Domino's command with `PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR` in front; the steps define it once as a shell function:
  ```sh
  rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
  ```
- The machine is shared: only one emulator runs at a time machine-wide, so every step that runs PinMAME or a firmware program goes through `flock /code/spooky_domino/work/emu.lock` (on another machine drop the `flock` prefix); builds and heavy suites run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6`. Every PinMAME launch has a `timeout -k 30`, a private `-nvram_directory` and `-cfg_directory`, and no sound device (`SDL_AUDIO_DRIVER=dummy`). The emulation runs at 1.1–1.4× real time with `-nothrottle`; the times below are wall-clock with other agents' work beside them.

## Global Constraints

- Domino's is byte-identical: `bench.sh` with `REFERENCE` (the build of `964122ec`) requires its UART1 log, display frames, sound and NVRAM in attract mode and with video equal byte for byte; the scripted game ends with Plan 8b's score; every Domino's check passes with its earlier output.
- `pinheck_tGameData` is the only per-game mechanism: every game of `GEN_PINHECK` is defined through it (`INIT_PINHECK` in `pinheckgames.c`, `dominos.c`, `rzspook.c`); `core.hw.gameSpecific1` stays the firmware version `PINHECK_INSERVICE` writes.
- Numbering is Plan 8a's (spec §4.5): document switch *n* (0–63) = PinMAME `(n/8+1)*10 + n%8+1`; cabinet switch 13 = 95, 14 = 96; document coil *n* = PinMAME solenoid *n*+1; servo *n* = custom solenoid 57+*n*; the documents number switches and coils from 0.
- The simulator follows Plan 8b's conventions: PinMAME's framework, coils read as "on during the last frame" (`coreGlobals.solenoids`), `autoBall`, keys tapped for one frame, every public name outside the file `pinheck_`-prefixed.
- The checks select the game with `PINHECK_GAME` (default `dominos`); with it unset every check runs Domino's exactly as before.
- Code comments short and factual; the driver C89-syntax-clean.

## Rulings

Decisions the research and the user's choices left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it). Addresses are `RZO_V026.PRG` flash addresses; times are emulated seconds.

1. **The per-game mechanism is a struct whose first member is the game's `core_tGameData`.** `pinheck_tGameData { core_tGameData core; int width, height, aligned, servoMin, servoMax, rgbInverted, inService; }`; `core_gameData = &game.core`, and the driver reads its fields through `(const pinheck_tGameData *)core_gameData`, which C allows for a struct's first member. Rejected: more `gameSpecific` words (two `UINT32`s cannot hold a pointer on 64-bit hosts, and packing a servo range, a position and three flags into bits is unreadable); a table in the driver keyed by the game's name (a clone or a renamed set would miss it); a pointer set by each game's `init_` (equivalent, but not reached from `core_gameData`). Cost if wrong: a `GEN_PINHECK` game defined without the struct would have the driver read past its data; every pinHeck game is defined in this repository through it.
2. **What the driver reads now.** `aligned` (Ruling 3), `servoMin`/`servoMax` (Ruling 4) and `inService` (`PINHECK_INSERVICE` seeds the update record only when it is 1). `width`, `height` and `rgbInverted` are carried with the values the driver supports (128 × 32, 0) for Plans 11b (Jetsons 128 × 64) and 11c (AMH's inverted lines; its `inService` 0); `PINHECK_GAME_DEFAULTS` holds Domino's values. Cost if wrong: one struct field more or less in Plans 11b/11c.
3. **The aligned `POSITION`.** The driver adds `DISPLAY_ALIGNED − aligned` to the decoded `POSITION`; `display.c` keeps its arithmetic (its literal 340 becomes `DISPLAY_ALIGNED`), and its unit tests and `lookdump` are unchanged. Domino's adds 0. Rob Zombie's factory `POSITION` is 460, the Propeller's own default (research §2): the look check sees the start-up packet `00 fa 01 cc 00 00 00 ff 00 80 00 20 00 3e` and draws it unshifted. `look.py` applies the same shift when it decodes a packet (`parse`) and when it predicts a menu state (`shown`), so `render.py` and the look check compare against the driver's drawing.
4. **Rob Zombie's servo levels are its degrees: 0.544–2.4 ms.** Its servo test drives the Spaulding gate (servo 0) to 1,227 µs (closed, also at boot) and 2,222 µs (open), the robot (servo 1) to 1,476 µs (start) and 2,097 µs (end); `[S00160]` gives 2,201 µs. With Domino's 1.0–2.0 ms, open and end would both read 255. 544–2,400 µs is the servo library's 0–180° and keeps every position distinct (94, 231, 128, 213). Domino's keeps 1.0–2.0 ms (its check 5 expects 0, 255 and 161).
5. **The game and its romset.** `rzspook`, a clone of `pinheck` (`CORE_CLONEDEFNV`, `GAME_NOT_WORKING` as Domino's), 7 balls, `gameSpecific1` = 26; `RZO_V026.PRG` (`0x4B350` bytes, CRC `db1ee6f9`) and its own `PRP_V008.BIN` (CRC `caeb2c41`); `DRIVERNV(rzspook)` after Domino's in `driver.c`. The update zip renamed is the romset, as for Domino's.
6. **First boot and the banner.** The update flashes 309,248 bytes and shows `CODE UPDATE COMPLETE` / `PLEASE RESTART` at 181.2 s (Domino's about 135 s); the boot check restarts at 184 s, then the PIC32 stores its defaults and stops, and the next launch reaches attract mode, as on Domino's. Rob Zombie prints `Version: ` with nothing after it, and no `NAMING GAME`; both games print the stored version word after the sync check (Domino's `600BAFA`, Rob Zombie `1A00BAFA` = 26 << 24 | `$BAFA`), which the boot check now asserts for both, with the banner's `Game:` line (`DOM - DOMINOS`, `RZO - SPOOK SHOW`; `banner.py uart` takes the EEPROM traces out that interleave it).
7. **The simulator's geometry is inferred and marked as such** (research §7 risk 3): the rules card, `WHATSNEW.TXT`, the switch and coil lists and the firmware's own reactions in an exploratory game. The drop target stands in front of the right inner orbit: a shot knocks it down (48 closes and stays) while it is up and passes over it while it is down; coil 7 raises it. Behind it is the lock: the right rail, whose stop post is up while unpowered, so the first ball rests on Rail Lower (33), the next on Rail Upper (34), a third on the right inner orbit (35); each pulse of the Ball Stop coil (5) lets one ball roll on to the right inlane. Evidence: in a game the firmware never powered coil 5 when a ball passed the right inner orbit, shook the cabinet (coil 2) 0.6 s after Rail Lower closed, and 12 s later fired coils 5 and 7 together; ball search pulses the post (`WHATSNEW`: "Stop post added to ball search routine"). The Spaulding gate is open while servo 0 is above 1,700 µs: a shot closes the gate opto (95), and through the open gate the upper playfield's exit opto (96). The upper playfield is not a separate place: its shots (Chicken 37, Gasoline 38, Spaulding) are keys like the others. The flippers' end-of-stroke switches (41, 24, 25) close while a coil of their flipper was on in the last frame. The robot has no switch; its position is tracked (`getMech(1)`). Cost if wrong: a shot's switch order differs from the machine's; the firmware's rules see the same switches.
8. **The service tests of Rob Zombie** (check 5): the solenoid test starts at `AUTOPLUNGER` (item 16) and wraps; the servo test is `GATE OPEN`, `GATE CLOSE`, `ROBOT START`, `ROBOT END` (Ruling 4's widths); the RGB test adds `LDG RED/GREEN/BLUE/WHITE` on the external WS2801 LED (62–64), whose lines are red, blue, green while `SWAP G <-> B` is `NO`, its factory setting (`LDG GREEN` sends `00 00 ff`); attract mode lights the playfield GI but its flashers (37–40, 43, 44 on; 41, 42 off). At the switch test five balls rest in the trough (1–5) and one in the shooter lane (0).
9. **The checks select the game with `PINHECK_GAME`.** `tests/pinheck/games.sh` names each game's PIC32 image, update size, restart time, stored version word, banner, display clip and simulator check; `check5.py`, `look.py`, `bench.sh` and `names.py` keep their per-game parts beside the code that uses them. With `PINHECK_GAME` unset every check runs Domino's as before (check 5 plans the same 266 steps; `names.py vbs src` writes the same `pinheck.vbs`).
10. **The display check's clip is `DMD/_DD/DMB.VID`** (30 frames, no two consecutive frames alike); the Propeller's framebuffer is at hub `$586C`, as the research inferred (Domino's `$5870`).
11. **The scripted Rob Zombie game is left to a later plan.** At mode start the firmware holds the ball in the VUK until a mode is chosen with the flippers ("double tap or hit Start to enter", rules card); in the exploratory game Start during ball 1 added players, and a double tap of the left flipper 0.3 s apart did not start the mode. A scripted game needs that protocol (and the lock's award, Ruling 7) read from the firmware first. Until then Rob Zombie's play is checked by the simulator check and measured by `bench.sh play` (55 s of one-ball play from the keys). Cost: scoring, multiball, high-score entry and match are not checked for Rob Zombie.
12. **Speed** (`bench.sh`, a build without a profile, at the loads printed): Rob Zombie runs attract mode, video and play faster than real time; its 7-ball multiball is not reached by any workload (Ruling 11) and is not measured. The emulation's cost follows the firmware's work on the display, sound and lamps; the simulator's per-ball work is a few state changes a frame.
13. **Names.** A `rzspook` section in `pinheck_names.h` (one typedef and one file for `names.py`), ids equal to `rzspook.c`'s constants as `names.py check` requires; `names.py vbs src rzspook` writes the game's VBScript constants; `names.py vbs src` still writes Domino's `pinheck.vbs` byte for byte.

## Review Focus

- **The struct behind `core_gameData`**: every `GEN_PINHECK` game (the system set through `INIT_PINHECK`, `dominos.c`, `rzspook.c`) must be a `pinheck_tGameData`, or `pinheck_game()` reads past it. The three definitions are in this plan; a new game must follow them.
- **Domino's unchanged**: the shift is `+ 0`, the servo level `(us − 1000.0) / (2000.0 − 1000.0)` is the same double arithmetic as `(us − 1000.0) / 1000.0`, `inService` is 1. `bench.sh` with `REFERENCE` (Tasks 1 and 7), Domino's look, display, board, simulator, boot and scripted-game checks (Task 8).
- **The lock's exclusive states** (Ruling 7): balls queue on Rail Lower, Rail Upper and the right inner orbit; one coil pulse lets exactly one ball on. The simulator check locks two balls and releases them one by one.
- **The parameterised checks** must run Domino's exactly as before with `PINHECK_GAME` unset (check 5's plan, the look plan and packets, the boot check's restart time and frames, the display clip, the bench workloads and their build directory).
- **The aligned `POSITION`** in the look model: `look.py` shifts both the decoded packets and the predicted menu states; the restart line prints the drawn position (341 for Rob Zombie's stored 461).

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck.h` | `pinheck_tGameData`, `PINHECK_GAME_DEFAULTS` (modified) |
| `src/wpc/pinheck.c` | reads the aligned `POSITION`, the servo range and `inService` (modified) |
| `src/wpc/pinheck/display.[ch]` | `DISPLAY_ALIGNED` (modified) |
| `src/wpc/pinheckgames.c`, `src/wpc/sims/pinheck/dominos.c` | the system set and Domino's through `pinheck_tGameData` (modified) |
| `src/wpc/sims/pinheck/rzspook.c` | Rob Zombie's game definition and playfield simulator |
| `src/wpc/driver.c`, `tests/pinheck/link/register_build.py`, build files | `rzspook` in the driver list and every build (modified / regenerated) |
| `src/wpc/pinheck_names.h`, `tests/pinheck/vpx/names.py`, `check.sh` | Rob Zombie's names and VBScript constants (modified) |
| `tests/pinheck/games.sh` | per-game facts for the machine checks |
| `tests/pinheck/pic32mx/{pinmame_check.sh,romset_check.sh,banner.py,check.sh}` | the boot check by game (modified) |
| `tests/pinheck/sim/{pinmame_sim.sh,rzsim.py}` | the simulator check by game; Rob Zombie's |
| `tests/pinheck/board/{pinmame_board.sh,check5.py}` | check 5 by game (modified) |
| `tests/pinheck/display/{pinmame_display.sh,pinmame_look.sh,look.py}` | the frame and look checks by game (modified) |
| `tests/pinheck/perf/bench.sh` | workloads by game, Rob Zombie's `play` (modified) |
| `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | this plan's row and what it carries (modified) |

---
### Task 1: Reference build and the per-game data

**Files:**
- Modify: `src/wpc/pinheck.h`, `src/wpc/pinheck.c`, `src/wpc/pinheck/display.h`, `src/wpc/pinheck/display.c`, `src/wpc/pinheckgames.c`, `src/wpc/sims/pinheck/dominos.c`

**Interfaces:**
- Consumes: Plan 8b's `core_tGameData` definitions in `pinheckgames.c` (`INIT_PINHECK`) and `dominos.c`; the look's `pinheck_display_look`/`pinheck_display_render` (`display.c`); the board block's servo callback; the NVRAM handler's `PINHECK_INSERVICE`; Plan 9's `bench.sh` with `REFERENCE`.
- Produces: `pinheck_tGameData` (`core_tGameData core; int width, height, aligned, servoMin, servoMax, rgbInverted, inService;`), `PINHECK_GAME_DEFAULTS` (`128, 32, 340, 1000, 2000, 0, 1`), `DISPLAY_ALIGNED` (340); the driver's `static const pinheck_tGameData *pinheck_game(void)`; `build/reference/sdl3pinmame`.

- [ ] **Step 1: Build the reference**

Build `pinheck` as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`) and keep the binary; Domino's outputs are compared with it in this task and in Task 7:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11a.log 2>&1; tail -1 build/sdl3pinmame/11a.log
mkdir -p build/reference && cp build/sdl3pinmame/sdl3pinmame build/reference/sdl3pinmame
```
Expected: `[100%] Built target sdl3pinmame`.

- [ ] **Step 2: The struct and its readers**

`pinheck.h` gains the struct and Domino's values; `display.[ch]` name the aligned `POSITION`; the driver reads the game's aligned `POSITION` (after the look decoded a packet), servo range (the board block's servo callback) and `inService` (the NVRAM handler); the system set and Domino's are defined through the struct:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.h', [
    (r'''#define PINHECK_ROMEND ROM_END

''',
     r'''#define PINHECK_ROMEND ROM_END

/* per-game data: a pinHeck game's core_gameData points at the core member of one of these */
typedef struct {
  core_tGameData core;
  int width, height;      /* display module in dots; the driver supports 128 x 32 */
  int aligned;            /* the POSITION the look draws unshifted: the game's factory POSITION */
  int servoMin, servoMax; /* servo pulse widths in us drawn as servo levels 0 and 255 */
  int rgbInverted;        /* WS2801 lines inverted on the board; the driver supports 0 */
  int inService;          /* PINHECK_INSERVICE seeds the update record (version: core.hw.gameSpecific1) */
} pinheck_tGameData;

/* Domino's values: 128 x 32, POSITION 340, servo levels 1.0-2.0 ms, WS2801 as sent, PINHECK_INSERVICE applies */
#define PINHECK_GAME_DEFAULTS 128, 32, 340, 1000, 2000, 0, 1

'''),
])
edit('src/wpc/pinheck/display.h', [
    (r'''#define DISPLAY_HIGHREZ 2
''',
     r'''#define DISPLAY_HIGHREZ 2
#define DISPLAY_ALIGNED 340 /* the POSITION drawn unshifted */
'''),
    (r'''	int position;   /* POSITION: 300-500 in the menu, 340 = aligned */
''',
     r'''	int position;   /* POSITION: 300-500 in the menu, DISPLAY_ALIGNED = aligned */
'''),
])
edit('src/wpc/pinheck/display.c', [
    (r'''	look->position = 340;
''',
     r'''	look->position = DISPLAY_ALIGNED;
'''),
    (r'''	int x, y, k, dy = look->position - 340;
''',
     r'''	int x, y, k, dy = look->position - DISPLAY_ALIGNED;
'''),
])
edit('src/wpc/pinheck.c', [
    (r'''static uint8_t pinheck_brd_swcol(void *ctx, int col) { (void)ctx; return coreGlobals.swMatrix[col + 1]; }
''',
     r'''static const pinheck_tGameData *pinheck_game(void) { return (const pinheck_tGameData *)core_gameData; }

static uint8_t pinheck_brd_swcol(void *ctx, int col) { (void)ctx; return coreGlobals.swMatrix[col + 1]; }
'''),
    (r'''	double us = pulse / (PINHECK_CLOCK / 1e6), v = (us - 1000.0) / 1000.0;
''',
     r'''	const double lo = pinheck_game()->servoMin, hi = pinheck_game()->servoMax;
	double us = pulse / (PINHECK_CLOCK / 1e6), v = (us - lo) / (hi - lo);
'''),
    (r'''	if (!pinheck_display_look(&disp_look, bytes, n)) pinheck_prop_log(NULL, "display: unknown config packet, exact pixels");
''',
     r'''	if (!pinheck_display_look(&disp_look, bytes, n)) pinheck_prop_log(NULL, "display: unknown config packet, exact pixels");
	else disp_look.position += DISPLAY_ALIGNED - pinheck_game()->aligned; /* the game's factory POSITION is aligned */
'''),
    (r'''	if (first && getenv("PINHECK_INSERVICE")) pinheck_in_service(propmem, core_gameData->hw.gameSpecific1);
''',
     r'''	if (first && getenv("PINHECK_INSERVICE") && pinheck_game()->inService) pinheck_in_service(propmem, core_gameData->hw.gameSpecific1);
'''),
])
edit('src/wpc/pinheckgames.c', [
    (r'''static core_tGameData name##GameData = { GEN_PINHECK, pinheck_disp, {FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, version, 0, pinheck_getsol} }; \
static void init_##name(void) { core_gameData = &name##GameData; }
''',
     r'''static pinheck_tGameData name##GameData = { { GEN_PINHECK, pinheck_disp, {FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, version, 0, pinheck_getsol} }, PINHECK_GAME_DEFAULTS }; \
static void init_##name(void) { core_gameData = &name##GameData.core; }
'''),
])
edit('src/wpc/sims/pinheck/dominos.c', [
    (r'''static core_tGameData dominosGameData = {
  GEN_PINHECK, dominos_disp,
  { FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, 6, 0,
    pinheck_getsol, dominos_handleMech, dominos_getMech },
  &dominosSimData
};

static void init_dominos(void) {
  int i;
  core_gameData = &dominosGameData;
''',
     r'''static pinheck_tGameData dominosGameData = {
  { GEN_PINHECK, dominos_disp,
    { FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, 6, 0,
      pinheck_getsol, dominos_handleMech, dominos_getMech },
    &dominosSimData },
  PINHECK_GAME_DEFAULTS
};

static void init_dominos(void) {
  int i;
  core_gameData = &dominosGameData.core;
'''),
])
EOF
```

- [ ] **Step 3: Build**

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11a.log 2>&1; tail -1 build/sdl3pinmame/11a.log
grep -i warning build/sdl3pinmame/11a.log | grep -cE 'pinheck|dominos|display\.c'
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 4: Domino's is byte-identical**

`bench.sh` runs attract mode and the video workload with the new build and the reference and compares UART1, frames, sound and NVRAM:

```sh
REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video
```
Expected (about 3 minutes; speeds vary with the load):
```text
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.280x over 30.0 s emulated (23.4 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 1.025x, load 2.81
bench attract ref: 1.286x over 30.0 s emulated (23.3 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 1.099x, load 2.64
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.122x over 24.0 s emulated (21.4 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.913x, load 2.15
bench video ref: 1.127x over 24.0 s emulated (21.3 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.967x, load 2.06
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 5: The display suite**

The look's decoding and rendering unit tests, `lookdump` against `look.py`, and the look in the standalone renderer only:

Run: `PINHECK_SKIP_FIRMWARE=1 nice -n 15 tests/pinheck/display/check.sh | tail -3`
Expected:
```text
look: display.c equals look.py in 8 looks
look: sdl3pinmame renders the look, libpinmame gets the frame as sent
display: 0 failed
```

- [ ] **Step 6: Commit**

```bash
git add src/wpc/pinheck.h src/wpc/pinheck.c src/wpc/pinheck/display.h src/wpc/pinheck/display.c src/wpc/pinheckgames.c src/wpc/sims/pinheck/dominos.c
git commit -m "pinheck: per-game data (pinheck_tGameData): aligned POSITION, servo range, PINHECK_INSERVICE; Domino's values unchanged"
```

### Task 2: The `rzspook` game and its boot check

**Files:**
- Create: `src/wpc/sims/pinheck/rzspook.c`, `tests/pinheck/games.sh`
- Modify: `src/wpc/pinheckgames.c`, `src/wpc/driver.c`, `tests/pinheck/link/register_build.py`, `tests/pinheck/pic32mx/pinmame_check.sh`, `tests/pinheck/pic32mx/romset_check.sh`, `tests/pinheck/pic32mx/banner.py`, `tests/pinheck/pic32mx/check.sh`; regenerated: `src/pinmame.mak`, `cmake/*/CMakeLists*.txt`, `vcproj/*.vcxproj(.filters)`

**Interfaces:**
- Consumes: Task 1's `pinheck_tGameData`; Plan 8b's simulator framework and `pinheck_servo()`; the board block's `coreGlobals.solenoids`.
- Produces: the `rzspook` game (keys in the file's header; `getMech(0)` gate open, `getMech(1)` robot pulse width in µs, `getMech(2)` drop target down; `HandleMechanics` bit 0 the drop target's coil, bit 1 the end-of-stroke switches); `tests/pinheck/games.sh`, sourced by the machine checks, setting `GAME`, `PRG`, `PROGRAMMED`, `UPDATED_AT`, `STORED`, `BANNER`, `CLIP`, `SIM` from `PINHECK_GAME`; `banner.py uart LOG 'line|line|…'`.

- [ ] **Step 1: The checks' game table and the boot check by game**

`tests/pinheck/games.sh`:

```sh
# The game a machine check runs, sourced by the checks: PINHECK_GAME (default dominos), whose romset is
# PINHECK_ZIP and whose unzipped update is PINHECK_UPDATE_DIR.
#   PRG         the PIC32 image in the update
#   PROGRAMMED  bytes the first-boot update flashes
#   UPDATED_AT  emulated seconds by which the Propeller shows PLEASE RESTART on a first boot
#   STORED      the version word the firmware prints after the sync check (version << 24 | $BAFA, hex)
#   BANNER      the UART1 banner lines checked (| separated)
#   CLIP        a .VID clip the display check plays ([V00<CLIP>])
#   SIM         the simulator check (tests/pinheck/sim)
GAME=${PINHECK_GAME:-dominos}
case $GAME in
dominos) PRG=DOM_V006.PRG PROGRAMMED=204288 UPDATED_AT=138 STORED=600BAFA CLIP=LT5 SIM=sim.py
	BANNER='pinHeck System 2011-2016|Game: DOM - DOMINOS|Version: 006' ;;
rzspook) PRG=RZO_V026.PRG PROGRAMMED=309248 UPDATED_AT=184 STORED=1A00BAFA CLIP=DMB SIM=rzsim.py
	BANNER='pinHeck System 2011-2016|Game: RZO - SPOOK SHOW' ;;
*) echo "PINHECK_GAME: no machine checks for '$GAME'"; exit 2 ;;
esac
```

The boot check, the romset check and the launch lint take the game from it; the banner lines are checked with the EEPROM traces taken out (Ruling 6):

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/pic32mx/pinmame_check.sh', [
    (r''': "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
''',
     r''': "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
'''),
    (r'''cd "$(dirname "$0")" || exit 2
B=build/pinmame
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
B=build/pinmame
'''),
    (r'''	ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
else
	(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/dominos.zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || exit 2
fi
''',
     r'''	ln -s "$PINHECK_ZIP" $B/roms/$GAME.zip || exit 2
else
	(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/$GAME.zip" $PRG PRP_V008.BIN DMD SFX) || exit 2
fi
'''),
    (r'''timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms''',
     r'''timeout -k 30 3000 "$SDL3PINMAME" $GAME -rompath roms'''),
    (r'''PINHECK_RESET_AT=138 launch 1 9000
''',
     r'''# the first boot flashes the update; the Propeller then asks for a restart, which comes 12 s later
PINHECK_RESET_AT=$UPDATED_AT launch 1 $(((UPDATED_AT + 12) * 60))
'''),
    (r'''for want in "boot: sign-on 0" "boot: leave, 204288 bytes programmed" "prop: CLKSET 6f"; do
''',
     r'''for want in "boot: sign-on 0" "boot: leave, $PROGRAMMED bytes programmed" "prop: CLKSET 6f"; do
'''),
    (r'''for want in "PROPELLER SYNC CHECK" "pinHeck System 2011-2016" "Version: 006" "Ball Search: DISABLED"; do
	grep -aqF "$want" $B/uart2.log || { echo "PINMAME FAIL: launch 2 UART1 log lacks '$want'"; fail=1; }
done
''',
     r'''python3 banner.py uart $B/uart2.log "PROPELLER SYNC CHECK|$STORED|$BANNER|Ball Search: DISABLED" || fail=1
'''),
    (r'''[ -s $B/nvram/dominos.nv ] || { echo "PINMAME FAIL: no NVRAM written"; fail=1; }
''',
     r'''[ -s $B/nvram/$GAME.nv ] || { echo "PINMAME FAIL: no NVRAM written"; fail=1; }
'''),
])
edit('tests/pinheck/pic32mx/romset_check.sh', [
    (r''': "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
''',
     r''': "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
'''),
    (r'''cd "$(dirname "$0")" || exit 2
B=build/romset
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
B=build/romset
'''),
    (r'''(cd "$PINHECK_UPDATE_DIR" && zip -q -0 "$OLDPWD/$B/nomedia/dominos.zip" DOM_V006.PRG PRP_V008.BIN) || exit 2
''',
     r'''(cd "$PINHECK_UPDATE_DIR" && zip -q -0 "$OLDPWD/$B/nomedia/$GAME.zip" $PRG PRP_V008.BIN) || exit 2
'''),
    (r'''rc=$(run dominos nomedia nomedia -frames_to_run 120)
if [ "$rc" -gt 128 ] || ! grep -qF "pinheck: the SD card from dominos.zip has no DMD/ and no SFX/" $B/nomedia.out; then
	echo "ROMSET FAIL: dominos without media exited $rc"; tail -3 $B/nomedia.out; fail=1
''',
     r'''rc=$(run $GAME nomedia nomedia -frames_to_run 120)
if [ "$rc" -gt 128 ] || ! grep -qF "pinheck: the SD card from $GAME.zip has no DMD/ and no SFX/" $B/nomedia.out; then
	echo "ROMSET FAIL: $GAME without media exited $rc"; tail -3 $B/nomedia.out; fail=1
'''),
])
edit('tests/pinheck/pic32mx/banner.py', [
    (r'''def main():
    b = boots(open(sys.argv[1], encoding='latin-1').read())
''',
     r'''def uart(path, want):
    """a PinMAME UART1 log, its EEPROM traces taken out, has each of the |-separated lines in want"""
    text = TRACE.sub('', open(path, encoding='latin-1').read()).replace('\r', '')
    bad = [w for w in want.split('|') if w not in text]
    for w in bad:
        print('PINMAME FAIL: UART1 log %s lacks %r' % (path, w))
    return 1 if bad else 0


def main():
    if sys.argv[1] == 'uart':
        return uart(sys.argv[2], sys.argv[3])
    b = boots(open(sys.argv[1], encoding='latin-1').read())
'''),
])
edit('tests/pinheck/pic32mx/check.sh', [
    (r'''bad=$(grep -n '"[$]SDL3PINMAME" \(dominos\|pinheck\|"[$]name"\)' ../*/*.sh | grep -v 'timeout -k')
''',
     r'''bad=$(grep -n '"[$]SDL3PINMAME" \(dominos\|pinheck\|[$]GAME\|"[$]name"\)' ../*/*.sh | grep -v 'timeout -k')
'''),
])
EOF
```

- [ ] **Step 2: Confirm the Rob Zombie boot check fails before the game exists**

```sh
rz() { PINHECK_GAME=rzspook PINHECK_ZIP=$RZSPOOK_ZIP PINHECK_UPDATE_DIR=$RZSPOOK_UPDATE_DIR "$@"; }
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
```
Expected (exit 1):
```text
PINMAME FAIL: launch 1 exited 1
```
followed by PinMAME's own lines for a set it does not know (`fuzzy name compare, running …`, `ERROR: required files are missing, the game cannot be run.`).

- [ ] **Step 3: The game and its simulator**

`src/wpc/sims/pinheck/rzspook.c`:

```c
// license:BSD-3-Clause

/*******************************************************************************
 Rob Zombie's Spookshow International (Spooky Pinball, 2016): game definition
 and playfield simulator.

 The simulator models the mechanics the rules depend on: the 7-ball trough and
 shooter lane (a manual plunger and the autoplunger), the VUK, the drop target
 in front of the right inner orbit and the lock behind it (the right rail, whose
 stop post holds the balls until its coil drops it), the Spaulding gate
 (servo 0) and the upper playfield's exit, the robot (servo 1), the flippers'
 end-of-stroke switches, pops, slings and the drain.
 Coils are read as "on at some time during the last frame" (coreGlobals.solenoids,
 set by the board block), so a pulse of a few milliseconds is never missed.
 ******************************************************************************/

/*------------------------------------------------------------------------------
  Keys (L/R Ctrl selects the left or right one of a pair):
    +-  L/R Lower Slingshot   +=  L/R Upper Slingshot   +I  L/R Inlane
    +O  L/R Outlane (drain)   +R  L/R Orbit             +N  L/R Inner Orbit
    +B  Upper Left/Right Pop   B  Lower Left Pop        +L  L/R Living Dead Girl
     C  Ramp                   V  VUK                    S  Secret Passage
     G  Spaulding (gate)       H  Chicken                J  Gasoline
     T  Ramp Target            E  Extra Ball             A  Rattle
     X  Pop Target             U  Upper Playfield Exit
     Q  Drain between the flippers           Space  Plunger (hold, release)
  The right inner orbit knocks the drop target down while it is up and reaches
  the lock behind it while it is down: the first ball rests on Rail Lower, the
  next on Rail Upper, a third on the right inner orbit switch.
  The keys move a ball on the playfield (autoBall); Up/Down still select one.
------------------------------------------------------------------------------*/

#include "driver.h"
#include "core.h"
#include "sim.h"
#include "sndbrd.h"
#include "pinheck.h"

PINHECK_INPUT_PORTS_START(rzspook, 7)
  PORT_START /* 0 */
    COREPORT_BIT(0x0001, "Left Qualifier",   KEYCODE_LCONTROL)
    COREPORT_BIT(0x0002, "Right Qualifier",  KEYCODE_RCONTROL)
    COREPORT_BIT(0x0004, "L/R Slingshot",    KEYCODE_MINUS)
    COREPORT_BIT(0x0008, "L/R Upper Sling",  KEYCODE_EQUALS)
    COREPORT_BIT(0x0010, "L/R Inlane",       KEYCODE_I)
    COREPORT_BIT(0x0020, "L/R Outlane",      KEYCODE_O)
    COREPORT_BIT(0x0040, "L/R Orbit",        KEYCODE_R)
    COREPORT_BIT(0x0080, "L/R Inner Orbit",  KEYCODE_N)
    COREPORT_BIT(0x0100, "Pop Bumpers",      KEYCODE_B)
    COREPORT_BIT(0x0200, "L/R LDG Target",   KEYCODE_L)
    COREPORT_BIT(0x0400, "Ramp",             KEYCODE_C)
    COREPORT_BIT(0x0800, "VUK",              KEYCODE_V)
    COREPORT_BIT(0x1000, "Secret Passage",   KEYCODE_S)
    COREPORT_BIT(0x2000, "Drain",            KEYCODE_Q)
  PORT_START /* 1 */
    COREPORT_BIT(0x0001, "Spaulding",        KEYCODE_G)
    COREPORT_BIT(0x0002, "Chicken",          KEYCODE_H)
    COREPORT_BIT(0x0004, "Gasoline",         KEYCODE_J)
    COREPORT_BIT(0x0008, "Ramp Target",      KEYCODE_T)
    COREPORT_BIT(0x0010, "Extra Ball",       KEYCODE_E)
    COREPORT_BIT(0x0020, "Rattle",           KEYCODE_A)
    COREPORT_BIT(0x0040, "Pop Target",       KEYCODE_X)
    COREPORT_BIT(0x0080, "Upper PF Exit",    KEYCODE_U)
PINHECK_INPUT_PORTS_END

/* switches: document switch n (0-63) is (n/8+1)*10 + n%8+1; the optos are cabinet inputs 13 and 14 */
#define swShooter   11
#define swTrough1   12
#define swTrough2   13
#define swTrough3   14
#define swTrough4   15
#define swTrough5   16
#define swTrough6   17
#define swTrough7   18
#define swROutlane  21
#define swRInlane   22
#define swRSling    23
#define swRFlipEOS      24
#define swLFlipEOS      25
#define swLSling    26
#define swLInlane   27
#define swLOutlane  28
#define swExtraBall 31
#define swRUSling   32
#define swRailLow   33
#define swRailUp    34
#define swRInner    35
#define swRPop      36
#define swChicken   37
#define swGasoline  38
#define swUFlipEOS      41
#define swROrbit    42
#define swVUK       43
#define swSecret    44
#define swLInner    45
#define swRLDG      46
#define swLLDG      47
#define swDrop      48
#define swRampTgt   51
#define swRattle    52
#define swLLPop     54
#define swULPop     55
#define swPopTarget 56
#define swLUSling   57
#define swLOrbit    58
#define swRamp      61
#define swGateOpto  95
#define swExitOpto  96

/* solenoids: document coil n is PinMAME solenoid n+1 */
#define sUFlipHigh  3
#define sVUK        4
#define sPost       5
#define sDrop       7
#define sUFlipLow   8
#define sLFlipLow   13
#define sLFlipHigh  14
#define sLaunch     17
#define sLoad       18
#define sRFlipLow   19
#define sRFlipHigh  20

#define GATE_OPEN_US 1700 /* servo 0: 1,227 us closed, 2,222 us open (servo test, factory settings) */

static struct {
  int since[25];  /* frames since each coil was last on */
  int drop;       /* drop target down, -1 = not yet set */
  int gateUs;     /* servo 0 (Spaulding gate): last pulse width in us, 0 = none yet */
  int robotUs;    /* servo 1 (robot): last pulse width in us, 0 = none yet */
  int eos[3];     /* upper, right, left flipper end-of-stroke switches as last set, -1 = not yet */
} locals;

static int sol(int n) { return (coreGlobals.solenoids >> (n - 1)) & 1; }

enum { stTrough7 = SIM_FIRSTSTATE, stTrough6, stTrough5, stTrough4, stTrough3, stTrough2, stTrough1,
       stShooter, stLaunched, stDrain,
       stLOutlane, stROutlane, stLInlane, stRInlane, stLSling, stRSling, stLUSling, stRUSling,
       stLOrbit, stROrbit, stLOrbitExit, stROrbitExit, stLInner, stRInnerShot, stRInner,
       stRailUp, stRailLow, stRamp, stVUK, stSecret, stGate, stExit,
       stULPop, stRPop, stLLPop, stLLDG, stRLDG,
       stChicken, stGasoline, stRampTgt, stExtraBall, stRattle, stPopTarget };

static sim_tState rzspook_stateDef[] = {
  {"Not Installed", 0, 0,           0, stDrain,   0, 0, 0, SIM_STNOTEXCL},
  {"Moving"},
  {"Playfield",     0, 0,           0, 0,         0, 0, 0, SIM_STNOTEXCL},

  {"Trough 7",      1, swTrough7,   0, stTrough6, 3},
  {"Trough 6",      1, swTrough6,   0, stTrough5, 3},
  {"Trough 5",      1, swTrough5,   0, stTrough4, 3},
  {"Trough 4",      1, swTrough4,   0, stTrough3, 3},
  {"Trough 3",      1, swTrough3,   0, stTrough2, 3},
  {"Trough 2",      1, swTrough2,   0, stTrough1, 3},
  {"Trough 1",      1, swTrough1,   0, 0,         0},
  {"Shooter Lane",  1, swShooter,   0, 0,         0},
  {"Launched",      1, 0,           0, stFree,   10},
  {"Drain",         1, 0,           0, stTrough7, 1, 0, 0, SIM_STNOTEXCL},

  {"Left Outlane",  1, swLOutlane,  0, stDrain,  10},
  {"Right Outlane", 1, swROutlane,  0, stDrain,  10},
  {"Left Inlane",   1, swLInlane,   0, stFree,    5},
  {"Right Inlane",  1, swRInlane,   0, stFree,    5},
  {"Left Sling",    1, swLSling,    0, stFree,    2},
  {"Right Sling",   1, swRSling,    0, stFree,    2},
  {"L Upper Sling", 1, swLUSling,   0, stFree,    2},
  {"R Upper Sling", 1, swRUSling,   0, stFree,    2},

  {"Left Orbit",    1, swLOrbit,    0, stLOrbitExit, 5, 0, 0, SIM_STNOTEXCL},
  {"Right Orbit",   1, swROrbit,    0, stROrbitExit, 5, 0, 0, SIM_STNOTEXCL},
  {"L Orbit Exit",  1, swROrbit,    0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"R Orbit Exit",  1, swLOrbit,    0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"L Inner Orbit", 1, swLInner,    0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"R Inner Shot",  1, 0,           0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"R Inner Orbit", 1, swRInner,    0, stRailUp,  2},
  {"Rail Upper",    1, swRailUp,    0, stRailLow, 2},
  {"Rail Lower",    1, swRailLow,   0, 0,         0},
  {"Ramp",          1, swRamp,      0, stFree,   10, 0, 0, SIM_STNOTEXCL},
  {"VUK",           1, swVUK,       0, 0,         0},
  {"Secret Passage",1, swSecret,    0, stVUK,     5},
  {"Spaulding",     2, swGateOpto,  0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"Upper PF Exit", 2, swExitOpto,  0, stFree,   10, 0, 0, SIM_STNOTEXCL},

  {"U Left Pop",    1, swULPop,     0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Right Pop",     1, swRPop,      0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"L Left Pop",    1, swLLPop,     0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Left LDG",      1, swLLDG,      0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Right LDG",     1, swRLDG,      0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Chicken",       1, swChicken,   0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Gasoline",      1, swGasoline,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Ramp Target",   1, swRampTgt,   0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Extra Ball",    1, swExtraBall, 0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Rattle",        1, swRattle,    0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Pop Target",    1, swPopTarget, 0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {0}
};

static void rzspook_setDrop(int down) {
  if (down != locals.drop) core_setSw(swDrop, locals.drop = down);
}

static int rzspook_handleBallState(sim_tBallStatus *ball, int *inports) {
  (void)inports;
  switch (ball->state) {
    case stTrough1:  if (sol(sLoad) && !core_getSw(swShooter)) return setState(stShooter, 5); break; /* a full lane loses the pulse */
    case stShooter:  if (sol(sLaunch) || sim_getSol(sShooterRel)) return setState(stLaunched, 2); break;
    case stVUK:      if (sol(sVUK)) return setState(stFree, 5); break;
    case stRInnerShot:
      if (locals.drop == 1) return setState(stRInner, 2);
      rzspook_setDrop(1);    /* the ball knocks the target down and comes back */
      return setState(stFree, 3);
    case stRailLow:  if (sol(sPost)) return setState(stRInlane, 5); break; /* the post drops: one ball rolls on */
    case stGate:     return setState(locals.gateUs > GATE_OPEN_US ? stExit : stFree, 5);
  }
  return 0;
}

/* HandleMechanics bit 0: the drop target, raised by its coil; bit 1: the flippers' end-of-stroke switches,
   closed while a flipper coil is on. The gate and robot positions are tracked from the servo pulses. */
static void rzspook_handleMech(int mech) {
  static const int coils[3][3] = { { sUFlipHigh, sUFlipLow, swUFlipEOS }, { sRFlipHigh, sRFlipLow, swRFlipEOS }, { sLFlipHigh, sLFlipLow, swLFlipEOS } };
  int i;
  for (i = 1; i <= 24; i++)
    locals.since[i] = sol(i) ? 0 : locals.since[i] < 10000 ? locals.since[i] + 1 : 10000;
  if (pinheck_servo(0)) locals.gateUs = pinheck_servo(0);
  if (pinheck_servo(1)) locals.robotUs = pinheck_servo(1);
  if ((mech & 0x01) && (sol(sDrop) || locals.drop < 0)) rzspook_setDrop(0);
  if (mech & 0x02)
    for (i = 0; i < 3; i++) {
      const int on = sol(coils[i][0]) || sol(coils[i][1]);
      if (on != locals.eos[i]) core_setSw(coils[i][2], locals.eos[i] = on); /* only on change: the switch test may toggle it */
    }
}

static int rzspook_getMech(int mechNo) {
  return mechNo == 0 ? locals.gateUs > GATE_OPEN_US : mechNo == 1 ? locals.robotUs : mechNo == 2 ? locals.drop == 1 : 0;
}

static sim_tInportData rzspook_inportData[] = {
  {0, 0x0005, stLSling},   {0, 0x0006, stRSling},
  {0, 0x0009, stLUSling},  {0, 0x000a, stRUSling},
  {0, 0x0011, stLInlane},  {0, 0x0012, stRInlane},
  {0, 0x0021, stLOutlane}, {0, 0x0022, stROutlane},
  {0, 0x0041, stLOrbit},   {0, 0x0042, stROrbit},
  {0, 0x0081, stLInner},   {0, 0x0082, stRInnerShot},
  {0, 0x0101, stULPop},    {0, 0x0102, stRPop},     {0, 0x0100, stLLPop},
  {0, 0x0201, stLLDG},     {0, 0x0202, stRLDG},
  {0, 0x0400, stRamp},     {0, 0x0800, stVUK},      {0, 0x1000, stSecret},
  {0, 0x2000, stDrain},
  {1, 0x0001, stGate},     {1, 0x0002, stChicken},  {1, 0x0004, stGasoline},
  {1, 0x0008, stRampTgt},  {1, 0x0010, stExtraBall}, {1, 0x0020, stRattle},
  {1, 0x0040, stPopTarget}, {1, 0x0080, stExit},
  {0}
};

static sim_tSimData rzspookSimData = {
  2,                    /* 2 game specific input ports */
  rzspook_stateDef,
  rzspook_inportData,
  { stTrough1, stTrough2, stTrough3, stTrough4, stTrough5, stTrough6, stTrough7 },
  NULL,                 /* no init */
  rzspook_handleBallState,
  NULL,                 /* no static drawing */
  TRUE,                 /* manual plunger (Space) next to the autoplunger */
  NULL,                 /* no custom key conditions */
  TRUE                  /* the keys move a ball on the playfield: no Up/Down in multiball */
};

static core_tLCDLayout rzspook_disp[] = {
  {0, 0, PINHECK_VIDEO_H, PINHECK_VIDEO_W, CORE_VIDEO, (genf *)pinheck_video, NULL}, {0}
};

/* factory POSITION 460 (the Propeller's defaults); servo levels 0-255 = 0-180 degrees (0.544-2.4 ms) */
static pinheck_tGameData rzspookGameData = {
  { GEN_PINHECK, rzspook_disp,
    { FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, 26, 0,
      pinheck_getsol, rzspook_handleMech, rzspook_getMech },
    &rzspookSimData },
  128, 32, 460, 544, 2400, 0, 1
};

static void init_rzspook(void) {
  int i;
  core_gameData = &rzspookGameData.core;
  memset(&locals, 0, sizeof(locals));
  for (i = 0; i <= 24; i++) locals.since[i] = 10000;
  locals.drop = -1;
  for (i = 0; i < 3; i++) locals.eos[i] = -1;
}

PINHECK_ROMSTART(rzspook, "RZO_V026.PRG", 0x4B350, CRC(db1ee6f9) SHA1(9bf575f033cfdb17dd5abad5e05735ea24f0ef24),
                 "PRP_V008.BIN", CRC(caeb2c41) SHA1(0393c4bbb6902cf18ef921fb19451e5e1064b6d6))
PINHECK_ROMEND
CORE_CLONEDEFNV(rzspook, pinheck, "Rob Zombie's Spookshow International", 2016, "Spooky Pinball", gl_mPINHECK, GAME_NOT_WORKING)
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
    (r'''/* Domino's Spectacular Pinball Adventure: sims/pinheck/dominos.c */
''',
     r'''/* Domino's Spectacular Pinball Adventure: sims/pinheck/dominos.c */
/* Rob Zombie's Spookshow International: sims/pinheck/rzspook.c */
'''),
])
edit('src/wpc/driver.c', [
    (r'''DRIVERNV(dominos)       //pinHeck 07/16 Domino's Spectacular Pinball Adventure
''',
     r'''DRIVERNV(dominos)       //pinHeck 07/16 Domino's Spectacular Pinball Adventure
DRIVERNV(rzspook)       //pinHeck 2016 Rob Zombie's Spookshow International (V26)
'''),
])
edit('tests/pinheck/link/register_build.py', [
    (r'''    ('src/wpc/sims/pinheck/dominos.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/dominos.o\n'),
]
''',
     r'''    ('src/wpc/sims/pinheck/dominos.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/dominos.o\n'),
    ('src/wpc/sims/pinheck/rzspook.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/rzspook.o\n'),
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
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11a.log 2>&1; tail -1 build/sdl3pinmame/11a.log
grep -i warning build/sdl3pinmame/11a.log | grep -cE 'rzspook|pinheck|dominos'
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 5: Both games boot**

Rob Zombie's first launch flashes the update and restarts at 184 s; its second launch prints the banner and takes `[E97000]`; then the romset check runs the system set alone and `rzspook` without media. Domino's runs the same check as before:

```sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/pic32mx/pinmame_check.sh
```
Expected (about 2 minutes each):
```text
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
git add src/wpc/sims/pinheck/rzspook.c src/wpc/pinheckgames.c src/wpc/driver.c tests/pinheck/games.sh tests/pinheck/pic32mx tests/pinheck/link/register_build.py src/pinmame.mak cmake vcproj
git commit -m "pinheck: Rob Zombie's Spookshow International (rzspook): game, simulator, boot check by game"
```

### Task 3: The simulator check

**Files:**
- Create: `tests/pinheck/sim/rzsim.py`
- Modify: `tests/pinheck/sim/pinmame_sim.sh`

**Interfaces:**
- Consumes: Task 2's simulator and `games.sh` (`SIM`); Plan 8b's `PINHECK_OUT_LOG` `W`, `S` and `V` lines and the console's `[MXXzzz]`, `[SXXzzz]`.
- Produces: `rzsim.py plan DIR` / `verify DIR`, as `sim.py`.

The check runs as Plan 8b's: a first boot in service, then attract mode with ball search off, where nothing moves a ball unless the check does. One ball goes from the trough to the shooter lane and out (autoplunger), into the VUK and out, knocks the drop target down, is raised by coil 7 and knocked down again, then rolls behind it into the lock; a second ball served the same way waits behind it on Rail Upper; two pulses of the stop post let them on one by one. Then the Spaulding gate, closed and open, the four end-of-stroke switches, the drain of both balls, and two more balls (one autoplunged, one plunged with Space) drained.

- [ ] **Step 1: The check**

`tests/pinheck/sim/rzsim.py`:

```python
#!/usr/bin/env python3
"""Rob Zombie's Spookshow simulator check: in attract mode the firmware's console coil and servo commands
([MXXzzz], [SXXzzz]) and the simulator keys move the balls; the switches the simulator closes
(PINHECK_OUT_LOG 'W' lines) must follow the coils and servos.
  rzsim.py plan DIR     write DIR/send, send_at, send_gap, keys.txt, frames
  rzsim.py verify DIR   check DIR/out2.log against the plan"""
import sys

FPS = 60
SEND_AT, GAP = 10.0, 0.05
# (time, console command) and (time, keys, hold frames): a ball's trip round the playfield, two balls locked on the
# right rail and released one by one, two balls drained. [MXX002] pulses a coil for about 5 ms, shorter than a
# frame; [S00160] sets the gate servo to 160 degrees
SEND = [(10.0, '[E97000]'), (12.0, '[M17002]'), (13.5, '[M16002]'), (16.5, '[M03002]'), (19.0, '[M06002]'),
        (22.0, '[M17002]'), (22.5, '[M16002]'), (24.8, '[M04002]'), (25.6, '[M04002]'), (26.5, '[S00160]'),
        (28.0, '[M02002]'), (28.5, '[M07002]'), (29.0, '[M18002]'), (29.5, '[M12002]'),
        (31.0, '[M17002]'), (32.0, '[M16002]'), (32.5, '[M17002]')]
KEYS = [(15.0, 'V', 1), (18.0, 'RCONTROL N', 1), (20.0, 'RCONTROL N', 1), (21.0, 'RCONTROL N', 1), (23.8, 'RCONTROL N', 1),
        (26.3, 'G', 1), (27.3, 'G', 1), (30.0, 'Q', 1), (30.5, 'Q', 1), (33.0, 'SPACE', 40), (35.0, 'Q', 1), (35.5, 'Q', 1)]
END = 38.0
TROUGH = list(range(12, 19))


def plan(d):
    slots = {round((t - SEND_AT) / GAP): c for t, c in SEND}
    send = ''.join(slots.get(k, '') + '~' for k in range(max(slots) + 1))
    keys = ''.join('%d tap %d %s\n' % (round(t * FPS), h, ' '.join('KEYCODE_' + k for k in ks.split())) for t, ks, h in KEYS)
    for name, text in (('send', send), ('send_at', '%g' % SEND_AT), ('send_gap', '%g' % GAP),
                       ('keys.txt', keys), ('frames', '%d' % round(END * FPS))):
        open('%s/%s' % (d, name), 'w').write(text + ('\n' if name != 'keys.txt' else ''))


def load(path):
    sw, coils, servo = [], [], []
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
    return sw, coils, servo


def verify(d):
    sw, coils, servo = load(d + '/out2.log')
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

    def pulse(c, t0):
        on = coil_on(c, t0, t0 + 1.0)
        off = [t for t, w in coils if on and t > on[0] and not w >> (c - 1) & 1]
        return off[0] - on[0] if off else 1.0

    rest = [s for s in [11] + TROUGH if state(s, 9.0)]
    check(rest == TROUGH and state(48, 9.0) == 0, 'at rest closed %s, drop target %d; expected the trough 12-18, the target up' % (rest, state(48, 9.0)))
    check(pulse(18, 12.0) < 1.0 / FPS and pulse(17, 13.5) < 1.0 / FPS and pulse(4, 16.4) < 1.0 / FPS,
          'the test pulses are not shorter than a frame: %.4f %.4f %.4f s' % (pulse(18, 12.0), pulse(17, 13.5), pulse(4, 16.4)))
    eject = coil_on(18, 12.0, 13.0)
    check(eject and edges(12, eject[0], eject[0] + 0.1, 0) and edges(11, eject[0], eject[0] + 1.0, 1),
          'trough: [M17002] at 12.0 (coil 18 %s) did not move trough 1 (12) to the shooter lane (11)' % eject[:1])
    check([state(s, 13.4) for s in TROUGH] == [1] * 6 + [0], 'trough: the six balls did not roll down to 12-17: %s' % [state(s, 13.4) for s in TROUGH])
    launch = coil_on(17, 13.5, 14.5)
    check(launch and edges(11, launch[0], launch[0] + 0.1, 0), 'autoplunger: [M16002] did not clear the shooter lane')
    vuk = coil_on(4, 16.4, 17.0)
    check(state(43, 16.4) == 1 and vuk and edges(43, vuk[0], vuk[0] + 0.1, 0),
          'VUK: held %d, coil 4 %s, released %s' % (state(43, 16.4), vuk[:1], edges(43, 16.4, 17.0, 0)))
    check(edges(35, 18.0, 18.2, 1) == [] and edges(48, 18.0, 18.2, 1) and state(48, 18.9) == 1,
          'drop target: the right inner orbit with the target up did not knock it down (48 %s, 35 %s)' % (edges(48, 18.0, 18.2), edges(35, 18.0, 18.2)))
    reset = coil_on(7, 19.0, 19.5)
    check(reset and edges(48, reset[0], reset[0] + 0.1, 0) and state(48, 19.9) == 0, 'drop target: coil 7 (%s) did not raise it' % reset[:1])
    check(edges(48, 20.0, 20.2, 1) and not edges(35, 20.0, 20.9), 'drop target: the second shot did not knock it down, or went past it')
    check(edges(35, 21.0, 21.2, 1) and edges(34, 21.2, 21.6, 1) and edges(33, 21.4, 22.0, 1) and not edges(33, 21.9, 24.8),
          'lock: the ball behind the dropped target did not come to rest on Rail Lower (35 %s, 34 %s, 33 %s)' %
          (edges(35, 21.0, 21.2), edges(34, 21.0, 22.0), edges(33, 21.0, 24.8)))
    check(edges(34, 24.0, 24.6, 1) and not edges(34, 24.6, 24.8) and state(35, 24.7) == 0,
          'lock: the second ball did not wait on Rail Upper behind the first (34 %s)' % edges(34, 23.8, 24.8))
    post = coil_on(5, 24.8, 25.0)
    out = edges(33, 24.8, 25.0, 0)
    check(post and out and edges(22, out[0], out[0] + 0.5, 1) and edges(34, out[0], out[0] + 0.6, 0) and state(33, 25.5) == 1,
          'stop post: coil 5 at %s released %s; the first ball must roll to the right inlane (22 %s), the second down to Rail Lower' %
          (post[:1], out, edges(22, 24.8, 25.4, 1)))
    post = coil_on(5, 25.6, 25.8)
    check(post and edges(33, 25.6, 25.8, 0) and [state(s, 26.2) for s in (33, 34, 35)] == [0, 0, 0],
          'stop post: the second pulse (%s) did not release the second ball' % post[:1])
    shut = [us for t, n, us in servo if n == 0 and 25.5 <= t < 26.4]
    check(shut and all(abs(us - 1227) < 10 for us in shut), 'gate servo before [S00160]: %s us, expected 1227 (closed)' % sorted(set(round(u) for u in shut)))
    check(edges(95, 26.3, 26.5, 1) and not edges(96, 26.3, 27.2), 'Spaulding: a shot at the closed gate must close the gate opto (95) only')
    wide = [us for t, n, us in servo if n == 0 and 26.9 <= t < 27.3]
    check(wide and all(abs(us - 2201) < 10 for us in wide), 'gate servo after [S00160]: %s us, expected 2201' % sorted(set(round(u) for u in wide)))
    check(edges(95, 27.3, 27.5, 1) and edges(96, 27.3, 27.8, 1), 'Spaulding: through the open gate the ball must pass the exit opto (96)')
    for c, s, t in ((3, 41, 28.0), (8, 41, 28.5), (19, 24, 29.0), (13, 25, 29.5)):
        on = coil_on(c, t, t + 0.4)
        check(on and edges(s, on[0], on[0] + 0.05, 1) and edges(s, on[0], on[0] + 0.1, 0),
              'flipper coil %d (%s) did not close and open its end-of-stroke switch %d' % (c, on[:1], s))
    check(edges(17, 30.0, 31.0, 1) and [state(s, 30.95) for s in [11] + TROUGH] == [0] + [1] * 7,
          'drain: the two balls did not return to the trough (11-18: %s)' % [state(s, 30.95) for s in [11] + TROUGH])
    check(len(edges(11, 30.9, 34.5, 1)) == 2 and len(edges(11, 30.9, 34.5, 0)) == 2,
          'two balls: an autoplunger and a plunger (Space) launch did not each clear the shooter lane')
    check(len(edges(18, 35.0, 37.5, 1)) >= 1 and [state(s, 37.5) for s in [11] + TROUGH] == [0] + [1] * 7,
          'two balls: Q twice did not drain both balls (11-18: %s)' % [state(s, 37.5) for s in [11] + TROUGH])
    for f in fails:
        print('SIM FAIL: ' + f)
    print('sim: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'verify'):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2]))
```

`chmod +x tests/pinheck/sim/rzsim.py`

`pinmame_sim.sh` takes the game, its check and its simulator file from `games.sh`:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/sim/pinmame_sim.sh', [
    (r''': "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
''',
     r''': "${PINHECK_ZIP:?set PINHECK_ZIP to the game's romset zip}"
'''),
    (r'''cd "$(dirname "$0")" || exit 2
B=build/pinmame
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
B=build/pinmame
'''),
    (r'''ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
python3 sim.py plan $B || exit 2
''',
     r'''ln -s "$PINHECK_ZIP" $B/roms/$GAME.zip || exit 2
python3 $SIM plan $B || exit 2
'''),
    (r'''timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms''',
     r'''timeout -k 30 3000 "$SDL3PINMAME" $GAME -rompath roms'''),
    (r'''	-I$S -I$S/wpc -I$S/unix -I$S/unix/sysdep $S/wpc/sims/pinheck/dominos.c || { echo "SIM FAIL: dominos.c does not compile cleanly"; exit 1; }
python3 sim.py verify $B || exit 1
''',
     r'''	-I$S -I$S/wpc -I$S/unix -I$S/unix/sysdep $S/wpc/sims/pinheck/$GAME.c || { echo "SIM FAIL: $GAME.c does not compile cleanly"; exit 1; }
python3 $SIM verify $B || exit 1
'''),
])
EOF
```

- [ ] **Step 2: Both simulator checks pass**

```sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/sim/pinmame_sim.sh
```
Expected (under a minute each):
```text
sim: 24 checks, 0 failures
pinmame sim: ok
sim: 19 checks, 0 failures
pinmame sim: ok
```

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/sim
git commit -m "pinheck: Rob Zombie's simulator check (trough, VUK, drop target, lock on the right rail, gate, end-of-stroke switches)"
```

### Task 4: The service tests (check 5)

**Files:**
- Modify: `tests/pinheck/board/check5.py`, `tests/pinheck/board/pinmame_board.sh`

**Interfaces:**
- Consumes: Plan 8a's check 5 (console coil and lamp commands, the service menu's solenoid, servo, lamp, RGB and switch tests read back from `PINHECK_OUT_LOG` and the frame log); Task 1's servo range; the switch, lamp and coil lists of the documents (research §4).
- Produces: check 5 by `PINHECK_GAME`.

For Rob Zombie the console commands and the lamp test are Domino's (the same 64 lamps, 24 coils and GI chain); the solenoid test starts at `AUTOPLUNGER`, the servo test moves the gate and the robot to the widths of Ruling 4 and checks the custom outputs 57–58 in the game's range, the RGB test adds the LDG light on 62–64, attract mode checks the GI with its flashers off, and the switch test starts from five balls in the trough and one in the shooter lane (Ruling 8).

- [ ] **Step 1: Check 5 by game**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/board/pinmame_board.sh', [
    (r''': "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
''',
     r''': "${PINHECK_ZIP:?set PINHECK_ZIP to the game's romset zip}"
'''),
    (r'''cd "$(dirname "$0")" || exit 2
B=build/pinmame
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
B=build/pinmame
'''),
    (r'''ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
''',
     r'''ln -s "$PINHECK_ZIP" $B/roms/$GAME.zip || exit 2
'''),
    (r'''timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms''',
     r'''timeout -k 30 3000 "$SDL3PINMAME" $GAME -rompath roms'''),
])
edit('tests/pinheck/board/check5.py', [
    (r'''  check5.py verify DIR    check DIR/out2.log and DIR/frames2.bin against the plan"""
import struct
import sys
''',
     r'''  check5.py verify DIR    check DIR/out2.log and DIR/frames2.bin against the plan
PINHECK_GAME (dominos, rzspook) selects the game's servo and RGB tests and its resting balls."""
import os
import struct
import sys
'''),
    (r'''RGB = ['S%d' % s for s in range(51, 57)]
''',
     r'''RGB = ['S%d' % s for s in range(51, 57)]
GAME = os.environ.get('PINHECK_GAME', 'dominos')
if GAME == 'rzspook':
    RGB += ['S62', 'S63', 'S64']                    # the LDG light, the external WS2801 LED
SERVO_US = (544, 2400) if GAME == 'rzspook' else (1000, 2000)   # the game's servo levels 0 and 255
'''),
    (r'''    tap('0', 1.0)                                   # SOLENOID TEST: KNOCKER
    for c in range(24):
''',
     r'''    tap('0', 1.0)                                   # SOLENOID TEST: KNOCKER (Rob Zombie: AUTOPLUNGER)
    for c in (list(range(16, 24)) + list(range(16)) if GAME == 'rzspook' else range(24)):
'''),
    (r'''    tap('RSHIFT', 1.0)                              # SERVO
    tap('0', 1.0)                                   # SERVO TEST: NOID RIGHT
    tap('0', 1.5, ('servo', 0, 0))                  # servo 0 runs at 0 deg
    tap('RSHIFT', 1.0)                              # NOID STOP
    tap('0', 1.0)                                   # Enter: pulses stop
    tap('RSHIFT', 1.0)                              # NOID LEFT
    tap('0', 1.5, ('servo', 0, 255))                # servo 0 runs at 180 deg
    tap('7', 1.0)                                   # back to SERVO
    tap('RSHIFT', 1.0)                              # LAMP
''',
     r'''    tap('RSHIFT', 1.0)                              # SERVO
    if GAME == 'rzspook':
        tap('0', 1.0)                               # SERVO TEST: GATE OPEN
        tap('0', 1.5, ('position', 0, 2222))        # the Spaulding gate opens
        tap('RSHIFT', 1.0)                          # GATE CLOSE
        tap('0', 1.5, ('position', 0, 1227))
        tap('RSHIFT', 1.0)                          # ROBOT START
        tap('0', 1.5, ('position', 1, 1476))
        tap('RSHIFT', 1.0)                          # ROBOT END
        tap('0', 1.5, ('position', 1, 2097))
    else:
        tap('0', 1.0)                               # SERVO TEST: NOID RIGHT
        tap('0', 1.5, ('servo', 0, 0))              # servo 0 runs at 0 deg
        tap('RSHIFT', 1.0)                          # NOID STOP
        tap('0', 1.0)                               # Enter: pulses stop
        tap('RSHIFT', 1.0)                          # NOID LEFT
        tap('0', 1.5, ('servo', 0, 255))            # servo 0 runs at 180 deg
    tap('7', 1.0)                                   # back to SERVO
    tap('RSHIFT', 1.0)                              # LAMP
'''),
    (r'''    rgb = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    tap('0', 1.0, ('rgb', rgb[0] + (0, 0, 0)))      # RGB1 RED
    for v in rgb[1:]:
        tap('RSHIFT', 1.0, ('rgb', v + (0, 0, 0)))
    for v in rgb:
        tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) + v))
    tap('7', 1.0)                                   # back to RGB LIGHTING
''',
     r'''    rgb = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    ext = (255, 255, 255) if GAME == 'rzspook' else ()          # the LDG light stays white from attract
    tap('0', 1.0, ('rgb', rgb[0] + (0, 0, 0) + ext))    # RGB1 RED
    for v in rgb[1:]:
        tap('RSHIFT', 1.0, ('rgb', v + (0, 0, 0) + ext))
    for v in rgb:
        tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) + v + ext))
    if GAME == 'rzspook':
        # LDG RED, GREEN, BLUE, WHITE: the LDG light's lines are red, blue, green (SWAP G <-> B: NO)
        for v in [(255, 0, 0), (0, 0, 255), (0, 255, 0), (255, 255, 255)]:
            tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) * 2 + v))
    tap('7', 1.0)                                   # back to RGB LIGHTING
'''),
    (r'''    # attract mode: blinking start lamp 91, external WS2801 LED white, GI_14 flashing
    check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
    check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [255] * 3, 'external LED 0 is not white on 62-64')
    check(o.rises('S43', 5, MENU_AT), 'attract flasher GI_14 never reached solenoid 43')
''',
     r'''    # attract mode: blinking start lamp 91, external WS2801 LED white; Domino's flashes GI_14, Rob Zombie
    # lights the playfield GI but its flashers GI_12 and GI_13
    check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
    check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [255] * 3, 'external LED 0 is not white on 62-64')
    if GAME == 'rzspook':
        gi = [o.at('S%d' % s, MENU_AT) for s in range(37, 45)]
        check(gi == [255] * 4 + [0, 0] + [255] * 2, 'attract playfield GI 37-44 is %s, expected all on but the flashers 41 and 42' % gi)
    else:
        check(o.rises('S43', 5, MENU_AT), 'attract flasher GI_14 never reached solenoid 43')
'''),
    (r'''    # the simulator's balls: [M17020] loaded one into the shooter lane, the solenoid test's PLUNGER launched it
    # and its LOAD BALL loaded the next, so the shooter lane (0) and trough 1 (1) rest closed; Noid Home (39) may
    t0 = min(t for t, k, hold, e in ev if e and e[0] == 'switch')
    rest = [g for g in (grid(f) for tt, f in frames if t0 - 1.0 <= tt < t0 - 0.1) if g]
    base = set(rest[-1][0]) if rest else set()
    check(rest and base - {39} == {0, 1} and rest[-1][1] == (1,), 'switch test at rest shows %s, expected the shooter lane (0), trough 1 (1), maybe Noid Home (39), and the closed coin door' % (rest[-1:],))
''',
     r'''    # the simulator's balls: [M17020] loaded one into the shooter lane, the solenoid test's PLUNGER launched it
    # and its LOAD BALL loaded the next, so the shooter lane (0) and trough 1 (1) rest closed; Noid Home (39) may.
    # Rob Zombie's trough keeps the other five of its seven balls (trough 1-5)
    t0 = min(t for t, k, hold, e in ev if e and e[0] == 'switch')
    rest = [g for g in (grid(f) for tt, f in frames if t0 - 1.0 <= tt < t0 - 0.1) if g]
    base = set(rest[-1][0]) if rest else set()
    want = {0, 1, 2, 3, 4, 5} if GAME == 'rzspook' else {0, 1}
    check(rest and base - {39} == want and rest[-1][1] == (1,), 'switch test at rest shows %s, expected switches %s, maybe Noid Home (39), and the closed coin door' % (rest[-1:], sorted(want)))
'''),
    (r'''            check(o.at('S58', t + 1.1) == 161, 'target bank servo 58 is %d, expected 161 (1631 us)' % o.at('S58', t + 1.1))
''',
     r'''            check(o.at('S58', t + 1.1) == 161, 'target bank servo 58 is %d, expected 161 (1631 us)' % o.at('S58', t + 1.1))
        elif e[0] == 'position':
            w = [us for tt, us in o.servo.get(e[1], []) if t + 0.2 <= tt < t + 0.5]
            level = round((e[2] - SERVO_US[0]) * 255 / (SERVO_US[1] - SERVO_US[0]))
            check(w and all(abs(us - e[2]) < 10 for us in w), 'servo %d pulses %s, expected %d us' % (e[1], sorted(set(round(us) for us in w)), e[2]))
            check(abs(o.at('S%d' % (57 + e[1]), t + 0.5) - level) <= 1, 'servo output %d is %d, expected %d' % (57 + e[1], o.at('S%d' % (57 + e[1]), t + 0.5), level))
'''),
])
EOF
```

- [ ] **Step 2: Domino's plan is unchanged**

```sh
mkdir -p build/c5a build/c5b && git show HEAD:tests/pinheck/board/check5.py > build/check5-before.py
python3 build/check5-before.py plan build/c5a && python3 tests/pinheck/board/check5.py plan build/c5b
for f in send send_at send_gap keys.txt frames; do cmp -s build/c5a/$f build/c5b/$f || echo "differs: $f"; done; echo "check 5 plan compared"
rm -rf tests/pinheck/board/__pycache__
```
Expected: `check 5 plan compared` and no `differs` line.

- [ ] **Step 3: Both games' check 5**

```sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/board/pinmame_board.sh
```
Expected (about 2 minutes each):
```text
timing: 74878 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.588 0.706 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 272 service-test steps, 0 failures
pinmame board: ok
timing: 74937 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.580 0.722 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 266 service-test steps, 0 failures
pinmame board: ok
```

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/board
git commit -m "pinheck: check 5 by game; Rob Zombie's solenoid, servo, lamp, RGB (LDG) and switch tests"
```

### Task 5: The display frames and the look

**Files:**
- Modify: `tests/pinheck/display/look.py`, `tests/pinheck/display/pinmame_look.sh`, `tests/pinheck/display/pinmame_display.sh`

**Interfaces:**
- Consumes: Task 1's aligned `POSITION`; the look check (every value of every display setting through the service menu, snapshots against the model, the stored settings after a restart); the display check (`[V00<clip>]` pixel-exact against the `.VID`, frames equal to the hub framebuffer).
- Produces: both checks by `PINHECK_GAME`; `look.py`'s `shown(st)`.

Rob Zombie's display items are `MAIN SETTINGS` 11–14 (Domino's 13–16), its start-up packet carries word 0 = 250 and `POSITION` 460, and after the sweep its stored `POSITION` is 461 (Ruling 3).

- [ ] **Step 1: The checks by game**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/look.py', [
    (r'''  look.py verify DIR       check DIR/prop2.log's config packets and DIR/snap/*.png against the model"""
''',
     r'''  look.py verify DIR       check DIR/prop2.log's config packets and DIR/snap/*.png against the model
PINHECK_GAME (dominos, rzspook) selects the game: its factory POSITION is drawn unshifted (the driver
moves it to display.c's 340), its menu and its Propeller's packets."""
'''),
    (r'''PROP_DEFAULT = '00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e'   # the Propeller's own packet at start-up
SAVED = '00 b1 01 55 -- -- 00 af 00 80 00 20 00 00'          # at the next start-up: POSITION, BRIGHTNESS, BAR BRIGHT kept
''',
     r'''PROP_DEFAULT = '00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e'   # the Propeller's own packet at start-up
SAVED = '00 b1 01 55 -- -- 00 af 00 80 00 20 00 00'          # at the next start-up: POSITION, BRIGHTNESS, BAR BRIGHT kept
ALIGNED, TO_SHAPE = 340, 12                                   # factory POSITION; MAIN SETTINGS steps to PIXEL SHAPE
GAME = os.environ.get('PINHECK_GAME', 'dominos')
if GAME == 'rzspook':
    PROP_DEFAULT = '00 fa 01 cc 00 00 00 ff 00 80 00 20 00 3e'
    SAVED = '00 fa 01 cd -- -- 00 af 00 80 00 20 00 00'
    ALIGNED, TO_SHAPE = 460, 10
'''),
    (r'''    return {'shape': w[2] if w[2] <= HIGHREZ else SQUARE, 'brightness': min(w[3], 255),
            'position': w[1], 'bar': min(w[6], 62)}, True
''',
     r'''    return {'shape': w[2] if w[2] <= HIGHREZ else SQUARE, 'brightness': min(w[3], 255),
            'position': w[1] + 340 - ALIGNED, 'bar': min(w[6], 62)}, True


def shown(st):
    """the look display.c draws for the menu state st: the game's factory POSITION is its 340"""
    return dict(st, position=st['position'] + 340 - ALIGNED)
'''),
    (r'''    st = {'shape': ROUND, 'brightness': 255, 'position': 340, 'bar': 62}
''',
     r'''    st = {'shape': ROUND, 'brightness': 255, 'position': ALIGNED, 'bar': 62}
'''),
    (r'''    for _ in range(12):
        tap('RSHIFT', 30)                           # ... PIXEL SHAPE
''',
     r'''    for _ in range(TO_SHAPE):
        tap('RSHIFT', 30)                           # ... PIXEL SHAPE
'''),
    (r'''    files = ['dominos.png'] + ['domi%04d.png' % k for k in range(len(snaps) - 1)]
''',
     r'''    files = [GAME + '.png'] + [GAME[:4] + '%04d.png' % k for k in range(len(snaps) - 1)]
'''),
    (r'''        hit = next((f for f in reversed(recent) if matches(rows, render(st, f))), None)
''',
     r'''        hit = next((f for f in reversed(recent) if matches(rows, render(shown(st), f))), None)
'''),
    (r'''        elif st != EXACT and matches(rows, render(EXACT, hit)):
''',
     r'''        elif shown(st) != EXACT and matches(rows, render(EXACT, hit)):
'''),
    (r'''        rows = read_png(os.path.join(d, 'snap3', 'dominos.png'))
''',
     r'''        rows = read_png(os.path.join(d, 'snap3', GAME + '.png'))
'''),
])
edit('tests/pinheck/display/pinmame_look.sh', [
    (r''': "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
''',
     r''': "${PINHECK_ZIP:?set PINHECK_ZIP to the game's romset zip}"
'''),
    (r'''cd "$(dirname "$0")" || exit 2
B=build/look
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
B=build/look
'''),
    (r'''ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
''',
     r'''ln -s "$PINHECK_ZIP" $B/roms/$GAME.zip || exit 2
'''),
    (r'''timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms''',
     r'''timeout -k 30 3000 "$SDL3PINMAME" $GAME -rompath roms'''),
])
edit('tests/pinheck/display/pinmame_display.sh', [
    (r''': "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
''',
     r''': "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the game's unzipped update}"
'''),
    (r''': "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
''',
     r''': "${PINHECK_ZIP:?set PINHECK_ZIP to the game's romset zip}"
'''),
    (r'''cd "$(dirname "$0")" || exit 2
B=build/pinmame
CLIP=${PINHECK_CLIP:-LT5}
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
B=build/pinmame
CLIP=${PINHECK_CLIP:-$CLIP}
'''),
    (r'''ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
''',
     r'''ln -s "$PINHECK_ZIP" $B/roms/$GAME.zip || exit 2
'''),
    (r'''timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms''',
     r'''timeout -k 30 3000 "$SDL3PINMAME" $GAME -rompath roms'''),
    (r'''python3 render.py $B/snap/dominos.png $B/frames1.bin''',
     r'''python3 render.py $B/snap/$GAME.png $B/frames1.bin'''),
    (r'''python3 render.py $B/snap2/dominos.png $B/frames2.bin''',
     r'''python3 render.py $B/snap2/$GAME.png $B/frames2.bin'''),
])
EOF
```

- [ ] **Step 2: Both games' display and look checks**

```sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_look.sh | grep -v '^look: rzsp'
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_display.sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 tests/pinheck/display/pinmame_look.sh | grep -v '^look: domi'
```
Expected (about a minute each):
```text
frames: 403 frames from 1.68s to 19.99s, 22.0 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $586c when latched
render: frame region equals decoded frame 7 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (7 pens, none a frame colour)
frames: 91 frames from 12.02s to 15.01s, 30.1 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $586c when latched
frames: DMB.VID: frames 0..29 of 30 shown pixel-exact, contiguous and in order (log frames 0..29)
render: frame region equals decoded frame 1 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (12 pens, none a frame colour)
render: the core's solenoid log shows (115 pixels)
pinmame display: ok
look: 258 config packets, one per menu step, as predicted
look: after the restart SQUARE brightness 175 position 341 bar 0
look: 0 failed
pinmame look: ok
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
look: 258 config packets, one per menu step, as predicted
look: after the restart ROUND brightness 175 position 341 bar 0
look: 0 failed
pinmame look: ok
```
The look check's snapshot lines (`look: rzspook.png = ROUND brightness 255 position 460 bar 62` and ten more; Domino's eleven) are filtered out above. Rob Zombie's restart line prints the drawn position, 341, for its stored 461 (Ruling 3); the restart's pixel shape is either, as on Domino's (Plan "display look", restart finding).

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/display
git commit -m "pinheck: display and look checks by game; Rob Zombie's look aligned at its factory POSITION 460"
```

### Task 6: Names and the VBScript constants

**Files:**
- Modify: `src/wpc/pinheck_names.h`, `tests/pinheck/vpx/names.py`, `tests/pinheck/vpx/check.sh`

**Interfaces:**
- Consumes: Plan 10's `pinheck_name_t`, `names.py check SRC` / `vbs SRC`, `read(src)` (used by the test table's generator outside the repository).
- Produces: `pinheck_rzspook_switch_names[]`, `pinheck_rzspook_lamp_names[]`, `pinheck_rzspook_solenoid_names[]`; `names.py check SRC [GAME]`, `names.py vbs SRC [GAME]`, `read(src, game='dominos')`.

- [ ] **Step 1: Domino's script before the change**

Run: `python3 tests/pinheck/vpx/names.py vbs src > build/pinheck-before.vbs`

- [ ] **Step 2: The names**

Switch and lamp names are the PDFs', coils, GI, servos and RGB the wiring chart's; ids are `rzspook.c`'s constants (Ruling 13):

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck_names.h', [
    (r'''/* Domino's Spectacular Pinball Adventure (pinHeck): switch, lamp and output names for table scripts.

   Sources: Dominos-Switch-Matrix.pdf''',
     r'''/* pinHeck games: switch, lamp and output names for table scripts, one section per game.

   Domino's Spectacular Pinball Adventure (pinheck_dominos_*).
   Sources: Dominos-Switch-Matrix.pdf'''),
    (r'''     servos 0-4                      57-61 (0-255 = 1.0-2.0 ms pulse)
''',
     r'''     servos 0-4                      57-61 (0-255: the game's pulse range, Domino's 1.0-2.0 ms)
'''),
    (r'''   HandleMechanics bit 0 simulates the Noid (servo 0, closing Noid Home 58), bit 1 the target bank (servo 1);
   a table that moves them itself clears the bit. */
''',
     r'''   HandleMechanics bit 0 simulates the Noid (servo 0, closing Noid Home 58), bit 1 the target bank (servo 1);
   a table that moves them itself clears the bit.

   Rob Zombie's Spookshow International (pinheck_rzspook_*).
   Sources: RZ_Switch_Matrix_Production.pdf, RZ_Lamp_Matrix_Production.pdf and wiring-board-chart.png (coils, GI,
   servos); numbering as above. Not in them: User (2) and Back (5), as for Domino's. Servo levels 0-255 are
   0-180 degrees (0.544-2.4 ms). The LDG light (external RGB LED) gets red, blue, green on 62-64 while the
   service menu's SWAP G <-> B is NO, its factory setting. HandleMechanics bit 0 raises the drop target (48) on
   its coil, bit 1 closes the flippers' end-of-stroke switches while their coils are on. */
'''),
    (r'''  { 64, "sExtB",       "External RGB LED blue" },
  { 0 }
};
''',
     r'''  { 64, "sExtB",       "External RGB LED blue" },
  { 0 }
};

static const pinheck_name_t pinheck_rzspook_switch_names[] = {
  {   1, "swCoinDoor",  "Coin Door (closed)" },
  {   2, "swUser",      "User" },
  {   3, "swRFlip",     "Right Flipper" },
  {   4, "swLFlip",     "Left Flipper" },
  {   5, "swBack",      "Back" },
  {   6, "swEnter",     "Enter (menu)" },
  {   7, "swCoin",      "Coin Mech" },
  {   8, "swTilt",      "Tilt" },
  {  11, "swShooter",   "Shooter Lane" },
  {  12, "swTrough1",   "Trough 1" },
  {  13, "swTrough2",   "Trough 2" },
  {  14, "swTrough3",   "Trough 3" },
  {  15, "swTrough4",   "Trough 4" },
  {  16, "swTrough5",   "Trough 5" },
  {  17, "swTrough6",   "Trough 6" },
  {  18, "swTrough7",   "Trough 7" },
  {  21, "swROutlane",  "Right Out Lane" },
  {  22, "swRInlane",   "Right In Lane" },
  {  23, "swRSling",    "Right Lower Sling" },
  {  24, "swRFlipEOS",  "Right Flip EOS" },
  {  25, "swLFlipEOS",  "Left Flip EOS" },
  {  26, "swLSling",    "Left Lower Sling" },
  {  27, "swLInlane",   "Left In Lane" },
  {  28, "swLOutlane",  "Left Out Lane" },
  {  31, "swExtraBall", "Extra Ball" },
  {  32, "swRUSling",   "Right Upper Sling" },
  {  33, "swRailLow",   "Right Rail Lower" },
  {  34, "swRailUp",    "Right Rail Upper" },
  {  35, "swRInner",    "Right Inner Orbit" },
  {  36, "swRPop",      "Right Pop Bumper" },
  {  37, "swChicken",   "Left Upper Playfield (Chicken)" },
  {  38, "swGasoline",  "Right Upper Playfield (Gasoline)" },
  {  41, "swUFlipEOS",  "Upper Flipper EOS" },
  {  42, "swROrbit",    "Right Outer Orbit" },
  {  43, "swVUK",       "VUK" },
  {  44, "swSecret",    "Secret Passage" },
  {  45, "swLInner",    "Left Inner Orbit" },
  {  46, "swRLDG",      "Right Living Dead Girl" },
  {  47, "swLLDG",      "Left Living Dead Girl" },
  {  48, "swDrop",      "Drop Target (down)" },
  {  51, "swRampTgt",   "Ramp Target" },
  {  52, "swRattle",    "Right Rattle Switch" },
  {  54, "swLLPop",     "Lower Left Pop" },
  {  55, "swULPop",     "Upper Left Pop" },
  {  56, "swPopTarget", "Pop Target" },
  {  57, "swLUSling",   "Left Upper Sling" },
  {  58, "swLOrbit",    "Left Orbit" },
  {  61, "swRamp",      "Ramp" },
  {  94, "swStart",     "Start Button" },
  {  95, "swGateOpto",  "Upper PF Opto Spaulding (gate)" },
  {  96, "swExitOpto",  "Upper PF Opto (exit)" },
  { 112, "swLRFlip",    "Right Flipper button (reaches switch 3)" },
  { 114, "swLLFlip",    "Left Flipper button (reaches switch 4)" },
  { 0 }
};

static const pinheck_name_t pinheck_rzspook_lamp_names[] = {
  {  11, "lRockAgain",         "Rock Again" },
  {  12, "lLDG",               "LDG" },
  {  13, "lRedHot",            "Red Hot" },
  {  14, "lDragula",           "Dragula" },
  {  15, "lHouse100K",         "House 100K" },
  {  16, "lSuperBeast",        "Super Beast" },
  {  17, "lDeadCityRadio",     "Dead City Radio" },
  {  18, "lMurderRide",        "Murder Ride" },
  {  21, "lWhat",              "What" },
  {  22, "lDemonoid",          "Demonoid Phen" },
  {  23, "lAmerWitch",         "Amer. Witch" },
  {  24, "lHellBound",         "Hell Bound" },
  {  25, "lExtraBall",         "Extra Ball" },
  {  26, "lNum3",              "3" },
  {  27, "lLetterO",           "O" },
  {  28, "lLetterP",           "P" },
  {  31, "lSkillShot2",        "Skill Shot 2" },
  {  32, "lGein",              "Gein" },
  {  33, "lFish",              "Fish" },
  {  34, "lDrSatan",           "Dr. Satan" },
  {  35, "lInnerRightArrow",   "Inner Right Arrow" },
  {  36, "lRightOrbitArrow",   "Right Orbit Arrow" },
  {  37, "lTopX",              "Top X" },
  {  38, "lBottomX",           "Bottom X" },
  {  41, "lLetterC",           "C" },
  {  42, "lLetterH",           "H" },
  {  43, "lSkillShot1",        "Skill Shot 1" },
  {  44, "lNum1",              "1" },
  {  45, "lHurryUp",           "Hurry Up" },
  {  46, "lVideoMode",         "Video Mode" },
  {  47, "lModeStart",         "Mode Start" },
  {  48, "lLeftOrbitArrow",    "Left Orbit Arrow" },
  {  51, "lSkillShot3",        "Skill Shot 3" },
  {  52, "lLock1",             "Lock 1" },
  {  53, "lLock2",             "Lock 2" },
  {  54, "lLock3",             "Lock 3" },
  {  55, "lInnerLeftArrow",    "Inner Left Arrow" },
  {  56, "lAdvJackpot",        "Adv. Jackpot" },
  {  57, "lLDGLeftArrow",      "LDG Left Arrow" },
  {  58, "lLDGRightArrow",     "LDG Right Arrow" },
  {  61, "lCollectJackpot",    "Collect Jackpot" },
  {  62, "lRampArrow",         "Ramp Arrow" },
  {  63, "lNum2",              "2" },
  {  64, "lChicken",           "Chicken" },
  {  65, "lGasoline",          "Gasoline" },
  {  66, "l2X",                "2X" },
  {  67, "l5X",                "5X" },
  {  68, "l10X",               "10X" },
  {  91, "lStart",             "Start Button" },
  { 0 }
};

/* solenoid outputs: coils, GI strings, RGB channels and servos */
static const pinheck_name_t pinheck_rzspook_solenoid_names[] = {
  {  1, "sKnocker",    "Knocker" },
  {  2, "sShaker",     "Shaker" },
  {  3, "sUFlipHigh",  "Upper Flipper High" },
  {  4, "sVUK",        "VUK" },
  {  5, "sPost",       "Ball Stop (stop post, right inner orbit)" },
  {  6, "sRPop",       "Right Pop Bumper" },
  {  7, "sDrop",       "Drop Target (raise)" },
  {  8, "sUFlipLow",   "Upper Flipper Low" },
  {  9, "sLLPop",      "Lower Left Pop" },
  { 10, "sULPop",      "Upper Left Pop" },
  { 11, "sLUSling",    "Upper Left Sling" },
  { 12, "sLSling",     "Left Sling" },
  { 13, "sLFlipLow",   "Left Flipper Low" },
  { 14, "sLFlipHigh",  "Left Flipper High" },
  { 17, "sLaunch",     "Autoplunger" },
  { 18, "sLoad",       "Ball Load (trough)" },
  { 19, "sRFlipLow",   "Right Flipper Low" },
  { 20, "sRFlipHigh",  "Right Flipper High" },
  { 21, "sRSling",     "Right Sling" },
  { 22, "sRUSling",    "Upper Right Sling" },
  { 25, "sGI0",        "GI_0 (backbox)" },
  { 26, "sGI1",        "GI_1 (backbox)" },
  { 27, "sGI2",        "GI_2 (backbox)" },
  { 28, "sGI3",        "GI_3 (backbox)" },
  { 29, "sGI4",        "GI_4 (backbox)" },
  { 30, "sGI5",        "GI_5 (backbox)" },
  { 31, "sGI6",        "GI_6 (backbox)" },
  { 32, "sGI7",        "GI_7 (backbox)" },
  { 37, "sGI8",        "GI_8 (no wire on the chart)" },
  { 38, "sGI9",        "GI_9 bottom playfield GI 1" },
  { 39, "sGI10",       "GI_10 bottom playfield GI 2" },
  { 40, "sGI11",       "GI_11 red GI" },
  { 41, "sGI12",       "GI_12 purple flasher" },
  { 42, "sGI13",       "GI_13 red flasher" },
  { 43, "sGI14",       "GI_14 white GI" },
  { 44, "sGI15",       "GI_15 (no driver fitted)" },
  { 51, "sRGB1R",      "RGB1 red" },
  { 52, "sRGB1G",      "RGB1 green" },
  { 53, "sRGB1B",      "RGB1 blue" },
  { 54, "sRGB2R",      "RGB2 red" },
  { 55, "sRGB2G",      "RGB2 green" },
  { 56, "sRGB2B",      "RGB2 blue" },
  { 57, "sGate",       "Servo 0: Spaulding gate" },
  { 58, "sRobot",      "Servo 1: robot" },
  { 59, "sServo2",     "Servo 2" },
  { 60, "sServo3",     "Servo 3" },
  { 61, "sServo4",     "Servo 4" },
  { 62, "sLDGR",       "LDG light red" },
  { 63, "sLDGB",       "LDG light blue" },
  { 64, "sLDGG",       "LDG light green" },
  { 0 }
};
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


edit('tests/pinheck/vpx/names.py', [
    (r'''"""Domino's names (src/wpc/pinheck_names.h) against the driver's numbering, and the pinHeck system script for VPX.
  names.py check SRC     every name has a number the driver gives that device; dominos.c and pinheck.c agree
  names.py vbs SRC       pinheck.vbs, the system script a table loads with LoadVPM (to stdout)"""
import re
import sys

TABLES = ('switch', 'lamp', 'solenoid')


def read(src):
    text = open(src + '/wpc/pinheck_names.h').read()
    out = {}
    for t in TABLES:
        body = text.split('pinheck_dominos_%s_names[] = {' % t, 1)[1].split('{ 0 }', 1)[0]
''',
     r'''"""A game's names (src/wpc/pinheck_names.h) against the driver's numbering, and the pinHeck system script for VPX.
  names.py check SRC [GAME]   every name has a number the driver gives that device; GAME.c and pinheck.c agree
  names.py vbs SRC [GAME]     the system script a table loads with LoadVPM (to stdout): pinheck.vbs for dominos
GAME is dominos (the default) or rzspook."""
import re
import sys

TABLES = ('switch', 'lamp', 'solenoid')
TITLE = {'dominos': "Domino's Spectacular Pinball Adventure", 'rzspook': "Rob Zombie's Spookshow International"}
# the ids of servo 0 and the external LED's first channel
ROLES = {'dominos': ('sNoid', 'sExtR'), 'rzspook': ('sGate', 'sLDGR')}


def read(src, game='dominos'):
    text = open(src + '/wpc/pinheck_names.h').read()
    out = {}
    for t in TABLES:
        body = text.split('pinheck_%s_%s_names[] = {' % (game, t), 1)[1].split('{ 0 }', 1)[0]
'''),
    (r'''def check(src):
    names, fails = read(src), []
''',
     r'''def check(src, game='dominos'):
    names, fails = read(src, game), []
'''),
    (r'''    sim = dict((i, int(n)) for i, n in re.findall(r'#define (s\w+)\s+(\d+)', open(src + '/wpc/sims/pinheck/dominos.c').read()))
    for i, n in sim.items():
        if ids.get(i) != n:
            fails.append('dominos.c has %s = %d, pinheck_names.h %s' % (i, n, ids.get(i)))
''',
     r'''    sim = dict((i, int(n)) for i, n in re.findall(r'#define (s\w+)\s+(\d+)', open(src + '/wpc/sims/pinheck/%s.c' % game).read()))
    for i, n in sim.items():
        if ids.get(i) != n:
            fails.append('%s.c has %s = %d, pinheck_names.h %s' % (game, i, n, ids.get(i)))
'''),
    (r'''            'sNoid': define(drv, 'PINHECK_SOL_SRV') + 1, 'sExtR': define(drv, 'PINHECK_SOL_EXT') + 1,
''',
     r'''            ROLES[game][0]: define(drv, 'PINHECK_SOL_SRV') + 1, ROLES[game][1]: define(drv, 'PINHECK_SOL_EXT') + 1,
'''),
    (r'''    print('names: %d switches, %d lamps, %d solenoid outputs; %d sim constants agree' % (
        len(names['switch']), len(names['lamp']), len(names['solenoid']), len(sim)))
''',
     r'''    print('names %s: %d switches, %d lamps, %d solenoid outputs; %d sim constants agree' % (
        game, len(names['switch']), len(names['lamp']), len(names['solenoid']), len(sim)))
'''),
    (r'''' pinHeck (Spooky Pinball) system script: Domino's Spectacular Pinball Adventure. Generated by
''',
     r'''' pinHeck (Spooky Pinball) system script: %s. Generated by
'''),
    (r'''def vbs(src):
    names = read(src)
    out = [HEAD.lstrip('\n')]
''',
     r'''def vbs(src, game='dominos'):
    names = read(src, game)
    out = [HEAD.lstrip('\n') % TITLE[game]]
'''),
    (r'''    if len(sys.argv) != 3 or sys.argv[1] not in ('check', 'vbs'):
        sys.exit(__doc__)
    sys.exit((check if sys.argv[1] == 'check' else vbs)(sys.argv[2]))
''',
     r'''    if len(sys.argv) not in (3, 4) or sys.argv[1] not in ('check', 'vbs') or sys.argv[3:] and sys.argv[3] not in TITLE:
        sys.exit(__doc__)
    sys.exit((check if sys.argv[1] == 'check' else vbs)(*sys.argv[2:]))
'''),
])
edit('tests/pinheck/vpx/check.sh', [
    (r'''python3 names.py check $S || fail=$((fail + 1))
''',
     r'''python3 names.py check $S || fail=$((fail + 1))
python3 names.py check $S rzspook || fail=$((fail + 1))
'''),
    (r'''printf '#include "pinheck_names.h"\nint main(void) { return pinheck_dominos_switch_names[0].num != 1; }\n' > $B/names.c
''',
     r'''printf '#include "pinheck_names.h"\nint main(void) { return pinheck_dominos_switch_names[0].num != 1 || pinheck_rzspook_switch_names[0].num != 1; }\n' > $B/names.c
'''),
])
EOF
```

- [ ] **Step 3: The checks**

```sh
python3 tests/pinheck/vpx/names.py vbs src | cmp - build/pinheck-before.vbs && echo "pinheck.vbs: unchanged"
python3 tests/pinheck/vpx/names.py vbs src rzspook | grep -c '^Const'
nice -n 15 tests/pinheck/vpx/check.sh | grep -v selftest
```
Expected:
```text
pinheck.vbs: unchanged
152
names dominos: 46 switches, 48 lamps, 47 solenoid outputs; 39 sim constants agree
names rzspook: 53 switches, 49 lamps, 50 solenoid outputs; 53 sim constants agree
mingw: strcasecmp kept
builds: 10 build lists carry the same 34 pinHeck sources
scale: 2x2 dots in PinMAME and VPinMAME windows, the 128x32 panel as sent to libpinmame hosts
vpx: LIBPINMAME or PINMAME_ROMS not set: libpinmame's display list and getters not run
vpx: 0 failed
```
(the `mingw:` line only where `x86_64-w64-mingw32-gcc` is installed)

- [ ] **Step 4: Commit**

```bash
git add src/wpc/pinheck_names.h tests/pinheck/vpx
git commit -m "pinheck_names.h: Rob Zombie's switch, lamp and output names; names.py by game"
```

### Task 7: Speed

**Files:**
- Modify: `tests/pinheck/perf/bench.sh`

**Interfaces:**
- Consumes: Plan 9d's `bench.sh` (`attract`, `short`, `video`, `contention`, `REFERENCE`, `BENCH_MACHINE`, `BENCH_CPUS`, `BENCH_THROTTLE`).
- Produces: `bench.sh` by `PINHECK_GAME`; for `rzspook` the workloads `attract`, `video` (`[V00AT9]` and five of its sounds) and `play` (coin, start, plunge, a shot every 1.5 s from 20 s, timed from 25 s for 55 s), its runs under `tests/pinheck/perf/build/rzspook`.

- [ ] **Step 1: Workloads by game**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/perf/bench.sh', [
    (r'''#                                  and the worker must stay on
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
''',
     r'''#                                  and the worker must stay on
# PINHECK_GAME selects the game (tests/pinheck/games.sh); rzspook has attract, video and play (a game started and
# played from the simulator's keys), each in build/rzspook
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to the game's romset zip}"
'''),
    (r'''cd "$(dirname "$0")" || exit 2
B=$PWD/build
''',
     r'''cd "$(dirname "$0")" || exit 2
. ../games.sh
B=$PWD/build
[ $GAME = dominos ] || B=$PWD/build/$GAME
'''),
    (r'''# workload: frames to run, frame the timed window starts at, UART1 commands
spec() {
	case $1 in
	attract) FRAMES=2400 MARK=600 SEND_AT=0 SEND= ;;
	short) FRAMES=1200 MARK=600 SEND_AT=0 SEND= ;;
	video) FRAMES=2400 MARK=960 SEND_AT=10 SEND='[E96000]~[V00AT9]~[F00ZM0]~[F00BWI]~[F00IF0]~[F00NBI]~~~~~~~~~~[F00NBI]~~~~[F00IF0]' ;;
	*) echo "bench: unknown workload $1"; exit 2 ;;
	esac
}
''',
     r'''# play: coin, start, plunge, then a shot every 1.5 s from 20 s on (none to the VUK or the lock, which hold the ball);
# the ball never drains
play_keys() {
	printf '600 tap 6 KEYCODE_5\n660 tap 6 KEYCODE_1\n960 tap 40 KEYCODE_SPACE\n'
	i=0
	for k in B 'LCONTROL R' 'RCONTROL R' 'LCONTROL MINUS' 'RCONTROL MINUS' C 'LCONTROL L' 'RCONTROL L' H J G E X A T \
		'LCONTROL B' 'RCONTROL B' 'LCONTROL N' 'LCONTROL EQUALS' 'RCONTROL EQUALS' B 'LCONTROL R' 'RCONTROL R' 'LCONTROL MINUS' \
		'RCONTROL MINUS' C 'LCONTROL L' 'RCONTROL L' H J G E X A T 'LCONTROL B' 'RCONTROL B' 'LCONTROL N' 'LCONTROL EQUALS'; do
		printf '%d tap 1 KEYCODE_%s\n' $((1200 + 90 * i)) "$(echo "$k" | sed 's/ / KEYCODE_/')"
		i=$((i + 1))
	done
}

# workload: frames to run, frame the timed window starts at, UART1 commands, keys
spec() {
	KEYS=
	case $GAME/$1 in
	*/attract) FRAMES=2400 MARK=600 SEND_AT=0 SEND= ;;
	*/short) FRAMES=1200 MARK=600 SEND_AT=0 SEND= ;;
	dominos/video) FRAMES=2400 MARK=960 SEND_AT=10 SEND='[E96000]~[V00AT9]~[F00ZM0]~[F00BWI]~[F00IF0]~[F00NBI]~~~~~~~~~~[F00NBI]~~~~[F00IF0]' ;;
	rzspook/video) FRAMES=2400 MARK=960 SEND_AT=10 SEND='[E96000]~[V00AT9]~[F00ZM0]~[F00ZAA]~[F00ZAB]~[F00ZAC]~~~~~~~~~~[F00ZAD]~~~~[F00ZAE]' ;;
	rzspook/play) FRAMES=4800 MARK=1500 SEND_AT=10 SEND='[E97000]' KEYS=$(play_keys) ;;
	*) echo "bench: no workload $1 for $GAME"; exit 2 ;;
	esac
}
'''),
    (r'''	[ -s $M/nvram.base/dominos.nv ] && [ "$(cat $M/binary.sha 2> /dev/null)" = "$sum" ] && return
''',
     r'''	[ -s $M/nvram.base/$GAME.nv ] && [ "$(cat $M/binary.sha 2> /dev/null)" = "$sum" ] && return
'''),
    (r'''	ln -s "$PINHECK_ZIP" $M/roms/dominos.zip || exit 2
	(cd $M && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 timeout -k 30 3000 "$2" dominos -rompath roms \
''',
     r'''	ln -s "$PINHECK_ZIP" $M/roms/$GAME.zip || exit 2
	(cd $M && LD_PRELOAD=$B/fixtime.so PINHECK_FIXTIME=$FIXTIME PINHECK_INSERVICE=6 timeout -k 30 3000 "$2" $GAME -rompath roms \
'''),
    (r'''	key="$FRAMES $MARK $SEND_AT $SEND"
''',
     r'''	key="$FRAMES $MARK $SEND_AT $SEND $KEYS"
'''),
    (r'''	echo "$MARK mark window" > $D/keys.txt
''',
     r'''	{ echo "$MARK mark window"; [ -z "$KEYS" ] || echo "$KEYS"; } > $D/keys.txt
'''),
    (r'''		timeout -k 30 5400 $wrap "$bin" dominos -rompath $B/$ml/roms''',
     r'''		timeout -k 30 5400 $wrap "$bin" $GAME -rompath $B/$ml/roms'''),
    (r'''		for f in uart.log frames.bin snd.wav nvram/dominos.nv; do
			cmp -s $B/$m/short/$f''',
     r'''		for f in uart.log frames.bin snd.wav nvram/$GAME.nv; do
			cmp -s $B/$m/short/$f'''),
    (r'''	if cmp -s $B/opt/nvram.base/dominos.nv $B/ref/nvram.base/dominos.nv; then echo "determinism first boot: identical nvram.base/dominos.nv"
	else echo "DETERMINISM FAIL first boot: nvram.base/dominos.nv differs from the reference build's"; fail=1; fi
''',
     r'''	if cmp -s $B/opt/nvram.base/$GAME.nv $B/ref/nvram.base/$GAME.nv; then echo "determinism first boot: identical nvram.base/$GAME.nv"
	else echo "DETERMINISM FAIL first boot: nvram.base/$GAME.nv differs from the reference build's"; fail=1; fi
'''),
    (r'''	for f in uart.log frames.bin snd.wav nvram/dominos.nv; do
		if cmp -s''',
     r'''	for f in uart.log frames.bin snd.wav nvram/$GAME.nv; do
		if cmp -s'''),
])
EOF
```

- [ ] **Step 2: Rob Zombie's speed, Domino's beside it, and Domino's still identical**

```sh
rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video play
REFERENCE=build/reference/sdl3pinmame SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 9000 tests/pinheck/perf/bench.sh attract video
```
Expected (about 2 minutes each; speeds vary with the load):
```text
bench attract opt: 1.438x over 30.0 s emulated (20.9 s wall), whole run 11.4 G instructions and 1.0 s CPU per emulated s, worst 100 ms 1.114x, load 1.74
bench video opt: 1.235x over 24.0 s emulated (19.4 s wall), whole run 12.4 G instructions and 1.1 s CPU per emulated s, worst 100 ms 0.484x, load 1.66
bench play opt: 1.107x over 55.0 s emulated (49.7 s wall), whole run 14.1 G instructions and 1.3 s CPU per emulated s, worst 100 ms 0.451x, load 1.64
determinism first boot: identical nvram.base/dominos.nv
bench attract opt: 1.279x over 30.0 s emulated (23.4 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 1.057x, load 1.81
bench attract ref: 1.292x over 30.0 s emulated (23.2 s wall), whole run 12.6 G instructions and 1.1 s CPU per emulated s, worst 100 ms 1.105x, load 1.73
determinism attract: identical uart.log frames.bin snd.wav nvram/dominos.nv
bench video opt: 1.122x over 24.0 s emulated (21.4 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.974x, load 1.56
bench video ref: 1.130x over 24.0 s emulated (21.2 s wall), whole run 13.6 G instructions and 1.2 s CPU per emulated s, worst 100 ms 0.975x, load 1.41
determinism video: identical uart.log frames.bin snd.wav nvram/dominos.nv
```

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/perf/bench.sh
git commit -m "pinheck bench: workloads by game; Rob Zombie's attract, video and play"
```

### Task 8: Regression, the tests bite, the roadmap

**Files:**
- Modify: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`

- [ ] **Step 1: Domino's scripted game and the remaining suites**

```sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/game/pinmame_game.sh | tail -6
PINHECK_SKIP_FIRMWARE=1 PINHECK_UPDATE_DIR= nice -n 15 tests/pinheck/link/check.sh | tail -1
```
Expected (about 5 minutes):
```text
game: 3 balls, 4 trough ejects, 1 autolaunches, final score 32176700
game: high score 1 ACE, match NA1
game: 148 clips started, 87 shown pixel-exact
game: 44 checks, 0 failures
pinmame game: ok
link: 0 failed
```

- [ ] **Step 2: The tests bite**

Apply each mutation to `src/wpc/sims/pinheck/rzspook.c`, rebuild (`cmake --build build/sdl3pinmame -j6`, about a minute), run the named check with `rz`, confirm the named failure line (other failure lines may follow), then `git checkout src/wpc/sims/pinheck/rzspook.c`. Rebuild once more at the end.

Each with a helper that asserts the anchor exists once:

```sh
F=src/wpc/sims/pinheck/rzspook.c
mut() { python3 -c "import sys; p='$F'; s=open(p).read(); a,b=sys.argv[1],sys.argv[2]; assert s.count(a)==1; open(p,'w').write(s.replace(a,b))" "$1" "$2" || return
	flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout -k 30 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3pinmame/11a.log 2>&1; tail -1 build/sdl3pinmame/11a.log
	shift 2; rz env SDL3PINMAME=build/sdl3pinmame/sdl3pinmame flock /code/spooky_domino/work/emu.lock nice -n 15 ionice -c 3 timeout -k 30 3300 "$@" 2>&1 | grep -m3 FAIL; git checkout $F; }
```

| Mutation | Command | Caught by |
|---|---|---|
| `aligned`: the look aligned at Domino's 340 | `mut '  128, 32, 460, 544, 2400, 0, 1' '  128, 32, 340, 544, 2400, 0, 1' tests/pinheck/display/pinmame_look.sh` | `LOOK FAIL: rzspook.png shows none of the last 8 frames in the look ROUND brightness 255 position 460 bar 62` |
| `servo`: Domino's servo range | `mut '  128, 32, 460, 544, 2400, 0, 1' '  128, 32, 460, 1000, 2000, 0, 1' tests/pinheck/board/pinmame_board.sh` | `BOARD FAIL: servo output 57 is 255, expected 231` |
| `lock`: the stop post never holds | `mut '    case stRailLow:  if (sol(sPost)) return setState(stRInlane, 5); break;' '    case stRailLow:  return setState(stRInlane, 5);' tests/pinheck/sim/pinmame_sim.sh` | `SIM FAIL: lock: the ball behind the dropped target did not come to rest on Rail Lower (35 [21.183333338], 34 [21.5, 21.6], 33 [21.816666663, 21.916666663, 24.616666663, 24.716666663])` |

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
     r'''| `2026-10-01-pinheck-11a-rzspook.md` | 11a: per-game data in the driver (`pinheck_tGameData`: display size, aligned `POSITION`, servo range, WS2801 polarity, `PINHECK_INSERVICE`); Rob Zombie's Spookshow International (`rzspook`): game, simulator, names; machine checks by game (`PINHECK_GAME`) | Plan 9d | none | `rzspook` passes the boot (first-boot update and restart), simulator, service-test, display and look checks; Domino's byte-identical (`bench.sh` with `REFERENCE`, the scripted game's score 32176700) | written |
| release | release:'''),
    (r'''- After Plan 9d: a profile-guided build''',
     r'''- After Plan 11a: `rzspook` runs at 1.44× attract, 1.24× video and 1.11× in play (`bench.sh` without a profile, load 1.6–1.7; slowest 100 ms 1.11×, 0.48×, 0.45×), Domino's in the same session 1.28× and 1.12×; the scripted Rob Zombie game is open (Plan 11a Ruling 11).
- After Plan 9d: a profile-guided build'''),
])
s = open('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md').read()
s = s.rstrip('\n') + r'''

## Carried into Plans 11b and 11c

- `pinheck_tGameData` (`src/wpc/pinheck.h`) carries what the Jetsons and America's Most Haunted need, with Domino's values in `PINHECK_GAME_DEFAULTS`: `width`/`height` (Jetsons 128×64: `display.c`'s frame size, the look, the libpinmame export and the display checks), `rgbInverted` (AMH: `board.c` inverts the WS2801 lines), `inService` (AMH 0: it has no update record) and `servoMin`/`servoMax` (Jetsons 544–2,400 µs, as `rzspook`). The driver reads `aligned`, the servo range and `inService`; the other fields wait for those plans.
- The machine checks take `PINHECK_GAME` (`tests/pinheck/games.sh`, and the per-game parts of `check5.py`, `look.py`, `bench.sh` and `names.py`); a game adds its line there, its simulator check, and its names section.
'''
open('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', 'w').write(s)
EOF
```

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "docs: roadmap row for Plan 11a and what it carries into 11b/11c"
```

## Windows checklist

For the Windows session (VS2022 BuildTools / MSVC 14.44, `C:\code\dominos`), after Plan 11a is pushed. Results come back by message; a Windows-only fix comes back as a diff with a test, applied on Linux. Never push. Never play sound on a device the user is listening to unless the user asks; muted or the silent device is fine for every item but 6.

Setup: `roms\rzspook.zip` = `C:\code\spooky\Rob Zombie’s Spookshow\rzupdate_V26.zip` copied under that name (526,373,675 bytes; not renamed in place), `roms\pinheck.zip` as before. A fresh `nvram\` and `cfg\` directory for item 2 (PinMAME on Windows does not create them).

1. **Build.** MSVC x64 standalone PinMAME, VPinMAME64 and libpinmame at the Plan 11a commit, exactly as CI does. Expected: all build; no warning in `rzspook.c`, `dominos.c`, `pinheck.c`, `pinheckgames.c`; `PinMAME.exe -listfull rzspook` lists `rzspook` as "Rob Zombie's Spookshow International".
2. **First boot.** `PinMAME.exe rzspook` with the empty `nvram\`. Expected, as on Domino's but longer: the update screens (`FLASHING TO:`, the sector count) for about three minutes of emulated time, then `CODE UPDATE COMPLETE` / `PLEASE RESTART`; close and start again: the PIC32 stores its defaults (`System has been updated` / `Please restart your machine` or a blank stop); close and start a third time: attract mode with Rob Zombie's clips and sound. With `PINHECK_UART1_LOG=uart.log` the third launch logs `1A00BAFA` and `Game: RZO - SPOOK SHOW` (between EEPROM trace lines) and `Version: ` with nothing after it (the firmware's).
3. **The look.** In attract mode the picture fills the window from the top, not shifted down by half (the factory `POSITION` 460 is drawn aligned). In the service menu (`0`, then Right Shift to `MAIN SETTINGS`, `0`, ten Right Shifts) `PIXEL SHAPE`, `BRIGHTNESS`, `POSITION` (460) and `BAR BRIGHT` change the window as on Domino's; `POSITION` 464 moves the picture one row of the window down, 456 one row up. Screenshot at 460 and at 480.
4. **Service tests.** `SOLENOID TEST` opens at `AUTOPLUNGER`; `0` fires each coil (the core's solenoid row shows it); `SERVO TEST` `GATE OPEN`/`GATE CLOSE` and `ROBOT START`/`ROBOT END`; `LAMP TEST` steps through the GI and all 64 lamp positions (48 lamps on the lamp chart); `RGB TEST` ends with `LDG RED/GREEN/BLUE/WHITE` and `SWAP G <-> B: NO`; `SWITCH TEST` after `Del` (simulator keys off) shows trough 1–7 closed and reacts to the column/row keys (`Q`–`I` × `A`–`K`). Back with `7`.
5. **Keyboard play.** `5` coin, `1` start, hold and release `Space`: the ball leaves the shooter lane; `B`, `Ctrl`+`R`, `C`, `G`, `H`, `J`, `L/R Ctrl`+`L` score; `Q` drains; balls 2 and 3 are served. Report anything the firmware complains about on the display (`MISSING BALLS`, ball search).
6. **Sound** (unmuted, if the user agrees): music and call-outs in attract mode and in the game; no drift over a minute.
7. **VPinMAME smoke test** (Route A of Milestone 10, `[Plugin.PinMAME] Enable = 0`): the Domino's test table copied to `rzspook_test.vpx` with `Const cGameName = "rzspook"` in its script (VPX script editor), `roms\rzspook.zip` in VPinMAME's ROM folder, `nvram\` there emptied first. Expected: the three launches of item 2 (VPinMAME restarts with the table), then the VPinMAME DMD window shows the display crisp at 2×2 per dot and aligned, sound plays, the table's lamp indicators follow the attract lamp show (their labels are Domino's; the numbers 11–68 and 91 are the same matrix), `table.log` has `route com`. The table's switch keys are Domino's and are not part of this test. For the names, `python tests\pinheck\vpx\names.py vbs src rzspook > rzspook.vbs` gives Rob Zombie's constants for a table of its own.

## Result

Measured in the replay on the Ryzen 7 7730U while other agents' emulators and builds shared it (one emulator at a time, `emu.lock`); builds without a profile; the load average is printed with every speed.

| | attract (load) | video (load) | play (load) | G host instructions per emulated s, attract / video / play |
|---|---|---|---|---|
| `rzspook` | 1.438×, slowest 100 ms 1.11× (1.7) | 1.235×, slowest 100 ms 0.48× (1.7) | 1.107×, slowest 100 ms 0.45× (1.6) | 11.4 / 12.4 / 14.1 |
| `dominos`, same session | 1.279×, slowest 100 ms 1.06× (1.8) | 1.122×, slowest 100 ms 0.97× (1.6) | — | 12.6 / 13.6 |
| `dominos`, the reference (`964122ec`) | 1.292× (1.7) | 1.130× (1.4) | — | 12.6 / 13.6 |

Domino's is byte-identical to the reference in UART1, frames, sound and NVRAM (attract mode and video, Tasks 1 and 7) and its scripted game ends at 32176700 as before (Task 8). Rob Zombie needs fewer host instructions than Domino's in attract mode and with video; its play workload (one ball, a shot every 1.5 s, clips and call-outs) is the heaviest of the three and stays above real time on average, with slowest 100 ms windows at 0.45×. Its 7-ball multiball is not reached (Ruling 11).

Rob Zombie's first boot: the update flashes 309,248 bytes and shows `PLEASE RESTART` at 181.2 s of emulated time (Domino's about 135 s); with the restart and the defaults its boot check took about 2 minutes of wall time with `-nothrottle` (Task 2).

Exploratory findings carried forward: at mode start the VUK holds the ball until a mode is chosen (Ruling 11); after the lock the firmware shakes the cabinet and releases the ball 12 s later with the drop target reset (Ruling 7); `Start` during ball 1 adds players.

## Self-Review

**Scope coverage** (the brief of this plan):

| Requirement | Where |
|---|---|
| per-game parameters (display size, aligned `POSITION`, servo range, RGB polarity, `PINHECK_INSERVICE`, version) in a form that carries the Jetsons and AMH | Task 1, Rulings 1–2; the roadmap's section for Plans 11b/11c (Task 8) |
| implement and test what Rob Zombie uses; Domino's byte-identical | aligned `POSITION` (Ruling 3, the look checks of Task 5, mutation `aligned`), servo range (Ruling 4, check 5 of Task 4, mutation `servo`), `inService` (every check's first boot in service); Domino's: `bench.sh` with `REFERENCE` (Tasks 1 and 7), its boot, simulator, check 5, display, look and scripted-game checks unchanged in output (Tasks 2–5, 8) |
| `rzspook` game: ROMs, CRCs, input ports, game data | Task 2 (`rzspook.c`), Ruling 5 |
| simulator: 7-ball trough, VUK, drop target with the stop-post lock, upper playfield with the gate optos, pops, slings, upper flipper, servo 0 (gate) and 1 (robot) | Task 2 (`rzspook.c`), Ruling 7; Task 3's check, mutation `lock` |
| registered in every build list | Task 2 Step 3 (`register_build.py`: `pinmame.mak`, 10 cmake lists, 4 vcxproj and their filters); `pic32mx/check.sh` (makefile link, symbols, vcxproj) and `vpx/check.sh` (the build lists agree) |
| names and the VBS generator | Task 6, Ruling 13 |
| checks: boot (first-boot update and restart, attract with the banner), service menu (switch, lamp, coil tests against the PDFs), simulator, display frames against `.VID` clips | Tasks 2, 4, 3, 5 |
| a scripted game | not in this plan (Ruling 11) |
| speed of attract and play, measured once | Task 7, the Result, Ruling 12 |
| Windows checklist | the section above |

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay (speeds as measured, with the load).

**Type consistency:** `pinheck_tGameData` is defined once in `pinheck.h` and initialised positionally in `pinheckgames.c` (`INIT_PINHECK`), `dominos.c` and `rzspook.c` (`{ core }, width, height, aligned, servoMin, servoMax, rgbInverted, inService`); `core_gameData` is `&…GameData.core` in all three. `pinheck_game()` is static in `pinheck.c` and used by the servo callback, the config callback and the NVRAM handler. `rzspook.c`'s constants are the ids `names.py check src rzspook` compares. `games.sh` sets `GAME`, `PRG`, `PROGRAMMED`, `UPDATED_AT`, `STORED`, `BANNER`, `CLIP` and `SIM`, used by `pinmame_check.sh`, `romset_check.sh`, `pinmame_sim.sh`, `pinmame_board.sh`, `pinmame_display.sh`, `pinmame_look.sh` and `bench.sh`.

**Review Focus:** five items, each pinned by the named checks and the mutations of Task 8.

**Proof:** every file and edit this plan writes was extracted from this document (its edit scripts and the three new files) into a fresh worktree of `pinheck` at `964122ec`, task by task, and the tree it produced is byte-identical to the proven prototype's (with the roadmap edit); every Expected output above was reproduced there on a machine shared with other agents' work. Each mutation of Task 8 was caught with the named line, and the tree was clean afterwards.
