# pinHeck / Domino's: plan roadmap

Spec: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md`

Each milestone of spec §8 gets its own implementation plan, written when its inputs exist, so that no plan has to guess. This file lists them in order, what each depends on, and the spec §9 open item each resolves or needs.

| Plan | Milestone | Depends on | Open items | Exit criterion (spec §7/§8) | Status |
|---|---|---|---|---|---|
| `2026-09-28-pinheck-m1-mips32-core.md` | 1: `mips32` core + disassembler | none | none | QEMU `4KEc` differential suite + golden/unit tests pass | written |
| `2026-09-28-pinheck-m2-pic32mx-soc.md` | 2: `pic32mx` SoC, PinMAME CPU registration, driver skeleton, `pinheck`/`dominos` romsets for the PIC32 side | Plan 1 | resolves **1** (chipKIT logical pin → port bit tables, return UART and baud) as its final task | machine check 1 via a test-only link stub (banner appears on the second boot); in PinMAME the run reaches `PROPELLER SYNC CHECK` | written; its Task 2 needs Plan 3's Task 2 (CAT24M01) |
| `2026-09-28-pinheck-m3-p8x32a-core.md` | 3: `p8x32a` core + disassembler, CAT24M01 model, boot of `PRP_V008.BIN` | none (parallel with Plans 1–2) | resolves **4** (mask ROM image and CRC) as its first task | spinsim differential suite passes; boots `PRP_V008.BIN` from the EEPROM model | written; mask ROM = silicon image, 32768 bytes, CRC32 f99b3070, SHA1 b7b4fdf4f096db7d18bda6355725cb42ae4a9378 |
| m4 | 4: SD (SPI mode) + virtual FAT32 over the romset zip | Plan 3 for the exit criterion only | none | `fsck.vfat -n` clean, host mount byte-exact against the zip; Propeller mounts the card and opens `_DE/ERR.VID` | after Plan 3 |
| m5 | 5: link: edge log, catch-up, UART decode | Plans 2, 3 | needs **1** | machine check 2: `PROPELLER SYNC CHECK`, `[E00000]` round trip | after Plans 2–4 |
| m6 | 6: video generator + display decoder | Plan 5 | resolves **2** (video cog signalling) as its first task, using Plan 3's disassembler | machine check 3: `[V00ABC]` frames pixel-exact against the `.VID` | after Plan 5 |
| m7 | 7: audio (counter DUTY integration) | Plan 5 | none | machine check 4: `[F00ABC]` cross-correlation ≥ 0.95 against the `.wav` | after Plan 5 |
| m8 | 8: board I/O + full PinMAME integration | Plans 6, 7 | resolves **3** (external WS2801 chain length) | machine check 5; a game can be started, played and ended | after Plans 6–7 |
| m9 | 9: performance | Plan 8 | none | real time on the reference machine; baseline from Plan 1's `bench.sh` (106–118 M instr/s interpreter-only) | last |

Plans 1–2 and 3–4 are independent tracks; either can run first.

## Obligations carried into Plan 5 (from Plans 2 and 3)

- The link is a full-duplex bit-banged 16-byte exchange: data out on RF5 (COMM_OUT), clock on RF12 (COMM_CLK_RX), reply in on RF13 (COMM_IN_TX), LSB first, command in byte 15; the Propeller stages its reply from the previous packet. There is no Propeller→PIC32 UART, so spec §5.3's UART decode is replaced by catch-up on reads of RF13 (Plan 2 Task 4 updates the spec).
- The real Propeller must pass Plan 2's `banner.py` in place of the test-only link stub.
- Spec §5.1's exact 13/10 cycle ratio holds only after `CLKSET $6F`; the booter runs on the internal RC clock until then, so time conversion must follow the core's `clkset` callback.
- Declare the `pinheck` BIOS set with `p8x32a.rom` (CRC32 f99b3070, SHA1 b7b4fdf4f096db7d18bda6355725cb42ae4a9378; GPL 3.0, user-supplied, never committed).
- Add `src/wpc/pinheck/eeprom.c` and `src/cpu/p8x32a/` to the PinMAME build files (neither Plan 2 nor Plan 3 does).

## Carried into Plan 9

- Interpreter throughput: `mips32` 106–118 M instr/s (needs ~80); `p8x32a` ~52 M cycles/s with one busy cog (needs 104 M cycles/s with up to 8 cogs), so the Propeller core is the real-time risk.
