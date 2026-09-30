# Milestone 10: VPX support (libpinmame export proof, names, the test table) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Domino's reaches Visual Pinball X: through libpinmame (VPX standalone's PinMAME plugin) the display frames, the sound, the lamps, solenoids, GI, RGB and servo outputs equal the driver's own, headless on Linux; `src/wpc/pinheck_names.h` names every switch, lamp and output; a minimal test table, built from text outside the repository, runs in VPX standalone on Linux with its data paths checked headless, and comes with a manual checklist for Linux and one for Windows VPX with VPinMAME.

**Architecture:** Three fixes in generic code, each with a test on games other than Domino's: libpinmame announces a second display for every DMD or video game that never gets a frame (the extra slot is for segment-only games), `PinmameGetLamp` brings the wrong output up to date for games with their own lamp numbering and indexes outside the lamp matrix for numbers no game has, and `PinmameGetSolenoid` does the same for solenoids. The pinHeck driver sends libpinmame the 128×32 panel as the firmware sent it (the 2×2 window and the display look stay in PinMAME and VPinMAME), rejects lamp and switch numbers the matrix does not have, lets `HandleMechanics` switch off the simulated Noid and target bank, lists GI 8–15 among the plugin's outputs, closes its link log when the machine stops and resets the link decoder with the machine, and no longer clears the core's solenoid log in the window. A test host drives libpinmame through its callback API and through the plugin message API VPX uses and compares what it receives with the driver's frame, sound and output logs. The test table, its script and the system script `pinheck.vbs` are generated from `pinheck_names.h`; a headless VPX standalone run on Linux (Xvfb) checks lamps, outputs and keys against the driver's log.

**Tech Stack:** C (the driver, C89-syntax-clean as the pinHeck code is), C++20 (libpinmame, the test host), Python 3 (no third-party modules in the repository), POSIX sh; outside the repository: VPX standalone (VPinballX_GL linux-x64), vpxtool 0.34.8, ImageMagick 7, Xvfb, xdotool, xwd.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m10-vpx-design.md` (binding), with the parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` and the addenda `2026-09-29-pinheck-m6-m7-display-audio-design.md` and `2026-09-29-pinheck-m8-m9-design.md` (binding where the M10 addendum is silent).

## Prerequisites

- Base: `pinheck` at `29692743` (pushed as `feature/pinheck-dominos`); the later `c9d1862a` (an ARM yield in `prop.c` and a link test) touches none of this plan's files. The spec's precondition (Milestone 9: real time) is met there: `dominos` runs at 1.28× in attract mode and 1.08× with video on the reference machine (Plans 9b and 9c, measured at load 1.7).
- This plan was proven by prototyping on branch `pinheck-m10-proto2` and replayed from its own text on a fresh worktree of `29692743`; every Expected block below is the replay's output. The replay's diff is `/code/spooky_domino/work/m10/m10-replay.patch`.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip; and `PINMAME_ROMS` = a folder holding `pb_l5.zip`, `tz_94h.zip` and `babypac.zip` (a segment, a DMD and a video game; on the reference machine `/code/roms/_flat`). On the reference machine `. /code/spooky_domino/work/m8a/env.sh` sets the first three.
- The reference machine is shared: builds run niced under `flock /code/spooky_domino/work/heavy.lock` with at most `-j6` (on another machine drop the `flock` prefix); every PinMAME, libpinmame and VPX run has a `timeout -k`; never more than three emulators at once.
- Task 5 needs, outside the repository in `/code/spooky_domino/vpx/tools/`: an unpacked VPinballX_GL linux-x64 build from after 2026-09-05 (Ruling 13; the replay used `10.8.1-5947-4dc8d8afb` from vpinball's CI) and vpxtool 0.34.8; and on the machine `xvfb-run`, `xwd`, `xdotool`, `magick`, `zip`.

## Global Constraints

- The pinHeck driver code stays C89-syntax-clean and warning-free; code comments short and factual.
- Every existing suite stays green; this plan touches the driver, so the machine checks `display/pinmame_display.sh`, `display/pinmame_look.sh`, `board/pinmame_board.sh`, `sim/pinmame_sim.sh` and `game/pinmame_game.sh` are rerun on the result (Task 6).
- Test hooks stay inert unless set. The standalone PinMAME window, its 256×64 panel and the display look do not change.
- Every sdl3pinmame launch uses private `-nvram_directory` and `-cfg_directory` and a `timeout -k 30`; every VPX run uses a private `HOME` and `-PrefPath` (VPX writes its settings under `~/.local/share/VPinballX` otherwise).
- No Spooky firmware, media or ROM bytes and no Propeller mask ROM in the repository; the checks take them from environment variables. The test table and its files live outside the repository, in `/code/spooky_domino/vpx/` (spec §4.5).
- A missing export is a defect in the driver or the core, fixed there with a test, never worked around in the table script (spec §5).

## Findings this plan rests on

- **The phantom display.** `OnStateChange` in `src/libpinmame/libpinmame.cpp` adds a display slot unconditionally after counting the layouts. Only segment-only games fill it (the 128×32 DMD rendered from their segments); a DMD or video game announces `count` 2 and never updates index 1. Verified on Windows (`work/windows/from-windows/taskB-libpinmame-displaycount.diff`) and here with `tz_94h` and `babypac`; `pb_l5` (eight segment displays) needs the slot.
- **The lamp getter.** `PinmameGetLamp(n)` brought output `CORE_MODOUT_LAMP0 + n - 1` up to date, which is lamp `n`'s output only for games without their own lamp numbering; `vp_getLamp` then indexes `lampMatrix` and `physicOutputState` with whatever `lamp2m` returns, unchecked: pinHeck's `lamp2m` (the switch conversion) maps 10 to −1 (a shift by −1) and 19 to lamp 21's index, and a game without `lamp2m` indexes lamp 100000 far outside the matrix.
- **The solenoid getter.** `PinmameGetSolenoid(0)` or `(-1)` updates output −1 or −2; for 29–48 `core_getSol` reads other outputs than `solNo − 1` (pinHeck's GI 8–15, 37–44, are outputs 40–47).
- **The plugin's output list.** `SetupMsgApiGameStates` lists 37–44 only for S11, SAM and SPA; pinHeck's GI 8–15 are stored there too (`solenoids2` bits 8–15, outputs 40–47).
- **The frame libpinmame hosts get.** Since the 2×2 window, libpinmame exported a 256×64 frame of doubled dots. The Windows worker's diff (`taskA-pinheck-libpinmame-native.diff`) sets `PINHECK_VIDEO_SCALE` to 1 under `LIBPINMAME`; the window code already draws the look only when the scale is 2, so libpinmame gets the exact panel. VPinMAME keeps 256×64 (Windows: crisp, `DmdWidth`/`DmdHeight` 256×64).
- **Flipper buttons.** `core_updateSw` copies PinMAME's flipper column (switches 111–118) to the game's flipper switches (`FLIP_SWNO(4, 3)`) every frame, so a table that sets switch 4 or 3 directly is overwritten; it must press 114 (lower left) and 112 (lower right).
- **VPX's PinMAME plugin.** libpinmame's plugin messages carry a version (`GetStateSrc:1`, upstream `3215a8ea`, 2026-09-05). The plugin of the VPX 10.8.1-5436 release asks for `GetStateSrc`, so it sees no lamps, solenoids or display with this libpinmame; a VPX master build (`10.8.1-5947`) asks for `GetStateSrc:1` and works.
- **VPX on Linux, headless.** VPinballX_GL runs under `xvfb-run` with software rendering and quits cleanly on SIGINT; `xdotool` keys reach the table's key events; the table script can log through `Scripting.FileSystemObject` to `GetCustomParam(1)` (`-c1 FILE`); `GameTime` runs ahead of the emulated time by 1–9 s while software rendering takes the CPU. `P` pauses VPX. With the B2S legacy plugin on, reading `Controller.Version` in the table script crashes VPX.

## Rulings

Decisions the spec left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it).

1. **The earlier prototype.** Branch `pinheck-m10-proto` (`3437c6ca`, on `d8ddb9e9`) was cherry-picked onto `29692743` without conflicts and reviewed. Kept: the display-count gate, the getters' range checks, `vp_getLampIndex`, the scale switch, the mech mask, the 37–44 listing, the names header and the test host with its checks. Changed: `check.sh` resolved its paths after `cd` (fixed); lamp numbers that name nothing now give −1 instead of an unlit spare lamp (Ruling 6); the letter lamps `lO`, `lV`, `lE`, `lN` became `lLetterO` … (`lV` collided with the test script's `Lv`: VBScript ignores case, and the collision stopped `pinheck.vbs` from loading); `host.cpp` gained `<cstdarg>` and a second session (`-R`); the table generator and script were rewritten (the prototype's table crashed VPX through `Controller.Version` and never received an output, Ruling 13). Cost if wrong: none; every kept part is under a test of this plan.
2. **The display count (generic).** Gate the extra slot on `!hasDMDOrVideo`, as the Windows diff does. Tested on `tz_94h` (DMD) and `babypac` (video): one display each, updated; on `pb_l5` (segments only): nine, all updated. These ROMs are not in the repository: `check.sh` takes them from `PINMAME_ROMS` and skips that part with a message when it is not set. The change is generic upstream code and a candidate for its own upstream pull request (the user decides). Cost if wrong: a host that relied on the phantom index; none found.
3. **The getters (generic).** `PinmameGetLamp` asks `vp_getLampIndex` (new in `vpintf.c`: the lamp's matrix index after `lamp2m`, −1 outside `0 … CORE_MODOUT_LAMP_MAX − 1`) and brings that output up to date; `vp_getLamp` returns 0 for −1. `PinmameGetSolenoid` returns 0 outside 1 … `CORE_MODOUT_SOL_MAX` and brings all solenoid outputs up to date for 29–48, where `core_getSol` reads other outputs. Cost if wrong: a game whose `lamp2m` returns indexes past `CORE_MODOUT_LAMP_MAX` on purpose; none exists.
4. **libpinmame gets the panel as sent.** `PINHECK_VIDEO_SCALE` is 1 under `LIBPINMAME` (the Windows diff); the look (dot shape, brightness, position) is a window rendering and stays out of libpinmame, whose hosts draw their own dots. Cost if wrong: a host that wanted the 2×2 look; it can scale itself.
5. **Frames are compared as libpinmame packs them.** pinHeck's screen is 15 bpp `VIDEO_RGB_DIRECT`, so libpinmame packs frames as RGB565 (depth 16); `vpx.py` converts each decoded RGB332 frame byte through the driver's 15-bit pen to 5.6.5 and requires every frame a host receives, through the callback and through the plugin's `GetRenderFrame`, to equal one of the driver's decoded frames, in order.
6. **Numbers that name nothing.** A switch number outside rows 1–8 of columns 0–11 goes to column 15 row 8, a slot nothing reads (the generic `core_setSw`/`core_getSw` have no range check, and a spare slot keeps them harmless); a lamp number outside rows 1–8 of columns 0–9 gives −1, which `vp_getLamp` rejects. Lamps 1–8 and 92–98 keep their indexes (the core's matrix validation requires column 0 to invert, and 92–98 are within the driver's 72 lamps); they read 0. Cost if wrong: a table using those numbers sees dark lamps, which is what the machine has.
7. **Flipper switches: table-side numbering.** The driver keeps `FLIP_SWNO(4, 3)`: the core's copy of the flipper column is what makes PinMAME's own flipper keys work. `pinheck_names.h` and `pinheck.vbs` name 114 `swLLFlip` and 112 `swLRFlip`, and `pinheck.vbs`'s key handlers press them. Cost if wrong: a table that sets 4 or 3 itself is overwritten each frame, as on every core game with `FLIP_SWNO`.
8. **`HandleMechanics` bits.** Bit 0: the Noid (servo 0 turns it, it closes Noid Home 58); bit 1: the target bank (servo 1). The coil timers the ball simulator reads run in every case. Standalone PinMAME passes `0xFF`, so nothing changes there. Tested by the service menu's servo test with mask 1 and 2: Noid Home toggles only with bit 0. The bank bit has no separate test (it reads the same mask). Cost if wrong: a table author who numbers mechs differently; the header's comment documents the bits.
9. **The names header.** `src/wpc/pinheck_names.h` follows `p2k_names.h`'s form (static tables of `{ number, id, name }`) and is not compiled into the driver: VPinMAME has no name lookup, and the consumers are `names.py` (which checks it against the driver's numbering and the simulator's constants, and generates `pinheck.vbs`) and table scripts. Names and numbers come from `Dominos-Switch-Matrix.pdf`, `Dominos-Lamp-Matrix.pdf` and `Spooky_Pinball_Domino's_Solenoid_List.pdf` (Ken Layton, 2019) with Plan 8a's numbering; the two optos' names come from the simulator (ramp and oven ramp). Ids are unique ignoring case, as VBScript requires.
10. **The link log and the link decoder.** The log (`PINHECK_LINK_LOG`) is closed at `MACHINE_STOP` and reopened for appending when the machine starts again in the same process; the packet decoder's state is cleared at `MACHINE_RESET`. Tested with two libpinmame sessions in one process, as VPX's reset key (`Controller.Stop`, `.Run`) does: at the first session's end the log holds all its packets, and the second session's first 40 packets equal the first session's. That second check does not catch a missing decoder reset: the first session ends between packets (a packet takes about 50 µs of PIC32 time, and neither a host nor `PINHECK_RESET_AT`, which acts on a 1 ms timer, can stop a session inside one), so the reset is a one-line state clear under review only. Cost if wrong: none for play (test hook).
11. **The core's solenoid log.** `pinheck_video` cleared the whole cliprect each frame, which wiped the solenoid numbers the core writes at the panel's top right when a solenoid switches on (`core_updateSw`). The panel code draws every pixel of the layout (the look renders black where it moves the picture), and the rest of the screen is the core's, so the clear goes, as in Baby Pac-Man's driver. Tested by a snapshot at the end of the display check's second launch, when attract mode has switched GI on and off. Cost if wrong: stale core drawing outside the panel; the core redraws its panel every frame.
12. **`W`/`P` lines keep their time.** `pinheck_brd_vblank` logs at each vblank before `core_updateSw` runs the simulator, so a switch the simulator sets at vblank k is logged at vblank k+1; `P` lines hold the outputs integrated up to the vblank they are stamped with. Every consumer (`check5.py`, `sim.py`, `game.py`, `vpx.py`) allows at least a frame. Moving the log after `core_updateSw` would shift every simulator and game log by a frame for no test that needs it. Cost if wrong: one frame (16.7 ms) of timing in test logs.
13. **VPX standalone build.** The Linux proof uses a VPX master build from vpinball's CI (`VPinballX_GL-10.8.1-5947-4dc8d8afb-linux-x64-Release`, run 36695732862), because the latest release predates versioned plugin messages. Users of VPX standalone need a build from after 2026-09-05 (spec §8 records it). Cost if wrong: none for the code; the table and checks are the same with a later release.
14. **Producing the `.vpx`.** A `.vpx` is a binary OLE file. `make_table.py` builds it from text: `vpxtool new`, `extract`, generated JSON parts (a label flasher, a display flasher, 97 light indicators, three timers), a label image drawn with ImageMagick from `pinheck_names.h`, the script `dominos_test.vbs`, then `vpxtool assemble`. Nothing is edited by hand; the `.vpx` is not committed and is rebuilt from the repository's names. Whether any of it ships is part of the release decision (spec §4.5).
15. **What is automated on Linux.** Fully: the libpinmame export proofs (`check.sh`, `pinmame_vpx.sh`: frames, sound, lamps, solenoids, GI, RGB, servos, switches, mechanics, a second session). VPX standalone, headless (`run_vpx.sh`): the first launch writes the NVRAM; in the second launch every lamp and solenoid output the driver lights (coils through the console's coil commands, GI, RGB, servos) reaches the table script lit and nothing else does, and every mapped key (29 playfield keys, both flipper buttons, Enter) closes and opens its switch in the driver in order. Manual (`CHECKLIST-linux.md`): the display in the table's flasher, sound through VPX, the look of the board. Windows (`CHECKLIST-windows.md`): for the Windows worker session, unverified until run.
16. **Sound through VPX is not automated.** With SDL's disk audio driver VPX opens two devices (table and backglass) on the same file and they overwrite each other. The stream VPX's plugin receives is the `AudioUpdate` message the test host subscribes to, which `pinmame_vpx.sh` checks against the driver's capture (±1, the dither); what VPX then plays is on the manual checklist.
17. **The display flasher is unverified.** In the headless runs the table's `Display` flasher (render mode display, `ctrl://default/display`) stayed empty, for Domino's and for Baby Pac-Man alike, so the cause is the flasher's setup or the headless renderer, not the driver; and the board rendered black while lamp callbacks ran, though the same table showed its labels while no lamp changed. Both are on the Linux and Windows checklists with a screenshot request; `run_vpx.sh` keeps its screenshots. Cost if wrong: the table shows no display until someone sets the flasher up in the VPX editor; the plugin's frames themselves are checked (Ruling 5).
18. **Keys.** The table maps 29 playfield switches to letter and punctuation keys (not `P`, which pauses VPX, nor digits, which VPX and `pinheck.vbs` use); flippers, Enter (0), Back (7), User (9), coin door (End), coin (5) and start (1) come from `pinheck.vbs` and VPX's defaults.
19. **The first launch** is documented in the spec (§8, added by Task 6) and in both checklists: two restarts on an empty NVRAM (the PIC32 reflash ends in `PLEASE RESTART`, then `System has been updated / Please restart your machine`); with `PINHECK_INSERVICE=6` the first launch shows the second message only.

## Review Focus

- **A table that sets switch numbers the matrix lacks** (0, −1, 9, 10, 19, 20, 50, 89, 90, 99, 100, 109, 119, 120, 128): no real switch may move. `vpx.py outputs` sets them all at frame 900 and requires no `W` line in the driver's log for 32 frames (Task 2).
- **VPX's reset key** (`Controller.Stop`, `.Run` in one process): the second session must run and the link log must be whole. `pinmame_vpx.sh`'s restart launch (Task 2).
- **Hosts that read outputs as legacy values and as physical outputs** (`SolMask(2)` 0 or 2): both paths must equal the driver's. `vpx.py outputs` on the `board` and `physout` launches (Task 2).
- **`HandleMechanics` masks**: 1 simulates the Noid, 2 leaves it to the table. `vpx.py mech` (Task 2).
- **A VPX build older than versioned plugin messages**: the table would show nothing without an error. `run_vpx.sh` refuses such a build with a message (Task 5), and spec §8 records the requirement (Task 6).

## File Structure

| File | Responsibility |
|---|---|
| `src/libpinmame/libpinmame.cpp` | the display-count gate, the lamp and solenoid getters' ranges (Task 1); GI 8–15 among the plugin's outputs for pinHeck (Task 2) (modified) |
| `src/wpc/vpintf.[ch]` | `vp_getLampIndex`; `vp_getLamp` rejects −1 (Task 1) (modified) |
| `src/wpc/pinheck.h` | `PINHECK_VIDEO_SCALE` 1 under `LIBPINMAME` (Task 2) (modified) |
| `src/wpc/pinheck.c` | switch and lamp numbers that name nothing, the link log closed at stop and the decoder reset (Task 2); no clear of the core's screen (Task 4) (modified) |
| `src/wpc/sims/pinheck/dominos.c` | `dominos_handleMech` honours its mask (Task 2) (modified) |
| `src/wpc/pinheck_names.h` | Domino's switch, lamp and output names and numbers (Task 3) |
| `tests/pinheck/vpx/host.cpp` | libpinmame test host: callback API or plugin message API, logs frames, sound, outputs; second session (Task 1) |
| `tests/pinheck/vpx/vpx.py` | checks of the host's logs against the driver's (Task 1) |
| `tests/pinheck/vpx/check.sh` | no firmware: libpinmame's display list and getters on `pb_l5`, `tz_94h`, `babypac` (Task 1); the scale (Task 2); names and build lists (Task 3) |
| `tests/pinheck/vpx/pinmame_vpx.sh` | Domino's through libpinmame: frames, sound, outputs, mechanics, restart (Task 2) |
| `tests/pinheck/vpx/names.py` | names header against the driver; generates `pinheck.vbs` (Task 3) |
| `tests/pinheck/display/pinmame_display.sh`, `render.py` | the core's solenoid log in a snapshot (Task 4) (modified) |
| `docs/superpowers/specs/…m6-m7…`, `…m10-vpx…`, `plans/…roadmap.md` | the window and libpinmame frame sizes, findings, first launch, roadmap row (Task 6) (modified) |
| outside the repo, `/code/spooky_domino/vpx/`: `make_table.py`, `dominos_test.vbs`, `session.sh`, `run_vpx.sh`, `check_vpx.py`, `CHECKLIST-linux.md`, `CHECKLIST-windows.md` | the test table, its headless run and check, the checklists (Task 5) |

---
### Task 1: libpinmame on Linux: the display count and the lamp and solenoid getters

**Files:**
- Modify: `src/libpinmame/libpinmame.cpp` (`OnStateChange`, `PinmameGetSolenoid`, `PinmameGetLamp`), `src/wpc/vpintf.c`, `src/wpc/vpintf.h`
- Create: `tests/pinheck/vpx/host.cpp`, `tests/pinheck/vpx/vpx.py`, `tests/pinheck/vpx/check.sh`, `tests/pinheck/vpx/.gitignore`

**Interfaces:**
- Consumes: libpinmame's C API (`libpinmame.h`) and plugin message API (`plugins/ControllerPlugin.h`, `plugins/MsgPlugin.h`).
- Produces: `int vp_getLampIndex(int lampNo)` (matrix index after `lamp2m`, −1 if the game has no such lamp); `build/libpinmame/libpinmame.so` built from `build/libpinmame-src` (the libpinmame CMake list with links to the tree); the host `host [-p] [-o] [-x] [-P] [-R] [-m mech] [-s switches] GAME FRAMES DIR` writing `DIR/api.log` (`avail`, `display`, `plugin …`, `O <frame> key=value…`, `restart after`, `end` lines), `DIR/frames.bin`, `DIR/audio.raw`, `DIR/link1.log`; `vpx.py displays|probe|plan|outputs|media|mech|restart`.

The host and `vpx.py` are written whole here; Task 2 runs their Domino's modes.

- [ ] **Step 1: Build libpinmame on Linux**

libpinmame's CMake list must be the source root; a directory of links keeps the root `CMakeLists.txt` free for sdl3pinmame:

```sh
mkdir -p build/libpinmame-src && ln -sfn ../../cmake build/libpinmame-src/cmake && ln -sfn ../../ext build/libpinmame-src/ext && ln -sfn ../../src build/libpinmame-src/src
cp cmake/libpinmame/CMakeLists.txt build/libpinmame-src/CMakeLists.txt
cmake -S build/libpinmame-src -B build/libpinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 -DBUILD_STATIC=OFF > build/libpinmame-cfg.log 2>&1; tail -1 build/libpinmame-cfg.log
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
```
Expected (about 10 minutes):
```text
-- Build files have been written to: /code/spooky_domino/work/wt-m10r/build/libpinmame
[100%] Built target pinmame_test
```

- [ ] **Step 2: Write the test host**

```sh
mkdir -p tests/pinheck/vpx
printf 'build/\n' > tests/pinheck/vpx/.gitignore
cat > tests/pinheck/vpx/host.cpp <<'EOF'
/* libpinmame test host: runs a game headless through libpinmame's callback API or, with -p, the plugin
   message API VPX standalone uses, and logs what a host receives.
   host [-p] [-o] [-x] [-P] [-R] [-m mech] [-s switches] GAME FRAMES DIR
     -p  plugin message API (a minimal MsgPluginAPI host); -o sample lamps 1-98 and solenoids 1-64 each frame;
     -x  probe lamp and solenoid numbers no game has (0, -1, 100000) and pinHeck lamp numbers that do not exist; -m  HandleMechanics mask (default 0);
     -s  file of "frame switch state" lines applied with PinmameSetSwitch; -P  physical outputs (SolMask(2) = 2);
     -R  stop after FRAMES, copy $PINHECK_LINK_LOG to DIR/link1.log, run FRAMES more in a new session
   DIR gets api.log, frames.bin (VIDEO frames: uint32 frame, then the pixels) and audio.raw (int16 stereo). */
#include "libpinmame.h"
#include "plugins/ControllerPlugin.h"
#include <atomic>
#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static FILE *logf_, *framef, *audiof;
static std::atomic<int> frame{0}, target{0}, done{0};
static std::mutex logm;
static std::map<int, std::vector<std::pair<int, int>>> switches;
static bool sample_outputs, probe, physout;
static int updates[16], with_data[16], announced[16], counts[16], channels = 2;

static void logl(const char *fmt, ...)
{
	va_list ap;
	std::lock_guard<std::mutex> g(logm);
	va_start(ap, fmt);
	vfprintf(logf_, fmt, ap);
	va_end(ap);
}

/* plugin message API: one endpoint for libpinmame, one for this host; callbacks queued to the main thread */
static std::map<std::string, unsigned> msg_ids;
static std::map<unsigned, std::vector<std::pair<msgpi_msg_callback, void *>>> subs;
static std::vector<std::pair<msgpi_timer_callback, void *>> queue;
static std::mutex qm;
static std::thread::id main_id;
static unsigned MSGPIAPI GetPluginEndpoint(const char *) { return 0; }
static void MSGPIAPI GetEndpointInfo(const uint32_t, MsgEndpointInfo *) {}
static unsigned MSGPIAPI GetMsgID(const char *ns, const char *name)
{
	std::string k = std::string(ns) + "." + name;
	auto it = msg_ids.find(k);
	if (it != msg_ids.end()) return it->second;
	unsigned id = (unsigned)msg_ids.size() + 1;
	msg_ids[k] = id;
	return id;
}
static void MSGPIAPI SubscribeMsg(const uint32_t, const unsigned id, const msgpi_msg_callback cb, void *ud) { subs[id].push_back({cb, ud}); }
static void MSGPIAPI UnsubscribeMsg(const unsigned id, const msgpi_msg_callback cb, void *ud)
{
	auto &v = subs[id];
	for (size_t i = 0; i < v.size(); i++)
		if (v[i].first == cb && v[i].second == ud) { v.erase(v.begin() + i); break; }
}
static void MSGPIAPI BroadcastMsg(const uint32_t, const unsigned id, void *data)
{
	auto v = subs[id];
	for (auto &s : v) s.first(id, s.second, data);
}
static void MSGPIAPI SendMsg(const uint32_t ep, const unsigned id, const uint32_t, void *data) { BroadcastMsg(ep, id, data); }
static void MSGPIAPI ReleaseMsgID(const unsigned) {}
static void MSGPIAPI RegisterSetting(const uint32_t, MsgSettingDef *) {}
static void MSGPIAPI SaveSetting(const uint32_t, MsgSettingDef *) {}
static void MSGPIAPI RunOnMainThread(const uint32_t, const double, const msgpi_timer_callback cb, void *ud)
{
	if (std::this_thread::get_id() == main_id) { cb(ud); return; }
	std::lock_guard<std::mutex> g(qm);
	queue.push_back({cb, ud});
}
static void run_queue()
{
	std::vector<std::pair<msgpi_timer_callback, void *>> q;
	{
		std::lock_guard<std::mutex> g(qm);
		q.swap(queue);
	}
	for (auto &c : q) c.first(c.second);
}
static void MSGPIAPI FlushPendingCallbacks(const uint32_t) { if (std::this_thread::get_id() == main_id) run_queue(); }
static MsgPluginAPI api = { 1, GetPluginEndpoint, GetEndpointInfo, GetMsgID, SubscribeMsg, UnsubscribeMsg, BroadcastMsg, SendMsg,
	ReleaseMsgID, RegisterSetting, SaveSetting, RunOnMainThread, FlushPendingCallbacks };

static std::vector<DisplaySrcId> displays;
static std::vector<StateSrcId> groups;
static std::atomic<int> plugin_ready{0};
static unsigned last_frame_id = ~0u;

static void MSGPIAPI OnAudio(const unsigned, void *, void *data)
{
	AudioUpdateMsg *m = (AudioUpdateMsg *)data;
	if (!m->buffer) return;
	if (m->sampleFormat != CTLPI_AUDIO_FORMAT_SAMPLE_INT16 || m->channelFormat != CTLPI_AUDIO_FORMAT_CHANNEL_STEREO) {
		logl("plugin audio format %u channels %u\n", m->sampleFormat, m->channelFormat);
		return;
	}
	fwrite(m->buffer, 1, m->bufferSize, audiof);
}

static void plugin_enumerate()
{
	GetDisplaySrcMsg dm = { 0, 0, nullptr };
	SendMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG), 0, &dm);
	displays.resize(dm.count);
	dm.maxEntryCount = dm.count;
	dm.count = 0;
	dm.entries = displays.data();
	SendMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG), 0, &dm);
	for (auto &d : displays) logl("plugin display %ux%u format %u hardware %08x\n", d.width, d.height, d.frameFormat, d.hardware);
	GetStateSrcMsg sm = { 0, 0, nullptr };
	SendMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_STATE_GET_SRC_MSG), 0, &sm);
	groups.resize(sm.count);
	sm.maxEntryCount = sm.count;
	sm.count = 0;
	sm.entries = groups.data();
	SendMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_STATE_GET_SRC_MSG), 0, &sm);
	for (auto &g : groups) {
		std::string ids;
		for (unsigned i = 0; i < g.nStates; i++) ids += " " + std::to_string((int)g.stateDefs[i].mappingId);
		logl("plugin group %s %u:%s\n", g.name, g.nStates, ids.c_str());
	}
	plugin_ready = 1;
}

/* one byte per value: floats 0..1 scaled like libpinmame's saturatedByte, integers as they are */
static int state_byte(const StateDef &s)
{
	union { uint8_t u8; int32_t i32; float f; } v;
	memset(&v, 0, sizeof(v));
	s.GetState(s.callContext, &v);
	if (s.dataFormat == CTLPI_STATE_FORMAT_FLOAT) return (int)(255.0f * (v.f < 0.0f ? 0.0f : v.f > 1.0f ? 1.0f : v.f));
	if (s.dataFormat == CTLPI_STATE_FORMAT_INT32) return v.i32;
	return v.u8;
}

static std::map<std::string, int> last;
static void change(std::string &line, const std::string &k, int v)
{
	auto it = last.find(k);
	if (it != last.end() && it->second == v) return;
	last[k] = v;
	line += " " + k + "=" + std::to_string(v);
}

static const int bad_numbers[] = { 9, 10, 19, 20, 29, 30, 50, 89, 90, 99, 100, 109, 119, 120, 128 };

static void sample(int f)
{
	std::string line;
	if (sample_outputs) {
		for (int n = 1; n <= 98; n++) if (n % 10 >= 1 && n % 10 <= 8) change(line, "L" + std::to_string(n), PinmameGetLamp(n));
		for (int n = 1; n <= 64; n++) change(line, "S" + std::to_string(n), PinmameGetSolenoid(n));
		for (int n = 1; n <= 98; n++) if (n % 10 >= 1 && n % 10 <= 8) change(line, "W" + std::to_string(n), PinmameGetSwitch(n) != 0);
	}
	if (probe) {
		for (int n : bad_numbers) change(line, "X" + std::to_string(n), PinmameGetLamp(n));
		for (int n : { 0, -1, 100000 }) {
			change(line, "X" + std::to_string(n), PinmameGetLamp(n));
			change(line, "Y" + std::to_string(n), PinmameGetSolenoid(n));
		}
	}
	if (plugin_ready) {
		for (auto &g : groups) {
			const char *p = !strcmp(g.name, "Solenoids") ? "PS" : !strcmp(g.name, "Lamps") ? "PL" : nullptr;
			if (!p) continue;
			for (unsigned i = 0; i < g.nStates; i++) change(line, p + std::to_string((int)g.stateDefs[i].mappingId), state_byte(g.stateDefs[i]));
		}
		for (auto &d : displays) {
			DisplayFrame fr = d.GetRenderFrame(d.callContext);
			if (fr.frameId == last_frame_id) continue;
			last_frame_id = fr.frameId;
			const size_t n = (size_t)d.width * d.height * (d.frameFormat == CTLPI_DISPLAY_FORMAT_SRGB565 ? 2 : 3);
			uint32_t tag = (uint32_t)f | 0x80000000u;
			fwrite(&tag, 4, 1, framef);
			fwrite(fr.frame, 1, n, framef);
		}
	}
	if (!line.empty()) logl("O %d%s\n", f, line.c_str());
}

static void PINMAMECALLBACK OnStateUpdated(int state, void *const) { logl("state %d\n", state); }
static void PINMAMECALLBACK OnDisplayAvailable(int index, int count, PinmameDisplayLayout *l, void *const)
{
	logl("avail %d %d type %d %dx%d depth %d length %d\n", index, count, l->type, l->width, l->height, l->depth, l->length);
	if (index >= 0 && index < 16) { announced[index]++; counts[index] = count; }
}
static void PINMAMECALLBACK OnDisplayUpdated(int index, void *data, PinmameDisplayLayout *l, void *const)
{
	if (index >= 0 && index < 16) { updates[index]++; if (data) with_data[index]++; }
	if (index != 0) return;
	const int f = ++frame;
	if (data && (l->type & PINMAME_DISPLAY_TYPE_SEGMASK) == PINMAME_DISPLAY_TYPE_VIDEO) {
		uint32_t tag = (uint32_t)f;
		fwrite(&tag, 4, 1, framef);
		fwrite(data, 1, (size_t)l->width * l->height * (l->depth == 16 ? 2 : 3), framef);
	}
	auto it = switches.find(f);
	if (it != switches.end()) for (auto &s : it->second) PinmameSetSwitch(s.first, s.second);
	sample(f);
	if (f >= target) done = 1;
}
static int PINMAMECALLBACK OnAudioAvailable(PinmameAudioInfo *a, void *const)
{
	logl("audio format %d channels %d rate %.2f fps %.2f\n", a->format, a->channels, a->sampleRate, a->framesPerSecond);
	channels = a->channels;
	return a->samplesPerFrame;
}
static int PINMAMECALLBACK OnAudioUpdated(void *buf, int samples, void *const)
{
	fwrite(buf, 2 * (size_t)channels, (size_t)samples, audiof);
	return samples;
}
static void PINMAMECALLBACK OnLogMessage(PINMAME_LOG_LEVEL level, const char *fmt, va_list args, void *const)
{
	char b[1024];
	vsnprintf(b, sizeof(b), fmt, args);
	if (level == PINMAME_LOG_LEVEL_ERROR) logl("error %s\n", b);
}
static int PINMAMECALLBACK IsKeyPressed(PINMAME_KEYCODE, void *const) { return 0; }

int main(int argc, char **argv)
{
	bool plugin = false, restart = false;
	int mech = 0, a = 1;
	for (; a < argc && argv[a][0] == '-'; a++) {
		if (!strcmp(argv[a], "-p")) plugin = true;
		else if (!strcmp(argv[a], "-o")) sample_outputs = true;
		else if (!strcmp(argv[a], "-x")) probe = true;
		else if (!strcmp(argv[a], "-P")) physout = true;
		else if (!strcmp(argv[a], "-R")) restart = true;
		else if (!strcmp(argv[a], "-m") && a + 1 < argc) mech = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-s") && a + 1 < argc) {
			FILE *f = fopen(argv[++a], "r");
			int fr, sw, st;
			if (!f) return 2;
			while (fscanf(f, "%d %d %d", &fr, &sw, &st) == 3) switches[fr].push_back({sw, st});
			fclose(f);
		} else return 2;
	}
	if (argc - a != 3) {
		fprintf(stderr, "usage: host [-p] [-o] [-x] [-P] [-R] [-m mech] [-s switches] GAME FRAMES DIR\n");
		return 2;
	}
	const char *game = argv[a], *dir = argv[a + 2];
	target = atoi(argv[a + 1]);
	main_id = std::this_thread::get_id();
	std::string d(dir);
	logf_ = fopen((d + "/api.log").c_str(), "w");
	if (logf_) setvbuf(logf_, nullptr, _IOLBF, 0);
	framef = fopen((d + "/frames.bin").c_str(), "wb");
	audiof = fopen((d + "/audio.raw").c_str(), "wb");
	if (!logf_ || !framef || !audiof) return 2;
	PinmameConfig config = { PINMAME_AUDIO_FORMAT_INT16, 44100, "", &OnStateUpdated, &OnDisplayAvailable, &OnDisplayUpdated,
		&OnAudioAvailable, &OnAudioUpdated, nullptr, nullptr, nullptr, nullptr, &IsKeyPressed, &OnLogMessage, nullptr };
	snprintf((char *)config.vpmPath, PINMAME_MAX_PATH, "%s/", dir);
	PinmameSetConfig(&config);
	PinmameSetHandleKeyboard(0);
	PinmameSetHandleMechanics(mech);
	if (physout) PinmameSetSolenoidMask(2, 2);
	if (plugin) {
		PinmameSetMsgAPI(&api, 0);
		SubscribeMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_AUDIO_ON_UPDATE_MSG), OnAudio, nullptr);
	}
	logl("options plugin %d physout %d mechanics %d\n", plugin, physout, mech);
	const auto t0 = std::chrono::steady_clock::now();
	for (int session = 0; session < (restart ? 2 : 1); session++) {
		if (session) {
			/* what the first session left in the link log, read while the process still runs */
			const char *link = getenv("PINHECK_LINK_LOG");
			FILE *in = link ? fopen(link, "rb") : nullptr, *out = fopen((d + "/link1.log").c_str(), "wb");
			char buf[4096];
			size_t n;
			while (in && out && (n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
			if (in) fclose(in);
			if (out) fclose(out);
			logl("restart after %d frames\n", (int)frame);
			frame = 0;
			done = 0;
		}
		if (PinmameRun(game) != PINMAME_STATUS_OK) { logl("run failed\n"); return 1; }
		while (!done) {
			run_queue();
			if (plugin && !plugin_ready && !groups.size() && PinmameIsRunning() && !subs[GetMsgID(CTLPI_NAMESPACE, CTLPI_STATE_GET_SRC_MSG)].empty())
				plugin_enumerate();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		plugin_ready = 0;
		PinmameStop();
		run_queue();
	}
	for (int i = 0; i < 16; i++)
		if (announced[i] || updates[i]) logl("display %d announced %d count %d updates %d with data %d\n", i, announced[i], counts[i], updates[i], with_data[i]);
	logl("end %d frames %.1f s\n", (int)frame, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	fclose(logf_);
	fclose(framef);
	fclose(audiof);
	return 0;
}
EOF
```

- [ ] **Step 3: Write the checks**

`vpx.py` reads the host's `api.log` and, for Domino's (Task 2), the driver's `PINHECK_OUT_LOG`, `PINHECK_FRAME_LOG` and `PINHECK_WAV`. `check.sh` runs the host on the three reference games; the displays check requires every announced index to exist and get updates, the probe requires lamp and solenoid numbers 0, −1 and 100000 to read 0 without stopping the emulation.

```sh
cat > tests/pinheck/vpx/vpx.py <<'EOF'
#!/usr/bin/env python3
"""What a libpinmame host (VPX standalone's PinMAME plugin, or any other) receives (spec M10 4.1), from host's logs.
  vpx.py displays DIR...        every announced display index exists and gets updates
  vpx.py probe DIR...           lamp and solenoid numbers no game has read 0
  vpx.py plan DIR               the workloads: DIR/switches.txt, send, send_at, send_gap, frames; DIR/mech.txt
  vpx.py outputs DIR            lamps, solenoids, GI, RGB, servos and switches through the API equal the driver's
  vpx.py media DIR REF.wav      display frames and sound through the API equal the driver's frame log and capture
  vpx.py mech ON_DIR OFF_DIR    HandleMechanics bit 0 turns the simulated Noid on and off
  vpx.py restart DIR            a second session in the same process: the link log is complete at the first
                                session's end, and the second session's packets start whole"""
import array
import os
import struct
import sys
import wave

FPS = 60
W, H = 128, 32
MENU_AT, SEND_AT, GAP = 10.5, 11.5, 0.25
ENTER, BACK, LFLIP, RFLIP = 6, 5, 114, 112      # cabinet switches; the flippers through PinMAME's flipper column
PROBE_AT = 900                                  # frame: switch numbers that do not exist are set, then cleared
BAD = (9, 10, 19, 20, 29, 30, 50, 89, 90, 99, 100, 109, 119, 120, 128, 0, -1)


def api_log(d):
    """api.log -> ({key: [(frame, value)]}, other lines)"""
    ser, other = {}, []
    for line in open(os.path.join(d, 'api.log')):
        f = line.split()
        if f and f[0] == 'O':
            fr = int(f[1])
            for x in f[2:]:
                k, v = x.split('=')
                ser.setdefault(k, []).append((fr, int(v)))
        else:
            other.append(line.rstrip('\n'))
    return ser, other


def displays(dirs):
    fail = 0
    for d in dirs:
        _, other = api_log(d)
        avail = [l.split() for l in other if l.startswith('avail ')]
        counts = set(int(a[2]) for a in avail)
        idx = sorted(int(a[1]) for a in avail)
        upd = {int(l.split()[1]): int(l.split()[7]) for l in other if l.startswith('display ')}
        n = counts.pop() if len(counts) == 1 else None
        ok = n is not None and idx == list(range(n)) and all(upd.get(i, 0) > 0 for i in range(n))
        print('displays: %s announces %s display(s), indices %s, updates %s%s' % (
            os.path.basename(d), n if n is not None else sorted(counts), idx, [upd.get(i, 0) for i in idx], '' if ok else '  FAIL'))
        fail += not ok
    return fail


def probe(dirs):
    """lamp and solenoid numbers no game has read 0 and do not stop the emulation"""
    fail = 0
    for d in dirs:
        api, other = api_log(d)
        vals = dict((k, set(v for _, v in api.get(k, []))) for k in ('X0', 'X-1', 'X100000', 'Y0', 'Y-1', 'Y100000'))
        ok = any(l.startswith('end ') for l in other) and all(v == {0} for v in vals.values())
        print('probe: %s %s%s' % (os.path.basename(d), ' '.join('%s=%s' % (k, sorted(v)) for k, v in vals.items()), '' if ok else '  FAIL'))
        fail += not ok
    return fail


def menu_plan():
    """(time, switch) presses of the service menu walk after the console commands"""
    ev, t = [], 19.0

    def press(sw, dt):
        nonlocal t
        ev.append((t, sw))
        t += dt
    for _ in range(3):
        press(RFLIP, 1.0)                           # AUDIO/MUSIC, SOLENOID, SERVO
    press(ENTER, 1.0)                               # SERVO TEST: NOID RIGHT
    press(ENTER, 1.5)                               # servo 0 at 0 degrees
    press(RFLIP, 1.0)                               # NOID STOP
    press(ENTER, 1.0)
    press(RFLIP, 1.0)                               # NOID LEFT
    press(ENTER, 1.5)                               # servo 0 at 180 degrees
    press(BACK, 1.0)                                # SERVO
    press(RFLIP, 1.0)                               # LAMP
    press(ENTER, 0.5)                               # LAMP TEST: ALL OFF
    for _ in range(20):
        press(RFLIP, 0.5)                           # PLAYFIELD GI 1..128, BACKBOX GI 0..7, ALL ... GI ON, GI AND LAMPS ALL ON, ALL OFF
    press(BACK, 1.0)                                # LAMP
    press(RFLIP, 1.0)                               # RGB LIGHTING
    press(ENTER, 1.0)                               # RGB1 RED
    for _ in range(7):
        press(RFLIP, 1.0)                           # ... RGB2 WHITE
    press(LFLIP, 1.0)                               # back to RGB2 BLUE
    press(BACK, 1.0)
    return ev, t


def uart_plan():
    return ['[E97000]', '[L99000]'] + ['[M%02d020]' % c for c in range(24)] + ['[L98000]']


def plan(d):
    ev, end = menu_plan()
    lines = ['%d %d 1\n%d %d 0\n' % (round(MENU_AT * FPS), ENTER, round(MENU_AT * FPS) + 6, ENTER)]
    lines += ['%d %d 1\n%d %d 0\n' % (round(t * FPS), sw, round(t * FPS) + 6, sw) for t, sw in ev]
    lines += ['%d %d 1\n' % (PROBE_AT, n) for n in BAD] + ['%d %d 0\n' % (PROBE_AT + 30, n) for n in BAD]
    mech = [(MENU_AT, ENTER)] + [(MENU_AT + 1 + k, RFLIP) for k in range(3)] + [(MENU_AT + 4, ENTER), (MENU_AT + 5, ENTER)]
    open(os.path.join(d, 'mech.txt'), 'w').write(''.join('%d %d 1\n%d %d 0\n' % (round(t * FPS), sw, round(t * FPS) + 6, sw) for t, sw in mech))
    for name, text in (('switches.txt', ''.join(lines)), ('send', ''.join(c + '~' for c in uart_plan())),
                       ('send_at', '%g' % SEND_AT), ('send_gap', '%g' % GAP), ('frames', '%d' % round((end + 1) * FPS))):
        open(os.path.join(d, name), 'w').write(text + ('\n' if name != 'switches.txt' else ''))


def driver_log(d):
    """out.log -> {key: [(vblank, value)]}: P lamps/solenoids, B lamp bits, W switches"""
    ser = {}
    for line in open(os.path.join(d, 'out.log')):
        f = line.split()
        if len(f) < 3 or f[1] not in ('P', 'B', 'W'):
            continue
        k = round(float(f[0]) * FPS)
        if f[1] == 'B':
            m = bytes.fromhex(f[2])
            for i in range(72):
                ser.setdefault('B%d' % ((i // 8 + 1) * 10 + i % 8 + 1), []).append((k, (m[i // 8] >> (i % 8)) & 1))
            continue
        for x in f[2:]:
            n, v = x.split('=')
            ser.setdefault(('W' + n) if f[1] == 'W' else n, []).append((k, int(v)))
    return ser


class Series:
    def __init__(self, pts):
        self.pts, self.i, self.v = pts, 0, 0

    def at(self, k):
        """value at frame k; k must not decrease between calls"""
        while self.i < len(self.pts) and self.pts[self.i][0] <= k:
            self.v = self.pts[self.i][1]
            self.i += 1
        return self.v


LAMPS = [c * 10 + r for c in range(1, 9) for r in range(1, 9)] + [91]
SOLS = list(range(1, 33)) + list(range(37, 49)) + list(range(51, 65))
DRIVEN = list(range(1, 33)) + list(range(37, 45)) + list(range(51, 59)) + [62, 63, 64]
SWITCHES = list(range(1, 9)) + [c * 10 + r for c in range(1, 10) for r in range(1, 9)]


def outputs(d):
    """Each value a host read at frame f must be one the driver's output has at vblank f-1 .. f+2: an output's
    integrator brings its value up to date one query late, so the host's reads between the driver's vblank
    logs move the driver's series by up to two frames. Matrix lamps are strobed, and a read between two
    vblanks may catch one in its dark phase: up to 1% of a lamp's reads may differ."""
    api, other = api_log(d)
    drv = driver_log(d)
    physout = 'physout 1' in ' '.join(l for l in other if l.startswith('options '))
    last = max(fr for s in api.values() for fr, _ in s)
    fails = []
    same = lambda t: lambda a, r: abs(a - r) <= t
    pairs = []                                   # (API key, driver key, match, fraction of reads that may differ)
    for n in LAMPS:
        if physout:
            pairs += [('L%d' % n, 'L%d' % n, same(1), 0.01), ('PL%d' % n, 'L%d' % n, same(1), 0.01)]
        else:
            pairs += [('L%d' % n, 'B%d' % n, same(0), 0.01), ('PL%d' % n, 'B%d' % n, lambda a, r: a == 255 * r, 0.01)]
    for n in SOLS:
        pairs.append(('S%d' % n, 'S%d' % n, same(1), 0))
        if physout or n > 44:
            pairs.append(('PS%d' % n, 'S%d' % n, same(1), 0))
        elif n <= 32:                            # a legacy host's solenoid: on from half power
            pairs.append(('PS%d' % n, 'S%d' % n, lambda a, r: a == (255 if r >= 128 else 0), 0))
    pairs += [('W%d' % n, 'W%d' % n, same(0), 0) for n in SWITCHES]
    compared, lit, bad = 0, set(), 0
    for ak, dk, match, frac in pairs:
        if ak not in api:
            fails.append('%s is not exported' % ak)
            continue
        a = Series(api[ak])
        r = [Series(drv.get(dk, [])) for _ in range(4)]
        n, first = 0, None
        for f in range(2, last - 1):
            av, rv = a.at(f), [r[i].at(f - 1 + i) for i in range(4)]
            compared += 1
            if not any(match(av, x) for x in rv):
                n += 1
                first = first or '%s is %d at frame %d, the driver has %s' % (ak, av, f, '/'.join(map(str, rv)))
            elif av:
                lit.add(ak)
        bad += n
        if n > frac * (last - 3):
            fails.append('%s (%d reads differ)' % (first, n))
    for n in range(37, 45):                      # a legacy host's GI_8..15: on at some time in the frame
        if api.get('PS%d' % n):
            lit.update('PS%d' % n for _, v in api['PS%d' % n] if v)
    want = ['%s%d' % (p, n) for n in LAMPS for p in ('L', 'PL')] + ['%s%d' % (p, n) for n in DRIVEN for p in ('S', 'PS')]
    want += ['W%d' % n for n in (3, 4, 5, 6)]
    dark = [k for k in want if k not in lit]
    if dark:
        fails.append('never seen on: %s' % ' '.join(dark))
    groups = {l.split()[2]: l.split(':', 1)[1].split() for l in other if l.startswith('plugin group ')}
    ids = [int(x) for x in groups.get('Solenoids', [])]
    if not all(n in ids for n in range(37, 45)):
        fails.append('the plugin lists no solenoids 37-44 (GI_8..15): %s' % ids)
    for n in BAD:
        vals = set(v for _, v in api.get('X%d' % n, [(0, 0)]))
        if vals != {0}:
            fails.append('lamp %d, which does not exist, reads %s' % (n, sorted(vals)))
    moved = [x for x in open(os.path.join(d, 'out.log'))
             if x.split()[1:2] == ['W'] and PROBE_AT <= round(float(x.split()[0]) * FPS) <= PROBE_AT + 32]
    if moved:
        fails.append('setting switches that do not exist moved real ones: %s' % moved[0].strip())
    for f in fails:
        print('VPX FAIL: ' + f)
    print('outputs: %s, %d reads of %d outputs compared, %d differ, %d failures' % (
        'physical outputs' if physout else 'legacy outputs', compared, len(pairs), bad, len(fails)))
    return 1 if fails else 0


def rgb565(v):
    """a frame byte as libpinmame exports it: RGB332 -> the driver's 15-bit pen -> 5.6.5"""
    r, g, b = ((v >> 5) & 7) * 255 // 7, ((v >> 2) & 7) * 255 // 7, (v & 3) * 255 // 3
    p = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
    return (p & 0x7fe0) << 1 | (p >> 4) & 0x20 | (p & 0x1f)


def media(d, ref):
    fails = []
    _, other = api_log(d)
    avail = [l for l in other if l.startswith('avail ')]
    if avail != ['avail 0 1 type 15 128x32 depth 16 length 0']:
        fails.append('display announced as %s, expected one 128x32 VIDEO display of depth 16' % avail)
    lut = [rgb565(v) for v in range(256)]
    raw = open(os.path.join(d, 'frames.log'), 'rb').read()
    rec = 20 + W * H
    drv = []
    for i in range(0, len(raw) - rec + 1, rec):
        f = raw[i + 20:i + rec]
        if not drv or drv[-1] != f:
            drv.append(f)
    want = [array.array('H', (lut[v] for v in f)).tobytes() for f in drv]
    raw = open(os.path.join(d, 'frames.bin'), 'rb').read()
    rec = 4 + W * H * 2
    got = {False: [], True: []}
    plugin_run = any(l.startswith('plugin display') for l in other)
    for i in range(0, len(raw) - rec + 1, rec):
        tag = struct.unpack_from('<I', raw, i)[0]
        f = raw[i + 4:i + rec]
        if tag >= 0x80000000 and not got[True] and not any(f):
            continue                             # the plugin's frame buffer before the first frame
        got[tag >= 0x80000000].append(f)
    for plugin in (False, True) if plugin_run else (False,):
        k, seen = 0, 0
        for f in got[plugin]:
            while k < len(want) and want[k] != f:
                k += 1
            if k == len(want):
                break
            seen += 1
        path = 'plugin' if plugin else 'callback'
        if seen < len(got[plugin]):
            fails.append('%s frame %d is none of the decoded frames that follow the last match' % (path, seen))
        else:
            print('media: %s path: %d frames, each a decoded frame, in order; the frame log has %d distinct frames' % (path, seen, len(want)))
        if len(got[plugin]) < len(want) - 2:
            fails.append('%s path showed %d of %d frames' % ('plugin' if plugin else 'callback', len(got[plugin]), len(want)))
    a = array.array('h', open(os.path.join(d, 'audio.raw'), 'rb').read())
    with wave.open(os.path.join(d, 'api.wav'), 'wb') as out:     # for corr.py
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(44100)
        out.writeframes(a.tobytes())
    w = array.array('h', open(ref, 'rb').read()[44:])
    n = min(len(a), len(w))
    diff = max((abs(a[i] - w[i]) for i in range(n)), default=99)
    loud = sum(1 for i in range(n) if abs(w[i]) > 1000)
    print('media: %d API samples, %d captured, largest difference %d, %d loud samples' % (len(a), len(w), diff, loud))
    if abs(len(a) - len(w)) > 2 * 800 or diff > 1 or loud < 1000:
        fails.append('the API stream is not the driver\'s (TPDF dither allows 1)')
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


def mech(on, off):
    fails = []
    for d, want in ((on, True), (off, False)):
        api, _ = api_log(d)
        edges = [fr for fr, v in api.get('W58', [])][1:]
        print('mech: %s, Noid Home changed %d times' % (os.path.basename(d), len(edges)))
        if (len(edges) >= 2) != want:
            fails.append('Noid Home %s with HandleMechanics %s' % ('moved' if edges else 'never moved', os.path.basename(d)))
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


def restart(d):
    fails = []
    _, other = api_log(d)
    if not any(l.startswith('restart after ') for l in other) or not any(l.startswith('end ') for l in other):
        fails.append('the host did not run two sessions')
    first = open(os.path.join(d, 'link1.log')).read()
    lines = open(os.path.join(d, 'link.log')).read().splitlines(True)
    times = [float(l.split()[0]) for l in lines]
    drops = [i for i in range(1, len(times)) if times[i] < times[i - 1]]
    if len(drops) != 1:
        fails.append('the link log has %d time resets, want 1 (one per new session)' % len(drops))
    else:
        k = drops[0]
        if first != ''.join(lines[:k]):
            fails.append('at the first session\'s end the link log held %d of its %d bytes' % (len(first), len(''.join(lines[:k]))))
        one = [l.split()[1:17] for l in lines[:k]][:40]
        two = [l.split()[1:17] for l in lines[k:]][:40]
        if len(two) < 40 or one != two:
            n = next((i for i in range(min(len(one), len(two))) if one[i] != two[i]), min(len(one), len(two)))
            fails.append('the second session\'s packet %d is %s, the first session\'s %s' % (n, ' '.join(two[n]) if n < len(two) else '-', ' '.join(one[n]) if n < len(one) else '-'))
        print('restart: %d packets in the first session, %d in the second, the first 40 of each equal' % (k, len(lines) - k) if not fails else 'restart: %d + %d packets' % (k, len(lines) - k))
    for f in fails:
        print('VPX FAIL: ' + f)
    return 1 if fails else 0


if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['displays'] and len(a) > 1:
        sys.exit(1 if displays(a[1:]) else 0)
    if a[:1] == ['probe'] and len(a) > 1:
        sys.exit(1 if probe(a[1:]) else 0)
    if a[:1] == ['plan'] and len(a) == 2:
        sys.exit(plan(a[1]))
    if a[:1] == ['outputs'] and len(a) == 2:
        sys.exit(outputs(a[1]))
    if a[:1] == ['media'] and len(a) == 3:
        sys.exit(media(a[1], a[2]))
    if a[:1] == ['mech'] and len(a) == 3:
        sys.exit(mech(a[1], a[2]))
    if a[:1] == ['restart'] and len(a) == 2:
        sys.exit(restart(a[1]))
    sys.exit(__doc__)
EOF
cat > tests/pinheck/vpx/check.sh <<'EOF'
#!/bin/sh
# VPX support without the firmware: (with LIBPINMAME and PINMAME_ROMS) libpinmame's display list and getters on a
# segment, a DMD and a video game
[ -z "$LIBPINMAME" ] || LIBPINMAME=$(realpath "$LIBPINMAME") || exit 2
[ -z "$PINMAME_ROMS" ] || PINMAME_ROMS=$(realpath "$PINMAME_ROMS") || exit 2
cd "$(dirname "$0")" || exit 2
S=../../../src
B=build
mkdir -p $B || exit 2
fail=0
if [ -n "$LIBPINMAME" ] && [ -n "$PINMAME_ROMS" ]; then
	c++ -std=c++20 -O1 -Wall -Wextra -Werror -I$S/libpinmame host.cpp "$LIBPINMAME" -Wl,-rpath,"$(dirname "$LIBPINMAME")" -pthread -o $B/host || exit 2
	run() {
		rm -rf $B/$1 && mkdir -p $B/$1/nvram $B/$1/cfg && ln -s "$PINMAME_ROMS" $B/$1/roms || exit 2
		(cd $B/$1 && timeout -k 30 300 ../host -x $2 $1 180 . > run.out 2>&1) || { echo "VPX FAIL: $1 exited $?"; fail=$((fail + 1)); }
	}
	run pb_l5            # outputs as a legacy host reads them
	run tz_94h -P        # physical outputs
	run babypac -P
	python3 vpx.py displays $B/pb_l5 $B/tz_94h $B/babypac || fail=$((fail + 1))
	python3 vpx.py probe $B/pb_l5 $B/tz_94h $B/babypac || fail=$((fail + 1))
else
	echo "vpx: LIBPINMAME or PINMAME_ROMS not set: libpinmame's display list and getters not run"
fi
echo "vpx: $fail failed"
[ $fail -eq 0 ]
EOF
chmod +x tests/pinheck/vpx/check.sh
```

- [ ] **Step 4: Run the checks to see them fail**

```sh
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 timeout -k 30 900 tests/pinheck/vpx/check.sh 2>&1 | tail -8
```
Expected:
```text
VPX FAIL: babypac exited 139
displays: pb_l5 announces 9 display(s), indices [0, 1, 2, 3, 4, 5, 6, 7, 8], updates [180, 180, 180, 180, 180, 180, 180, 180, 180]
displays: tz_94h announces 2 display(s), indices [0], updates [0]  FAIL
displays: babypac announces 2 display(s), indices [0], updates [0]  FAIL
probe: pb_l5 X0=[0, 1] X-1=[0, 1] X100000=[0] Y0=[0] Y-1=[0] Y100000=[0]  FAIL
probe: tz_94h X0=[] X-1=[] X100000=[] Y0=[] Y-1=[] Y100000=[]  FAIL
probe: babypac X0=[] X-1=[] X100000=[] Y0=[] Y-1=[] Y100000=[]  FAIL
vpx: 4 failed
```

- [ ] **Step 5: The display count and the getters**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/libpinmame/libpinmame.cpp', [
    ('''		// Reserve one extra slot unconditionally.
		// Using hasDMDOrVideo as the gate is wrong here: segment-only games also create an extra
		// synthetic 128x32 DMD below, so they need one additional display slot as well.
		displayCount++;
''',
     '''		// Segment-only games get one more slot, for the 128x32 DMD rendered from their segments below
		if (!hasDMDOrVideo)
			displayCount++;
'''),
    ('''PINMAMEAPI int PinmameGetSolenoid(const int solNo)
{
	if (!_isRunning)
		return 0;

	if (options.usemodsol & (CORE_MODOUT_FORCE_ON | CORE_MODOUT_ENABLE_PHYSOUT_SOLENOIDS | CORE_MODOUT_ENABLE_MODSOL))
		core_update_pwm_outputs(CORE_MODOUT_SOL0 + solNo - 1, 1);
''',
     '''PINMAMEAPI int PinmameGetSolenoid(const int solNo)
{
	if (!_isRunning || solNo < 1 || solNo > CORE_MODOUT_SOL_MAX)
		return 0;

	if (options.usemodsol & (CORE_MODOUT_FORCE_ON | CORE_MODOUT_ENABLE_PHYSOUT_SOLENOIDS | CORE_MODOUT_ENABLE_MODSOL)) {
		// core_getSol reads 29-48 from other outputs than solNo - 1
		if (solNo >= 29 && solNo <= 48)
			core_update_pwm_solenoids();
		else
			core_update_pwm_outputs(CORE_MODOUT_SOL0 + solNo - 1, 1);
	}
'''),
    ('''PINMAMEAPI int PinmameGetLamp(const int lampNo)
{
	if (!_isRunning)
		return 0;

	if (options.usemodsol & (CORE_MODOUT_FORCE_ON | CORE_MODOUT_ENABLE_PHYSOUT_LAMPS))
		core_update_pwm_outputs(CORE_MODOUT_LAMP0 + lampNo - 1, 1);
''',
     '''PINMAMEAPI int PinmameGetLamp(const int lampNo)
{
	const int index = _isRunning ? vp_getLampIndex(lampNo) : -1;
	if (index < 0)
		return 0;

	if (options.usemodsol & (CORE_MODOUT_FORCE_ON | CORE_MODOUT_ENABLE_PHYSOUT_LAMPS))
		core_update_pwm_outputs(CORE_MODOUT_LAMP0 + index, 1);
'''),
])

edit('src/wpc/vpintf.c', [
    ('''int vp_getLamp(int lampNo) {
  if (coreData && coreData->lamp2m) lampNo = coreData->lamp2m(lampNo) - 8;
''',
     '''int vp_getLampIndex(int lampNo) {
  if (coreData && coreData->lamp2m) lampNo = coreData->lamp2m(lampNo) - 8;
  return lampNo >= 0 && lampNo < CORE_MODOUT_LAMP_MAX ? lampNo : -1;
}

int vp_getLamp(int lampNo) {
  lampNo = vp_getLampIndex(lampNo);
  if (lampNo < 0) return 0;
'''),
])

edit('src/wpc/vpintf.h', [
    ('''int vp_getLamp(int lampNo);
''',
     '''int vp_getLamp(int lampNo);

/*------------------------------------
/  lamp matrix index of a lamp number, -1 if the game has no such lamp
/-------------------------------------*/
int vp_getLampIndex(int lampNo);
'''),
])
EOF
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log; grep -c 'warning:.*\(libpinmame.cpp\|vpintf\)' build/libpinmame-build.log
```
Expected:
```text
[100%] Built target pinmame_test
0
```

- [ ] **Step 6: Run the checks to see them pass**

```sh
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 timeout -k 30 900 tests/pinheck/vpx/check.sh 2>&1 | tail -8
```
Expected:
```text
displays: pb_l5 announces 9 display(s), indices [0, 1, 2, 3, 4, 5, 6, 7, 8], updates [180, 180, 180, 180, 180, 180, 180, 180, 180]
displays: tz_94h announces 1 display(s), indices [0], updates [180]
displays: babypac announces 1 display(s), indices [0], updates [180]
probe: pb_l5 X0=[0] X-1=[0] X100000=[0] Y0=[0] Y-1=[0] Y100000=[0]
probe: tz_94h X0=[0] X-1=[0] X100000=[0] Y0=[0] Y-1=[0] Y100000=[0]
probe: babypac X0=[0] X-1=[0] X100000=[0] Y0=[0] Y-1=[0] Y100000=[0]
vpx: 0 failed
```

- [ ] **Step 7: Commit**

```sh
git add src/libpinmame/libpinmame.cpp src/wpc/vpintf.c src/wpc/vpintf.h tests/pinheck/vpx/.gitignore tests/pinheck/vpx/host.cpp tests/pinheck/vpx/vpx.py tests/pinheck/vpx/check.sh
git commit -q -m "libpinmame: announce the extra display only for segment games; lamp and solenoid getters check their numbers; libpinmame test host" && git log --oneline -1 | cut -c10-
```
Expected:
```text
libpinmame: announce the extra display only for segment games; lamp and solenoid getters check their numbers; libpinmame test host
```

---
### Task 2: Domino's through libpinmame

**Files:**
- Modify: `src/wpc/pinheck.h`, `src/wpc/pinheck.c`, `src/wpc/sims/pinheck/dominos.c`, `src/libpinmame/libpinmame.cpp` (`SetupMsgApiGameStates`), `tests/pinheck/vpx/check.sh`
- Create: `tests/pinheck/vpx/pinmame_vpx.sh`

**Interfaces:**
- Consumes: Task 1's host and `vpx.py`; Plan 8a's `PINHECK_OUT_LOG` (`P`, `B`, `W` lines), Plan 6's `PINHECK_FRAME_LOG` (20-byte stamp + 4096 bytes), Plan 7's `PINHECK_WAV`, Plan 8b's `PINHECK_LINK_LOG` and `PINHECK_RTC`, Plan 5's `PINHECK_UART1_SEND*` and `PINHECK_INSERVICE`; the console commands `[E97000]`, `[L99000]`, `[Mnnzzz]`, `[L98000]`, `[V00ABC]`, `[F00ABC]` (Plans 6–8a).
- Produces: `pinmame_vpx.sh` printing `pinmame vpx: ok`; `PINHECK_VIDEO_SCALE` 1 under `LIBPINMAME`; `HandleMechanics` bits 0 (Noid) and 1 (bank).

`pinmame_vpx.sh` runs the host on `dominos`: a first boot (its NVRAM seeds the other launches), a video clip and a sound effect through the plugin message API (frames through both paths and the sound against the driver's capture and the source `.wav`), the console's coil commands and the service menu's servo, lamp and RGB tests with the flippers pressed as a table does (114, 112), once as a legacy host and once as physical outputs, the servo test under `HandleMechanics` 1 and 2, and two sessions in one process.

- [ ] **Step 1: Write the Domino's check**

```sh
cat > tests/pinheck/vpx/pinmame_vpx.sh <<'EOF'
#!/bin/sh
# Domino's through libpinmame (spec M10 4.1): display, sound, outputs, switches and mechanics as a host receives them
: "${LIBPINMAME:?set LIBPINMAME to the built libpinmame.so}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
LIBPINMAME=$(realpath "$LIBPINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
S=../../../src
B=build/pinmame
rm -rf $B && mkdir -p $B/boot/roms $B/boot/nvram $B/boot/cfg || exit 2
c++ -std=c++20 -O1 -Wall -Wextra -Werror -I$S/libpinmame host.cpp "$LIBPINMAME" -Wl,-rpath,"$(dirname "$LIBPINMAME")" -pthread -o $B/host || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j boot/roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/boot/roms/dominos.zip || exit 2
launch() {
	name=$1
	shift
	[ -d $B/$name ] || cp -r $B/boot $B/$name || exit 2
	rm -f $B/$name/*.log $B/$name/*.bin $B/$name/*.raw $B/$name/*.wav
	(cd $B/$name && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out.log PINHECK_FRAME_LOG=$PWD/frames.log PINHECK_WAV=$PWD/capture.wav \
		PINHECK_UART1_LOG=$PWD/uart.log timeout -k 30 3000 ../host "$@" > run.out 2>&1) || { echo "VPX FAIL: $name exited $?"; tail -5 $B/$name/run.out; exit 1; }
}
fail=0
# the first boot writes the NVRAM every other launch starts from
launch boot dominos 1200 .
python3 vpx.py displays $B/boot || fail=1
python3 vpx.py media $B/boot $B/boot/capture.wav || fail=1
# a video clip, then a sound effect, through the plugin message API
PINHECK_UART1_SEND_AT=12 PINHECK_UART1_SEND="[V00LT5]~~~~[F00Z00]~~~[F00IR0]" launch media -p dominos 2100 .
grep -aq "Playing Video" $B/media/uart.log || { echo "VPX FAIL: [V00LT5] not acknowledged"; fail=1; }
python3 vpx.py media $B/media $B/media/capture.wav || fail=1
python3 ../audio/corr.py $B/media/api.wav "$PINHECK_UPDATE_DIR/SFX/_FI/IR0.wav" 22052.59 || fail=1
# console coil commands and the service menu's servo, lamp and RGB tests, the flippers pressed as a table does,
# once as a legacy host sees the outputs and once as physical outputs (Controller.SolMask(2) = 2)
python3 vpx.py plan $B || exit 2
cp $B/switches.txt $B/mech.txt $B/boot/ || exit 2
board() {
	PINHECK_UART1_SEND_AT=$(cat $B/send_at) PINHECK_UART1_SEND_GAP=$(cat $B/send_gap) PINHECK_UART1_SEND="$(cat $B/send)" \
		launch $1 -p -o -x $2 -s switches.txt dominos "$(cat $B/frames)" .
	python3 vpx.py outputs $B/$1 || fail=1
}
board board
board physout -P
# the service menu's servo test turns the Noid: HandleMechanics 1 simulates it, 2 leaves it to the table
launch mech1 -o -m 1 -s mech.txt dominos 1200 .
launch mech2 -o -m 2 -s mech.txt dominos 1200 .
python3 vpx.py mech $B/mech1 $B/mech2 || fail=1
# two sessions in one process, as a table's reset key does: the link log and its packet decoder
PINHECK_RTC=1790683200 PINHECK_LINK_LOG=$PWD/$B/restart/link.log launch restart -R dominos 900 .
python3 vpx.py restart $B/restart || fail=1
[ $fail -eq 0 ] || exit 1
echo "pinmame vpx: ok"
EOF
chmod +x tests/pinheck/vpx/pinmame_vpx.sh
```

- [ ] **Step 2: Run it to see it fail**

```sh
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/vpx/pinmame_vpx.sh 2>&1 | tail -30
```
Expected (about 6 minutes):
```text
VPX FAIL: lamp 50, which does not exist, reads [0, 1]
VPX FAIL: lamp 89, which does not exist, reads [0, 1]
VPX FAIL: lamp 90, which does not exist, reads [0, 1]
VPX FAIL: setting switches that do not exist moved real ones: 15.016666662 W 7=1 8=1 11=1 18=1 21=1 28=1 31=1 48=1 88=1 91=1 98=1
outputs: legacy outputs, 1026186 reads of 318 outputs compared, 32 differ, 11 failures
VPX FAIL: PS37 is not exported
VPX FAIL: PS38 is not exported
VPX FAIL: PS39 is not exported
VPX FAIL: PS40 is not exported
VPX FAIL: PS41 is not exported
VPX FAIL: PS42 is not exported
VPX FAIL: PS43 is not exported
VPX FAIL: PS44 is not exported
VPX FAIL: never seen on: PS37 PS38 PS39 PS40 PS41 PS42 PS43 PS44
VPX FAIL: the plugin lists no solenoids 37-44 (GI_8..15): [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 45, 46, 47, 48, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64]
VPX FAIL: lamp 9, which does not exist, reads [0, 1, 4, 5, 8, 29, 36, 47, 76, 137, 149, 188, 199, 219, 226, 236, 239, 240, 241, 242, 243, 244, 246]
VPX FAIL: lamp 19, which does not exist, reads [0, 1, 2, 3, 4, 5, 6, 7, 8, 22, 24, 25, 28, 30, 31, 39, 41, 43, 44, 45, 46, 48, 51, 53, 64, 76, 125, 130, 134, 141, 153, 155, 157, 177, 178, 187, 188, 194, 196, 201, 202, 207, 212, 213, 220, 225, 226, 234, 235, 236, 239, 240, 241, 242, 243, 245, 246, 248, 250, 255]
VPX FAIL: lamp 20, which does not exist, reads [0, 1, 2, 3, 4, 5, 6, 7, 8, 22, 24, 25, 26, 28, 29, 35, 36, 40, 41, 42, 43, 44, 46, 47, 48, 51, 59, 64, 76, 125, 130, 132, 134, 135, 136, 137, 142, 149, 153, 157, 177, 178, 185, 187, 188, 194, 196, 199, 201, 207, 213, 216, 217, 220, 226, 229, 234, 235, 238, 239, 240, 241, 242, 243, 244, 245, 246, 250, 255]
VPX FAIL: lamp 29, which does not exist, reads [0, 1, 4, 7, 9, 13, 16, 17, 26, 27, 28, 29, 32, 33, 36, 43, 51, 52, 54, 58, 59, 63, 66, 68, 70, 71, 76, 84, 92, 94, 95, 97, 98, 111, 113, 126, 127, 128, 129, 130, 135, 136, 137, 138, 139, 140, 148, 150, 164, 166, 167, 169, 175, 182, 184, 190, 191, 192, 195, 196, 198, 199, 204, 206, 208, 209, 213, 219, 220, 226, 227, 228, 230, 232, 240, 241, 242, 243, 244, 246, 248, 252, 253, 255]
VPX FAIL: lamp 30, which does not exist, reads [0, 1, 4, 5, 8, 25, 31, 50, 76, 137, 155, 188, 204, 211, 226, 236, 239, 240, 241, 242, 243, 244, 246]
VPX FAIL: lamp 50, which does not exist, reads [0, 1, 4, 26, 76, 136, 172, 226, 240, 241, 242, 243, 246]
VPX FAIL: lamp 89, which does not exist, reads [0, 1, 7, 22, 40, 43, 184, 195, 202, 249, 251, 252, 254, 255]
VPX FAIL: lamp 90, which does not exist, reads [0, 4, 8, 29, 62, 147, 188, 229, 240, 241, 242, 243, 244, 246]
VPX FAIL: setting switches that do not exist moved real ones: 15.016666662 W 7=1 8=1 11=1 18=1 21=1 28=1 31=1 48=1 88=1 91=1 98=1
outputs: physical outputs, 1026186 reads of 326 outputs compared, 552 differ, 19 failures
mech: mech1, Noid Home changed 5 times
mech: mech2, Noid Home changed 5 times
VPX FAIL: Noid Home moved with HandleMechanics mech2
restart: 57 + 57 packets
VPX FAIL: at the first session's end the link log held 0 of its 3705 bytes
```

- [ ] **Step 3: The driver**

The 128×32 panel to libpinmame, numbers that name nothing, the link log and decoder, the mech mask, GI 8–15 in the plugin's list, and the scale check:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.h', [
    ('''/* the 128x32 panel is drawn 2x2 per dot, like a DMD, and that is also the exported frame */
#define PINHECK_VIDEO_SCALE 2
''',
     '''/* the 128x32 panel is drawn 2x2 per dot in the PinMAME and VPinMAME windows; libpinmame hosts
   draw their own dots, so they get the panel as sent */
#ifdef LIBPINMAME
#define PINHECK_VIDEO_SCALE 1
#else
#define PINHECK_VIDEO_SCALE 2
#endif
'''),
])

edit('src/wpc/pinheck.c', [
    ('''/* switch n (0-63) is PinMAME (n/8+1)*10 + n%8+1, lamps likewise; cabinet inputs are columns 0 and 9 */
static int pinheck_sw2m(int no) { return (no / 10) * 8 + no % 10 - 1; }
''',
     '''/* switch n (0-63) is PinMAME (n/8+1)*10 + n%8+1, lamps likewise; cabinet inputs are columns 0 and 9.
   A switch number the matrix does not have goes to column 15 row 8, which nothing reads; a lamp
   number the matrix does not have to -1, which vp_getLamp rejects */
#define PINHECK_NOSUCH (15 * 8 + 7)
static int pinheck_2m(int no, int cols, int none) { return no > 0 && no / 10 < cols && no % 10 >= 1 && no % 10 <= 8 ? (no / 10) * 8 + no % 10 - 1 : none; }
static int pinheck_sw2m(int no) { return pinheck_2m(no, CORE_STDSWCOLS, PINHECK_NOSUCH); }
static int pinheck_lamp2m(int no) { return pinheck_2m(no, CORE_CUSTLAMPCOL + 2, -1); }
'''),
    ('''static FILE *link_log;
static struct { int clk, nbits; uint64_t last, gap; uint8_t b[16]; } lnk;
''',
     '''static FILE *link_log;
static int link_opened;
static struct { int clk, nbits; uint64_t last, gap; uint8_t b[16]; } lnk;
'''),
    ('''	if (!link_log && getenv("PINHECK_LINK_LOG")) link_log = fopen(getenv("PINHECK_LINK_LOG"), "w");
''',
     '''	if (!link_log && getenv("PINHECK_LINK_LOG")) link_log = fopen(getenv("PINHECK_LINK_LOG"), link_opened ? "a" : "w");
	link_opened = 1;
'''),
    ('''	pinheck_disp_reset();
	cat24m01_init(&u13, u13mem, 0);
''',
     '''	pinheck_disp_reset();
	memset(&lnk, 0, sizeof(lnk));
	cat24m01_init(&u13, u13mem, 0);
'''),
    ('''	if (time_log) fclose(time_log);
	time_log = NULL;
}
''',
     '''	if (time_log) fclose(time_log);
	time_log = NULL;
	if (link_log) fclose(link_log);
	link_log = NULL;
}
'''),
    ('''	MDRV_LAMP_CONV(pinheck_sw2m, pinheck_m2sw)
''',
     '''	MDRV_LAMP_CONV(pinheck_lamp2m, pinheck_m2sw)
'''),
])

edit('src/wpc/sims/pinheck/dominos.c', [
    ('''/* the Noid turns while its continuous-rotation servo gets pulses away from 1.5 ms */
static void dominos_handleMech(int mech) {
  int i, us = pinheck_servo(0), bank = pinheck_servo(1);
  (void)mech;
  if (bank) locals.bankDown = bank > 1500;
  for (i = 1; i <= 24; i++)
    locals.since[i] = sol(i) ? 0 : locals.since[i] < 10000 ? locals.since[i] + 1 : 10000;
''',
     '''/* HandleMechanics bit 0: the Noid, which turns while its continuous-rotation servo gets pulses away
   from 1.5 ms and closes Noid Home; bit 1: the target bank, down while servo 1 is above 1.5 ms */
static void dominos_handleMech(int mech) {
  int i, us = pinheck_servo(0), bank = pinheck_servo(1);
  for (i = 1; i <= 24; i++)
    locals.since[i] = sol(i) ? 0 : locals.since[i] < 10000 ? locals.since[i] + 1 : 10000;
  if ((mech & 0x02) && bank) locals.bankDown = bank > 1500;
  if (!(mech & 0x01)) return;
'''),
])

edit('src/libpinmame/libpinmame.cpp', [
    ('''         // 37..44, S11, SAM, SPA: extension board with 8 outputs (stored in 0xFF00 of solenoids2)
         else if (core_gameData->gen & (GEN_ALLS11 | GEN_SAM | GEN_SPA))
         {
            for (uint16_t i = 37; i <= 44; i++)
               addPhysSol(
                  fmtString("%s Ext Output #%d", (core_gameData->gen & GEN_ALLS11) ? "S11" : (core_gameData->gen & GEN_SAM) ? "SAM" : "SPA", i - 36),
''',
     '''         // 37..44, S11, SAM, SPA: extension board with 8 outputs; pinHeck: GI 8..15 (stored in 0xFF00 of solenoids2)
         else if (core_gameData->gen & (GEN_ALLS11 | GEN_SAM | GEN_SPA | GEN_PINHECK))
         {
            for (uint16_t i = 37; i <= 44; i++)
               addPhysSol(
                  (core_gameData->gen & GEN_PINHECK) ? fmtString("pinHeck GI #%d", i - 29)
                     : fmtString("%s Ext Output #%d", (core_gameData->gen & GEN_ALLS11) ? "S11" : (core_gameData->gen & GEN_SAM) ? "SAM" : "SPA", i - 36),
'''),
])

edit('tests/pinheck/vpx/check.sh', [
    ('mkdir -p $B || exit 2\nfail=0\n',
     'mkdir -p $B || exit 2\nfail=0\nscale() { cc -E -dM -x c "$@" -I$S -I$S/wpc -I$S/unix -I$S/unix/sysdep $S/wpc/pinheck.h | sed -n \'s/^#define PINHECK_VIDEO_SCALE //p\'; }\nif [ "$(scale)" = 2 ] && [ "$(scale -DLIBPINMAME)" = 1 ]; then\n\techo "scale: 2x2 dots in PinMAME and VPinMAME windows, the 128x32 panel as sent to libpinmame hosts"\nelse\n\techo "SCALE FAIL: PINHECK_VIDEO_SCALE is $(scale) without LIBPINMAME and $(scale -DLIBPINMAME) with it"; fail=$((fail + 1))\nfi\n'),
])
EOF
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log; grep -c 'warning:.*\(pinheck\|libpinmame.cpp\|dominos.c\)' build/libpinmame-build.log
```
Expected:
```text
[100%] Built target pinmame_test
0
```

- [ ] **Step 4: Run the checks to see them pass**

```sh
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 timeout -k 30 900 tests/pinheck/vpx/check.sh 2>&1 | tail -9
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/vpx/pinmame_vpx.sh 2>&1 | tail -16
```
Expected:
```text
scale: 2x2 dots in PinMAME and VPinMAME windows, the 128x32 panel as sent to libpinmame hosts
displays: pb_l5 announces 9 display(s), indices [0, 1, 2, 3, 4, 5, 6, 7, 8], updates [180, 180, 180, 180, 180, 180, 180, 180, 180]
displays: tz_94h announces 1 display(s), indices [0], updates [180]
displays: babypac announces 1 display(s), indices [0], updates [180]
probe: pb_l5 X0=[0] X-1=[0] X100000=[0] Y0=[0] Y-1=[0] Y100000=[0]
probe: tz_94h X0=[0] X-1=[0] X100000=[0] Y0=[0] Y-1=[0] Y100000=[0]
probe: babypac X0=[0] X-1=[0] X100000=[0] Y0=[0] Y-1=[0] Y100000=[0]
vpx: 0 failed
displays: boot announces 1 display(s), indices [0], updates [1200]
media: callback path: 7 frames, each a decoded frame, in order; the frame log has 7 distinct frames
media: 1764000 API samples, 1764002 captured, largest difference 1, 3926 loud samples
media: callback path: 184 frames, each a decoded frame, in order; the frame log has 185 distinct frames
media: plugin path: 184 frames, each a decoded frame, in order; the frame log has 185 distinct frames
media: 3087000 API samples, 3087002 captured, largest difference 1, 227693 loud samples
corr: L 0.9991 R 0.9991, swapped L~R -0.0158 R~L -0.0160, source L/R -0.0160, at 19.0086 s (215418 samples at 44100 Hz, 44100 per emulated second, source played at 22052.59 Hz)
outputs: legacy outputs, 1009650 reads of 318 outputs compared, 32 differ, 0 failures
outputs: physical outputs, 1035050 reads of 326 outputs compared, 552 differ, 0 failures
mech: mech1, Noid Home changed 5 times
mech: mech2, Noid Home changed 0 times
restart: 57 packets in the first session, 57 in the second, the first 40 of each equal
pinmame vpx: ok
```

- [ ] **Step 5: Commit**

```sh
git add src/wpc/pinheck.h src/wpc/pinheck.c src/wpc/sims/pinheck/dominos.c src/libpinmame/libpinmame.cpp tests/pinheck/vpx/check.sh tests/pinheck/vpx/pinmame_vpx.sh
git commit -q -m "pinheck: 128x32 frames to libpinmame, no aliases for lamp and switch numbers, HandleMechanics mask, GI 8-15 in the plugin outputs, link log closed at stop; Domino's through libpinmame" && git log --oneline -1 | cut -c10-
```
Expected:
```text
pinheck: 128x32 frames to libpinmame, no aliases for lamp and switch numbers, HandleMechanics mask, GI 8-15 in the plugin outputs, link log closed at stop; Domino's through libpinmame
```

---
### Task 3: Names

**Files:**
- Create: `src/wpc/pinheck_names.h`, `tests/pinheck/vpx/names.py`
- Modify: `tests/pinheck/vpx/check.sh`

**Interfaces:**
- Consumes: the driver's numbering (`PINHECK_SOL_GI0`, `PINHECK_SOL_RGB`, `PINHECK_SOL_SRV`, `PINHECK_SOL_EXT`, `PINHECK_LAMP_ST` in `pinheck.c`, `PINHECK_SWLFLIP`/`PINHECK_SWRFLIP` in `pinheck.h`) and the simulator's constants in `sims/pinheck/dominos.c`.
- Produces: `pinheck_dominos_switch_names[]`, `pinheck_dominos_lamp_names[]`, `pinheck_dominos_solenoid_names[]` (`pinheck_name_t { int num; const char *id; const char *name; }`, ended by `{ 0 }`); `names.py check SRC` and `names.py vbs SRC` (the system script `pinheck.vbs` on stdout); `read(src)` → `{'switch'|'lamp'|'solenoid': [(num, id, name)]}` for `make_table.py`.

- [ ] **Step 1: Write the check and the generator**

```sh
cat > tests/pinheck/vpx/names.py <<'EOF'
#!/usr/bin/env python3
"""Domino's names (src/wpc/pinheck_names.h) against the driver's numbering, and the pinHeck system script for VPX.
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
        out[t] = [(int(n), i, name) for n, i, name in re.findall(r'\{\s*(\d+),\s*"(\w+)",\s*"([^"]*)"\s*\}', body)]
    return out


def define(path, name):
    m = re.search(r'#define %s\s+(\d+)' % name, open(path).read())
    return int(m.group(1))


def check(src):
    names, fails = read(src), []
    matrix = [c * 10 + r for c in range(1, 9) for r in range(1, 9)]
    valid = {'switch': set(range(1, 9)) | set(matrix) | set(range(91, 99)) | {112, 114},
             'lamp': set(matrix) | {91},
             'solenoid': set(range(1, 33)) | set(range(37, 45)) | set(range(51, 65))}
    ids, lower = {}, set()
    for t in TABLES:
        nums = [n for n, _, _ in names[t]]
        if len(set(nums)) != len(nums):
            fails.append('%s numbers repeat' % t)
        for n, i, name in names[t]:
            if n not in valid[t]:
                fails.append('%s %d (%s) is no %s the driver has' % (t, n, name, t))
            if i.lower() in lower:
                fails.append('id %s repeats (VBScript ignores case)' % i)
            lower.add(i.lower())
            ids[i] = n
    sim = dict((i, int(n)) for i, n in re.findall(r'#define (s\w+)\s+(\d+)', open(src + '/wpc/sims/pinheck/dominos.c').read()))
    for i, n in sim.items():
        if ids.get(i) != n:
            fails.append('dominos.c has %s = %d, pinheck_names.h %s' % (i, n, ids.get(i)))
    drv = src + '/wpc/pinheck.c'
    lamp = define(drv, 'PINHECK_LAMP_ST')
    want = {'sGI0': define(drv, 'PINHECK_SOL_GI0') + 1, 'sGI8': 37, 'sRGB1R': define(drv, 'PINHECK_SOL_RGB') + 1,
            'sNoid': define(drv, 'PINHECK_SOL_SRV') + 1, 'sExtR': define(drv, 'PINHECK_SOL_EXT') + 1,
            'lStart': (lamp // 8 + 1) * 10 + lamp % 8 + 1, 'swLFlip': define(src + '/wpc/pinheck.h', 'PINHECK_SWLFLIP'),
            'swRFlip': define(src + '/wpc/pinheck.h', 'PINHECK_SWRFLIP')}
    for i, n in want.items():
        if ids.get(i) != n:
            fails.append('%s is %s, the driver has %d' % (i, ids.get(i), n))
    for f in fails:
        print('NAMES FAIL: ' + f)
    print('names: %d switches, %d lamps, %d solenoid outputs; %d sim constants agree' % (
        len(names['switch']), len(names['lamp']), len(names['solenoid']), len(sim)))
    return 1 if fails else 0


HANDLERS = r'''
' Keyboard handlers: flippers through PinMAME's flipper column; Enter 0, Back 7, User 9 as in PinMAME
Function vpmKeyDown(ByVal keycode)
	vpmKeyDown = True
	With Controller
		Select Case keycode
			Case LeftFlipperKey  .Switch(swLLFlip) = True : vpmKeyDown = False
			Case RightFlipperKey .Switch(swLRFlip) = True : vpmKeyDown = False
			Case keyInsertCoin1  vpmTimer.PulseSw swCoin
			Case keyInsertCoin2  vpmTimer.PulseSw swCoin
			Case keyInsertCoin3  vpmTimer.PulseSw swCoin
			Case StartGameKey    .Switch(swStart) = True
			Case keyEnter        .Switch(swEnter) = True
			Case keyCancel       .Switch(swBack) = True
			Case keyUp           .Switch(swUser) = True
			Case keyCoinDoor     .Switch(swCoinDoor) = Not .Switch(swCoinDoor)
			Case keyBangBack     vpmNudge.DoMechTilt
			Case keyVPMVolume    vpmVol
			Case Else            vpmKeyDown = False
		End Select
	End With
End Function

Function vpmKeyUp(ByVal keycode)
	vpmKeyUp = True
	With Controller
		Select Case keycode
			Case LeftFlipperKey  .Switch(swLLFlip) = False : vpmKeyUp = False
			Case RightFlipperKey .Switch(swLRFlip) = False : vpmKeyUp = False
			Case StartGameKey    .Switch(swStart) = False
			Case keyEnter        .Switch(swEnter) = False
			Case keyCancel       .Switch(swBack) = False
			Case keyUp           .Switch(swUser) = False
			Case keyShowOpts     .Pause = True : vpmShowOptions : .Pause = False
			Case keyShowKeys     .Pause = True : vpmShowHelp : .Pause = False
			Case keyReset        .Stop : BeginModal : .Run : vpmTimer.Reset : EndModal
			Case Else            vpmKeyUp = False
		End Select
	End With
End Function
'''

HEAD = r'''
' pinHeck (Spooky Pinball) system script: Domino's Spectacular Pinball Adventure. Generated by
' tests/pinheck/vpx/names.py from src/wpc/pinheck_names.h; do not edit.
Option Explicit
LoadCore
Private Sub LoadCore
	On Error Resume Next
	If VPBuildVersion < 0 Or Err Then
		Dim fso : Set fso = CreateObject("Scripting.FileSystemObject") : Err.Clear
		ExecuteGlobal fso.OpenTextFile("core.vbs", 1).ReadAll    : If Err Then MsgBox "Can't open ""core.vbs""" : Exit Sub
		ExecuteGlobal fso.OpenTextFile("VPMKeys.vbs", 1).ReadAll : If Err Then MsgBox "Can't open ""vpmkeys.vbs""" : Exit Sub
	Else
		ExecuteGlobal GetTextFile("core.vbs")    : If Err Then MsgBox "Can't open ""core.vbs"""    : Exit Sub
		ExecuteGlobal GetTextFile("VPMKeys.vbs") : If Err Then MsgBox "Can't open ""vpmkeys.vbs""" : Exit Sub
	End If
End Sub

vpmSystemHelp = "pinHeck keys:" & vbNewLine &_
  vpmKeyName(keyEnter)       & vbTab & "Enter (menu)" & vbNewLine &_
  vpmKeyName(keyCancel)      & vbTab & "Back"         & vbNewLine &_
  vpmKeyName(keyUp)          & vbTab & "User"         & vbNewLine &_
  vpmKeyName(keyCoinDoor)    & vbTab & "Coin Door"
'''


def vbs(src):
    names = read(src)
    out = [HEAD.lstrip('\n')]
    for t in TABLES:
        out.append("\n' %s\n" % {'switch': 'Switches', 'lamp': 'Lamps', 'solenoid': 'Solenoid outputs'}[t])
        out += ['Const %-20s = %3d  \' %s\n' % (i, n, name) for n, i, name in names[t]]
    out.append(HANDLERS)
    sys.stdout.write(''.join(out))
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('check', 'vbs'):
        sys.exit(__doc__)
    sys.exit((check if sys.argv[1] == 'check' else vbs)(sys.argv[2]))
EOF
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/vpx/check.sh', [
    ("# VPX support without the firmware: (with LIBPINMAME and PINMAME_ROMS) libpinmame's display list and getters on a\n# segment, a DMD and a video game",
     "# VPX support without the firmware: Domino's names, the pinHeck sources in every build list, the display size\n# libpinmame exports, and (with LIBPINMAME and PINMAME_ROMS) libpinmame's display list and getters on a\n# segment, a DMD and a video game"),
    ('mkdir -p $B || exit 2\nfail=0\n',
     'mkdir -p $B || exit 2\nfail=0\npython3 names.py check $S || fail=$((fail + 1))\nprintf \'#include "pinheck_names.h"\\nint main(void) { return pinheck_dominos_switch_names[0].num != 1; }\\n\' > $B/names.c\ncc -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$S/wpc $B/names.c -o $B/names && ./$B/names || { echo "NAMES FAIL: pinheck_names.h"; fail=$((fail + 1)); }\nsrcs() { grep -o \'src/\\(wpc/pinheck\\|wpc/sims/pinheck\\|cpu/mips32\\|cpu/pic32mx\\|cpu/p8x32a\\)[^ )"]*\' "$1" | sort -u; }\nsrcs ../../../cmake/libpinmame/CMakeLists.txt > $B/srcs.ref\nn=0\nfor f in ../../../cmake/*/CMakeLists*.txt; do\n\tcase $f in *instvpm*) continue ;; esac\n\tsrcs "$f" | cmp -s - $B/srcs.ref || { echo "BUILD FAIL: $f lists other pinHeck sources than cmake/libpinmame"; fail=$((fail + 1)); }\n\tn=$((n + 1))\ndone\necho "builds: $n build lists carry the same $(wc -l < $B/srcs.ref) pinHeck sources"\n'),
])
EOF
```

- [ ] **Step 2: Run it to see it fail**

```sh
nice -n 15 tests/pinheck/vpx/check.sh 2>&1 | head -3
```
Expected:
```text
Traceback (most recent call last):
  File "/code/spooky_domino/work/wt-m10r/tests/pinheck/vpx/names.py", line 144, in <module>
    sys.exit((check if sys.argv[1] == 'check' else vbs)(sys.argv[2]))
```
(the traceback ends in `FileNotFoundError` for `src/wpc/pinheck_names.h`.)

- [ ] **Step 3: The header**

```sh
cat > src/wpc/pinheck_names.h <<'EOF'
/* Domino's Spectacular Pinball Adventure (pinHeck): switch, lamp and output names for table scripts.

   Sources: Dominos-Switch-Matrix.pdf, Dominos-Lamp-Matrix.pdf and Spooky_Pinball_Domino's_Solenoid_List.pdf
   (Ken Layton, 2019), with the numbering of the pinHeck driver (src/wpc/pinheck.c):
     matrix switch or lamp n (0-63)  (n / 8 + 1) * 10 + n % 8 + 1
     cabinet switch n                n (1-8), n + 82 (9-15)
     start button lamp               91
     coil n (0-23)                   solenoid n + 1
     GI_0..7, GI_8..15               solenoids 25-32, 37-44
     on-board RGB left R, G, B, right R, G, B    51-56 (0-255)
     servos 0-4                      57-61 (0-255 = 1.0-2.0 ms pulse)
     external RGB LED R, G, B        62-64 (0-255)
   A table presses the flipper buttons through PinMAME's flipper column, 114 (left) and 112 (right),
   which PinMAME copies to cabinet switches 4 and 3 every frame. Numbers not listed are unused.
   HandleMechanics bit 0 simulates the Noid (servo 0, closing Noid Home 58), bit 1 the target bank (servo 1);
   a table that moves them itself clears the bit. */
#ifndef PINHECK_NAMES_H
#define PINHECK_NAMES_H

typedef struct { int num; const char *id; const char *name; } pinheck_name_t;

static const pinheck_name_t pinheck_dominos_switch_names[] = {
  {   1, "swCoinDoor",  "Coin Door (closed)" },
  {   2, "swUser",      "User" },
  {   3, "swRFlip",     "Right Flipper" },
  {   4, "swLFlip",     "Left Flipper" },
  {   5, "swBack",      "Back" },
  {   6, "swEnter",     "Enter (menu)" },
  {   7, "swCoin",      "Coin Mech" },
  {   8, "swTilt",      "Tilt" },
  {  11, "swShooter",   "Shooter Lane" },
  {  12, "swTrough1",   "Trough Ball 1" },
  {  13, "swTrough2",   "Trough Ball 2" },
  {  14, "swTrough3",   "Trough Ball 3" },
  {  15, "swRFlipEOS",  "Right Flipper EOS" },
  {  16, "swRSling",    "Right Sling" },
  {  17, "swRInlane",   "Right Inlane" },
  {  18, "swROutlane",  "Right Outlane" },
  {  21, "swLFlipEOS",  "Left Flipper EOS" },
  {  22, "swLSling",    "Left Sling" },
  {  23, "swLInlane",   "Left Inlane" },
  {  24, "swLOutlane",  "Left Outlane" },
  {  25, "swLScoop",    "Left Scoop" },
  {  26, "swBankR",     "Noid Bank Right" },
  {  27, "swBankM",     "Noid Bank Middle" },
  {  28, "swBankL",     "Noid Bank Left" },
  {  31, "swDelivery",  "Target Delivery" },
  {  32, "swQuality",   "Target Quality Check" },
  {  33, "swBake",      "Target Bake" },
  {  34, "swPrepare",   "Target Prepare" },
  {  35, "swOrder",     "Target Order Placed" },
  {  36, "swROrbit",    "Right Orbit" },
  {  37, "swRNoidOrb",  "Right Noid Orbit" },
  {  38, "swLNoidOrb",  "Left Noid Orbit" },
  {  41, "swStarLane",  "Star Lane" },
  {  42, "sw5Lane",     "5 Lane" },
  {  43, "swRPop",      "Right Pop Bumper" },
  {  44, "swLowerPop",  "Lower Pop Bumper" },
  {  45, "swLPop",      "Left Pop Bumper" },
  {  46, "swLOrbit",    "Left Orbit" },
  {  47, "swSpinner",   "Spinner" },
  {  48, "swRScoop",    "Right Scoop" },
  {  58, "swNoidHome",  "Noid Home" },
  {  94, "swStart",     "Start Button" },
  {  95, "swRampOpto",  "Opto (Center Ramp)" },
  {  96, "swOvenOpto",  "Opto (Oven Ramp)" },
  { 112, "swLRFlip",    "Right Flipper button (reaches switch 3)" },
  { 114, "swLLFlip",    "Left Flipper button (reaches switch 4)" },
  { 0 }
};

static const pinheck_name_t pinheck_dominos_lamp_names[] = {
  {  11, "lLetterO",           "O" },
  {  12, "lLetterV",           "V" },
  {  13, "lMystery",           "Mystery" },
  {  14, "lStartCareer",       "Start Career" },
  {  15, "lGoldFranny",        "Gold Franny" },
  {  16, "lFranchisee",        "Franchisee" },
  {  17, "lManager",           "Manager" },
  {  18, "lDriver",            "Driver" },
  {  21, "lOrderAgain",        "Order Again" },
  {  22, "lMakePizza",         "Make Pizza" },
  {  23, "lLostTopping",       "Lost Topping" },
  {  24, "lPizzaDispatch",     "Pizza Dispatch" },
  {  25, "lMegaWeek",          "Mega Week" },
  {  26, "lHandleTheRush",     "Handle The Rush" },
  {  27, "lLetterE",           "E" },
  {  28, "lLetterN",           "N" },
  {  31, "lLDominoOrbit",      "Left Domino Orbit" },
  {  32, "lLMakePizzaOrbit",   "Left Make Pizza Orbit" },
  {  33, "lLostToppingL",      "Lost Topping Left (Noid Orbit)" },
  {  34, "lTarget1",           "Target 1" },
  {  35, "lTarget2",           "Target 2" },
  {  36, "lTarget3",           "Target 3" },
  {  37, "lBattleNoidN",       "Battle Noid N" },
  {  41, "lLostToppingR",      "Lost Topping Right (Noid Orbit)" },
  {  42, "lRampPizzaDispatch", "Ramp Pizza Dispatch" },
  {  43, "lExtraBall",         "Extra Ball" },
  {  44, "lRampDomino",        "Ramp Domino" },
  {  45, "lOvenRampRush",      "Oven Ramp Rush" },
  {  46, "lOvenRampJackpot",   "Oven Ramp Jackpot" },
  {  47, "lOvenRampMegaWeek",  "Oven Ramp Mega Week" },
  {  48, "lTrackerGreen",      "Pizza Tracker Green Side" },
  {  51, "lDominoDotTop",      "Domino Dot Top" },
  {  52, "lDominoDotMiddle",   "Domino Dot Middle" },
  {  53, "lDominoDotBottomL",  "Domino Dot Bottom Left" },
  {  54, "lPizzaWars",         "Pizza Wars" },
  {  55, "lGlobalConquest",    "Global Conquest" },
  {  56, "lFastestPizzaMaker", "Fastest Pizza Maker" },
  {  57, "l5Lane",             "5 Lane" },
  {  58, "lStarLane",          "Star Lane" },
  {  61, "lOvenPizzaScoop",    "Oven Pizza Scoop" },
  {  62, "lMakePizzasROrbit",  "Make Pizzas Right Orbit" },
  {  63, "lOrderPlaced",       "Order Placed" },
  {  64, "lPrepare",           "Prepare" },
  {  65, "lBake",              "Bake" },
  {  66, "lQualityCheck",      "Quality Check" },
  {  67, "lDelivery",          "Delivery" },
  {  68, "lTrackerBlue",       "Pizza Tracker Blue Side" },
  {  91, "lStart",             "Start Button" },
  { 0 }
};

/* solenoid outputs: coils, GI strings, RGB channels and servos */
static const pinheck_name_t pinheck_dominos_solenoid_names[] = {
  {  1, "sKnocker",    "Knocker (optional)" },
  {  2, "sShaker",     "Shaker Motor (optional)" },
  {  3, "sLowerPop",   "Lower Pop Bumper" },
  {  4, "sLPop",       "Left Pop Bumper" },
  {  5, "sRPop",       "Right Pop Bumper" },
  {  6, "sMagnet",     "Magnet Coil" },
  {  7, "sPost",       "Up Post" },
  {  9, "sLScoop",     "Left Scoop" },
  { 10, "sLFlipHigh",  "Left Flipper High Center (power)" },
  { 11, "sLSling",     "Left Slingshot" },
  { 12, "sLFlipLow",   "Left Flipper Low (hold)" },
  { 13, "sRScoop",     "Right Scoop" },
  { 17, "sLaunch",     "Auto Launcher" },
  { 18, "sLoad",       "Ball Trough" },
  { 19, "sRFlipLow",   "Right Flipper Low (hold)" },
  { 20, "sRSling",     "Right Slingshot" },
  { 21, "sRFlipHigh",  "Right Flipper High Center (power)" },
  { 25, "sGI0",        "GI_0 (backbox)" },
  { 26, "sGI1",        "GI_1 (backbox)" },
  { 27, "sGI2",        "GI_2 (backbox)" },
  { 28, "sGI3",        "GI_3 (backbox)" },
  { 29, "sGI4",        "GI_4 (backbox)" },
  { 30, "sGI5",        "GI_5 (backbox)" },
  { 31, "sGI6",        "GI_6 (backbox)" },
  { 32, "sGI7",        "GI_7 (backbox)" },
  { 37, "sGI8",        "GI_8 (playfield)" },
  { 38, "sGI9",        "GI_9 (playfield)" },
  { 39, "sGI10",       "GI_10 (playfield)" },
  { 40, "sGI11",       "GI_11 (playfield)" },
  { 41, "sGI12",       "GI_12 (playfield)" },
  { 42, "sGI13",       "GI_13 (playfield)" },
  { 43, "sGI14",       "GI_14 (playfield)" },
  { 44, "sGI15",       "GI_15 (playfield)" },
  { 51, "sRGB1R",      "RGB1 (left) red" },
  { 52, "sRGB1G",      "RGB1 (left) green" },
  { 53, "sRGB1B",      "RGB1 (left) blue" },
  { 54, "sRGB2R",      "RGB2 (right) red" },
  { 55, "sRGB2G",      "RGB2 (right) green" },
  { 56, "sRGB2B",      "RGB2 (right) blue" },
  { 57, "sNoid",       "Servo 0: Noid (continuous rotation)" },
  { 58, "sBank",       "Servo 1: target bank" },
  { 59, "sServo2",     "Servo 2" },
  { 60, "sServo3",     "Servo 3" },
  { 61, "sServo4",     "Servo 4" },
  { 62, "sExtR",       "External RGB LED red" },
  { 63, "sExtG",       "External RGB LED green" },
  { 64, "sExtB",       "External RGB LED blue" },
  { 0 }
};

#endif
EOF
```

- [ ] **Step 4: Run it to see it pass**

```sh
nice -n 15 tests/pinheck/vpx/check.sh 2>&1 | tail -4
python3 tests/pinheck/vpx/names.py vbs src | grep -c '^Const'
```
Expected:
```text
builds: 10 build lists carry the same 33 pinHeck sources
scale: 2x2 dots in PinMAME and VPinMAME windows, the 128x32 panel as sent to libpinmame hosts
vpx: LIBPINMAME or PINMAME_ROMS not set: libpinmame's display list and getters not run
vpx: 0 failed
141
```

- [ ] **Step 5: Commit**

```sh
git add src/wpc/pinheck_names.h tests/pinheck/vpx/names.py tests/pinheck/vpx/check.sh
git commit -q -m "pinheck_names.h: Domino's switch, lamp and output names; names.py checks them and writes pinheck.vbs" && git log --oneline -1 | cut -c10-
```
Expected:
```text
pinheck_names.h: Domino's switch, lamp and output names; names.py checks them and writes pinheck.vbs
```

---
### Task 4: The core's solenoid log in the window

**Files:**
- Modify: `src/wpc/pinheck.c` (`pinheck_video`), `tests/pinheck/display/pinmame_display.sh`, `tests/pinheck/display/render.py`

**Interfaces:**
- Consumes: Plan 6's display check and `render.py` (the snapshot rules), the core's `core_textOutf(max_x - 12 * 8, 0, …)` solenoid log.
- Produces: `render.py --sol-log` (the panel's first text row, right 96 pixels, must hold non-black pixels); a second snapshot at the end of the display check's second launch.

- [ ] **Step 1: Build sdl3pinmame**

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > build/sdl3-cfg.log 2>&1; tail -1 build/sdl3-cfg.log
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3-build.log 2>&1; tail -1 build/sdl3-build.log
```
Expected (about 20 minutes):
```text
-- Build files have been written to: /code/spooky_domino/work/wt-m10r/build/sdl3pinmame
[100%] Built target sdl3pinmame
```

- [ ] **Step 2: The snapshot check**

A second snapshot at the end of launch 2, and `render.py --sol-log`:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/pinmame_display.sh', [
    ('''rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg $B/snap || exit 2
''',
     '''rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg $B/snap $B/snap2 || exit 2
'''),
    ('''PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND="[V00$CLIP]" launch 2 900
''',
     '''# and at the end of launch 2, when attract mode has switched GI on and off: the core's solenoid log must show
echo "890 tap 2 KEYCODE_F12" > $B/snap2.ks
PINHECK_UART1_SEND_AT=$SEND_AT PINHECK_UART1_SEND="[V00$CLIP]" launch 2 900 "-key_script $PWD/$B/snap2.ks -snapshot_directory $PWD/$B/snap2"
'''),
    ('''python3 frames.py $B/frames2.bin --after "$SEND_AT" --vid "$PINHECK_UPDATE_DIR/DMD/_D$dir/$CLIP.VID" || fail=1
''',
     '''python3 frames.py $B/frames2.bin --after "$SEND_AT" --vid "$PINHECK_UPDATE_DIR/DMD/_D$dir/$CLIP.VID" || fail=1
python3 render.py $B/snap2/dominos.png $B/frames2.bin --sol-log --config "$(grep '^display: config ' $B/prop2.log | tail -1 | cut -d' ' -f3-)" || fail=1
'''),
])

edit('tests/pinheck/display/render.py', [
    ('''own pens, never an RGB332 frame colour, and a game simulator's ball list and plunger bar to its
right (from x = 130, in white: sim.c's sim_draw), which are not judged."""''',
     '''own pens, never an RGB332 frame colour, and a game simulator's ball list and plunger bar to its
right (from x = 130, in white: sim.c's sim_draw), which are not judged. With --sol-log the core's
solenoid log (the last four solenoids switched on, drawn at the panel's top right) must be on screen."""'''),
    ('''    ap.add_argument('--config', help='the last config packet (hex bytes) before the snapshot; none = the exact look')
''',
     '''    ap.add_argument('--config', help='the last config packet (hex bytes) before the snapshot; none = the exact look')
    ap.add_argument('--sol-log', action='store_true', help='the core\\'s solenoid log must show')
'''),
    ('''        print('render: %dx%d visible, black outside the frame apart from the core panel (%d pens, none a frame colour)' % (w, h, len(pens)))
''',
     '''        print('render: %dx%d visible, black outside the frame apart from the core panel (%d pens, none a frame colour)' % (w, h, len(pens)))
    if a.sol_log:
        # core_textOutf(max_x - 12 * 8, 0, ...): the panel's first text row, 8 pixels high
        lit = sum(1 for y in range(panel, min(h, panel + 8)) for x in range(w - 12 * 8, w) if not near(rows[y][x], (0, 0, 0)))
        if lit:
            print('render: the core\\'s solenoid log shows (%d pixels)' % lit)
        else:
            print('render: FAIL, the core\\'s solenoid log is not on screen')
            fail = 1
'''),
])
EOF
```

- [ ] **Step 3: Run the display check to see it fail**

```sh
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame nice -n 15 ionice -c 3 timeout -k 30 1800 tests/pinheck/display/pinmame_display.sh 2>&1 | tail -4
```
Expected:
```text
frames: LT5.VID: frames 1..33 of 34 shown pixel-exact, contiguous and in order (log frames 1..33)
render: frame region equals decoded frame 7 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (13 pens, none a frame colour)
render: FAIL, the core's solenoid log is not on screen
```

- [ ] **Step 4: Take the clear out, rebuild, run it to see it pass**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
    ('''	/* the core's visible area is larger than the panel: clear it so nothing stale shows */
	fillbitmap(bitmap, get_black_pen(), cliprect);
''',
     '''	(void)cliprect;
'''),
])
EOF
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/sdl3pinmame -j6 > build/sdl3-build.log 2>&1; tail -1 build/sdl3-build.log
SDL3PINMAME=build/sdl3pinmame/sdl3pinmame nice -n 15 ionice -c 3 timeout -k 30 1800 tests/pinheck/display/pinmame_display.sh 2>&1 | tail -5
```
Expected:
```text
[100%] Built target sdl3pinmame
frames: LT5.VID: frames 1..33 of 34 shown pixel-exact, contiguous and in order (log frames 1..33)
render: frame region equals decoded frame 7 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (13 pens, none a frame colour)
render: the core's solenoid log shows (110 pixels)
pinmame display: ok
```

- [ ] **Step 5: Commit**

```sh
git add src/wpc/pinheck.c tests/pinheck/display/pinmame_display.sh tests/pinheck/display/render.py
git commit -q -m "pinheck: leave the core's screen outside the panel alone, so its solenoid log shows" && git log --oneline -1 | cut -c10-
```
Expected:
```text
pinheck: leave the core's screen outside the panel alone, so its solenoid log shows
```

---
### Task 5: The test table and VPX standalone on Linux (outside the repository)

**Files (outside the repository, not committed):**
- Create: `/code/spooky_domino/vpx/make_table.py`, `dominos_test.vbs`, `session.sh`, `run_vpx.sh`, `check_vpx.py`, `CHECKLIST-linux.md`, `CHECKLIST-windows.md`
- Uses: `/code/spooky_domino/vpx/tools/vpxtool/vpxtool` (0.34.8), `/code/spooky_domino/vpx/tools/n5947/vpx/` (VPinballX_GL 10.8.1-5947 linux-x64)

**Interfaces:**
- Consumes: Task 3's `names.py` (`read`, `vbs`), Task 2's libpinmame, the driver's `PINHECK_OUT_LOG`, `PINHECK_UART1_SEND*`, `PINHECK_INSERVICE`.
- Produces: `make_table.py VPXTOOL PINMAME_REPO FONT OUTDIR` → `OUTDIR/dominos_test.vpx`, `OUTDIR/pinheck.vbs`; `run_vpx.sh VPX_DIR LIBPINMAME_SO PINMAME_REPO OUT` printing `vpx standalone: ok`; the table log format `<GameTime s> L|S|K|W <n> <v>`.

The table: a 1600×1000 board seen from above, the display flasher at the top, the 64 matrix lamps and the start lamp as labelled indicators in matrix order, the 16 GI strings, the two on-board RGB LEDs and the external one (coloured by their channels), the five servos and the 24 coils. Labels are drawn into an image from `pinheck_names.h`. The script uses `pinheck.vbs` (flippers 114/112, Enter, Back, User, coin door, coin, start) and maps 29 playfield switches to keys; with `-c1 FILE` it logs every lamp, output and key it receives.

`run_vpx.sh` builds a VPX tree whose PinMAME plugin loads the given libpinmame, a private home with the ROMs, the table, then two headless launches: the first on an empty NVRAM (`PINHECK_INSERVICE=6`, 30 s), the second with the keys and the console's coil commands. `check_vpx.py` compares the table's log with the driver's.

- [ ] **Step 1: The tools**

```sh
T=/code/spooky_domino/vpx/tools
[ -x $T/vpxtool/vpxtool ] || { mkdir -p $T/vpxtool && gh release download v0.34.8 -R francisdb/vpxtool -p 'vpxtool-linux-x86_64-v0.34.8.tar.gz' -D $T && tar xzf $T/vpxtool-linux-x86_64-v0.34.8.tar.gz -C $T/vpxtool; }
[ -x $T/n5947/vpx/VPinballX_GL ] || { mkdir -p $T/n5947/vpx && gh api repos/vpinball/vpinball/actions/artifacts/11088029138/zip > $T/n5947/vpx.tar.gz && tar xzf $T/n5947/vpx.tar.gz -C $T/n5947/vpx; }
$T/vpxtool/vpxtool --version; grep -c 'GetStateSrc:1' $T/n5947/vpx/plugins/pinmame/plugin-pinmame.so
```
Expected:
```text
vpxtool git:v0.34.8
1
```

(vpinball's CI artifacts expire after 90 days. A later master build of VPinballX_GL linux-x64 works the same; the artifact is a `.tar.gz` served without a zip wrapper.)

- [ ] **Step 2: The table generator and script**

```sh
mkdir -p /code/spooky_domino/vpx
cat > /code/spooky_domino/vpx/make_table.py <<'EOF'
#!/usr/bin/env python3
"""Builds the Domino's test table from text: vpxtool's empty table, a playfield image with the labels (ImageMagick),
the indicators generated from the PinMAME repository's src/wpc/pinheck_names.h, and dominos_test.vbs as its script.
  make_table.py VPXTOOL PINMAME_REPO FONT.ttf OUTDIR    writes OUTDIR/dominos_test.vpx and OUTDIR/pinheck.vbs"""
import json
import math
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 1600, 1000                                    # the board, in table units (= pixels of the label image)
DISPLAY = (300, 30, 1300, 280)                       # the 128x32 display: 7.8 units per dot
LAMP_X, LAMP_DX, LAMP_Y, LAMP_DY = 40, 192, 330, 58  # lamp matrix: column c at x, row r at y
ROW_GI, ROW_RGB, ROW_COIL = 812, 870, 935
CAMERA = json.loads(os.environ.get('CAMERA', '{}')) or {
    'bg_view_mode_desktop': 'camera', 'bg_rotation_desktop': 0.0, 'bg_inclination_desktop': 0.0, 'bg_layback_desktop': 0.0,
    'bg_fov_desktop': 50.0, 'bg_offset_x_desktop': 0.0, 'bg_offset_y_desktop': 500.0, 'bg_offset_z_desktop': 1450.0,
    'bg_scale_x_desktop': 1.0, 'bg_scale_y_desktop': 1.0, 'bg_scale_z_desktop': 1.0}


def points(pts):
    return [{'x': x, 'y': y, 'z': 0.0, 'smooth': False, 'is_slingshot': False, 'has_auto_texture': True, 'tex_coord': 0.0,
             'is_locked': False, 'editor_layer_visibility': True} for x, y in pts]


def circle(x, y, r):
    return points([(x + r * math.cos(k * math.pi / 8), y + r * math.sin(k * math.pi / 8)) for k in range(16)])


def light(name, x, y, r, color):
    return {'Light': {'center': {'x': x, 'y': y}, 'height': 0.0, 'falloff_radius': 1.6 * r, 'falloff_power': 2.0,
                      'state_u32': 0, 'state': 0.0, 'color': color, 'color2': color, 'is_timer_enabled': False,
                      'timer_interval': 100, 'blink_pattern': '10', 'image': '', 'blink_interval': 125, 'intensity': 2.0,
                      'transmission_scale': 0.0, 'surface': '', 'name': name, 'is_backglass': False, 'depth_bias': 0.0,
                      'fade_speed_up': 1000.0, 'fade_speed_down': 1000.0, 'is_bulb_light': False, 'is_image_mode': False,
                      'show_bulb_mesh': False, 'has_static_bulb_mesh': False, 'show_reflection_on_ball': False,
                      'mesh_radius': 20.0, 'bulb_modulate_vs_add': 0.9, 'bulb_halo_height': 0.0, 'shadows': 'none',
                      'fader': 'none', 'visible': True, 'drag_points': circle(x, y, r)}}


def timer(name, x, interval):
    return {'Timer': {'center': {'x': x, 'y': H - 20.0}, 'is_timer_enabled': True, 'timer_interval': interval,
                      'name': name, 'backglass': False}}


def display():
    x0, y0, x1, y1 = DISPLAY
    return {'Flasher': {'height': 1.0, 'pos_x': (x0 + x1) / 2, 'pos_y': (y0 + y1) / 2, 'rot_x': 0.0, 'rot_y': 0.0, 'rot_z': 0.0,
                        'color': '#ffffff', 'is_timer_enabled': False, 'timer_interval': 100, 'name': 'Display',
                        'image_a': '', 'image_b': '', 'alpha': 100, 'modulate_vs_add': 0.9, 'is_visible': True,
                        'add_blend': 'none', 'render_mode': 'display', 'render_style': 0, 'glass_roughness': 0.0,
                        'glass_ambient': 0, 'glass_pad_top': 0.0, 'glass_pad_bottom': 0.0, 'glass_pad_left': 0.0,
                        'glass_pad_right': 0.0, 'image_src_link': 'ctrl://default/display', 'display_texture': True,
                        'depth_bias': 0.0, 'image_alignment': 'world', 'filter': 'none', 'filter_amount': 100,
                        'light_map': '', 'backglass': False, 'drag_points': points([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])}}


def board():
    """the labels as an unlit flasher under the indicators"""
    return {'Flasher': {'height': 0.1, 'pos_x': W / 2, 'pos_y': H / 2, 'rot_x': 0.0, 'rot_y': 0.0, 'rot_z': 0.0,
                        'color': '#ffffff', 'is_timer_enabled': False, 'timer_interval': 100, 'name': 'Board',
                        'image_a': 'labels', 'image_b': '', 'alpha': 100, 'modulate_vs_add': 1.0, 'is_visible': True,
                        'add_blend': 'none', 'is_dmd': False, 'render_mode': None, 'render_style': None, 'glass_roughness': None,
                        'glass_ambient': None, 'glass_pad_top': None, 'glass_pad_bottom': None, 'glass_pad_left': None,
                        'glass_pad_right': None, 'image_src_link': None, 'display_texture': True, 'depth_bias': 100.0,
                        'image_alignment': 'wrap', 'filter': 'none', 'filter_amount': 100, 'light_map': '', 'backglass': None,
                        'drag_points': points([(0, 0), (W, 0), (W, H), (0, H)])}}


def layout(names):
    """(item kind, name, JSON, label text, label x, label y) of every part"""
    lamp = dict((n, name) for n, _, name in names['lamp'])
    sol = dict((n, name) for n, _, name in names['solenoid'])
    out = [('Flasher', 'Board', board(), None, 0, 0),
           ('Flasher', 'Display', display(), 'Display 128x32 (libpinmame)', DISPLAY[0], DISPLAY[1] - 4),
           ('Timer', 'PinMAMETimer', timer('PinMAMETimer', 20.0, 1), None, 0, 0),
           ('Timer', 'PulseTimer', timer('PulseTimer', 60.0, 1), None, 0, 0),
           ('Timer', 'SwitchTimer', timer('SwitchTimer', 100.0, 20), None, 0, 0)]

    def add(name, x, y, r, color, text):
        out.append(('Light', name, light(name, x, y, r, color), text, x + r + 6, y))
    for c in range(1, 9):
        for r in range(1, 9):
            n = c * 10 + r
            add('L%d' % n, LAMP_X + 16 + (c - 1) * LAMP_DX, LAMP_Y + (r - 1) * LAMP_DY, 14, '#ffd060', '%d %s' % (n, lamp.get(n, '-')))
    add('L91', LAMP_X + 16, ROW_GI - 58 + 12, 14, '#ffd060', '91 ' + lamp[91])
    for i, n in enumerate(list(range(25, 33)) + list(range(37, 45))):
        add('G%d' % n, LAMP_X + 16 + i * 96, ROW_GI, 13, '#c0d8ff', '%d GI%d' % (n, n - 25 if n < 33 else n - 29))
    for i, (name, text) in enumerate((('RGB1', '51-53 RGB1'), ('RGB2', '54-56 RGB2'), ('RGBX', '62-64 ext'))):
        add(name, LAMP_X + 16 + i * 192, ROW_RGB, 18, '#000000', text)
    for i, n in enumerate(range(57, 62)):
        add('V%d' % n, LAMP_X + 16 + (3 + i) * 192 * 3 // 4 + 150, ROW_RGB, 13, '#40ff40', '%d %s' % (n, sol[n].split(':')[-1].strip()[:14]))
    for n in range(1, 25):
        add('C%d' % n, LAMP_X + 16 + (n - 1) % 12 * 128, ROW_COIL + (n - 1) // 12 * 36, 11, '#ff3030', '%d %s' % (n, sol.get(n, '-')[:12]))
    return out


def label_image(parts, font, path):
    """the playfield: dark board, white labels next to their indicators"""
    cmd = ['magick', '-size', '%dx%d' % (W, H), 'xc:#181818', '-font', font, '-fill', '#e0e0e0', '-pointsize', '15']
    for _, _, _, text, x, y in parts:
        if text:
            cmd += ['-annotate', '+%d+%d' % (x, y + 5), text[:24]]
    subprocess.run(cmd + [path], check=True)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    vpxtool, repo, font, outdir = [os.path.abspath(a) for a in sys.argv[1:]]
    sys.path.insert(0, os.path.join(repo, 'tests', 'pinheck', 'vpx'))
    sys.dont_write_bytecode = True
    import names as pinheck_names
    names = pinheck_names.read(os.path.join(repo, 'src'))
    os.makedirs(outdir, exist_ok=True)
    work = os.path.join(outdir, 'dominos_test')
    shutil.rmtree(work, ignore_errors=True)
    for f in ('base.vpx', 'dominos_test.vpx'):
        if os.path.exists(os.path.join(outdir, f)):
            os.remove(os.path.join(outdir, f))
    subprocess.run([vpxtool, 'new', os.path.join(outdir, 'base.vpx')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([vpxtool, 'extract', os.path.join(outdir, 'base.vpx')], check=True, stdout=subprocess.DEVNULL)
    os.rename(os.path.join(outdir, 'base'), work)
    os.remove(os.path.join(outdir, 'base.vpx'))
    parts = layout(names)
    os.makedirs(os.path.join(work, 'images'), exist_ok=True)
    label_image(parts, font, os.path.join(work, 'images', 'labels.png'))
    json.dump([{'name': 'labels', 'path': 'labels.png', 'alpha_test_value': -1.0}], open(os.path.join(work, 'images.json'), 'w'), indent=2)
    gd = json.load(open(os.path.join(work, 'gamedata.json')))
    gd.update({'right': float(W), 'bottom': float(H), 'image': 'labels', 'playfield_material': 'Board'})
    json.dump([{'name': 'Board', 'type': 'basic', 'wrap_lighting': 0.0, 'roughness': 0.0, 'glossy_image_lerp': 1.0,
                'thickness': 0.05, 'edge': 1.0, 'edge_alpha': 1.0, 'opacity': 1.0, 'base_color': '#ffffff',
                'glossy_color': '#000000', 'clearcoat_color': '#000000', 'opacity_active': False, 'elasticity': 0.0,
                'elasticity_falloff': 0.0, 'friction': 0.0, 'scatter_angle': 0.0, 'refraction_tint': '#ffffff'}],
              open(os.path.join(work, 'materials.json'), 'w'), indent=2)
    if os.path.exists(os.path.join(work, 'materials-old.json')):
        os.remove(os.path.join(work, 'materials-old.json'))
    gd.update(CAMERA)
    json.dump(gd, open(os.path.join(work, 'gamedata.json'), 'w'), indent=2)
    os.makedirs(os.path.join(work, 'gameitems'), exist_ok=True)
    index = json.load(open(os.path.join(work, 'gameitems.json')))
    for kind, name, item, _, _, _ in parts:
        fn = '%s.%s.json' % (kind, name)
        json.dump(item, open(os.path.join(work, 'gameitems', fn), 'w'), indent=2)
        index.append({'file_name': fn, 'is_locked': False, 'editor_layer_visibility': True})
    json.dump(index, open(os.path.join(work, 'gameitems.json'), 'w'), indent=2)
    shutil.copy(os.path.join(HERE, 'dominos_test.vbs'), os.path.join(work, 'script.vbs'))
    subprocess.run([vpxtool, 'assemble', work, os.path.join(outdir, 'dominos_test.vpx')], check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(outdir, 'pinheck.vbs'), 'w') as f:
        subprocess.run([sys.executable, os.path.join(repo, 'tests', 'pinheck', 'vpx', 'names.py'), 'vbs', os.path.join(repo, 'src')],
                       check=True, stdout=f)
    shutil.rmtree(work)
    print('table: %s, %d parts; %s' % (os.path.join(outdir, 'dominos_test.vpx'), len(index), os.path.join(outdir, 'pinheck.vbs')))


if __name__ == '__main__':
    main()
EOF
cat > /code/spooky_domino/vpx/dominos_test.vbs <<'EOF'
' Domino's (pinHeck) test table: the colour display, every lamp, GI string, RGB channel, coil and servo as a
' labelled indicator, the switches on keys. Needs pinheck.vbs next to the table or in VPX's scripts folder.
' VPinballX -c1 <file> logs what the table receives to <file>.
Option Explicit
Randomize

Const cGameName = "dominos"
Const UseSolenoids = 1
Const UseLamps = 0
Const UseGI = 0
Const UseVPMModSol = 2
Const UseSync = 0

On Error Resume Next
ExecuteGlobal GetTextFile("controller.vbs")
If Err Then MsgBox "You need controller.vbs from VPX's scripts folder to run this table"
On Error Goto 0

LoadVPM "03060000", "pinheck.vbs", 3.61

' log: <game time in s> <kind> <number> <value>
Dim dtLog : Set dtLog = Nothing
Sub dtOpenLog
	Dim path, fso
	path = GetCustomParam(1)
	If path = "" Then Exit Sub
	Set fso = CreateObject("Scripting.FileSystemObject")
	Set dtLog = fso.CreateTextFile(path, True)
End Sub
Sub dtLine(s)
	If Not dtLog Is Nothing Then dtLog.WriteLine FormatNumber(GameTime / 1000.0, 3, -1, 0, 0) & " " & s
End Sub

' solenoid outputs 1-64 at 0..1: coils C1-C24, GI G25-G32 and G37-G44, servos V57-V61, RGB channels 51-56, 62-64
Dim dtLevel(64)
Sub dtSetSol(n, v)
	dtLevel(n) = v
	dtLine "S " & n & " " & Round(v * 255)
	On Error Resume Next
	If n <= 24 Then Eval("C" & n).State = v
	If (n >= 25 And n <= 32) Or (n >= 37 And n <= 44) Then Eval("G" & n).State = v
	If n >= 57 And n <= 61 Then Eval("V" & n).State = v
	On Error Goto 0
	If n >= 51 And n <= 56 Then dtRGB RGB1, 51 : dtRGB RGB2, 54
	If n >= 62 And n <= 64 Then dtRGB RGBX, 62
End Sub
Function dtByte(n) : dtByte = Round(dtLevel(n) * 255) : End Function
Sub dtRGB(lamp, n)
	lamp.Color = RGB(dtByte(n), dtByte(n + 1), dtByte(n + 2))
	lamp.ColorFull = lamp.Color
	If dtByte(n) + dtByte(n + 1) + dtByte(n + 2) > 0 Then lamp.State = 1 Else lamp.State = 0
End Sub

Dim dtN
For dtN = 1 To 64
	SolModCallback(dtN) = "dtSetSol " & dtN & ","
Next

' lamps 11-88 and 91 at 0..255
Sub dtLamps
	Dim chg, i
	chg = Controller.ChangedLamps
	If IsEmpty(chg) Then Exit Sub
	On Error Resume Next
	For i = 0 To UBound(chg)
		Eval("L" & chg(i, 0)).State = chg(i, 1) / 255
		dtLine "L " & chg(i, 0) & " " & chg(i, 1)
	Next
	On Error Goto 0
End Sub
Set LampCallback = GetRef("dtLamps")

' playfield switches on keys (DirectInput scan codes), closed while the key is held
Dim dtKeys : Set dtKeys = CreateObject("Scripting.Dictionary")
dtKeys.Add 16, swDelivery : dtKeys.Add 17, swQuality  : dtKeys.Add 18, swBake     : dtKeys.Add 19, swPrepare
dtKeys.Add 21, swOrder    : dtKeys.Add 22, swBankL    : dtKeys.Add 23, swBankM    : dtKeys.Add 24, swBankR
dtKeys.Add 26, swRampOpto : dtKeys.Add 27, swOvenOpto : dtKeys.Add 30, swLSling
dtKeys.Add 31, swLInlane  : dtKeys.Add 32, swLOutlane : dtKeys.Add 33, swLScoop   : dtKeys.Add 34, swLOrbit
dtKeys.Add 35, swLNoidOrb : dtKeys.Add 36, swStarLane : dtKeys.Add 37, sw5Lane    : dtKeys.Add 38, swLPop
dtKeys.Add 39, swShooter  : dtKeys.Add 44, swRSling   : dtKeys.Add 45, swRInlane  : dtKeys.Add 46, swROutlane
dtKeys.Add 47, swRScoop   : dtKeys.Add 48, swROrbit   : dtKeys.Add 49, swRNoidOrb : dtKeys.Add 50, swRPop
dtKeys.Add 51, swLowerPop : dtKeys.Add 52, swSpinner

Sub Table1_KeyDown(ByVal keycode)
	dtLine "K " & keycode & " 1"
	If dtKeys.Exists(keycode) Then Controller.Switch(dtKeys(keycode)) = True : Exit Sub
	If vpmKeyDown(keycode) Then Exit Sub
End Sub

Sub Table1_KeyUp(ByVal keycode)
	dtLine "K " & keycode & " 0"
	If dtKeys.Exists(keycode) Then Controller.Switch(dtKeys(keycode)) = False : Exit Sub
	If vpmKeyUp(keycode) Then Exit Sub
End Sub

Sub Table1_Init
	dtOpenLog
	vpmInit Me
	With Controller
		.GameName = cGameName
		.SplashInfoLine = "Domino's Spectacular Pinball Adventure (pinHeck) test table"
		.HandleKeyboard = 0
		.HandleMechanics = 3
		.ShowTitle = 0
		.ShowDMDOnly = 1
		.ShowFrame = 0
		.Hidden = 0
		On Error Resume Next
		.Run GetPlayerHWnd
		If Err Then MsgBox Err.Description
		On Error Goto 0
	End With
	dtLine "run " & cGameName
	' three balls in the trough
	Controller.Switch(swTrough1) = True : Controller.Switch(swTrough2) = True : Controller.Switch(swTrough3) = True
	vpmNudge.TiltSwitch = swTilt
	vpmNudge.Sensitivity = 3
End Sub

Sub Table1_Exit
	dtLine "exit"
	If Not dtLog Is Nothing Then dtLog.Close
	Controller.Stop
End Sub

' switches PinMAME sets: the flipper buttons (3, 4) as it copies them, Noid Home (58) from the simulator
Dim dtLast(3)
Sub SwitchTimer_Timer
	Dim i, n : i = 0
	For Each n In Array(swRFlip, swLFlip, swNoidHome, swEnter)
		If Controller.Switch(n) <> dtLast(i) Then dtLast(i) = Controller.Switch(n) : dtLine "W " & n & " " & -CInt(dtLast(i))
		i = i + 1
	Next
End Sub
EOF
chmod +x /code/spooky_domino/vpx/make_table.py
```

- [ ] **Step 3: The headless run and its check**

```sh
cat > /code/spooky_domino/vpx/session.sh <<'EOF'
#!/bin/sh
# run inside xvfb-run: start VPX, wait for the player, press the table's keys, take screenshots, stop.
#   session.sh OUT keys|SECONDS VPX-COMMAND...   keys: press the keys, then run 40 s; SECONDS: just run that long
R=$1; S=$2; shift 2
"$@" > $R/vpx.out 2>&1 &
pid=$!
n=0
until grep -q "Startup done" $R/vpx.out 2>/dev/null; do
	sleep 1; n=$((n + 1))
	kill -0 $pid 2>/dev/null || { echo "session: VPX exited before its player started"; exit 1; }
	[ $n -le 300 ] || { echo "session: no player after 300 s"; kill -INT $pid; exit 1; }
done
sleep 8
xwd -root -silent > $R/shot1.xwd
if [ "$S" = keys ]; then
	key() { xdotool keydown $1; sleep ${2:-0.3}; xdotool keyup $1; sleep ${3:-0.3}; }
	# the playfield keys of dominos_test.vbs, both flipper buttons, Enter (the service menu)
	for k in q w e r y u i o bracketleft bracketright a s d f g h j k l semicolon z x c v b n m comma period; do key $k; done
	key Shift_L 0.5 0.5
	key Shift_R 0.5 0.5
	key 0 0.2 2
	xwd -root -silent > $R/shot2.xwd
	sleep 40
else
	sleep $S
fi
kill -INT $pid
wait $pid
echo "session: done"
EOF
cat > /code/spooky_domino/vpx/run_vpx.sh <<'EOF'
#!/bin/sh
# The Domino's test table in VPX standalone on Linux, headless (Xvfb), with the PinMAME plugin on a given libpinmame.
#   run_vpx.sh VPX_DIR LIBPINMAME_SO PINMAME_REPO OUT
# VPX_DIR: an unpacked VPinballX_GL linux-x64 build whose PinMAME plugin speaks versioned messages (built after
# 2026-09-05); P8X32A_ROM and PINHECK_ZIP as for the PinMAME checks. OUT gets the table, the logs and the screenshots.
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
[ $# -eq 4 ] || { echo "usage: run_vpx.sh VPX_DIR LIBPINMAME_SO PINMAME_REPO OUT"; exit 2; }
V=$(realpath "$1") && L=$(realpath "$2") && REPO=$(realpath "$3") || exit 2
mkdir -p "$4" && O=$(realpath "$4") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") && PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
H=$(dirname "$(realpath "$0")")
for t in xvfb-run xwd xdotool magick; do command -v $t > /dev/null || { echo "run_vpx: $t is missing"; exit 2; }; done
rm -rf "$O/app" "$O/home" "$O/pref" "$O/table" "$O"/*.log "$O"/*.xwd "$O"/*.png "$O"/*.out || exit 2
# VPX with its own PinMAME plugin, which loads the given libpinmame
mkdir -p "$O/app/plugins/pinmame" || exit 2
cp "$V/VPinballX_GL" "$O/app/" || exit 2
for f in "$V"/*; do case $(basename "$f") in VPinballX_GL|plugins) ;; *) ln -s "$f" "$O/app/" ;; esac; done
for f in "$V"/plugins/*; do [ "$(basename "$f")" = pinmame ] || ln -s "$f" "$O/app/plugins/"; done
cp "$V/plugins/pinmame/plugin.cfg" "$V/plugins/pinmame/plugin-pinmame.so" "$O/app/plugins/pinmame/" || exit 2
ln -s "$L" "$O/app/plugins/pinmame/libpinmame.so.3.7.0" && ln -s libpinmame.so.3.7.0 "$O/app/plugins/pinmame/libpinmame.so" || exit 2
grep -q "GetStateSrc:1" "$O/app/plugins/pinmame/plugin-pinmame.so" || { echo "run_vpx: this VPX's PinMAME plugin predates versioned plugin messages"; exit 2; }
# a private home: the ROMs, the NVRAM and VPX's settings (never the user's)
mkdir -p "$O/home/.pinmame/roms" "$O/home/.pinmame/nvram" "$O/pref" || exit 2
(cd "$O" && cp "$P8X32A_ROM" p8x32a.rom && zip -q -j home/.pinmame/roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" "$O/home/.pinmame/roms/dominos.zip" || exit 2
python3 "$H/make_table.py" "$H/tools/vpxtool/vpxtool" "$REPO" "$V/assets/LiberationSans-Regular.ttf" "$O/table" || exit 2
vpx() {
	(cd "$O" && HOME="$O/home" SDL_AUDIO_DRIVER=dummy PINHECK_INSERVICE=6 "$@") || { echo "run_vpx: VPX run failed"; exit 1; }
}
play() { echo "$O/app/VPinballX_GL -PrefPath $O/pref -Play $O/table/dominos_test.vpx"; }
# first launch on an empty NVRAM: "System has been updated / Please restart your machine"
vpx timeout -k 10 300 xvfb-run -a -s "-screen 0 1280x1024x24" "$H/session.sh" "$O" 30 $(play) > "$O/session1.out" || exit 1
mv "$O/vpx.out" "$O/vpx1.out"; mv "$O/shot1.xwd" "$O/first.xwd"
[ -s "$O/home/.pinmame/nvram/dominos.nv" ] || { echo "VPX FAIL: the first launch wrote no NVRAM"; exit 1; }
# second launch: attract mode, the keys, the console's coil commands
vpx env PINHECK_OUT_LOG="$O/out.log" PINHECK_UART1_LOG="$O/uart.log" PINHECK_UART1_SEND_AT=40 PINHECK_UART1_SEND_GAP=0.25 \
	PINHECK_UART1_SEND="$(python3 -c "print('~'.join(['[E97000]', '[L99000]'] + ['[M%02d020]' % c for c in range(24)] + ['[L98000]']))")" \
	timeout -k 10 400 xvfb-run -a -s "-screen 0 1280x1024x24" "$H/session.sh" "$O" keys $(play) -c1 "$O/table.log" > "$O/session2.out" || exit 1
for s in first shot1 shot2; do [ -f "$O/$s.xwd" ] && magick "$O/$s.xwd" -resize 50% "$O/$s.png"; done
python3 "$H/check_vpx.py" "$O" "$O/table" || exit 1
echo "vpx standalone: ok (screenshots for a look: $O/first.png $O/shot1.png $O/shot2.png)"
EOF
cat > /code/spooky_domino/vpx/check_vpx.py <<'EOF'
#!/usr/bin/env python3
"""Checks a VPX run of the Domino's test table (run_vpx.sh) against the driver's own log.
  check_vpx.py RUN_DIR TABLE_DIR
RUN_DIR holds table.log (the table script's log: '<s> L|S|K|W <n> <v>'), out.log (PINHECK_OUT_LOG) and vpx.out;
TABLE_DIR holds dominos_test.vbs's key map (embedded copy: dominos_test.vbs next to this script) and pinheck.vbs."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SLACK = 1.0          # seconds of slack between the table's clock (GameTime) and the emulated time
LAG = 10.0           # outputs lit this close to the end are not required at the table


def consts(path):
    return dict((k, int(v)) for k, v in re.findall(r'^Const (\w+)\s*=\s*(\d+)', open(path).read(), re.M))


def keymap(vbs, names):
    return dict((int(k), names[s]) for k, s in re.findall(r'dtKeys\.Add (\d+), (\w+)', open(vbs).read()))


def table_log(path):
    out = []
    for line in open(path):
        f = line.split()
        if len(f) == 4 and f[1] in 'LSKW':
            out.append((float(f[0]), f[1], int(f[2]), int(f[3])))
    return out


def driver_log(path):
    """[(t, kind, n, v)]: P lamps and solenoids at 0-255 as 'L'/'S', W switches"""
    out = []
    for line in open(path):
        f = line.split()
        if len(f) < 3 or f[1] not in ('P', 'W'):
            continue
        t = float(f[0])
        for x in f[2:]:
            k, v = x.split('=')
            if f[1] == 'W':
                out.append((t, 'W', int(k), int(v)))
            else:
                out.append((t, k[0], int(k[1:]), int(v)))
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    run, table = sys.argv[1:]
    fails = []
    head = open(os.path.join(run, 'table.log')).readline().split()
    if head[1:] != ['run', 'dominos']:
        fails.append('the table script did not start dominos: %s' % head)
    tl = table_log(os.path.join(run, 'table.log'))
    dl = driver_log(os.path.join(run, 'out.log'))
    end = max(t for t, _, _, _ in tl) - LAG
    for kind, what in (('L', 'lamps'), ('S', 'solenoid outputs')):
        seen = set(n for t, k, n, v in tl if k == kind and v > 0)
        lit = set(n for t, k, n, v in dl if k == kind and v >= 16 and t < end)
        driven = set(n for t, k, n, v in dl if k == kind and v > 0)
        if lit - seen:
            fails.append('%s the driver lit and the table never saw lit: %s' % (what, sorted(lit - seen)))
        if seen - driven:
            fails.append('%s the table saw lit and the driver never lit: %s' % (what, sorted(seen - driven)))
        print('vpx: %d %s lit in the driver, %d seen lit by the table' % (len(lit), what, len(seen)))
    names = consts(os.path.join(table, 'pinheck.vbs'))
    keys = keymap(os.path.join(HERE, 'dominos_test.vbs'), names)
    keys.update({42: names['swLFlip'], 54: names['swRFlip'], 11: names['swEnter']})
    # each key press closes, then opens its switch in the driver, in the order the table saw the keys (the
    # emulation may fall behind the table's clock while software rendering takes the CPU)
    presses = [(t, code, keys[code]) for t, k, code, v in tl if k == 'K' and v == 1 and code in keys]
    events = sorted((u, n, vv) for u, kk, n, vv in dl if kk == 'W' and n in set(keys.values()))
    pressed, missed, since, lag = len(presses), [], 0.0, []
    for t, code, sw in presses:
        j = next((j for j in range(len(events)) if events[j][0] >= since and events[j][1] == sw and events[j][2] == 1), None)
        k = j is not None and next((k for k in range(j + 1, len(events)) if events[k][1] == sw and events[k][2] == 0), None)
        if j is None or k is None:
            missed.append('key %d -> switch %d' % (code, sw))
            continue
        lag.append(events[j][0] - t)
        since = events[j][0]
    if missed:
        fails.append('keys whose switch did not close and open in the driver, in order: %s' % ', '.join(missed))
    else:
        print('vpx: %d key presses closed and opened their switches in the driver in order (emulated time minus table time %.1f to %.1f s)' % (
            pressed, min(lag), max(lag)))
    if pressed < len(keys):
        fails.append('only %d of the %d mapped keys were pressed' % (pressed, len(keys)))
    for f in fails:
        print('VPX FAIL: ' + f)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
EOF
chmod +x /code/spooky_domino/vpx/session.sh /code/spooky_domino/vpx/run_vpx.sh /code/spooky_domino/vpx/check_vpx.py
```

- [ ] **Step 4: Refused with the release's plugin**

```sh
/code/spooky_domino/vpx/run_vpx.sh /code/spooky_domino/vpx/tools/vpx $PWD/build/libpinmame/libpinmame.so.3.7.0 $PWD build/vpxrel 2>&1 | tail -1
```
Expected (the VPX 10.8.1-5436 release, when it is unpacked in `tools/vpx`):
```text
run_vpx: this VPX's PinMAME plugin predates versioned plugin messages
```

- [ ] **Step 5: Run it**

```sh
nice -n 15 /code/spooky_domino/vpx/run_vpx.sh /code/spooky_domino/vpx/tools/n5947/vpx $PWD/build/libpinmame/libpinmame.so.3.7.0 $PWD build/vpxrun 2>&1 | tail -5
```
Expected (about 4 minutes):
```text
table: /code/spooky_domino/work/wt-m10r/build/vpxrun/table/dominos_test.vpx, 118 parts; /code/spooky_domino/work/wt-m10r/build/vpxrun/table/pinheck.vbs
vpx: 45 lamps lit in the driver, 45 seen lit by the table
vpx: 35 solenoid outputs lit in the driver, 35 seen lit by the table
vpx: 33 key presses closed and opened their switches in the driver in order (emulated time minus table time -10.0 to -1.1 s)
vpx standalone: ok (screenshots for a look: /code/spooky_domino/work/wt-m10r/build/vpxrun/first.png /code/spooky_domino/work/wt-m10r/build/vpxrun/shot1.png /code/spooky_domino/work/wt-m10r/build/vpxrun/shot2.png)
```

`build/vpxrun/first.png`, `shot1.png` and `shot2.png` are the screenshots of the first launch, of attract mode and after the keys; look at them (Ruling 17).

- [ ] **Step 6: The checklists**

```sh
cat > /code/spooky_domino/vpx/CHECKLIST-linux.md <<'EOF'
# Domino's test table: VPX standalone on Linux, manual checklist

`run_vpx.sh` checks the data paths headless (lamps, solenoid outputs, keys to switches). This list covers what a
person has to see and hear. Status: not yet run by a person.

Setup
- [ ] A VPinballX_GL linux-x64 build from after 2026-09-05 (the 10.8.1-5436 release's PinMAME plugin cannot talk to
      this libpinmame: no lamps, no display). Its `plugins/pinmame/libpinmame.so.3.7.0` replaced by (or linked to) the
      libpinmame built from the `pinheck` branch.
- [ ] `~/.pinmame/roms/` holds `dominos.zip` (the update zip) and `pinheck.zip` (`p8x32a.rom`).
- [ ] `dominos_test.vpx` and `pinheck.vbs` from `make_table.py` in one folder.

First launch (empty `~/.pinmame/nvram/`)
- [ ] Launch 1: the display shows the update, then `PLEASE RESTART`. Quit (Esc, Quit).
- [ ] Launch 2: `System has been updated / Please restart your machine`. Quit.
- [ ] Launch 3: attract mode.

Every launch
- [ ] The display flasher at the top shows the 128×32 colour display, sharp, colours as in PinMAME.
- [ ] Sound plays (attract music and effects); no crackle or drift over a minute.
- [ ] Lamp indicators 11–88 and 91 follow the attract lamp show; the labels match the lamp matrix.
- [ ] GI indicators 25–32 and 37–44 light when the game switches GI on.
- [ ] RGB1, RGB2 and the external LED indicator take the colours the game sends.
- [ ] Left/Right Shift (flipper buttons) move the menu cursor in the service menu (switches 4 and 3).
- [ ] 0 opens the service menu, 7 goes back, 9 is User, End opens the coin door, 5 adds a credit, 1 starts a game.
- [ ] The playfield keys (Q W E R Y U I O [ ] A S D F G H J K L ; Z X C V B N M , .) close their switches in the
      service menu's switch test.
- [ ] Service menu, solenoid test: each coil indicator C1–C24 flashes as its coil fires.
- [ ] Service menu, servo test: the Noid (57) and bank (58) indicators change brightness with the servo position.
- [ ] F3 (reset) restarts the game without a crash.
- [ ] Quitting leaves `~/.pinmame/nvram/dominos.nv`; the next launch starts in attract mode.

Known
- Under Xvfb with software rendering the board went black and the display flasher stayed empty while the lamp
  callbacks ran; the same table showed its labels while no lamp changed. Check both on a real desktop.
- `Controller.Version` crashes VPX when the B2S legacy plugin is on; the table does not call it.
EOF
cat > /code/spooky_domino/vpx/CHECKLIST-windows.md <<'EOF'
# Domino's test table: Windows VPX with VPinMAME, checklist (for the Windows worker)

Status: unverified. Report each line as pass or fail, with a screenshot where it says so.

Setup
- [ ] VPinMAME64 built from the `pinheck` branch (`feature/pinheck-dominos`) at the commit under test, registered
      (`Setup64.exe`), `roms\dominos.zip` and `roms\pinheck.zip` in its ROM folder.
- [ ] VPX 10.8 (any build; VPinMAME is COM and does not use the plugin messages).
- [ ] `dominos_test.vpx` (built on Linux with `make_table.py`) and `pinheck.vbs` next to it or in VPX's `Scripts`.

First launch (no `nvram\dominos.nv`)
- [ ] Launch 1: `PLEASE RESTART` after the update; launch 2: `System has been updated / Please restart your
      machine`; launch 3: attract mode. VPinMAME does not create missing `nvram\` or `cfg\` folders: create them first.

Display and sound
- [ ] The VPinMAME DMD window shows the display crisp at 2×2 per dot (256×64), in the look the service menu sets.
      Screenshot.
- [ ] The table's Display flasher shows the display too (VPX 10.8 external DMD). Screenshot.
- [ ] Sound plays; no drift over a minute.

Outputs and switches
- [ ] Lamp indicators follow the attract lamp show; GI 25–32 and 37–44, RGB1, RGB2 and the external LED work.
- [ ] Left/Right flipper keys reach switches 4 and 3 (`Switch(114)` and `Switch(112)`); the service menu's switch
      test shows them as Left and Right Flipper.
- [ ] Service menu: solenoid test flashes C1–C24; servo test changes 57 and 58; lamp test lights every lamp.
- [ ] The playfield keys close their switches in the switch test.
- [ ] `HandleMechanics = 3` in the table: Noid Home (58) toggles during the servo test. With `HandleMechanics = 0` it
      does not.
- [ ] F3 (reset) and quitting work; `nvram\dominos.nv` is written.
EOF
cp build/vpxrun/table/dominos_test.vpx build/vpxrun/table/pinheck.vbs /code/spooky_domino/vpx/ && ls /code/spooky_domino/vpx/ | tr '\n' ' '; echo
```
Expected:
```text
CHECKLIST-linux.md CHECKLIST-windows.md check_vpx.py dominos_test.vbs dominos_test.vpx make_table.py pinheck.vbs run_vpx.sh session.sh tools
```

Hand `CHECKLIST-windows.md`, `dominos_test.vpx` and `pinheck.vbs` to the Windows worker session; its results stay "unverified" in the reports until it has run the list.

---
### Task 6: Records, the suites, and proof that the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md` (§4.1), `docs/superpowers/specs/2026-09-29-pinheck-m10-vpx-design.md` (§4, §8), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` (m10 row)

- [ ] **Step 1: The records**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md', [
    ('''- A `CORE_VIDEO` 128×32 layout.
''',
     '''- A `CORE_VIDEO` layout of 256×64 in the PinMAME and VPinMAME windows, each dot drawn 2×2 in the display look (M8/M9 addendum §5); libpinmame exports the 128×32 panel as sent (Milestone 10).
'''),
])

edit('docs/superpowers/specs/2026-09-29-pinheck-m10-vpx-design.md', [
    ('''   - shows the 128×32 colour display;
''',
     '''   - shows the colour display (libpinmame's 128×32 frame, as sent);
'''),
    ('''   - It is established how VPinMAME's display window presents a 128×32 `CORE_VIDEO` display, with Baby Pac-Man as the precedent. Any change needed goes into the driver or core.
''',
     '''   - VPinMAME's display window presents the 256×64 `CORE_VIDEO` layout (2×2 dots, in the display look) crisply, as the standalone window does (established on Windows: `DmdWidth`/`DmdHeight` 256×64; `RawDmdWidth`/`RawDmdHeight` are −1 for every `CORE_VIDEO` game, Baby Pac-Man included). No change is needed.
'''),
    ('''## 7. Order

After Milestone 9, and before or together with the release phase.
''',
     '''## 7. Order

After Milestone 9, and before or together with the release phase.

## 8. Findings of Milestone 10

- **Precondition met.** `pinheck` at `29692743` runs `dominos` in real time on the reference machine: 1.28× in attract mode and 1.08× with video (Plans 9b and 9c).
- **VPX version.** libpinmame names its plugin messages with a version (`GetStateSrc:1`, `GetDisplays:1`, upstream since 2026-09-05). The PinMAME plugin of the VPX 10.8.1-5436 release (2026-08-21) asks for the unversioned names, so with this libpinmame it gets no lamps, solenoids, switches or display. VPX builds from after 2026-09-05 (tested: 10.8.1-5947 `4dc8d8afb`) work. On Windows, VPX with VPinMAME (COM) does not use these messages.
- **Flipper buttons.** PinMAME copies its flipper column to cabinet switches 4 (left) and 3 (right) every frame, so a table presses switch 114 (lower left) and 112 (lower right); `pinheck_names.h` names them `swLLFlip` and `swLRFlip`.
- **Numbers that name nothing.** A switch number outside the matrix goes to an unused switch; a lamp number outside the matrix reads 0.
- **First launch.** On an empty NVRAM the firmware updates itself and asks for two restarts: the PIC32 is reflashed and the display shows `PLEASE RESTART`; after the restart it shows `System has been updated / Please restart your machine`; after the second restart the game runs. With the test hook `PINHECK_INSERVICE=6` the first launch skips the reflash and shows `System has been updated`, so one restart is enough.
'''),
])

edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    ('''| m10 | 10: VPX support: libpinmame export proof, `pinheck_names.h`, minimal test table (outside the repo), VPX standalone on Linux and Windows VPX + VPinMAME | Plan 9 | none | export paths equal M6/M7/M8 outputs; test table checklist passes on Linux; Windows checklist handed to the user | after Plan 9 |
''',
     '''| `2026-09-30-pinheck-m10-vpx.md` | 10: VPX support: libpinmame's display count and getters, the 128×32 frame to libpinmame hosts, export proof, `pinheck_names.h`, lamp and switch numbers that name nothing, the Noid and bank under `HandleMechanics`, the link log and the core's solenoid log, minimal test table (outside the repo), VPX standalone on Linux and Windows VPX + VPinMAME | Plan 9c | none | display frames, sound, lamps, solenoids, GI, RGB and servos through libpinmame equal the driver's; the test table's data paths in VPX standalone on Linux (headless); visual and sound checklist on Linux, Windows checklist handed to the user | written |
'''),
])
EOF
git diff --stat | tail -1
```
Expected:
```text
 3 files changed, 12 insertions(+), 4 deletions(-)
```

- [ ] **Step 2: The machine checks the driver change touches**

```sh
for s in display/pinmame_look.sh board/pinmame_board.sh sim/pinmame_sim.sh game/pinmame_game.sh; do
	SDL3PINMAME=build/sdl3pinmame/sdl3pinmame nice -n 15 ionice -c 3 timeout -k 30 7200 tests/pinheck/$s > build/$(basename $s .sh).log 2>&1
	echo "$s: $? $(tail -1 build/$(basename $s .sh).log)"
done
```
Expected (about 25 minutes):
```text
display/pinmame_look.sh: 0 pinmame look: ok
board/pinmame_board.sh: 0 pinmame board: ok
sim/pinmame_sim.sh: 0 pinmame sim: ok
game/pinmame_game.sh: 0 pinmame game: ok
```

- [ ] **Step 3: Prove the libpinmame checks bite**

Three mutants, each undoing fixes of Tasks 1 and 2, each built and run through both checks (about 10 minutes each). The link decoder's reset has no mutant: no check can stop a session inside a packet (Ruling 10).

Mutant A: the phantom display, no GI 8–15 in the plugin list, the mech mask ignored, the link log left open.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


# the phantom display, no GI 8-15 in the plugin list, the mech mask ignored, the link log left open
edit('src/libpinmame/libpinmame.cpp', [
    ('		if (!hasDMDOrVideo)\n			displayCount++;\n		_displays.resize(displayCount);', '		displayCount++;\n		_displays.resize(displayCount);'),
    ('(core_gameData->gen & (GEN_ALLS11 | GEN_SAM | GEN_SPA | GEN_PINHECK))', '(core_gameData->gen & (GEN_ALLS11 | GEN_SAM | GEN_SPA))'),
])
edit('src/wpc/pinheck.c', [
    ('	if (link_log) fclose(link_log);\n	link_log = NULL;\n}', '}'),
])
edit('src/wpc/sims/pinheck/dominos.c', [
    ('  if (!(mech & 0x01)) return;\n', ''),
])
EOF
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 timeout -k 30 900 tests/pinheck/vpx/check.sh 2>&1 | grep FAIL
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/vpx/pinmame_vpx.sh 2>&1 | grep FAIL | cut -c1-120 | sort | uniq -c
git checkout -- src
```
Expected:
```text
[100%] Built target pinmame_test
displays: tz_94h announces 2 display(s), indices [0], updates [180]  FAIL
displays: babypac announces 2 display(s), indices [0], updates [180]  FAIL
      1 displays: boot announces 2 display(s), indices [0], updates [1200]  FAIL
      1 VPX FAIL: at the first session's end the link log held 0 of its 3705 bytes
      2 VPX FAIL: display announced as ['avail 0 2 type 15 128x32 depth 16 length 0'], expected one 128x32 VIDEO display of dept
      2 VPX FAIL: never seen on: PS37 PS38 PS39 PS40 PS41 PS42 PS43 PS44
      1 VPX FAIL: Noid Home moved with HandleMechanics mech2
      1 VPX FAIL: PS37 is not exported
      1 VPX FAIL: PS38 is not exported
      1 VPX FAIL: PS39 is not exported
      1 VPX FAIL: PS40 is not exported
      1 VPX FAIL: PS41 is not exported
      1 VPX FAIL: PS42 is not exported
      1 VPX FAIL: PS43 is not exported
      1 VPX FAIL: PS44 is not exported
      2 VPX FAIL: the plugin lists no solenoids 37-44 (GI_8..15): [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18
```

Mutant B: no lamp range check, 2×2 frames to libpinmame (a lamp probe past the matrix crashes the host, so the Domino's check stops at its first board launch).

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


# no lamp range check, 2x2 frames to libpinmame
edit('src/wpc/vpintf.c', [
    ('  return lampNo >= 0 && lampNo < CORE_MODOUT_LAMP_MAX ? lampNo : -1;', '  return lampNo;'),
])
edit('src/libpinmame/libpinmame.cpp', [
    ('	const int index = _isRunning ? vp_getLampIndex(lampNo) : -1;\n	if (index < 0)', '	const int index = vp_getLampIndex(lampNo);\n	if (!_isRunning)'),
])
edit('src/wpc/pinheck.h', [
    ('#ifdef LIBPINMAME\n#define PINHECK_VIDEO_SCALE 1\n#else\n#define PINHECK_VIDEO_SCALE 2\n#endif', '#define PINHECK_VIDEO_SCALE 2'),
])
EOF
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 timeout -k 30 900 tests/pinheck/vpx/check.sh 2>&1 | grep FAIL
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/vpx/pinmame_vpx.sh 2>&1 | grep FAIL | cut -c1-120 | sort | uniq -c
git checkout -- src
```
Expected:
```text
[100%] Built target pinmame_test
SCALE FAIL: PINHECK_VIDEO_SCALE is 2 without LIBPINMAME and 2 with it
VPX FAIL: tz_94h exited 139
VPX FAIL: babypac exited 139
displays: tz_94h announces 1 display(s), indices [0], updates [0]  FAIL
displays: babypac announces 1 display(s), indices [0], updates [0]  FAIL
probe: tz_94h X0=[] X-1=[] X100000=[] Y0=[] Y-1=[] Y100000=[]  FAIL
probe: babypac X0=[] X-1=[] X100000=[] Y0=[] Y-1=[] Y100000=[]  FAIL
      1 VPX FAIL: board exited 139
      2 VPX FAIL: callback frame 0 is none of the decoded frames that follow the last match
      2 VPX FAIL: display announced as ['avail 0 1 type 15 256x64 depth 16 length 0'], expected one 128x32 VIDEO display of dept
      1 VPX FAIL: plugin frame 0 is none of the decoded frames that follow the last match
```

Mutant C: lamp and switch numbers that name nothing aliased onto real ones.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


# lamp and switch numbers that name nothing aliased onto real ones, as before
edit('src/wpc/pinheck.c', [
    ('static int pinheck_sw2m(int no) { return pinheck_2m(no, CORE_STDSWCOLS, PINHECK_NOSUCH); }', 'static int pinheck_sw2m(int no) { return (no / 10) * 8 + no % 10 - 1; }'),
    ('static int pinheck_lamp2m(int no) { return pinheck_2m(no, CORE_CUSTLAMPCOL + 2, -1); }', 'static int pinheck_lamp2m(int no) { return (no / 10) * 8 + no % 10 - 1; }'),
])
EOF
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 timeout -k 30 900 tests/pinheck/vpx/check.sh 2>&1 | grep FAIL
LIBPINMAME=build/libpinmame/libpinmame.so nice -n 15 ionice -c 3 timeout -k 30 5400 tests/pinheck/vpx/pinmame_vpx.sh 2>&1 | grep FAIL | cut -c1-120 | sort | uniq -c
git checkout -- src
```
Expected:
```text
[100%] Built target pinmame_test
      1 VPX FAIL: lamp 19, which does not exist, reads [0, 1]
      1 VPX FAIL: lamp 19, which does not exist, reads [0, 1, 2, 3, 4, 5, 6, 7, 8, 22, 24, 25, 28, 30, 31, 39, 41, 43, 44, 45, 4
      1 VPX FAIL: lamp 20, which does not exist, reads [0, 1]
      1 VPX FAIL: lamp 20, which does not exist, reads [0, 1, 2, 3, 4, 5, 6, 7, 8, 22, 24, 25, 26, 28, 29, 35, 36, 40, 41, 42, 4
      1 VPX FAIL: lamp 29, which does not exist, reads [0, 1]
      1 VPX FAIL: lamp 29, which does not exist, reads [0, 1, 4, 7, 9, 13, 16, 17, 26, 27, 28, 29, 32, 33, 36, 43, 51, 52, 54, 5
      1 VPX FAIL: lamp 30, which does not exist, reads [0, 1]
      1 VPX FAIL: lamp 30, which does not exist, reads [0, 1, 4, 5, 8, 25, 31, 50, 76, 137, 155, 188, 204, 211, 226, 236, 239, 2
      1 VPX FAIL: lamp 50, which does not exist, reads [0, 1]
      1 VPX FAIL: lamp 50, which does not exist, reads [0, 1, 4, 26, 76, 136, 172, 226, 240, 241, 242, 243, 246]
      1 VPX FAIL: lamp 89, which does not exist, reads [0, 1]
      1 VPX FAIL: lamp 89, which does not exist, reads [0, 1, 7, 22, 40, 43, 184, 195, 202, 249, 251, 252, 254, 255]
      1 VPX FAIL: lamp 90, which does not exist, reads [0, 1]
      1 VPX FAIL: lamp 90, which does not exist, reads [0, 4, 8, 29, 62, 147, 188, 229, 240, 241, 242, 243, 244, 246]
      1 VPX FAIL: lamp 9, which does not exist, reads [0, 1]
      1 VPX FAIL: lamp 9, which does not exist, reads [0, 1, 4, 5, 8, 29, 36, 47, 76, 137, 149, 188, 199, 219, 226, 236, 239, 24
      2 VPX FAIL: setting switches that do not exist moved real ones: 15.016666662 W 7=1 8=1 11=1 18=1 21=1 28=1 31=1 48=1 88=1
```

The fixed build again:

```sh
flock /code/spooky_domino/work/heavy.lock nice -n 15 ionice -c 3 timeout 2700 cmake --build build/libpinmame -j6 > build/libpinmame-build.log 2>&1; tail -1 build/libpinmame-build.log; git status --short | wc -l
```
Expected:
```text
[100%] Built target pinmame_test
3
```
(the three records of Step 1, committed in Step 4)

(The display check's failing run in Task 4 Step 3 is the same proof for the solenoid log.)

- [ ] **Step 4: Commit**

```sh
git add docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md docs/superpowers/specs/2026-09-29-pinheck-m10-vpx-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -q -m "docs: M10 findings (VPX version, flipper switches, first launch), window and libpinmame frame sizes, roadmap" && git log --oneline -1 | cut -c10-
```
Expected:
```text
docs: M10 findings (VPX version, flipper switches, first launch), window and libpinmame frame sizes, roadmap
```

## Result

Replayed from this text on a fresh worktree of `29692743` (branch `pinheck-m10-replay`, 2026-09-30, load 5–6 from other agents); the diff is `/code/spooky_domino/work/m10/m10-replay.patch`.

- **libpinmame, generic** (Task 1): before the fix `tz_94h` and `babypac` announced two displays and a lamp probe past the matrix crashed them (exit 139) or read lit (`pb_l5`, lamps 0 and −1); after it, one display each, nine for `pb_l5`, every probe 0.
- **Domino's through libpinmame** (Task 2): every frame a host receives, through the callback and through the plugin's `GetRenderFrame`, is one of the driver's decoded 128×32 frames, in order (184 of 185 in the clip run); the callback and the plugin's `AudioUpdate` stream equal the driver's capture to ±1 (3,087,000 samples), and `IR0` correlates 0.9991 with its source; 1,009,650 legacy and 1,035,050 physical reads of 318 and 326 outputs (lamps, solenoids 1–32, GI 37–44, RGB 51–56 and 62–64, servos 57–61, switches) match the driver's board log within its frame tolerance; Noid Home follows `HandleMechanics` bit 0; a second session in the same process runs and the link log is whole at the first session's end.
- **Names** (Task 3): 46 switches, 48 lamps, 47 outputs, 39 simulator constants agree; ten build lists carry the same 33 pinHeck sources; `pinheck.vbs` has 141 constants.
- **The window** (Task 4): the core's solenoid log shows (110 pixels) where it was cleared before.
- **VPX standalone on Linux** (Task 5), headless with VPinballX_GL 10.8.1-5947 and this libpinmame: the first launch writes the NVRAM; in the second, all 45 lamps and 35 solenoid outputs the driver lit reached the table lit and nothing else, and 33 key presses (29 playfield keys, both flipper buttons, Enter) closed and opened their switches in the driver in order, 1–10 s behind the table's clock. The release 10.8.1-5436 is refused. Not verified: the display in the table's flasher and the board's look (Ruling 17), sound through VPX (Ruling 16).
- **Suites** (Task 6): `display/pinmame_look.sh`, `board/pinmame_board.sh`, `sim/pinmame_sim.sh` and `game/pinmame_game.sh` pass on the result (about 9 minutes); `display/pinmame_display.sh` passes in Task 4; the libpinmame checks pass in Tasks 1–3.
- **Mutants** (Task 6): A (phantom display, GI 8–15 unlisted, mech mask ignored, link log left open), B (no lamp range check, 2×2 frames) and C (lamp and switch aliases) each fail the checks named in their Expected blocks; the link decoder's reset has no catching test (Ruling 10).
- **Windows:** `CHECKLIST-windows.md` is handed to the Windows worker; unverified.

## Self-Review

**Spec coverage.** §4.1 export proof: display frames (Task 2, `vpx.py media`, both paths), the audio stream (callback and plugin message, ±1 against the driver's capture, 0.999 against the source), lamps, solenoids, GI and custom outputs against the board outputs during the console's coil commands and the service menu's servo, lamp and RGB tests (`vpx.py outputs`, legacy and physical). §4.2 names: Task 3. §4.3 VPX standalone on Linux: Task 5, data paths headless, display and sound on the manual checklist (Rulings 15–17). §4.4 Windows: every pinHeck source in every build list (Task 3's build-list check), the display window established (spec §4 text, Task 6), the checklist (Task 5). §4.5 the table outside the repository: Task 5. §5 failure behaviour: every missing export found was fixed in the core or the driver (the display count, the getters, GI 8–15, the numbers that name nothing), none in the table script. The brief's deferred items: the link log and decoder (Task 2), the solenoid log (Task 4), the `W`/`P` times (Ruling 12), the stale spec text (Task 6), the first launch (Task 6, checklists).

**Placeholder scan.** No TBD; every code step carries its full content or an anchored edit script; every Run has its Expected block from the replay.

**Type consistency.** `vp_getLampIndex(int) → int` (Task 1) is used by `PinmameGetLamp` and `vp_getLamp` only. `pinheck_name_t` fields `num`, `id`, `name` match `names.py`'s parser and `make_table.py`. The host's `api.log` keys (`L`, `S`, `W`, `X`, `Y`, `PL`, `PS`) are the ones `vpx.py` reads; the table log's kinds (`L`, `S`, `K`, `W`) are the ones `check_vpx.py` reads.
