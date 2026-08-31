# Why `0x0616` still fails: the `0xA5` is never transmitted, and would be thrown away twice over if it were

Date: 2026-08-31. Repo `pinmame/`, branch `sport2000-phase0`, from `15ea573e`.
Follows `docs/findings/2026-08-31-i8051-interrupt-fix.md` (commit `781554e4`) and
`docs/findings/2026-09-01-sound-link.md` (workspace `docs/findings/`).

Evidence tags: `[OBSERVED]` = measured live on this machine in this task; `[STATIC]` =
read from ROM disassembly or source; `[INFERRED]` = reasoned from the two.

**Diagnosis task. No driver or core change is committed by this document.** All
instrumentation described below was temporary and has been reverted; `git status
--porcelain -- src/` is clean and the three files touched are byte-identical to their
pre-task copies. `scripts/probe.sh` passes for `sport2k`, `mephisto` and `mephist1`
against the rebuilt, uninstrumented binary.

## Bottom line

The leading hypothesis — that `i8256_receive()` drops the RX *interrupt* when `CMD3.RxE`
is still clear — is **chronologically true and causally irrelevant**. The handshake at
`0x0603`/`0x0616` is **polled, not interrupt driven**: it tests `STATUS.RBF`, which
`i8256_receive()` sets unconditionally. Losing the interrupt costs nothing here.

Three separate defects sit between the sound ROM's `MOV SBUF,#0A5h` and the main ROM's
`CMP [0x2A00E],0xA5`, and **all three have to be fixed for the handshake to pass**. Each
was isolated by fixing the previous one and watching the failure move:

| # | Defect | Where | Symptom once the earlier ones are fixed |
|---|---|---|---|
| 1 | The i8051 core has no Timer-1 baud-rate bit clock, so a UART-mode-1 transmit never finishes and `serial_tx_callback` is never called | `src/cpu/i8051/i8051.c:2264` (`//TODO: Timer 1 can be set as Serial Baud Rate in the 8051 only... process bits here..`) and `:2397` | `cirsa_snd_tx()` fires 0 times per boot |
| 2 | `RST ASIN` (MUART Port 2 bit 4) is not wired to the 8051's reset, so the sound CPU transmits its one `0xA5` at t≈0.03 s and never again | `src/wpc/mephisto.c:674`, `cirsa_p2_out()` is an empty stub | Byte arrives at t=0.078 s; the ROM's own `CMD3.RST` at `0x059B` (t=0.940 s) clears `STATUS.RBF`; all four polls at t=2.08…9.60 s read `RBF=0` |
| 3 | `i8051_reset()` moves the serial callbacks out of the `hold_*` statics and NULLs them, so the **second** reset of an 8051 destroys them permanently | `src/cpu/i8051/i8051.c:586` and `:2447` (`i8752_reset`) | Sound CPU is reset and re-transmits on cue, but `TX-DONE data=a5 cb=0` — the callback pointer is gone |

With all three patched as a throwaway probe, the handshake passes on the **first** poll:
`STATUS=0x70 (RBF=1)` at `PC=0x0607`, buffer reads `0xA5` at `PC=0x061A`, and the ROM
takes the success arm at `0x0622`/`0x0627` instead of falling into `0x062C`. `[OBSERVED]`
`mephisto` behaves identically under the same probe. `[OBSERVED]`

## Setup

Build `make -f makefile.unix REMOTE_DEBUG=1 DEBUG=1 -j$(nproc)`. Every capture:

```
xpinmamed.x11 -headless -startpaused -httpport <9701..9706> -nosound -fakesound \
    -skip_gamewarnings -skip_gameinfo -rompath build/roms -log <file> <game>
```

NVRAM cleared before every run, `/api/debugger/control?cmd=resume` once `/api/info`
answers, 35 s window (25 s for `mephisto`). `-nosound -fakesound` together, per
`docs/findings/2026-09-01-sound-link.md` §1 — `-nosound` alone suspends the audio CPU.

Temporary `logerror` probes (all reverted): `serial_transmit()` and the callback site in
`update_serial()` in `i8051.c`; `i8256_receive()`, `i8256_soft_reset()`, `i8256_request()`
and the `CMD3` / `SETINT` / `BUFFER` / `STATUS` cases of `i8256_w`/`i8256_r` in `i8256.c`;
`cirsa_snd_tx()`, `cirsa_txd_out()`, `cirsa_snd_rx()` and `cirsa_p2_out()` in
`mephisto.c`. `activecpu_get_pc()` reports the PC of the *next* instruction, so a log
line tagged `PC=0x05A0` was produced by the instruction at `0x059B`.

Five runs, referred to below as:

- **run 1** — unmodified tree. The state the question is about.
- **run 2** — probe fix 1 only (Timer-1 baud clock).
- **run 3** — probe fixes 1 + 2 (`cirsa_p2_out` → `cpu_set_reset_line(1, …)`).
- **run 4/5** — probe fixes 1 + 2 + 3 (`hold_*` callbacks not NULLed).
- **run 6** — `mephisto` on the run-4/5 build.

## 1. Does `cirsa_snd_tx()` fire?

**No. Zero times in a 35 s boot, on the unmodified tree.** `[OBSERVED]`
`grep -c 'SND-TX callback'` → 0, and so are `i8256_receive` (0) and
`i8256_request(I8256_INT_RX)` (0) and any read of the MUART receive buffer (0).

The sound CPU *does* try. `MOV SBUF,#0A5h` at sound-ROM `0x00A3` executes **twice** per
boot, at t=0.033968 s and t=0.077790 s: `[OBSERVED]`

```
SNDDBG t=0.033968 8051 SBUF<-a5 mode=1 SCON=50 PC=00a6 bits_left_before=0
SNDDBG t=0.077790 8051 SBUF<-a5 mode=1 SCON=50 PC=00a6 bits_left_before=10
```

`bits_left_before=10` on the second write is the whole story: `uart.bits_to_send` was
still at its initial `8+2` from the *first* write, 44 ms earlier, having never been
decremented. (It also proves the two passes are the firmware re-entering its own reset
preamble, not a CPU reset — a reset would have `memset(&uart, …)`.) `[OBSERVED]`

Why it never decrements, statically: `SCON=0x50` → `SM0=0, SM1=1` → UART mode 1, so
`serial_transmit()` (`i8051.c:2363`) sets `uart.timerbaud = 1`. `update_serial()`
(`:2391`) then does nothing at all for `timerbaud` — `//Let Timer overflow handle
removing bits` (`:2397`) — and the only timer overflow that removes bits is **Timer 2**
in baud-generator mode (`:2340`, gated on `GET_TCLK || GET_RCLK`, and this is an
`I8051`, not an 8052). Timer 1's own overflow path carries the unimplemented
`//TODO: Timer 1 can be set as Serial Baud Rate in the 8051 only... process bits here..`
at `:2264`. `uart.bits_to_send` is therefore stuck at 10 forever, `if(!uart.bits_to_send)`
is never true, and `serial_tx_callback` is unreachable. `[STATIC]`

This ROM drives its baud rate from Timer 1 in mode 2 (`MOV TCON,#50h` at `0x009A` starts
both timers; `TH1=TL1=0xFE`), which is the single most common 8051 UART configuration
there is. The path is simply not implemented.

Adding a Timer-1 bit clock as a probe (run 2) makes it fire immediately: `[OBSERVED]`

```
SNDDBG t=0.033988 8051 TX-DONE data=a5 cb=1
SNDDBG t=0.033988 SND-TX callback data=a5 sndPC=049a
SNDDBG t=0.033988 MUART RECEIVE data=a5 CMD3=00 RxE=0 inten=00
```

**Answer: no — 0 times, no data, at no point.** With the core's missing baud clock
supplied, twice per boot, `0xA5` both times, at t=0.034 s and t=0.078 s — i.e. during the
main CPU's ROM/RAM self-test, roughly 0.9 s before the main CPU reaches the handshake.

## 2. Is `CMD3.RxE` set when it fires?

**No, and it is set 0.43 s later — but this does not matter.** `[OBSERVED]`

Timeline from a single run-2 log:

| t (s) | Event | Source |
|---|---|---|
| 0.033988 | `i8256_receive(0xA5)`, `CMD3=0x00`, **RxE=0**, `inten=0x00` | 8051 `0x00A3` |
| 0.077790 | `i8256_receive(0xA5)`, `CMD3=0x00`, **RxE=0** | 8051 `0x00A3` |
| **0.509369** | `CMD3` ← `0xF0` = SET+RxE+IAE+NIE; `SETINT` ← `0x36` | main `0x04C4`, the `REP MOVSW` of "MUART table B" at `0x06B8` |
| 0.940423 | `CMD3` ← `0xF1` → **soft reset**, `status_before=0x70` **`RBF=1`** → status forced to `0x30` | main `0x059B` |
| 0.940468 | `CMD3` ← `0xF0` (RxE re-asserted, already set) | main `0x05E5` |
| 2.076138 | `STATUS` read → `0x30`, **`RBF=0`** | main `0x0603` |

So the race the hypothesis describes is real: the byte lands 0.47 s before `RxE`. But
`i8256_receive()` sets `ST_RBF` unconditionally, the handshake is polled, and
`status_before=0x70` at t=0.940423 shows `RBF` was in fact set and waiting. What destroys
it is the **ROM's own `CMD3.RST`** at `0x059B`, whose datasheet-mandated effect is "all
bits in the Status Register except bits 4 and 5 are cleared, and bits 4 and 5 are set"
(Intel 8256AH datasheet, CMD3 `RST`) — bits 4 and 5 being `TRE`/`TBE`. `i8256.c`'s
`i8256_soft_reset()` implements exactly that, correctly. `[OBSERVED]` + `[STATIC]`

The byte arrived a second too early and the ROM wiped the flag before looking at it.
Making `i8256_receive()` latch the interrupt regardless of `RxE` would change nothing.

## 3. Does `i8256_request(I8256_INT_RX)` ever run?

**On the unmodified tree, no — 0 calls.** `[OBSERVED]` Nor with fix 1 alone: the byte
arrives while `RxE` is clear, so `i8256_receive()`'s guard skips it. `[OBSERVED]`

With all three probe fixes it runs 37 times in 35 s. The first, at t=1.079004, logs
`inten=0x07 enabled=0` — level 4 is *not* in the enable mask yet, because the ROM only
adds it at `0x0622` (`OR [0x2A00A],0x10`), on the success arm, *after* the polled
handshake. Every later request logs `enabled=1`, and the main CPU does take the level-4
interrupt: `ISR_SerialRX` at `0xC994` is entered and reads the buffer
(`MUART BUFFER read -> a5 PC=c9aa`). `[OBSERVED]`

So the interrupt path works, and the ROM deliberately arms it only once the polled
handshake has succeeded. The interrupt is a consequence of the handshake, not a
precondition for it.

## 4. `0x0616` and every route to `0x062C`

`[STATIC]`, from `disasm/sport2k.lst`. MUART registers are at `0x2A000` in 8086 mode
(A0 is a second chip select), so `0xA00E` = register 7 = receive buffer, `0xA01E` =
register 15 = status, `0xA004` = CMD3, `0xA00A` = SETINT, `0xA012` = Port 2.

```
05BE:  STI
05BF:  MOV  [0000],0A5        ; stores the expected byte in NVRAM; not the MUART
05C4:  MOV  CX,0004           ; four attempts
05C7:  MOV  [0B804],06        ; <- retry loop top
05CC:  MOV  [0B805],40
05D1:  MOV  [0B800],0C3
05D6:  AND  [0A000],07        ; CMD1 read-modify-write (8086 mode + BITI must survive)
05DB:  OR   [0A000],00
05E0:  MOV  [0A002],02        ; CMD2 = 8 bit, 1 stop, no parity
05E5:  OR   [0A004],0C0       ; CMD3 |= SET|RxE      -- enable receiver
05EA:  MOV  [0A01E],02        ; STATUS(write) = modification register
05EF:  OR   [0A012],10        ; Port 2 bit 4 = RST ASIN -> HIGH
05F4:  MOV  AL,03  / CALL 0A82   ; short delay (~0.10 s observed)
05F9:  AND  [0A012],0EF       ; RST ASIN -> LOW
05FE:  MOV  AL,1E  / CALL 0A82   ; long delay (~1.03 s observed)
0603:  TEST [0A01E],40        ; STATUS.RBF ?
0608:  JNE  0616              ;   set -> go compare
060A:  MOV  AL,1E  / CALL 0A82   ; second long delay
060F:  TEST [0A01E],40        ; STATUS.RBF ?
0614:  JE   062A              ;   still clear -> give up on this attempt
0616:  CMP  [0A00E],0A5       ; <-- THE CHECK: plain byte compare, receive buffer vs 0xA5
061B:  JNE  062A              ;   wrong byte -> give up on this attempt
061D:  MOV  [06BB],0FF        ; success
0622:  OR   [0A00A],10        ;   SETINT |= level 4 (RX) -- arm the serial ISR
0627:  JMP  0651
062A:  LOOP 05C7              ; four attempts, then fall through
062C:  MOV  SI,067C           ; "   NO   AUDIO" -> display
```

**`0x0616` is a plain byte compare, nothing else.** `CMP byte ptr [0x2A00E], 0xA5`:
one operand is the MUART receive-buffer register, the other an immediate. No status bit,
no timeout, no second condition, no 16-bit or word compare. (Reading `0xA00E` clears
`RBF` as a side effect, which is why the ROM tests status first and reads the buffer
once.)

**There is exactly one route to `0x062C`: exhausting the `LOOP` at `0x062A` with `CX=4`.**
Each of the four attempts reaches `0x062A` from one of two places — `0x0614` (`RBF`
clear on *both* polls of that attempt) or `0x061B` (`RBF` set but the byte is not
`0xA5`). Nothing else in the ROM jumps to `0x062A` or `0x062C`; the byte-level
CALL/JMP/Jcc scan already done for the `0x076D` correction (see `CLAUDE.md`) covers this
region. In every unmodified run, all four attempts take the `0x0614` arm — `RBF` is
never set, and `0x0616` is never executed at all. `[OBSERVED]`, 8 status reads
(`0x0607`/`0x0613`, i.e. the two polls × four attempts) at t=2.076, 3.107, 4.242, 5.274,
6.407, 7.438, 8.574, 9.605 s, all returning `0x30`, and 0 buffer reads.

Which arm was taken is directly observable in the log without a breakpoint, because the
two arms write `SETINT` from different addresses: `PC=0x0627` is the success arm's
`0x0622`, `PC=0x0651` is the failure arm's `0x064C`. Runs 1-3 log
`SETINT data=17 PC=0651` at t=16.444669; runs 4-5 log `SETINT data=17 PC=0627` at
t=2.076141. `[OBSERVED]`

Note the ROM's structure: `OR [0A012],10` … delay … `AND [0A012],0EF` … long delay …
poll. That is *pulse the sound board's reset, wait for it to boot, catch its power-on
`0xA5`*. Port 2 is `0xFF` in both MUART init tables (`0x0698` "everything off" and
`0x06B8` "go live"), so `RST ASIN` idles **high** from power-on and the `AND` at `0x05F9`
is the first *release*, at t=1.045013 s. `[STATIC]` + `[OBSERVED]`

## 5. The first divergence between what the ROM expects and what it gets

**The sound ROM's `MOV SBUF,#0A5h` at `0x00A3` never completes a transmission**, because
PinMAME's shared i8051 core has no Timer-1 baud-rate bit clock (`i8051.c:2264`). Nothing
downstream ever sees the byte. That is the first thing that goes wrong, at t=0.033968 s,
about 0.9 s before the main CPU reaches the handshake. `[OBSERVED]` + `[STATIC]`

Fixing only that moves the divergence, it does not remove it — this is the part the
task's framing did not anticipate, and it is why "one fix" would have looked like a
regression. Run 2, with the byte delivered:

- the `0xA5` lands at t=0.078 s, **0.86 s before** the ROM's own `CMD3.RST` at `0x059B`,
  which clears `STATUS.RBF` exactly as the datasheet requires;
- the sound CPU sends `0xA5` **only in its reset preamble**, so there is no second copy;
- `cirsa_p2_out()` is an empty stub, so the four `RST ASIN` pulses the ROM issues at
  t=1.045, 3.107, 5.274 and 7.438 s reset nothing and produce no new `0xA5`;
- all four attempts poll `RBF=0` and the machine displays `NO AUDIO`, identically to run 1.

Wiring `RST ASIN` (run 3) moves it again: the sound CPU now reboots on cue and reaches
`MOV SBUF,#0A5h` 34 ms after each release — but the log reads `TX-DONE data=a5 cb=0`.
`i8051_reset()` does `memset(&i8051, …)`, then `i8051.serial_tx_callback =
hold_serial_tx_callback; hold_serial_tx_callback = NULL;` (`i8051.c:585-588`, and the
identical block in `i8752_reset()` at `:2446-2449`). The `hold_*` statics are written
once, from `MACHINE_INIT`, before the first reset. **Every reset after the first
therefore installs `NULL`.** `[OBSERVED]` + `[STATIC]`

This one is not Sport 2000-specific. `capcoms.c:641` (`capcoms_manual_reset`) pulses the
reset line of an `I8752` whose serial callbacks are registered at `capcoms.c:653-654`;
under this defect Capcom sound goes permanently deaf after any manual reset. `[STATIC]`

With all three fixed (runs 4/5), the first poll succeeds:

```
SNDDBG t=1.045013 MAIN P24 RST_ASIN=0 PC=05fe          <- reset released
SNDDBG t=1.078984 8051 SBUF<-a5 mode=1 SCON=50 PC=00a6 <- 34 ms later
SNDDBG t=1.079004 SND-TX callback data=a5 sndPC=049a
SNDDBG t=1.079004 MUART RECEIVE data=a5 CMD3=70 RxE=1 inten=07
SNDDBG t=2.075932 MUART STATUS read -> 70 (RBF=1) PC=0607   <- 0x0603 passes
SNDDBG t=2.075938 MUART BUFFER read -> a5 PC=061a RBF_was=1 <- 0x0616 passes
SNDDBG t=2.076141 MUART SETINT data=17 PC=0627              <- success arm
```

and the link then carries real traffic in both directions — the main CPU starts sending
7-byte command packets (`b0 b5 04 3d a6 9c 01`) from `0xCBBD`/`0xCB61` and the sound CPU
receives them. `[OBSERVED]` `mephisto` (run 6) does the same thing at its own addresses:
`RST ASIN` released at t=0.669, `0xA5` at t=1.034, buffer read at `PC=0x07EE` t=1.204.
`[OBSERVED]`

## 6. Loose ends this explanation does *not* cover

**The handshake succeeds and then keeps being redone, every ~0.7 s, 37 times in 35 s.**
`[OBSERVED]` This is the ROM's sound-board supervisor at `0xC9CA`-`0xCA9A` `[STATIC]`:
it watches activity flags `0x6C0`/`0x6C1`/`0x6BC`, counts down `0x6F7`, increments a
strike counter `0x6F9`, and at four strikes (`0xCA14`) runs the full re-init +
`RST ASIN` pulse + `CMP [0x2A00E],0xA5` sequence at `0xCA1C`-`0xCA8E`, clearing `0x6F9`
and `0x6FA` on success. It is succeeding each time and then re-arming, which means the
sound board is never satisfying the activity flags — i.e. the 8051 transmits its power-on
`0xA5` and then nothing else, ever (all 37 `SND-TX callback` events carry `0xA5`).

The most likely reason is visible in the same log and is a *driver* limitation, not a ROM
one: `cirsa_txd_out()` stores into a single `locals.sndToSnd` byte with no queue, and
run 5 counts **9,228 main→sound bytes written but only 1,036 collected** by
`cirsa_snd_rx()` — 89% of the command stream is overwritten before the 8051 reads it.
`[OBSERVED]` A sound CPU that receives one byte in nine cannot parse a 7-byte packet.
That is a distinct problem from this one and should be its own task; nothing in this
document depends on it.

Two smaller ones, both flagged rather than resolved:

- **`RST ASIN` polarity is inferred, not documented.** Plate 5 and Plate 11 of the manual
  name the signal at both ends (`J14.1`) but give no polarity, and the 74LS14/74LS04
  inverters on the audio board are not traced. Active-high straight onto the 8051's own
  active-high `RST` pin is what makes the ROM's pulse/release/poll sequence work, in both
  games, and matches the idiom in `rfranco.c:706` (which uses the opposite polarity for
  its own board). It should be checked against the PDF schematic before it is committed.
- **The sound firmware re-enters its reset preamble once at power-on** (two `0x00A3`
  executions, 44 ms apart, with no CPU reset in between). Unexplained; harmless for this
  question, since both passes send the same byte.

## 7. Recommendation

**Three changes, in this order, each verified before the next.** The diagnosis is not
inconclusive — the causal chain is closed end to end, and the probe demonstrates the
handshake passing in both games — but the first change is in shared core code and must be
swept, not assumed.

1. **`src/cpu/i8051/i8051.c`: preserve the serial callbacks across reset.** Drop the two
   `hold_serial_tx_callback = NULL; hold_serial_rx_callback = NULL;` lines at `:586` and
   `:2447` (the `hold_eram_iaddr_callback` line next to them has the same shape and should
   be looked at at the same time). This is the smallest and safest of the three: it cannot
   change behaviour for any CPU that is reset exactly once, and it is a genuine bug fix
   for `capcoms.c` as well. Do this first so it is not entangled with the rest.

2. **`src/cpu/i8051/i8051.c`: implement the Timer-1 baud-rate bit clock.** In
   `update_timer()`'s Timer 1 section, decrement `uart.bits_to_send` on overflow when
   `uart.sending && uart.timerbaud`, mirroring the existing Timer 2 block at `:2340`. The
   probe put it in the mode-2 (8-bit auto-reload) case, which is what both these ROMs use;
   modes 0/1 carry the same TODO at `:2264` and should be handled too. **This is the one
   with blast radius.** It makes `serial_tx_callback` reachable — and, through
   `SET_TI(1)`, the serial *interrupt* reachable — for every 8051 in the tree whose
   firmware writes `SBUF` in UART mode 1 with a Timer-1 baud rate. The two to watch first
   are the ones that register serial callbacks at all: `spinb.c`'s DMD CPU
   (`spinb.c:890`) and `capcoms.c`'s 8752 (`capcoms.c:653-654`). Run it through the
   35-game sweep harness
   from commit `77e06c32` with NVRAM cleared per run (`48d7f62d`), the same way the
   `check_interrupts()` fix was adjudicated, and expect and explain any game that moves.
   A per-overflow decrement is 1 bit per overflow and ignores the `SMOD`/16 divider, i.e.
   16× too fast — acceptable for a byte-level link, but say so in the comment.

3. **`src/wpc/mephisto.c`: wire `RST ASIN`.** `cirsa_p2_out()` gains
   `cpu_set_reset_line(1, (data & 0x10) ? ASSERT_LINE : CLEAR_LINE);` — edge-triggered on
   change, as the probe did, so it is not re-asserted on every Port 2 write. Confirm the
   polarity against Plate 11 in the PDF first (§6). This is shared by `mephisto`,
   `mephist1` and `sport2k`, and all three must be re-probed.

Do **not** change `i8256_receive()` to request the interrupt unconditionally, and do
**not** make `i8256_soft_reset()` preserve `ST_RBF`. Both would paper over the real
problem, and the second contradicts the datasheet.

**Confidence: high** for the mechanism (each of the three defects was isolated by fixing
its predecessor and watching the failure move to the next one, in both games), **and for
changes 1 and 2 as the correct fixes**. **Moderate for change 3's polarity**, which rests
on the ROM's pulse/release/poll structure and on both games behaving correctly under it,
not on the schematic. Nothing here claims Sport 2000 will *make sound* once these land —
§6's 89% byte-loss on the main→sound path says it will not, and that is the next task.
