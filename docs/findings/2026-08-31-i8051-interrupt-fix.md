# Fixing `check_interrupts()` in the shared 8051 core: the change, the sweep, the adjudication

Date: 2026-08-31. Repo `pinmame/`, branch `sport2000-phase0`.
Spec: `docs/superpowers/specs/2026-08-31-i8051-interrupt-fix-design.md` (workspace root).
Task 1 (instrumentation, `-ftr` headless fix, harness, baseline) is
`docs/findings/2026-08-31-i8051-baseline-sweep.tsv` and its commits.

Evidence tags: `[OBSERVED]` = measured on this machine in this task; `[STATIC]` = read
from ROM disassembly or source; `[INFERRED]` = reasoned from the two.

## Bottom line

**32 of 35 games are byte-identical after the fix — same audio checksum, same six
per-source dispatch counters. Spinball is 7 of 7 identical.** `[OBSERVED]`

**The only three games that move are `sport2k`, `mephisto` and `mephist1`** — the three
this project cares about — and they move for a reason that is provable from their ROMs
rather than argued from the sound: their 8051 firmware **never sets `IE.ET1`**, in the
entire 32 KB image, yet the old core was dispatching a Timer 1 interrupt at them on
every boot. `[OBSERVED]` + `[STATIC]`

**The pre-registered prediction in `docs/findings/2026-09-01-sound-link.md` §4.2-4.3 is
confirmed.** Before: the Sport 2000 sound CPU took the spurious Timer 1 interrupt at
`0x0490`, never reached `MOV IE,#092h` at `0x00BA`, and parked. After: `0x001B` and
`0x04A1` are never entered, `0x00BA` executes, and the 8051 starts taking Timer 0 and
**serial** interrupts. `[OBSERVED]`

**Spinball sound is not broken.** It could not have been: `spinb.c`'s only 8051 is the
**DMD** CPU. Its sound is two Z80s plus an MSM5205/MSM6585. The 2016 comment this whole
exercise exists because of attributes a sound regression to a CPU that has no sound
role in those games. `[STATIC]`

## 1. The change (`src/cpu/i8051/i8051.c`, commit `781554e4`)

Three parts, not the two the brief anticipated.

**(a) Per-source enable checks.** `EXT0_IRQ`, `TIMER0_IRQ`, `EXT1_IRQ`, `TIMER1_IRQ`
macros pair each flag with its own `IE` bit, matching what the serial path already did
with `GET_ES`. External 0/1 were already *partly* masked (`i8051_set_irq_line` only sets
`IE0`/`IE1` when `EX0`/`EX1` is set); Timers 0 and 1 were not masked at all, because
`TF0`/`TF1` are set by the timer hardware regardless of `ET0`/`ET1`. **Every game that
moved, moved because of Timer 1.**

**(b) Timer 2 gated on the CPU subtype, and `GET_ET2` restored.** Both `#ifdef PINMAME`
sites are gone — the dispatch and the `NO_PENDING_IRQ` early-out — replaced by a single
`TIMER2_IRQ` macro used in both places:

```c
#define TIMER2_IRQ  (TYPE != 8051 && GET_ET2 && (GET_TF2 || GET_EXF2))
```

`TYPE` is `i8051.subtype`, already maintained by the core (`8051` at `i8051.c:578`,
`8752` at `:2439`) and never consulted by `check_interrupts()` before. The
`HAS_I8052 || HAS_I8752` guards around Timer 2 are compile-time and `HAS_I8752` is
defined for the whole build (`src/pinmame.h:57`), so the Timer 2 path was live on I8051
instances — chips with no Timer 2 register at all.

**(c) A latent jump-to-zero that (a) would have introduced.** This is not in the brief
and it is not cosmetic.

`NO_PENDING_IRQ` tested the raw `TCON` flags (`!(R_TCON & 0xaa)`). Its job is to return
early when nothing is pending; the six proposal blocks below it then choose a vector.
Today the two agree exactly, so any call that gets past the early-out always selects a
vector. Add the enable checks to the proposals **only**, and they no longer agree: a set
flag whose enable is clear passes the early-out, no proposal matches, `i8051.int_vec`
stays `0`, and the function reaches

```c
	push_pc();
	PC = i8051.int_vec;      /* == 0: the reset vector, with a return address pushed */
```

Sport 2000's sound ROM sits in exactly that state permanently — Timer 1 free-running as
the baud generator with `ET1` clear — so the four-line version of this fix would have
sent its 8051 to `PC=0` on every pass. `NO_PENDING_IRQ` was therefore rewritten from the
same `*_IRQ` macros, and an explicit `if(!i8051.int_vec) return 0;` was added before the
dispatch as a guard should the two lists ever drift apart again. Both changes can only
*remove* dispatches, so the sweep's core property is preserved.

## 2. A harness correction: NVRAM was a hidden input (commit `48d7f62d`)

Task 1's report ruled out NVRAM ("none are written"). That is wrong: every one of the 35
games leaves a `$HOME/.xpinmame/nvram/<game>.nv` behind, and the next run reads it.
`[OBSERVED]`

It is not a small effect. Same binary, same flags, `-ftr 3600`, three runs each: `[OBSERVED]`

| game | NVRAM cleared each run | NVRAM carried over |
|---|---|---|
| `bsb105` | `ie0=51 riti=265`, hash `c531474d`, 3/3 | `ie0=8 riti=247`, hash `c73ce5f7`, 3/3 |
| `bbb109` | `riti=8`, 3/3 | `riti=11` (and `riti=23` from a third NVRAM state), 3/3 |

Both are perfectly reproducible *once the NVRAM state is pinned*. So the run-to-run
variation Task 1 attributed to "reads of uninitialised memory in those drivers" is
mostly this, and it is controllable. With `rm -f $NVDIR/<game>.nv` before each run, **two
complete 35-game passes now agree on every measured field** — `bsb105`, `bbb109` and
`jolypark`, the three Task 1 called unusable or unstable, included. Only the wall-clock
column differs. `[OBSERVED]`

Every number below is from that harness. Baseline and fixed sweeps were each run twice
and each pair is identical; the committed TSVs are the first pass of each.

- `docs/findings/2026-08-31-i8051-baseline-sweep-nvram-cleared.tsv` (pre-fix)
- `docs/findings/2026-08-31-i8051-fixed-sweep.tsv` (post-fix)

Task 1's `2026-08-31-i8051-baseline-sweep.tsv` is left as it is. It was taken with
NVRAM carried over and disagrees with the clean baseline on `bsb105` and `bbb108`/
`bbb109`; it is not the comparator for this change and should not be used as one.

## 3. The sweep

35 games, `-ftr 3600` (60 s emulated), `-nosound -fakesound`, build
`make -f makefile.unix I8051_SWEEP=1` (no `REMOTE_DEBUG`, no `DEBUG`). All 35 exited 0
in both sweeps.

| game | driver (CPU) | audio hash before | after | ie0 | tf0 | ie1 | tf1 | riti | tf2 | verdict |
|---|---|---|---|---|---|---|---|---|---|---|
| `sport2k` | mephisto.c (I8051) | `1ad5b7ff` | `c065f979` | 0 | **0→6** | 0 | **1→0** | **0→6** | 0 | **MOVED** |
| `mephisto` | mephisto.c (I8051) | `c3701aa7` | `5ec6e519` | 0 | **0→584579** | 0 | **1→0** | **0→1267** | 0 | **MOVED** |
| `mephist1` | mephisto.c (I8051) | `c3701aa7` | `5ec6e519` | 0 | **0→584579** | 0 | **1→0** | **0→1267** | 0 | **MOVED** |
| `uboat65` | nuova.c (I8752) | `3ed9bf4c` | `3ed9bf4c` | 0 | 0 | 1275 | 0 | 600 | 0 | identical |
| `wrldtour` | alvgdmd.c (I8051) | `2d5a231a` | `2d5a231a` | 854 | 5998 | 4405 | 0 | 0 | 0 | identical |
| `wrldtou2` | alvgdmd.c (I8051) | `2d5a231a` | `2d5a231a` | 853 | 5998 | 4405 | 0 | 0 | 0 | identical |
| `wrldtou3` | alvgdmd.c (I8051) | `2d5a231a` | `2d5a231a` | 1283 | 5998 | 4403 | 0 | 0 | 0 | identical |
| `mystcast` | alvgdmd.c (I8051) | `2d5a231a` | `2d5a231a` | 345 | 5995 | 4477 | 0 | 0 | 0 | identical |
| `mystcasa` | alvgdmd.c (I8051) | `2d5a231a` | `2d5a231a` | 44 | 5996 | 4478 | 0 | 0 | 0 | identical |
| `pstlpkr` | alvgdmd.c (I8051) | `2d5a231a` | `2d5a231a` | 99 | 5998 | 4480 | 0 | 0 | 0 | identical |
| `pstlpkr1` | alvgdmd.c (I8051) | `2d5a231a` | `2d5a231a` | 99 | 5998 | 4478 | 0 | 0 | 0 | identical |
| `pmv112` | capcoms.c (I8752) | `f7f15cf3` | `f7f15cf3` | 16 | 56939 | 16 | 0 | 247 | 0 | identical |
| `pmv112r` | capcoms.c (I8752) | `19f2c0dc` | `19f2c0dc` | 16 | 56879 | 16 | 0 | 247 | 0 | identical |
| `abv106` | capcoms.c (I8752) | `f7f15cf3` | `f7f15cf3` | 16 | 56938 | 16 | 0 | 247 | 0 | identical |
| `abv105` | capcoms.c (I8752) | `f7f15cf3` | `f7f15cf3` | 16 | 56938 | 16 | 0 | 247 | 0 | identical |
| `abv106r` | capcoms.c (I8752) | `19f2c0dc` | `19f2c0dc` | 16 | 56877 | 16 | 0 | 247 | 0 | identical |
| `bsv103` | capcoms.c (I8752) | `6fda16cb` | `6fda16cb` | 8 | 58013 | 0 | 0 | 11 | 0 | identical |
| `bsv102` | capcoms.c (I8752) | `6fda16cb` | `6fda16cb` | 8 | 58012 | 0 | 0 | 247 | 0 | identical |
| `bsv100r` | capcoms.c (I8752) | `fb53cb55` | `fb53cb55` | 8 | 57952 | 0 | 0 | 247 | 0 | identical |
| `bsv102r` | capcoms.c (I8752) | `fb53cb55` | `fb53cb55` | 8 | 57952 | 0 | 0 | 247 | 0 | identical |
| `bsb105` | capcoms.c (I8752) | `c531474d` | `c531474d` | 51 | 58072 | 0 | 0 | 265 | 0 | identical |
| `pp100` | capcoms.c (I8752) | `6fda16cb` | `6fda16cb` | 8 | 58013 | 0 | 0 | 247 | 0 | identical |
| `ffv104` | capcoms.c (I8752) | `915de411` | `915de411` | 16 | 56882 | 16 | 0 | 246 | 0 | identical |
| `ffv103` | capcoms.c (I8752) | `915de411` | `915de411` | 16 | 56882 | 16 | 0 | 246 | 0 | identical |
| `ffv101` | capcoms.c (I8752) | `915de411` | `915de411` | 16 | 56882 | 16 | 0 | 246 | 0 | identical |
| `bbb109` | capcoms.c (I8752) | `32156a20` | `32156a20` | 16 | 56711 | 16 | 0 | 8 | 0 | identical |
| `bbb108` | capcoms.c (I8752) | `32156a20` | `32156a20` | 16 | 56711 | 16 | 0 | 12 | 0 | identical |
| `kpb105` | capcoms.c (I8752) | `c484a494` | `c484a494` | 16 | 56741 | 16 | 0 | 258 | 0 | identical |
| `bushido` | spinb.c (I8051) | `cadf6e91` | `cadf6e91` | 67 | 323837 | 0 | 0 | 0 | 0 | identical |
| `bushidoa` | spinb.c (I8051) | `cadf6e91` | `cadf6e91` | 1 | 323836 | 0 | 0 | 0 | 0 | identical |
| `bushidob` | spinb.c (I8051) | `cadf6e91` | `cadf6e91` | 67 | 323837 | 0 | 0 | 0 | 0 | identical |
| `mach2` | spinb.c (I8051) | `efed25da` | `efed25da` | 132 | 323870 | 0 | 0 | 0 | 0 | identical |
| `mach2a` | spinb.c (I8051) | `efed25da` | `efed25da` | 124 | 323870 | 0 | 0 | 0 | 0 | identical |
| `jolypark` | spinb.c (I8051) | `5e3b1bc1` | `5e3b1bc1` | 124 | 320571 | 0 | 0 | 0 | 0 | identical |
| `vrnwrld` | spinb.c (I8051) | `18f53fe1` | `18f53fe1` | 123 | 320706 | 0 | 0 | 0 | 0 | identical |

Per-source totals across all 35 games, before → after: `[OBSERVED]`

| source | before | after | change |
|---|---|---|---|
| IE0 (External 0) | 4,482 | 4,482 | — |
| TF0 (Timer 0) | 3,275,902 | 4,445,066 | +1,169,164 (all of it `sport2k`/`mephisto`/`mephist1`) |
| IE1 (External 1) | 32,577 | 32,577 | — |
| TF1 (Timer 1) | 3 | 0 | −3 (one spurious dispatch each in the three moved games) |
| RI/TI (serial) | 4,115 | 6,655 | +2,540 (same three games) |
| TF2/EXF2 (Timer 2) | 0 | 0 | — |

## 4. Adjudication of every game whose digest moved

Method, per the brief: the question is not whether it sounds different, it is whether
the ROM sets the source's enable bit. Both 8051 code ROMs were disassembled by
recursive descent from the seven interrupt vectors, and *additionally* scanned byte by
byte over the whole 32 KB image for every opcode that can write `IE` (SFR `0xA8`) or its
bit addresses `0xA8`–`0xAF`, so unreached code is covered too. `[STATIC]`

### `sport2k` — `roms/c541_256.bin`, the 8051 sound-board ROM

Every `IE` access in the entire image:

```
0029: MOV IE,#0x00        []
00BA: MOV IE,#0x92        [ET0 ES EA]
0488/0490/0495/049D: CLR / SETB IE.7 (EA)
0F42/0F4A/0F56/0F5E: CLR / SETB IE.7 (EA)
```

**Two `MOV IE,#imm` instructions exist in 32 KB, and their values are `0x00` and `0x92`.
`ET1` is never set. Neither is `ET2`, `EX0` or `EX1`.** The only interrupts this
firmware ever enables are Timer 0 and Serial.

Counters: `tf1 1 → 0`, `tf0 0 → 6`, `riti 0 → 6`. `[OBSERVED]`

Verdict: **the old core was dispatching an interrupt the chip could not have taken.**
Timer 1 runs continuously as the UART baud-rate generator (`TH1=TL1=0xFE`, mode 2), so
`TCON.TF1` is essentially always set; the bare `SETB EA` at `0x0490` then handed
execution to the Timer 1 vector, whose handler is a "give up" trampoline that never
executes `RETI`. Removing that dispatch is exactly what MCS-51 semantics require, and it
is why `MOV IE,#092h` at `0x00BA` now runs and `ES` finally gets set. Full mechanism in
§5.

### `mephisto` and `mephist1` — `roms/Mephisto_ROMS/Ic15_sonido_control02`

Both revisions share the sound ROM, so they are one case. Every `IE` access:

```
0029: MOV IE,#0x00        []
0433: MOV IE,#0x92        [ET0 ES EA]
051D: PUSH IE   0571: POP IE
052F/0545, 0864/086C, 0878/0880, 25A6/25AE, 25BA/25C2: CLR / SETB IE.7 (EA)
```

Identical picture: the only value ever written to `IE` is `0x92`; `ET1` is never set.
Counters: `tf1 1 → 0`, `tf0 0 → 584,579`, `riti 0 → 1,267`. `[OBSERVED]`

Verdict: same defect, same fix, and it matters more here — Mephisto's sound CPU went
from **one** interrupt in 60 s to **585,846**. Its display is unchanged: `no Audio` on
both revisions before and after (§6).

Note the direction of every move. The counter that went **down** is the one source whose
enable bit the ROM never sets. The counters that went **up** are the two sources it does
enable, and they went up because the CPU is no longer parked. Nothing was added that the
`IE` register did not authorise.

### The other 32 games

Byte-identical audio digest and identical counters in all six sources. Per the design's
own property — a per-source enable check can only remove dispatches, never add one or
change one's timing — the fix is *provably* a no-op for them. No explanation needed and
none is offered. `[OBSERVED]`

## 5. The Sport 2000 prediction, tested explicitly

`docs/findings/2026-09-01-sound-link.md` §4.2-4.3 made a falsifiable prediction: with
`ET1` checked, the spurious Timer 1 dispatch cannot happen, so the 8051 should get past
`0x0490` and reach `MOV IE,#092h` at `0x00BA`.

Measured with PC hit counters on CPU 1 (`/api/debugger/instrument`), `-nosound
-fakesound`, 45 s, NVRAM cleared, same binary except for this commit: `[OBSERVED]`

| CPU 1 address | what it is | before | after |
|---|---|---|---|
| `0x0490` | `SETB EA` in the subroutine at `0x0485` | 1 | 2 |
| `0x001B` | Timer 1 interrupt vector | **1** | **0** |
| `0x04A1` | the "give up" trampoline `LJMP 0BD` | **1** | **0** |
| `0x00BA` | `MOV IE,#092h` — the write that sets `ES` | **0** | **2** |
| `0x00BD` | `LCALL 024Dh` | 1 | 2 |
| `0x024D` | the do-nothing subroutine | 7,325,417 | 6,474,267 |

Every element of the prediction holds. The Timer 1 vector is never entered, the
trampoline is never entered, `0x00BA` executes (twice — once per startup CPU reset), so
`IE` reaches `0x92` and `ES` is set. `SCON.REN` was already being set at `0x009D`, so
the serial link is now armed at both ends, and the sweep confirms it: `riti` goes
`0 → 6` for `sport2k` and `0 → 1,267` for `mephisto`. `sport2k`'s audio also leaves the
dither floor for the first time — `loud` goes `0 → 478,781` of 479,066 samples.

**What this does not do: it does not make Sport 2000's sound work.** The main CPU still
displays `NO AUDIO` during its boot self-test, before and after, and the rest of the
attract cycle is unchanged. `[OBSERVED]` — 60 s of segment sampling on both binaries:

```
before:  NO AUDIO / GAME OVER / 1000000... / SPORT 2000 / UNIDESA CIRSA / FREE GAME AT / HIGHEST SCORES
after :  NO AUDIO / GAME OVER / 1000000... / SPORT 2000 / UNIDESA CIRSA / FREE GAME AT
```

So the 8051-side deadlock is fixed and the sound CPU is alive and taking serial
interrupts, but the handshake the main CPU polls for at `0x0616` is still not being
satisfied. That is the next question, and it is a driver/link question, not a CPU-core
one. `sport2k` taking only 6 Timer 0 and 6 serial interrupts in 60 s — against
Mephisto's 584,579 and 1,267 on the same code path — says the Sport 2000 sound CPU
reaches its main loop and then has very little to do, which is consistent with a link
that is armed but not conversing.

## 6. Spinball, by name

The documented prior regression. All seven, before → after: `[OBSERVED]`

| game | audio hash before | audio hash after | ie0 | tf0 | ie1 | tf1 | riti | tf2 |
|---|---|---|---|---|---|---|---|---|
| `bushido`  | `cadf6e91` | `cadf6e91` | 67 | 323,837 | 0 | 0 | 0 | 0 |
| `bushidoa` | `cadf6e91` | `cadf6e91` | 1  | 323,836 | 0 | 0 | 0 | 0 |
| `bushidob` | `cadf6e91` | `cadf6e91` | 67 | 323,837 | 0 | 0 | 0 | 0 |
| `mach2`    | `efed25da` | `efed25da` | 132 | 323,870 | 0 | 0 | 0 | 0 |
| `mach2a`   | `efed25da` | `efed25da` | 124 | 323,870 | 0 | 0 | 0 | 0 |
| `jolypark` | `5e3b1bc1` | `5e3b1bc1` | 124 | 320,571 | 0 | 0 | 0 | 0 |
| `vrnwrld`  | `18f53fe1` | `18f53fe1` | 123 | 320,706 | 0 | 0 | 0 | 0 |

**7 of 7 byte-identical, in the audio checksum and in every counter.** Note that
`jolypark`'s checksum — which Task 1 found unusable because it moved run to run — is
stable and usable now that NVRAM is cleared, so this is a real comparison for all seven,
not six plus a shrug.

### Why the 2016 comment was pointing at the wrong thing

Two things came out of reading it properly, and they matter more than the sweep row.

**`spinb.c`'s only 8051 is the DMD CPU, not a sound CPU.** `[STATIC]` The machine driver
adds `MDRV_CPU_ADD(I8051, ...)` for the DMD and two `Z80`s flagged `CPU_AUDIO_CPU` for
sound, driving an MSM5205 (Bushido, Mach 2) or MSM6585 (Jolly Park, Verne's World). That
was already true at `f8d61ac0`, the 2016 commit that added the `#ifdef` — checked
against that revision of `spinb.c`, not just today's. An 8051 interrupt change cannot
break spinball sound directly. The only route is indirect: `ci23_portc_r` calls
`activecpu_abort_timeslice()` while the DMD is not ready, so a wedged DMD CPU starves the
main Z80, which is what actually issues sound commands.

**The 2016 commit changed four things at once.** `[STATIC]` `f8d61ac0` ("some partial
merges from MAME/MESS", message: "partial, as otherwise spinball sound does not work
anymore") also fixed the Timer 0 and Timer 1 mode decode (`GET_M0_0 + GET_M0_1` →
`(GET_M0_1<<1) | GET_M0_0`), added split-timer mode 3, and fixed the serial mode decode
the same way. Those went in unguarded; only Timer 2's `GET_ET2` was put behind the
`#ifdef`. Whether the regression was ever attributed to Timer 2 by bisection or by guess
is not recorded, and with four semantic changes in one commit it cannot be recovered now.

## 7. Timer 2: what justifies the change, and what does not

**The sweep does not clear the Timer 2 change and is not offered as doing so.** `tf2` is
0 in all 35 games before *and* after, so the sweep can only fail to condemn it. "Spinball
7/7 unchanged" is true and, for Timer 2 specifically, means nothing.

What the change rests on, strongest first:

1. **The datasheet.** An 8051 has no Timer 2. `spinb.c`, `mephisto.c` and `alvgdmd.c`
   instantiate `I8051`; the dispatch could not happen on that silicon, so gating it on
   `TYPE != 8051` is a correctness fix independent of any measurement. `[STATIC]`

2. **`GET_ET2` is plain MCS-51 semantics** for the instances that do have a Timer 2
   (`capcoms.c`, `nuova.c`, both `I8752`). No game in the tree sets `ET2` — see 4 below —
   so on real hardware none of them can take a Timer 2 interrupt, whatever `TF2` does.

3. **`EXF2` can never be set by this core.** `SET_EXF2` is defined at `i8051.c:332` and
   called from nowhere. `[STATIC]` So the `GET_EXF2` half of the old condition was dead
   in every game regardless.

4. **The firmware, scanned.** Whole-image scans for `T2CON` (SFR `0xC8`, bits
   `0xC8`–`0xCF`) and `IE` writes: `[STATIC]`

   - **Spinball DMD ROMs** (`g-disply.bin`, `m2dmdf.01`, `jpdmd0.rom`, `vwdmd0.rom`, 64 KB
     each): the only `IE` values written anywhere are `0x00`, `0x83` (`EA ET0 EX0`) and
     `0x82` (`EA ET0`). **`ET2` is never set, and there is no `T2CON` write anywhere in
     the image** for Bushido, Mach 2 or Verne's World. Jolly Park has two candidate byte
     pairs at `0x673C`/`0x68E4`, both far past its ~`0x1700` code region and unreached by
     recursive descent from the vectors — DMD graphics data, and neither would set `TR2`.
     With `TR2` never set, `update_timer()` never runs Timer 2, so `TF2` never rises. This
     is workload-independent: it holds in gameplay as much as in attract.
   - **Alvin G DMD ROM** (`dot27c.512`, upper 32 KB = the 8051 image): sets `EA`, `EX0`,
     `EX1`, `ET0`. Never `ET1`, `ET2` or `ES` — which is exactly the `ie0`/`tf0`/`ie1`
     mix the sweep shows, with all three unchanged by the fix.
   - **Capcom sound ROM** (`u24_v11.bin`, `I8752`) — the interesting one. It *does* use
     Timer 2: `MOV T2CON,#0x30` (`TCLK|RCLK`) then `SETB TR2`, i.e. **Timer 2 as the UART
     baud-rate generator**. It then does `SETB ES`, `SETB ET0`, `SETB EX1`, `SETB EA` —
     **`ET2` is never set.** And in `update_timer()` the baud-generator branch deliberately
     does *not* call `SET_TF2(1)`. So `tf2=0` for the Capcom games is not an accident of
     the workload; it has a mechanism.
   - **`uboat65` sound ROM** (`I8752`, `snd_ic3.256` + `snd_ic5.256` assembled per the
     driver's `ROM_CONTINUE` mapping): recursive descent finds no `T2CON` access at all in
     reachable code, and `IE` only ever gains `EX1`, `ES`, `EA`. The `SETB EXF2` byte pairs
     the raw scan finds are in unreachable regions.

**The validation gap, stated plainly.** No game in this tree was observed taking a Timer
2 interrupt, before or after, so the change to that path has **not** been exercised end
to end by execution. Points 1–4 are a hardware argument, a semantics argument and a
static ROM argument; they are not a measurement of the changed path running. If a future
game or ROM revision does set `ET2` and let Timer 2 overflow outside baud mode, this
change alters its behaviour and nothing here has tested that.

Two further limits on the static half: recursive descent from the seven vectors reaches
1–13% of each image (it stops at `JMP @A+DPTR` tables), so "never set" outside that
fraction rests on the whole-image byte scan, which finds direct-addressed writes only.
On the **I8051** subtype an indirect write (`MOV @Ri,A` with `Ri = 0xC8`) would also land
on `T2CON`, because `i8051_reset` points `iram_iwrite` at `internal_ram_write`; that
cannot be excluded statically. It is moot after this change — `TIMER2_IRQ` gates on
`TYPE != 8051` — but it would not have been before.

## 8. Step 0: the bounded attempt to make TF2 fire

Attempted on `mach2` with the **pre-fix** binary, PC hit counters on the DMD 8051's six
interrupt vectors, `-nosound -fakesound`: booted, coined up four times through
`/api/input/port?port=2&val=1`, pressed start (`val=10`), then pulsed all eight rows of
switch columns 0–6 and both flippers. `[OBSERVED]`

The machine responded — `solenoids` went to `0x19` and the lamp matrix started changing —
so this got past attract into a started game, though not to anything worth calling
gameplay. After ~2 minutes:

```
vector  0x03 (IE0) = 132      0x0B (TF0) = 547,913
vector  0x13 (IE1) = 0        0x1B (TF1) = 0
vector  0x23 (RI/TI) = 0      0x2B (TF2) = 0
```

**TF2 never fired.** So there is no Timer 2 oracle from this route either, and the
Timer 2 change ships on §7's grounds with §7's gap. Given §7.4 — the DMD firmware
contains no `T2CON` write at all — reaching deeper into gameplay could not have changed
this answer for spinball.

## 9. What remains unverified

- **Timer 2 dispatch is untested by execution.** See §7. This is the one part of the
  change with no measurement behind it.
- **Sport 2000 sound still does not work.** The 8051 deadlock is fixed and the sound CPU
  now runs and takes serial interrupts, but the main CPU still shows `NO AUDIO` and the
  handshake at `0x0616` is still unsatisfied. This fix was necessary, not sufficient.
- **"Audio output" is inferred from `loud`, not heard.** `sport2k` going from `loud=0` to
  `loud=478,781` proves the DAC left the dither floor; it does not prove the samples are
  music. Task 1's caution that a constant idle level reads as `loud` applies.
- **Gameplay is not covered for any game.** All 35 rows are 60 s from reset, which for
  most of them is attract. A source enabled only during a ball in play would not appear.
- **The 26 games outside this project's three are byte-identical, not "verified good".**
  Identical output means the fix changed nothing for them, which is a strong statement
  about *this* change and no statement at all about their overall accuracy.
- **The `#if FIXIRQ` interrupt-blocking path** (`i8051.c:80`, active) interacts with `IE`
  writes and was not analysed; it was equally active before and after, so it cannot
  explain any difference reported here, but it is unexamined.
- **A pre-existing stale-`int_vec` path was found and deliberately not touched.** When
  `check_interrupts()` returns at the "low priority irq in progress" test, `int_vec` is
  left set, so a later call can dispatch a vector whose flag has since cleared. It behaves
  identically before and after this commit; fixing it would change dispatches rather than
  only remove them, which would have invalidated the sweep's core property.

## 10. Commits

| SHA | what |
|---|---|
| `781554e4` | `i8051: require each interrupt source's own IE enable bit` |
| `48d7f62d` | `i8051 sweep: start every run from cleared NVRAM` |
| this one | the two sweep TSVs and this document |

Reproduce:

```bash
make -f makefile.unix I8051_SWEEP=1 -j$(nproc)          # -> ./xpinmames.x11
scripts/i8051_sweep.sh OUT.tsv                          # 35 games, ~3 min
```

No emulator processes were left running.
