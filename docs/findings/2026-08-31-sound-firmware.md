# Why the sound board takes the packets and stays silent: a handshake line that is
# not wired, an AY-3-8910 on the wrong bus, and `MOVX @Ri` with no P2

Date: 2026-08-31. Repo `pinmame/`, branch `sport2000-phase0`, from `365da015`.
Follows `docs/findings/2026-08-31-sound-handshake.md` (`d1b426e4`) and
`docs/findings/2026-08-31-sound-link-fixes.md` (`bfccf20e`), which made the link work,
and `365da015`, which mapped the YM3812.

Evidence tags: `[OBSERVED]` = measured live on this machine in this task; `[STATIC]` =
read from ROM disassembly, driver source or the manual; `[INFERRED]` = reasoned from the
two.

**Diagnosis task. No driver or core change is committed by this document.** All
instrumentation was temporary and has been reverted: `git status --porcelain -- src/` is
empty and `src/cpu/i8051/i8051.c` and `src/wpc/mephisto.c` are byte-identical (md5
`b83592af…`, `2ea3cfe6…`) to their pre-task copies. The tree was rebuilt from the reverted
sources and `sport2k`, `mephisto` and `mephist1` all still boot and run.

## Bottom line

**The sound CPU never executes a single command.** It receives every packet correctly and
validates every checksum, and then hangs — permanently, 0.6 ms later — in the routine that
transmits its `0xB0` acknowledgement, spinning on an input handshake line the driver does
not drive. `[OBSERVED]` The main CPU's sound supervisor sees no activity, strikes out and
pulses `RST ASIN`; the board reboots about every 0.74 s and does the same thing again.
Every OPL2 register write in the log is one boot-time silence sweep per reboot.

Three defects, all in `src/wpc/mephisto.c`, all shared by `mephisto` and `mephist1`:

| # | Defect | Effect |
|---|---|---|
| 1 | The 8051's P3.2 (INT0) input is not driven from MUART Port 1 bit 6, and its P3.3 (INT1) output is not fed back to MUART Port 1 bit 5. `cirsa_p1_out()` is an empty stub, `cirsa_p1_in()` returns 0, and the sound CPU's port 3 reads fall through to `port_r`, which returns 0. | The 8051 hangs at `0x0250` on the first byte it tries to send. **Nothing else happens.** |
| 2 | The AY-3-8910 is wired to the wrong pins. On the real board P1 is the AY's 8-bit data bus and P3.4/P3.5 are BDIR/BC1; the driver maps i8051 *port 1* to `AY8910_control_port_0_w` and *port 3* to `AY8910_write_port_0_w`. | The AY receives the four P3 handshake states (`c7 cf df ff`) as data and never a music byte. There is no read path at all, which `mephisto` needs. |
| 3 | `MOVX @Ri` supplies only the low 8 bits of the address — the driver registers no `i8051_set_eram_iaddr_callback`, so P2 (the high byte) is dropped. | Every one of the firmware's paged XRAM accesses lands on page 0. This is what makes the OPL2 sweep write `00`/`ff` instead of the intended values, and it scribbles OPL2 register data over the serial packet buffer. |

Fix #1 alone and the firmware starts running: parsed commands go from **0 to 180** and AY
register writes from 184 to 2,832 in a 40 s attract run. `[OBSERVED]` Fix #2 as well and
what those writes carry is the actual attract chime, which is byte-for-byte identical to
the score data in the ROM. `[OBSERVED]` + `[STATIC]`

## Setup

`make -f makefile.unix REMOTE_DEBUG=1 DEBUG=1`. Every capture:

```
xpinmamed.x11 -headless -nosound -fakesound -skip_gamewarnings -skip_gameinfo \
    -rompath build/roms -log <file> -ftr 2400 <game>
```

`-nosound -fakesound` together — `-nosound` alone suspends the audio CPU
(`cpuexec.c:347`). NVRAM cleared before every run. `-ftr 2400` = 40 s emulated.

Temporary probes (all reverted): a landmark-PC counter called from `i8051_execute()`'s
instruction loop, and wrappers on `bank_w`, `0x10F00`, the YM3812 ports and the sound
CPU's port 1 / port 3 handlers, all in `mephisto.c`. One probe wrapper is switchable by
environment variable, `CIRSA_HS=1`, which forces the sound CPU's P3.2 input high — the
line the main CPU is supposed to drive. Two configurations are quoted throughout:

- **HS0** — the tree exactly as committed. Final counter dump at t=34.6488 s.
- **HS1** — identical binary, `CIRSA_HS=1`. Final counter dump at t=39.7013 s.

Both are the same 2,400-frame run; the dump timestamps differ only because the dump is
triggered off an instruction counter. Rates are normalised where the two are compared.

The 8051's own disassembly comes from `roms/c541_256.bin` (`ic15_02` for Mephisto),
disassembled with a purpose-written MCS-51 disassembler and cross-checked against
`dasm51`.

## 0. The map this rests on

The sound 8051 sees (`cirsa_readsnd`/`cirsa_writesnd`, `DATAMEM_R(a) = cpu_readmem20(a +
0x10000)` in `i8051.c:231`):

| 8051 address | driver address | device |
|---|---|---|
| code `0x0000-0x7FFF` | `0x00000-0x07FFF` | IC15 27256, the firmware |
| code `0x8000-0xFFFF` | `0x08000-0x0FFFF` | the banked sample window, `MRA_BANKNO(1)` |
| xdata `0x0000-0x07FF` | `0x10000-0x107FF` | IC10 6116, 2K RAM |
| xdata `0x0800` | `0x10800` | ROM bank latch (`bank_w`) |
| xdata `0x0F00` | `0x10F00` | **also the ROM bank latch** — section 5 |
| xdata `0x1000` | `0x11000` | DAC-08 latch |
| xdata `0x1800/0x1801` | `0x11800/1` | YM3812 address/data |
| P1 + P3.4/P3.5 | i8051 ports | AY-3-8910 data bus + BDIR/BC1 — section 7 |
| P3.2 in, P3.3 out | i8051 port 3 | the two handshake lines — section 6 |

Interrupt vectors: reset `0x0026`, INT0/INT1/T1 all `0x04A1` (which is `LJMP 0x00BD`, back
to the main loop), Timer 0 `0x0C16`, serial `0x0375`. `[STATIC]`

## 1. Does the serial ISR consume the packets, and where does it put them?

**Yes, completely and correctly.** `[OBSERVED]` + `[STATIC]`

`0x0375` is the serial ISR. It tests `TI` first; on `RI` it calls the receiver at `0x03A6`
and clears `RI`. `0x03A6` is a three-state machine whose state lives in **XRAM `0x0005`**:

| state | address | what it does |
|---|---|---|
| 0 (`0x03C2`) | — | accepts only `0xB5` or `0xB6`; stores it at **XRAM `0x0009`**, state ← 1. Anything else resets state ← 0. |
| 1 (`0x03EE`) | — | stores the length byte at **XRAM `0x000A`** *and* at **XRAM `0x0008`** (the countdown), sets the write pointer at **XRAM `0x0006`/`0x0007`** to `0x000B`, state ← 2. |
| 2 (`0x0411`) | — | stores the byte at the pointer, advances it, decrements the countdown; at zero sets flag **`20h.4`** and state ← 0. |

The first thing `0x03A6` does is `JNB 20h.4` — **if `20h.4` is still set from an unconsumed
packet, the byte is discarded at `0x03A9`.** That is the entire fate of the command stream
today.

Measured, HS0, 34.65 s: `[OBSERVED]`

```
serial-isr = 1313  =  rx-byte 322  +  rx-dropped 945  +  tx-done-isr 46
```

The accounting is exact. 322 = 46 boots x 7 bytes: the frame is
`b0 b5 04 3d a6 9c 01`, and `0xB0` is consumed and rejected by state 0 (`rx-hdr` fires
92 = 46 x 2 times, once for the `b0` and once for the `b5`). `rx-len` = 46, `rx-data` =
184 = 46 x 4, `pkt-done` = 46. **Exactly one packet is stored per boot, byte for byte;
the other 945 bytes are dropped at `0x03A9` because `20h.4` never clears.**

## 2. Does the firmware parse a packet into a command?

**It knows how to, and it never gets there.** `[OBSERVED]` + `[STATIC]`

The frame decodes as `type=0xB5, len=0x04, payload 0x3D 0xA6, checksum 0x9C 0x01`. The
checksum routine at `0x0204` sums `len` bytes starting at XRAM `0x0009` into a 16-bit
value and compares it with the two bytes at `0x0009+len`:
`0xB5 + 0x04 + 0x3D + 0xA6 = 0x019C`. `[STATIC]` It passes: `checksum` = 46, `ACK-B0` =
46, and the failure arm at `0x00E1` (`NAK-A0`) fires **zero** times in the whole run.
`[OBSERVED]`

The dispatcher is at `0x0107`. `R1` = `len - 2` payload bytes are walked and split by
value: `< 0xA0` is queued into a 10-deep FIFO at IRAM `0x2D..0x36` by `0x025B`;
`0xA0-0xBF` are immediate commands (`0xA2`, `0xA3`, `0xA6`; `0xA4` is unreachable — the
test is `ANL A,#0F0h` then `CJNE A,#0A4h`, which can never be equal); anything else falls
into the fault handler.

So `b0 b5 04 3d a6 9c 01` *should* mean: **queue sound command `0x3D`, then send a status
reply**. `0xA6` at `0x016B` builds a six-byte packet headed `0xA7` at XRAM `0x0036` with
its own checksum and transmits it.

The 100 Hz tick (`0x0450`, called from the Timer 0 ISR every 100 ticks) pops the FIFO at
`0x0269` and dispatches by range. `0x3D` lands in the `0x20-0x3F` arm at `0x02D2`: it
looks the priority up in the table at `0x0F7B` (`0x0F7B[0x1D] = 4`), compares it against
the currently playing priority in `38h`, calls `0x0E71` to stop what is playing and
`0x0E62` to select sound `0x3D - 0x20 = 0x1D`. The sequencer at `0x0E7F` then reads the
score pointer from the table at `0x0F9B` (`entry[0x1D] = 0x3315`) and plays it.
`[STATIC]`

**None of this executes.** In HS0 the counters for `0x00F8` (dispatch), `0x0107`
(cmdloop), `0x0118` (queue), `0x0166`/`0x016B` (the `0xA6` arm), `0x02D2` and `0x0E62` are
all **zero**. `[OBSERVED]` The reason is section 6: the `0xB0` acknowledgement at
`0x00E6`, which is sent *before* the dispatcher runs, never completes.

With `CIRSA_HS=1` every one of them fires: `dispatch=180  cmdloop=360  queue-cmd=180
cmd-A6=180  cmd-20+=180  start-seq=180  snd-set=180`. `[OBSERVED]` 360 = 180 x 2 payload
bytes; the numbers are internally consistent to the byte.

## 3. Why only `0x00` and `0xFF` to the OPL2?

Two separate reasons, and neither is "it is stuck in an init loop".

**The sweep is the ROM's ordinary FM silence routine, run once per reboot.** `0x0626`
walks a 123-entry register list at `0x0645` (`01 02 03 04 08 20…25 28…2D 30…35 40… 60…
80… A0…A8 B0…B8 BD C0…C8 E0…E5 E8…ED F0…F5`) and writes each register from a shadow copy
of the OPL2 register file. Its only caller is `0x05EA`, which first zeroes the shadow;
`0x05EA`'s callers are `0x0354` ("stop everything") and `0x0303`. `0x0354` runs once from
the boot preamble at `0x00B7`. `[STATIC]`

The arithmetic settles it. HS0: **5,658 OPL2 data writes, `00` x 5,566 and `ff` x 92**,
and 46 reboots. `[OBSERVED]` 46 x 123 = 5,658 exactly, and 92 = 46 x 2. **Every OPL2
write in the run is a boot silence sweep, one per reboot, and there are no others.** The
board is rebooting (section 6), not looping. HS1 gives 2,952 = 24 x 123 with `ff` x 48 =
24 x 2, from 24 reboots — same identity, different reboot count. The figure quoted in
`365da015`'s commit message, `0x00` x 5,203 and `0xFF` x 86 over a 40 s run, factorises the
same way and was measured before this task: 5,203 + 86 = 5,289 = 43 x 123, and 86 = 43 x 2.
`[OBSERVED]`

**The values are wrong because of defect 3.** `0x0626` reads the shadow with
`MOV 26h,#02h / MOV P2,#02h / … / MOVX A,@R1`, i.e. XRAM `0x0200 + reg`. PinMAME's
`MOVX @Ri` builds its address in `external_ram_iaddr()` (`i8051.c:1991`), which needs an
`eram_iaddr_callback` to supply the upper bits from P2 and, when none is registered,
**returns the bare 8-bit offset**. `mephisto.c` registers none — `spinb.c:888` is the only
driver in the tree that does. `[STATIC]` So the sweep reads XRAM `0x0000-0x00FF` instead
of `0x0200-0x02FF`.

That page is zero after the RAM test, except for `0x0029` and `0x002A`, which `0x0354`
sets to `0xFF` at `0x0364`/`0x036D` immediately before calling `0x05EA`. Registers `0x29`
and `0x2A` are both in the table. **Two `0xFF` per sweep, everything else `0x00` — which
is exactly the observed histogram.** `[OBSERVED]` + `[STATIC]` The two values the routine
*means* to write, `reg 0x01 <- 0x20` (wave select enable, set at `0x0602`) and
`reg 0xBD <- 0xC0` (set at `0x060E`), are lost.

So the answer to "init path, fault path, or no-sound-selected default" is **none of the
three**: it is the normal power-on silence sweep, executed once per boot, and the board is
being rebooted about once a second.

## 4. Does it bank its sound ROMs?

**Yes — on every single PCM sample byte. But the bank number is always `0x00`.**
`[OBSERVED]`

The Timer 0 ISR (`0x0C16`, ~8.5 kHz) writes the DAC from `45h` and then services two
independent sample streams, each with its own bank byte, 16-bit ROM pointer and page
count in register bank 3:

```
0C2C: DEC DPH          ; DPTR 0x1000 -> 0x0F00
0C2E: MOV A,R3         ; stream 1 bank
0C2F: MOVX @DPTR,A
0C30: MOV DPH,R1       ; pointer high, always >= 0x80
0C33: MOVC A,@A+DPTR   ; the sample byte
...
0C42: MOV DPH,#08h     ; DPTR = 0x0800
0C45: MOV A,R7         ; stream 2 bank
0C46: MOVX @DPTR,A
0C4A: MOVC A,@A+DPTR
```

HS0: `bank` (0x10800) = 35,328 writes and `f00` (0x10F00) = 35,328 writes in 34.65 s, all
`0x00`. HS1: 18,432 and 85,933, all `0x00`. `[OBSERVED]` 35,328 = 46 x 768 = 46 x 3 pages
x 256, i.e. one 768-byte block per reboot on each stream.

The bank byte comes from a three-byte descriptor at `0x5624 + 3n` (`0x0CAE`): start page,
page count, bank. `[STATIC]` It is `0` for the only sound the machine plays in attract, so
`bank_w`'s `data * 0x8000` arithmetic is **exercised but never with a non-zero value** —
untested, not unused. A probe that would test it: send a command in the `0x60-0xBF` range
(the sample commands, dispatched at `0x02B1` to `0x0D9F`) and watch `bankval[]`.

## 5. What is `0x10F00`?

**It is the sound-ROM bank latch again — a second address for the same register.**
`[INFERRED]`, from a static argument that leaves no room:

- The 8051's code space is 64K. `0x0000-0x7FFF` is the firmware EPROM. `0x8000-0xFFFF` is
  therefore the *only* banked window, and both streams read from it: the descriptor's
  start page is loaded as `table[0] + 0x80` (`0x0CC2`, `0x0CDA`), so both pointers are
  always in `0x80xx-0xFFxx`. `[STATIC]`
- Two bank latches cannot drive one window. Both addresses are written with a complete
  bank number taken from the same descriptor field, immediately before that stream's own
  `MOVC`. Writing the bank per access is what makes a single shared latch correct.
- The schematic (plate 11, PDF p.34) has a 74LS373 latch feeding IC22, a 74LS138 whose
  Y0…Y7 are `CST0…CST7`, the chip selects of the sound EPROMs, plus IC23 (74LS138,
  `CS1…CS3`) and IC24 (74LS139) doing the address decode. An incomplete decode that
  ignores A8-A10 inside the `0x0800-0x0FFF` block would alias the latch across the whole
  block, `0x0800` and `0x0F00` included. `[STATIC]`
- Mephisto's firmware writes only `0x0800`, which is why `mephisto` makes no unmapped
  accesses. `[STATIC]`

**Recommended wiring: `{ 0x10f00, 0x10f00, bank_w }`.** The one observation that would
falsify this is the two streams running simultaneously from *different* banks — that could
not work on real hardware with one latch. It does not happen here: a counter on
`0x10F00` writes taken while stream 2 was also active recorded `bothPCM = 35,144` samples
with `bankdiff = 0` (HS0) and `18,336` / `0` (HS1). `[OBSERVED]` The test is inconclusive
only because both banks are `0` throughout; the probe in section 4 would settle it
properly.

## 6. The first thing the firmware needs and is not getting

**The P3.2 (INT0) handshake input, which the main CPU drives from MUART Port 1 bit 6.**
`[OBSERVED]` + `[STATIC]`

The 8051's byte transmitter is five instructions long:

```
024E: SETB P3.2        ; release the pin
0250: LCALL 024D       ; (a bare RET -- a delay)
0253: JNB  P3.2, 0250h ; wait until the line is driven HIGH
0256: CLR  21h.0
0258: MOV  SBUF,A
```

`JNB` on a port bit reads the **pin**, not the latch (`jnb()` in `i8051ops.c:419-427` does not
set `RWM`, so `sfr_read(P3)` takes the `IN(3)` arm at `i8051.c:1916-1920`). `IN(3)` reaches
`cirsa_readsndport`, where port 3 falls through to `port_r`, **which returns 0**.
`[STATIC]` The bit is never set, so the loop never exits.

Measured: `txwait` (`0x0250`) is entered **1,475,198** times in 34.65 s and the port 3 read
count is **1,475,167**. `tx-go` (`0x0256`, one instruction later) fires **zero** times.
`[OBSERVED]` The sound CPU spends essentially its entire life in that three-instruction
loop, from 0.6 ms after the first packet until the main CPU resets it. The clean,
uninstrumented build has been saying so all along: a 15 s `sport2k` log contains
**589,181** `SND PORT 3 READ` lines from `port_r`'s own `logerror`. `[OBSERVED]`

The other end is in the main ROM and is explicit:

```
CB41: TEST [0A010], 0020   ; MUART Port 1 bit 5 -- the sound board's BUSY line
CB47: JNE  0CB53           ;   busy -> do not send
CB49: TEST [0A01E], 0020   ; MUART STATUS bit 5 = TBE
CB4F: JE   0CB53
CB54: ...  MOV [0A00E],AX  ; send the next command byte
```

and, in the frame routine at `0x08E3`,

```
08E3: MOV AL,[071C] / OR AL,[0A01E] / AND AL,040 / JNE 008F3
08EE: OR  [0A010], 040     ; Port 1 bit 6 HIGH -- "I am ready to receive"
0915: AND [0A010], 0BF     ; and low again
```

with `ISR_SerialRX` at `0xC994` clearing the same bit on every byte received
(`C9A1: AND [0A010],0BF` — already annotated as such in `disasm/sport2k.lst`). `[STATIC]`
`PORT1C = 0x5C` makes bit 6 an output and bit 5 an input, which matches
(`i8256_port1_read()`/`i8256_port1_write()` already mask by `R_PORT1C`). `[STATIC]`

So the pair is:

| line | driver | receiver | meaning |
|---|---|---|---|
| MUART P1 bit 6 (out) | `cirsa_p1_out()` — **empty stub** | 8051 P3.2 (INT0) | main CPU ready for a byte |
| 8051 P3.3 (INT1, out) | 8051 `MOV P3.3,C` at `0x00C7`, `0x00D8`, `0x0179`, `0x01D0`, `0x03A1`, `0x046A` | MUART P1 bit 5 — `cirsa_p1_in()` **returns 0** | sound board busy |

Neither is connected. The second direction currently fails safe (P15 reads low = "not
busy", so the main CPU always transmits, which is why delivery works at all); the first
is the hang.

**The probe.** Forcing the 8051's P3.2 read to return `0x04`, changing nothing else:

| counter | HS0 (34.65 s) | HS1 (39.70 s) |
|---|---|---|
| `txwait` entered / `tx-go` reached | 1,475,198 / **0** | 928 / **928** |
| packets dispatched (`0x00F8`) | **0** | **180** |
| commands queued (`0x0118`) | **0** | **180** |
| sequencer starts (`0x02F3`) | **0** | **180** |
| AY register writes (`0x0F40`) | 184 | **2,832** |
| 8051 resets by `RST ASIN` | 47 | 25 |
| OPL2 writes | 5,658 (46 sweeps) | 2,952 (24 sweeps) |

`[OBSERVED]` Every wait completes, every packet is parsed, and the reboot rate halves.
That is the blocker.

Mephisto's sound ROM has the identical routine at `0x0222`/`0x0227`, so the same fix
serves it — though in a 30 s `mephisto` attract run the port 3 read count is **0**, i.e.
its firmware does not reach its transmitter in that window and the change is a no-op there
today. `[OBSERVED]`

## 7. What actually plays once it is unblocked — and why it still would not

**The AY-3-8910 is on the wrong bus.** `[STATIC]`, from an unambiguous ROM idiom, and
`[OBSERVED]`.

The sequencer's output primitive (`0x0F40`, and Mephisto's identical `0x0862`) is:

```
0F40: MOV P1,R1              ; register number (0..0x0D)
0F42: CLR EA
0F44: ORL P3,#30h            ; P3.5 + P3.4 high
0F47: ANL P3,#CFh            ; both low
0F4A: SETB EA
0F4C: MOV A,R3               ; the data byte
0F4D: CJNE R1,#07h,0F54h
0F50: ANL A,#3Fh / ORL A,#40h  ; register 7: force IOA to output
0F54: MOV P1,A
0F58: ORL P3,#10h            ; P3.4 high only
0F5B: ANL P3,#CFh
```

That is the AY-3-8910's BDIR/BC1 protocol verbatim, with **P1 as the data bus, P3.4 =
BDIR and P3.5 = BC1**: both high = latch address, BDIR alone = write data, both low =
inactive. The special case for register 7 — clear bit 7, set bit 6, i.e. IOA output, IOB
input — is the AY's mixer/IO-direction register and nothing else. Mephisto removes the
last doubt by using the read case as well:

```
05A6: MOV P1,#0Fh   ; AY register 15 = port B
05A9: ORL P3,#30h / ANL P3,#CFh
05AF: MOV P1,#FFh   ; release the bus
05B2: ORL P3,#20h   ; BC1 alone = READ DATA
05B5: MOV 44h,P1    ; read it
05B8: ANL P3,#CFh
```

The driver maps i8051 **port 1** to `AY8910_control_port_0_w` and **port 3** to
`AY8910_write_port_0_w`. So `MOV P1,#07` happens to latch register 7 and then
`ORL P3,#30h` writes the *P3 pin state* into it. Measured: in 34.65 s the AY receives
12,370 data writes carrying exactly four distinct values — `c7`, `cf`, `df`, `ff` — the
four states of the handshake port. `[OBSERVED]` There is no read handler for port 3 at
all, and `AY8910_read_port_0_r` is bound to port 1, where nothing reads it.

Decoding the BDIR/BC1 protocol in the probe shows what the firmware *means* to write.
The boot silence sequence (sound #3, score at `0x279E`) intends
`r7<-7F, r8<-00, r9<-00, rA<-00`, then end-of-sequence — and the ROM bytes at `0x279E` are
`07 3F 08 00 09 00 0A 00 0F 0F`, which is that exactly. `[OBSERVED]` + `[STATIC]`

With the handshake forced, command `0x3D` reaches the sequencer and the intended stream
becomes:

```
r8<-10 r9<-10 r1<-00 r3<-00 rC<-02 rB<-00 r7<-7C r0<-C2 r2<-C2 rD<-00 r0<-F4 r2<-F4 rD<-00
```

and the score at `0x3315` reads
`08 10 09 10 01 00 03 00 0C 02 0B 00 07 FC 00 C2 02 C2 4D 00 00 F4 02 F4`. `[OBSERVED]` +
`[STATIC]` Byte for byte the same thing: channels A and B at volume 0x10, tone A and B
enabled (`0xFC & 0x3F | 0x40 = 0x7C`), two notes, envelope period 2. **Sport 2000's
attract chime is a two-tone AY figure, and the emulator is sending it to the wrong pins.**

Mephisto also drives AY register 14 (port A) with a walking bit — `rE <- 10, 20, 40, 80,
08, …` — which is the audio board's own switch matrix column strobe (plate 11 lists
`T COLUMN 1…5`, `T ROW 1…3`, `A COLUMN 3/4/5`, `COIN 1/2/3` on J17-J20). `[OBSERVED]` +
`[STATIC]` `ay8910_porta_r/w` and `ay8910_portb_r/w` in the driver currently read
`swMatrix[0..1]` and write `tmpLampMatrix[0..1]`, which are MAME placeholders and almost
certainly wrong for both machines. Out of scope here, flagged.

## 8. The third defect, and the trap in fixing it

`MOVX @Ri` needs P2 for its high address byte. The firmware relies on it heavily and sets
P2 explicitly every time (it even keeps a shadow of P2 in `26h` and restores it):
page 0 for the packet buffer (`0x010B`, `0x0186`), **page 1** for the FM voice state
(`0x0545`), **page 2** for the OPL2 register shadow (`0x0967`, `0x098F`, `0x0626`) and
**page 3** for the note timers (`0x0D00`). `[STATIC]` With no callback registered all four
land on page 0.

Two consequences beyond section 3: the FM voice engine at `0x0578-0x05E7` reads the
*packet buffer* where it should read voice state, and the OPL2 shadow write-back at
`0x098F` writes OPL2 register values **over XRAM `0x0000-0x00FF`**, which is the sequencer's
current-sound byte (`0x0000`), its saved pointer (`0x0001-0x0004`) and the serial state
machine's whole working set (`0x0005-0x000E`). Today that is latent — every OPL2 write in
both runs is a boot sweep that happens before any packet arrives — but it will not stay
latent once the FM engine plays a note.

**The trap.** `i8051_reset()` still does
`i8051.eram_iaddr_callback = hold_eram_iaddr_callback; hold_eram_iaddr_callback = NULL;`
(`i8051.c:601-602`). The two serial callbacks next to it had those `NULL` assignments
removed in `bcf772a8`; this one was deliberately left, with a comment saying "no driver in
this tree both registers it and resets its 8051 more than once". `mephisto.c` resets its
8051 on `RST ASIN` — 47 times in 34.65 s. **The moment `mephisto.c` registers an
`eram_iaddr` callback that comment stops being true, and the callback will be destroyed at
the second reset**, silently putting every paged access back on page 0. The two lines have
to go in the same change.

## 9. What this does not explain

- **The main CPU still resets the sound board every 1.6 s even with the handshake forced.**
  Forcing P3.2 permanently high is not the protocol — the main CPU is supposed to raise it
  per byte and `ISR_SerialRX` lowers it again — so the sound board can outrun the main
  CPU's receiver. Whether a properly pulsed line satisfies the supervisor at `0xC9CA` is
  untested and will only be answerable once the line is really wired.
- **The OPL2 never plays a note, even in HS1.** All 2,952 writes are boot sweeps. Command
  `0x3D` is an AY-only score. Nothing here shows the FM path working; it needs a command
  that selects an FM sound, and defect 3 is in that path.
- **`0x10F00` is identified by argument, not by reading the trace off the schematic.** The
  falsifying observation is named in section 5 and did not occur, but neither did the
  positive one.
- **The AY port A/B handlers are almost certainly wrong** (section 7) and were not
  investigated.
- The `0xA4`/`0xA8` immediate commands at `0x013E-0x0165` are unreachable dead code
  (`ANL A,#0F0h` then `CJNE A,#0A4h`). Noted, not explained.

## 10. Recommendation

**One change, in `src/wpc/mephisto.c`, doing the handshake and the AY bus together** —
they are the same two handlers and splitting them would leave the AY receiving music-shaped
garbage in between:

1. Give the sound CPU a real port 1 / port 3 pair.
   - port 1 write: latch the byte (it is the AY data bus), do **not** call
     `AY8910_control_port_0_w`.
   - port 3 write: decode `data & 0x30` — `0x30` = `AY8910_control_port_0_w(latched)`,
     `0x10` = `AY8910_write_port_0_w(latched)`, `0x20` = arm a read; and feed bit 3 (P3.3)
     to `i8256_set_p1_pin(5, …)`, the sound board's busy line.
   - port 1 read: return the AY data when a read is armed, else `0xFF`.
   - port 3 read: return bit 2 set when the main CPU's Port 1 bit 6 is high.
2. `cirsa_p1_out()` stores bit 6 for that port 3 read. It is already called with the
   output-masked value by `i8256_port1_write()`, so no `i8256.c` change is needed.
3. Verify with the counters in section 6: `tx-go` must equal `txwait` entries,
   `0x00F8` must fire once per packet, and the AY must receive `r7<-7C, r0<-C2, r2<-C2`
   rather than `cf`/`ff`. Re-run `mephisto` and `mephist1` and expect them **unchanged** in
   attract (their port 3 read count is 0 today), then re-run the 35-game
   `scripts/i8051_sweep.sh` — this touches only `mephisto.c`, so any movement elsewhere
   would be a surprise worth chasing.

**Then, as a separate change:** register an `eram_iaddr` callback returning
`(P2 << 8) | offset`, **and in the same commit delete the two
`hold_eram_iaddr_callback = NULL;` lines at `i8051.c:602` and `:2499`** for the reason in
section 8. Reading P2 from the driver needs `i8051_internal_r(0xA0)`, which is already
exported.

Do **not** map `0x10F00` to anything other than `bank_w`, and do not "fix" the OPL2 sweep's
`0x00`/`0xFF` values directly — they are a symptom of the P2 defect, not of the FM code.

**Confidence: high** for sections 1, 2, 3, 4, 6 and 7 — each rests on an exact numerical
identity between a static reading of the ROM and a live count (46 x 123 = 5,658 OPL2
writes; 46 x 7 = 322 stored bytes; `serial-isr = rx-byte + rx-dropped + tx-done-isr` to
the unit; the score bytes at `0x279E` and `0x3315` matching the decoded AY stream byte for
byte), and the P3.2 hypothesis was tested by forcing the line and watching the firmware
start running. **Moderate for section 5** — the argument that there is only one banked
window is solid, but the observation that would have confirmed a single latch never
arose. **This document does not claim the machine will make sound after change 1 and 2.**
It claims the firmware will finally be executing its own commands with its own data, which
it is not doing today.
