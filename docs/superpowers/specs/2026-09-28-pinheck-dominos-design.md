# pinHeck system and Domino's Spectacular Pinball Adventure — design

Status: approved design, pre-implementation
Date: 2026-09-28

## 1. Goal

Run Spooky Pinball's *Domino's Spectacular Pinball Adventure* (2016) in PinMAME as a playable game: switches, the 24 coils, the 8×8 lamp matrix with its 8 brightness levels, 16 GI/flasher outputs, the 128×32 colour display and 4-channel audio, all produced by the machine's own firmware.

Both CPUs are emulated at low level. The design must also admit the other pinHeck games (America's Most Haunted, Rob Zombie's Spookshow International, Total Nuclear Annihilation, Alice Cooper's Nightmare Castle) without structural change, though only Domino's is in scope.

## 2. Hardware (verified facts)

Sources: the update package (`DOM_V006.PRG`, `PRP_V008.BIN`, `DMD/`, `SFX/`), the open-source pinHeck Eagle schematic (github.com/LonghornEngineer/Pinheck_Pinball_System, `Hardware/Pinheck Main Board/PinHeck.sch`), the archived pinHeck wiki (benheck.com/pinHeck_wiki, via web.archive.org) and Spooky's Domino's wiring, matrix and solenoid documents.

### 2.1 Processors

| | Part | Clock | Role |
|---|---|---|---|
| Game CPU | PIC32MX795F512L-80I/PT, MIPS32 Release 2, M4K core | 80 MHz (8.000 MHz crystal) | game logic, all playfield/cabinet I/O |
| A/V CPU | Parallax P8X32A-Q44 Propeller | 104 MHz (6.5 MHz crystal, PLL16) | SD card, display, audio |

`DOM_V006.PRG` (203,152 bytes, CRC32 `750e27a4`, SHA1 `3fbebce7f885563e61dd2e4f5d7f0a8a54c53b23`) is a raw image of PIC32 program flash, loaded at `0x9D000000`:

- EBase `0x9D000000`; general exception vector `0x9D000180` (spins on itself); 64-entry interrupt vector table at `0x9D000200` with 32-byte spacing, each entry jumping through a function pointer in RAM (chipKIT/MPIDE).
- Entry stub at `0x9D001000`; startup checks `Status.NMI`, sets `sp = 0xA001FFF8`, `gp = 0xA0008200` and configures `SRSCtl`.
- Uses MIPS32r2 instructions (`ext`, `ins`, `ehb`) and shadow register sets.
- Built with the chipKIT Arduino core (RTTI `14HardwareSerial`, `6Stream`, `5Print`).
- SFR blocks the firmware addresses: INTC, Timer2–5, PORTA–G, UART1, I²C, ADC (pin configuration), OSC, BMX, CHECON, DDPCON, RCON. No DMA, USB, Ethernet, CAN or PMP.

`PRP_V008.BIN` (32,768 bytes, CRC32 `a51ee28d`, SHA1 `0556d88b6f0c7cb15e648e1f76f4d47890ddb858`) is a Propeller EEPROM image: `clkfreq` 104,000,000, `clkmode` `0x6F` (xtal1 + pll16x), PBASE `0x0010`, VBASE `0x3F50`, DBASE `0x7DA0`, PCURR `0x02EC`, DCURR `0x7DB0`, byte sum ≡ 0 mod 256. The top object is Spin, so the chip's mask-ROM Spin interpreter is required. A PASM block around `0x3300–0x3E00` writes `VCFG`/`VSCL` and `CTRA`/`FRQA`.

The P8X32A mask ROM (hub `$8000–$FFFF`: font, log/antilog/sine tables, booter, Spin interpreter) was released by Parallax under GPL 3.0 and is not part of the update package.

### 2.2 Board wiring

PIC32 GPIO drives directly:

- 24 solenoids, one pin per IRL530 gate (`PIC_SOL_0..23`), in three banks of eight.
- The 8×8 lamp matrix: `PIC_LIGHT_C_0..7` to TIP107 column drivers, `PIC_LIGHT_R_0..7` to TIP102 row drivers. 8 brightness levels per lamp (firmware: `[LXXzzz] Set light XX (0-63) to ZZZ brightness (0-7)`).
- The 8×8 switch matrix (`PIC_SW_C_0..7`, `PIC_SW_R_0..7`).
- 5 servo pins.
- `ST_LI_GATE` (start button lamp).

Shift-register chains share `CAB_CLK`:

- 2× 74HC165 (`CAB_LAT`, serial out on `CAB_SWITCH_IN`): 16 cabinet inputs. U12 carries DOOR, USER_0, R_FLIP, L_FLIP, BACK, ENTER, COIN, TILT on D0–D7 and feeds U11's SER. U11 carries AUX_0, AUX_1, AUX_2, ST_BU, AUX_3–AUX_6.
- 2× 74HC595 (`CAB_GI_OUT` data, `!DATA_LAT` latch): 16 GI/flasher outputs `GI_0..15` to TIP102s.

Other PIC32-side devices: 2× WS2801 on `RGB_DATA`/`RGB_CLK` driving six RGB MOSFETs (left and right cabinet RGB); an external WS2801 chain on `EXT_RGB_DATA`/`EXT_RGB_CLK`; a DS1340 RTC and a CAT24M01 on `I2C_SCL`/`I2C_SDA`; UART1 (`TX_PIC32`/`RX_PIC32`) as the service console.

The Propeller's pins:

| Pins | Use |
|---|---|
| P0–P3 | SD card (DO, SCLK, DI, CS) |
| P14, P15 | audio (AUD1, AUD0) to the TRS jack |
| P16–P22 | display connector DMD_13, 11, 9, 7, 5, 3, 1 (odd pins of a 2×7 header; even pins are GND) |
| P23 | PROP_AUX9 |
| P24, P25, P26 | link to the PIC32 (COMM_IN_TX, COMM_CLK_RX, COMM_OUT) |
| P28, P29 | CAT24M01 boot EEPROM (SCL_PROP, SDA_PROP) |
| P30, P31 | programming UART |

### 2.3 PIC32 ↔ Propeller link

PIC32 to Propeller is bit-banged in software (`0x9D02DBE0`). A 16-byte buffer at `0xA000261C` holds 15 payload bytes, with the command ID in byte 15. It is shifted out LSB-first by `digitalWrite`: data on chipKIT logical pin 16, and a clock pulse (high then low) on logical pin 15 per bit.

28 call sites use 24 command IDs in `0x01–0x27`. The archived wiki documents the API those implement: `playSFX`, `playSFXQ`, `stereoSFX`, `volumeSFX`, `playMusic`, `playMusicOnce`, `stopMusic`, `fadeMusic`, `repeatMusic`, `video`, `videoQ`, `videoControl`, `videoPriority`, `stopVideo`, `killQ`, `text`, `value`, `numbers`, `killNumbers`, `killTimer`, `characterSprite`, `loadSprite`, `showProgressBar`, `showValue`, `graphicsMode`, `AddScore`, `SetScore`, `Update`, `EOBnumbers`, `sendHighScores`, `sendInitials`, `sendSwitches`, `readEEPROM`, `writeEEPROM`.

Propeller to PIC32 travels in the same exchange: while clocking each bit out on RF5 with RF12, the PIC32 samples RF13 (COMM_IN_TX), which the Propeller drives with its reply staged from the previous packet. There is no Propeller-to-PIC32 UART. The only UART in use is UART1, the service console (BRG 42, 115,200 baud nominal); the chipKIT `HardwareSerial` ring buffer in the firmware (head/tail at object offsets `+0x22C`/`+0x230`, 512-byte buffer at `+0x2C`) belongs to that console.

Per the wiki: the PIC32 keeps its non-volatile data in the top half of the Propeller's EEPROM, and the PIC32 main loop runs at about 10 kHz. It also keeps its settings in U13 on I2C1, next to the DS1340.

Before serving the link, the Propeller checks its EEPROM long `$8000`. If its low 16 bits are not `$BAFA`, or P13 (`PROP_CONFIG`) is held low, or the long at `$8004` is `$ABBA0001` (written by the PIC32's `UPDATE CODE` action through `writeEEPROM`, word 1), it runs its update: `FLASHING TO:`, then a serial driver on P24 (RX) / P25 (TX) at 115,200 baud sends an STK500v2 `CMD_SIGN_ON` (`1B 00 00 01 0E 01 15`) to the PIC32 bootloader about 1.7 s after power-on and waits without timeout for an answer with status 0 and a signature starting `ST`. It then clears `$8004`, loads address 0 and programs the card's `DOM_V006.PRG` with `CMD_PROGRAM_FLASH_ISP` in 128/128/256-byte blocks, reads it back with `CMD_READ_FLASH_ISP` (256 bytes), reprograms and verifies its own EEPROM from the card's `PRP_V008.BIN` (`PRP V8 FOUND`, `PROGRAM AV EEPROM`, `VERIFY AV EEPROM`), writes `$8004 = $ABBA0002` and `$8000 = (v << 24) | $BAFA` (v = the digits after `_V`, 6 for `DOM_V006.PRG`), sends `CMD_LEAVE_PROGMODE_ISP` and shows `PLEASE RESTART` (about 135 s after power-on in total); it does not serve the game link in that power cycle. The Propeller drives neither MCLR nor RG9. Link pins RF12/RF13 are U3BRX/U3BTX (UART5) on the PIC32MX795. The open-source `Bootloader_Max32.hex` talks on UART1 and is CC BY-NC-SA 4.0, so it is not used.

### 2.4 Media

- `DMD/_D<A–Z>/<NNN>.VID` (569 files, 272 MB): 512-byte header, then N × 4096-byte frames of 128 × 32 pixels, one byte per pixel, **RGB332** (verified by the gray ramp `00 24 49 6D 92 B6 DB FF` and by rendering). 30 fps for one layer; two layers (black transparent) at 15 fps.
- `DMD/_DZ/`: fonts (`*.FNT`) and sprites (`*.SPR`, some with lowercase extensions).
- `SFX/_F<A–Z>/<NNN>.wav` (410 files, 497 MB): RIFF PCM, 16-bit stereo, 22,050 Hz. 4 simultaneous channels; channel 3 is music.
- All names are 8.3.

## 3. Decisions

| Decision | Choice | Reason |
|---|---|---|
| Propeller | Low-level emulation of the P8X32A; `PRP_V008.BIN` runs unmodified | exact fonts, layout, timing; covers every pinHeck game |
| Propeller peripherals | cogs, ISA, hub and hub arbitration exact; counters and video generator as functional devices | true LLE at a cost that can reach real time; no pin-level waveform synthesis |
| SD assets | read-only FAT32 synthesised from the romset zip | the official update zip is the romset, as for every PinMAME game |
| Integration | PIC32 is the PinMAME CPU; the Propeller is a device core stepped by the driver | bit-exact link without fine interleave; PinMAME debugger on the game CPU |
| Verification | differential testing against external oracles plus ground truth from the media files | no hardware access |

## 4. Components

```
src/cpu/mips32/        M4K core + mips32dasm.c                      pure C, no PinMAME dependency
src/cpu/pic32mx/       PIC32MX795F512L SoC on mips32 + cpuintrf glue (HAS_PIC32MX)
src/cpu/p8x32a/        Propeller core + p8x32adasm.c                pure C, no PinMAME dependency
src/wpc/pinheck/prop.c     Propeller device: stepping, pin edge log, link
src/wpc/pinheck/sd.c       SD card, SPI mode, over a block-device interface
src/wpc/pinheck/vfat.c     read-only FAT32 volume synthesised from zip entries
src/wpc/pinheck/eeprom.c   CAT24M01, bit-level I²C driven from pins
src/wpc/pinheck/rtc.c      DS1340
src/wpc/pinheck.c          machine driver
src/wpc/pinheckgames.c     BIOS set and game definitions
tests/pinheck/             headless harness and differential tests
```

### 4.1 `mips32`

MIPS32r2 user ISA without FPU:

- ALU, shifts, rotates, branches (including likely and link forms), loads/stores (including unaligned `lwl`/`lwr`/`swl`/`swr`, `ll`/`sc`), `mult`/`multu`/`div`/`divu`, `madd`/`maddu`/`msub`/`msubu`, `mul`, `clz`/`clo`, `ext`/`ins`, `seb`/`seh`/`wsbh`, `movn`/`movz`, `sync`, `syscall`/`break`, traps, `rdhwr`, `rdpgpr`/`wrpgpr`, `di`/`ei`, `ehb`, `eret`, `wait`, `cache` (no-op), `pref` (no-op).

CP0:

- `Status`, `Cause`, `EPC`, `ErrorEPC`, `Count`/`Compare` (Count increments every other cycle), `EBase`, `IntCtl`, `SRSCtl`, `SRSMap`, `PRId`, `Config`/`Config1–3`, `BadVAddr`, `HWREna`. No EJTAG: `Debug`/`DEPC` read as 0 and `sdbbp` raises Reserved Instruction.
- Up to 8 shadow register sets.
- Exceptions:
  - Interrupt, AdEL/AdES, IBE/DBE, Sys, Bp, RI, CpU, Ov, Tr.
  - EIC-mode interrupt entry: vector offset from IntCtl.VS, shadow set from the controller.
  - `Cause.IV` / `Status.BEV` handling.
- Fixed-mapping translation: kseg0/kseg1 to physical, kuseg per the M4K FMT.

Interface:
- `read8/16/32`, `write8/16/32` and fetch callbacks with physical addresses
- an EIC input (requested level, vector number, shadow set)
- `run(cycles)` returning cycles consumed
- 1 cycle per instruction, plus the M4K's documented extra cycles for multiply/divide

### 4.2 `pic32mx`

Memory map: 128 KB data RAM `0x00000000` (KSEG0 `0x80000000`, KSEG1 `0xA0000000`); 512 KB program flash `0x1D000000`; boot flash `0x1FC00000` holding a synthesised stub that jumps to `0x9D001000`, preceded by the bootloader stand-in of §4.4, which holds the core through the board's `hold` callback; SFRs `0x1F800000–0x1F8FFFFF` including SET/CLR/INV aliases at +4/+8/+C.

Peripherals:
- **INTC**: `INTCON`, `INTSTAT`, `IFSx`, `IECx`, `IPCx`, priority and subpriority arbitration, `INTCON.MVEC`, `SRSMap` via priority, feeding the core's EIC input.
- **Timer1–5**, including 32-bit pairing.
- **PORTA–G**: `TRIS`, `PORT`, `LAT`, `ODC`, `CN*`. Pin reads and writes go through a board callback with the pin identity and the cycle timestamp.
- **UART1–6**: TX to a byte sink with timestamps; RX bytes injected by the board. The firmware enables only UART1, the service console (BRG 42: 115,200 baud nominal).
- **I²C1–5** master, bit-level onto pins.
- **Stubs**: OSC, CHECON, BMX, DDPCON, RCON, ADC configuration, NVM (read-only), WDT.
- Configuration words read as 80 MHz from the 8 MHz crystal.

The chipKIT logical-pin table used by `digitalWrite` is resolved from the firmware (open item 1); the SoC itself works only with physical port bits.

### 4.3 `p8x32a`

- 8 cogs × 512 longs, with special registers `PAR`, `CNT`, `INA`, `INB`, `OUTA`, `OUTB`, `DIRA`, `DIRB`, `CTRA`, `CTRB`, `FRQA`, `FRQB`, `PHSA`, `PHSB`, `VCFG`, `VSCL`.
- The full PASM instruction set with conditions and Z/C/R flags.
- 4 clocks per instruction; hub instructions wait for the cog's slot in the 16-clock rotation. `WAITCNT`/`WAITPEQ`/`WAITPNE`/`WAITVID` stall until their condition.
- 32 KB hub RAM and 32 KB ROM; 8 locks; `COGINIT`/`COGSTOP`/`COGID`/`CLKSET`.
- `OUTA`/`DIRA` are the OR of all running cogs.
- Counters: NCO single and differential outputs drive pins cycle-exactly. The firmware uses NCO on P1/P2 (SD clock and data), P21/P22 (display) and P25 (serial TX), mostly with `FRQ` = 0 and data shifted through `PHS`. DUTY modes (the firmware's audio DAC on P14/P15) are not driven onto pins: the audio device integrates `FRQ` instead (§5.4). PLL pin outputs and pin-sensing modes are reported once, not modelled.
- Video generator: not modelled. `PRP_V008.BIN` never executes `WAITVID`; the display is a serial frame link (§5.5). `WAITVID` is reported once, not modelled.
- Idle cogs (in a `WAIT*` with a known wake time) advance without executing.

Interface:
- `run_until(cycle)`
- a pin-input callback taking the cog-local cycle, plus a next-edge query for `WAITPEQ`/`WAITPNE`
- sinks for pin-output edges and counter-state changes

### 4.4 `pinheck/` devices

- **`prop.c`** owns the Propeller instance, its CAT24M01 on P28/P29 and the shared-pin edge log (PIC32 writes to P25/P26, stored in PIC32 cycles). It converts PIC32 cycles to Propeller cycles through the clock map of §5.1 and implements the catch-up rules of §5.2–§5.3. Undriven pins read 0 except where the board pulls them up: SD `DO` and `CS`, P13 and SCL/SDA; P31 (RX) floats and reads 0. The SD card attaches through a `(cs, sclk, mosi) → DO` callback. A `CLKSET` with the reset bit restarts the chip at the end of the catch-up in which it executed.
- **`sd.c`** implements SD in SPI mode as used by the firmware's SD driver: `CMD0/8/9/10/12/13/16/17/18/24/55/58/59` and `ACMD41`, the R1/R3/R7 responses and data tokens, with high-capacity addressing when the host negotiates it and byte addressing otherwise. The card shifts its output only after it has sampled a bit, because the firmware's NCO clock can be high when chip-select asserts. It is read-only: writes are accepted, discarded and counted.
- **`vfat.c`** builds a FAT32 volume from the romset zip's root files and its `DMD/` and `SFX/` entries (the Propeller's update reads `DOM_V006.PRG` and `PRP_V008.BIN` from the card root): an MBR partition at LBA 8192, boot sector, FSInfo, two FATs, root and subdirectories with 8.3 names stored uppercase, and one contiguous cluster run per file. Zip directory entries (for example empty folders) become empty directories. Clusters are 32 KB (64 sectors), because the firmware reads the reserved-sector and FAT-size fields from the boot sector but assumes 64 sectors per cluster. The volume has at least 65,536 clusters, as FAT32 requires, so it is padded with free space to about 2 GB. Data-sector reads map to (entry, offset).
- **`zipsrc.c`** reads the romset zip. Stored entries are read by range. Deflated entries are inflated whole into a bounded LRU cache, and an entry larger than the cache is held only until another entry is read.
- **`eeprom.c`** is a CAT24M01 (128 KB) state machine on SCL/SDA pin edges: device addressing including the A16 bit in the control byte, page writes, sequential reads, ACK/NACK. There are two instances: one on Propeller P28/P29, preloaded in its lower 32 KB with `PRP_V008.BIN` and in its upper half from NVRAM; one on the PIC32's I²C, from NVRAM.
- **`rtc.c`** is a DS1340 on I2C1 (device `$68`), sharing the bus with U13 (wired-AND). It is seeded from the host's local time at every reset and advances with PIC32 cycles; writes set the clock. Without it the firmware stops on the `SET DATETIME` screen on every boot after the first.
- **`bootldr.c`** is a high-level stand-in for the PIC32 bootloader, whose binary is not available. After every PIC32 reset it holds the core for a boot window and listens on UART5 (RF12 in, RF13 out, 8N1, BRG 42) for STK500v2 frames. `CMD_SIGN_ON` answers `STK500_2` and keeps the core held; `LOAD_ADDRESS`, `PROGRAM_FLASH_ISP` (byte addresses from `0x9D000000`, erasing each 4 KB page on its first write) and `READ_FLASH_ISP` act on the emulated program flash; `LEAVE_PROGMODE_ISP` starts the application once its reply has been sent. Other commands answer `STATUS_CMD_FAILED` (`$C0`) and are logged once. Without a sign-on the window ends and the application starts at `0x9D001000`.

### 4.5 `pinheck.c` machine driver

- **Board logic on PIC32 GPIO and shift chains**: lamp matrix, solenoids, switch matrix reads, the 74HC165 chain on `CAB_LAT`/`CAB_CLK`, the 74HC595 chain on `CAB_CLK`/`!DATA_LAT`, WS2801 decoding, servo pulse measurement.
- **Numbering**, following the Domino's documents (0-based there, PinMAME's 1-based numbers here):
  - Switch `n` (0–63) = column `n/8`, row `n%8` → PinMAME switch `(n/8+1)*10 + (n%8+1)`. The 16 cabinet inputs → custom columns 0 and 9.
  - Lamp `n` (0–63) = column `n/8`, row `n%8` → PinMAME lamp `(n/8+1)*10 + (n%8+1)`, modulated.
  - Coils 0–23 → solenoids 1–24. GI/flasher 0–7 → solenoids 25–32. GI/flasher 8–15 → solenoids 37–44.
  - Onboard RGB (left R, G, B, right R, G, B) → custom solenoids 51–56.
  - Servos 0–4 → custom solenoids 57–61, carrying the pulse width normalised over 1.0–2.0 ms.
  - All of these are modulated outputs.
- **Display**: a `CORE_VIDEO` layout of 128×32 with a custom renderer converting RGB332 to host colour, fed by the display module model (`display.c`, §5.5).
- **Sound**: one stereo stream from the audio device (§5.4).
- **NVRAM**: U13 (128 KB) and the Propeller EEPROM above the 32 KB `PRP_V008.BIN` image, through PinMAME's NVRAM handler. A first run starts blank, so the Propeller runs its update against `bootldr.c` and stops at `PLEASE RESTART`; the next reset or launch boots the game. Program flash written by the update persists across resets within a session and is reloaded from the romset's `DOM_V006.PRG` at every launch (decision D2, §10). The DS1340 is not persisted.
- **Service console**: UART1 TX is logged. An input path injects UART1 RX bytes for tests.

### 4.6 `pinheckgames.c`

- `pinheck`: a `NOT_A_DRIVER` parent set holding `p8x32a.rom` (32 KB, hub `$8000–$FFFF`, CRC32 `f99b3070`, SHA1 `b7b4fdf4f096db7d18bda6355725cb42ae4a9378`). It is GPL 3.0 and user-supplied, never committed. `dominos` is its clone, so PinMAME finds the ROM in `dominos.zip` or `pinheck.zip`.
- `dominos`: the Domino's update zip renamed. It declares `DOM_V006.PRG` and `PRP_V008.BIN` as ROM regions with the CRC/SHA1 above. `DMD/` and `SFX/` are opened from the same zip by `vfat.c` and are not declared.

Assumption: the official update zip carries both firmware files at its root, as in the extracted package.

## 5. Timing and data flow

### 5.1 Clock

PinMAME schedules the PIC32 at 80 MHz. The Propeller clock follows its own `CLKSET`: after reset it runs on RCFAST, taken as the nominal 12 MHz (3/20 of the PIC32 rate); XIN and PLL1×–16× derive from the board's 6.5 MHz crystal, so PLL16× (`CLKSET $6F`, 104 MHz) is exactly 13/10. `prop.c` keeps a piecewise-linear map from PIC32 cycles to Propeller cycles with one segment per `CLKSET`. PIC32 cycles are offset by an epoch that grows by the PIC32's cycle count at each PinMAME reset, so Propeller time never moves backwards.

### 5.2 Link, PIC32 to Propeller

Every PIC32 write that changes a pin wired to a Propeller input is appended to the edge log with its PIC32 timestamp; it becomes visible at Propeller cycle ⌊map(P)⌋, converted when the Propeller reads it, so a `CLKSET` between logging and use is honoured. The Propeller's `INA` is evaluated at the reading cog's local cycle from the log. `WAITPEQ`/`WAITPNE` resolve to the cycle of the first matching edge. A catch-up for PIC32 cycle R runs the Propeller to map(R) − 1, so a later PIC32 write always lands on a Propeller cycle not yet evaluated; results are then identical whether the Propeller is caught up every cycle or once. Appending to a full log first catches the Propeller up to that write, so edges are never dropped. Entries at or before the Propeller's current cycle are folded into its base pin state.

### 5.3 Link, Propeller to PIC32

The Propeller is advanced to the PIC32's current cycle:
- before any PIC32 read of a port containing a Propeller-driven pin
- at every tick of a 1 kHz PinMAME timer, so the Propeller keeps producing video and audio while the PIC32 is not reading it
- at every step of a bootloader hold (§4.4), before the stand-in decodes the Propeller's P25 edges

The game link has no Propeller→PIC32 UART. Its return path is RF13 (COMM_IN_TX, Propeller P24), which the PIC32 samples with `digitalRead(14)` after each falling edge of the clock it drives on RF12, during the same 16-byte exchange that shifts its own packet out on RF5. The first rule above therefore makes the return path exact. The update path (§2.3) is a UART in both directions: every change of the Propeller's P25 output reaches the bootloader stand-in (§4.4) with its PIC32 timestamp, and the stand-in's replies on RF13 enter the edge log as P24, like PIC32 pin writes.

### 5.4 Audio

For each counter in a DUTY mode on P14/P15, the device integrates `FRQx / 2^32` over time. Every value change of `FRQx` or `CTRx` closes a segment. Each output sample at the stream rate (44.1 kHz stereo) is the time-weighted average over its interval, scaled to ±1 and DC-centred. Other counter modes on these pins are logged and output silence.

### 5.5 Video

The Propeller sends finished frames to the display module over a clocked serial link: P22 (DMD_1) clock, data valid on the rising edge; P21 (DMD_3) data, MSB first; P20 (DMD_5) latch pulse; P17 (DMD_11) held high during a config packet. The latch pulse carries one clock edge of its own, which is not data. A frame is 4096 bytes, 128 × 32 pixels, RGB332, the `.VID` format; the firmware sends one only when the picture changes (every 33 ms while a clip plays at 30 fps, every 46 ms on the score screen). The config packet is 14 bytes (`00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e` on a default machine), sent once at start-up. `display.c` models the receiving module from the Propeller's pin edges: it latches a transfer on the rising edge of P20, delivers exactly 4096 bytes as a frame, and discards anything else with one log line. The renderer shows the last complete frame. The firmware keeps its framebuffer at hub `$5870`; every frame it sends equals that buffer when latched. Starting a clip with `video()`, the firmware shows the clip from its frame 1: frame 0 is loaded but not sent.

### 5.6 I/O

GPIO writes update the board logic immediately with their timestamp, so lamp and solenoid modulation is integrated by PinMAME's PWM machinery from real on/off times. Switch-matrix reads return the switch rows for the column currently driven, from `coreGlobals.swMatrix`.

## 6. Failure behaviour

- **Missing BIOS or firmware, or bad CRC**: standard PinMAME romset failure and audit report.
- **Media**: the volume contains exactly the zip's entries. A missing clip is left to the firmware.
- **PIC32 reserved instruction**: Reserved Instruction exception as on the M4K, and the PC logged once.
- **Unmodelled SFR**: reads return 0 and writes are ignored, logged once per address.
- **Unmodelled counter mode or video-generator mode**: logged once. The device outputs silence or black.
- **SD write commands**: accepted, data discarded, logged.
- **Unsupported STK500v2 command** (bootloader stand-in): answered with `STATUS_CMD_FAILED`, logged once per command.
- **Edge-log full**: forces catch-up (§5.2).
- **Below real time**: runs slow. The quantum and edge resolution do not change with speed.

## 7. Verification

External oracles are test tools only and are never linked into PinMAME.

### Core level (headless, no PinMAME)

- **`mips32`**:
  - Differential against QEMU `mipsel` with CPU model `4KEc`: cross-compiled test programs and randomised instruction streams, comparing registers and memory after each run.
  - CP0, exceptions, shadow register sets and EIC entry: hand-written tests, each citing its section of the MIPS32 Privileged Resource Architecture or the PIC32 Family Reference Manual.
- **`p8x32a`**:
  - Differential against spinsim: PASM test programs, Spin programs through the interpreter, and booting `PRP_V008.BIN` from reset until cog 0 starts the Spin interpreter (hub RAM and pin trace compared over that whole interval).
  - Parallax's published P1 RTL under Verilator is the arbiter for hub-slot timing, counters and the video generator.
- **`vfat`/`sd`**: the synthesised volume passes `fsck.vfat -n`, and mounting it read-only on the host reproduces every file byte-exact against the zip.
- **`eeprom`**: I²C transaction tests per the CAT24M01 datasheet.

### Machine level, through the firmware's UART1 console

1. The PIC32 prints its banner (`pinHeck System 2011-2016`, `Game: DOM - DOMINOS`, `Version:`) on the second boot. The firmware prints it only once U13 holds its settings, which the first boot on a blank EEPROM stores. Reaching it needs the Propeller's sync reply and its `readEEPROM`/`writeEEPROM` service; until milestone 5 the test stub `tests/pinheck/pic32mx/linkstub.c` provides them.
2. On blank NVRAM the Propeller's update runs to `PLEASE RESTART` against the bootloader stand-in, programming and verifying the card's `DOM_V006.PRG` and writing its EEPROM record; after a restart `PROPELLER SYNC CHECK` succeeds against the emulated Propeller, the banner appears, and `[E97000]` injected on UART1 returns `Ball Search: DISABLED`. (`[E00000]`, which the help text documents as returning the version, prints an empty line in `DOM_V006`.)
3. `[V00ABC]` for a chosen clip produces display frames equal, pixel for pixel, to that `.VID`'s frames, contiguous and in order from the first frame shown (frame 1, §5.5) through the last. Each equals the firmware framebuffer at hub `$5870` when latched.
4. `[F00ABC]` produces audio whose normalised cross-correlation with the `.wav` is at least 0.95.
5. The service-menu switch, lamp and solenoid tests, `[MXXzzz]` and `[LXXzzz]` drive exactly the PinMAME switches, lamps and solenoids given by §4.5.

## 8. Build order

| # | Milestone | Exit criterion |
|---|---|---|
| 1 | `mips32` + disassembler | QEMU differential suite passes |
| 2 | `pic32mx`, no Propeller | machine check 1 |
| 3 | `p8x32a` + disassembler | spinsim differential suite passes; boots `PRP_V008.BIN` from the EEPROM model |
| 4 | `sd` + `vfat` | volume tests pass; the Propeller mounts the card and opens `_DE/ERR.VID` |
| 5 | Link (§5.2, §5.3) | machine check 2 |
| 6 | Display module model and renderer | machine check 3 |
| 7 | Audio | machine check 4 |
| 8 | Board I/O + PinMAME integration | machine check 5; a game can be started, played and ended |
| 9 | Performance | real time on the reference machine; any JIT/DRC work is driven by profiles |

Milestones 1–2 and 3–4 are independent of each other.

## 9. Open items

Each is resolved by analysis of the shipped firmware before the milestone that needs it. None changes the architecture.

1. **chipKIT logical pins → PIC32 port bits** (resolved by milestone 2; checked by `tests/pinheck/pic32mx/pins.py`). Logical 16 = RF5 = COMM_OUT (PIC32→Propeller data), 15 = RF12 = COMM_CLK_RX (clock, driven by the PIC32), 14 = RF13 = COMM_IN_TX (Propeller→PIC32 data). The link is a full-duplex, bit-banged exchange of 16 bytes, LSB first, byte 15 the command; the Propeller stages its reply from the previous packet. There is no Propeller→PIC32 UART: the firmware enables only UART1.
2. **Display signalling on P16–P22** (resolved by milestone 6; checked by `tests/pinheck/display/`). Not the video generator: a clocked serial frame link, P22 clock, P21 data, P20 latch, P17 config (§5.5).
3. **External WS2801 chain length** (needed by milestone 8). Read it from the firmware's RGB routines. The chain is exposed as further custom solenoids after 61, three per LED.
4. **Mask ROM image** (needed by milestone 3). Fix the exact 32 KB image the `pinheck` BIOS set declares, and its CRC, from the Parallax release. Check how the interpreter region is stored against what the booter expects.

## 10. Out of scope

- High-level emulation of the Propeller.
- The PIC32 bootloader binary itself. Decision D1: the bootloader is emulated at protocol level by `bootldr.c` (§4.4), so the Propeller's update path and `UPDATE CODE` work unmodified. Decision D2: program flash written by an update persists until the next launch, which reloads the romset's `DOM_V006.PRG`; since the card is the same romset zip, the images are normally identical.
- Games other than Domino's, beyond not precluding them.
- Pin-level synthesis of video or audio waveforms.
