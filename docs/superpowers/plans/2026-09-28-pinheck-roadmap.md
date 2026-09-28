# pinHeck / Domino's: plan roadmap

Spec: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md`

Each milestone of spec §8 gets its own implementation plan, written when its inputs exist, so that no plan has to guess. This file lists them in order, what each depends on, and the spec §9 open item each resolves or needs.

| Plan | Milestone | Depends on | Open items | Exit criterion (spec §7/§8) | Status |
|---|---|---|---|---|---|
| `2026-09-28-pinheck-m1-mips32-core.md` | 1: `mips32` core + disassembler | none | none | QEMU `4KEc` differential suite + golden/unit tests pass | written |
| m2 | 2: `pic32mx` SoC, PinMAME CPU registration, driver skeleton, `pinheck`/`dominos` romsets for the PIC32 side | Plan 1 | resolves **1** (chipKIT logical pin → port bit tables, return UART and baud) as its final task | machine check 1: UART1 banner `pinHeck System 2011-2016` / `Game: DOM - DOMINOS` / `Version:` | to write after Plan 1 |
| m3 | 3: `p8x32a` core + disassembler, CAT24M01 model, boot of `PRP_V008.BIN` | none (parallel with Plans 1–2) | resolves **4** (mask ROM image and CRC) as its first task | spinsim differential suite passes; boots `PRP_V008.BIN` from the EEPROM model | can be written now |
| m4 | 4: SD (SPI mode) + virtual FAT32 over the romset zip | Plan 3 for the exit criterion only | none | `fsck.vfat -n` clean, host mount byte-exact against the zip; Propeller mounts the card and opens `_DE/ERR.VID` | after Plan 3 |
| m5 | 5: link: edge log, catch-up, UART decode | Plans 2, 3 | needs **1** | machine check 2: `PROPELLER SYNC CHECK`, `[E00000]` round trip | after Plans 2–4 |
| m6 | 6: video generator + display decoder | Plan 5 | resolves **2** (video cog signalling) as its first task, using Plan 3's disassembler | machine check 3: `[V00ABC]` frames pixel-exact against the `.VID` | after Plan 5 |
| m7 | 7: audio (counter DUTY integration) | Plan 5 | none | machine check 4: `[F00ABC]` cross-correlation ≥ 0.95 against the `.wav` | after Plan 5 |
| m8 | 8: board I/O + full PinMAME integration | Plans 6, 7 | resolves **3** (external WS2801 chain length) | machine check 5; a game can be started, played and ended | after Plans 6–7 |
| m9 | 9: performance | Plan 8 | none | real time on the reference machine; baseline from Plan 1's `bench.sh` (106–118 M instr/s interpreter-only) | last |

Plans 1–2 and 3–4 are independent tracks; either can run first.
