# pinHeck / Domino's: plan roadmap

Spec: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md`, with addenda `2026-09-29-pinheck-m6-m7-display-audio-design.md`, `2026-09-29-pinheck-m8-m9-design.md` and `2026-09-29-pinheck-m10-vpx-design.md`

Each milestone of spec §8 gets its own implementation plan, written when its inputs exist, so that no plan has to guess. This file lists them in order, what each depends on, and the spec §9 open item each resolves or needs.

| Plan | Milestone | Depends on | Open items | Exit criterion (spec §7/§8) | Status |
|---|---|---|---|---|---|
| `2026-09-28-pinheck-m1-mips32-core.md` | 1: `mips32` core + disassembler | none | none | QEMU `4KEc` differential suite + golden/unit tests pass | executed |
| `2026-09-28-pinheck-m2-pic32mx-soc.md` | 2: `pic32mx` SoC, PinMAME CPU registration, driver skeleton, `pinheck`/`dominos` romsets for the PIC32 side | Plan 1 | resolved **1**: logical pins 14/15/16 = RF13/RF12/RF5, no return UART (console UART1 at 115,200) | machine check 1 via a test-only link stub (banner appears on the second boot); in PinMAME the run reaches `PROPELLER SYNC CHECK` | executed |
| `2026-09-28-pinheck-m3-p8x32a-core.md` | 3: `p8x32a` core + disassembler, CAT24M01 model, boot of `PRP_V008.BIN` | none (parallel with Plans 1–2) | resolves **4** (mask ROM image and CRC) as its first task | spinsim differential suite passes; boots `PRP_V008.BIN` from the EEPROM model | executed; mask ROM = silicon image, 32768 bytes, CRC32 f99b3070, SHA1 b7b4fdf4f096db7d18bda6355725cb42ae4a9378 |
| `2026-09-28-pinheck-m4-sd-vfat.md` | 4: counter pin outputs, SD (SPI mode) + virtual FAT32 over the romset zip | Plan 3 | none | `fsck.vfat -n` clean, host mount byte-exact against the zip; the Propeller mounts the card and reads `DMD/_DZ/ZMB.VID` | executed |
| `2026-09-28-pinheck-m5-link.md` | 5: link: edge log, catch-up on reads of RF13, bootloader stand-in, DS1340, EEPROM NVRAM, `pinheck` BIOS set | Plans 2, 3, 4 (executed) | needs **1**; decisions **D1** (bootloader stand-in), **D2** (flash persistence) | machine check 2: update to `PLEASE RESTART` on blank NVRAM, then `PROPELLER SYNC CHECK`, banner and `[E97000]` | executed |
| `2026-09-29-pinheck-m6-display.md` | 6: display: serial frame link (P22 clock, P21 data, P20 frame strobe, P17 config) → `CORE_VIDEO` 128×32 RGB332, exact pixels | Plan 5 | resolved **2** (a serial frame link, not the video generator) | machine check 3: `[V00ABC]` frames pixel-exact against the `.VID`, and equal to the firmware framebuffer at `$57EE` | being written (parallel with 7) |
| `2026-09-29-pinheck-m7-audio.md` | 7: audio: counter-state sink, exact DUTY integration on P15/P14, DC blocker, stereo stream | Plan 5 | none | machine check 4: `[F00ABC]` cross-correlation ≥ 0.95 per channel against the `.wav` | being written (parallel with 6) |
| `2026-09-29-pinheck-m8a-board.md` | 8a: board I/O (lamps, coils, switches, cabinet chain, GI, RGB, servos, start lamp), PinMAME time in board callbacks, PORTB/sub-word/I²C carried items, libpinmame `PINHECK` | Plans 6, 7 | resolved **3** (one external WS2801 LED) | machine check 5: `[MXXzzz]`, `[LXXzzz]` and the service-menu solenoid, servo, lamp, RGB and switch tests land on spec §4.5 numbers | written |
| `2026-09-29-pinheck-m8b-sim-game.md` | 8b: playfield simulator (`sims/pinheck/dominos.c`, framework `autoBall`), keyboard play, link packet log, repeatable clock, scripted headless game | Plan 8a | none | simulator check; the scripted game plays from coin-up through multiball to high-score entry and match, byte-identical between runs | written |
| m9 | 9: performance, staged: profile, interpreter fast paths, `asmjit` JIT, deterministic threading only if needed | Plan 8 | none | ≥ 1.0× real time on the Ryzen 7 7730U for attract, scripted game and video+audio, byte-identical to the reference build | after Plan 8 (parallel with the display look) |
| look | display look: decode and render the config packet (`PIXEL SHAPE`, `BRIGHTNESS`, `POSITION`, `BAR BRIGHT`) | Plans 6, 8 | none | packet decoding tests and golden images per setting; shape rendering labelled an approximation | after Plan 8 (parallel with 9) |
| m10 | 10: VPX support: libpinmame export proof, `pinheck_names.h`, minimal test table (outside the repo), VPX standalone on Linux and Windows VPX + VPinMAME | Plan 9 | none | export paths equal M6/M7/M8 outputs; test table checklist passes on Linux; Windows checklist handed to the user | after Plan 9 |
| release | release: destination decided when the game runs (upstream PR or fork); conventions, CI, what ships, ROM instructions, deferred minors | Plan 10 | none | designed when the game runs; branch stays local until then | last |

Plans 1–2 and 3–4 are independent tracks; either can run first.

## Obligations carried into Plan 4 (from the Milestone 2+3 review)

- First task: counter pin outputs. The firmware's SD driver (cog 3) clocks the card with counter A in NCO mode on P1 and sends data with counter B in NCO mode on P2; cog 6 drives P21/P22 the same way. The core must drive NCO/DUTY counter outputs onto the pins cycle-exactly (RTL-compared), visible in `pins_out`, `INA` and `WAITPEQ`/`WAITPNE`, and the suite must show that booting `PRP_V008.BIN` for several seconds logs no unmodelled counter mode. Without this the card never sees a clock and the exit criterion cannot be met.

## Obligations carried into Plan 5 (from Plans 2 and 3)

- The link is a full-duplex bit-banged 16-byte exchange: data out on RF5 (COMM_OUT), clock on RF12 (COMM_CLK_RX), reply in on RF13 (COMM_IN_TX), LSB first, command in byte 15; the Propeller stages its reply from the previous packet. There is no Propeller→PIC32 UART, so spec §5.3's UART decode is replaced by catch-up on reads of RF13 (Plan 2 Task 4 updates the spec).
- The real Propeller must pass Plan 2's `banner.py` in place of the test-only link stub.
- Spec §5.1's exact 13/10 cycle ratio holds only after `CLKSET $6F`; the booter runs on the internal RC clock until then, so time conversion must follow the core's `clkset` callback.
- Declare the `pinheck` BIOS set with `p8x32a.rom` (CRC32 f99b3070, SHA1 b7b4fdf4f096db7d18bda6355725cb42ae4a9378; GPL 3.0, user-supplied, never committed).
- Add `src/wpc/pinheck/eeprom.c` and `src/cpu/p8x32a/` to the PinMAME build files (neither Plan 2 nor Plan 3 does).
- `pins_next` may return `P8X32A_NEVER` while no PIC32 edge is known yet; since 217130d1 the core re-queries on every `run_until`, so the driver only has to append edges before advancing the Propeller past them.
- Inject UART1 RX bytes in `pinheck.c` (machine check 2's `[E00000]` needs them) and wire the U13 CAT24M01 onto I2C1.
- A PinMAME reset sets the PIC32 cycle count back to 0 while Propeller time must never go backwards: rebase the edge log and the time conversion on reset.

## Carried into Plan 6

- The Propeller's framebuffer is readable in hub RAM around `$57EE`–`$67ED` (128×32 RGB332, row-wrapped); it shows the boot and update screens, which is a useful cross-check for the display decoder.

## Carried into Plan 7

- The counter-state sink required by spec §4.3/§5.4 (every `FRQx`/`CTRx` change with its cycle) is not in `p8x32a_bus` yet; the audio device needs it.

## Carried into Plan 8

- `pic32cpu_ICount` is only updated when `pic32mx_run` returns, so `timer_get_time()` inside a board callback reports the slice start. Spec §5.6 relies on per-edge timestamps for lamp and solenoid PWM: update the count around each board callback and honour `activecpu_abort_timeslice`, with a test that two GPIO edges in one slice get different times.

## Carried into Plan 9

- The scripted game (`tests/pinheck/game/pinmame_game.sh`) is the workload and the determinism reference: 315 s emulated (about 50 minutes at 0.1×), with `PINHECK_RTC` fixed its `link2.log`, `out2.log`, `uart2.log` and `frames2.bin` must stay byte-identical to the reference build's.

- Interpreter throughput: `mips32` 106–118 M instr/s (needs ~80); `p8x32a` ~52 M cycles/s with one busy cog (needs 104 M cycles/s with up to 8 cogs), so the Propeller core is the real-time risk.
