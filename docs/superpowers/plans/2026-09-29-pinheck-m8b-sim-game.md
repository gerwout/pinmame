# Milestone 8b: playfield simulator and scripted game Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Domino's can be played from the keyboard in `sdl3pinmame` through a playfield simulator built on PinMAME's own simulator framework, and a scripted headless game (coin, start, three balls with a multiball, every ball drained, high-score entry, match) proves the whole machine end to end against the firmware's own output and stays in the suite as the regression test and Milestone 9's workload.

**Architecture:** `src/wpc/sims/pinheck/dominos.c` becomes Domino's game definition, as PinMAME's full simulators are (ROMs, input ports, game data, simulator): a `sim_tSimData` ball-state table for the trough, shooter lane, scoops, orbits, ramps, targets, pops and drain, a state handler that moves balls on the coils the firmware fires, and a mechanics handler for the Noid and the target bank driven by the servo pulse widths. Coils are read as "on at some time during the last frame" (`coreGlobals.solenoids`, which Plan 8a's board block builds each frame), so the simulator reacts to every pulse however short. The shared framework gains one opt-in field, `autoBall`, so the keys move a ball on the playfield without Up/Down juggling in multiball. The driver gains `pinheck_servo()` (pulse widths for the simulator), switch changes in `PINHECK_OUT_LOG` (`W` lines) and `PINHECK_LINK_LOG`, a log of every PIC32-to-Propeller link packet: in `DOM_V006` the game's own progress (player, ball, score, clips, high-score table) travels there, not on UART1. A simulator check drives the firmware's console coil commands and the service menu; the scripted game plays a full game from the keyboard script.

**Tech Stack:** C (PinMAME's simulator framework `src/wpc/sim.[ch]`, `-std=c99 -pedantic -Wall` clean), Python 3 (no third-party modules), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §3.3 and the scripted-game part of §3.4 (binding), parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` §4.5, §6, §7 (binding where the addendum is silent). Plan 8a (`docs/superpowers/plans/2026-09-29-pinheck-m8a-board.md`) defines the board outputs, numbering, input ports and keys this plan builds on.

## Prerequisites

- Plans 1–8a executed and merged: `pinheck` at `0fdf7bd2` or later, which carries Plan 8a's board block (`PINHECK_OUT_LOG`, `coreGlobals.solenoids` built each frame, `pinheck_getsol`, the cabinet ports and keys, machine check 5) and the display fix (`PINHECK_VIDEO_W`/`PINHECK_VIDEO_H`, the standard-size screen). Proven and replayed on exactly that commit. This plan edits PinMAME's simulator framework (`src/wpc/sim.[ch]`, one opt-in field), Plan 5's and 8a's `pinheck.c` (the board block's servo callback, reset and output log; the RTC seeding and the port-write callback; nothing in the renderer, vblank or sound blocks), `pinheck.h`, `pinheckgames.c`, Plan 8a's `check5.py`, `register_build.py` and `symbol_check.py`.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools`; `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip; `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`. Every command below uses these.
- The emulation runs at about 0.1× real time on the reference machine while another emulator runs, about 0.15× alone. Every PinMAME run below has a `timeout -k 30` (the emulator can outlive a plain `SIGTERM`). The simulator check runs about 8 minutes, machine check 5 about 25, the scripted game about 50 minutes.

## Global Constraints

- The simulator is PinMAME's own framework (`sim_tSimData`, `sim_tState`, `sim_tInportData`, `setState`, the manual shooter, `handleMech`/`getMech`), in the layout of PinMAME's full simulators: `src/wpc/sims/pinheck/dominos.c` holds Domino's ROMs, input ports, game data and simulator. It compiles warning-free in the PinMAME build and under `-std=c99 -pedantic -Wall -Werror` with PinMAME's headers; code comments stay short. Every public name outside it stays `pinheck_`-prefixed (`symbol_check.py` now covers `src/wpc/sims/pinheck/`).
- The framework change is opt-in and backwards compatible: `sim_tSimData` gains a last field, `autoBall`, which every existing simulator leaves 0.
- Numbering is Plan 8a's (spec §4.5): document switch *n* (0–63) = PinMAME `(n/8+1)*10 + n%8+1`; cabinet switch 13 = 95, 14 = 96; document coil *n* = PinMAME solenoid *n*+1; servo *n* = custom solenoid 57+*n*; the documents number switches and coils from 0.
- The simulator reads a coil as on when it was on at any time during the last frame (`coreGlobals.solenoids`), and reacts to that edge, never to a pulse length.
- Keys (the simulator's; the cabinet keys are Plan 8a's: coin `5`, start `1`, flippers Shift, tilt `Insert`, coin door `End`, Enter `0`, Back `7`, User `9`). `L/R Ctrl` + key picks the left or right one of a pair: `-` slingshot, `I` inlane, `O` outlane (drain), `S` scoop, `R` orbit, `N` Noid orbit, `B` pop bumper (`B` alone: lower pop), `L` Star lane / 5 lane. `C` Center Ramp, `V` Oven Ramp, `G` spinner, `Z`/`X`/`M` target bank left/middle/right, `F`/`H`/`J`/`K`/`U` Pizza Tracker targets (Delivery, Quality Check, Bake, Prepare, Order Placed), `Q` drain between the flippers, `Space` plunger (hold, release). The framework's own keys stay: `Del` toggles simulator keys and PinMAME's column/row switch keys, `Up`/`Down` select a ball, `LAlt` ignores the ball's location.
- Test hooks, inert unless set (as earlier plans'): `PINHECK_OUT_LOG` gains `<time> W <switch>=<0|1> …`, each frame, for every PinMAME switch that changed; `PINHECK_LINK_LOG=file` logs each PIC32-to-Propeller packet as `<seconds> <16 hex bytes> gap <cycles>`; `PINHECK_RTC=<seconds since 1970>` seeds the DS1340 instead of host time.
- Every sdl3pinmame launch in a check uses private `-nvram_directory` and `-cfg_directory` (the shared `~/.sdl3pinmame/cfg/dominos.cfg` keeps input settings, such as the `Del` toggle, between runs).
- Every existing suite stays green: `tests/pinheck/{storage,link,pic32mx,board,display,audio}/check.sh`, `pinmame_check.sh`, `pinmame_display.sh`, `pinmame_audio.sh`, `pinmame_board.sh` (the `mips32` and `p8x32a` suites cover code this plan does not touch).

## Rulings

Decisions the spec left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it). Addresses are `DOM_V006.PRG` flash addresses.

1. **The spec's coil numbers are the documents' 0-based numbers.** Addendum §3.3's 17, 16, 8/12, 6 and 5 are the solenoid list's `Ball Trough`, `Auto Launcher`, `Left`/`Right Scoop`, `Up Post` and `Magnet Coil`, which are PinMAME solenoids 18, 17, 9/13, 7 and 6 (spec §4.5: coil *n* → solenoid *n*+1). The firmware's solenoid test names agree (`LOAD BALL`, `PLUNGER`, `LEFT SCOOP`, `RIGHT SCOOP`, `STOP POST`, `MAGNET`).
2. **Domino's game definition moves into its simulator file**, `src/wpc/sims/pinheck/dominos.c`, as every full PinMAME simulator holds its game (`sims/wpc/full/*.c`, `sims/s11/full/dd.c`, `sims/se/prelim/*.c`: ROMs, input ports, game data with `&xxxSimData`); no games file references a simulator from outside. `pinheckgames.c` keeps the `pinheck` system set. The game data is `INIT_PINHECK`'s (flippers 4/3, lamp column, 14 custom solenoids, version 6 in `gameSpecific1` for `PINHECK_INSERVICE`) plus the simulator, mechanics handler and inputs; the display layout repeats `pinheckgames.c`'s one line with `pinheck.h`'s constants.
3. **The simulator reads coils as "on during the last frame"** (`coreGlobals.solenoids`, which Plan 8a's board block builds from every coil edge before the simulator runs), never `core_getSol` or `pulsedSolState`: the firmware's pulses are 3–20 ms, shorter than a frame. The simulator check drives the coils with 5 ms console pulses, and a mutant reading `pulsedSolState` fails it.
4. **Manual plunger.** The firmware waits with the ball in the shooter lane until it leaves (first probe: the ball sat there for the whole run); it fires the autolauncher only for ball saves and multiball. The simulator uses the framework's manual shooter (Space: hold, release) and moves the ball out on either.
5. **Optos.** PinMAME 95 (cabinet switch 13) is the Center Ramp and 96 (cabinet switch 14) the Oven Ramp: the switch dispatch at `0x9D017538` calls `0x9D02C0B0` on cabinet switch 14, which energises the magnet (`0x9D02FDA0`, coil 5); in a game a shot through 96 holds the magnet for 1.1 s, and the rules card's "Right Oven Ramp" locks balls. The wiring document's opto boards ("Scoop", "Noid Loop") do not name these two inputs.
6. **Up-post in the orbits.** The left and right orbit handlers (`0x9D028040` for switch 29, `0x9D023DE4` for switch 21) raise the post through `0x9D0307DC`. A ball entering an orbit meets the post 9 frames later: if the post came on since, it rolls back down the same orbit (its switch closes again); otherwise it goes round and out past the other orbit switch.
7. **Magnet.** A ball through the oven ramp reaches the magnet 6 frames after the opto and is caught if the magnet came on within the last 12 frames; it is held while the firmware keeps pulsing the magnet (every 11 ms) and released 6 frames after the last pulse.
8. **The Noid** is servo 0 as a continuous-rotation servo: pulses above 1.55 ms turn it one way, below 1.45 ms the other, pulses in between or none (detached) stop it; one turn takes 2 s and Noid Home (58) is closed for 1/12 of it. The firmware homes the Noid by that switch: it detaches the servo within 50 ms of Noid Home closing. The simulator sets Noid Home only when its value changes, so the switch test can toggle it by hand.
9. **The target bank** is servo 1: 646 µs = up (every ball starts there), 1,621 µs = down (after the three bank targets, clip `BCI`). Its targets (26–28) close in both positions: with the bank down the firmware still takes them, as hits on the Noid behind it (clip `NO3`, the servo driven down again for 1.4 s each). The simulator tracks the position (`getMech(1)`) without gating the targets.
10. **`autoBall`.** The framework's keys move only the selected ball (Up/Down), which a scripted game cannot follow once multiball changes the ball order (a probe drained one ball of a multiball; the other stayed on the playfield and ball 3 never came). `sim_tSimData` gains an opt-in last field, `autoBall`: while the selected ball is off the playfield, the keys move the first ball on it. Existing simulators leave it 0 and behave as before. Shot keys are tapped for one frame so that one tap moves one ball.
11. **The game's own progress is on the link; UART1 carries a few mode prints.** In `DOM_V006`, `PLAYER:%d BALL:%d SCORE: %luK` is formatted by `sprintf` (`0x9D01A8EC` → `0x9D02F408`) into the text sent to the Propeller, and `[E11000]` (`0x9D015FF4` → `0x9D030DB0`) prints only an empty line; in a game UART1 prints only mode notes (`pizzaDispatchModeStart[player] 0`, `COLLECTING JP??`, `RIGHT FLASHER`, `MODES ARE ENDING`) and, after it, the high scores sent to the display. `PINHECK_LINK_LOG` logs every PIC32-to-Propeller packet, and the scripted game asserts on the packets it identifies from their content: `0x03` score (player, 32-bit value), `0x0C` status (players: 0 before the first game, 1 since; ball, 0 when no game runs; player), `0x0F` scrolling text (an 8-character window: `PLAYER:1 BALL:2`), `0x02` clip (`DMD/_D<first letter>/<name>.VID`), `0x0E` high-score table entry (rank, score, initials), besides the UART1 lines.
12. **Repeatable runs: `PINHECK_RTC`.** The DS1340 is seeded from host time at every reset (spec §4.4), and the firmware's random choices follow it: two runs of the same script diverged at the right scoop (multiball in one, a 1M award in the other). The test hook `PINHECK_RTC=<seconds since 1970>` (inert unless set) seeds the clock instead; the scripted game and Milestone 9's determinism check use it.
13. **Check 5 and the simulator.** With the simulator, three balls rest in the trough (12–14) and the Noid at home (58), and PinMAME's column/row keys only reach the matrix after `Del` (the framework's Switch/Simulator toggle). Check 5 presses `Del` before the switch test and expects the resting switches with each key-toggled switch flipped.
14. **Ball search off in the scripted game** (`[E97000]`, as check 2): no unprompted coil moves a ball the script did not move.
15. **Free play.** The default settings are free play (the attract ticker shows `FREE PLA`); the script still inserts a coin, as the spec asks, and the check sees coin switch 7 close.


## Review Focus

- **More than one ball on the playfield** (multiball, a ball save): the keys must still move a ball, and every ball must drain back; otherwise the firmware waits for a ball that never comes (a probe without `autoBall` never got ball 3). The simulator check's two-ball step in Task 1 (two ejects, two `Q` taps, full trough; the `autoBall` mutant fails it), and the scripted game's multiball in Task 2 (a trough eject and an autolaunch during ball 2, the trough full when ball 3 is served).
- **Coil pulses shorter than a frame** (the firmware's trough, scoop and pop pulses are 3–20 ms): each must move the ball exactly once. The simulator check in Task 1 fires 5 ms console pulses and first checks they are shorter than a frame; a simulator reading the instantaneous coil state (`pulsedSolState`) fails 11 of its checks.
- **A coil that acts on a ball already on its way**: the firmware raises the up-post when the ball's orbit switch closes and energises the magnet when the oven opto closes, so the simulator must look back over the ball's travel, not only at the instant it arrives. The simulator check's orbit shots with and without the post, and the magnet hold with a shot that must be ignored while the ball is held.
- **Switches toggled by hand while the simulator holds balls** (the service switch test after `Del`): the simulator must not overwrite them, and the balls it holds must show as closed switches. Check 5's switch test in Task 1, which starts from the simulator's resting switches (Noid Home is only set when the Noid moves).
- **Firmware randomness seeded from the host clock**: a fixed key script must replay the same game, or it drifts into a different one (two probes on different clocks diverged at the right scoop). `PINHECK_RTC` in Task 2, and the scripted game run twice with byte-identical link, board, UART1 and frame logs.

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/sims/pinheck/dominos.c` | Domino's game definition and playfield simulator: ball states and keys, coil-driven moves, Noid and target-bank mechanics |
| `src/wpc/sim.[ch]` | the framework's opt-in `autoBall` (modified) |
| `src/wpc/pinheckgames.c` | keeps the `pinheck` system set; Domino's moves out (modified) |
| `src/wpc/pinheck.[ch]` | `pinheck_servo()`; `W` lines in `PINHECK_OUT_LOG`; `PINHECK_LINK_LOG`; `PINHECK_RTC` (modified) |
| `tests/pinheck/sim/sim.py`, `pinmame_sim.sh`, `.gitignore` | simulator check: console coil pulses and the servo test against the switches the simulator closes |
| `tests/pinheck/board/check5.py` | check 5 with the simulator's resting switches (modified) |
| `tests/pinheck/link/register_build.py`, `symbol_check.py`, build files | the simulator in every build; its names checked (modified / regenerated) |
| `tests/pinheck/game/link.py`, `test_link.py` | link packet decoding and its unit test |
| `tests/pinheck/game/game.py`, `pinmame_game.sh`, `.gitignore` | the scripted game and its checks |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | what the simulator and the scripted game established (modified) |

---
### Task 1: Playfield simulator

**Files:**
- Create: `src/wpc/sims/pinheck/dominos.c`, `tests/pinheck/sim/sim.py`, `tests/pinheck/sim/pinmame_sim.sh`, `tests/pinheck/sim/.gitignore`
- Modify: `src/wpc/sim.h`, `src/wpc/sim.c`, `src/wpc/pinheck.c`, `src/wpc/pinheck.h`, `src/wpc/pinheckgames.c`, `tests/pinheck/board/check5.py`, `tests/pinheck/display/render.py`, `tests/pinheck/link/register_build.py`, `tests/pinheck/link/symbol_check.py`; regenerated: `src/pinmame.mak`, `cmake/*/CMakeLists*.txt`, `vcproj/*.vcxproj(.filters)`

**Interfaces:**
- Consumes: Plan 8a's board block: `coreGlobals.solenoids` = the coils (bits 0–23 = solenoids 1–24) that were on at any time during the last frame, set by `pinheck_brd_vblank` before `core_updateSw` runs the simulator; `PINHECK_OUT_LOG`; `pinheck_getsol`; `PINHECK_INPUT_PORTS_START`, `PINHECK_SWLFLIP`, `PINHECK_SWRFLIP`, `PINHECK_CUSTSOLS`, `PINHECK_ROMSTART`; the display fix's `pinheck_video`, `PINHECK_VIDEO_W`, `PINHECK_VIDEO_H`. PinMAME's framework: `sim_tSimData`, `sim_tState`, `sim_tInportData`, `setState`, `sim_getSol(sShooterRel)`, `core_setSw`, `hw.handleMech`/`hw.getMech`.
- Produces: `int pinheck_servo(int servo)` in `pinheck.h` (servo 0–4: the last pulse width in µs, 0 while it gets no pulses); `PINHECK_OUT_LOG` lines `<time> W <switch>=<0|1> …` for every PinMAME switch that changed since the last frame; `sim_tSimData.autoBall` (`int`, last field); the `dominos` game with its simulator and keys (Global Constraints); `getMech(0)` = Noid angle in frames (0–119, 0 = home), `getMech(1)` = target bank down.

The check runs the in-service machine twice, as check 5: a first boot on blank NVRAM, then attract mode, where nothing moves a ball unless the check does. It moves one ball round the playfield with the firmware's console coil commands (`[MXX002]`: about 5 ms, shorter than a frame) and the simulator's keys: trough to shooter lane, autolauncher, left scoop and its kick, an orbit with and without the up-post, the oven ramp onto a held magnet, the drain; then two balls at once, drained with two `Q` taps; then the service menu's `NOID RIGHT` turns the Noid past Noid Home until `NOID STOP`. Every step is read back from the switches the simulator closes (`W` lines) against the coils and servo pulses the board logged.

- [ ] **Step 1: Write the simulator check**

`tests/pinheck/sim/sim.py`:

```python
#!/usr/bin/env python3
"""Playfield simulator check: in attract mode the firmware's console coil and servo commands
([MXXzzz], [SXXzzz]) and the simulator keys move the balls; the switches the simulator closes
(PINHECK_OUT_LOG 'W' lines) must follow the coils and servos.
  sim.py plan DIR     write DIR/send, send_at, send_gap, keys.txt, frames
  sim.py verify DIR   check DIR/out2.log against the plan"""
import sys

FPS = 60
SEND_AT, GAP = 10.0, 0.05
# (time, console command) and (time, keys, hold frames): one ball's trip round the playfield
# [MXX002] pulses a coil for 30 firmware loop units, about 5 ms: shorter than a frame
SEND = [(10.0, '[E97000]'), (12.0, '[M17002]'), (13.5, '[M16002]'), (17.0, '[M08002]'),
        (19.55, '[M06250]')] + [(20.95 + 0.05 * i, '[M05250]') for i in range(20)] + \
       [(28.0, '[M17002]'), (29.0, '[M16002]'), (29.5, '[M17002]'), (30.5, '[M16002]')]
KEYS = [(15.0, 'LCONTROL S', 1), (18.0, 'LCONTROL R', 1), (19.5, 'LCONTROL R', 1), (21.0, 'V', 1),
        (21.6, 'F', 1), (23.5, 'F', 1), (26.0, 'Q', 1), (32.0, 'Q', 1), (32.5, 'Q', 1),
        # service menu: SERVO TEST, NOID RIGHT runs the Noid's continuous-rotation servo, NOID STOP stops it
        (35.0, '0', 6), (36.0, 'RSHIFT', 6), (37.0, 'RSHIFT', 6), (38.0, 'RSHIFT', 6), (39.0, '0', 6), (40.0, '0', 6),
        (44.0, 'RSHIFT', 6), (45.0, '0', 6)]
END = 48.0


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

    rest = sorted(s for s in (12, 13, 14, 11) if state(s, 9.0))
    check(rest == [12, 13, 14], 'at rest closed %s, expected the trough 12 13 14' % rest)
    eject = coil_on(18, 12.0, 13.0)
    check(pulse(18, 12.0) < 1.0 / FPS and pulse(17, 13.5) < 1.0 / FPS and pulse(9, 16.9) < 1.0 / FPS,
          'the test pulses are not shorter than a frame: %.4f %.4f %.4f s' % (pulse(18, 12.0), pulse(17, 13.5), pulse(9, 16.9)))
    check(eject and edges(12, eject[0], eject[0] + 0.1, 0) and edges(11, eject[0], eject[0] + 1.0, 1),
          'trough: [M17002] at 12.0 (coil 18 %s) did not move trough 1 (12) to the shooter lane (11)' % eject[:1])
    check(state(12, 13.4) == 1 and state(14, 13.4) == 0, 'trough: the next ball did not roll down to 12')
    launch = coil_on(17, 13.5, 14.5)
    check(launch and edges(11, launch[0], launch[0] + 0.1, 0), 'autolauncher: [M16002] did not clear the shooter lane')
    kick = coil_on(9, 16.9, 17.5)
    check(state(25, 16.9) == 1 and kick and edges(25, kick[0], kick[0] + 0.1, 0),
          'left scoop: held %d, coil 9 %s, released %s' % (state(25, 16.9), kick[:1], edges(25, 16.9, 17.5, 0)))
    check(edges(46, 18.0, 18.2, 1) and edges(36, 18.0, 18.6, 1), 'orbit: a left orbit shot without the post did not reach the right orbit (36)')
    post = coil_on(7, 19.5, 19.7)
    check(post and len(edges(46, 19.5, 20.2, 1)) == 2 and not edges(36, 19.5, 20.6),
          'up-post: coil 7 at %s, left orbit closed %s, right orbit %s: the ball must come back down the left orbit' %
          (post[:1], edges(46, 19.5, 20.2, 1), edges(36, 19.5, 20.6)))
    check(edges(96, 21.0, 21.2, 1), 'oven ramp opto (96) did not close')
    check(not edges(31, 21.5, 22.0), 'magnet: the ball left the magnet while coil 6 held it (Delivery target 31 closed)')
    check(edges(31, 23.5, 23.7, 1), 'magnet: the ball was not released (Delivery target 31 stayed open)')
    check(edges(14, 26.0, 27.5, 1) and [state(s, 27.9) for s in (11, 12, 13, 14)] == [0, 1, 1, 1],
          'drain: the ball did not return to the trough (11-14: %s)' % [state(s, 27.9) for s in (11, 12, 13, 14)])
    served = [t for t in coil_on(18, 27.9, 31.0)]
    check(len(edges(11, 27.9, 31.0, 1)) == 2 and len(edges(11, 27.9, 31.5, 0)) == 2,
          'two balls: two ejects (coil 18 at %s) did not reach and leave the shooter lane' % served[:1])
    check(len(edges(14, 32.0, 34.4, 1)) == 2 and [state(s, 34.4) for s in (11, 12, 13, 14)] == [0, 1, 1, 1],
          'two balls: Q twice did not drain both balls on the playfield (11-14: %s)' % [state(s, 34.4) for s in (11, 12, 13, 14)])
    run = [t for t, n, us in servo if n == 0 and 40.0 <= t < 44.0 and 500 < us < 600]
    home = edges(58, 40.0, 44.0)
    check(run and len(home) >= 3, 'Noid: servo 0 pulses %d times at 544 us, Noid Home (58) changed %s' % (len(run), home))
    check(not edges(58, 45.3, 48.0), 'Noid: still turning after NOID STOP')
    for f in fails:
        print('SIM FAIL: ' + f)
    print('sim: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'verify'):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2]))
```

`tests/pinheck/sim/pinmame_sim.sh`:

```sh
#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
python3 sim.py plan $B || exit 2
launch() {
	(cd $B && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out$1.log PINHECK_UART1_LOG=$PWD/uart$1.log \
		timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -cfg_directory cfg -headless -frames_to_run $2 -skip_gamewarnings -nothrottle $3 > run$1.out 2>&1) \
		|| { echo "SIM FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
launch 1 1200
grep -q ' W .*12=1' $B/out1.log || { echo "SIM FAIL: launch 1 logged no ball in the trough (PINHECK_OUT_LOG 'W' lines)"; exit 1; }
PINHECK_UART1_SEND_AT=$(cat $B/send_at) PINHECK_UART1_SEND_GAP=$(cat $B/send_gap) PINHECK_UART1_SEND="$(cat $B/send)" \
	launch 2 "$(cat $B/frames)" "-key_script keys.txt"
S=../../../src
cc -std=c99 -pedantic -Wall -Werror -fsyntax-only -DPINMAME -DMAMEVER=7300 -DLSB_FIRST '-DINLINE=static __inline__' -DPI=M_PI \
	-I$S -I$S/wpc -I$S/unix -I$S/unix/sysdep $S/wpc/sims/pinheck/dominos.c || { echo "SIM FAIL: dominos.c does not compile cleanly"; exit 1; }
python3 sim.py verify $B || exit 1
echo "pinmame sim: ok"
```

`tests/pinheck/sim/.gitignore`:

```text
build/
```

`chmod +x tests/pinheck/sim/sim.py tests/pinheck/sim/pinmame_sim.sh`

- [ ] **Step 2: Confirm it fails before the simulator**

Build PinMAME as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`), then run the check:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m8b.log 2>&1; tail -1 build/sdl3pinmame/m8b.log
SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/sim/pinmame_sim.sh
```
Expected: `[100%] Built target sdl3pinmame`, then after the first launch (about 3 minutes; exit 1):
```text
SIM FAIL: launch 1 logged no ball in the trough (PINHECK_OUT_LOG 'W' lines)
```

- [ ] **Step 3: The framework field, the driver's servo accessor and switch log, and the simulator**

`sim.[ch]` gain `autoBall`; `pinheck.[ch]` gain `pinheck_servo()` and the `W` lines (in Plan 8a's board block: the servo callback, the reset and the per-frame output log); Domino's game definition leaves `pinheckgames.c`; `register_build.py` learns the simulator (a `PINGAMES` line, the object directory, the same cmake and vcxproj lists as the other pinHeck files); `symbol_check.py` covers `src/wpc/sims/pinheck/`; `render.py` (the display check's screen snapshot) leaves the simulator's ball list and plunger bar, drawn in white right of the core's panel, out of its panel check, since white is also a frame colour.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/sim.h', [
    (r'''  int manShooter; /* true if a manual shooter should be simulated */
  int (*keyCond)(int cond, int ballState, int *inports); /* advanced key conditions */
} sim_tSimData;

''',
     r'''  int manShooter; /* true if a manual shooter should be simulated */
  int (*keyCond)(int cond, int ballState, int *inports); /* advanced key conditions */
  int autoBall;   /* true: while the selected ball is off the playfield, the keys move the first ball on it */
} sim_tSimData;

'''),
])
edit('src/wpc/sim.c', [
    (r'''    if (locals.currBall < 0)                     locals.currBall = noOfBalls -1;
    if (locals.currBall >= noOfBalls)            locals.currBall = 0;
  locals.balls[locals.currBall].current = TRUE;

''',
     r'''    if (locals.currBall < 0)                     locals.currBall = noOfBalls -1;
    if (locals.currBall >= noOfBalls)            locals.currBall = 0;
    if (simData->autoBall && locals.balls[locals.currBall].state != stFree)
      for (ii = 0; ii < noOfBalls; ii++)
        if (locals.balls[ii].state == stFree) { locals.currBall = ii; break; }
  locals.balls[locals.currBall].current = TRUE;

'''),
])
edit('src/wpc/pinheck.c', [
    (r'''static UINT16 brd_gi8_seen;
static UINT8 brd_cust[PINHECK_NSOLS - PINHECK_SOL_RGB], brd_logged[PINHECK_NLAMPS + PINHECK_NSOLS];

static uint8_t pinheck_brd_swcol(void *ctx, int col) { (void)ctx; return coreGlobals.swMatrix[col + 1]; }
''',
     r'''static UINT16 brd_gi8_seen;
static UINT8 brd_cust[PINHECK_NSOLS - PINHECK_SOL_RGB], brd_logged[PINHECK_NLAMPS + PINHECK_NSOLS];
static int brd_servo_us[BOARD_SERVOS];
static UINT8 brd_sw_logged[10];

static uint8_t pinheck_brd_swcol(void *ctx, int col) { (void)ctx; return coreGlobals.swMatrix[col + 1]; }
'''),
    (r'''	(void)ctx;
	if (brd_log) fprintf(brd_log, "%.9f V %d %.1f %llu\n", timer_get_time(), servo, us, (unsigned long long)t);
	if (!pulse) return; /* no pulses: the servo holds its position */
	if (v < 0.0) v = 0.0;
''',
     r'''	(void)ctx;
	if (brd_log) fprintf(brd_log, "%.9f V %d %.1f %llu\n", timer_get_time(), servo, us, (unsigned long long)t);
	brd_servo_us[servo] = pulse ? (int)(us + 0.5) : 0;
	if (!pulse) return; /* no pulses: the servo holds its position */
	if (v < 0.0) v = 0.0;
'''),
    (r'''static int pinheck_sw2m(int no) { return (no / 10) * 8 + no % 10 - 1; }
static int pinheck_m2sw(int col, int row) { return col * 10 + row + 1; }

int pinheck_getsol(int solNo)
''',
     r'''static int pinheck_sw2m(int no) { return (no / 10) * 8 + no % 10 - 1; }
static int pinheck_m2sw(int col, int row) { return col * 10 + row + 1; }

/* last pulse width of servo 0-4 in microseconds, 0 while it gets no pulses (for the playfield simulator) */
int pinheck_servo(int servo)
{
	return servo >= 0 && servo < BOARD_SERVOS ? brd_servo_us[servo] : 0;
}

int pinheck_getsol(int solNo)
'''),
    (r'''	}
	if (n) fprintf(brd_log, "%.9f P%s\n", timer_get_time(), line);
}

''',
     r'''	}
	if (n) fprintf(brd_log, "%.9f P%s\n", timer_get_time(), line);
	/* switches (PinMAME numbers) that changed since the last frame, e.g. by the simulator */
	for (n = 0, i = 0; i < 80; i++)
		if (((coreGlobals.swMatrix[i / 8] ^ brd_sw_logged[i / 8]) >> (i % 8)) & 1)
			n += sprintf(line + n, " %d=%d", coreData->m2sw(i / 8, i % 8), (coreGlobals.swMatrix[i / 8] >> (i % 8)) & 1);
	memcpy(brd_sw_logged, (void *)coreGlobals.swMatrix, sizeof(brd_sw_logged));
	if (n) fprintf(brd_log, "%.9f W%s\n", timer_get_time(), line);
}

'''),
    (r'''	core_write_pwm_output_8b(CORE_MODOUT_SOL0 + PINHECK_SOL_GI8, 0);
	for (i = PINHECK_SOL_RGB; i < PINHECK_NSOLS; i++) pinheck_brd_level(i, 0);
	coreGlobals.pulsedSolState = 0;
	brd_rgb_extra = 0;
''',
     r'''	core_write_pwm_output_8b(CORE_MODOUT_SOL0 + PINHECK_SOL_GI8, 0);
	for (i = PINHECK_SOL_RGB; i < PINHECK_NSOLS; i++) pinheck_brd_level(i, 0);
	memset(brd_servo_us, 0, sizeof(brd_servo_us));
	coreGlobals.pulsedSolState = 0;
	brd_rgb_extra = 0;
'''),
])
edit('src/wpc/pinheck.h', [
    (r'''extern PINMAME_VIDEO_UPDATE(pinheck_video);
extern int pinheck_getsol(int solNo);
extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK
''',
     r'''extern PINMAME_VIDEO_UPDATE(pinheck_video);
extern int pinheck_getsol(int solNo);
extern int pinheck_servo(int servo);
extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK
'''),
])
edit('src/wpc/pinheckgames.c', [
    (r'''GAMEX(2014,pinheck,0,PINHECK,pinheck,pinheck,ROT0,"Spooky Pinball","pinHeck System",NOT_A_DRIVER)

/*-------------------------------------------------------------------
/ Domino's Spectacular Pinball Adventure (2016)
/-------------------------------------------------------------------*/
INIT_PINHECK(dominos, 3, 6)
PINHECK_ROMSTART(dominos, "DOM_V006.PRG", 0x31990, CRC(750e27a4) SHA1(3fbebce7f885563e61dd2e4f5d7f0a8a54c53b23),
                 "PRP_V008.BIN", CRC(a51ee28d) SHA1(0556d88b6f0c7cb15e648e1f76f4d47890ddb858))
PINHECK_ROMEND
CORE_CLONEDEFNV(dominos, pinheck, "Domino's Spectacular Pinball Adventure", 2016, "Spooky Pinball", gl_mPINHECK, GAME_NOT_WORKING)
''',
     r'''GAMEX(2014,pinheck,0,PINHECK,pinheck,pinheck,ROT0,"Spooky Pinball","pinHeck System",NOT_A_DRIVER)

/* Domino's Spectacular Pinball Adventure: sims/pinheck/dominos.c */
'''),
])
edit('tests/pinheck/display/render.py', [
    (r'''drawn SCALE x SCALE at the top left, and everything else is black apart from the core's
lamp/switch/solenoid panel, which starts 3 rows under the display and uses only the core's
own pens, never an RGB332 frame colour."""
import argparse
import struct
''',
     r'''drawn SCALE x SCALE at the top left, and everything else is black apart from the core's
lamp/switch/solenoid panel, which starts 3 rows under the display and uses only the core's
own pens, never an RGB332 frame colour, and a game simulator's ball list and plunger bar to its
right (from x = 130, in white: sim.c's sim_draw), which are not judged."""
import argparse
import struct
'''),
    (r'''SCREEN_H = 256            # CORE_SCREENY: standalone PinMAME shows the full screen height
TOL = 7                   # 8 -> 5 bit -> 8 bit rounding of a 15 bpp screen


''',
     r'''SCREEN_H = 256            # CORE_SCREENY: standalone PinMAME shows the full screen height
TOL = 7                   # 8 -> 5 bit -> 8 bit rounding of a 15 bpp screen
SIM_X = 128               # the simulator's plunger bar starts at x = 130, its ball list at 160


'''),
    (r'''    bad = [(x, y) for y in range(min(h, panel)) for x in range(w)
           if not (y < H * SCALE and x < W * SCALE) and not near(rows[y][x], (0, 0, 0))]
    pens = set(rows[y][x] for y in range(panel, h) for x in range(w)) - {(0, 0, 0)}
    shown = [tuple((c >> 3) << 3 | c >> 5 for c in rgb332(v)) for v in range(1, 256)]  # as a 15 bpp screen shows them
    frame_pens = [p for p in pens if any(all(abs(a - b) <= 1 for a, b in zip(p, q)) for q in shown)]
''',
     r'''    bad = [(x, y) for y in range(min(h, panel)) for x in range(w)
           if not (y < H * SCALE and x < W * SCALE) and not near(rows[y][x], (0, 0, 0))]
    pens = set(rows[y][x] for y in range(panel, h) for x in range(min(w, SIM_X))) - {(0, 0, 0)}
    shown = [tuple((c >> 3) << 3 | c >> 5 for c in rgb332(v)) for v in range(1, 256)]  # as a 15 bpp screen shows them
    frame_pens = [p for p in pens if any(all(abs(a - b) <= 1 for a, b in zip(p, q)) for q in shown)]
'''),
])
edit('tests/pinheck/link/register_build.py', [
    (r'''    (['src/wpc/pinheck/board.c', 'src/wpc/pinheck/board.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/board.o\n'),
]
changed = []
''',
     r'''    (['src/wpc/pinheck/board.c', 'src/wpc/pinheck/board.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/board.o\n'),
]
# game definitions with a playfield simulator: (source, pinmame.mak line after pinheckgames.o)
SIMS = [
    ('src/wpc/sims/pinheck/dominos.c', 'PINGAMES += $(PINOBJ)/sims/pinheck/dominos.o\n'),
]
changed = []
'''),
    (r'''    edit('src/pinmame.mak', 'DRVLIBS += $(PINOBJ)/pinheck.o\n', drvlibs)
edit('src/pinmame.mak', 'OBJDIRS += $(PINOBJ)\n', 'OBJDIRS += $(PINOBJ)/pinheck $(OBJ)/cpu/p8x32a\n')

for path in sorted(glob.glob('cmake/*/CMakeLists*.txt')):
''',
     r'''    edit('src/pinmame.mak', 'DRVLIBS += $(PINOBJ)/pinheck.o\n', drvlibs)
edit('src/pinmame.mak', 'OBJDIRS += $(PINOBJ)\n', 'OBJDIRS += $(PINOBJ)/pinheck $(OBJ)/cpu/p8x32a\n')
for path, pingames in SIMS:
    if not os.path.exists(path):
        continue
    srcs.append(path)
    edit('src/pinmame.mak', 'PINGAMES += $(PINOBJ)/pinheckgames.o\n', pingames)
    edit('src/pinmame.mak', 'OBJDIRS += $(PINOBJ)/sims/se/prelim\n', 'OBJDIRS += $(PINOBJ)/sims/pinheck\n')

for path in sorted(glob.glob('cmake/*/CMakeLists*.txt')):
'''),
])
edit('tests/pinheck/link/symbol_check.py', [
    (r'''ours = [p for p in glob.glob(root + '/src/cpu/mips32/*.c') + glob.glob(root + '/src/cpu/pic32mx/*.c')
        + glob.glob(root + '/src/cpu/p8x32a/*.c') + glob.glob(root + '/src/wpc/pinheck/*.c')
        + glob.glob(root + '/src/wpc/pinheck*.c')]
mine = {}
for p in ours:
''',
     r'''ours = [p for p in glob.glob(root + '/src/cpu/mips32/*.c') + glob.glob(root + '/src/cpu/pic32mx/*.c')
        + glob.glob(root + '/src/cpu/p8x32a/*.c') + glob.glob(root + '/src/wpc/pinheck/*.c')
        + glob.glob(root + '/src/wpc/pinheck*.c') + glob.glob(root + '/src/wpc/sims/pinheck/*.c')]
mine = {}
for p in ours:
'''),
])
EOF
```

`src/wpc/sims/pinheck/dominos.c`:

```c
// license:BSD-3-Clause

/*******************************************************************************
 Domino's Spectacular Pinball Adventure (Spooky Pinball, 2016): game definition
 and playfield simulator.

 The simulator models only the mechanics the rules depend on: the trough and
 shooter lane (a manual plunger), the autolauncher, both scoops, the orbits with
 the up-post, the oven ramp with the magnet, the Noid and its target bank, and
 the drain.
 Coils are read as "on at some time during the last frame" (coreGlobals.solenoids,
 set by the board block), so a pulse of a few milliseconds is never missed.
 ******************************************************************************/

/*------------------------------------------------------------------------------
  Keys (L/R Ctrl selects the left or right one of a pair):
    +-  L/R Slingshot        +I  L/R Inlane         +O  L/R Outlane (drain)
    +S  L/R Scoop            +R  L/R Orbit          +N  L/R Noid Orbit
    +B  L/R Pop Bumper        B  Lower Pop Bumper   +L  Star/5 Lane
     C  Center Ramp           V  Oven Ramp           G  Spinner
     Z/X/M  Target Bank left/middle/right (the Noid when the bank is down)
     F/H/J/K/U  Pizza Tracker targets (Delivery .. Order Placed)
     Q  Drain between the flippers           Space  Plunger (hold, release)
  The keys move a ball on the playfield (autoBall); Up/Down still select one.
------------------------------------------------------------------------------*/

#include "driver.h"
#include "core.h"
#include "sim.h"
#include "sndbrd.h"
#include "pinheck.h"

PINHECK_INPUT_PORTS_START(dominos, 3)
  PORT_START /* 0 */
    COREPORT_BIT(0x0001, "Left Qualifier",   KEYCODE_LCONTROL)
    COREPORT_BIT(0x0002, "Right Qualifier",  KEYCODE_RCONTROL)
    COREPORT_BIT(0x0004, "L/R Slingshot",    KEYCODE_MINUS)
    COREPORT_BIT(0x0008, "L/R Inlane",       KEYCODE_I)
    COREPORT_BIT(0x0010, "L/R Outlane",      KEYCODE_O)
    COREPORT_BIT(0x0020, "L/R Scoop",        KEYCODE_S)
    COREPORT_BIT(0x0040, "L/R Orbit",        KEYCODE_R)
    COREPORT_BIT(0x0080, "L/R Noid Orbit",   KEYCODE_N)
    COREPORT_BIT(0x0100, "L/R/Lower Pop",    KEYCODE_B)
    COREPORT_BIT(0x0200, "Star/5 Lane",      KEYCODE_L)
    COREPORT_BIT(0x0400, "Center Ramp",      KEYCODE_C)
    COREPORT_BIT(0x0800, "Oven Ramp",        KEYCODE_V)
    COREPORT_BIT(0x1000, "Spinner",          KEYCODE_G)
    COREPORT_BIT(0x2000, "Drain",            KEYCODE_Q)
  PORT_START /* 1 */
    COREPORT_BIT(0x0001, "Bank Left",        KEYCODE_Z)
    COREPORT_BIT(0x0002, "Bank Middle",      KEYCODE_X)
    COREPORT_BIT(0x0004, "Bank Right",       KEYCODE_M)
    COREPORT_BIT(0x0008, "Delivery",         KEYCODE_F)
    COREPORT_BIT(0x0010, "Quality Check",    KEYCODE_H)
    COREPORT_BIT(0x0020, "Bake",             KEYCODE_J)
    COREPORT_BIT(0x0040, "Prepare",          KEYCODE_K)
    COREPORT_BIT(0x0080, "Order Placed",     KEYCODE_U)
PINHECK_INPUT_PORTS_END

/* switches: document switch n (0-63) is (n/8+1)*10 + n%8+1; the optos are cabinet inputs 13 and 14 */
#define swShooter   11
#define swTrough1   12
#define swTrough2   13
#define swTrough3   14
#define swRSling    16
#define swRInlane   17
#define swROutlane  18
#define swLSling    22
#define swLInlane   23
#define swLOutlane  24
#define swLScoop    25
#define swBankR     26
#define swBankM     27
#define swBankL     28
#define swDelivery  31
#define swQuality   32
#define swBake      33
#define swPrepare   34
#define swOrder     35
#define swROrbit    36
#define swRNoidOrb  37
#define swLNoidOrb  38
#define swStarLane  41
#define sw5Lane     42
#define swRPop      43
#define swLowerPop  44
#define swLPop      45
#define swLOrbit    46
#define swSpinner   47
#define swRScoop    48
#define swNoidHome  58
#define swRampOpto  95
#define swOvenOpto  96

/* solenoids: document coil n is PinMAME solenoid n+1 */
#define sMagnet     6
#define sPost       7
#define sLScoop     9
#define sRScoop     13
#define sLaunch     17
#define sLoad       18

#define NOID_TURN   120  /* frames per Noid revolution */
#define NOID_HOME   10   /* frames of it with the home switch closed */

static struct {
  int since[25];   /* frames since each coil was last on */
  int noid;        /* Noid angle, 0 .. NOID_TURN-1, 0 = home */
  int home;        /* Noid Home as last set, -1 = not yet */
  int bankDown;    /* target bank lowered: servo 1 last pulsed above 1.5 ms */
} locals;

static int sol(int n) { return (coreGlobals.solenoids >> (n - 1)) & 1; }

enum { stTrough3 = SIM_FIRSTSTATE, stTrough2, stTrough1, stShooter, stLaunched, stDrain,
       stLOutlane, stROutlane, stLInlane, stRInlane, stLSling, stRSling,
       stLScoop, stRScoop, stLOrbit, stROrbit, stLOrbitOut, stROrbitOut, stLOrbitBack, stROrbitBack,
       stLNoidOrb, stRNoidOrb, stLNoidOut, stRNoidOut, stCRamp, stOvenRamp, stMagnet,
       stLPop, stRPop, stLowerPop, stStarLane, st5Lane, stSpinner,
       stBankL, stBankM, stBankR, stDelivery, stQuality, stBake, stPrepare, stOrder };

static sim_tState dominos_stateDef[] = {
  {"Not Installed", 0, 0,          0, stDrain,  0, 0, 0, SIM_STNOTEXCL},
  {"Moving"},
  {"Playfield",     0, 0,          0, 0,        0, 0, 0, SIM_STNOTEXCL},

  {"Trough 3",      1, swTrough3,  0, stTrough2, 3},
  {"Trough 2",      1, swTrough2,  0, stTrough1, 3},
  {"Trough 1",      1, swTrough1,  0, 0,         0},
  {"Shooter Lane",  1, swShooter,  0, 0,         0},
  {"Launched",      1, 0,          0, stFree,   10},
  {"Drain",         1, 0,          0, stTrough3, 1, 0, 0, SIM_STNOTEXCL},

  {"Left Outlane",  1, swLOutlane, 0, stDrain,  10},
  {"Right Outlane", 1, swROutlane, 0, stDrain,  10},
  {"Left Inlane",   1, swLInlane,  0, stFree,    5},
  {"Right Inlane",  1, swRInlane,  0, stFree,    5},
  {"Left Sling",    1, swLSling,   0, stFree,    2},
  {"Right Sling",   1, swRSling,   0, stFree,    2},

  {"Left Scoop",    1, swLScoop,   0, 0,         0},
  {"Right Scoop",   1, swRScoop,   0, 0,         0},
  {"Left Orbit",    1, swLOrbit,   0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"Right Orbit",   1, swROrbit,   0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"L Orbit Exit",  1, swROrbit,   0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"R Orbit Exit",  1, swLOrbit,   0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"L Orbit Post",  1, swLOrbit,   0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"R Orbit Post",  1, swROrbit,   0, stFree,    5, 0, 0, SIM_STNOTEXCL},

  {"L Noid Orbit",  1, swLNoidOrb, 0, stLNoidOut, 3, 0, 0, SIM_STNOTEXCL},
  {"R Noid Orbit",  1, swRNoidOrb, 0, stRNoidOut, 3, 0, 0, SIM_STNOTEXCL},
  {"L Noid Exit",   1, swRNoidOrb, 0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"R Noid Exit",   1, swLNoidOrb, 0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Center Ramp",   1, swRampOpto, 0, stFree,   10, 0, 0, SIM_STNOTEXCL},
  {"Oven Ramp",     1, swOvenOpto, 0, 0,         0, 0, 0, SIM_STNOTEXCL},
  {"Magnet",        1, 0,          0, 0,         0},

  {"Left Pop",      1, swLPop,     0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Right Pop",     1, swRPop,     0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Lower Pop",     1, swLowerPop, 0, stFree,    2, 0, 0, SIM_STNOTEXCL},
  {"Star Lane",     1, swStarLane, 0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"5 Lane",        1, sw5Lane,    0, stFree,    5, 0, 0, SIM_STNOTEXCL},
  {"Spinner",       1, swSpinner,  0, stFree,    5, 0, 0, SIM_STNOTEXCL | SIM_STSPINNER},

  {"Bank Left",     1, swBankL,    0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Bank Middle",   1, swBankM,    0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Bank Right",    1, swBankR,    0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Delivery",      1, swDelivery, 0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Quality Check", 1, swQuality,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Bake",          1, swBake,     0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Prepare",       1, swPrepare,  0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {"Order Placed",  1, swOrder,    0, stFree,    3, 0, 0, SIM_STNOTEXCL},
  {0}
};

/* frames: the ball meets the up-post 9 frames after the orbit switch and is stopped if the post rose since;
   it reaches the magnet 6 frames after the oven opto and is caught if the magnet came on within 12 frames */
#define ORBIT_POST   9
#define MAGNET_CATCH 12
#define MAGNET_DROP  6   /* the firmware pulses the held magnet every 11 ms: 6 frames off is a release */

static int dominos_handleBallState(sim_tBallStatus *ball, int *inports) {
  (void)inports;
  switch (ball->state) {
    case stTrough1:   if (sol(sLoad))   return setState(stShooter, 5); break;
    case stShooter:   if (sol(sLaunch) || sim_getSol(sShooterRel)) return setState(stLaunched, 2); break;
    case stLScoop:    if (sol(sLScoop)) return setState(stFree, 5); break;
    case stRScoop:    if (sol(sRScoop)) return setState(stFree, 5); break;
    case stLOrbit:    return setState(locals.since[sPost] < ORBIT_POST ? stLOrbitBack : stLOrbitOut, 3);
    case stROrbit:    return setState(locals.since[sPost] < ORBIT_POST ? stROrbitBack : stROrbitOut, 3);
    case stOvenRamp:  return setState(locals.since[sMagnet] < MAGNET_CATCH ? stMagnet : stFree, 5);
    case stMagnet:    if (locals.since[sMagnet] > MAGNET_DROP) return setState(stFree, 5); break;
  }
  return 0;
}

/* the Noid turns while its continuous-rotation servo gets pulses away from 1.5 ms */
static void dominos_handleMech(int mech) {
  int i, us = pinheck_servo(0), bank = pinheck_servo(1);
  (void)mech;
  if (bank) locals.bankDown = bank > 1500;
  for (i = 1; i <= 24; i++)
    locals.since[i] = sol(i) ? 0 : locals.since[i] < 10000 ? locals.since[i] + 1 : 10000;
  if (us > 1550) locals.noid = (locals.noid + 1) % NOID_TURN;
  else if (us && us < 1450) locals.noid = (locals.noid + NOID_TURN - 1) % NOID_TURN;
  if ((locals.noid < NOID_HOME) != locals.home) /* only on change: the switch test may toggle it by hand */
    core_setSw(swNoidHome, locals.home = locals.noid < NOID_HOME);
}

static int dominos_getMech(int mechNo) {
  return mechNo == 0 ? locals.noid : mechNo == 1 ? locals.bankDown : 0;
}

static sim_tInportData dominos_inportData[] = {
  {0, 0x0005, stLSling},   {0, 0x0006, stRSling},
  {0, 0x0009, stLInlane},  {0, 0x000a, stRInlane},
  {0, 0x0011, stLOutlane}, {0, 0x0012, stROutlane},
  {0, 0x0021, stLScoop},   {0, 0x0022, stRScoop},
  {0, 0x0041, stLOrbit},   {0, 0x0042, stROrbit},
  {0, 0x0081, stLNoidOrb}, {0, 0x0082, stRNoidOrb},
  {0, 0x0101, stLPop},     {0, 0x0102, stRPop},     {0, 0x0100, stLowerPop},
  {0, 0x0201, stStarLane}, {0, 0x0202, st5Lane},
  {0, 0x0400, stCRamp},    {0, 0x0800, stOvenRamp}, {0, 0x1000, stSpinner},
  {0, 0x2000, stDrain},
  {1, 0x0001, stBankL},    {1, 0x0002, stBankM},    {1, 0x0004, stBankR},
  {1, 0x0008, stDelivery}, {1, 0x0010, stQuality},  {1, 0x0020, stBake},
  {1, 0x0040, stPrepare},  {1, 0x0080, stOrder},
  {0}
};

static sim_tSimData dominosSimData = {
  2,                    /* 2 game specific input ports */
  dominos_stateDef,
  dominos_inportData,
  { stTrough1, stTrough2, stTrough3, stDrain, stDrain, stDrain, stDrain },
  NULL,                 /* no init */
  dominos_handleBallState,
  NULL,                 /* no static drawing */
  TRUE,                 /* manual plunger (Space) next to the autolauncher */
  NULL,                 /* no custom key conditions */
  TRUE                  /* the keys move a ball on the playfield: no Up/Down in multiball */
};

static core_tLCDLayout dominos_disp[] = {
  {0, 0, PINHECK_VIDEO_H, PINHECK_VIDEO_W, CORE_VIDEO, (genf *)pinheck_video, NULL}, {0}
};

static core_tGameData dominosGameData = {
  GEN_PINHECK, dominos_disp,
  { FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, 6, 0,
    pinheck_getsol, dominos_handleMech, dominos_getMech },
  &dominosSimData
};

static void init_dominos(void) {
  int i;
  core_gameData = &dominosGameData;
  memset(&locals, 0, sizeof(locals));
  for (i = 0; i <= 24; i++) locals.since[i] = 10000;
  locals.home = -1;
}

PINHECK_ROMSTART(dominos, "DOM_V006.PRG", 0x31990, CRC(750e27a4) SHA1(3fbebce7f885563e61dd2e4f5d7f0a8a54c53b23),
                 "PRP_V008.BIN", CRC(a51ee28d) SHA1(0556d88b6f0c7cb15e648e1f76f4d47890ddb858))
PINHECK_ROMEND
CORE_CLONEDEFNV(dominos, pinheck, "Domino's Spectacular Pinball Adventure", 2016, "Spooky Pinball", gl_mPINHECK, GAME_NOT_WORKING)
```

Then: `python3 tests/pinheck/link/register_build.py`
Expected: 19 `updated …` lines (`cmake/*` ×10, `src/pinmame.mak`, `vcproj/*` ×8); a second run prints nothing.

- [ ] **Step 4: Rebuild**

The build reads the root `CMakeLists.txt`, a copy that predates the registration, so copy it again first:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m8b.log 2>&1; tail -1 build/sdl3pinmame/m8b.log
grep -i warning build/sdl3pinmame/m8b.log | grep -cE 'dominos\.c|sim\.c|pinheck\.c|pinheckgames\.c'
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 5: The unit suites pass**

Run: `timeout 300 tests/pinheck/board/check.sh 2>&1 | tail -1; timeout 900 tests/pinheck/pic32mx/check.sh 2>&1 | tail -4`
Expected:
```text
board: 0 failed
makefile link: ok
symbols: ok
vcxproj: ok
pic32mx: 0 failed
```

- [ ] **Step 6: The simulator check passes**

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/sim/pinmame_sim.sh` (about 8 minutes)
Expected:
```text
sim: 16 checks, 0 failures
pinmame sim: ok
```

- [ ] **Step 7: Check 5 meets the simulator**

The simulator holds three balls from the first frame and turns PinMAME's column/row keys off. Run Plan 8a's check 5 unchanged:

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/board/pinmame_board.sh > build/c5.txt 2>&1; grep -v 'switch test after' build/c5.txt | cut -c1-100; grep -c 'switch test after' build/c5.txt` (about 25 minutes)
Expected: every switch-test step fails (the simulator holds balls in the shooter lane and the trough, and its keys, not PinMAME's column/row keys, own `Q`–`I` and `A`–`K`):
```text
timing: 74937 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.580 0.722 0.831 0.949 (median, 0-1)
BOARD FAIL: switch test showed unexpected states [((0, 1), (1,)), ((0, 1), (1, 6)), ((0, 1, 2), ()),
check5: 106 uart commands, 266 service-test steps, 145 failures
144
```

- [ ] **Step 8: Check 5 with the simulator's balls**

Check 5 turns the simulator keys off with `Del` before the switch test, reads the resting switches from the switch test's first screen (the shooter lane and trough 1: the check's own `[M17020]` loaded a ball, the solenoid test's `PLUNGER` launched it and its `LOAD BALL` loaded the next; Noid Home may be closed) and expects each key-toggled switch flipped against them:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/board/check5.py', [
    (r'''    for i in range(5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
    tap('0', 1.5)                                   # SWITCH TEST
    for n in range(64):
''',
     r'''    for i in range(5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
    tap('DEL', 0.5)                                 # simulator keys off: column/row keys reach the matrix
    tap('0', 1.5)                                   # SWITCH TEST
    for n in range(64):
'''),
    (r'''    ev, _ = key_plan()
    wanted = set()
    for i, (t, k, hold, e) in enumerate(ev):
        if not e:
''',
     r'''    ev, _ = key_plan()
    wanted = set()
    # the simulator's balls: [M17020] loaded one into the shooter lane, the solenoid test's PLUNGER launched it
    # and its LOAD BALL loaded the next, so the shooter lane (0) and trough 1 (1) rest closed; Noid Home (39) may
    t0 = min(t for t, k, hold, e in ev if e and e[0] == 'switch')
    rest = [g for g in (grid(f) for tt, f in frames if t0 - 1.0 <= tt < t0 - 0.1) if g]
    base = set(rest[-1][0]) if rest else set()
    check(rest and base - {39} == {0, 1} and rest[-1][1] == (1,), 'switch test at rest shows %s, expected the shooter lane (0), trough 1 (1), maybe Noid Home (39), and the closed coin door' % (rest[-1:],))
    for i, (t, k, hold, e) in enumerate(ev):
        if not e:
'''),
    (r'''            got = [grid(f) for tt, f in frames if t + 0.12 <= tt < t1 + 0.2]
            got = [g for g in got if g]
            check((e[1], e[2]) in got, 'switch test after %s: screens %s, expected %s' % (k, sorted(set(got)), (e[1], e[2])))
            wanted.add((e[1], e[2]))
        elif e[0] == 'exit':
            after = [grid(f) for tt, f in frames if tt >= t + 0.1]
''',
     r'''            got = [grid(f) for tt, f in frames if t + 0.12 <= tt < t1 + 0.2]
            got = [g for g in got if g]
            want = (tuple(sorted(base ^ set(e[1]))), e[2])
            check(want in got, 'switch test after %s: screens %s, expected %s' % (k, sorted(set(got)), want))
            wanted.add(want)
        elif e[0] == 'exit':
            after = [grid(f) for tt, f in frames if tt >= t + 0.1]
'''),
])
EOF
```

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/board/pinmame_board.sh` (about 25 minutes)
Expected:
```text
timing: 74937 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.580 0.722 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 266 service-test steps, 0 failures
pinmame board: ok
```

- [ ] **Step 9: Nothing else regressed**

Run, each with `timeout 3300` and `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame`: `tests/pinheck/storage/check.sh`, `tests/pinheck/display/check.sh`, `tests/pinheck/audio/check.sh`, `tests/pinheck/link/check.sh`, `tests/pinheck/pic32mx/pinmame_check.sh`, `tests/pinheck/display/pinmame_display.sh`, `tests/pinheck/audio/pinmame_audio.sh`.
Expected: `storage: 0 failed`, `display: 0 failed`, `audio: 0 failed`, `link: 0 failed`; `romset: ok` then `pinmame: ok`; `pinmame display: ok` (its screen snapshot shows the simulator's ball list, which `render.py` now leaves out); `pinmame audio: ok`.

- [ ] **Step 10: Commit**

```bash
git add src/wpc/sims/pinheck/dominos.c src/wpc/sim.h src/wpc/sim.c src/wpc/pinheck.c src/wpc/pinheck.h src/wpc/pinheckgames.c tests/pinheck/sim tests/pinheck/board/check5.py tests/pinheck/display/render.py tests/pinheck/link/register_build.py tests/pinheck/link/symbol_check.py src/pinmame.mak cmake vcproj
git commit -m "pinheck: Domino's playfield simulator, simulator check, check 5 with the simulator's balls"
```

### Task 2: Scripted game

**Files:**
- Create: `tests/pinheck/game/link.py`, `tests/pinheck/game/test_link.py`, `tests/pinheck/game/game.py`, `tests/pinheck/game/pinmame_game.sh`, `tests/pinheck/game/.gitignore`
- Modify: `src/wpc/pinheck.c`

**Interfaces:**
- Consumes: Task 1's simulator and keys, `W` lines; Plan 8a's `PINHECK_OUT_LOG` (`S` coil words, `P` levels), `PINHECK_UART1_SEND[_AT]`, `PINHECK_INSERVICE`; Plan 6's `PINHECK_FRAME_LOG` (20-byte stamp — Propeller time, PIC32 cycle, hub address, little-endian — and the 4,096-byte RGB332 frame).
- Produces: `PINHECK_LINK_LOG` lines `<seconds> <16 hex bytes> gap <cycles>` (PIC32 time of the packet's last clock; byte 15 = command; `gap` = longest pause between two of its clocks, in PIC32 cycles); `PINHECK_RTC`; `link.py`: `packets(path)` → `[(seconds, command, payload bytes 0–14)]`, `scores(pk)` → `[(seconds, player, score)]`, `status(pk)` → `[(seconds, players, ball, player)]` (ball 0 when no game runs), `videos(pk)` → `[(seconds, clip)]`, `hiscores(pk)` → `[(seconds, rank, score, initials)]`, `ticker(pk)` → `[(seconds, message)]`; `game.py plan|verify`, `pinmame_game.sh` — the scripted game, Milestone 9's workload and determinism reference.

The link packets are the firmware's own record of the game (Ruling 11). `link.py` decodes the five kinds this plan relies on; its unit test writes packets exactly as `PINHECK_LINK_LOG` does.

- [ ] **Step 1: Write the decoder's unit test**

`tests/pinheck/game/test_link.py`:

```python
#!/usr/bin/env python3
"""Unit test of link.py's packet decoding on packets written the way PINHECK_LINK_LOG writes them."""
import os
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the tests
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import link  # noqa: E402

fails = []


def check(ok, what):
    if not ok:
        fails.append(what)
        print('LINK FAIL: %s' % what)


def pkt(t, cmd, payload):
    b = bytes(payload) + bytes(15 - len(payload)) + bytes([cmd])
    return '%.6f %s gap 300\n' % (t, ' '.join('%02x' % x for x in b))


lines = [pkt(1.0, 0x0C, [1, 2, 1, 0x81]), pkt(1.5, 0x03, [1, 0x40, 0x42, 0x0F, 0x00]), pkt(2.0, 0x02, b'NBH'),
         pkt(2.1, 0x02, [0, 0, 0]), pkt(3.0, 0x0E, [3] + [0x20, 0xBC, 0xBE, 0x00] + list(b'BJH'))]
msg = 'PLAYER:1 BALL:2 '
for k in range(24):
    w = (msg * 3)[k:k + 8]
    lines.append(pkt(4.0 + 0.25 * k, 0x0F, [6] + list(w.encode())))
lines.append(pkt(11.0, 0x0F, [6] + list(b'FREE PLA')))
lines.append('garbage line\n')
fd, path = tempfile.mkstemp()
with os.fdopen(fd, 'w') as f:
    f.write(''.join(lines))
pk = link.packets(path)
os.remove(path)
check(len(pk) == 30, 'packets: %d, expected 30 (a short line is skipped)' % len(pk))
check(link.status(pk) == [(1.0, 1, 2, 1)], 'status %s' % link.status(pk))
check(link.scores(pk) == [(1.5, 1, 1000000)], 'scores %s' % link.scores(pk))
check(link.videos(pk) == [(2.0, 'NBH')], 'videos %s (a clip stop has no name)' % link.videos(pk))
check(link.hiscores(pk) == [(3.0, 3, 12500000, 'BJH')], 'high scores %s' % link.hiscores(pk))
check(link.ticker(pk) == [(4.0, (msg * 4)[:31]), (11.0, 'FREE PLA')], 'ticker %s' % link.ticker(pk))
check(link.said(pk, r'PLAYER:\d BALL:\d') == [(5.75, 'PLAYER:1 BALL:2'), (9.75, 'PLAYER:1 BALL:2')],
      'said %s' % link.said(pk, r'PLAYER:\d BALL:\d'))
print('link: ok' if not fails else 'link: %d failures' % len(fails))
sys.exit(1 if fails else 0)
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python3 tests/pinheck/game/test_link.py 2>&1 | tail -1`
Expected: `ModuleNotFoundError: No module named 'link'`

- [ ] **Step 3: Write the decoder**

`tests/pinheck/game/link.py`:

```python
#!/usr/bin/env python3
"""PIC32-to-Propeller link packets as logged by PINHECK_LINK_LOG: '<seconds> <16 hex bytes> gap <cycles>',
byte 15 the command.
  link.py LOG   print the decoded game-flow packets"""
import re
import sys

SFX, VIDEO, SCORE, STATUS, HISCORE, TICKER = 0x01, 0x02, 0x03, 0x0C, 0x0E, 0x0F


def packets(path):
    """(seconds, command, payload bytes 0-14) for every logged packet"""
    out = []
    for line in open(path):
        f = line.split()
        if len(f) >= 17:
            b = bytes(int(x, 16) for x in f[1:17])
            out.append((float(f[0]), b[15], b[:15]))
    return out


def u32(b):
    return b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24


def text(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)


def scores(pk):
    """(seconds, player, score) of every score update"""
    return [(t, b[0], u32(b[1:5])) for t, c, b in pk if c == SCORE]


def status(pk):
    """(seconds, players, ball, player) of every status packet"""
    return [(t, b[0], b[1], b[2]) for t, c, b in pk if c == STATUS]


def videos(pk):
    """(seconds, clip name) of every clip started; the clip is DMD/_D<first letter>/<name>.VID"""
    return [(t, b[:3].decode('latin-1')) for t, c, b in pk if c == VIDEO and b[:3].isalnum()]


def hiscores(pk):
    """(seconds, rank, score, initials) of every high-score table entry sent"""
    return [(t, b[0], u32(b[1:5]), text(b[5:8])) for t, c, b in pk if c == HISCORE]


def _runs(pk):
    """each run of scrolling text as [(seconds, character)]: a run goes on while each 8-character window
    is the previous one moved on by one character"""
    runs, prev = [], None
    for t, c, b in pk:
        if c != TICKER:
            continue
        w = text(b[1:9]).rstrip('.')
        if prev is not None and len(w) == len(prev) and w[:-1] == prev[1:]:
            runs[-1].append((t, w[-1]))
        elif w != prev:
            runs.append([(t, ch) for ch in w])
        prev = w
    return runs


def ticker(pk):
    """(seconds, text) for each run of scrolling text, the text as far as it scrolled"""
    return [(r[0][0], ''.join(ch for _, ch in r)) for r in _runs(pk)]


def said(pk, pattern):
    """(seconds, match) for each match of the regular expression in the scrolling text, at the time its last
    character scrolled in"""
    out = []
    for r in _runs(pk):
        s = ''.join(ch for _, ch in r)
        out += [(r[m.end() - 1][0], m.group(0)) for m in re.finditer(pattern, s)]
    return out


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pk = packets(sys.argv[1])
    for t, n, b, p in status(pk):
        print('%9.3f status players %d ball %d player %d' % (t, n, b, p))
    for t, p, s in scores(pk):
        print('%9.3f score player %d %d' % (t, p, s))
    for t, v in videos(pk):
        print('%9.3f video %s' % (t, v))
    for t, r, s, i in hiscores(pk):
        print('%9.3f high score %d %d %s' % (t, r, s, i))
    for t, m in ticker(pk):
        print('%9.3f ticker %s' % (t, m[:60]))
```

- [ ] **Step 4: Run it and confirm it passes**

Run: `python3 tests/pinheck/game/test_link.py`
Expected: `link: ok`

- [ ] **Step 5: Write the scripted game**

The game runs in the in-service machine, as check 5: a first boot on blank NVRAM, then the game, both with the clock at `PINHECK_RTC` = 2026-09-29 12:00:00 so that the firmware's random choices repeat (Ruling 12). After `[E97000]` (ball search off) the script inserts a coin, presses start and plays three balls: each is plunged, shot through 42 keys (skill shot at the left scoop, the target bank, both orbits and Noid orbits, both ramps, both scoops, spinner, lanes, pops, slings, inlanes, the flippers), drained through an outlane or between the flippers, and followed by 15 `Q` taps a second apart so that every multiball ball drains too; the next ball is plunged 25 s after the drain. Ball 2's right scoop starts a multiball. After the last drain it enters the initials `ACE` (start takes a letter, the right flipper steps to the next; a letter starts from the one before) and lets the match and the high-score table run.

The check reads the firmware's own record: the link packets (status, scrolling text, scores, clips, high-score table), UART1 (mode prints, the high scores sent after the game, the date from `PINHECK_RTC`), the board log (switches the simulator closed, coil rises, the start lamp) and the frame log (clips shown pixel-exact against the update's `.VID` files).

`tests/pinheck/game/game.py`:

```python
#!/usr/bin/env python3
"""Scripted game: coin, start, three balls played on the playfield simulator (a multiball among them),
every ball drained, high-score entry and match, checked against the firmware's own output.
  game.py plan DIR          write DIR/keys.txt, send, frames
  game.py verify DIR DMD    check DIR/{link2.log,out2.log,uart2.log,frames2.bin}; DMD = the update's DMD folder"""
import hashlib
import os
import re
import struct
import sys
import time

sys.dont_write_bytecode = True  # no __pycache__ next to the tests
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import link  # noqa: E402

FPS = 60
RTC = 1790683200          # PINHECK_RTC: 2026-09-29 12:00:00, so every run plays the same game
COIN, START, PLUNGE = 12.0, 13.0, 18.0
# (keys, seconds to the next shot): a scoop holds the ball about 2.5 s, the magnet about 1.2 s
BALL = [('LCONTROL S', 3.0), ('Z', 1.2), ('X', 1.2), ('M', 1.2), ('Z', 1.2), ('LCONTROL N', 1.5), ('RCONTROL N', 1.5),
        ('RCONTROL R', 1.5), ('C', 1.2), ('V', 2.0), ('RCONTROL S', 3.0), ('G', 1.5), ('LCONTROL L', 1.2), ('RCONTROL L', 1.2),
        ('B', 1.2), ('LCONTROL B', 1.2), ('RCONTROL B', 1.2), ('LCONTROL MINUS', 1.2), ('RCONTROL MINUS', 1.2),
        ('LCONTROL I', 1.2), ('RCONTROL I', 1.2), ('C', 1.2), ('C', 1.2), ('V', 2.0), ('V', 2.0), ('LCONTROL S', 3.0),
        ('LCONTROL R', 1.5), ('LCONTROL N', 1.5), ('Z', 1.2), ('X', 1.2), ('M', 1.2), ('RCONTROL S', 3.0), ('LCONTROL S', 3.0),
        ('LCONTROL R', 1.5), ('RCONTROL R', 1.5), ('LCONTROL S', 3.0), ('V', 2.0), ('LCONTROL R', 1.5), ('RCONTROL R', 1.5),
        ('LCONTROL S', 3.0), ('LSHIFT', 1.0), ('RSHIFT', 1.0)]
DRAIN = ['LCONTROL O', 'RCONTROL O', 'Q']      # left outlane, right outlane, between the flippers
# the switch each key's shot closes first (Task 1's key table)
SWITCH = {'LCONTROL S': 25, 'RCONTROL S': 48, 'Z': 28, 'X': 27, 'M': 26, 'LCONTROL N': 38, 'RCONTROL N': 37, 'LCONTROL R': 46,
          'RCONTROL R': 36, 'C': 95, 'V': 96, 'G': 47, 'LCONTROL L': 41, 'RCONTROL L': 42, 'B': 44, 'LCONTROL B': 45,
          'RCONTROL B': 43, 'LCONTROL MINUS': 22, 'RCONTROL MINUS': 16, 'LCONTROL I': 23, 'RCONTROL I': 17,
          'LCONTROL O': 24, 'RCONTROL O': 18, 'LSHIFT': 4, 'RSHIFT': 3}
SWEEP, NEXT = 15, 25                           # Q every second after the drain, next plunge after the drain
ENTRY, END = 16, 12                            # name entry keys after the last drain; the end after the last key
INITIALS = 'ACE'


def script():
    """(seconds, keys, hold frames) of the whole game, and the drain time of each ball"""
    ev, drains, p = [(COIN, '5', 6), (START, '1', 6)], [], PLUNGE
    for b in range(3):
        ev.append((p, 'SPACE', 40))
        t = p + 3
        for k, dt in BALL:
            ev.append((t, k, 6 if 'SHIFT' in k else 1))
            t += dt
        ev.append((t, DRAIN[b], 1))
        ev += [(t + 1 + i, 'Q', 1) for i in range(SWEEP)]
        drains.append(t)
        p = t + NEXT
    return ev, drains


def entry_and_end(last_drain):
    """name entry (right flipper: next letter, start: take it; a letter starts from the one before) and the end"""
    ev, t = [], last_drain + ENTRY
    for k, n in (('1', 0), ('RSHIFT', 2), ('1', 0), ('RSHIFT', 2), ('1', 0)):   # A, C, E
        for _ in range(max(n, 1)):
            ev.append((t, k, 6))
            t += 0.5
    return ev, t + END


def plan(d):
    ev, drains = script()
    more, end = entry_and_end(drains[-1])
    keys = ''.join('%d tap %d %s\n' % (round(t * FPS), h, ' '.join('KEYCODE_' + x for x in k.split())) for t, k, h in ev + more)
    for name, text in (('keys.txt', keys), ('send', '[E97000]'), ('rtc', '%d' % RTC), ('frames', '%d' % round(end * FPS))):
        open('%s/%s' % (d, name), 'w').write(text + ('\n' if name != 'keys.txt' else ''))


def board(path):
    """(switch changes, coil rises, levels) from PINHECK_OUT_LOG: W, S and P lines"""
    sw, rises, lv, prev = [], [], {}, 0
    for line in open(path):
        f = line.split()
        if len(f) < 3:
            continue
        t = float(f[0])
        if f[1] == 'W':
            sw += [(t, int(a), int(b)) for a, b in (x.split('=') for x in f[2:])]
        elif f[1] == 'S':
            w = int(f[2], 16)
            if w & ~prev:
                rises.append((t, w & ~prev))
            prev = w
        elif f[1] == 'P':
            for x in f[2:]:
                k, v = x.split('=')
                lv.setdefault(k, []).append((t, int(v)))
    return sw, rises, lv


def frames(path):
    raw, rec = open(path, 'rb').read(), 20 + 4096
    return [(struct.unpack_from('<Q', raw, i + 8)[0] / 80e6, hashlib.md5(raw[i + 20:i + rec]).digest())
            for i in range(0, len(raw) - rec + 1, rec)]


def clip_shown(dmd, name, t, shown):
    """frames of DMD/_D<x>/<name>.VID shown pixel-exact within 3 s after the clip started"""
    path = '%s/_D%s/%s.VID' % (dmd, name[0], name)
    if not os.path.exists(path):
        return 0
    v = open(path, 'rb').read()
    want = set(hashlib.md5(v[512 + 4096 * k:512 + 4096 * (k + 1)]).digest() for k in range((len(v) - 512) // 4096))
    return len(set(h for tt, h in shown if t <= tt < t + 3.0 and h in want))


def verify(d, dmd):
    pk = link.packets(d + '/link2.log')
    sw, rises, lv = board(d + '/out2.log')
    uart = open(d + '/uart2.log', 'rb').read().decode('latin-1').replace('\r', '')
    ev, drains = script()
    plunges = [t for t, k, h in ev if k == 'SPACE']
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

    def closed(n, t0, t1):
        return [t for t, s, v in sw if s == n and v and t0 <= t < t1]

    def fired(c, t0, t1):
        return [t for t, w in rises if t0 <= t < t1 and w >> (c - 1) & 1]

    def ejects(t0, t1):   # the trough coil pulses twice per ball, 95 ms apart
        ts = fired(18, t0, t1)
        return [t for i, t in enumerate(ts) if i == 0 or t - ts[i - 1] > 0.5]

    sc = link.scores(pk)
    st = link.status(pk)
    end = drains[-1] + 60
    # coin, start, attract
    check(closed(7, COIN, COIN + 0.3), 'the coin key closed no coin switch (7)')
    starts = [x for x in st if x[0] > START and x[2] == 1]
    check(starts and starts[0][0] < START + 1.5, 'no status with ball 1 within 1.5 s of the start key: %s' % st[:4])
    check([p for t, p, v in sc if COIN < t < START + 1.5 and v == 0] == [1, 2, 3, 4], 'the four scores were not zeroed for the game')
    blinks = [t for t, v in lv.get('L91', []) if v >= 128]
    check(len([t for t in blinks if 5 < t < COIN]) >= 3 and not [t for t in blinks if START + 1 < t < drains[-1]],
          'the start button lamp (91) must blink in attract mode and stay dark in the game')
    # every ball: served from the trough, plunged, drained with all others
    served = ejects(START, end)
    for b in range(3):
        t0 = START if b == 0 else drains[b - 1]
        msg = [t for t, m in link.said(pk, r'PLAYER:1 BALL:%d' % (b + 1)) if t0 < t < plunges[b] + 5]
        check([x for x in st if t0 < x[0] < plunges[b] and x[2] == b + 1], 'ball %d: no status packet before its plunge' % (b + 1))
        check(msg, "ball %d: the scrolling text never showed 'PLAYER:1 BALL:%d'" % (b + 1, b + 1))
        e = [t for t in served if t0 < t < plunges[b]]
        check(len(e) == 1 and closed(11, e[0], e[0] + 1.0), 'ball %d: trough ejects %s before the plunge, shooter lane %s' % (b + 1, e, closed(11, t0, plunges[b])))
        gone = [t for t, s, v in sw if s == 11 and not v and plunges[b] < t < plunges[b] + 1.5]
        check(gone, 'ball %d: the plunger did not clear the shooter lane' % (b + 1))
        home = e[0] - 0.05 if e else drains[b]
        check([state(n, home) for n in (12, 13, 14)] == [1, 1, 1], 'ball %d: the trough was not full when it was served' % (b + 1))
    check([state(n, drains[-1] + SWEEP + 1) for n in (11, 12, 13, 14)] == [0, 1, 1, 1], 'the last ball: not every ball back in the trough')
    # the skill shot: the left scoop straight after the plunge is worth a million, then the scoop kicks the ball out
    ls = plunges[0] + 3
    jump = [v2 - v1 for (t1, p1, v1), (t2, p2, v2) in zip(sc, sc[1:]) if ls < t2 < ls + 1.0]
    kick = fired(9, ls, ls + 5)
    check(closed(25, ls - 0.1, ls + 0.2) and jump == [1000000] and kick, 'skill shot: left scoop %s, score steps %s, kick %s' % (closed(25, ls - 0.1, ls + 0.2), jump, kick[:1]))
    # multiball: more balls served during a ball than balls played
    extra = [t for t in served if not any(t0 < t < p for t0, p in zip([START] + drains, plunges))]
    launched = fired(17, START, end)
    check(extra and launched, 'no multiball: extra trough ejects %s, autolauncher %s' % (extra, launched))
    missing = sorted(set(k for t, k, h in ev if k in SWITCH and not closed(SWITCH[k], START, end)))
    check(not missing, 'the simulator never closed the switch of %s' % missing)
    # coils the shots and flippers fire
    for c, what in ((3, 'lower pop'), (4, 'left pop'), (5, 'right pop'), (6, 'magnet'), (9, 'left scoop'), (13, 'right scoop'),
                    (11, 'left sling'), (20, 'right sling'), (10, 'left flipper'), (19, 'right flipper')):
        check(fired(c, START, end), 'the %s (solenoid %d) never fired' % (what, c))
    # score, high-score entry, match, back to attract
    vals = [v for t, p, v in sc if p == 1 and t > START]
    check(all(b >= a for a, b in zip(vals, vals[1:])), 'player 1 score went down')
    final = vals[-1] if vals else 0
    over = [x for x in st if x[0] > drains[-1] and x[2] == 0]
    check(over, 'no status packet with ball 0 after the last drain')
    hs = [x for x in link.hiscores(pk) if x[0] > drains[-1]]
    rank = [r for t, r, v, i in hs if i == INITIALS and v == final]
    check(len(hs) == 5 and rank, 'high-score table after the game %s, expected %s with %d' % ([x[1:] for x in hs], INITIALS, final))
    clips = [(t, v) for t, v in link.videos(pk) if START < t < end]
    entry = [v for t, v in clips if t > drains[-1] and v.startswith('NE')]
    match = [(t, v) for t, v in clips if t > drains[-1] and v.startswith('NA')]
    check(entry[:1] == ['NEA'] and match, 'name entry clips %s, match clips %s' % (entry, match))
    check(len(re.findall(r'Send High Scores', uart.split('Ball Search: DISABLED', 1)[-1])) == 5,
          'UART1: the high scores were not sent to the display five times after the game')
    for line in ('pizzaDispatchModeStart[player] 0', 'COLLECTING JP??', 'MODES ARE ENDING'):
        check(line in uart, 'UART1: the game never printed %r' % line)
    check('%s ' % time.strftime('%Y/%-m/%-d', time.gmtime(RTC)) in uart, 'UART1: the clock did not start at PINHECK_RTC')
    # the display: clips the firmware starts are shown pixel-exact
    shown = frames(d + '/frames2.bin')
    exact = [(t, v) for t, v in clips if clip_shown(dmd, v, t, shown)]
    check([v for t, v in exact if v == 'BLK'], 'the skill shot clip BLK was never shown pixel-exact')
    check(len(exact) * 2 > len(clips), 'only %d of %d clips shown pixel-exact' % (len(exact), len(clips)))
    for f in fails:
        print('GAME FAIL: ' + f)
    print('game: 3 balls, %d trough ejects, %d autolaunches, final score %d' % (len(served), len(launched), final))
    print('game: high score %s %s, match %s' % (rank[0] + 1 if rank else '-', INITIALS, ' '.join(v for t, v in match)))
    print('game: %d clips started, %d shown pixel-exact' % (len(clips), len(exact)))
    print('game: %d checks, %d failures' % (checks[0], len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) < 3 or sys.argv[1] not in ('plan', 'verify') or (sys.argv[1] == 'verify') != (len(sys.argv) == 4):
        sys.exit(__doc__)
    sys.exit(plan(sys.argv[2]) if sys.argv[1] == 'plan' else verify(sys.argv[2], sys.argv[3]))
```

`tests/pinheck/game/pinmame_game.sh`:

```sh
#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update (for the .VID clips)}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
python3 game.py plan $B || exit 2
launch() {
	(cd $B && PINHECK_RTC=$(cat rtc) PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out$1.log PINHECK_UART1_LOG=$PWD/uart$1.log PINHECK_LINK_LOG=$PWD/link$1.log \
		PINHECK_FRAME_LOG=$PWD/frames$1.bin timeout -k 30 5400 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -cfg_directory cfg -headless \
		-frames_to_run $2 -skip_gamewarnings -nothrottle $3 > run$1.out 2>&1) \
		|| { echo "GAME FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
launch 1 1200
[ -s $B/link1.log ] || { echo "GAME FAIL: launch 1 logged no link packets (PINHECK_LINK_LOG)"; exit 1; }
# the sync check packet, command $23: bytes 1-14 count up from $30
grep -q ' 30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 23 gap' $B/link1.log || { echo "GAME FAIL: launch 1 logged no sync check packet"; exit 1; }
PINHECK_UART1_SEND_AT=10 PINHECK_UART1_SEND="$(cat $B/send)" launch 2 "$(cat $B/frames)" "-key_script keys.txt"
python3 game.py verify $B "$PINHECK_UPDATE_DIR/DMD" || exit 1
echo "pinmame game: ok"
```

`tests/pinheck/game/.gitignore`:

```text
build/
__pycache__/
```

`chmod +x tests/pinheck/game/game.py tests/pinheck/game/pinmame_game.sh tests/pinheck/game/link.py tests/pinheck/game/test_link.py`

- [ ] **Step 6: Confirm it fails before the link log**

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 6000 tests/pinheck/game/pinmame_game.sh`
Expected, after the first launch (about 3 minutes; exit 1):
```text
GAME FAIL: launch 1 logged no link packets (PINHECK_LINK_LOG)
```

- [ ] **Step 7: The link log and the clock hook**

`PINHECK_LINK_LOG` samples RF5 on every rising edge of RF12 in the port-write callback, 16 bytes least significant bit first, and writes a line per packet; `PINHECK_RTC` replaces the host time in the DS1340's seed at every reset.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
    (r'''}

static void pinheck_port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	uint32_t drv = lat & ~tris;
	(void)ctx;
	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
''',
     r'''}

/* test log (PINHECK_LINK_LOG): every PIC32-to-Propeller packet, 16 bytes shifted LSB first, data sampled on the rising clock */
static FILE *link_log;
static struct { int clk, nbits; uint64_t last, gap; uint8_t b[16]; } lnk;

static void pinheck_link_bit(uint32_t drv, uint64_t cycle)
{
	int clk = (drv & RF12) != 0, i;
	if (clk && !lnk.clk) {
		if (lnk.nbits && cycle - lnk.last > lnk.gap) lnk.gap = cycle - lnk.last;
		if (drv & RF5) lnk.b[lnk.nbits >> 3] |= (uint8_t)(1u << (lnk.nbits & 7));
		else lnk.b[lnk.nbits >> 3] &= (uint8_t)~(1u << (lnk.nbits & 7));
		lnk.last = cycle;
		if (++lnk.nbits == 128) {
			fprintf(link_log, "%.6f", cycle / (double)PINHECK_CLOCK);
			for (i = 0; i < 16; i++) fprintf(link_log, " %02x", lnk.b[i]);
			fprintf(link_log, " gap %llu\n", (unsigned long long)lnk.gap);
			lnk.nbits = 0;
			lnk.gap = 0;
		}
	}
	lnk.clk = clk;
}

static void pinheck_port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	uint32_t drv = lat & ~tris;
	(void)ctx;
	if (port == PIC32MX_PORTF && link_log) pinheck_link_bit(drv, cycle);
	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
'''),
    (r'''	pinheck_disp_init();
	pinheck_brd_init();
	pic32cpu_set_board(&board);
}
''',
     r'''	pinheck_disp_init();
	pinheck_brd_init();
	if (!link_log && getenv("PINHECK_LINK_LOG")) link_log = fopen(getenv("PINHECK_LINK_LOG"), "w");
	pic32cpu_set_board(&board);
}
'''),
    (r'''	pinheck_disp_reset();
	cat24m01_init(&u13, u13mem, 0);
	ds1340_init(&rtc, pinheck_local_now(), PINHECK_CLOCK);
	locals.rtc_at = 0;
	locals.send = getenv("PINHECK_UART1_SEND");
''',
     r'''	pinheck_disp_reset();
	cat24m01_init(&u13, u13mem, 0);
	/* test hook: PINHECK_RTC (seconds since 1970, local time) starts the clock there, so a run is repeatable */
	ds1340_init(&rtc, getenv("PINHECK_RTC") ? strtoll(getenv("PINHECK_RTC"), NULL, 10) : pinheck_local_now(), PINHECK_CLOCK);
	locals.rtc_at = 0;
	locals.send = getenv("PINHECK_UART1_SEND");
'''),
])
EOF
```

- [ ] **Step 8: Rebuild**

Run: `timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m8b.log 2>&1; tail -1 build/sdl3pinmame/m8b.log; grep -i warning build/sdl3pinmame/m8b.log | grep -c 'pinheck\.c'`
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 9: The scripted game passes**

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 6000 tests/pinheck/game/pinmame_game.sh` (about 50 minutes: 315 s of game at about 0.1× real time)
Expected:
```text
game: 3 balls, 4 trough ejects, 1 autolaunches, final score 32176700
game: high score 1 ACE, match NA1
game: 148 clips started, 87 shown pixel-exact
game: 44 checks, 0 failures
pinmame game: ok
```

- [ ] **Step 10: The same game again**

Keep the first run's logs and play the game once more: every log must be byte-identical.

```sh
rm -rf build/game1 && cp -r tests/pinheck/game/build/pinmame build/game1
SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 6000 tests/pinheck/game/pinmame_game.sh | tail -1
for f in link2.log out2.log uart2.log frames2.bin; do cmp build/game1/$f tests/pinheck/game/build/pinmame/$f && echo "$f same"; done
```
Expected:
```text
pinmame game: ok
link2.log same
out2.log same
uart2.log same
frames2.bin same
```

- [ ] **Step 11: Commit**

```bash
git add src/wpc/pinheck.c tests/pinheck/game
git commit -m "pinheck: link packet log, repeatable clock, scripted game"
```

### Task 3: Record the findings, prove the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` (§3.3, §3.4, §4), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` (the m8b row, notes for Plan 9)

- [ ] **Step 1: Update the spec and the roadmap**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md', [
    ('Playfield switches get keyboard keys. The cabinet keys follow PinMAME\'s usual layout: coin, start, both flippers, tilt, and Enter/Back/Menu/User.\n',
     'Playfield switches get keyboard keys. The cabinet keys follow PinMAME\'s usual layout: coin, start, both flippers, tilt, and Enter/Back/Menu/User.\n'
     '\n'
     'Established by Plan 8b:\n'
     '\n'
     '- **Numbers.** The coil numbers above are the documents\' 0-based ones: PinMAME solenoids 18 (trough), 17 (autolauncher), 9 and 13 (scoops), 7 (up-post) and 6 (magnet). Domino\'s game definition lives in the simulator file, as for PinMAME\'s other full simulators.\n'
     '- **Coils as edges.** The simulator reads a coil as on when it was on at any time during the last frame (`coreGlobals.solenoids`, built by the board block), so pulses of 3–20 ms move the ball exactly once.\n'
     '- **Mechanics.** The shooter lane has a manual plunger; the firmware autolaunches only ball saves and multiball balls. The optos are the Center Ramp (95) and the Oven Ramp (96), whose handler energises the magnet. The orbit handlers raise the up-post: a ball meeting it comes back down its orbit. The firmware homes the Noid (servo 0, continuous rotation) by Noid Home (58); the target bank (servo 1) is up at 646 µs and down at 1,621 µs, and its switches register in both positions.\n'
     '- **Framework.** `sim_tSimData` gained an opt-in `autoBall`: while the selected ball is off the playfield, the keys move a ball on it, which multiball needs.\n'),
    ('  - It stays in the suite as the regression test for every later change.\n',
     '  - It stays in the suite as the regression test for every later change.\n'
     '  - Established by Plan 8b: `DOM_V006` formats `PLAYER:%d BALL:%d …` into the text it sends to the Propeller and `[E11000]` prints only an empty line, so the game\'s own record is the PIC32-to-Propeller link: `PINHECK_LINK_LOG` logs every packet (score, status, scrolling text, clip, high-score table). UART1 carries the firmware\'s mode prints (`pizzaDispatchModeStart[player]`, `COLLECTING JP??`, `MODES ARE ENDING`) and the high scores sent after the game.\n'
     '  - `PINHECK_RTC` seeds the DS1340 instead of host time; the firmware\'s random choices follow the clock, and with it fixed two runs of the scripted game give byte-identical link, board, UART1 and frame logs. The game (`tests/pinheck/game/pinmame_game.sh`: three balls, a multiball, `ACE` at the top of the high-score table, the match) plays 315 s of emulated time, about 50 minutes at today\'s speed.\n'),
    ('  - a determinism check: the optimised build reproduces the reference build\'s UART1 log, display frames and audio byte for byte over a fixed firmware run.\n',
     '  - a determinism check: the optimised build reproduces the reference build\'s UART1 log, display frames and audio byte for byte over a fixed firmware run. The scripted game under `PINHECK_RTC` is that run: its link, board, UART1 and frame logs are already byte-identical between runs of one build.\n'),
])
edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    ('| m8b | 8b: playfield simulator, keyboard play, scripted headless game via `-key_script` | Plan 8a | none | the scripted game plays from coin-up to high-score entry | after Plan 8a |\n',
     '| `2026-09-29-pinheck-m8b-sim-game.md` | 8b: playfield simulator (`sims/pinheck/dominos.c`, framework `autoBall`), keyboard play, link packet log, repeatable clock, scripted headless game | Plan 8a | none | simulator check; the scripted game plays from coin-up through multiball to high-score entry and match, byte-identical between runs | written |\n'),
    ('## Carried into Plan 9\n',
     '## Carried into Plan 9\n'
     '\n'
     '- The scripted game (`tests/pinheck/game/pinmame_game.sh`) is the workload and the determinism reference: 315 s emulated (about 50 minutes at 0.1×), with `PINHECK_RTC` fixed its `link2.log`, `out2.log`, `uart2.log` and `frames2.bin` must stay byte-identical to the reference build\'s.\n'),
])
EOF
```

- [ ] **Step 2: Mutations**

Apply each mutation, run the named check, confirm the named failure line (other failure lines may follow), then restore the file with `git checkout <file>`. The `link.py` mutations take seconds (`python3 tests/pinheck/game/test_link.py`); each C mutation needs `timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)"` first, and a rebuild after restoring. The six simulator mutations run the simulator check (`SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/sim/pinmame_sim.sh`, about 8 minutes each); the link-log mutation runs the scripted game, which stops after its first launch (about 3 minutes).

| File | Replace → with | Check | Must show |
|---|---|---|---|
| `tests/pinheck/game/link.py` | in `u32`, `return b[0] \| b[1] << 8 \| b[2] << 16 \| b[3] << 24` → `return b[3] \| b[2] << 8 \| b[1] << 16 \| b[0] << 24` | `test_link.py` | `LINK FAIL: scores [(1.5, 1, 1078071040)]` |
| `tests/pinheck/game/link.py` | in `_runs`, `w[:-1] == prev[1:]` → `w[1:] == prev[:-1]` | `test_link.py` | `LINK FAIL: said []` |
| `tests/pinheck/game/link.py` | in `videos`, drop ` and b[:3].isalnum()` | `test_link.py` | `LINK FAIL: videos [(2.0, 'NBH'), (2.1, '\x00\x00\x00')] (a clip stop has no name)` |
| `tests/pinheck/game/link.py` | in `said`, `r[m.end() - 1][0]` → `r[0][0]` | `test_link.py` | `LINK FAIL: said [(4.0, 'PLAYER:1 BALL:2'), (4.0, 'PLAYER:1 BALL:2')]` |
| `src/wpc/sims/pinheck/dominos.c` | in `sol`, `coreGlobals.solenoids` → `coreGlobals.pulsedSolState` | simulator check | `SIM FAIL: trough: [M17002] at 12.0 (coil 18 [12.00774…]) did not move trough 1 (12) to the shooter lane (11)` … `sim: 16 checks, 11 failures` |
| `src/wpc/sims/pinheck/dominos.c` | `case stTrough1:   if (sol(sLoad))` → `case stTrough1:   if (sol(sLaunch))` | simulator check | `SIM FAIL: trough: [M17002] at 12.0 (coil 18 [12.00774…]) did not move trough 1 (12) to the shooter lane (11)` … `sim: 16 checks, 8 failures` |
| `src/wpc/sims/pinheck/dominos.c` | `locals.since[sPost] < ORBIT_POST ? stLOrbitBack` → `0 ? stLOrbitBack` | simulator check | `SIM FAIL: up-post: coil 7 at [19.5577…], left orbit closed [19.533333338], right orbit [19.7, 19.8]: the ball must come back down the left orbit` |
| `src/wpc/sims/pinheck/dominos.c` | `if (locals.since[sMagnet] > MAGNET_DROP) return` → `if (1) return` | simulator check | `SIM FAIL: magnet: the ball left the magnet while coil 6 held it (Delivery target 31 closed)` |
| `src/wpc/sims/pinheck/dominos.c` | the line `else if (us && us < 1450) locals.noid = …` → nothing | simulator check | `SIM FAIL: Noid: servo 0 pulses 200 times at 544 us, Noid Home (58) changed []` |
| `src/wpc/sims/pinheck/dominos.c` | `dominosSimData`'s last initialiser `TRUE` (`autoBall`) → `FALSE` | simulator check | `SIM FAIL: two balls: Q twice did not drain both balls on the playfield (11-14: [0, 1, 0, 0])` |
| `src/wpc/pinheck.c` | in `pinheck_link_bit`, both `(1u << (lnk.nbits & 7))` → `(0x80u >> (lnk.nbits & 7))` | scripted game (stops after its first launch) | `GAME FAIL: launch 1 logged no sync check packet` |

(The simulator check does not fix the clock, so the firmware's reply times vary by microseconds between runs; `…` marks those digits.)

Then `git status --short src tests` prints nothing and a final rebuild restores the tested binary.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "Spec: what the playfield simulator and the scripted game established; roadmap m8b"
```

## Self-Review

**Spec coverage** (addendum §3.3, the scripted-game part of §3.4; parent §4.5, §7):

| Requirement | Where |
|---|---|
| simulator on the existing framework, in `src/wpc/sims/pinheck/dominos.c` | Task 1 (`sim_tSimData` ball states, `handleBallState`, `handleMech`, the manual shooter; the game definition in the simulator file, Ruling 2) |
| trough and its switches, solenoid 17 loading the shooter lane | Task 1: trough states 12–14 → shooter lane 11 on PinMAME solenoid 18 (Ruling 1); simulator check `trough` steps (5 ms pulse), two-ball step; the scripted game's trough ejects and "trough full when served" |
| autolauncher (16) | Task 1: shooter lane leaves on PinMAME solenoid 17 or the plunger (Ruling 4); simulator check `autolauncher`; the game's multiball autolaunch |
| left and right scoops (8, 12) | Task 1: scoop states 25/48 leave on solenoids 9/13; simulator check `left scoop`; the game's skill shot and kicks, `fired(9)`/`fired(13)` |
| up-post (6) and magnet (5) | Task 1: orbits with the post (Ruling 6), oven ramp and magnet (Rulings 5, 7); simulator check `orbit`, `up-post`, `magnet` |
| Noid and target-bank servos | Task 1: `pinheck_servo()`, `handleMech` (Rulings 8, 9); simulator check `Noid` via the servo test; the game's Noid homing and bank |
| outlanes and drain back into the trough | Task 1: outlane and drain states; simulator check `drain`; the game's three drains and sweeps |
| playfield switches get keyboard keys | Task 1 key table (Global Constraints), `autoBall` (Ruling 10) |
| scripted game: coin, start, switch sequence, every ball drained, match and high-score entry | Task 2 `game.py` (coin switch 7, status and scores at the start, three balls, multiball, `ACE` in the table, match clip) |
| asserts on the firmware's own output | Task 2: link packets (Ruling 11: the V006 home of `PLAYER:%d BALL:%d`), UART1 mode prints and high scores, `PINHECK_RTC` date |
| … on decoded display frames | Task 2: clips shown pixel-exact against the `.VID` files (the skill shot `BLK`, the match clip, more than half of all clips) |
| … on lamp and solenoid activity | Task 2: start lamp 91 in attract and in the game; pops, slings, magnet, scoops, flippers fired |
| stays in the suite as the regression test | Task 2 `pinmame_game.sh`, repeatable with `PINHECK_RTC` (Step 10), Milestone 9's workload (Task 3 spec edit) |
| build registration, symbol and link checks | Task 1 `register_build.py` (`PINGAMES`, object directory), `symbol_check.py` covers `sims/pinheck`, `makefile_link_check.sh` stays green |

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay.

**Type consistency:** `pinheck_servo(int)` is declared in `pinheck.h` and defined in `pinheck.c`, used by `dominos.c`; `sim_tSimData.autoBall` is the last field in `sim.h`, set by `dominosSimData`'s last initialiser and read in `sim_run`; `link.py`'s `packets`/`scores`/`status`/`videos`/`hiscores`/`ticker`/`said` signatures match their uses in `game.py` and `test_link.py`; `W` lines are written by `pinheck_brd_log_outputs` and read by `sim.py` and `game.py` in the same `<time> W <n>=<v> …` form.

**Review Focus:** five items, each pinned by the named tests in Tasks 1–2.

**Proof:** every file and edit this plan writes was extracted from this document into a fresh worktree of `0fdf7bd2` and is byte-identical to the proven prototype; every Expected output above was reproduced there. The scripted game gave the same four logs (`link2.log`, `out2.log`, `uart2.log`, `frames2.bin`) byte for byte in three runs from two build trees (the prototype's and the replay's, the replay's twice, Step 10) and took 47–58 minutes of wall time for 315 s of game, depending on how many other emulators ran. Each mutation of Task 3 was caught with the named line.
