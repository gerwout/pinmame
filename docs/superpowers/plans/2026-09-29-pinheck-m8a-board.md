# Milestone 8a: board I/O Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every lamp, coil, switch, cabinet input, GI/flasher, RGB channel, servo and the start lamp of Domino's reaches PinMAME under the numbering of spec §4.5, proven by the firmware's own console commands and service-menu tests in headless `sdl3pinmame` (machine check 5).

**Architecture:** A new pure-C `src/wpc/pinheck/board.c` receives every PIC32 port write with its cycle and answers port reads: it models the lamp matrix, the 24 coil gates behind the CD74HC123 watchdog, the switch matrix, the 74HC165 cabinet chain, the 74HC595 GI chain, both WS2801 chains, the five servo pulse widths and the start lamp, and reports changes through callbacks with PIC32-cycle timestamps. The driver's board block in `pinheck.c` turns those callbacks into PinMAME's PWM-integrated lamp and solenoid outputs and custom solenoids, reads switches from `coreGlobals.swMatrix`, and registers spec §4.5's switch/lamp numbering. `pic32mx.c` keeps the host's cycle counter (`pic32cpu_ICount`) exact inside every board callback, so PinMAME's `timer_get_time()` is exact per edge and `activecpu_abort_timeslice` ends the slice. Machine check 5 drives the console (`[MXXzzz]`, `[LXXzzz]`) and the service menu (`-key_script`), reads PinMAME's own outputs from a per-frame log and the switch test's on-screen grid from the display frame log.

**Tech Stack:** C99 (C89-syntax-clean for the new pure-C file), PinMAME 0.37 core PWM outputs (`core_write_pwm_output*`, `core_getAllPhysicSols`), Python 3 (no third-party modules), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §3.1, §3.2 and the service-test part of §3.4 (binding), parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` §2.2, §4.5, §6, §7 check 5 (binding where the addendum is silent). The playfield simulator, keyboard play and the scripted game (§3.3 and the rest of §3.4) are Plan 8b.

## Prerequisites

- Plans 1–7, the Milestone 6+7 fix pass and the display-API prefix executed and merged: `pinheck` at `bab9dd73` (display decoder API prefixed, `tests/pinheck/link/symbol_check.py` added to the `pic32mx` suite) or later. Proven and replayed on exactly that commit. This plan edits Plan 2's `pic32mx.[ch]`, `pic32mxcpu.c` and `soc_test.c`, Plan 5's `pinheck.c` (a new board block plus one-line hooks, all outside the sound block), `pinheck.h`, `pinheckgames.c`, `core.c`, `libpinmame.h`, `register_build.py` and `makefile_link_check.sh`.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools`; `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip; `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`. Every command below uses these.
- Machine check 5 runs about 22 minutes (the emulation runs at about 0.155× real time: 20 s of first boot, then 180 s of tests). Every PinMAME run below has a `timeout`.

## Global Constraints

- `src/wpc/pinheck/board.[ch]` are pure C: only `<stdint.h>` and `<string.h>`; no PinMAME headers; every public name prefixed `pinheck_board_` (`symbol_check.py` rejects pinHeck functions that are defined elsewhere in `src/`); `extern "C"` guards; C89-syntax-clean (`-std=c89 -pedantic-errors -Wno-long-long`) and warning-free under `-std=c99 -Wall -Wextra -Werror -pedantic`. Code comments stay short.
- Board wiring (schematic, verified against `DOM_V006.PRG`): `PIC_SOL_0..23` = RC2, RC3, RE5, RF3, RE6, RE7, RE8, RC14, RE9, RC4, RA2, RA0, RA1, RA4, RA5, RC13, RG1, RG0, RA6, RA7, RG14, RG12, RG13, RA9; lamp columns RB0–7, rows RB8–15 (both high = lit); switch columns RD8–15 (driven low one at a time), rows RD0–7 (low = closed); `CAB_CLK` RE0, `CAB_LAT` RG8, `CAB_SWITCH_IN` RF0 (low = closed), `CAB_GI_OUT` RG7, `!DATA_LAT` = inverted `CAB_LAT`; `RGB_DATA`/`RGB_CLK` RE1/RE2; `EXT_RGB_DATA`/`EXT_RGB_CLK` RG6/RC1; `SERVO_0..4` RF1, RA10, RF4, RE3, RE4; `ST_LI_GATE` RA3; `PIC_ENABLE` RG15. A pin counts as driven high only when its LAT bit is 1 and its TRIS bit 0.
- Coils: gate = `PIC_SOL_n` AND the CD74HC123 output, which a falling `PIC_ENABLE` (pull-down, so tristating counts) retriggers for 1.125 s (0.45 × 25 kΩ × 100 µF).
- Numbering (spec §4.5): switch/lamp `n` → PinMAME `(n/8+1)*10 + n%8+1`, registered with `MDRV_SWITCH_CONV`/`MDRV_LAMP_CONV`; firmware cabinet switch `n` → PinMAME `n` (1–8) or `n + 82` (9–15), `AUX_6` → 98; coils 0–23 → solenoids 1–24; `GI_0..7` → 25–32, `GI_8..15` → 37–44 (through `core.c`'s System 11 layout, extended to `GEN_PINHECK`); on-board RGB left R, G, B, right R, G, B → 51–56; servos 0–4 → 57–61 (pulse width normalised over 1.0–2.0 ms, clamped); external WS2801 LED 0 R, G, B → 62–64; start lamp → lamp 91. All modulated: `CORE_MODOUT_FORCE_ON`, lamps `CORE_MODOUT_LED` (matrix ×12), coils `CORE_MODOUT_SOL_2_STATE`, GI `CORE_MODOUT_LED`, 51–64 `CORE_MODOUT_NONE` with the driver writing the value. `hw.custSol` = 14, `hw.lampCol` = 1, flipper buttons `FLIP_SWNO(4, 3)`.
- Cabinet keys (PinMAME conventions): coin `5`, start `1`, flippers left/right Shift, tilt `Insert`, coin door `End` (toggle, closed by default), Enter/Menu `0`, Back `7`, User `9`. Matrix switches through PinMAME's own column/row keys (`Q`–`I` + `A`–`K`), active because the machine has no simulator yet.
- PinMAME time: `pic32mx` keeps `*icount` = cycles left in the current `pic32mx_run` across every `port_write`, `port_read`, `uart_tx` and `i2c_pins` callback; a callback that lowers it ends the run at that point.
- Test hooks, inert unless set (as earlier plans'): `PINHECK_OUT_LOG=file` logs board events (`L`, `S`, `G`, `T`, `R`, `V` lines with PinMAME time and PIC32 cycle) and, each frame, the PinMAME lamps and solenoids whose 0–255 level changed (`P` lines); `PINHECK_UART1_SEND_GAP=seconds` sets the pause of each `~` in `PINHECK_UART1_SEND` (default 1 s), and the pauses are now scheduled from the previous send time rather than from when the `~` is reached.
- Every existing suite stays green: `tests/pinheck/{storage,link,pic32mx,display,audio}/check.sh`, `pinmame_check.sh`, `pinmame_display.sh`, `pinmame_audio.sh` (the `mips32` and `p8x32a` suites cover code this plan does not touch).

## Rulings

Decisions the spec left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it).

1. **GI/flasher numbers are the board's `GI_0..15`** (U3 Q0 … U5 Q7, the names spec §2.2 uses), not the firmware's GI word bits. The firmware shifts its word least significant bit first, so word bit *n* is `GI_(15−n)`; the lamp test proves it (`PLAYFIELD GI` 1, 2, … 128 light solenoids 44, 43, … 37; `BACKBOX GI` 0 … 7 light 32 … 25). Board numbering stays valid whatever a future firmware calls its bits.
2. **Cabinet switches**: the firmware reads each cabinet bit after a clock edge, so its cabinet switch *n* is chain input *n* − 1 and matches the switch document (1 Coin Door, 3 Right Flipper, 4 Left Flipper, 6 Enter, 7 Coin Mech, 8 Tilt, 12 Start). PinMAME switch = firmware number for 1–8 (custom column 0) and number + 82 for 9–15 (column 9); U12's serial input (firmware switch 0) is unconnected and modelled high (open); U11 D7 (`AUX_6`, PinMAME 98) is wired but the firmware never reads it.
3. **Start button lamp is lamp 91** (custom lamp column 9, `hw.lampCol` = 1), mirroring the custom switch column 9.
4. **External WS2801 chain: one LED** (spec open item 3), custom solenoids 62–64, `hw.custSol` = 14. The only external-chain writer in `DOM_V006.PRG` (`0x9D02FFEC`, three bytes) is called once, from the top of `loop()` (`0x9D01FD88`), with 255, 255, 255. Further LEDs in a frame are logged once and ignored.
5. **Servos keep spec §4.5's 1.0–2.0 ms normalisation.** The firmware's Servo library uses 544 µs (0°) to 2,400 µs (180°), so the Noid test's two directions read 0 and 255 and positions outside 1–2 ms saturate. For Plan 8b the board also reports a detach (no pulse for 60 ms, three frames) as a pulse of 0; the PinMAME output then holds its last value, as a detached positional servo does.
6. **Lamp integration**: `CORE_MODOUT_LED` with relative brightness 12 for the 64 matrix lamps. The firmware lights a column for 2 of every 24 lamp-timer periods, so ×12 makes a lamp strobed in every frame read 1.0 (the lamp test's `LAMP =` and `ALL ON` read 240–247). At brightness *b* a lamp is lit for exactly *b*/8 of that time (Task 2); PinMAME's LED model boosts short pulses (emission grows with duration^0.75), so the output's median reads 0.169, 0.322, 0.455, 0.580, 0.722, 0.831, 0.949 for *b* = 1–7 (Task 3): seven distinct rising levels.
7. **Coil gating by the watchdog** is modelled (spec §2.2 hardware, CD74HC123 values from the schematic): a coil pin only reaches PinMAME while the one-shot runs.
8. **Enter doubles as Menu.** The board has Enter, Back and User buttons; the switch document labels cabinet switch 6 "Enter (menu)". Keys follow PinMAME's coin-door layout: `0` Enter, `7` Back, `9` User, `End` coin door.
9. **Switch and lamp numbering is registered** (`MDRV_SWITCH_CONV`/`MDRV_LAMP_CONV`, WPC-style column-row). PinMAME's default conversion is sequential: without it the left flipper button (switch 4) lands on matrix switch 3 (found while proving this plan).
10. **Test-console pacing**: `~` now schedules the next command one gap after the previous send (`send_at += gap`), and `PINHECK_UART1_SEND_GAP` sets the gap, so check 5 can place 99 commands in exact 0.25 s windows. Plan 5–7 scripts only use `~` for waits of whole seconds and are unaffected beyond a few milliseconds.
11. **The cycle counter is kept inside `pic32mx.c`** (a host `int *icount` plus the run's end cycle) rather than in PinMAME wrappers, so the carried item is unit-tested without PinMAME (`host_icount` in `soc_test.c`) and proven in PinMAME by check 5's timing step.

## Firmware facts this plan relies on (from `DOM_V006.PRG`, `0x9D000000` flash image)

- Lamp interrupt (timer 2, PR2 = 10,000, 1:1): a tick counter; on tick 2 `LATB = 0`, on tick 3 `LATB = rows << 8 | 1 << column` with row *r* lit when the frame phase (0–7, advancing after column 7) is below the lamp's brightness. `[LXX007]` therefore lights a lamp in 7 of 8 frames.
- Switch interrupt (timer 3, PR3 = 40,000): `LATD = column_word` (starting `0xFEFF`, shifted left once per interrupt, columns 0–7), a short delay (`0x9D03068C`), `rows = ~PORTD`; `LATD = 0` on exit.
- Cabinet/GI bits (`0x9D01A2A8`, the first 16 of the 64-switch loop at `0x9D01A094`): `GI_OUT = word & 1; word >>= 1; CAB_CLK = 1; CAB_CLK = 0; in = in << 1 | !CAB_SWITCH_IN`; then `CAB_LAT = 0; CAB_LAT = 1`.
- `digitalWrite` (`0x9D02B8F4`) does nothing on an input pin; `digitalRead` (`0x9D02DE94`) on PORTB first sets the pin digital in `AD1PCFG`.
- `[MXXzzz]` (`0x9D02E028`): coil on, off-time = loop counter + min(15 × zzz, 255); the counter advances 3 per call of `0x9D019F60`, which also kicks the watchdog. Emulated, `[M16020]` is on for 42.4 ms.
- Service menu (Enter): `TEST: SWITCH EDGE, AUDIO/MUSIC, SOLENOID, SERVO, LAMP, RGB LIGHTING, …`, right flipper = next, left = previous, Enter = select, Back = up. Solenoid test: items 0–23 in coil order (`KNOCKER` … `SOL23`), Enter fires. Servo test: `NOID RIGHT` (servo 0 pulses 544 µs from the moment the item is shown), `NOID STOP` (Enter stops the pulses), `NOID LEFT` (2,400 µs), `TARGET DOWN` … (Enter there starts a sequence that ignores the flippers for a while); the target bank (servo 1) holds 1,631 µs throughout, and leaving the test parks the Noid at 1,477 µs. Pulse widths jitter by up to ±3 µs. Lamp test (right flipper steps): `ALL OFF`, `PLAYFIELD GI` 1 … 128, `BACKBOX GI` 0 … 7, `ALL PL.FIELD GI ON`, `ALL BACKBOX GI ON`, `GI AND LAMPS ALL ON`, `… ALL OFF`, `LAMP =` 0 … 63, `ALL ON`, `ALL OFF`. RGB test: `RGB1 RED/GREEN/BLUE/WHITE`, `RGB2 …` (RGB1 = left). Switch test: an 8 × 8 grid (column 0 drawn on the right, 4-pixel cells, lit cell = closed) plus a 2 × 8 cabinet column (0–7 right, 8–15 left) in the frame's left 40 pixels.

## Review Focus

- **Coil pins set while the watchdog is not running** (the firmware stopped kicking `PIC_ENABLE`, or tristated it: the pull-down makes that a falling edge): no coil may reach PinMAME, and a running one-shot drops every coil exactly 1.125 s after the last kick. `sols_watchdog` in Task 2 (expiry to the cycle, and a `PIC_ENABLE` that is an input never arms it).
- **Several board edges inside one PinMAME CPU slice, and slices a callback aborts.** Each edge must get its own `timer_get_time()` (PinMAME's PWM integration timestamps every lamp and coil flip with it), and an `activecpu_abort_timeslice` from a callback must end the slice there. `host_icount` in Task 1; check 5's `timing` step in Task 3 (every pair of lamp edges less than 1 ms apart, about 75,000 in the first boot, gets the PinMAME time of its own cycle).
- **All switch columns driven at once** (the firmware leaves `LATD = 0` between scan interrupts) and a column pin turned into an input: rows must read the OR of every driven column's closed switches, an input column contributes nothing. `switch_scan` in Task 2.
- **WS2801 frames longer than the chain**: at boot the firmware clocks 96 bits into the two on-board chips in one latch window. Only the first 24 bits per chip count, in chain order (U38 = left first); extra LEDs are ignored and logged once. `ws2801` in Task 2 (LED order and latch timing), check 5's RGB test in Task 3 (the six channels after the boot frames are exactly the test's colours).
- **Servo pulses that stop** (the Noid test's `NOID STOP` detaches the servo): a detach is reported once, 60 ms after the last rising edge, and the PinMAME value holds. `servos` in Task 2; check 5's servo steps in Task 3.

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/pic32mx/pic32mx.[ch]` | host cycle counter across board callbacks; PORTB reads masked by `AD1PCFG`; byte/halfword writes to `PORTx`/`TMRx` merge with `LATx`/the live timer; I²C write collisions (modified) |
| `src/cpu/pic32mx/pic32mxcpu.c` | hands `pic32cpu_ICount` to the SoC (modified) |
| `tests/pinheck/pic32mx/soc_test.c` | unit tests for the above (modified) |
| `src/wpc/pinheck/board.[ch]` | pure-C board logic: port writes/reads in, timestamped lamp, coil, GI, start-lamp, RGB and servo callbacks out |
| `tests/pinheck/board/board_test.c`, `check.sh`, `.gitignore` | board unit tests driven by the firmware's own port sequences; libpinmame check |
| `src/wpc/pinheck.c` | board block: callbacks to PinMAME outputs, switch reads, numbering, cabinet inputs, `PINHECK_OUT_LOG`, `PINHECK_UART1_SEND_GAP` (modified) |
| `src/wpc/pinheck.h`, `src/wpc/pinheckgames.c` | cabinet input port, flipper switches, custom solenoids, lamp column (modified) |
| `src/wpc/core.c` | solenoids 37–44 for `GEN_PINHECK` (modified) |
| `src/libpinmame/libpinmame.h` | `PINMAME_HARDWARE_GEN_PINHECK` (modified) |
| `tests/pinheck/link/register_build.py`, build files, `tests/pinheck/pic32mx/makefile_link_check.sh` | `board.c` in every build (modified / regenerated) |
| `tests/pinheck/board/check5.py`, `pinmame_board.sh` | machine check 5 |
| `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md`, `2026-09-29-pinheck-m8-m9-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | verified wiring and numbering, open item 3, Plan 8b notes (modified) |

---
### Task 1: SoC carried items

**Files:**
- Modify: `src/cpu/pic32mx/pic32mx.h`, `src/cpu/pic32mx/pic32mx.c`, `src/cpu/pic32mx/pic32mxcpu.c`
- Test: `tests/pinheck/pic32mx/soc_test.c`

**Interfaces:**
- Consumes: Plan 2's `pic32mx` SoC (`pic32mx_run`, `pic32mx_board` callbacks) and `soc_test.c` helpers (`setup`, `rd`, `wr`, `put`, `rec`, `CHECK`).
- Produces: `pic32mx.icount` (`int *`, NULL = unused) and `pic32mx.run_end` (`uint64_t`): inside every `port_write`, `port_read`, `uart_tx` and `i2c_pins` callback `*icount` equals `run_end − cpu.cycles`; a callback that lowers `*icount` ends the run at that point (`run_end = cycles + max(*icount, 0)`); on return `*icount = run_end − cycles` (≤ 0). `pic32cpu_execute` passes `&pic32cpu_ICount`. PORTB input bits read 0 unless set in `AD1PCFG` (`0xBF809060`). Sub-word writes to `PORTx` merge with `LATx`, to `TMRx` with the running count. An I²C command bit written while a command is pending is cleared and sets `IWCOL` (`I2CxSTAT` bit 7); a `I2CxTRN` write while pending sets `IWCOL` and sends nothing.

- [ ] **Step 1: Write the failing behaviour tests**

`port_b_analog` (PORTB reads per `AD1PCFG`), `port_subword` (byte and halfword writes to `PORTB` land in `LATB`), `timer_subword` (a byte write to `TMR1` keeps the counted low byte), `i2c_collision` (a stop written during a start is cleared, `IWCOL` set, the start completes; a `I2CxTRN` write during a stop is refused with `IWCOL`), and `port_input_mix` now makes PORTB digital first:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/pic32mx/soc_test.c', [
    (r'''	int err = 0;
	soc.cpu.bus.write(soc.cpu.bus.ctx, va & 0x1FFFFFFFu, v, 4, &err);
}

''',
     r'''	int err = 0;
	soc.cpu.bus.write(soc.cpu.bus.ctx, va & 0x1FFFFFFFu, v, 4, &err);
}

static void wrs(uint32_t va, uint32_t v, int size)
{
	int err = 0;
	soc.cpu.bus.write(soc.cpu.bus.ctx, va & 0x1FFFFFFFu, v, size, &err);
}

'''),
    (r'''{
	setup();
	wr(0xBF886040u, 0x00F0);
	wr(0xBF886060u, 0xFFFF);
	rec.port_in = 0x0050;
	CHECK(rd(0xBF886050u) == 0xFF5F);
}

''',
     r'''{
	setup();
	wr(0xBF809060u, 0xFFFF);
	wr(0xBF886040u, 0x00F0);
	wr(0xBF886060u, 0xFFFF);
	rec.port_in = 0x0050;
	CHECK(rd(0xBF886050u) == 0xFF5F);
}

static void port_b_analog(void)
{
	setup();
	wr(0xBF886040u, 0xFFFF);
	rec.port_in = 0x00FF;
	CHECK(rd(0xBF886050u) == 0);
	wr(0xBF809060u, 0x000F);
	CHECK(rd(0xBF886050u) == 0x000F);
	wr(0xBF886040u, 0x00FF);
	wr(0xBF886060u, 0xFF00);
	CHECK(rd(0xBF886050u) == 0xFF0F);
}

static void port_subword(void)
{
	setup();
	wr(0xBF886040u, 0);
	wr(0xBF886060u, 0xFF00);
	wrs(0xBF886050u, 0x12, 1);
	CHECK(rd(0xBF886060u) == 0xFF12);
	wrs(0xBF886051u, 0x34, 1);
	CHECK(rd(0xBF886060u) == 0x3412);
	wrs(0xBF886052u, 0xABCD, 2);
	CHECK(rd(0xBF886060u) == 0xABCD3412u);
	CHECK(rec.port_last == PIC32MX_PORTB && rec.port_lat == 0xABCD3412u);
}

static void timer_subword(void)
{
	uint32_t t;
	setup();
	wr(0xBF800620u, 0xFFFF);
	wr(0xBF800600u, 0x8000);
	pic32mx_run(&soc, 0x1234);
	t = rd(0xBF800610u);
	CHECK(t > 0x100);
	wrs(0xBF800611u, 0x56, 1);
	CHECK(rd(0xBF800610u) == (0x5600u | (t & 0xFFu)));
}

'''),
    (r'''}

static void unmapped_sfr(void)
{
''',
     r'''}

static void i2c_collision(void)
{
	setup();
	wr(0xBF805340u, 10);
	wr(0xBF805300u, 0x8000);
	wr(0xBF805308u, 1u);
	wr(0xBF805308u, 4u);
	CHECK((rd(0xBF805300u) & 0x1Fu) == 1u);
	CHECK(rd(0xBF805310u) & (1u << 7));
	pic32mx_run(&soc, 30);
	CHECK(!(rd(0xBF805300u) & 1u));
	wr(0xBF805314u, 1u << 7);
	wr(0xBF805308u, 4u);
	wr(0xBF805350u, 0xA0);
	CHECK(rd(0xBF805310u) & (1u << 7));
	CHECK(!(rd(0xBF805310u) & (1u << 14)));
	pic32mx_run(&soc, 30);
	CHECK(!(rd(0xBF805300u) & 4u));
}

static void unmapped_sfr(void)
{
'''),
    (r'''	port_set_clr_inv();
	port_input_mix();
	timer_period();
	timer_prescale_pbdiv();
	timer_t32();
	intc_priority();
	core_timer_level();
	uart_tx_rx();
	i2c_timing_nack();
	unmapped_sfr();
	reserved_instruction();
''',
     r'''	port_set_clr_inv();
	port_input_mix();
	port_b_analog();
	port_subword();
	timer_period();
	timer_prescale_pbdiv();
	timer_t32();
	timer_subword();
	intc_priority();
	core_timer_level();
	uart_tx_rx();
	i2c_timing_nack();
	i2c_collision();
	unmapped_sfr();
	reserved_instruction();
'''),
])
EOF
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `timeout 900 tests/pinheck/pic32mx/check.sh 2>&1 | grep -E 'FAIL|failed'`
Expected (the suite exits 1):
```text
SOC FAIL soc_test.c:85: rd(0xBF886050u) == 0
SOC FAIL soc_test.c:87: rd(0xBF886050u) == 0x000F
SOC FAIL soc_test.c:90: rd(0xBF886050u) == 0xFF0F
SOC FAIL soc_test.c:99: rd(0xBF886060u) == 0xFF12
SOC FAIL soc_test.c:101: rd(0xBF886060u) == 0x3412
SOC FAIL soc_test.c:103: rd(0xBF886060u) == 0xABCD3412u
SOC FAIL soc_test.c:104: rec.port_last == PIC32MX_PORTB && rec.port_lat == 0xABCD3412u
SOC FAIL soc_test.c:117: rd(0xBF800610u) == (0x5600u | (t & 0xFFu))
SOC FAIL soc_test.c:245: (rd(0xBF805300u) & 0x1Fu) == 1u
SOC FAIL soc_test.c:246: rd(0xBF805310u) & (1u << 7)
SOC FAIL soc_test.c:252: rd(0xBF805310u) & (1u << 7)
soc: FAIL
pic32mx: 1 failed
```

- [ ] **Step 3: Write the cycle-counter test**

`host_icount` runs a loop of `LATASET`/`LATACLR` stores: the counter must equal `1000 − cycle` at each write, and zeroing it in the third write must end the run there.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/pic32mx/soc_test.c', [
    (r'''}

int main(void)
{
''',
     r'''}

static struct { int n, abort_at, seen[4]; uint64_t at[4]; } host;

static void b_host_write(void *c, int port, uint32_t lat, uint32_t tris, uint64_t cy)
{
	(void)c; (void)port; (void)lat; (void)tris;
	if (host.n < 4) { host.seen[host.n] = *soc.icount; host.at[host.n] = cy; }
	if (++host.n == host.abort_at) *soc.icount = 0;
}

/* LATASET, LATACLR, loop: a port write every few cycles */
static void host_setup(int *ic, int abort_at)
{
	setup();
	put(0x1000, 0x3C08BF88u); put(0x1004, 0x34090001u); put(0x1008, 0xAD096028u);
	put(0x100C, 0xAD096024u); put(0x1010, 0x1000FFFDu); put(0x1014, 0x00000000u);
	soc.board.port_write = b_host_write;
	soc.icount = ic;
	memset(&host, 0, sizeof(host));
	host.abort_at = abort_at;
}

/* The host's cycle counter is exact inside each board callback, and zeroing it there ends the run. */
static void host_icount(void)
{
	int ic, ran, i;
	host_setup(&ic, 0);
	ic = 1000;
	ran = pic32mx_run(&soc, 1000);
	CHECK(host.n > 4 && ran >= 1000 && ic == 1000 - ran);
	for (i = 0; i < 4; i++) CHECK(host.seen[i] == 1000 - (int)host.at[i]);
	CHECK(host.seen[0] > host.seen[1] && host.seen[1] > host.seen[2]);
	host_setup(&ic, 3);
	ic = 5000;
	ran = pic32mx_run(&soc, 5000);
	CHECK(host.n == 3 && ic <= 0 && ran <= (int)host.at[2] + 4);
}

int main(void)
{
'''),
    (r'''	reserved_instruction();
	board_hold();
	printf("soc: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
''',
     r'''	reserved_instruction();
	board_hold();
	host_icount();
	printf("soc: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
'''),
])
EOF
```

- [ ] **Step 4: Run it and confirm it fails**

Run: `timeout 900 tests/pinheck/pic32mx/check.sh 2>&1 | grep -E 'error|failed' | head -3`
Expected (the suite exits 2; the SoC has no counter yet):
```text
soc_test.c:299:51: error: ‘pic32mx’ has no member named ‘icount’
soc_test.c:300:44: error: ‘pic32mx’ has no member named ‘icount’
soc_test.c:310:12: error: ‘pic32mx’ has no member named ‘icount’
```

- [ ] **Step 5: Implement**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/pic32mx/pic32mx.h', [
    (r'''	uint64_t exc_count;
	uint8_t logged[PIC32MX_SFR_SIZE / 16];
} pic32mx;

''',
     r'''	uint64_t exc_count;
	uint8_t logged[PIC32MX_SFR_SIZE / 16];
	int *icount;      /* host cycle counter, kept current across board callbacks; NULL = none */
	uint64_t run_end; /* cycle at which the current pic32mx_run ends */
} pic32mx;

'''),
])
edit('src/cpu/pic32mx/pic32mx.c', [
    (r'''#define OFF_RCON        0x0F600u
#define OFF_TRISA       0x86000u
#define INTCON_MVEC     (1u << 12)
#define CON_ON          (1u << 15)
''',
     r'''#define OFF_RCON        0x0F600u
#define OFF_TRISA       0x86000u
#define OFF_AD1PCFG     0x09060u
#define INTCON_MVEC     (1u << 12)
#define CON_ON          (1u << 15)
'''),
    (r'''}

static void uart_write(pic32mx *p, int u, uint32_t reg, uint32_t old, uint32_t v)
{
	uint32_t base = uart_base[u];

	if (reg == 0x20) {
		if ((SFR(p, base) & CON_ON) && (SFR(p, base + 0x10) & (1u << 10))) {
			if (p->board.uart_tx) p->board.uart_tx(p->board.ctx, u + 1, (uint8_t)v, p->cpu.cycles);
			pic32mx_set_irq(p, uart_irq[u] + 2);
		}
''',
     r'''}

/* the host's cycle counter is current inside board callbacks; lowering it ends the run there */
static void host_enter(pic32mx *p)
{
	if (p->icount) *p->icount = (int)((int64_t)p->run_end - (int64_t)p->cpu.cycles);
}

static void host_leave(pic32mx *p)
{
	int left;
	if (!p->icount) return;
	left = (int)((int64_t)p->run_end - (int64_t)p->cpu.cycles);
	if (*p->icount >= left) return;
	p->run_end = p->cpu.cycles + (uint64_t)(*p->icount > 0 ? *p->icount : 0);
	p->cpu.stop = 1;
}

static void uart_write(pic32mx *p, int u, uint32_t reg, uint32_t old, uint32_t v)
{
	uint32_t base = uart_base[u];

	if (reg == 0x20) {
		if ((SFR(p, base) & CON_ON) && (SFR(p, base + 0x10) & (1u << 10))) {
			if (p->board.uart_tx) { host_enter(p); p->board.uart_tx(p->board.ctx, u + 1, (uint8_t)v, p->cpu.cycles); host_leave(p); }
			pic32mx_set_irq(p, uart_irq[u] + 2);
		}
'''),
    (r'''static int i2c_pins(pic32mx *p, int m, int scl, int sda)
{
	return p->board.i2c_pins ? p->board.i2c_pins(p->board.ctx, m + 1, scl, sda, p->cpu.cycles) : sda;
}

''',
     r'''static int i2c_pins(pic32mx *p, int m, int scl, int sda)
{
	int r;
	if (!p->board.i2c_pins) return sda;
	host_enter(p);
	r = p->board.i2c_pins(p->board.ctx, m + 1, scl, sda, p->cpu.cycles);
	host_leave(p);
	return r;
}

'''),
    (r'''	int i, b, ack;

	if (!(con & CON_ON) || p->i2c[m].pending) return;
	if (reg == 0x50) {
		for (i = 7; i >= 0; i--) {
''',
     r'''	int i, b, ack;

	if (!(con & CON_ON)) return;
	if (p->i2c[m].pending) {
		uint32_t col = reg == 0x00 ? v & 0x1Fu & ~p->i2c[m].con_clear : reg == 0x50 ? 1u : 0;
		if (reg == 0x00) SFR(p, base) &= ~col;
		if (col) SFR(p, base + 0x10) |= 1u << 7;
		return;
	}
	if (reg == 0x50) {
		for (i = 7; i >= 0; i--) {
'''),
    (r'''	uint32_t reg = (off - OFF_TRISA) & 0x30u, tris = SFR(p, OFF_TRISA + (uint32_t)port * 0x40);
	if (reg == 0x10) {
		uint32_t in = p->board.port_read ? p->board.port_read(p->board.ctx, port, p->cpu.cycles) : 0xFFFFu;
		return (SFR(p, OFF_TRISA + (uint32_t)port * 0x40 + 0x20) & ~tris) | (in & tris);
	}
''',
     r'''	uint32_t reg = (off - OFF_TRISA) & 0x30u, tris = SFR(p, OFF_TRISA + (uint32_t)port * 0x40);
	if (reg == 0x10) {
		uint32_t in = 0xFFFFu;
		if (p->board.port_read) { host_enter(p); in = p->board.port_read(p->board.ctx, port, p->cpu.cycles); host_leave(p); }
		if (port == PIC32MX_PORTB) in &= SFR(p, OFF_AD1PCFG);
		return (SFR(p, OFF_TRISA + (uint32_t)port * 0x40 + 0x20) & ~tris) | (in & tris);
	}
'''),
    (r'''
	if (((off - OFF_TRISA) & 0x30u) == 0x30 || old == v) return;
	if (p->board.port_write)
		p->board.port_write(p->board.ctx, port, SFR(p, base + 0x20), SFR(p, base), p->cpu.cycles);
}

''',
     r'''
	if (((off - OFF_TRISA) & 0x30u) == 0x30 || old == v) return;
	if (!p->board.port_write) return;
	host_enter(p);
	p->board.port_write(p->board.ctx, port, SFR(p, base + 0x20), SFR(p, base), p->cpu.cycles);
	host_leave(p);
}

'''),
    (r'''}

static void bus_write(void *ctx, uint32_t pa, uint32_t data, int size, int *err)
{
''',
     r'''}

static uint32_t merge_base(pic32mx *p, uint32_t reg)
{
	int i;
	uint32_t sub;
	if (reg >= OFF_TRISA && reg < OFF_TRISA + PIC32MX_PORTS * 0x40 && (reg & 0x30u) == 0x10) return SFR(p, reg + 0x10);
	if (timer_reg(reg, &i, &sub) && sub == 0x10) return timer_read(p, i, sub);
	return SFR(p, reg);
}

static void bus_write(void *ctx, uint32_t pa, uint32_t data, int size, int *err)
{
'''),
    (r'''		if (size < 4) {
			uint32_t mask = ((1u << (size * 8)) - 1) << sh;
			if (!(off & 0xCu)) data = (SFR(p, off & ~0xFu) & ~mask) | ((data << sh) & mask);
			else data = (data << sh) & mask;
		}
''',
     r'''		if (size < 4) {
			uint32_t mask = ((1u << (size * 8)) - 1) << sh;
			if (!(off & 0xCu)) data = (merge_base(p, off & ~0xFu) & ~mask) | ((data << sh) & mask);
			else data = (data << sh) & mask;
		}
'''),
    (r'''int pic32mx_run(pic32mx *p, int cycles)
{
	uint64_t start = p->cpu.cycles, end = start + (uint64_t)(cycles > 0 ? cycles : 0);

	while (p->cpu.cycles < end) {
		uint64_t slice = end - p->cpu.cycles, ev = next_timer_event(p), e2 = next_i2c_event(p);
		uint64_t h = p->board.hold ? p->board.hold(p->board.ctx, p->cpu.cycles) : 0;
		if (h) {
''',
     r'''int pic32mx_run(pic32mx *p, int cycles)
{
	uint64_t start = p->cpu.cycles;

	p->run_end = start + (uint64_t)(cycles > 0 ? cycles : 0);
	while (p->cpu.cycles < p->run_end) {
		uint64_t slice = p->run_end - p->cpu.cycles, ev = next_timer_event(p), e2 = next_i2c_event(p);
		uint64_t h = p->board.hold ? p->board.hold(p->board.ctx, p->cpu.cycles) : 0;
		if (h) {
'''),
    (r'''		if (mips32_timer_irq(&p->cpu)) pic32mx_set_irq(p, 0);
	}
	return (int)(p->cpu.cycles - start);
}
''',
     r'''		if (mips32_timer_irq(&p->cpu)) pic32mx_set_irq(p, 0);
	}
	host_enter(p);
	return (int)(p->cpu.cycles - start);
}
'''),
])
edit('src/cpu/pic32mx/pic32mxcpu.c', [
    (r'''	pic32cpu_ICount = cycles;
	if (!soc.flash) return cycles;
	while (pic32cpu_ICount > 0) pic32cpu_ICount -= pic32mx_run(&soc, pic32cpu_ICount);
	return cycles - pic32cpu_ICount;
}
''',
     r'''	pic32cpu_ICount = cycles;
	if (!soc.flash) return cycles;
	soc.icount = &pic32cpu_ICount;
	pic32mx_run(&soc, cycles);
	return cycles - pic32cpu_ICount;
}
'''),
])
EOF
```

- [ ] **Step 6: Run the suite and confirm it passes**

Run: `timeout 900 tests/pinheck/pic32mx/check.sh 2>&1 | tail -4`
Expected:
```text
makefile link: ok
symbols: ok
vcxproj: ok
pic32mx: 0 failed
```

- [ ] **Step 7: Commit**

```bash
git add src/cpu/pic32mx/pic32mx.h src/cpu/pic32mx/pic32mx.c src/cpu/pic32mx/pic32mxcpu.c tests/pinheck/pic32mx/soc_test.c
git commit -m "pic32mx: exact host cycle counter in board callbacks, AD1PCFG, sub-word PORT/TMR writes, I2C collisions"
```

### Task 2: Board logic

**Files:**
- Create: `src/wpc/pinheck/board.h`, `src/wpc/pinheck/board.c`
- Test: `tests/pinheck/board/board_test.c`, `tests/pinheck/board/check.sh`, `tests/pinheck/board/.gitignore`

**Interfaces:**
- Consumes: nothing (pure C).
- Produces (`board.h`):
  - `pinheck_board_io { void *ctx; uint8_t (*sw_col)(ctx, int col); uint16_t (*cabinet)(ctx); void (*lamps)(ctx, uint64_t t, uint8_t cols, uint8_t rows); void (*sols)(ctx, uint64_t t, uint32_t sols); void (*gi)(ctx, uint64_t t, uint16_t gi); void (*start_lamp)(ctx, uint64_t t, int on); void (*rgb)(ctx, uint64_t t, int chain, int led, uint8_t r, uint8_t g, uint8_t b); void (*servo)(ctx, uint64_t t, int servo, uint32_t pulse); }` — `sw_col` returns the closed rows of switch column 0–7; `cabinet` returns the closed cabinet inputs, bits 0–7 U12 D0–D7, bits 8–15 U11 D0–D7; `sols` bit *n* = coil *n* after the watchdog gate; `gi` bit *k* = `GI_k`; `chain` is `BOARD_RGB_ONBOARD` or `BOARD_RGB_EXTERNAL`; `pulse` in PIC32 cycles, 0 = detached. Every `t` is the PIC32 cycle of the event.
  - `void pinheck_board_init(pinheck_board *b, const pinheck_board_io *io, uint32_t hz)` (all pins inputs), `void pinheck_board_port(pinheck_board *b, int port, uint32_t lat, uint32_t tris, uint64_t t)` (every port write, port 0 = A … 6 = G), `uint32_t pinheck_board_read(pinheck_board *b, int port, uint64_t t)` (the board's drive of an input port, 1 where nothing pulls low), `void pinheck_board_tick(pinheck_board *b, uint64_t t)` (time-based events: watchdog expiry, WS2801 latch after 500 µs, servo detach after 60 ms).
  - `BOARD_SOLS` 24, `BOARD_SERVOS` 5, `BOARD_RGB_MAX` 16 LEDs per chain, fields `sols`, `gi`, `ws_leds[chain]` readable by the driver.

The tests replay the firmware's own sequences: the lamp interrupt with its tick counter and frame phase, the 16-bit cabinet/GI loop, the switch scan's `0xFEFF << c` column words, the WS2801 byte writer.

- [ ] **Step 1: Write the failing tests**

`tests/pinheck/board/board_test.c`:

```c
#include "board.h"
#include <stdio.h>
#include <string.h>

#define HZ 80000000u
#define TICK 10001u /* lamp timer period: PR2 = 10000 at 80 MHz */
enum { PA, PB, PC, PD, PE, PF, PG };

static pinheck_board b;
static uint64_t now;
static uint32_t lat[BOARD_PORTS], tris[BOARD_PORTS];
static uint8_t closed[8];
static uint16_t cab;
static int fails;
static struct {
	int lamps, sols, gi, start, rgb, servo;
	uint8_t cols, rows;
	uint32_t sol;
	uint16_t gi_v;
	int start_v;
	uint64_t t;
	int rgb_chain[8], rgb_led[8];
	uint8_t rgb_v[8][3];
	uint64_t rgb_t;
	int servo_n;
	uint32_t pulse;
} rec;

#define CHECK(c) do { if (!(c)) { printf("BOARD FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t io_sw_col(void *c, int col) { (void)c; return closed[col]; }
static uint16_t io_cabinet(void *c) { (void)c; return cab; }
static void io_lamps(void *c, uint64_t t, uint8_t cols, uint8_t rows) { (void)c; rec.lamps++; rec.cols = cols; rec.rows = rows; rec.t = t; }
static void io_sols(void *c, uint64_t t, uint32_t s) { (void)c; rec.sols++; rec.sol = s; rec.t = t; }
static void io_gi(void *c, uint64_t t, uint16_t g) { (void)c; rec.gi++; rec.gi_v = g; rec.t = t; }
static void io_start(void *c, uint64_t t, int on) { (void)c; rec.start++; rec.start_v = on; rec.t = t; }
static void io_rgb(void *c, uint64_t t, int chain, int led, uint8_t r, uint8_t g, uint8_t bl)
{
	(void)c;
	if (rec.rgb < 8) {
		rec.rgb_chain[rec.rgb] = chain; rec.rgb_led[rec.rgb] = led;
		rec.rgb_v[rec.rgb][0] = r; rec.rgb_v[rec.rgb][1] = g; rec.rgb_v[rec.rgb][2] = bl;
	}
	rec.rgb++;
	rec.rgb_t = t;
}
static void io_servo(void *c, uint64_t t, int s, uint32_t pulse) { (void)c; rec.servo++; rec.servo_n = s; rec.pulse = pulse; rec.t = t; }

static void reset(void)
{
	pinheck_board_io io = { NULL, io_sw_col, io_cabinet, io_lamps, io_sols, io_gi, io_start, io_rgb, io_servo };
	pinheck_board_init(&b, &io, HZ);
	memset(&rec, 0, sizeof(rec));
	memset(lat, 0, sizeof(lat));
	memset(tris, 0, sizeof(tris));
	memset(closed, 0, sizeof(closed));
	tris[PD] = 0x00FFu;
	tris[PF] = 0x0001u;
	cab = 0;
	now = 1000;
}

static void write_port(int port, uint32_t v)
{
	lat[port] = v;
	pinheck_board_port(&b, port, lat[port], tris[port], now);
}

static void pin(int port, int bit, int v)
{
	write_port(port, v ? lat[port] | (1u << bit) : lat[port] & ~(1u << bit));
}

static void step(int port, int bit, int v, uint64_t dt)
{
	pin(port, bit, v);
	now += dt;
}

/* The firmware's T2 lamp interrupt, one call per timer period (DOM_V006 0x9D026470). */
static struct { int tick, col, phase; uint8_t level[64]; } fw;

static void fw_lamp_isr(void)
{
	int r;
	if (++fw.tick == 2) write_port(PB, 0);
	else if (fw.tick == 3) {
		uint32_t rows = 0;
		fw.tick = 0;
		for (r = 0; r < 8; r++)
			if (fw.phase < fw.level[fw.col * 8 + r]) rows |= 1u << r;
		write_port(PB, rows << 8 | 1u << fw.col);
		if (++fw.col == 8) { fw.col = 0; fw.phase = (fw.phase + 1) & 7; }
	}
	now += TICK;
}

/* Lamp 21 (column 2, row 5) at each brightness 0-7: lit for exactly level x 2 periods per 8 frames. */
static void lamps_levels(void)
{
	int level, i;
	for (level = 0; level < 8; level++) {
		uint64_t on = 0, since = 0;
		int lit = 0, other = 0;
		reset();
		memset(&fw, 0, sizeof(fw));
		fw.level[21] = (uint8_t)level;
		fw.level[20] = 7;
		for (i = 0; i < 8 * 24; i++) {
			int was = rec.lamps;
			fw_lamp_isr();
			if (rec.lamps == was) continue;
			if (lit) { on += rec.t - since; lit = 0; }
			if ((rec.cols & 4u) && (rec.rows & 0x20u)) { lit = 1; since = rec.t; }
			if ((rec.cols & ~4u) && rec.rows) other = 1;
		}
		CHECK(on == (uint64_t)level * 2 * TICK);
		CHECK(!other);
	}
	reset();
	tris[PB] = 0xFFFFu;
	write_port(PB, 0xFFFFu);
	CHECK(rec.lamps == 0);
}

static const char *sol_pins[BOARD_SOLS] = {
	"C2", "C3", "E5", "F3", "E6", "E7", "E8", "C14", "E9", "C4", "A2", "A0",
	"A1", "A4", "A5", "C13", "G1", "G0", "A6", "A7", "G14", "G12", "G13", "A9"
};

static void kick(void)
{
	step(PG, 15, 1, 10);
	step(PG, 15, 0, 10);
}

/* Coils need the HC123 watchdog: a falling PIC_ENABLE (RG15) arms it for 0.45 x 25k x 100u = 1.125 s. */
static void sols_watchdog(void)
{
	int i;
	uint64_t t0;
	reset();
	step(PG, 1, 1, 10);
	CHECK(rec.sols == 0);
	t0 = now;
	kick();
	CHECK(rec.sols == 1 && rec.sol == 1u << 16 && rec.t == t0 + 10);
	pinheck_board_tick(&b, t0 + 10 + HZ / 8 * 9 - 1);
	CHECK(rec.sols == 1);
	pinheck_board_tick(&b, t0 + 10 + HZ / 8 * 9);
	CHECK(rec.sols == 2 && rec.sol == 0 && rec.t == t0 + 10 + HZ / 8 * 9);
	now = t0 + 10 + HZ;
	kick();
	CHECK(rec.sols == 3 && rec.sol == 1u << 16);
	step(PG, 1, 0, 10);
	CHECK(rec.sols == 4 && rec.sol == 0);
	for (i = 0; i < BOARD_SOLS; i++) {
		int port = sol_pins[i][0] - 'A', bit = sol_pins[i][1] - '0';
		if (sol_pins[i][2]) bit = bit * 10 + sol_pins[i][2] - '0';
		step(port, bit, 1, 10);
		CHECK(rec.sol == 1u << i);
		step(port, bit, 0, 10);
		CHECK(rec.sol == 0);
	}
	reset();
	tris[PG] = 1u << 15;
	kick();
	step(PG, 1, 1, 10);
	CHECK(rec.sols == 0);
}

/* The firmware's cabinet scan (DOM_V006 0x9D01A2A8): per bit, GI data out, CAB_CLK up and down,
   then read CAB_SWITCH_IN (low = closed); after 16 bits CAB_LAT low then high. */
static uint16_t fw_cab_bits(uint16_t gi)
{
	uint16_t in = 0;
	int i;
	for (i = 0; i < 16; i++) {
		in = (uint16_t)(in << 1);
		step(PG, 7, gi & 1u, 10);
		gi >>= 1;
		step(PE, 0, 1, 10);
		step(PE, 0, 0, 10);
		if (!(pinheck_board_read(&b, PF, now) & 1u)) in |= 1u;
	}
	return in;
}

static uint16_t fw_cab_scan(uint16_t gi)
{
	uint16_t in = fw_cab_bits(gi);
	step(PG, 8, 0, 10);
	step(PG, 8, 1, 10);
	return in;
}

/* Cabinet input i (U12 D0-7, U11 D0-7) is firmware cabinet switch i+1; U11 D7 is never read
   and switch 0 reads U12's unconnected serial input (open). */
static void cabinet_chain(void)
{
	int i, k;
	for (i = 0; i < 16; i++) {
		uint16_t in;
		reset();
		cab = (uint16_t)(1u << i);
		fw_cab_scan(0);
		in = fw_cab_scan(0);
		for (k = 0; k < 16; k++) CHECK(((in >> k) & 1u) == (unsigned)(i < 15 && k == i + 1));
	}
	reset();
	cab = 0x0001u;
	CHECK((pinheck_board_read(&b, PF, now) & 1u) == 1u);
	cab = 0x8000u;
	CHECK((pinheck_board_read(&b, PF, now) & 1u) == 0u);
}

/* GI word bit n (shifted first) ends in U5 Q7: GI_(15-n), latched when CAB_LAT falls. */
static void gi_chain(void)
{
	int n;
	for (n = 0; n < 16; n++) {
		reset();
		step(PG, 8, 1, 10);
		fw_cab_bits((uint16_t)(1u << n));
		CHECK(rec.gi == 0);
		step(PG, 8, 0, 10);
		CHECK(rec.gi == 1 && rec.gi_v == (uint16_t)(0x8000u >> n));
		step(PG, 8, 1, 10);
		CHECK(rec.gi == 1);
	}
	reset();
	step(PG, 8, 1, 10);
	fw_cab_scan(0x00F1u);
	CHECK(rec.gi == 1 && rec.gi_v == 0x8F00u);
	fw_cab_scan(0x00F1u);
	CHECK(rec.gi == 1);
	step(PG, 8, 0, 10);
	CHECK(rec.gi == 1);
}

static void ws_bytes(int dport, int dbit, int cport, int cbit, const uint8_t *v, int n)
{
	int i, k;
	for (i = 0; i < n; i++)
		for (k = 7; k >= 0; k--) {
			step(cport, cbit, 0, 5);
			step(dport, dbit, (v[i] >> k) & 1, 5);
			step(cport, cbit, 1, 5);
		}
}

/* WS2801: MSB first on the rising clock, 24 bits per LED in chain order, latched after 500 us low. */
static void ws2801(void)
{
	static const uint8_t two[6] = { 0x12, 0x34, 0x56, 0xFF, 0x00, 0x80 }, one[3] = { 0x01, 0x80, 0x7E };
	uint64_t last;
	reset();
	ws_bytes(PE, 1, PE, 2, two, 6);
	step(PE, 2, 0, 5);
	last = now - 10;
	pinheck_board_tick(&b, last + HZ / 2000);
	CHECK(rec.rgb == 0);
	pinheck_board_tick(&b, last + HZ / 2000 + 1);
	CHECK(rec.rgb == 2 && rec.rgb_t == last + HZ / 2000 && b.ws_leds[BOARD_RGB_ONBOARD] == 2);
	CHECK(rec.rgb_chain[0] == BOARD_RGB_ONBOARD && rec.rgb_led[0] == 0 && rec.rgb_v[0][0] == 0x12 && rec.rgb_v[0][1] == 0x34 && rec.rgb_v[0][2] == 0x56);
	CHECK(rec.rgb_led[1] == 1 && rec.rgb_v[1][0] == 0xFF && rec.rgb_v[1][1] == 0x00 && rec.rgb_v[1][2] == 0x80);
	reset();
	ws_bytes(PG, 6, PC, 1, one, 3);
	now += HZ / 1000;
	ws_bytes(PG, 6, PC, 1, two, 3);
	CHECK(rec.rgb == 1 && rec.rgb_chain[0] == BOARD_RGB_EXTERNAL && rec.rgb_led[0] == 0);
	CHECK(rec.rgb_v[0][0] == 0x01 && rec.rgb_v[0][1] == 0x80 && rec.rgb_v[0][2] == 0x7E);
	pinheck_board_tick(&b, now + HZ);
	CHECK(rec.rgb == 2 && rec.rgb_v[1][0] == 0x12 && b.ws_leds[BOARD_RGB_EXTERNAL] == 1);
}

/* Servo pulse = rising to falling edge on SERVO_0-4 (RF1, RA10, RF4, RE3, RE4). */
static void servos(void)
{
	static const int port[BOARD_SERVOS] = { PF, PA, PF, PE, PE }, bit[BOARD_SERVOS] = { 1, 10, 4, 3, 4 };
	int i;
	reset();
	step(PF, 1, 0, 100);
	CHECK(rec.servo == 0);
	for (i = 0; i < BOARD_SERVOS; i++) {
		step(port[i], bit[i], 1, 93200 + (uint64_t)i);
		step(port[i], bit[i], 0, 100);
		CHECK(rec.servo == i + 1 && rec.servo_n == i && rec.pulse == 93200u + (uint32_t)i);
	}
	reset();
	step(PF, 1, 1, 43500);
	step(PF, 1, 0, 100);
	pinheck_board_tick(&b, 1000 + HZ * 3 / 50 - 1);
	CHECK(rec.servo == 1);
	pinheck_board_tick(&b, 1000 + HZ * 3 / 50);
	CHECK(rec.servo == 2 && rec.servo_n == 0 && rec.pulse == 0 && rec.t == 1000 + HZ * 3 / 50);
	pinheck_board_tick(&b, 1000 + HZ);
	CHECK(rec.servo == 2);
}

/* The firmware's T3 switch scan (DOM_V006 0x9D0199C0): LATD = 0xFEFF << column, read PORTD,
   rows low = closed; LATD = 0 afterwards. */
static void switch_scan(void)
{
	int c;
	uint8_t got[8], all = 0;
	reset();
	for (c = 0; c < 8; c++) { closed[c] = (uint8_t)(1u << c | 0x80u >> c); all |= closed[c]; }
	write_port(PD, 0xFFFFu);
	CHECK((pinheck_board_read(&b, PD, now) & 0xFFu) == 0xFFu);
	for (c = 0; c < 8; c++) {
		write_port(PD, (0xFEFFu << c) & 0xFFFFu);
		now += 40;
		got[c] = (uint8_t)~pinheck_board_read(&b, PD, now);
		write_port(PD, 0);
		now += 40000;
	}
	for (c = 0; c < 8; c++) CHECK(got[c] == closed[c]);
	CHECK((uint8_t)~pinheck_board_read(&b, PD, now) == all);
	tris[PD] |= 0x0100u;
	write_port(PD, 0xFEFFu);
	CHECK((pinheck_board_read(&b, PD, now) & 0xFFu) == 0xFFu);
}

static void start_lamp(void)
{
	reset();
	step(PA, 3, 1, 10);
	CHECK(rec.start == 1 && rec.start_v == 1);
	step(PA, 2, 1, 10);
	CHECK(rec.start == 1);
	step(PA, 3, 0, 10);
	CHECK(rec.start == 2 && rec.start_v == 0);
}

int main(void)
{
	lamps_levels();
	sols_watchdog();
	cabinet_chain();
	gi_chain();
	ws2801();
	servos();
	switch_scan();
	start_lamp();
	printf("board: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

`tests/pinheck/board/check.sh`:

```sh
#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic"
mkdir -p $B
fail=0
cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/wpc/pinheck/board.c || { echo "C89 FAIL board.c"; fail=$((fail + 1)); }
cc $CF -I$S/wpc/pinheck -o $B/board_test board_test.c $S/wpc/pinheck/board.c || exit 2
./$B/board_test || fail=$((fail + 1))
echo "board: $fail failed"
[ $fail -eq 0 ]
```

`tests/pinheck/board/.gitignore`:

```text
build/
```

`chmod +x tests/pinheck/board/check.sh`

- [ ] **Step 2: Run them and confirm they fail**

Run: `timeout 300 tests/pinheck/board/check.sh 2>&1 | grep -E 'fatal|FAIL'`
Expected (the suite exits 2):
```text
cc1: fatal error: ../../../src/wpc/pinheck/board.c: No such file or directory
C89 FAIL board.c
board_test.c:1:10: fatal error: board.h: No such file or directory
cc1: fatal error: ../../../src/wpc/pinheck/board.c: No such file or directory
```

- [ ] **Step 3: Write `board.[ch]`**

`src/wpc/pinheck/board.h`:

```c
#ifndef PINHECK_BOARD_H
#define PINHECK_BOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_SOLS     24
#define BOARD_SERVOS   5
#define BOARD_RGB_MAX  16
#define BOARD_PORTS    7

enum { BOARD_RGB_ONBOARD, BOARD_RGB_EXTERNAL, BOARD_RGB_CHAINS };

typedef struct pinheck_board_io {
	void *ctx;
	uint8_t (*sw_col)(void *ctx, int col);     /* closed rows of switch column 0..7 */
	uint16_t (*cabinet)(void *ctx);             /* closed cabinet inputs: bits 0-7 U12 D0-D7, bits 8-15 U11 D0-D7 */
	void (*lamps)(void *ctx, uint64_t t, uint8_t cols, uint8_t rows);
	void (*sols)(void *ctx, uint64_t t, uint32_t sols);
	void (*gi)(void *ctx, uint64_t t, uint16_t gi);
	void (*start_lamp)(void *ctx, uint64_t t, int on);
	void (*rgb)(void *ctx, uint64_t t, int chain, int led, uint8_t r, uint8_t g, uint8_t b);
	void (*servo)(void *ctx, uint64_t t, int servo, uint32_t pulse); /* pulse 0: no pulse for 60 ms */
} pinheck_board_io;

typedef struct pinheck_board_ws {
	int bits;
	uint64_t last;
	uint8_t buf[BOARD_RGB_MAX * 3];
} pinheck_board_ws;

typedef struct pinheck_board {
	pinheck_board_io io;
	uint32_t hz;
	uint32_t lat[BOARD_PORTS], tris[BOARD_PORTS];
	uint32_t sol_pins, sols;
	uint64_t wd_until;
	int wd_on;
	uint8_t lamp_cols, lamp_rows;
	int start;
	uint16_t sr595, gi, sr165;
	int cab_lat;
	pinheck_board_ws ws[BOARD_RGB_CHAINS];
	int ws_leds[BOARD_RGB_CHAINS];
	uint64_t servo_rise[BOARD_SERVOS];
	int servo_on[BOARD_SERVOS];
} pinheck_board;

void pinheck_board_init(pinheck_board *b, const pinheck_board_io *io, uint32_t hz);
void pinheck_board_port(pinheck_board *b, int port, uint32_t lat, uint32_t tris, uint64_t t);
uint32_t pinheck_board_read(pinheck_board *b, int port, uint64_t t);
void pinheck_board_tick(pinheck_board *b, uint64_t t);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/board.c`:

```c
#include "board.h"
#include <string.h>

enum { PA, PB, PC, PD, PE, PF, PG };

static const uint8_t sol_port[BOARD_SOLS] = { PC, PC, PE, PF, PE, PE, PE, PC, PE, PC, PA, PA, PA, PA, PA, PC, PG, PG, PA, PA, PG, PG, PG, PA };
static const uint8_t sol_bit[BOARD_SOLS]  = {  2,  3,  5,  3,  6,  7,  8, 14,  9,  4,  2,  0,  1,  4,  5, 13,  1,  0,  6,  7, 14, 12, 13,  9 };
static const uint8_t servo_port[BOARD_SERVOS] = { PF, PA, PF, PE, PE };
static const uint8_t servo_bit[BOARD_SERVOS]  = {  1, 10,  4,  3,  4 };

#define LVL(b, p, n) ((int)((((b)->lat[p] & ~(b)->tris[p]) >> (n)) & 1u))

static void ws_latch(pinheck_board *b, int chain, uint64_t t)
{
	pinheck_board_ws *w = &b->ws[chain];
	int i, n = w->bits / 24;
	if (n > BOARD_RGB_MAX) n = BOARD_RGB_MAX;
	for (i = 0; i < n; i++)
		if (b->io.rgb) b->io.rgb(b->io.ctx, t, chain, i, w->buf[3 * i], w->buf[3 * i + 1], w->buf[3 * i + 2]);
	b->ws_leds[chain] = n;
	w->bits = 0;
	memset(w->buf, 0, sizeof(w->buf));
}

static void ws_edge(pinheck_board *b, int chain, int data, uint64_t t)
{
	pinheck_board_ws *w = &b->ws[chain];
	if (w->bits && t - w->last > b->hz / 2000) ws_latch(b, chain, w->last + b->hz / 2000);
	if (w->bits < BOARD_RGB_MAX * 24 && data) w->buf[w->bits >> 3] |= (uint8_t)(0x80 >> (w->bits & 7));
	w->bits++;
	w->last = t;
}

static void update_sols(pinheck_board *b, uint64_t t)
{
	uint32_t s = b->wd_on ? b->sol_pins : 0;
	if (s == b->sols) return;
	b->sols = s;
	if (b->io.sols) b->io.sols(b->io.ctx, t, s);
}

void pinheck_board_tick(pinheck_board *b, uint64_t t)
{
	int c;
	if (b->wd_on && t >= b->wd_until) { b->wd_on = 0; update_sols(b, b->wd_until); }
	for (c = 0; c < BOARD_RGB_CHAINS; c++)
		if (b->ws[c].bits && t - b->ws[c].last > b->hz / 2000) ws_latch(b, c, b->ws[c].last + b->hz / 2000);
	for (c = 0; c < BOARD_SERVOS; c++)
		if (b->servo_on[c] && t - b->servo_rise[c] >= (uint64_t)b->hz * 3 / 50) {
			b->servo_on[c] = 0;
			if (b->io.servo) b->io.servo(b->io.ctx, b->servo_rise[c] + (uint64_t)b->hz * 3 / 50, c, 0);
		}
}

void pinheck_board_init(pinheck_board *b, const pinheck_board_io *io, uint32_t hz)
{
	memset(b, 0, sizeof(*b));
	b->io = *io;
	b->hz = hz;
	b->sr165 = 0xFFFFu;
	memset(b->tris, 0xFF, sizeof(b->tris));
}

void pinheck_board_port(pinheck_board *b, int port, uint32_t lat, uint32_t tris, uint64_t t)
{
	uint32_t old = b->lat[port] & ~b->tris[port], now = lat & ~tris, ch = old ^ now;
	int i;

	b->lat[port] = lat;
	b->tris[port] = tris;
	if (!ch) return;
	pinheck_board_tick(b, t);
	if (port == PB && (b->lamp_cols != (uint8_t)now || b->lamp_rows != (uint8_t)(now >> 8))) {
		b->lamp_cols = (uint8_t)now;
		b->lamp_rows = (uint8_t)(now >> 8);
		if (b->io.lamps) b->io.lamps(b->io.ctx, t, b->lamp_cols, b->lamp_rows);
	}
	for (i = 0; i < BOARD_SOLS; i++)
		if (sol_port[i] == port && (ch >> sol_bit[i] & 1u))
			b->sol_pins = (b->sol_pins & ~(1u << i)) | ((now >> sol_bit[i] & 1u) << i);
	if (port == PG && (ch >> 15 & 1u) && !(now >> 15 & 1u)) {
		b->wd_until = t + (uint64_t)b->hz * 9 / 8;
		b->wd_on = 1;
	}
	update_sols(b, t);
	if (port == PA && (ch >> 3 & 1u)) {
		b->start = (int)(now >> 3 & 1u);
		if (b->io.start_lamp) b->io.start_lamp(b->io.ctx, t, b->start);
	}
	if (port == PE && (ch & 1u) && (now & 1u)) {
		b->sr595 = (uint16_t)(b->sr595 << 1 | (uint16_t)LVL(b, PG, 7));
		if (b->cab_lat) b->sr165 = (uint16_t)(b->sr165 << 1 | 1u);
	}
	if (port == PG && (ch >> 8 & 1u)) {
		b->cab_lat = (int)(now >> 8 & 1u);
		if (!b->cab_lat && b->sr595 != b->gi) {
			b->gi = b->sr595;
			if (b->io.gi) b->io.gi(b->io.ctx, t, b->gi);
		}
		if (b->cab_lat) b->sr165 = (uint16_t)~(b->io.cabinet ? b->io.cabinet(b->io.ctx) : 0);
	}
	if (port == PE && (ch >> 2 & 1u) && (now >> 2 & 1u)) ws_edge(b, BOARD_RGB_ONBOARD, LVL(b, PE, 1), t);
	if (port == PC && (ch >> 1 & 1u) && (now >> 1 & 1u)) ws_edge(b, BOARD_RGB_EXTERNAL, LVL(b, PG, 6), t);
	for (i = 0; i < BOARD_SERVOS; i++) {
		if (servo_port[i] != port || !(ch >> servo_bit[i] & 1u)) continue;
		if (now >> servo_bit[i] & 1u) { b->servo_rise[i] = t; b->servo_on[i] = 1; }
		else if (b->servo_on[i] && b->io.servo) b->io.servo(b->io.ctx, t, i, (uint32_t)(t - b->servo_rise[i]));
	}
}

uint32_t pinheck_board_read(pinheck_board *b, int port, uint64_t t)
{
	uint32_t v = 0xFFFFu;
	int c;
	(void)t;
	if (port == PD && b->io.sw_col)
		for (c = 0; c < 8; c++)
			if (!(b->tris[PD] >> (8 + c) & 1u) && !(b->lat[PD] >> (8 + c) & 1u))
				v &= ~(uint32_t)b->io.sw_col(b->io.ctx, c);
	if (port == PF) {
		uint16_t q = b->cab_lat ? b->sr165 : (uint16_t)~(b->io.cabinet ? b->io.cabinet(b->io.ctx) : 0);
		if (!(q & 0x8000u)) v &= ~1u;
	}
	return v;
}
```

- [ ] **Step 4: Run them and confirm they pass**

Run: `timeout 300 tests/pinheck/board/check.sh`
Expected:
```text
board: ok
board: 0 failed
```

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck/board.h src/wpc/pinheck/board.c tests/pinheck/board/board_test.c tests/pinheck/board/check.sh tests/pinheck/board/.gitignore
git commit -m "pinheck: board logic for lamps, coils, switches, shift chains, WS2801 and servos"
```

### Task 3: Board I/O in PinMAME, machine check 5

**Files:**
- Create: `tests/pinheck/board/check5.py`, `tests/pinheck/board/pinmame_board.sh`
- Modify: `src/wpc/pinheck.c`, `src/wpc/pinheck.h`, `src/wpc/pinheckgames.c`, `src/wpc/core.c`, `src/libpinmame/libpinmame.h`, `tests/pinheck/board/check.sh`, `tests/pinheck/link/register_build.py`, `tests/pinheck/pic32mx/makefile_link_check.sh`; regenerated: `src/pinmame.mak`, `cmake/*/CMakeLists*.txt`, `vcproj/*.vcxproj(.filters)`

**Interfaces:**
- Consumes: `board.[ch]` (Task 2); `pic32mx.icount` (Task 1); Plan 5's `PINHECK_INSERVICE`, `PINHECK_UART1_SEND[_AT]`, `PINHECK_UART1_LOG`, `PINHECK_PROP_LOG`; Plan 6's `PINHECK_FRAME_LOG` (records of 20-byte stamp — Propeller time, PIC32 cycle, hub address, little-endian — and the 4,096-byte RGB332 frame).
- Produces: PinMAME outputs per the Global Constraints; `int pinheck_getsol(int solNo)` (custom solenoids 51–64, 0–255) in `pinheck.h`; `PINHECK_COMPORTS` (port `CORE_COREINPORT`: 0x0001 tilt, 0x0002 coin, 0x0004 start, 0x0008 coin door toggle, 0x0010 Back, 0x0020 Enter, 0x0040 User); `PINHECK_OUT_LOG` lines `<time> L <cols> <rows> <cycle>`, `S <coils hex6>`, `G <gi hex4>`, `T <0|1>`, `R <chain> <led> <rrggbb>`, `V <servo> <µs>` (each ending with the PIC32 cycle) and `<time> P L<n>=<0-255> … S<n>=<0-255> …`; `PINHECK_UART1_SEND_GAP`; `check5.py plan|timing|verify`.

Check 5, in two launches of the in-service machine (as Plan 6's display check): launch 1 is the first boot on blank NVRAM, and its lamp strobes prove PinMAME time per edge. Launch 2 opens the service menu at 10.5 s (attract mode runs light shows over any lamp the console sets), then from 11.5 s sends one console command every 0.25 s: ball search off, attract lights off, `[MXX020]` for coils 0–23, lamp 0 at brightness 1–7 (two windows each), off, `[LXX007]` for lamps 0–63 in turn (each window must add exactly that lamp), `[L98000]`. From 39 s the key script walks the service menu: the solenoid test (Enter fires each of the 24 items in turn, which must fire exactly that coil), the servo test (`NOID RIGHT` 544 µs → 0, `NOID LEFT` 2,400 µs → 255 on solenoid 57; the target bank's 1,631 µs → 161 on 58), the lamp test (all 87 steps: each GI value, all GI, GI and lamps, every `LAMP =` 0–63, all on, all off), the RGB test (eight colours on 51–56), and the switch test (each of the 64 matrix switches closed and opened with PinMAME's column/row keys, then the cabinet keys), whose grid is decoded from the display frames. Attract mode before 10.5 s must show the start lamp (91) blinking, the external LED white on 62–64 and the flasher `GI_14` blinking on 43.

- [ ] **Step 1: Write the check**

`tests/pinheck/board/check5.py`:

```python
#!/usr/bin/env python3
"""Machine check 5: the firmware's console commands and service tests reach exactly the
PinMAME lamps, solenoids and switches of spec 4.5.
  check5.py plan DIR      write DIR/send, send_at, send_gap, keys.txt, frames
  check5.py timing LOG    board edges in one CPU slice carry distinct PinMAME times
  check5.py verify DIR    check DIR/out2.log and DIR/frames2.bin against the plan"""
import struct
import sys

FPS = 60
MENU_AT = 10.5                                      # attract mode runs light shows: test from the service menu
SEND_AT, GAP = 11.5, 0.25
KEYS_AT = 39.0
FRAME = 4096
COLS, ROWS = 'QWERTYUI', 'ASDFGHJK'


def lamp(n):
    """firmware lamp or switch n (0-63) -> PinMAME number"""
    return (n // 8 + 1) * 10 + n % 8 + 1


LAMPS = ['L%d' % lamp(n) for n in range(64)]
COILS = ['S%d' % (c + 1) for c in range(24)]
GI = ['S%d' % s for s in list(range(25, 33)) + list(range(37, 45))]
RGB = ['S%d' % s for s in range(51, 57)]


def uart_plan():
    """(time, command, expectation) for every console command"""
    cmds = [('[E97000]', None), ('[L99000]', None)]
    cmds += [('[M%02d020]' % c, ('coil', c)) for c in range(24)]
    for v in range(1, 8):
        cmds += [('[L00%03d]' % v, ('level', v)), ('', None)]
    cmds += [('[L00000]', None)]
    cmds += [('[L%02d007]' % n, ('lamp', n)) for n in range(64)]
    cmds += [('[L98000]', ('alloff',))]
    return [(SEND_AT + k * GAP, c, e) for k, (c, e) in enumerate(cmds)]


def key_plan():
    """(time, keys, hold frames, expectation for the window after it)"""
    ev, t = [(MENU_AT, '0', 6, None)], KEYS_AT     # main menu: SWITCH EDGE

    def tap(keys, dt, exp=None, hold=6):
        nonlocal t
        ev.append((t, keys, hold, exp))
        t += dt
    tap('RSHIFT', 1.0)
    tap('RSHIFT', 1.0)                              # SOLENOID
    tap('0', 1.0)                                   # SOLENOID TEST: KNOCKER
    for c in range(24):
        tap('0', 1.5 if c == 1 else 0.5, ('fire', c))   # the shaker test ignores keys for a second
        tap('RSHIFT', 0.5)
    tap('7', 1.0)                                   # back to SOLENOID
    tap('RSHIFT', 1.0)                              # SERVO
    tap('0', 1.0)                                   # SERVO TEST: NOID RIGHT
    tap('0', 1.5, ('servo', 0, 0))                  # servo 0 runs at 0 deg
    tap('RSHIFT', 1.0)                              # NOID STOP
    tap('0', 1.0)                                   # Enter: pulses stop
    tap('RSHIFT', 1.0)                              # NOID LEFT
    tap('0', 1.5, ('servo', 0, 255))                # servo 0 runs at 180 deg
    tap('7', 1.0)                                   # back to SERVO
    tap('RSHIFT', 1.0)                              # LAMP
    tap('0', 0.5, ('gi', ()))                       # LAMP TEST: ALL OFF
    for n in range(8):
        tap('RSHIFT', 0.5, ('gi', (44 - n,)))       # PLAYFIELD GI = 1 << n
    for m in range(8):
        tap('RSHIFT', 0.5, ('gi', (32 - m,)))       # BACKBOX GI = m
    tap('RSHIFT', 0.5, ('gi', tuple(range(37, 45))))
    tap('RSHIFT', 0.5, ('gi', tuple(range(25, 33))))
    tap('RSHIFT', 0.5, ('gi+lamps', 'all'))
    tap('RSHIFT', 0.5, ('gi+lamps', 'none'))
    for n in range(64):
        tap('RSHIFT', 0.5, ('onelamp', n))          # LAMP = n
    tap('RSHIFT', 0.5, ('lamps', 'all'))
    tap('RSHIFT', 0.5, ('lamps', 'none'))
    tap('7', 1.0)                                   # back to LAMP
    tap('RSHIFT', 1.0)                              # RGB LIGHTING
    rgb = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
    tap('0', 1.0, ('rgb', rgb[0] + (0, 0, 0)))      # RGB1 RED
    for v in rgb[1:]:
        tap('RSHIFT', 1.0, ('rgb', v + (0, 0, 0)))
    for v in rgb:
        tap('RSHIFT', 1.0, ('rgb', (0, 0, 0) + v))
    tap('7', 1.0)                                   # back to RGB LIGHTING
    for i in range(5):
        tap('LSHIFT', 1.0)                          # back to SWITCH EDGE
    tap('0', 1.5)                                   # SWITCH TEST
    for n in range(64):
        k = 'KEYCODE_%s KEYCODE_%s' % (COLS[n // 8], ROWS[n % 8])
        tap(k, 16 / FPS, ('switch', (n,), (1,)), hold=4)
        tap(k, 16 / FPS, ('switch', (), (1,)), hold=4)
    for key, cab in (('1', 12), ('5', 7), ('INSERT', 8), ('9', 2), ('0', 6)):
        tap(key, 16 / FPS, ('switch', (), (1, cab)), hold=12)
        tap(None, 16 / FPS, ('switch', (), (1,)))
    tap('END', 32 / FPS, ('switch', (), ()), hold=4)
    tap('END', 32 / FPS, ('switch', (), (1,)), hold=4)
    for key, cab in (('LSHIFT', 4), ('RSHIFT', 3)):
        tap(key, 16 / FPS, ('switch', (), (1, cab)), hold=12)
        tap(None, 16 / FPS, ('switch', (), (1,)))
    tap('7', 1.0, ('exit',))                        # Back (cabinet switch 5) leaves the switch test
    return ev, t + 0.5


def plan(d):
    send = ''.join(c + '~' for _, c, _ in uart_plan())
    ev, end = key_plan()
    keys = []
    for t, k, hold, _ in ev:
        if k:
            names = k if k.startswith('KEYCODE_') else 'KEYCODE_' + k
            keys.append('%d tap %d %s\n' % (round(t * FPS), hold, names))
    for name, text in (('send', send), ('send_at', '%g' % SEND_AT), ('send_gap', '%g' % GAP),
                       ('keys.txt', ''.join(keys)), ('frames', '%d' % round(end * FPS))):
        open('%s/%s' % (d, name), 'w').write(text + ('\n' if name != 'keys.txt' else ''))


def timing(path):
    """PinMAME time of each lamp edge must be the edge's own PIC32 cycle, also inside one CPU slice."""
    prev, n, bad = None, 0, 0
    for line in open(path):
        f = line.split()
        if len(f) < 3 or f[1] != 'L':
            continue
        t, cyc = float(f[0]), int(f[-1])
        if prev and 0 < cyc - prev[1] < 80000:
            n += 1
            if abs((t - prev[0]) - (cyc - prev[1]) / 80e6) > 2e-8:
                bad += 1
        prev = (t, cyc)
    print('timing: %d edge pairs less than 1 ms apart, %d with the wrong PinMAME time' % (n, bad))
    return 0 if n > 1000 and bad == 0 else 1


class Out:
    def __init__(self, path):
        self.ev, self.servo = {}, {}
        for line in open(path):
            f = line.split()
            if len(f) > 2 and f[1] == 'P':
                for x in f[2:]:
                    k, v = x.split('=')
                    self.ev.setdefault(k, []).append((float(f[0]), int(v)))
            elif len(f) > 3 and f[1] == 'V':
                self.servo.setdefault(int(f[2]), []).append((float(f[0]), float(f[3])))

    def at(self, name, t):
        v = 0
        for tt, vv in self.ev.get(name, []):
            if tt > t:
                break
            v = vv
        return v

    def rises(self, name, t0, t1):
        v, out = self.at(name, t0), []
        for tt, vv in self.ev.get(name, []):
            if t0 <= tt < t1:
                if v < 128 <= vv:
                    out.append(tt)
                v = vv
        return out

    def on(self, names, t):
        return tuple(n for n in names if self.at(n, t) >= 128)

    def mean(self, name, t0, t1):
        n = int((t1 - t0) * FPS)
        return sum(self.at(name, t0 + i / FPS) for i in range(n)) / n


def grid(f):
    """switch test screen: closed matrix switches (column 0 drawn on the right) and cabinet inputs (0-7 right, 8-15 left)"""
    px = lambda x, y: f[y * 128 + x]
    g, c2 = px(0, 0), px(32, 0)
    if not g or not c2:
        return None
    for y in range(32):
        for x in range(40):
            if (x % 4 in (0, 3) or y % 4 in (0, 3)) and px(x, y) != (g if x < 32 else c2):
                return None
    if any(px(x, y) == g for x in range(1, 31, 4) for y in range(1, 31, 4)):
        return None
    sw = tuple(c * 8 + r for c in range(8) for r in range(8) if px((7 - c) * 4 + 1, r * 4 + 1))
    cab = tuple(k for k in range(16) if px(37 if k < 8 else 33, (k % 8) * 4 + 1))
    return sw, cab


def verify(d):
    o = Out(d + '/out2.log')
    raw = open(d + '/frames2.bin', 'rb').read()
    rec = 20 + FRAME
    frames = [(struct.unpack_from('<Q', raw, i + 8)[0] / 80e6, raw[i + 20:i + rec]) for i in range(0, len(raw) - rec + 1, rec)]
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)
    # attract mode: blinking start lamp 91, external WS2801 LED white, GI_14 flashing
    check(o.rises('L91', 5, MENU_AT), 'start lamp 91 never lit in attract')
    check([o.at('S%d' % s, MENU_AT) for s in (62, 63, 64)] == [255] * 3, 'external LED 0 is not white on 62-64')
    check(o.rises('S43', 5, MENU_AT), 'attract flasher GI_14 never reached solenoid 43')
    levels = {}
    for t, cmd, e in uart_plan():
        if not e:
            continue
        if e[0] == 'coil':
            got = [c for c in COILS if o.rises(c, t, t + GAP)]
            check(got == [COILS[e[1]]], '%s fired %s, expected %s' % (cmd, got, COILS[e[1]]))
        elif e[0] == 'lamp':
            got = sorted(set(o.on(LAMPS, t + GAP - 0.03)) - set(o.on(LAMPS, t - 0.03)))
            check(got == [LAMPS[e[1]]] and len(o.on(LAMPS, t + GAP - 0.03)) == e[1] + 1, '%s lit %s, expected %s' % (cmd, got, LAMPS[e[1]]))
        elif e[0] == 'level':
            xs = sorted(o.at('L11', t + i / FPS) for i in range(int(2 * GAP * FPS)))
            levels[e[1]] = xs[len(xs) // 2] / 255
        elif e[0] == 'alloff':
            check(o.on(LAMPS, t + GAP) == (), '[L98000] left lamps on: %s' % (o.on(LAMPS, t + GAP),))
    lv = [levels[v] for v in range(1, 8)]
    print('levels: lamp 11 at brightness 1-7 reads %s (median, 0-1)' % ' '.join('%.3f' % x for x in lv))
    check(lv[0] < 0.3 and lv[6] > 0.8 and all(b - a >= 0.04 for a, b in zip(lv, lv[1:])), 'brightness levels not 7 distinct rising steps')
    ev, _ = key_plan()
    wanted = set()
    for i, (t, k, hold, e) in enumerate(ev):
        if not e:
            continue
        t1 = ev[i + 1][0] if i + 1 < len(ev) else t + 1
        t, t1 = t - 0.1, t1 - 0.1                   # a scripted frame falls up to 1/60 s before its nominal time
        if e[0] == 'fire':
            got = [c for c in COILS if o.rises(c, t, t1)]
            check(got == [COILS[e[1]]], 'solenoid test item %d fired %s' % (e[1], got))
        elif e[0] == 'servo':
            w = [us for tt, us in o.servo.get(e[1], []) if t + 0.6 <= tt < t + 1.1]
            check(w and all(abs(us - (544 if e[2] == 0 else 2400)) < 10 for us in w), 'servo %d pulses %s' % (e[1], sorted(w)[::max(1, len(w) // 3)]))
            check(o.at('S%d' % (57 + e[1]), t + 1.1) == e[2], 'servo output %d is %d' % (57 + e[1], o.at('S%d' % (57 + e[1]), t + 1.1)))
            check(o.at('S58', t + 1.1) == 161, 'target bank servo 58 is %d, expected 161 (1631 us)' % o.at('S58', t + 1.1))
        elif e[0] in ('gi', 'gi+lamps'):
            want = tuple('S%d' % s for s in (e[1] if e[0] == 'gi' else (range(25, 33) if e[1] == 'all' else ()))) if e[0] == 'gi' else None
            if e[0] == 'gi+lamps':
                want = tuple(GI) if e[1] == 'all' else ()
                check(o.on(LAMPS, t1 - 0.05) == (tuple(LAMPS) if e[1] == 'all' else ()), 'GI AND LAMPS %s: lamps %s' % (e[1], o.on(LAMPS, t1 - 0.05)))
            got = o.on(GI, t1 - 0.05)
            check(sorted(got) == sorted(want), 'lamp test GI step at %.1f: %s, expected %s' % (t, got, want))
        elif e[0] == 'onelamp':
            got = o.on(LAMPS, t1 - 0.05)
            check(got == (LAMPS[e[1]],), 'LAMP = %d lit %s' % (e[1], got))
        elif e[0] == 'lamps':
            got = o.on(LAMPS, t1 - 0.05)
            check(got == (tuple(LAMPS) if e[1] == 'all' else ()), 'lamp test ALL %s: %d lamps' % (e[1], len(got)))
        elif e[0] == 'rgb':
            got = tuple(o.at(s, t1 - 0.05) for s in RGB)
            check(got == e[1], 'RGB test at %.1f: %s, expected %s' % (t, got, e[1]))
        elif e[0] == 'switch':
            got = [grid(f) for tt, f in frames if t + 0.12 <= tt < t1 + 0.2]
            got = [g for g in got if g]
            check((e[1], e[2]) in got, 'switch test after %s: screens %s, expected %s' % (k, sorted(set(got)), (e[1], e[2])))
            wanted.add((e[1], e[2]))
        elif e[0] == 'exit':
            after = [grid(f) for tt, f in frames if tt >= t + 0.1]
            check(after and after[-1] is None, 'Back did not leave the switch test')
    shown = set(g for g in (grid(f) for tt, f in frames if tt >= KEYS_AT) if g)
    check(shown <= wanted, 'switch test showed unexpected states %s' % sorted(shown - wanted))
    for f in fails:
        print('BOARD FAIL: ' + f)
    print('check5: %d uart commands, %d service-test steps, %d failures' % (len(uart_plan()), sum(1 for x in ev if x[3]), len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('plan', 'timing', 'verify'):
        sys.exit(__doc__)
    if sys.argv[1] == 'plan':
        plan(sys.argv[2])
    else:
        sys.exit((timing if sys.argv[1] == 'timing' else verify)(sys.argv[2]))
```

`tests/pinheck/board/pinmame_board.sh`:

```sh
#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
python3 check5.py plan $B || exit 2
launch() {
	(cd $B && PINHECK_INSERVICE=6 PINHECK_OUT_LOG=$PWD/out$1.log PINHECK_UART1_LOG=$PWD/uart$1.log PINHECK_PROP_LOG=$PWD/prop$1.log \
		timeout 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -headless -frames_to_run $2 -skip_gamewarnings -nothrottle $3 > run$1.out 2>&1) \
		|| { echo "BOARD FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
launch 1 1200
[ -s $B/out1.log ] || { echo "BOARD FAIL: launch 1 wrote no board log (PINHECK_OUT_LOG)"; exit 1; }
python3 check5.py timing $B/out1.log || exit 1
PINHECK_FRAME_LOG=$PWD/$B/frames2.bin PINHECK_UART1_SEND_AT=$(cat $B/send_at) PINHECK_UART1_SEND_GAP=$(cat $B/send_gap) PINHECK_UART1_SEND="$(cat $B/send)" \
	launch 2 "$(cat $B/frames)" "-key_script keys.txt"
python3 check5.py verify $B || exit 1
echo "pinmame board: ok"
```

Append the libpinmame check to the suite:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/board/check.sh', [
    (r'''cc $CF -I$S/wpc/pinheck -o $B/board_test board_test.c $S/wpc/pinheck/board.c || exit 2
./$B/board_test || fail=$((fail + 1))
echo "board: $fail failed"
[ $fail -eq 0 ]
''',
     r'''cc $CF -I$S/wpc/pinheck -o $B/board_test board_test.c $S/wpc/pinheck/board.c || exit 2
./$B/board_test || fail=$((fail + 1))
printf '#include "libpinmame.h"\n#include "gen.h"\nstatic_assert(PINMAME_HARDWARE_GEN_PINHECK == GEN_PINHECK, "PINHECK");\n' > $B/gen.cpp
c++ -std=c++17 -fsyntax-only -I$S/libpinmame -I$S/wpc -I$S $B/gen.cpp || { echo "GEN FAIL: libpinmame's PINMAME_HARDWARE_GEN lacks PINHECK"; fail=$((fail + 1)); }
echo "board: $fail failed"
[ $fail -eq 0 ]
'''),
])
EOF
```

`chmod +x tests/pinheck/board/check5.py tests/pinheck/board/pinmame_board.sh`

- [ ] **Step 2: Confirm it fails before the wiring**

Run: `timeout 300 tests/pinheck/board/check.sh 2>&1 | grep -E 'error|FAIL|failed'`
Expected (the suite exits 1):
```text
build/gen.cpp:3:15: error: ‘PINMAME_HARDWARE_GEN_PINHECK’ was not declared in this scope; did you mean ‘PINMAME_HARDWARE_GEN_WICO’?
GEN FAIL: libpinmame's PINMAME_HARDWARE_GEN lacks PINHECK
board: 1 failed
```

Build PinMAME as it stands (Release, `PLATFORM=linux`, `ARCH=x64`, never the user's `build-dbg`), then run the machine check:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m8a.log 2>&1; tail -1 build/sdl3pinmame/m8a.log
SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/board/pinmame_board.sh
```
Expected: `[100%] Built target sdl3pinmame`, then after the first launch (about 2 minutes; exit 1):
```text
BOARD FAIL: launch 1 wrote no board log (PINHECK_OUT_LOG)
```

- [ ] **Step 3: Wire the board into the driver**

The board block goes after `locals` in `pinheck.c`, before the UART and port callbacks that use it; the vblank, `MACHINE_INIT`, `MACHINE_RESET`, `MACHINE_STOP` and machine-driver hooks are one line each, and the port read/write callbacks forward to `board.c`. Nothing in the sound block changes.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
    (r'''#include "pinheck/audio.h"
#include "pinheck/display.h"
#include "pinheck.h"
#include <stdio.h>
''',
     r'''#include "pinheck/audio.h"
#include "pinheck/display.h"
#include "pinheck/board.h"
#include "pinheck.h"
#include <stdio.h>
'''),
    (r'''	FILE *uart1, *proplog;
	const char *send;
	uint64_t send_at, rtc_at;
	double reset_at;
	int have_zip, have_vol, idle;
	uint32_t logged[PINHECK_LOG_MAX];
	int nlogged;
} locals;

static void pinheck_uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
''',
     r'''	FILE *uart1, *proplog;
	const char *send;
	uint64_t send_at, send_gap, rtc_at;
	double reset_at;
	int have_zip, have_vol, idle;
	uint32_t logged[PINHECK_LOG_MAX];
	int nlogged;
} locals;

/* board I/O: lamps, coils, switches, GI, RGB and servos (board.c), numbered as in spec 4.5 */
#define PINHECK_SOL_GI0 24  /* GI 0-7: solenoids 25-32 */
#define PINHECK_SOL_GI8 40  /* GI 8-15: solenoids 37-44 through core.c's S11 layout */
#define PINHECK_SOL_RGB 50  /* on-board RGB left R,G,B, right R,G,B: 51-56 */
#define PINHECK_SOL_SRV 56  /* servos 0-4: 57-61 */
#define PINHECK_SOL_EXT 61  /* external WS2801 LED 0 R,G,B: 62-64 */
#define PINHECK_EXT_LEDS 1  /* the firmware drives one external LED */
#define PINHECK_NSOLS   64
#define PINHECK_LAMP_ST 64  /* start button lamp: lamp 91 */
#define PINHECK_NLAMPS  72

static pinheck_board brd;
static FILE *brd_log;
static int brd_opened, brd_rgb_extra;
static UINT32 brd_sols_seen;
static UINT16 brd_gi8_seen;
static UINT8 brd_cust[PINHECK_NSOLS - PINHECK_SOL_RGB], brd_logged[PINHECK_NLAMPS + PINHECK_NSOLS];

static uint8_t pinheck_brd_swcol(void *ctx, int col) { (void)ctx; return coreGlobals.swMatrix[col + 1]; }
static uint16_t pinheck_brd_cab(void *ctx) { (void)ctx; return (uint16_t)(coreGlobals.swMatrix[0] | coreGlobals.swMatrix[9] << 8); }

static void pinheck_brd_lamps(void *ctx, uint64_t t, uint8_t cols, uint8_t rows)
{
	(void)ctx;
	core_write_pwm_output_lamp_matrix(CORE_MODOUT_LAMP0, cols, rows, 8);
	if (brd_log) fprintf(brd_log, "%.9f L %02x %02x %llu\n", timer_get_time(), cols, rows, (unsigned long long)t);
}

static void pinheck_brd_sols(void *ctx, uint64_t t, uint32_t sols)
{
	int i;
	(void)ctx;
	for (i = 0; i < 3; i++) core_write_pwm_output_8b(CORE_MODOUT_SOL0 + 8 * i, (UINT8)(sols >> (8 * i)));
	coreGlobals.pulsedSolState = (coreGlobals.pulsedSolState & 0xFF000000u) | sols;
	brd_sols_seen |= sols;
	if (brd_log) fprintf(brd_log, "%.9f S %06x %llu\n", timer_get_time(), (unsigned)sols, (unsigned long long)t);
}

static void pinheck_brd_gi(void *ctx, uint64_t t, uint16_t gi)
{
	(void)ctx;
	core_write_pwm_output_8b(CORE_MODOUT_SOL0 + PINHECK_SOL_GI0, (UINT8)gi);
	core_write_pwm_output_8b(CORE_MODOUT_SOL0 + PINHECK_SOL_GI8, (UINT8)(gi >> 8));
	coreGlobals.pulsedSolState = (coreGlobals.pulsedSolState & 0x00FFFFFFu) | ((UINT32)(gi & 0xFF) << 24);
	brd_sols_seen |= (UINT32)(gi & 0xFF) << 24;
	brd_gi8_seen |= (UINT16)(gi & 0xFF00);
	if (brd_log) fprintf(brd_log, "%.9f G %04x %llu\n", timer_get_time(), gi, (unsigned long long)t);
}

static void pinheck_brd_start(void *ctx, uint64_t t, int on)
{
	(void)ctx;
	core_write_pwm_output(CORE_MODOUT_LAMP0 + PINHECK_LAMP_ST, 1, (UINT8)on);
	if (on) coreGlobals.tmpLampMatrix[8] |= 1;
	if (brd_log) fprintf(brd_log, "%.9f T %d %llu\n", timer_get_time(), on, (unsigned long long)t);
}

static void pinheck_brd_level(int idx, UINT8 v)
{
	brd_cust[idx - PINHECK_SOL_RGB] = v;
	coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + idx].value = v / 255.0f;
}

static void pinheck_brd_rgb(void *ctx, uint64_t t, int chain, int led, uint8_t r, uint8_t g, uint8_t b)
{
	(void)ctx;
	if (brd_log) fprintf(brd_log, "%.9f R %d %d %02x%02x%02x %llu\n", timer_get_time(), chain, led, r, g, b, (unsigned long long)t);
	if ((chain == BOARD_RGB_ONBOARD && led < 2) || (chain == BOARD_RGB_EXTERNAL && led < PINHECK_EXT_LEDS)) {
		int idx = chain == BOARD_RGB_ONBOARD ? PINHECK_SOL_RGB + 3 * led : PINHECK_SOL_EXT + 3 * led;
		pinheck_brd_level(idx, r);
		pinheck_brd_level(idx + 1, g);
		pinheck_brd_level(idx + 2, b);
	} else if (!brd_rgb_extra) {
		brd_rgb_extra = 1;
		logerror("pinheck: WS2801 chain %d carries LED %d, which has no output on this board\n", chain, led);
	}
}

static void pinheck_brd_servo(void *ctx, uint64_t t, int servo, uint32_t pulse)
{
	double us = pulse / (PINHECK_CLOCK / 1e6), v = (us - 1000.0) / 1000.0;
	(void)ctx;
	if (brd_log) fprintf(brd_log, "%.9f V %d %.1f %llu\n", timer_get_time(), servo, us, (unsigned long long)t);
	if (!pulse) return; /* no pulses: the servo holds its position */
	if (v < 0.0) v = 0.0;
	if (v > 1.0) v = 1.0;
	pinheck_brd_level(PINHECK_SOL_SRV + servo, (UINT8)(v * 255.0 + 0.5));
}

/* switch n (0-63) is PinMAME (n/8+1)*10 + n%8+1, lamps likewise; cabinet inputs are columns 0 and 9 */
static int pinheck_sw2m(int no) { return (no / 10) * 8 + no % 10 - 1; }
static int pinheck_m2sw(int col, int row) { return col * 10 + row + 1; }

int pinheck_getsol(int solNo)
{
	return solNo > PINHECK_SOL_RGB && solNo <= PINHECK_NSOLS ? brd_cust[solNo - 1 - PINHECK_SOL_RGB] : 0;
}

/* test log: PinMAME-numbered lamps and solenoids whose 0-255 level changed since the last frame */
static void pinheck_brd_log_outputs(void)
{
	float sol[CORE_MODOUT_SOL_MAX];
	char line[2048];
	int i, n = 0;
	core_update_pwm_lamps();
	core_update_pwm_solenoids();
	core_getAllPhysicSols(sol);
	for (i = 0; i < PINHECK_NLAMPS + PINHECK_NSOLS; i++) {
		float f = i < PINHECK_NLAMPS ? coreGlobals.physicOutputState[CORE_MODOUT_LAMP0 + i].value : sol[i - PINHECK_NLAMPS];
		UINT8 v = f <= 0.0f ? 0 : f >= 1.0f ? 255 : (UINT8)(f * 255.0f + 0.5f);
		if (v == brd_logged[i]) continue;
		brd_logged[i] = v;
		if (i < PINHECK_NLAMPS) n += sprintf(line + n, " L%d=%d", coreData->m2lamp(i / 8 + 1, i % 8), v);
		else n += sprintf(line + n, " S%d=%d", i - PINHECK_NLAMPS + 1, v);
	}
	if (n) fprintf(brd_log, "%.9f P%s\n", timer_get_time(), line);
}

static void pinheck_brd_init(void)
{
	const char *path = getenv("PINHECK_OUT_LOG");
	if (brd_log) fclose(brd_log);
	brd_log = path ? fopen(path, brd_opened ? "a" : "w") : NULL;
	brd_opened = 1;
	options.usemodsol |= CORE_MODOUT_FORCE_ON;
	coreGlobals.nLamps = PINHECK_NLAMPS;
	coreGlobals.nSolenoids = PINHECK_NSOLS;
	coreGlobals.nGI = 0;
	/* a lamp is lit at most 2 of every 24 lamp-timer periods: 12x brings full strobe to 1.0 */
	core_set_pwm_output_led_vfd(CORE_MODOUT_LAMP0, 64, 0, 12.0f);
	core_set_pwm_output_type(CORE_MODOUT_LAMP0 + PINHECK_LAMP_ST, PINHECK_NLAMPS - 64, CORE_MODOUT_LED);
	core_set_pwm_output_type(CORE_MODOUT_SOL0, BOARD_SOLS, CORE_MODOUT_SOL_2_STATE);
	core_set_pwm_output_type(CORE_MODOUT_SOL0 + PINHECK_SOL_GI0, 8, CORE_MODOUT_LED);
	core_set_pwm_output_type(CORE_MODOUT_SOL0 + PINHECK_SOL_GI8, 8, CORE_MODOUT_LED);
	core_set_pwm_output_type(CORE_MODOUT_SOL0 + PINHECK_SOL_RGB, PINHECK_NSOLS - PINHECK_SOL_RGB, CORE_MODOUT_NONE);
}

/* PIC32 reset: every pin is an input again, so every output is off */
static void pinheck_brd_reset(void)
{
	pinheck_board_io io = { NULL, pinheck_brd_swcol, pinheck_brd_cab, pinheck_brd_lamps, pinheck_brd_sols,
	                        pinheck_brd_gi, pinheck_brd_start, pinheck_brd_rgb, pinheck_brd_servo };
	int i;
	pinheck_board_init(&brd, &io, PINHECK_CLOCK);
	core_write_pwm_output_lamp_matrix(CORE_MODOUT_LAMP0, 0, 0, 8);
	core_write_pwm_output(CORE_MODOUT_LAMP0 + PINHECK_LAMP_ST, 1, 0);
	for (i = 0; i < 3; i++) core_write_pwm_output_8b(CORE_MODOUT_SOL0 + 8 * i, 0);
	core_write_pwm_output_8b(CORE_MODOUT_SOL0 + PINHECK_SOL_GI0, 0);
	core_write_pwm_output_8b(CORE_MODOUT_SOL0 + PINHECK_SOL_GI8, 0);
	for (i = PINHECK_SOL_RGB; i < PINHECK_NSOLS; i++) pinheck_brd_level(i, 0);
	coreGlobals.pulsedSolState = 0;
	brd_rgb_extra = 0;
	brd_sols_seen = 0;
	brd_gi8_seen = 0;
	coreGlobals.swMatrix[0] |= 1;
}

static void pinheck_brd_vblank(void)
{
	pinheck_board_tick(&brd, pic32cpu_soc()->cpu.cycles);
	memcpy((void *)coreGlobals.lampMatrix, (void *)coreGlobals.tmpLampMatrix, 9);
	memset((void *)coreGlobals.tmpLampMatrix, 0, 9);
	coreGlobals.solenoids = brd_sols_seen | coreGlobals.pulsedSolState;
	coreGlobals.solenoids2 = (coreGlobals.solenoids2 & ~0xFF00u) | brd_gi8_seen | (brd.gi & 0xFF00u);
	brd_sols_seen = 0;
	brd_gi8_seen = 0;
	if (brd_log) pinheck_brd_log_outputs();
}

static void pinheck_brd_stop(void)
{
	if (brd_log) fclose(brd_log);
	brd_log = NULL;
}

/* cabinet inputs: firmware cabinet switch n = PinMAME switch n (1-8), n+82 (9-15) */
static SWITCH_UPDATE(pinheck)
{
	UINT8 c0;
	if (!inports) return;
	c0 = coreGlobals.swMatrix[0] & 0x0Cu;
	if (!(inports[CORE_COREINPORT] & 0x0008)) c0 |= 0x01;
	if (inports[CORE_COREINPORT] & 0x0040) c0 |= 0x02;
	if (inports[CORE_COREINPORT] & 0x0010) c0 |= 0x10;
	if (inports[CORE_COREINPORT] & 0x0020) c0 |= 0x20;
	if (inports[CORE_COREINPORT] & 0x0002) c0 |= 0x40;
	if (inports[CORE_COREINPORT] & 0x0001) c0 |= 0x80;
	coreGlobals.swMatrix[0] = c0;
	coreGlobals.swMatrix[9] = (coreGlobals.swMatrix[9] & ~0x08u) | (inports[CORE_COREINPORT] & 0x0004 ? 0x08u : 0);
}

static void pinheck_uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
'''),
    (r'''	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
}

static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx;
	if (port != PIC32MX_PORTF) return 0xFFFFu;
	return (0xFFFFu & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}

''',
     r'''	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
	pinheck_board_port(&brd, port, lat, tris, cycle);
}

static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
{
	uint32_t v = pinheck_board_read(&brd, port, cycle);
	(void)ctx;
	if (port != PIC32MX_PORTF) return v;
	return (v & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}

'''),
    (r'''	}
	if (locals.send && *locals.send == '~' && soc->cpu.cycles >= locals.send_at) {
		locals.send_at = soc->cpu.cycles + PINHECK_CLOCK;
		locals.send++;
	}
''',
     r'''	}
	if (locals.send && *locals.send == '~' && soc->cpu.cycles >= locals.send_at) {
		locals.send_at += locals.send_gap;
		locals.send++;
	}
'''),
    (r'''static INTERRUPT_GEN(pinheck_vblank)
{
	core_updateSw(0);
}
''',
     r'''static INTERRUPT_GEN(pinheck_vblank)
{
	if (!locals.idle) pinheck_brd_vblank();
	core_updateSw(0);
}
'''),
    (r'''	pinheck_open_card();
	pinheck_disp_init();
	pic32cpu_set_board(&board);
}
''',
     r'''	pinheck_open_card();
	pinheck_disp_init();
	pinheck_brd_init();
	pic32cpu_set_board(&board);
}
'''),
    (r'''	locals.send = getenv("PINHECK_UART1_SEND");
	locals.send_at = (uint64_t)((at ? atof(at) : 0.0) * PINHECK_CLOCK);
}

''',
     r'''	locals.send = getenv("PINHECK_UART1_SEND");
	locals.send_at = (uint64_t)((at ? atof(at) : 0.0) * PINHECK_CLOCK);
	at = getenv("PINHECK_UART1_SEND_GAP");
	locals.send_gap = (uint64_t)((at ? atof(at) : 1.0) * PINHECK_CLOCK);
	pinheck_brd_reset();
}

'''),
    (r'''	locals.have_vol = locals.have_zip = 0;
	pinheck_disp_stop();
}

''',
     r'''	locals.have_vol = locals.have_zip = 0;
	pinheck_disp_stop();
	pinheck_brd_stop();
}

'''),
    (r'''	MDRV_TIMER_ADD(pinheck_tick, 1000)
	MDRV_NVRAM_HANDLER(pinheck)
	MDRV_SOUND_ADD(CUSTOM, pinheck_sndInt)
	MDRV_SOUND_ATTRIBUTES(SOUND_SUPPORTS_STEREO)
''',
     r'''	MDRV_TIMER_ADD(pinheck_tick, 1000)
	MDRV_NVRAM_HANDLER(pinheck)
	MDRV_SWITCH_UPDATE(pinheck)
	MDRV_SWITCH_CONV(pinheck_sw2m, pinheck_m2sw)
	MDRV_LAMP_CONV(pinheck_sw2m, pinheck_m2sw)
	MDRV_SOUND_ADD(CUSTOM, pinheck_sndInt)
	MDRV_SOUND_ATTRIBUTES(SOUND_SUPPORTS_STEREO)
'''),
])
edit('src/wpc/pinheck.h', [
    (r'''#define PINHECK_BIOSREGION REGION_USER2

#define PINHECK_INPUT_PORTS_START(name, balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls)

#define PINHECK_INPUT_PORTS_END INPUT_PORTS_END
''',
     r'''#define PINHECK_BIOSREGION REGION_USER2

#define PINHECK_SWLFLIP  4
#define PINHECK_SWRFLIP  3
#define PINHECK_CUSTSOLS 14

#define PINHECK_COMPORTS \
  PORT_START /* 0 */ \
    COREPORT_BITDEF(  0x0001, IPT_TILT,          KEYCODE_INSERT) \
    COREPORT_BITDEF(  0x0002, IPT_COIN1,         IP_KEY_DEFAULT) \
    COREPORT_BITDEF(  0x0004, IPT_START1,        IP_KEY_DEFAULT) \
    COREPORT_BITTOG(  0x0008, "Coin Door",       KEYCODE_END) \
    COREPORT_BIT(     0x0010, "Back",            KEYCODE_7) \
    COREPORT_BIT(     0x0020, "Enter",           KEYCODE_0) \
    COREPORT_BIT(     0x0040, "User",            KEYCODE_9)

#define PINHECK_INPUT_PORTS_START(name, balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls) \
    PINHECK_COMPORTS

#define PINHECK_INPUT_PORTS_END INPUT_PORTS_END
'''),
    (r'''
extern PINMAME_VIDEO_UPDATE(pinheck_video);
extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK
''',
     r'''
extern PINMAME_VIDEO_UPDATE(pinheck_video);
extern int pinheck_getsol(int solNo);
extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK
'''),
])
edit('src/wpc/pinheckgames.c', [
    (r'''#define INIT_PINHECK(name, balls, version) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
static core_tGameData name##GameData = { GEN_PINHECK, pinheck_disp, {0, 0, 0, 0, SNDBRD_NONE, 0, version} }; \
static void init_##name(void) { core_gameData = &name##GameData; }

''',
     r'''#define INIT_PINHECK(name, balls, version) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
static core_tGameData name##GameData = { GEN_PINHECK, pinheck_disp, {FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, version, 0, pinheck_getsol} }; \
static void init_##name(void) { core_gameData = &name##GameData; }

'''),
])
edit('src/wpc/core.c', [
    (r'''    return coreGlobals.nSolenoids && (options.usemodsol & (CORE_MODOUT_ENABLE_PHYSOUT_SOLENOIDS | CORE_MODOUT_ENABLE_MODSOL | CORE_MODOUT_FORCE_ON)) ? saturatedByte(coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + solNo - 1].value) : coreGlobals.solenoids & CORE_SOLBIT(solNo);
  else if (solNo <= 32) { // 29-32
    if (core_gameData->gen & GEN_ALLS11)
      return coreGlobals.nSolenoids && (options.usemodsol & (CORE_MODOUT_ENABLE_PHYSOUT_SOLENOIDS | CORE_MODOUT_ENABLE_MODSOL | CORE_MODOUT_FORCE_ON)) ? saturatedByte(coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + solNo - 1].value) : coreGlobals.solenoids & CORE_SOLBIT(solNo);
    else if (core_gameData->gen & GEN_ALLWPC) // Remap WPC GameOn/J111 GPIO (hacky)
''',
     r'''    return coreGlobals.nSolenoids && (options.usemodsol & (CORE_MODOUT_ENABLE_PHYSOUT_SOLENOIDS | CORE_MODOUT_ENABLE_MODSOL | CORE_MODOUT_FORCE_ON)) ? saturatedByte(coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + solNo - 1].value) : coreGlobals.solenoids & CORE_SOLBIT(solNo);
  else if (solNo <= 32) { // 29-32
    if (core_gameData->gen & (GEN_ALLS11 | GEN_PINHECK))
      return coreGlobals.nSolenoids && (options.usemodsol & (CORE_MODOUT_ENABLE_PHYSOUT_SOLENOIDS | CORE_MODOUT_ENABLE_MODSOL | CORE_MODOUT_FORCE_ON)) ? saturatedByte(coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + solNo - 1].value) : coreGlobals.solenoids & CORE_SOLBIT(solNo);
    else if (core_gameData->gen & GEN_ALLWPC) // Remap WPC GameOn/J111 GPIO (hacky)
'''),
    (r'''        return coreGlobals.solenoids & (1<<((solNo - 13)|4));
    }
    if (core_gameData->gen & GEN_ALLS11)
      return coreGlobals.nSolenoids && (options.usemodsol & (CORE_MODOUT_ENABLE_PHYSOUT_SOLENOIDS | CORE_MODOUT_ENABLE_MODSOL | CORE_MODOUT_FORCE_ON)) ? saturatedByte(coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + 32 + solNo - 37 + 8].value) : coreGlobals.solenoids2 & (1<<(solNo - 37 + 8));
  }
''',
     r'''        return coreGlobals.solenoids & (1<<((solNo - 13)|4));
    }
    if (core_gameData->gen & (GEN_ALLS11 | GEN_PINHECK))
      return coreGlobals.nSolenoids && (options.usemodsol & (CORE_MODOUT_ENABLE_PHYSOUT_SOLENOIDS | CORE_MODOUT_ENABLE_MODSOL | CORE_MODOUT_FORCE_ON)) ? saturatedByte(coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + 32 + solNo - 37 + 8].value) : coreGlobals.solenoids2 & (1<<(solNo - 37 + 8));
  }
'''),
    (r'''  else if (core_gameData->gen & GEN_ALLWS) // 33..36 various aux board outputs
     sol |= ((UINT64)(coreGlobals.solenoids2 & 0x00f0)) << 28;
  if (core_gameData->gen & (GEN_ALLS11 | GEN_SAM | GEN_SPA)) // 37-44 S11, SAM extra
     sol |= ((UINT64)(coreGlobals.solenoids2 & 0xff00)) << 28;
  { // 45-48 flipper solenoids (hold coil is set if either coil is set)
''',
     r'''  else if (core_gameData->gen & GEN_ALLWS) // 33..36 various aux board outputs
     sol |= ((UINT64)(coreGlobals.solenoids2 & 0x00f0)) << 28;
  if (core_gameData->gen & (GEN_ALLS11 | GEN_SAM | GEN_SPA | GEN_PINHECK)) // 37-44 S11, SAM extra, pinHeck GI 8-15
     sol |= ((UINT64)(coreGlobals.solenoids2 & 0xff00)) << 28;
  { // 45-48 flipper solenoids (hold coil is set if either coil is set)
'''),
    (r'''      state[i + 4] = coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + i].value;
    }
  else if (core_gameData->gen & (GEN_ALLS11 | GEN_SAM | GEN_SPA)) // 37-44 S11, SAM extra
    for (int i = 40; i < 48; i++)
      state[i - 4] = coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + i].value;
''',
     r'''      state[i + 4] = coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + i].value;
    }
  else if (core_gameData->gen & (GEN_ALLS11 | GEN_SAM | GEN_SPA | GEN_PINHECK)) // 37-44 S11, SAM extra, pinHeck GI 8-15
    for (int i = 40; i < 48; i++)
      state[i - 4] = coreGlobals.physicOutputState[CORE_MODOUT_SOL0 + i].value;
'''),
])
edit('src/libpinmame/libpinmame.h', [
    (r'''	PINMAME_HARDWARE_GEN_SPA = 0x4000000000000,         // Stern PA
	PINMAME_HARDWARE_GEN_P2K = 0x8000000000000,         // Midway Pinball 2000
	PINMAME_HARDWARE_GEN_ALLWPC = 0x00000000000ff,      // All WPC
	PINMAME_HARDWARE_GEN_ALLS11 = 0x000008000ff00,      // All Sys11
''',
     r'''	PINMAME_HARDWARE_GEN_SPA = 0x4000000000000,         // Stern PA
	PINMAME_HARDWARE_GEN_P2K = 0x8000000000000,         // Midway Pinball 2000
	PINMAME_HARDWARE_GEN_PINHECK = 0x10000000000000,    // Spooky pinHeck
	PINMAME_HARDWARE_GEN_ALLWPC = 0x00000000000ff,      // All WPC
	PINMAME_HARDWARE_GEN_ALLS11 = 0x000008000ff00,      // All Sys11
'''),
])
edit('tests/pinheck/link/register_build.py', [
    (r'''    (['src/wpc/pinheck/display.c', 'src/wpc/pinheck/display.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/display.o\n'),
]
changed = []
''',
     r'''    (['src/wpc/pinheck/display.c', 'src/wpc/pinheck/display.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/display.o\n'),
    (['src/wpc/pinheck/board.c', 'src/wpc/pinheck/board.h'],
     'DRVLIBS += $(PINOBJ)/pinheck/board.o\n'),
]
changed = []
'''),
])
edit('tests/pinheck/pic32mx/makefile_link_check.sh', [
    (r'''done
nm --defined-only $B/*.o | awk 'NF == 3 { print $3 }' | sort -u > $B/defined
nm -u $B/*.o | awk '{ print $NF }' | grep -E '^(mips32_|pic32mx_|pic32cpu_|prop_|cat24m01_|p8x32a_|sd_|vfat_|zipsrc_)' | sort -u > $B/needed
missing=$(comm -23 $B/needed $B/defined)
if [ -n "$missing" ]; then echo "makefile link: FAIL, pinHeck objects leave undefined: $missing"; exit 1; fi
''',
     r'''done
nm --defined-only $B/*.o | awk 'NF == 3 { print $3 }' | sort -u > $B/defined
nm -u $B/*.o | awk '{ print $NF }' | grep -E '^(mips32_|pic32mx_|pinheck_board_|pic32cpu_|prop_|cat24m01_|p8x32a_|sd_|vfat_|zipsrc_)' | sort -u > $B/needed
missing=$(comm -23 $B/needed $B/defined)
if [ -n "$missing" ]; then echo "makefile link: FAIL, pinHeck objects leave undefined: $missing"; exit 1; fi
'''),
])
EOF
```

Then: `python3 tests/pinheck/link/register_build.py`
Expected: 19 `updated …` lines (`cmake/*` ×10, `src/pinmame.mak`, `vcproj/*` ×8); a second run prints nothing.

- [ ] **Step 4: Rebuild**

The build reads the root `CMakeLists.txt`, a copy that predates the registration, so copy it again first:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m8a.log 2>&1; tail -1 build/sdl3pinmame/m8a.log
grep -i warning build/sdl3pinmame/m8a.log | grep -cE 'board\.c|pinheck\.c|pic32mx|core\.c'
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 5: The unit suites pass**

Run: `timeout 300 tests/pinheck/board/check.sh 2>&1 | tail -2; timeout 900 tests/pinheck/pic32mx/check.sh 2>&1 | tail -4`
Expected:
```text
board: ok
board: 0 failed
makefile link: ok
symbols: ok
vcxproj: ok
pic32mx: 0 failed
```

- [ ] **Step 6: Machine check 5**

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/board/pinmame_board.sh` (about 22 minutes)
Expected:
```text
timing: 74937 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.580 0.722 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 266 service-test steps, 0 failures
pinmame board: ok
```

- [ ] **Step 7: Nothing else regressed**

Run, each with `timeout 3300` and `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame`: `tests/pinheck/storage/check.sh`, `tests/pinheck/display/check.sh`, `tests/pinheck/audio/check.sh`, `tests/pinheck/link/check.sh`, `tests/pinheck/pic32mx/pinmame_check.sh`, `tests/pinheck/display/pinmame_display.sh`, `tests/pinheck/audio/pinmame_audio.sh`.
Expected: `storage: 0 failed`, `display: 0 failed`, `audio: 0 failed`, `link: 0 failed`; `romset: ok` then `pinmame: ok` (the `pinheck` system set alone never reaches the board block); `pinmame display: ok`; `pinmame audio: ok`. (`mips32` and `p8x32a` sources are untouched by this plan.)

- [ ] **Step 8: Commit**

```bash
git add src/wpc/pinheck.c src/wpc/pinheck.h src/wpc/pinheckgames.c src/wpc/core.c src/libpinmame/libpinmame.h tests/pinheck/board/check.sh tests/pinheck/board/check5.py tests/pinheck/board/pinmame_board.sh tests/pinheck/link/register_build.py tests/pinheck/pic32mx/makefile_link_check.sh src/pinmame.mak cmake vcproj
git commit -m "pinheck: board I/O in PinMAME, spec numbering and machine check 5"
```

### Task 4: Record the findings, prove the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§2.2, §4.5, §9 item 3), `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` (§3.3), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` (the m8 row splits into 8a and 8b)

- [ ] **Step 1: Update the specs and the roadmap**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md', [
    ('- 2× 74HC595 (`CAB_GI_OUT` data, `!DATA_LAT` latch): 16 GI/flasher outputs `GI_0..15` to TIP102s.\n',
     '- 2× 74HC595 (`CAB_GI_OUT` data, `!DATA_LAT` latch): 16 GI/flasher outputs `GI_0..15` to TIP102s.\n'
     '\n'
     'Verified from the board schematic and `DOM_V006.PRG` (Plan 8a):\n'
     '\n'
     '- **Pins.** `PIC_SOL_0..23` = RC2, RC3, RE5, RF3, RE6, RE7, RE8, RC14, RE9, RC4, RA2, RA0, RA1, RA4, RA5, RC13, RG1, RG0, RA6, RA7, RG14, RG12, RG13, RA9. Lamp columns RB0–RB7, rows RB8–RB15. Switch columns RD8–RD15, rows RD0–RD7. `CAB_CLK` RE0, `CAB_LAT` RG8, `CAB_SWITCH_IN` RF0, `CAB_GI_OUT` RG7, `RGB_DATA`/`RGB_CLK` RE1/RE2, `EXT_RGB_DATA`/`EXT_RGB_CLK` RG6/RC1, `SERVO_0..4` RF1, RA10, RF4, RE3, RE4, `ST_LI_GATE` RA3 (high = lit).\n'
     '- **Coil watchdog.** Each IRL530 gate is `PIC_SOL_n` AND `WATCHDOG` (74HC08s). `WATCHDOG` is a CD74HC123 one-shot triggered by a falling `PIC_ENABLE` (RG15, 10 kΩ pull-down), 0.45 × 25 kΩ × 100 µF = 1.125 s, retriggered by the firmware\'s main loop.\n'
     '- **Lamps.** Column high (ULN2803 to TIP107) and row high (TIP102) light a lamp. A timer-2 interrupt every 10,001 cycles (125 µs) blanks `LATB` on its second tick and drives the next column on its third, so each column is lit for 2 of every 24 ticks; a lamp at brightness *b* (0–7) is lit in *b* of every 8 frames.\n'
     '- **Switches.** A timer-3 interrupt drives one column low per interrupt (`LATD = 0xFEFF << c`, columns 0–7 in order), reads `PORTD` and stores the inverted rows: a closed switch reads low on its row while its column is low.\n'
     '- **Cabinet chain.** Per bit the firmware outputs the next GI bit, pulses `CAB_CLK`, then reads `CAB_SWITCH_IN` (low = closed); after 16 bits it pulses `CAB_LAT` low (which also latches the 74HC595s through the inverter). Because each read follows a clock, firmware cabinet switch *n* (1–15) is chain input *n* − 1 (U12 D0 = switch 1, DOOR … U11 D6 = switch 15), switch 0 reads U12\'s unconnected serial input (open) and U11 D7 (`AUX_6`) is never read.\n'
     '- **GI chain.** The firmware shifts its 16-bit GI word least significant bit first, so word bit *n* lands in `GI_(15−n)`: the lamp test\'s `PLAYFIELD GI` values 1–128 are `GI_15`–`GI_8`, `BACKBOX GI` 0–7 are `GI_7`–`GI_0`.\n'
     '- **RGB.** On-board chain: U38 (first) drives the left cabinet RGB, U36 the right; 24 bits per LED, red first, MSB first on the rising clock, latched after 500 µs low. The external chain carries exactly one LED: the only writer, called at the top of `loop()`, sends 255, 255, 255 once (open item 3).\n'
     '- **Servos.** The chipKIT Servo library (timers 4/5, 20 ms frame) pulses 544 µs for 0° and 2,400 µs for 180°. The service menu drives the Noid on servo 0 and the target bank on servo 1 (1,631 µs).\n'),
    ('  - Switch `n` (0–63) = column `n/8`, row `n%8` → PinMAME switch `(n/8+1)*10 + (n%8+1)`. The 16 cabinet inputs → custom columns 0 and 9.\n',
     '  - Switch `n` (0–63) = column `n/8`, row `n%8` → PinMAME switch `(n/8+1)*10 + (n%8+1)`. The 16 cabinet inputs → custom columns 0 and 9: firmware cabinet switch `n` is PinMAME switch `n` (1–8, U12 D0–D7) or `n + 82` (9–15, U11 D0–D6); U11 D7 (`AUX_6`, never read) is 98. The driver registers this numbering as its switch and lamp conversion (PinMAME\'s default is sequential).\n'),
    ('  - Coils 0–23 → solenoids 1–24. GI/flasher 0–7 → solenoids 25–32. GI/flasher 8–15 → solenoids 37–44.\n',
     '  - Coils 0–23 → solenoids 1–24. GI/flasher 0–7 → solenoids 25–32. GI/flasher 8–15 → solenoids 37–44. GI/flasher numbers are the board\'s `GI_0..15` (74HC595 U3 Q0 … U5 Q7); `core.c` reports 37–44 for `GEN_PINHECK` as for System 11.\n'
     '  - Start button lamp (`ST_LI_GATE`) → lamp 91.\n'),
    ('  - Servos 0–4 → custom solenoids 57–61, carrying the pulse width normalised over 1.0–2.0 ms.\n',
     '  - Servos 0–4 → custom solenoids 57–61, carrying the pulse width normalised over 1.0–2.0 ms (the firmware\'s 0° and 180° saturate at 0 and 255). When a servo\'s pulses stop for 60 ms the board reports it detached and the output holds its value.\n'
     '  - External WS2801 LED 0 (red, green, blue) → custom solenoids 62–64.\n'),
    ('3. **External WS2801 chain length** (needed by milestone 8). Read it from the firmware\'s RGB routines. The chain is exposed as further custom solenoids after 61, three per LED.\n',
     '3. **External WS2801 chain length** (resolved by Plan 8a). One LED: `DOM_V006.PRG` writes the external chain only from `0x9D02FFEC` (three bytes), called once at the top of `loop()` with 255, 255, 255. It is custom solenoids 62–64; a longer frame is logged once.\n'),
])
edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    ('| m8 | 8: board I/O (lamps, coils, switches, GI, RGB, servos), playfield simulator, scripted headless game via `-key_script` | Plans 6, 7 | resolves **3** (external WS2801 chain length) | machine check 5 via the firmware\'s service tests; the scripted game plays from coin-up to high-score entry | after Plans 6–7 |\n',
     '| `2026-09-29-pinheck-m8a-board.md` | 8a: board I/O (lamps, coils, switches, cabinet chain, GI, RGB, servos, start lamp), PinMAME time in board callbacks, PORTB/sub-word/I²C carried items, libpinmame `PINHECK` | Plans 6, 7 | resolved **3** (one external WS2801 LED) | machine check 5: `[MXXzzz]`, `[LXXzzz]` and the service-menu solenoid, servo, lamp, RGB and switch tests land on spec §4.5 numbers | written |\n'
     '| m8b | 8b: playfield simulator, keyboard play, scripted headless game via `-key_script` | Plan 8a | none | the scripted game plays from coin-up to high-score entry | after Plan 8a |\n'),
])
edit('docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md', [
    ('- the outlanes and drain back into the trough.\n',
     '- the outlanes and drain back into the trough.\n'
     '\n'
     'Measured by Plan 8a, for the simulator: the firmware times coil pulses in units of its main-loop counter (`[MXXzzz]` clamps 15 × zzz to 255 units, 42.4 ms emulated for any zzz ≥ 17; the solenoid test pulses the knocker 5.5 ms and the pops 10 ms), so the simulator reacts to solenoid edges, not to pulse lengths. The Noid is servo 0 driven as a continuous-rotation servo (544 µs one way, 2,400 µs the other, pulses stopped = stop); the board reports the stop as a detach event.\n'),
])
EOF
```

- [ ] **Step 2: Mutations**

Apply each mutation, run the named suite (board = `tests/pinheck/board/check.sh`, pic32mx = `tests/pinheck/pic32mx/check.sh`), confirm the named failure line (other failure lines may follow), then restore the file with `git checkout <file>`. Unit-level (seconds each):

| File | Replace → with | Suite | Must show |
|---|---|---|---|
| `src/wpc/pinheck/board.c` | `b->lamp_cols = (uint8_t)now;` / `b->lamp_rows = (uint8_t)(now >> 8);` → swap `now` and `now >> 8` | board | `BOARD FAIL board_test.c:117: on == (uint64_t)level * 2 * TICK` |
| `src/wpc/pinheck/board.c` | `(uint64_t)b->hz * 9 / 8;` → `(uint64_t)b->hz;` | board | `BOARD FAIL board_test.c:149: rec.sols == 1` |
| `src/wpc/pinheck/board.c` | `(uint16_t)(b->sr165 << 1 \| 1u);` → `(uint16_t)(b->sr165 << 1);` | board | `BOARD FAIL board_test.c:208: ((in >> k) & 1u) == (unsigned)(i < 15 && k == i + 1)` |
| `src/wpc/pinheck/board.c` | `if (!b->cab_lat && b->sr595 != b->gi) {` → `if (b->cab_lat && b->sr595 != b->gi) {` | board | `BOARD FAIL board_test.c:227: rec.gi == 1 && rec.gi_v == (uint16_t)(0x8000u >> n)` |
| `src/wpc/pinheck/board.c` | `(ch >> 2 & 1u) && (now >> 2 & 1u)` → `(ch >> 2 & 1u) && !(now >> 2 & 1u)` | board | `BOARD FAIL board_test.c:264: rec.rgb == 2 && …` |
| `src/wpc/pinheck/board.c` | `&& !(b->lat[PD] >> (8 + c) & 1u)` → `&& (b->lat[PD] >> (8 + c) & 1u)` | board | `BOARD FAIL board_test.c:318: got[c] == closed[c]` |
| `src/wpc/pinheck/board.c` | in `pinheck_board_tick`, `if (b->io.servo)` after `b->servo_on[c] = 0;` → `if (0)` | board | `BOARD FAIL board_test.c:296: rec.servo == 2 && rec.servo_n == 0 && rec.pulse == 0 && …` |
| `src/cpu/pic32mx/pic32mx.c` | the `host_enter(p);` line in `port_write` → nothing | pic32mx | `SOC FAIL soc_test.c:323: host.seen[i] == 1000 - (int)host.at[i]` |
| `src/cpu/pic32mx/pic32mx.c` | the `p->run_end = …` line in `host_leave` → nothing | pic32mx | `SOC FAIL soc_test.c:328: host.n == 3 && ic <= 0 && ran <= (int)host.at[2] + 4` |
| `src/cpu/pic32mx/pic32mx.c` | the line `if (port == PIC32MX_PORTB) in &= SFR(p, OFF_AD1PCFG);` → nothing | pic32mx | `SOC FAIL soc_test.c:85: rd(0xBF886050u) == 0` |
| `src/cpu/pic32mx/pic32mx.c` | in `merge_base`, `return SFR(p, reg + 0x10);` → `return SFR(p, reg);` | pic32mx | `SOC FAIL soc_test.c:99: rd(0xBF886060u) == 0xFF12` |
| `src/cpu/pic32mx/pic32mx.c` | in `merge_base`, `return timer_read(p, i, sub);` → `return SFR(p, reg);` | pic32mx | `SOC FAIL soc_test.c:117: rd(0xBF800610u) == (0x5600u \| (t & 0xFFu))` |
| `src/cpu/pic32mx/pic32mx.c` | the line `if (reg == 0x00) SFR(p, base) &= ~col;` → nothing | pic32mx | `SOC FAIL soc_test.c:245: (rd(0xBF805300u) & 0x1Fu) == 1u` |

Machine-level (each needs a rebuild, `timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)"`, and a check run; restore and rebuild after each):

| File | Replace → with | Must show |
|---|---|---|
| `src/cpu/pic32mx/pic32mx.c` | the `host_enter(p);` line in `port_write` → nothing | right after the first launch: `timing: 74937 edge pairs less than 1 ms apart, 74930 with the wrong PinMAME time`, exit 1 |
| `src/wpc/core.c` | in `core_getAllPhysicSols`, `(GEN_ALLS11 \| GEN_SAM \| GEN_SPA \| GEN_PINHECK)) // 37-44` → the same without `\| GEN_PINHECK` | `BOARD FAIL: attract flasher GI_14 never reached solenoid 43`, `BOARD FAIL: lamp test GI step at 78.4: (), expected ('S44',)` … `check5: 106 uart commands, 266 service-test steps, 11 failures`, exit 1 |

(Leaving out `soc.icount = &pic32cpu_ICount;` in `pic32cpu_execute` instead is not a usable mutation: the CPU then reports no cycles run, PinMAME's clock stands still and the first launch only ends at its `timeout`.)

Then `git status --short src tests` prints nothing and a final rebuild restores the tested binary.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "Spec: verified board wiring and numbering, open item 3 (one external WS2801 LED), Plan 8b notes"
```

## Self-Review

**Spec coverage** (addendum §3.1, §3.2, §3.4 service tests; parent §4.5, §7 check 5):

| Requirement | Where |
|---|---|
| lamp matrix, PWM integration, 8 levels | Task 2 `lamps_levels` (exact lit time per level from the firmware's own interrupt), Task 3 check 5 `levels` line, `[LXX007]` for all 64, lamp test `LAMP =` 0–63 |
| coils 1–24 | Task 2 `sols_watchdog` (every pin), Task 3 `[MXX020]` for all 24 and the solenoid test's 24 items |
| switch matrix, scan order from the firmware | Task 2 `switch_scan` (the firmware's `0xFEFF << c` sequence), Task 3 switch test for all 64 switches |
| 74HC165 cabinet chain | Task 2 `cabinet_chain` (every input, the firmware's clock-then-read order), Task 3 switch test cabinet column for start, coin, tilt, user, Enter, door, both flippers; Back through the menu |
| 74HC595 GI chain, 37–44 reach PinMAME | Task 2 `gi_chain`, Task 3 lamp test GI steps (all 16) and the attract flasher on 43, `core.c` edit |
| on-board WS2801 → 51–56 | Task 2 `ws2801`, Task 3 RGB test (8 colours) |
| external WS2801, length from the firmware (open item 3) | Ruling 4, Task 3 attract check (62–64 white), Task 4 spec edit |
| servos → 57–61 | Task 2 `servos`, Task 3 servo test (544 µs → 0, 2,400 µs → 255, target bank 161) |
| start lamp | Task 2 `start_lamp`, Task 3 attract check (lamp 91) |
| `pic32cpu_ICount` current, `activecpu_abort_timeslice` | Task 1 `host_icount`, Task 3 `timing` step |
| PORTB per `AD1PCFG`; sub-word `PORTx`/`TMRx`; I²C collision | Task 1 `port_b_analog`, `port_subword`, `timer_subword`, `i2c_collision` |
| libpinmame `PINHECK` | Task 3 (`check.sh` compile-time assertion against `gen.h`) |
| cabinet keys, switch-matrix inputs | Task 3 `PINHECK_COMPORTS`, `SWITCH_UPDATE`, switch test |
| `[MXXzzz]`/`[LXXzzz]` against the documents | Task 3: coil *n* → solenoid *n*+1 and lamp *n* → (n/8+1)·10+n%8+1, which the documents number 0–23 and 0–63 in the same column/row order |
| build registration | Task 3 `register_build.py` |

Out of scope, for Plan 8b: the playfield simulator (`src/wpc/sims/pinheck/dominos.c`), playfield switch keys beyond PinMAME's column/row keys, the scripted game. Plan 8b can use: `board.c`'s callbacks (coil edges with cycle times, servo pulses and detach), the PinMAME outputs above through `core_getSol`, `pinheck_getsol`, and `PINHECK_OUT_LOG`'s `P` lines for its assertions, and the service-menu map in "Firmware facts".

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay.

**Type consistency:** `pinheck_board_io` field order is the same in `board.h`, `board_test.c` and `pinheck.c`'s initialiser; `pinheck_getsol(int)` is declared in `pinheck.h` and defined in `pinheck.c`; `pic32mx.icount`/`run_end` are declared in Task 1 and used by `pic32mxcpu.c` in the same task.

**Review Focus:** five items, each pinned by the named tests in Tasks 1–3.

**Proof:** every file and edit this plan writes was extracted from this document into a fresh worktree of `bab9dd73` and is byte-identical to the proven prototype; every Expected output above was reproduced there, and machine check 5 gave the identical output (timing, levels, 0 failures) in three runs (on `1ec7192c` twice and on `bab9dd73`). The unit mutations of Task 4 were each caught at the named line; the two machine mutations were run on a copy of the same sources.
