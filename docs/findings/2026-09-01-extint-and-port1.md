# What drives EXTINT on both boards, and the one Port 1 pin that was failing every lamp

**Date:** 2026-09-01
**Repo:** this fork, branch `sport2000-phase0`, from `a1707f52`.
**Status:** **Resolved.** Mephisto's coil bus is live (24/24 coils assert in its own
COIL TEST, both ROM revisions), and the boot lamp test passes in both games.

Every claim is tagged `[OBSERVED]` (measured on the running emulator, or read
byte-for-byte out of a ROM image / off the scanned plate at 600 dpi), `[STATIC]`
(derived by disassembling ROM code, not measured) or `[INFERRED]` (reasoned from
something observed, not itself measured).

---

## 1. Bottom line

**Both boards drive the 8256's EXTINT pin from the same circuit, and it is not the
quick contacts. It is `PPCERO`, the mains zero crossing.**

```
    LM393 IC25, comparator B (pins 5,6 -> 7; open collector)
        |                         pulled up to 5V: R13 4K7 (sport2k) / R15 1K (mephisto)
        |
        +---> 8256 pin 38 = P11 .......... labelled PPCERO on the Sport 2000 plate
        |
        +---> 74HC02 IC26 pin 6 ..+
                                  :   NOR gate 2, out on pin 4
    8256 pin 37 = P12 (OUTPUT)    :        |
        -> 74LS14 IC24 pin 3      :        v
           -> pin 4 -> IC26 pin 5 +   74LS14 IC24 pin 9 -> pin 8 -> 8256 pin 16 EXTINT

    EXTINT = PPCERO OR NOT(P12)
```

`[OBSERVED]` on both plates: Sport 2000 plate 5 (PDF p. 28) and Mephisto plate 4
(PDF p. 24). Same three chips, same designators, same gate pins, same topology.

The driver now feeds P11 and EXTINT with a 100 Hz square wave. With that one change:

| | before | after |
|---|---|---|
| Mephisto boot | `FALLO LUCES n 01 .. n 63`, one every ~3.5 s, ~3 min 45 s of it | no fault at all; attract animates |
| Mephisto coils in COIL TEST | nothing on the PA bus but the watchdog byte | 24/24, `solenoids == 1 << N` for the N the machine displays |
| Sport 2000 lamps | full at boot, then **dark** from ~t=9 s for the rest of attract | lit and steady |
| Sport 2000 quick contacts | 5/5 | 5/5 (unchanged) |

---

## 2. How the plate was read

The transcriptions are component lists, not net-level traces, so this was done on the
600 dpi renders (`pdftoppm -r 600 -png`), and the ambiguous parts were settled by
scanning pixel columns/rows for dark runs rather than by eye. `[OBSERVED]`

**Pin identification.** Both plates draw the 8256 with its pin numbers next to the
leads, and they match the 8256AH DIP-40 pinout exactly: 39..32 = P10..P17, 31..24 =
P20..P27, 16 = EXTINT, 23 = TxD, 21 = CTS, 19 = RxD. On Sport 2000 every one of the
Port 1 leads is also named on the drawing:

| pin | | Sport 2000 label | Mephisto label |
|---|---|---|---|
| 32 | P17 | `F.T.` | (unlabelled) |
| 35 | P14 | `CL-WD` | (unlabelled) |
| 36 | P13 | `PWEN` (overbar) | (unlabelled) |
| 37 | P12 | `RST PER`, then `INT-CR` further out | `RST PER`, then `INP1` further out |
| 38 | P11 | `PPCERO` | (unlabelled) |
| 39 | P10 | `S.C. MAT`, then `CONT` further out | `S.C. MAT` |

`[OBSERVED]`

**The lane jogs are dodges, not crossings.** Three of these leads make a shallow
right-then-left chevron where they cross a horizontal bus, and reading that as a
lane *swap* would put the wrong name on the wrong pin. Scanning columns across the
crossing settles it: on Sport 2000 the pin-37 lane is at y=2984 before the bus and
y=2986 after it (it only bulges to 2997/3002 *at* the bus), and the pin-38 lane runs
straight through at y=3027/3029; only the pin-39 lane genuinely moves, 3067 -> 3088.
`[OBSERVED]` So `RST PER`/`INT-CR` are one net on P12, and `S.C. MAT`/`CONT` are one
net on P10.

**The EXTINT chain.** On the Mephisto plate the EXTINT lead (x=4969) runs straight
down from pin 16 to y=4935 and turns left onto a horizontal that is continuous from
x=4362 to x=4981 and ends at the output bubble (pin 8) of a 74LS14 marked IC24; that
gate's input pin 9 is the output pin 4 of the 74HC02 marked IC26. IC26's pin 6 sits
on a horizontal at y=4833 which is continuous from x=3988 (the LM393 IC25 pin-7 node,
with its 1K pull-up to 5V) to x=4795, where the P11 lead terminates. IC26's pin 5 is
driven by a second 74LS14 gate whose input (pin 3) is the `INP1`/P12 lead. `[OBSERVED]`
The Sport 2000 plate shows the identical `IC26 (5,6->4) -> IC24 (9->8)` pair with the
same `IC24 (3->4)` feeding pin 5, and PPCERO reaching pin 6 through the R13 4K7
pull-up. `[OBSERVED]`

---

## 3. Two independent ROM-side confirmations

**Both level-2 ISRs read P11 as their first act.** `[OBSERVED]` (ROM bytes)

```
  sport2k   0x0937   f6 06 10 a0 02   test byte ptr [0xA010], 2     ; MUART Port 1 bit 1
  mephisto  0x143B   f7 06 10 20 02   test word ptr [0x2010], 2     ; same bit
```

That is only sensible if EXTINT and P11 carry the same signal.

**Mephisto uses it to re-time the mains-switched loads.** When it finds P11 set, the
ISR falls into `0x1443`-`0x14BC`, which drives MUART Port 2 bits 5, 6 and 7 -- the
`INH LF`, `INH FLIP` and `INH L.C.` inhibit outputs -- from four RAM flags, then
continues into the coil-frame path. Switching those on the zero crossing is exactly
what a zero-cross signal is for. `[OBSERVED]` (ROM bytes) + `[INFERRED]` (the reading).

**Sport 2000 gates the interrupt with it.** Its ISR writes `RSTINT = 4` (disable
level 2) when P11 is set at entry (`0x093E`) and `SETINT = 4` (enable) when it is
clear (`0x0973`); the main loop re-arms the level the same way at `0x0894`/`0x089B`
and `0x08F6`/`0x08FD`. So the ROM itself limits the level to about one interrupt per
mains half cycle. `[OBSERVED]`

**And this replaces the driver's previous belief.** Sport 2000's level-2 handler is
`ISR_QuickContacts`, so the driver had been raising EXTINT from the quick-contact
switches in `cirsa_vblank`. That was a working stand-in, not the hardware: the
contacts reach the CPU only through IC11 (the 74LS373 the ISR strobes on IC9 PC5 and
reads back on IC9 Port B), and the ISR is a *poller* of that latch, not a
contact-triggered handler. Nothing on either plate routes a contact to EXTINT.
`[OBSERVED]` + `[INFERRED]`

---

## 4. Which pin was failing the lamps: measured, not attributed

The prior round attributed Mephisto's `FALLO LUCES` to P10. The brief for this round
was right to treat that as an attribution: `P11 PPCERO` was equally stubbed, and lamp
timing on this era of hardware is commonly zero-cross synchronised. So it was measured
by A/B, with EXTINT driven from PPCERO in **all** variants so the interrupt is not a
confound:

| variant | `cirsa_p1_in()` | sport2k lamps @30 s | mephisto @90-200 s (`-fakesound`) |
|---|---|---|---|
| A | P10 = 0, P11 = PPCERO | `000000000000000000000000` | `FALLO LUCES n 01 .. n 33` |
| B | P10 = 1, P11 = 0 | `FFFFFFFFFFFFFFFF00000000` | (not run) |
| C | P10 = 1, P11 = PPCERO | `FFFFFFFFFFFFFFFF00000000` | no fault; attract animates |

`[OBSERVED]`, all six cells. **P10 is the cause; P11 is not.** Driving P11 while
leaving P10 low changes nothing about the lamps in either game (variant A), and
driving P10 alone fixes Sport 2000 with P11 still dead (variant B).

The ROM's own state agrees, read straight out of RAM at t=40 s / t=90 s:

```
  sport2k   per-column "lamp failed" flags [0x583..0x58A]   A: 01 x8    C: 00 x8
            per-column fail counters       [0x573..0x57A]   A: 0F..0E   C: 00 x8
            last P10 sample                [0x523]          A: 00       C: 01
  mephisto  global lamp-fault flag         [0x8A]           A: (set, FALLO replay running)
                                                            C: 00
            per-lamp fail counters         [0x96..0x9D]     C: 00 x8
```

`[OBSERVED]`

**Why P10 does it**, from the ROM. A scan of every absolute-addressed access to the
MUART's Port 1 register in both 8088 images (all addressing forms, not just
`mov`-from-moffs) finds bit 0 masked in exactly one place per image, the lamp scan;
every other access is the P14 watchdog toggle, the P16 sound handshake, the P11 test
of §3, a P15 (`test 0x20`) sound-BUSY poll, or the `and 0xF3` of §5. In both images
the bit-0 read is a health check where HIGH means healthy: `[OBSERVED]`

```
  sport2k  0x0B080  mov al,[0xA010] / and al,1 / mov [0x523],al   (lamp rows off)
           0x0B0A3  test [0x523],1  -> set: per-column counter := 0
                                     -> clear: counter++, at 6 set [bx+0x583] = 1
  mephisto 0x00FAD  mov ax,[0x2010] / and al,1
           0x00FB2  set: [bx+0x96] := 0
                    clear: [bx+0x96]++, at 7 set [0x8A] = 0xFF, and 0x0FD3 then
                    switches the whole matrix off
```

Mephisto's boot census at `0x16CB` walks all 64 lamps and, for each, lights it,
delays, and marks it failed if `[0x8A]` is set -- so a permanently-low P10 condemns
every lamp and feeds the `FALLO LUCES` replay at `0x1748`. `[OBSERVED]`

**One caveat on reproducing it.** Mephisto only reaches that census under
`-nosound -fakesound`. With `-nosound` alone the sound CPU is suspended, the ROM
sits at `no Audio`, and `FALLO LUCES` never appears -- at `a1707f52` *or* after this
fix. A run without `-fakesound` cannot see this symptom at all. `[OBSERVED]`

**What S.C. MAT is** is a weaker, separate claim. Both plates name the net, and the
ROM samples it with the lamp rows switched off and treats HIGH as healthy, which reads
as a matrix short/overload sense ("sin corriente" / "sobrecarga"). That reading is
`[INFERRED]`. What is measured is only the polarity the ROM demands. The driver
therefore rests the pin HIGH, which is what a machine with no matrix fault gives.

---

## 5. What changed in the driver

`src/wpc/mephisto.c`, one commit, no change to `src/machine/i8256.c`:

- `locals.ppcero` plus `cirsa_zc_tick()`, a `timer_pulse` at `2 * CIRSA_ZC_HZ`
  (200 Hz, so a 100 Hz square) registered in `MACHINE_INIT(CIRSA)`. It toggles
  `ppcero` and calls `i8256_set_extint()` with the new level.
- `cirsa_p1_in()` returns `0x01 | (ppcero ? 0x02 : 0)`: P10 high, P11 = PPCERO.
  P15 still arrives separately through `i8256_set_p1_pin(5,...)` and is ORed in by
  `i8256_port1_read()`; **P17 still rests low** and nothing here touches it.
- `cirsa_vblank()` no longer raises EXTINT from the quick contacts. It still keeps
  `qcState` and the `qcLatch` transparency, which is the whole of the contact path.

Both games share all of it; there is no `gameSpecific1` branch in any of it.

### Deliberate simplifications, stated plainly

- **The `NOT(P12)` half of the OR is not modelled.** Both ROMs clear P12 (together
  with P13) immediately before their first `sti` -- sport2k `0x05B9` and mephisto
  `0x0783`, both `and Port1, 0xF3` -- and neither ever sets it again. `[OBSERVED]`
  Taken literally that holds EXTINT asserted for the whole run, and the interrupt
  rate is then set purely by the ROM's `SETINT`/`RSTINT` gating in §3. `i8256.c`
  raises a request only on a 0 -> 1 transition of the pin, so feeding it a
  permanently-asserted level would produce exactly **one** level-2 interrupt per
  session. Driving the pin with PPCERO instead reproduces the rate the ROM is written
  around and leaves the shared device alone. If `i8256.c` ever grows a genuinely
  level-sensitive EXTINT that re-requests after EOI, this is the thing to revisit.
- **100 Hz** is 50 Hz Spanish mains, full-wave rectified. `[INFERRED]` Supporting,
  not proving: Mephisto's coil-hold cut-off (`0x1582`) drops any coil left on for
  five consecutive level-2 passes, which at 100 Hz is a 50 ms solenoid pulse.
- **The 50% duty cycle is a modelling choice**, not a measurement. A real zero-cross
  detector emits a narrow pulse. A square is what makes both ROMs' P11 tests come
  out on both phases: Mephisto has to see it HIGH at ISR entry to update the INH
  outputs at all, and Sport 2000 has to see it LOW somewhere to re-arm the level.

---

## 6. Verification

All runs on this machine, `REMOTE_DEBUG=1 DEBUG=1`, rompath `build/roms`.

**Mephisto coils, dense sample.** Boot, enter the service menu (P22 low = MANUAL,
four P23 pulses -> `-4-FASE TEST BOBINAS`), then for each element sample
`coreGlobals.solenoids` every 50 ms for 2.2 s and OR the samples together. **24 of 24
elements, both `mephisto` and `mephist1`: the union of bits seen is exactly
`{N}` for the coil number N the machine itself displays.** `[OBSERVED]` (Sampling
matters: a single spot read per element mostly lands between pulses, which is
`[INFERRED]` to be the ROM's own coil-hold cut-off at `0x1582` limiting each element
to a short burst. The two rows printed as "MISMATCH" by the harness are its 7-segment decoder
failing on the ROM's `9` glyph; the bits were 9 and 19, in sequence.) The
service-menu walk also no longer needs the `0x197`/`0x191` RAM workaround from
`2026-08-31-mephisto-coils.md` §2.1, because there is no fault replay to skip.

**Lamps.** §4's table, plus: with `-fakesound`, Mephisto's attract now animates its
lamp matrix (`lampMatrix` values 0x88, 0x06, 0x7F, 0x01, 0x8E, 0x7E, 0x80, 0x60,
0x82 across a 260 s watch), where at `a1707f52` the same run was `FALLO LUCES n 01`
through `n 50` and all-zero lamps throughout. `[OBSERVED]`

**Interrupt rate**, PC hit counting on the ISR entry over a 10 s window in attract:
`mephisto` `0x1416` = **940**, `sport2k` `0x0931` = **858**. `[OBSERVED]` Both near
the 100 Hz the pin is driven at; Sport 2000 is lower because its own gating skips
half cycles.

**Quick contacts, Sport 2000** -- the regression that mattered most, since their
EXTINT drive was removed. Each of the five held for 1.2 s, read back through the
ROM's own raw Port B mirror `[0x71E]` at `0x2071E`: masks 0x01/0x02/0x04/0x08/0x10
produce `[0x71E]` = 1/2/4/8/16 and back to 0 on release. **5 of 5, no misses.**
`[OBSERVED]` Repeat events are not a hazard: the ROM's consumer at `0xC8E7` is an
edge detector on its own mirror of the byte (`xchg [0x6b3],al / xor / and`), so
re-reading an unchanged latch 100 times a second raises nothing. `[OBSERVED]`

**Service menu, Sport 2000** still walks DISPLAY 1-PHASE -> LAMPS 2-PHASE ->
transistor 3-PHASE -> COILS 4-PHASE -> SWITCH 5-PHASE -> AUDIO 6-PHASE on the
documented button recipe, and coils assert inside COILS 4-PHASE. `[OBSERVED]`

**Three-game regression**, `scripts/probe.sh --seconds 30`, `sport2k` / `mephisto` /
`mephist1`, against a control captured from `a1707f52` on this machine in the same
session: all three exit 0, switches and **segments byte-identical**, and the only
difference anywhere is `sport2k`'s lamps going from `000000000000000000000000` to
`FFFFFFFFFFFFFFFF00000000` -- which is the fix. `[OBSERVED]`

---

## 7. Still unknown

- **The real PPCERO waveform.** Frequency (100 Hz) is inferred from 50 Hz mains and
  supported by the coil-hold arithmetic; the duty cycle is a modelling choice (§5).
  The LM393 front end itself was only partly traced: its reference input sits on a
  3 V zener fed from `+12` through 1K, and its signal input on a 4K7 divider from a
  terminal that reads `5V` on the Mephisto plate. Read literally that is a static
  comparison, which cannot oscillate -- so at least one of those two terminals must
  carry an unfiltered, mains-ripple rail rather than the regulated one its label
  suggests. That was not resolved, and it is the one place where the plate reading
  and the net's own name (`PPCERO`) disagree.
- **`NOT(P12)`** (§5). Whether the real board really holds EXTINT asserted from the
  end of init onwards, and what `INT-CR` / `INP1` are called that for.
- **What `S.C. MAT` physically senses** (§4). The polarity the ROM needs is measured;
  the mechanism is not.
- **Mephisto's `swMatrix`** is still empty in the 30 s probe, and its lamp matrix
  reads all-zero under `-nosound` (no `-fakesound`) because the ROM never leaves the
  `no Audio` screen there. Neither is affected by this change.
