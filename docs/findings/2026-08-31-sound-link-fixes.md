# The three sound-link fixes: the changes, the sweep, the adjudication

Date: 2026-08-31. Repo `pinmame/`, branch `sport2000-phase0`.
Diagnosis: `docs/findings/2026-08-31-sound-handshake.md` (commit `d1b426e4`), which isolated
each of the three defects by fixing its predecessor and watching the failure move. This
document implements its section 7 recommendation and adjudicates the one change with blast
radius, the same way `docs/findings/2026-08-31-i8051-interrupt-fix.md` adjudicated `781554e4`.

Evidence tags: `[OBSERVED]` = measured on this machine in this task; `[STATIC]` = read from
ROM disassembly, source or schematic; `[INFERRED]` = reasoned from the two.

## Bottom line

**The handshake now passes on the first poll in all three games.** `[OBSERVED]` The first
`STATUS` read of the entire run returns `RBF=1`, the receive buffer reads `0xA5`, and the ROM
takes the success arm. There are no retries and the failure arm is never reached.

**The Timer 1 baud clock is measured unchanged on 32 of the 35 8051-family games in this tree,
and it is unreachable in all 32.** `[OBSERVED]` A temporary counter at the head of the new
`timer1_baud_tick()` shows Timer 1 does not overflow once in 60 s in any of them, so the new
code never runs there. The only three games that move are `sport2k`, `mephisto` and
`mephist1` -- the only ones whose sound 8051 uses Timer 1 as a baud generator.

**This does not make Sport 2000 produce sound**, and nothing below should be read as saying
it does. The sweep's `not_silent` label is a claim about variance, not about audio; the
correction in `docs/findings/2026-08-31-i8051-interrupt-fix.md` (bottom line and section 5)
still stands, and whether the AY-3-8910 is now programmed was not investigated here.

**Two claims in the diagnosis did not survive checking, and are corrected below**: change 1 is
not a `capcoms.c` fix (section 1.1), and the 89% main-to-sound byte loss is a boot-time
artefact rather than a property of the command stream (section 5).

## 1. Change 1 -- serial callbacks survive a CPU reset (`bcf772a8`)

`i8051_reset()` and `i8752_reset()` moved `hold_serial_tx_callback` /
`hold_serial_rx_callback` into the `i8051` struct and then set the statics to `NULL`. The
statics are written once, by `i8051_set_serial_*_callback()` from a driver's `MACHINE_INIT`,
before the first reset -- so the second and every later reset installed `NULL`. `[STATIC]`
The two `NULL` assignments in each function are dropped.

`hold_eram_iaddr_callback` has the same shape and is deliberately left alone: `spinb.c` is the
only driver that registers it (`spinb.c:888`) and it never resets its 8051, so changing it
would be a no-op with no way to test it.

**Nothing in the tree resets an 8051-family CPU more than once today**, which is why this is
invisible in the sweep and why the sweep after this change is identical to
`docs/findings/2026-08-31-i8051-fixed-sweep.tsv` in every field but wall-clock. `[OBSERVED]`
`cpu_pre_run()` calls `machine_init` *before* `cpunum_reset` (`cpuexec.c:364-372`), so a full
machine reset re-registers first; and no driver calls `cpu_set_reset_line()` on an 8051 --
the five that call it (`dedmd.c`, `capcoms.c`, `gts3.c`, `s11.c`, `rfranco.c`) target other
CPUs, with one exception treated next. `[STATIC]` Change 3 in this series is the first code
in the tree that resets an 8051 on a driver's say-so, and it is what makes change 1 load
bearing.

### 1.1 Correction: this is *not* a `capcoms.c` fix

The diagnosis states that `capcoms.c:641` (`capcoms_manual_reset`) pulses an 8752's reset line
and that "under this defect Capcom sound goes permanently deaf after any manual reset"
`[STATIC]`. Both halves are wrong, and the claim is withdrawn:

- `capcoms_manual_reset()` is **dead code**. `grep -rn capcoms_manual_reset src/` returns one
  hit, its own definition. Nothing in the tree calls it. `[STATIC]`
- Even if it were called, it would work. It does
  `cpu_set_reset_line(cpuNo, PULSE_LINE); capcoms_reset();`, and `capcoms_reset()`
  re-registers both callbacks (`capcoms.c:653-654`). `cpunum_set_reset_line()` only
  *schedules* the reset, via `timer_set(TIME_NOW, ...)` (`cpuexec.c:801`), so the
  re-registration always lands first. `[STATIC]`

The change is still correct and still needed -- just for `mephisto.c`, not for Capcom.

## 2. Change 2 -- the Timer 1 baud rate bit clock (`72eb7539`)

### 2.1 What it does

`update_timer()`'s Timer 1 section carried a literal `//TODO` where the UART bit clock
belongs (`i8051.c:2264` before the change). Only the Timer 2 block decremented
`uart.bits_to_send`, and that block needs `T2CON.TCLK/RCLK` on an 8052, so on a plain 8051 a
UART mode 1 transmit never finished: `bits_to_send` stuck at `8+2` forever, `update_serial()`
never reached `serial_tx_callback` and never set `TI`. `[STATIC]`

`timer1_baud_tick()` is called from both Timer 1 overflow paths -- 13/16-bit and 8-bit
auto-reload. It implements the real MCS-51 divider: baud = `(2^SMOD/32)` x Timer 1 overflow
rate, so 32 overflows per bit, 16 with `PCON.SMOD`. `GET_SMOD` is new; nothing else was.
A `TCLK`/`RCLK` guard keeps Timer 1 out of the bit clock when an 8052 has handed the serial
clock to Timer 2 -- defensive only: it is never exercised by any game in the tree (section
2.3).

This is deliberately *not* the shape the diagnosis proposed. It suggested one bit per
overflow, "16x too fast -- acceptable for a byte-level link, but say so in the comment". The
divider costs one counter byte in `I8051_UART` and is right, so it is implemented properly.
The existing Timer 2 block still spends one bit per overflow and is left exactly as it was:
every 8052 game in the tree was measured against it.

### 2.2 The sweep

Harness: `scripts/i8051_sweep.sh` from `77e06c32`, status labels corrected in `15ea573e`,
NVRAM cleared per run per `48d7f62d`. Binary `make -f makefile.unix I8051_SWEEP=1`, no
`REMOTE_DEBUG` and no `DEBUG`. 35 games, `-ftr 3600` (60 s emulated) each.

- before: `docs/findings/2026-08-31-timer1-baud-before-sweep.tsv` (tree with change 1 only)
- after: `docs/findings/2026-08-31-timer1-baud-after-sweep.tsv`

Each was run twice and each pair is identical in every field but wall-clock. The "before"
pass is also identical to the previously committed `2026-08-31-i8051-fixed-sweep.tsv`, which
confirms the harness still reproduces its own baseline on this machine. `[OBSERVED]`

**32 of 35 games are unchanged in every measured field.** The three that move:

| game | audio_hash | tf0 | riti | note |
|---|---|---|---|---|
| `sport2k` | `c065f979` -> `a61005e8` | 6 -> 6 | 6 -> 9 | 2 transmits completed where 0 did |
| `mephisto` | `5ec6e519` -> `4a817386` | 584579 -> 584116 | 1267 -> 1262 | 1 transmit completed where 0 did |
| `mephist1` | `5ec6e519` -> `4a817386` | 584579 -> 584116 | 1267 -> 1262 | same ROM path as `mephisto` |

### 2.3 Why the other 32 do not move -- measured, not argued

"Measured unchanged" is the claim the sweep supports. It can be made stronger here, and was.
With a temporary counter at the head of `timer1_baud_tick()` (reverted), and a second counter
on completed transmits, all 35 games were re-run: `[OBSERVED]`

| group | games | Timer 1 overflows in 60 s | mode-1 `SBUF` writes | transmits completed |
|---|---|---|---|---|
| Cirsa | `sport2k` | 29,982,663 | 2 | 0 -> 2 |
| Cirsa | `mephisto`, `mephist1` | 29,248,260 | 1 | 0 -> 1 |
| Capcom (`capcoms.c`) | 15 titles | **0** | 202 (4 or 7 on 3 titles) | 202, unchanged |
| Spinball (`spinb.c`) | 7 titles | **0** | **0** | ~5.18M, unchanged |
| Alvin G (`alvgdmd.c`) | 7 titles | **0** | 0 | 0 |
| Nuova (`nuova.c`) | `uboat65` | **0** | 1 | 0, unchanged |

**Timer 1 never overflows at all in 32 of the 35 games, so `timer1_baud_tick()` is never
called there.** That is a stronger statement than a matching checksum: the new code is not
merely harmless in those games, it is unreachable.

The per-group reasons hold up individually:

- **Capcom** does use UART mode 1 heavily -- 202 completed transmits per 60 s run, and 202
  `TI` interrupt dispatches -- but it clocks from Timer 2 and leaves Timer 1 stopped. It was
  already working and is untouched. `[OBSERVED]`
- **Spinball** drives its DMD from the serial port in **mode 0** (the shift-register mode,
  `fosc/12`), which has never used a timer: 5.18M completed transmits per run, zero mode-1
  `SBUF` writes. `dmd_serial_callback()` (`spinb.c:359`) is the pixel path. `[OBSERVED]` +
  `[STATIC]`
- **Alvin G** never writes `SBUF` at all in a 60 s run.
- **`uboat65`** writes `SBUF` once in mode 1 and that transmit never completes -- before *and*
  after. Timer 1 is not running and Timer 2 is not clocking the port either, so nothing feeds
  the bit clock. That is a pre-existing limitation this change does not reach and does not
  repair. `[OBSERVED]`

The divider is confirmed arithmetically by the same counters: `sport2k` ticks 640 times with
a transmit pending (2 bytes x 10 bits x 32 overflows) and `mephisto` 320 (1 x 10 x 32).
`[OBSERVED]`

### 2.4 Why the three that move, move -- and by how little

The 8051-side change is one byte, or two, actually leaving the port. What follows is second
order:

- The byte now reaches `cirsa_snd_tx()` -> `i8256_receive()`, which sets `STATUS.RBF`. The
  main CPU's boot path reads a different status once, which shifts its timing slightly.
- That re-phases the sound CPU's interrupt coalescing. `mephisto`'s `tf0` falls 0.08%
  (584,579 -> 584,116) and `riti` falls 5.

The `tf0` gap is transient, not a rate change. Measured at increasing run lengths
(`mephisto`, on/off of the bit clock in one binary): `[OBSERVED]`

| emulated seconds | 1 | 2 | 5 | 10 | 20 | 60 |
|---|---|---|---|---|---|---|
| `tf0` difference | -90 | -279 | -469 | -473 | -470 | -463 |

It accumulates over the first ~5 s and is then flat, drifting by about ten counts in 600,000
-- the two runs tick at the same rate with a slightly different phase. Over the same runs the
number of main-to-sound bytes is identical (8,192) and the sound CPU's RX-line assertions are
identical, so nothing about the traffic changed, only when it lands. `[OBSERVED]`

`sport2k` gains 3 serial dispatches (`riti` 6 -> 9), of which one is the first-ever `TI`.
Its sound ROM at `0x00A3` is `MOV SBUF,#0A5h` followed immediately by `CLR TI` at `0x00A6`
(bytes `75 99 A5 C2 99`), so it does not poll `TI` and was never blocked by the missing
clock -- it simply transmitted into a void. `[STATIC]`

Both games' audio digests move for the same reason. Neither is playing anything.

### 2.5 Residual risk

The sweep is 60 s of attract-mode boot from cleared NVRAM. It cannot prove Timer 1 stays off
later in a game. What bounds that is the shape of the change: `timer1_baud_tick()` does
nothing unless a mode 1/3 transmit is already in progress, and in that state the old core
could never complete it. The change can therefore only turn "never transmits" into
"transmits" (and, through `SET_TI(1)`, "never interrupts" into "interrupts"). It cannot alter
a transmit that already worked.

### 2.6 A pre-existing core quirk found while doing this, not fixed

`check_interrupts()` never clears `i8051.int_vec` on an early return -- only on a committed
dispatch (`i8051.c:1690`). A call that proposes a vector and then returns early (for
instance at "low priority irq in progress already") leaves both `int_vec` and
`priority_request` set, and the next call's proposals are gated on `!i8051.int_vec`, so the
stale vector can be dispatched later with none of its flags set. Measured: in a 60 s
`mephisto` run, 1,262 serial dispatches occur but only 841 of them have `RI` or `TI` set at
the moment of commit. `[OBSERVED]` This is orthogonal to everything in this document, was
present before it and is present after it, but it does mean the sweep's `riti` column is not
a clean count of serial interrupts. It is recorded here rather than fixed.

## 3. Change 3 -- `RST ASIN` wired to the 8051's reset (`86ca5350`)

### 3.1 Polarity: confirmed from the schematic, not inferred

The diagnosis rated this "moderate confidence", inferred from the ROM's pulse/release/poll
structure. It reads cleanly off both schematics, at 600 dpi. `[STATIC]`

Sport 2000 plate 11 (PDF p.34, drawing 880418.1) and the Mephisto manual's plate 9 (PDF p.29,
drawing V4P2204-68) draw the same circuit with the same reference designators:

```
                        +5V
                         |
             +-----------+-----------+
             |           |           |
            R19        T1 c        C29 1uF
            3K3      BC237          |
             |         (b)----------+---- RST (8051 pin 9)
  RST ASIN --+----------'         (e)|
  (J14.1, from MUART P24)            |
                                    R18 1K
                                     |
                                    GND
```

T1 is an **NPN emitter follower**: collector to +5V, emitter straight to the 8051's `RST`
pin, base to `RST ASIN`. R19 3K3 pulls the base up to +5V. R18 1K and C29 1uF are the
8051's ordinary power-on reset network (C from Vcc to RST, R from RST to ground).

There is **no inverter anywhere in the path**, and the 8051's `RST` is active high. The line
is therefore non-inverting: `RST ASIN` HIGH holds the sound CPU in reset, LOW releases it --
exactly the polarity the diagnosis inferred. The driver's
`cpu_set_reset_line(1, (data & 0x10) ? ASSERT_LINE : CLEAR_LINE)` is correct as written.

The 3K3 pull-up is a second, independent confirmation: it means the board sits in reset until
something drives the line low, which is why both MUART init tables leave port 2 at `0xFF` and
why the ROM's first *release* is the `AND [0A012],0EF` at `0x05F9`.

The earlier note that "the 74LS14/74LS04 inverters on the audio board are not traced" was a
false lead. Those inverters buffer `TxD`, `HSOC`, `HSIC` and `RxD`; `RST ASIN` is the one
signal in that group that bypasses them and goes to the transistor instead.

### 3.2 Verification

`REMOTE_DEBUG=1 DEBUG=1` build, `-headless -nosound -fakesound`, cleared NVRAM, temporary
`logerror` probes in `i8256.c` and `mephisto.c` (all reverted; `git status --porcelain --
src/` is clean and both files are byte-identical to their committed copies). `[OBSERVED]`

| | `sport2k` | `mephisto` | `mephist1` |
|---|---|---|---|
| `RST ASIN` asserted | t=0.000054 | t=0.000044 | t=0.000044 |
| released | t=1.045013 (PC=`05fe`) | t=0.669101 | t=0.680551 |
| 8051 sends `0xA5` | t=1.079622 | t=1.034774 | t=1.046223 |
| `STATUS` -> `0x70`, `RBF=1` | t=2.075934 (PC=`0607`) | t=1.203774 (PC=`07d9`) | t=1.215191 (PC=`0783`) |
| buffer reads `0xA5` | t=2.076137 (PC=`061a`) | t=1.203779 (PC=`07ee`) | t=1.215196 (PC=`0798`) |
| success arm | `SETINT 0x17` from PC=`0627` | | |

In each game **that `STATUS` read is the first one of the entire run**: the handshake passes
on the first poll, with no retries. `sport2k`'s failure arm (`SETINT` from PC=`0651`) is
never reached; in the unmodified tree it was the only outcome.

The link then carries the real command stream -- repeated 7-byte packets
`b0 b5 04 3d a6 9c 01`, 2.75 ms per byte, starting at t=2.111834. `[OBSERVED]`

The sound supervisor at `0xC9CA`-`0xCA9A` described in the diagnosis's section 6 still
re-pulses `RST ASIN` about every 0.72 s (PC=`ca6e`/`ca75`), each pulse producing a fresh
`0xA5` that the ROM reads at PC=`ca8d`. That behaviour is unchanged and out of scope here.

### 3.3 Regression

`scripts/probe.sh --seconds 30` exits 0 for `sport2k`, `mephisto` and `mephist1`, with
switches, lamps and segments **byte-identical** to the same build without this commit (run
both ways, ports 9700-9705). `[OBSERVED]`

That script runs `-nosound` alone, which sets `Machine->sample_rate` to 0 and suspends every
`CPU_AUDIO_CPU` (`cpuexec.c:347`) -- its own header says so. Under it the 8051 never
executes, never sends `0xA5`, and the handshake fails by design: `mephisto`'s display reads
`no AudIo` (segments 3,4 = `n`,`o`; 8-12 = `A`,`u`,`d`,`I`,`o`) both before and after this
commit. With `-fakesound`, so the sound CPU actually runs, that screen is gone and only
segment 29 is lit. `[OBSERVED]` `sport2k` shows its normal attract display either way -- its
`NO AUDIO` message is transient, so its display is not a discriminator.

## 4. The branch tip, swept

All three changes in, same harness: **the other 32 games are still identical to the committed
after-TSV in every field.** `[OBSERVED]` The three Cirsa games move again, and by much more
than change 2 moved them, because the sound CPU is now reset and re-run on cue instead of
running once and parking:

| game | audio_hash (change 2) -> (tip) | tf0 | riti |
|---|---|---|---|
| `sport2k` | `a61005e8` -> `acad37bd` | 6 -> 517,018 | 9 -> 2,335 |
| `mephisto` | `4a817386` -> `cda24615` | 584,116 -> 384,894 | 1,262 -> 703 |
| `mephist1` | `4a817386` -> `6de75fd9` | 584,116 -> 496,451 | 1,262 -> 546 |

`sport2k`'s sound CPU took 6 Timer 0 interrupts in 60 s before and takes 517,018 now: it is
running its firmware's ordinary timer ISR instead of sitting dead. `mephisto` and `mephist1`
also stop being byte-identical to each other, which they had been through every sweep in this
project so far -- once the link works, the two ROM revisions do different things.

None of this is a claim about audio.

## 5. Correction: the "89% of main-to-sound traffic is lost" figure

The diagnosis's section 6 reports 9,228 main-to-sound bytes written and 1,036 collected in one
run and attributes it to `cirsa_txd_out()` latching a single unqueued byte in
`locals.sndToSnd`. The arithmetic is right, the reading of it is not. `[OBSERVED]`

Measured on the branch tip, `sport2k`, 35 s, counting writes and collections separately either
side of t=2.1 s (the moment the command stream starts):

- **before t=2.1 s: 8,192 writes.** These are the MUART register-table copies during boot --
  `REP MOVSW` walking the 16-register table repeatedly, writing register 7 (the transmit
  buffer) with `0x00` about every 62 us, 8,000 of them inside the first 0.5 s. They are not
  commands, and until t=1.045 s the 8051 is held in reset and cannot collect anything.
- **after t=2.1 s: 1,288 writes and 1,288 collections**, byte for byte, in order, starting
  `b0 b5 04 3d a6 9c 01`. **Nothing is lost.**

The mechanism is that `cirsa_txd_out()` asserts `I8051_RX_LINE`, and `i8051_set_irq_line()`
handles that case inline -- it calls `serial_rx_callback()` and loads `SBUF` in the same
emulated instant as the write (identical timestamps in the log). Delivery is effectively
synchronous, so a byte can only be lost while the firmware has `ES` or `REN` clear, which is
precisely the boot window.

The single-byte latch is still a structural weakness with nothing guaranteeing it, and a real
queue would be more honest. But the 89% figure is a boot-time artefact, not evidence that the
command path drops nine bytes in ten, and the next task should not be planned around it.

## 6. What is not claimed

- **Sport 2000 does not produce sound.** Nothing here touches the AY-3-8910, the YM3812 or
  the DAC. Whether the AY is now programmed -- it was not before, see
  `docs/findings/2026-08-31-i8051-interrupt-fix.md` section 5 -- was not investigated.
- `uboat65`'s single stuck mode-1 transmit is untouched (section 2.3).
- The `int_vec` staleness in `check_interrupts()` is reported, not fixed (section 2.6).
- The sweep measures 60 s of attract-mode boot, not gameplay (section 2.5).
- Neither manual states the `RST ASIN` polarity in words. It is read off the schematic
  topology of both plates, which agree with each other and with the ROM's behaviour, but it is
  a reading of a 1988 scan rather than a datasheet sentence.

## 7. Commits

| commit | change |
|---|---|
| `bcf772a8` | `i8051`: let a driver's serial callbacks survive a CPU reset |
| `72eb7539` | `i8051`: implement the Timer 1 baud rate bit clock (+ both sweep TSVs) |
| `86ca5350` | `mephisto`: wire `RST ASIN` (MUART port 2 bit 4) to the 8051's reset |
