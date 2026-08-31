# Mephisto's coil numbering, and how to read it off the machine

**Date:** 2026-08-31
**Repo:** this fork, branch `sport2000-phase0`, from `bd3eb477`.
**Status:** **Resolved for all 24 coils, both ROM revisions, observed live.**
`ic9_pa_w`'s Mephisto gate is removed.

Every claim is tagged `[OBSERVED]` (measured on the running emulator, or read
byte-for-byte out of a ROM image / the scanned manual), `[STATIC]` (derived by
disassembling and simulating ROM code, not measured) or `[INFERRED]` (reasoned from
something observed, not itself measured).

---

## 1. Bottom line

**Mephisto numbers the positions in its coil frame the opposite way round from Sport
2000.** Same bus, same frame shape, same connectors, same physical pin order — but

```
  Sport 2000   coil N -> PA position  N % 8        , data bit 3 + N/8
  Mephisto     coil N -> PA position  7 - (N % 8)  , data bit 3 + N/8
```

A decode written for one game mislabels **six of every eight** coils on the other; only
`N % 8 == 3` and `N % 8 == 4` land on themselves. That is precisely the failure mode the
gate in `ic9_pa_w` existed to prevent, and it is why "the boards are identical, so the
decode carries over" was the wrong conclusion to reach from the schematic alone.

The numbering is the ROM's own, read directly off the machine: Mephisto's service menu
has a COIL TEST that drives one coil at a time and prints its number, and the number it
prints is the number this table uses.

| coil | group / PA data bit | PA position | PA byte observed | manual §5 name |
|---|---|---|---|---|
| 00 | 0 / PA3 (J11) | 7 | `8F` | — (`#1` is blank in the manual) |
| 01 | 0 / PA3 | 6 | `8E` | — |
| 02 | 0 / PA3 | 5 | `8D` | #2 Upper bumper |
| 03 | 0 / PA3 | 4 | `8C` | #3 Right bumper |
| 04 | 0 / PA3 | 3 | `CB` | #4 Lower bumper |
| 05 | 0 / PA3 | 2 | `8A` | — (`#5` blank) |
| 06 | 0 / PA3 | 1 | `C9` | — (`#6` blank) |
| 07 | 0 / PA3 | 0 | `C8` | — (`#7` blank) |
| 08 | 1 / PA4 (J12) | 7 | `97` | #8 Hole kickout |
| 09 | 1 / PA4 | 6 | `96` | #9 Hole kickout |
| 10 | 1 / PA4 | 5 | `95` | #10 Gate/shutter release |
| 11 | 1 / PA4 | 4 | `94` | #11 Ramp shutter |
| 12 | 1 / PA4 | 3 | `D3` | #12 Drop target, 3-bank |
| 13 | 1 / PA4 | 2 | `92` | #13 Drop target, single |
| 14 | 1 / PA4 | 1 | `D1` | #14 Ball-trough kickout |
| 15 | 1 / PA4 | 0 | `D0` | #15 Hole kickout |
| 16 | 2 / PA5 (J13) | 7 | `A7` | #16 Skull flasher |
| 17 | 2 / PA5 | 6 | `A6` | #17 Left-ramp flasher |
| 18 | 2 / PA5 | 5 | `A5` | #18 Eyes flasher |
| 19 | 2 / PA5 | 4 | `A4` | #19 Bat flasher |
| 20 | 2 / PA5 | 3 | `E3` | #20 Sun flasher |
| 21 | 2 / PA5 | 2 | `A2` | #21 Right-ramp flasher |
| 22 | 2 / PA5 | 1 | `E1` | #22 TACA (cabinet) |
| 23 | 2 / PA5 | 0 | `E0` | #23 GATE |

The **coil / group / position / PA byte** columns are `[OBSERVED]` — every one of the 24
rows was read off a live run, both for `mephisto` (rev 1.2) and `mephist1` (rev 1.1), with
the coil number taken from the machine's own display and the PA byte from a trace inside
`ic9_pa_w`. The **manual name** column is a separate claim with its own evidence and one
open conflict; see §5. Bit 6 rides along at positions 0 and 3 (it is the ROM's heartbeat /
`INH BOB` toggle, not coil data), which is why those bytes read `C.`/`D.`/`E.` rather than
`8.`/`9.`/`A.`.

`coreGlobals.solenoids` bit *N* is coil *N*, the same convention Sport 2000 already used.

---

## 2. Entering Mephisto's service menu

The controls are the same two MUART port-2 pins the driver already routes, but they do
**not** mean what their Sport 2000 names suggest. `[OBSERVED]` (ROM) + `[OBSERVED]` (live):

| driver name | MUART pin | `swMatrix[0]` bit | what it actually is on Mephisto |
|---|---|---|---|
| `TEST` | P22 | bit 0 (`val=1`) | the **automatic/manual switch** — a *level*. LOW = MANUAL, HIGH = AUTOMATIC |
| `AVANCE` | P23 | bit 1 (`val=2`) | the **programming switch** — an *edge* (rising) |
| `EG1` / `EG2` | P20 / P21 | bits 2, 3 | never read by this ROM |

The ROM's own dispatch, at `0x2EE7`: `[0xA8]` holds the debounced port-2 level and `[0xBE]`
its rising-edge flags (both produced by the edge detector at `0x105F`, called from `0x1578`
with `bx = 0x0A`). Only bit 3 of `[0xBE]` and bit 2 of `[0xA8]` are ever consumed anywhere
in the 32 KB image — an exhaustive byte-level scan for references to those two RAM
addresses returns 10 sites and no others. `[OBSERVED]`

```
  2EE7  call 0x23BD            ; the BOOKKEEPING menu: needs [0xA8] bit 2 SET
  2EEA  test [0xA8], 4         ; P22 high (AUTOMATIC) -> bookkeeping already handled it
  2EEF  je   0x2EF2
  2EF1  ret
  2EF2  test [0xBE], 8         ; rising edge on P23?
  2EF7  jne  0x2EFA
  2EF9  ret
  2EFA  and  [0xBE], 0xF7      ; consume it -> TEST menu, phase 1
```

So: **hold P22 LOW (MANUAL) and pulse P23 → TEST menu. Hold P22 HIGH (AUTOMATIC) and
pulse P23 → bookkeeping/adjustment menu.** That matches the manual's §2.1 / §2.2 wording
exactly, once you know which pin is which. `[OBSERVED]`

Inside the TEST menu each further P23 pulse steps the phase (`0x30FC`); the six phases are
1 DISPLAY, 2 LAMPS-all, 3 LAMPS-nº, **4 COILS-nº**, 5 SWITCHES, 6 SOUND, in that order.
Within a phase, `0x3112` decides how elements advance: with P22 LOW (MANUAL) **any**
playfield-matrix switch closure steps to the next element (`0x238C` just scans the ten
event bytes `[0xB4..0xBD]` for a nonzero one — it is not specifically the start button);
with P22 HIGH (AUTOMATIC) it free-runs on the `[0x27C]` timer. `[OBSERVED]`

### 2.1 The 3.7-minute boot, and how to skip it

Mephisto **no longer restarts its init every ~4 s** — that behaviour in `CLAUDE.md` is
stale. It now boots all the way to attract and stays there indefinitely. `[OBSERVED]`

But it takes about **3 minutes 45 seconds** to get there, and the reason is worth
recording because it also blocks the service menu: the boot lamp census at `0x16CB` walks
all 64 lamps, and **every one of them fails**, so the fault-replay loop at `0x1748` then
displays `FALLO LUCES nº 00` … `nº 63`, one every ~3.5 s. `[OBSERVED]`

The cause is a one-line driver gap, not a ROM problem. `0x0FAD` reads MUART **Port 1
bit 0 (P10, `S.C. MAT`)** as the lamp-drive current sense; seven consecutive passes with
it low set the fault flag `[0x8A]`, and `cirsa_p1_in()` returns `0x00` unconditionally, so
P10 is permanently low and every lamp is condemned. `[OBSERVED]` (ROM + the handler's own
source). Fixing that is a separate job — P10 is shared with Sport 2000, where the same
handler documents it as the `CONT` contact line — and was deliberately not attempted here.

The harness workaround, which touches only the ROM's own result table and nothing on the
coil path: once the replay has started, write `0xFF` over its 64-byte per-lamp "OK" table
and the replay ends on the next entry.

```
  mephisto  RAM 0x197 -> linear 0x10197     (loop at 0x1748, `test [bx+0x197], 0xFF`)
  mephist1  RAM 0x191 -> linear 0x10191     (same loop at 0x14FB)
```

### 2.2 Reproduction, verbatim

```bash
cd "/code/cirsa sport 2000/pinmame"
./xpinmamed.x11 -headless -startpaused -httpport 9710 -nosound -fakesound \
    -skip_gamewarnings -skip_gameinfo -rompath ../build/roms -log /tmp/coil.log mephisto &

P=http://localhost:9710/api
adv() { curl -s "$P/input/matrix?col=0&val=$1" >/dev/null; }   # col 0 = cabinet, hex
sw()  { curl -s "$P/input/matrix?col=1&val=$1" >/dev/null; }   # any playfield column
curl -s "$P/debugger/control?cmd=resume" >/dev/null

# ~75 s: wait until the display reads "FALLO / LUCES / n NN", then end the replay
curl -s "$P/debugger/memory/write?addr=10197&data=$(python3 -c 'print("FF"*64)')&cpu=0"
# wait until the display leaves FALLO (a few seconds)

adv 0                                   # P22 LOW = MANUAL.  Must be low BEFORE the pulse.
adv 2; sleep 0.3; adv 0; sleep 2        # -> "-1-FASE / TEST / DISPLAY"
adv 2; sleep 0.3; adv 0; sleep 2        # -> "-2-FASE / TEST / LUCES / TODA"
adv 2; sleep 0.3; adv 0; sleep 2        # -> "-3-FASE / TEST / LUCES / N"
adv 2; sleep 0.3; adv 0; sleep 2        # -> "-4-FASE / TEST / BOBINAS / N 00"

sw 01; sleep 1.2; sw 00; sleep 1.2      # one switch closure = next coil
```

Two things that cost time:

- **`/api/input/matrix?val=` is hex**, same trap as Sport 2000.
- The programming-switch pulses only register once the fault replay has actually finished.
  A pulse delivered while `FALLO LUCES` is still on screen is swallowed, and the phase walk
  then lands one phase short. Read the display back after each pulse rather than counting.
- The coil number is at `coreGlobals.segments[26]` and `[27]` (the ROM writes it to
  DisplayBuffer `[0x1F2]`/`[0x1F3]`; `mephist1` uses `[0x1EC]`/`[0x1ED]`, which lands on the
  same two `segments[]` slots).

---

## 3. Where the numbering comes from in the ROM

Two routines, and they are byte-for-byte identical between rev 1.2 and rev 1.1.

**The COIL TEST element routine** — `0x3178` (rev 1.1: `0x2E01`). `[OBSERVED]` (ROM bytes)

```
  3178  push bx
  3179  mov [2],0 / mov [3],0 / mov [4],0     ; the three per-group coil bytes
  3188  mov ax,bx                             ; bx = the coil number being tested, 0..23
  318B  mov ah,0 / mov cl,8 / div cl          ; al = N/8 (group), ah = N%8
  3191  mov bl,al / mov cl,ah
  3195  mov al,0x80
  3197  ror al,cl                             ; <-- mask = 0x80 >> (N%8), NOT 1 << (N%8)
  3199  mov [bx+2],al
  319E  call 0x3206                           ; N -> decimal digits -> the display
```

and the loop that drives it (`0x2FEC`) runs `bx` from 0 to **0x17**, wrapping — 24 coils,
not the 34 the manual's "nº 00 to nº 33" claims. `[OBSERVED]` The manual is simply wrong
there; the scan really does read 33, but the ROM's own bound is 24.

**The frame builder** — `0x126C` (rev 1.1: `0x103F`). `[OBSERVED]` (ROM bytes) /
`[STATIC]` (bit-order simulation). Eight iterations, `cx` counting 8→1 so the position
field `p` runs 7,6,5,4,3,2,1,0; each iteration shifts the four source bytes left through
carry and interleaves the carries into `al`:

```
  al after the three trailing ROLs:
    bit 7   strobe (always 1 here; pulsed low/high by the 0x12EA transaction)
    bit 6   RAM[0x24]  heartbeat / INH BOB      (not a coil group)
    bit 5   RAM[0x23]  group 2  (J13)
    bit 4   RAM[0x22]  group 1  (J12)
    bit 3   RAM[0x21]  group 0  (J11)
    bits2-0 position p
```

Because the sources are shifted left once per iteration and `p` counts down from 7, the
bit emitted at position `p` is **group-byte bit `p`**. Combine that with the `0x80 >> (N%8)`
mask above and coil `N` lands at position `7 - (N%8)`. `[STATIC]`, and then confirmed
`[OBSERVED]` end to end in §4.

`RAM[0x21..0x23]` are produced by the combine at `0x1513`:
`[0x21+g] = [g+2] & [g+0x1B] & [g+0x18]` for `g = 0,1,2` — the coil byte ANDed with a
hold-timeout mask and the census "enabled" mask. `[OBSERVED]`

For contrast, Sport 2000's `SolenoidOn(N)` (`0xC7D8`, table at `0xC81E`) sets
`1 << (N%8)`, and its own already-published live capture shows coil N at position N%8.
That is the whole of the difference.

---

## 4. The live join

`ic9_pa_w` was given a temporary `logerror` that prints the PA byte whenever the
coil-data bits change, the machine was walked through COIL TEST 4-PHASE one element at a
time, and the displayed number, the PA byte and `coreGlobals.solenoids` were read together
at each step. Both ROM revisions, 24 of 24 coils, no exceptions:

```
mephisto (rev 1.2)                       mephist1 (rev 1.1)
N=00 sol=0x000001  pa=8f pos=7 g0        N=00 sol=0x000001  pa=8f pos=7 g0
N=01 sol=0x000002  pa=8e pos=6 g0        N=01 sol=0x000002  pa=8e pos=6 g0
...                                      ...
N=07 sol=0x000080  pa=c8 pos=0 g0        N=07 sol=0x000080  pa=c8 pos=0 g0
N=08 sol=0x000100  pa=97 pos=7 g1        N=08 sol=0x000100  pa=97 pos=7 g1
N=15 sol=0x008000  pa=d0 pos=0 g1        N=15 sol=0x008000  pa=d0 pos=0 g1
N=16 sol=0x010000  pa=a7 pos=7 g2        N=16 sol=0x010000  pa=a7 pos=7 g2
N=23 sol=0x800000  pa=e0 pos=0 g2        N=23 sol=0x800000  pa=e0 pos=0 g2
```

`coreGlobals.solenoids == 1 << N` for every displayed `N`, in both games. `[OBSERVED]`

**One caveat, stated plainly.** That capture required a *second* temporary change, because
of §6: nothing in this driver drives Mephisto's EXTINT pin, and the ISR that builds and
sends the coil frame is EXTINT's. Both temporary changes — the trace and the EXTINT pulse
— were reverted before committing, and `git status --porcelain -- src/` shows only the
intended edit. With them reverted, `ic9_pa_w` sees only Mephisto's watchdog byte
(`0x00`/`0x40`/`0xC0` — position 0, all three data bits clear) and `coreGlobals.solenoids`
stays 0, which is correct: the decode is right, the data is simply not arriving yet.

---

## 5. Coil number vs the manual's playfield names

The driver only needs numbers, and §1-§4 settle those. This section is the weaker,
separate claim that **the ROM's coil number equals the manual's own item number** in
`mephisto_manual_en.md` §5 (*SITUACION BOBINAS*). Six independent things agree and one
disagrees.

**Agreeing:**

1. **The flasher block.** The boot coil census (`0x178D`, 34-byte table at `0x182E`) tests
   17 of the 24 positions and pre-marks the rest "already fine" without testing. Group 2's
   preset is `[0x1A] = 0xFC`, i.e. bits 2-7 — which under §1's mapping are coils
   **16, 17, 18, 19, 20, 21**. The manual's #16-#21 are exactly the six flasher circuits
   (lamps + ballast resistors, not solenoids), the six things an open-circuit solenoid test
   would have to skip. `[OBSERVED]` (ROM) + `[OBSERVED]` (manual scan).
2. **The hold exemption.** The coil-hold protection (`0x1582`) cuts any coil left on for
   five consecutive passes, masked by `cs:[0x15F7] = FF FF FE`. The single exempt bit is
   group 2 bit 0 = **coil 23** = manual **#23 GATE** — a gate solenoid is precisely the one
   that has to stay energised. `[OBSERVED]`
3. **The other census skip.** Group 1's preset `[0x19] = 0x02` is bit 1 = **coil 14** =
   manual **#14 "Exp. depósito bolas"**. The earlier B1-harness reading
   (`docs/findings/2026-09-01-mephisto-coil-bus.md` §6, in the workspace repo) independently
   places `DEPOSITO BOLA` on **J12 pin 10**, which is **PA position 1** — and coil 14 is at
   position `7-(14%8) = 1`. Three sources, one slot. That earlier document flagged this as
   "a concrete, checkable prediction for whoever closes the connector→group question": it
   checks out, and it also confirms J12 shares J11/J13's physical pin order, which that
   document had assumed but not verified. `[OBSERVED]` + `[INFERRED]`
4. **Group 0 is the quick-contact group.** The level-2 ISR loads the group-0 byte straight
   from IC9 Port B (`0x14D0: mov al,[0x4802]; mov [2],al`, guarded by `[0x148]`), i.e.
   coils 0-7 are the hardware-immediate contact-driven ones. Manual #2/#3/#4 — the three
   bumpers — are coils 2/3/4, and #1/#5/#6/#7 are blank in the manual: unused quick-contact
   slots. `[OBSERVED]` (ROM) + `[INFERRED]` (the reading of "blank").
5. **The three bumpers, by connector pin.** Translating the earlier B1-harness readings
   through `coil = group*8 + (7 - position)`:
   `BUMPER SUP` J11/3 → position 5 → coil 2 = **#2 Upper bumper**;
   `BUMPER DEREC` J11/2 → position 4 → coil 3 = **#3 Right bumper**;
   bumper `IZQ` J11/5 → position 3 → coil 4 = **#4 Lower bumper** (the `IZQ`-vs-`inferior`
   wording mismatch was already flagged in that document as a difference between the two
   printed sources, not a misreading). Three for three. `[OBSERVED]` (that document's scan)
   + `[INFERRED]` (the translation).
6. **TACA, GATE and the bat flasher.** `TACA` J13/10 → position 1 → coil 22 = **#22**;
   `GATE` J13/9 → position 0 → coil 23 = **#23**; `FLASH MURCIELAGO` J13/2 → position 4 →
   coil 19 = **#19 Bat flasher**. Same translation, three more hits.

**Disagreeing — reported, not smoothed over:** the same B1 transcription reads `OJO`
(eyes, manual #18) at **J13 pin 8**, which is position 7 and therefore coil **16**, whose
manual name is "Skull flasher". Under this mapping #18 should be on J13 pin 3. Nothing was
done to resolve it here: it is one shorthand word on a 1987 hand-drafted wiring plate, in
exactly the group of flasher/ejector shorthands that document's own §6 already listed as
"read with confidence as *groups*; the specific single-coil identity behind each is not
independently cross-checked". It is one conflict against six agreements, all six of which
rest on ROM behaviour rather than on reading that plate. Re-reading J13's flasher block at
600 DPI is the way to close it.

**None of this affects the driver.** `ic9_pa_w` publishes coil *numbers*, and those come
from the ROM's own COIL TEST, not from this section.

---

## 6. What still blocks Mephisto's coils, and it is not the numbering

**Nothing drives Mephisto's MUART EXTINT pin, and EXTINT is what sends the coil frame.**

- Mephisto's interrupt vector table (ROM `0x100..0x11F`, types 0x40-0x47) has handlers at
  L0 `0x08EB`, L1 `0x0A49`, **L2 `0x1416`**, L4 `0x0DF4`; L3/L5/L6/L7 are `FFFF:FFFF`.
  `SETINT` is written as `7` at `0x0771` then `|= 0x10` at `0x07F6` and `|= 1` at `0x0913`,
  so the enabled set is L0, L1, L2, L4. `[OBSERVED]`
  (`CLAUDE.md`'s "SETINT = 0x76 … L6 Timer 4" is stale: L6 has no handler, and enabling it
  would vector the machine into `FFFF:FFFF`.)
- **L2 is EXTINT**, and its handler `0x1416` is the one that reads IC9 Port B into the
  group-0 coil byte, pulses the `74LS373` latch on PC5, and then calls the combine
  (`0x1513`) and the frame builder (`0x122A`). It masks itself at entry
  (`[0x200C] = 4`) and re-enables itself at exit (`[0x200A] = 4`). `[OBSERVED]`
- `cirsa_vblank` calls `i8256_set_extint()` **only for Sport 2000**. So on Mephisto that
  ISR never runs. Measured directly: a 20,000-write histogram of every byte reaching
  `ic9_pa_w` while sitting *inside* COIL TEST 4-PHASE with coil 00 selected contains only
  `00`, `40` and `C0` — the watchdog byte from `0x1223` — and not one position-field value.
  `[OBSERVED]` Pulsing EXTINT once a frame makes full 8-byte frames appear immediately and
  the whole walk in §4 work. `[OBSERVED]`
- The other frame path, `0x0A15 → 0x1330`, is gated on `[0x47A]`, which the ROM sets only
  for a brief window during the service-menu entry (`0x2F05`/`0x2F15`) and at `0x083D`.
  It is not a substitute. `[OBSERVED]`

What actually drives that pin on the real board was **not** established. The Mephisto
manual's plate 4 lists `EXTINT` on IC4 without saying what feeds it; the board also carries
a 4060 oscillator/divider (IC22) and a 74LS393 divider (IC15) that would be plausible
sources, and `S.C. MAT` is on the same board. Guessing between them is exactly the kind of
invention this document set is written to avoid. It is the next task, and it is now a
well-defined one: find EXTINT's source on plate 4/5, wire it, and the coil bus lights up
with the numbering above already in place.

---

## 7. What changed in the driver

`src/wpc/mephisto.c`:

- `ic9_pa_w`: the `if (core_gameData->hw.gameSpecific1) return;` gate is gone; the position
  is decoded per game (`pos` for Sport 2000, `7 - pos` for Mephisto). The block comment
  above the handler now carries the per-game rule, the ROM addresses it comes from, and the
  observed PA bytes at each group boundary.
- The quick-contact OR (`locals.qcState & 0x0e`) is now explicitly Sport-2000-only. It was
  already inert for Mephisto (`qcState` is only maintained for Sport 2000), but the gate it
  used to sit behind is gone, so the condition is written out.
- `MACHINE_INIT(CIRSA)` advertises `nSolenoids = 24` and sets up the PWM integrator for
  both games rather than Sport 2000 alone. Mephisto feeds it from the same handler on the
  same schedule; until EXTINT is connected it feeds an honest all-zero state.

Regression, `scripts/probe.sh --seconds 30`, `sport2k` / `mephisto` / `mephist1`: switches,
lamps and segments are **byte-identical** to the same three probes run against `bd3eb477`
with the change stashed.

---

## 8. Still unknown

- **Mephisto's EXTINT source** (§6). The one thing between this table and a live coil bus.
- **Why every lamp fails the boot census** (§2.1) — traced to `cirsa_p1_in()` returning
  P10 = 0, but not fixed, because P10 is shared with Sport 2000.
- **`OJO` on J13 pin 8** (§5) — one wiring-plate reading that conflicts with six ROM-based
  agreements.
- **PA bit 6.** Still not latched into `coreGlobals` for either game. It is the ROM's
  heartbeat and the schematic's separate `INH BOB` 74HC259 sits on the same bus, but the
  net was not traced.
- **The manual's "nº 00 to nº 33"** (§3). The ROM's own bound is 24. Whether the manual is
  describing an earlier hardware revision or is simply a typo was not investigated.
