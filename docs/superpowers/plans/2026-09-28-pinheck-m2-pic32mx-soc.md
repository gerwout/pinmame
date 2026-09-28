# Milestone 2: PIC32MX SoC and PinMAME skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the real Domino's PIC32 firmware on a PIC32MX795F512L SoC model built on Milestone 1's `mips32` core. Headless, with a stand-in for the Propeller, it must reach the UART1 banner. Inside PinMAME, as game `dominos`, it must boot to the Propeller sync check.

**Architecture:** `src/cpu/pic32mx/pic32mx.c` is a PinMAME-independent SoC. It has a storage-backed SFR space with the PIC32's CLR/SET/INV aliases, an interrupt controller feeding the core's EIC input, lazily advanced Timer1–5, PORTA–G through a board callback, UART1–6, and I²C1–5 as bit-level masters that drive pins through the board with real bit timing. A thin cpuintrf adapter (`pic32mxcpu.c`) registers it with PinMAME as CPU `PIC32MX`, and `src/wpc/pinheck.c` is the machine driver. The Propeller does not exist until Milestone 5. The headless firmware test therefore uses a test-only stub of the Propeller's sync and EEPROM service on the link pins, and the PinMAME driver leaves the link idle, so there the firmware stops at its sync check.

**Tech Stack:** C99 (gcc/clang; C89-clean for VS2012's C compiler), Python 3, POSIX sh, CMake (the user's `sdl3pinmame` build), `zip`.

**Spec:** `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§2.2, §2.3, §4.2, §4.5, §4.6, §6, §7 machine check 1, §8 milestone 2, §9 open item 1). Roadmap: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`.

## Prerequisites

- Milestone 1 (`src/cpu/mips32/`) is on the branch. This plan uses `mips32_regs()`; the struct member `r` no longer exists.
- **Task 2 consumes Plan 3's CAT24M01 model** (`src/wpc/pinheck/eeprom.h`, `eeprom.c`) through the API both plans fixed: `typedef struct cat24m01 {...} cat24m01; void cat24m01_init(cat24m01 *e, uint8_t *mem, int addr_pins); int cat24m01_update(cat24m01 *e, int scl, int sda);`. `mem` is 131072 bytes and `addr_pins` is the A2:A1 strap. `scl`/`sda` are master levels (1 = released), and the return value is the SDA line level. Tasks 1, 3 and 4 do not need it. Execute Task 2 only once Plan 3's EEPROM task has landed.
- The firmware is never committed. Tests read it from `PINHECK_UPDATE_DIR`, the unzipped Domino's update folder that holds `DOM_V006.PRG` and `PRP_V008.BIN`.

## Global Constraints

- `src/cpu/pic32mx/pic32mx.c`/`.h` include only the M1 core header and C standard headers. They must compile with `-std=c99 -Wall -Wextra -Werror -pedantic` and pass `-std=c89 -pedantic-errors -Wno-long-long -fsyntax-only`. Only `pic32mxcpu.c` sees PinMAME headers.
- Clocks: SYSCLK 80 MHz; PBCLK = SYSCLK / 2^OSCCON.PBDIV (reset PBDIV = 0, so 80 MHz). The core timer is the M1 core's `Count`, one tick per 2 cycles.
- Reset: the boot flash holds `lui k0,0x9d00; ori k0,k0,0x1000; jr k0; nop` at `0x1FC00000`, so the CPU starts the firmware at `0x9D001000`. RAM is 128 KB at physical 0. Flash is 512 KB at `0x1D000000`, read-only, and reads past the image return `0xFFFFFFFF`.
- Unmodelled SFRs (addresses outside the modelled blocks) read 0, ignore writes, and are logged once per 16-byte register (spec §6). The stub blocks spec §4.2 lists (OSC, CHECON, BMX, DDPCON, RCON, ADC, NVM, WDT) are storage-backed: the firmware reads back what it wrote.
- Reserved-instruction and other CPU exceptions are delivered as on the M4K and reported to the board once per (code, PC) in the driver (spec §6).
- Interrupt controller: IRQ→vector mapping from Microchip's `PIC32MX795F512L.atdf` (DFP 1.7.380). RIPL = highest IPC priority; ties go to higher subpriority, then lower IRQ. Vector = the IRQ's vector with `INTCON.MVEC`, else 0. Priority 7 runs on shadow set 1 (chipKIT's `FSRSSEL = PRIORITY_7`).
- Link pins (spec open item 1): RF5 = PIC32→Propeller data, RF12 = clock, RF13 = Propeller→PIC32 data. There is no Propeller→PIC32 UART.
- No Propeller behaviour in product code. `tests/pinheck/pic32mx/linkstub.c` is a test fixture only.
- Romset `dominos`: `DOM_V006.PRG` 0x31990 bytes CRC32 `750e27a4` SHA1 `3fbebce7f885563e61dd2e4f5d7f0a8a54c53b23` in `REGION_CPU1` (erased to `0xFF`); `PRP_V008.BIN` 0x8000 bytes CRC32 `a51ee28d` SHA1 `0556d88b6f0c7cb15e648e1f76f4d47890ddb858` in `REGION_USER1`. No `pinheck` BIOS set yet (Plan 3 / Plan 5).
- Code comments stay short; no comment blocks.

## Review Focus

Each line is an input the firmware really produces. Each is pinned by a named test.

- **Pin writes through `LATxSET`/`CLR`/`INV` and `PORTx`** (chipKIT's `digitalWrite` uses `LATxSET`/`LATxCLR`, and a PORT write is a LAT write): only the addressed bits change, and the board sees the new latch. `port_set_clr_inv` in Task 1.
- **A peripheral re-raising its flag while its handler still runs.** The I²C master's completion must arrive 9 bit-times later, not instantly, or the chipKIT `Wire` handler clears it on exit and the boot hangs (it did during bring-up). `i2c_timing_nack` in Task 1.
- **The core-timer handler clears `CTIF` while `Cause.TI` is still asserted.** The flag follows TI as a level, so it re-asserts until `Compare` is written. An edge detector misses the next tick when TI falls and rises inside one slice (it did during bring-up: 1 interrupt in 5 s). `core_timer_level` in Task 1.
- **First boot on a blank EEPROM.** The firmware stores defaults and prints no banner; the banner appears on the next boot. `banner.py`'s boot-1 and boot-2 assertions in Task 2.
- **A 32-bit timer pair with the slave's `ON` bit also set.** In T32 mode `T3CON` must be ignored, and only the pair's T3 IRQ fires. `timer_t32` in Task 1.

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/pic32mx/pic32mx.h`, `pic32mx.c` | the SoC: memory map, SFR space, INTC→EIC, timers, ports, UARTs, I²C masters, run loop |
| `src/cpu/pic32mx/pic32mxcpu.h`, `pic32mxcpu.c` | PinMAME cpuintrf adapter for the single SoC instance (CPU `PIC32MX`, prefix `pic32cpu_`) |
| `src/wpc/pinheck.h`, `pinheck.c` | machine driver `PINHECK`: board wiring (UART1 log, idle link), 128×32 `CORE_VIDEO` placeholder |
| `src/wpc/pinheckgames.c` | game `dominos` and its romset |
| `tests/pinheck/pic32mx/soc_test.c` | SoC unit tests |
| `tests/pinheck/pic32mx/linkstub.h`, `linkstub.c` | test-only stand-in for the Propeller's link service |
| `tests/pinheck/pic32mx/boot.c` | headless multi-boot firmware runner (`pic32boot`) |
| `tests/pinheck/pic32mx/banner.py` | machine check 1 |
| `tests/pinheck/pic32mx/pins.py` | open item 1: chipKIT pin tables → port bits |
| `tests/pinheck/pic32mx/check.sh`, `pinmame_check.sh`, `.gitignore` | test drivers |
| build registration | `src/cpuintrf.[ch]`, `src/pinmame.h`, `src/pinmame.mak`, `src/rules.mak`, `src/wpc/gen.h`, `src/wpc/driver.c`, the 10 cmake lists that build CPUs, and the 4 VS2012/2015 projects with their `.filters` |

---
### Task 1: PIC32MX SoC model

**Files:**
- Create: `src/cpu/pic32mx/pic32mx.h`, `src/cpu/pic32mx/pic32mx.c`
- Test: `tests/pinheck/pic32mx/soc_test.c`, `tests/pinheck/pic32mx/check.sh`, `tests/pinheck/pic32mx/.gitignore`

**Interfaces:**
- Consumes: `mips32_init/reset/run/set_eic/timer_irq/translate/regs`, `mips32_state` fields `cycles`, `stop`, `bus`, `eic_ripl`, `cur_pc`, `cause`, `compare`, from Milestone 1.
- Produces:
  - **Setup:** `pic32mx_init(pic32mx *p, const pic32mx_board *board, const uint8_t *flash, uint32_t flash_size)`, `pic32mx_reset(p)`.
  - **Running:** `int pic32mx_run(pic32mx *p, int cycles)` returns cycles run, never less than asked.
  - **Test and board hooks:** `pic32mx_uart_rx(p, uart_index_0_based, byte)`, `pic32mx_set_irq(p, irq)`, `pic32mx_irq_vector(irq)`, `pic32mx_sfr_peek(p, va)`.
  - **Board callbacks** (`pic32mx_board`):
    - `port_write(ctx, port, lat, tris, cycle)`, called when LAT or TRIS changes
    - `port_read(ctx, port, cycle)`, returning the input pin levels
    - `uart_tx(ctx, uart_1_based, byte, cycle)`
    - `i2c_pins(ctx, module_1_based, scl, sda, cycle)`, returning the SDA line level
    - `unmapped(ctx, pa, write)`
    - `exception(ctx, code, pc)`
  - **Counters:** `pic32mx.vec_count[64]`, `pic32mx.exc_count`.

- [ ] **Step 1: Write the header**

`src/cpu/pic32mx/pic32mx.h`:

```c
#ifndef PIC32MX_H
#define PIC32MX_H

#include "../mips32/mips32.h"

#define PIC32MX_RAM_SIZE   0x20000u
#define PIC32MX_FLASH_SIZE 0x80000u
#define PIC32MX_BOOT_SIZE  0x3000u
#define PIC32MX_SFR_BASE   0x1F800000u
#define PIC32MX_SFR_SIZE   0x90000u
#define PIC32MX_SYSCLK     80000000u
#define PIC32MX_UARTS      6
#define PIC32MX_RXFIFO     8
#define PIC32MX_IRQS       76
#define PIC32MX_VECTORS    64
#define PIC32MX_I2CS       5

enum { PIC32MX_PORTA, PIC32MX_PORTB, PIC32MX_PORTC, PIC32MX_PORTD, PIC32MX_PORTE, PIC32MX_PORTF, PIC32MX_PORTG, PIC32MX_PORTS };

typedef struct pic32mx_board {
	void *ctx;
	void (*port_write)(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle);
	uint32_t (*port_read)(void *ctx, int port, uint64_t cycle);
	void (*uart_tx)(void *ctx, int uart, uint8_t byte, uint64_t cycle);
	int (*i2c_pins)(void *ctx, int module, int scl, int sda, uint64_t cycle);
	void (*unmapped)(void *ctx, uint32_t pa, int write);
	void (*exception)(void *ctx, int code, uint32_t pc);
} pic32mx_board;

typedef struct pic32mx_timer {
	uint32_t tmr, frac;
	uint64_t last;
} pic32mx_timer;

typedef struct pic32mx_i2c {
	int pending;
	uint64_t done_at;
	uint32_t con_clear, stat_set, stat_clear, rcv;
} pic32mx_i2c;

typedef struct pic32mx_uart {
	uint8_t rx[PIC32MX_RXFIFO];
	int rx_head, rx_count;
} pic32mx_uart;

typedef struct pic32mx {
	mips32_state cpu;
	pic32mx_board board;
	const uint8_t *flash;
	uint32_t flash_size;
	uint8_t ram[PIC32MX_RAM_SIZE];
	uint8_t boot[PIC32MX_BOOT_SIZE];
	uint32_t sfr[PIC32MX_SFR_SIZE / 4];
	pic32mx_timer timer[5];
	pic32mx_uart uart[PIC32MX_UARTS];
	pic32mx_i2c i2c[PIC32MX_I2CS];
	uint64_t vec_count[PIC32MX_VECTORS];
	uint64_t exc_count;
	uint8_t logged[PIC32MX_SFR_SIZE / 16];
} pic32mx;

void pic32mx_init(pic32mx *p, const pic32mx_board *board, const uint8_t *flash, uint32_t flash_size);
void pic32mx_reset(pic32mx *p);
int pic32mx_run(pic32mx *p, int cycles);
void pic32mx_uart_rx(pic32mx *p, int uart, uint8_t byte);
void pic32mx_set_irq(pic32mx *p, int irq);
int pic32mx_irq_vector(int irq);
uint32_t pic32mx_sfr_peek(const pic32mx *p, uint32_t va);

#endif
```

- [ ] **Step 2: Write the failing unit tests and the test driver**

`check.sh` gains its firmware and pin checks automatically in Tasks 2 and 4. They are keyed on `boot.c` and `pins.py` existing.

`tests/pinheck/pic32mx/soc_test.c`:

```c
#include "pic32mx.h"
#include <stdio.h>
#include <string.h>

static pic32mx soc;
static uint8_t flash[PIC32MX_FLASH_SIZE];
static int fails;
static struct {
	int port_writes, port_last, tx_count, tx_uart, unmapped, exc_count, exc_code;
	uint32_t port_lat, port_in, exc_pc;
	uint8_t tx_byte;
} rec;

#define CHECK(c) do { if (!(c)) { printf("SOC FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void b_port_write(void *c, int port, uint32_t lat, uint32_t tris, uint64_t cy) { (void)c; (void)tris; (void)cy; rec.port_writes++; rec.port_last = port; rec.port_lat = lat; }
static uint32_t b_port_read(void *c, int port, uint64_t cy) { (void)c; (void)port; (void)cy; return rec.port_in; }
static void b_uart_tx(void *c, int u, uint8_t b, uint64_t cy) { (void)c; (void)cy; rec.tx_count++; rec.tx_uart = u; rec.tx_byte = b; }
static void b_unmapped(void *c, uint32_t pa, int w) { (void)c; (void)pa; (void)w; rec.unmapped++; }
static void b_exception(void *c, int code, uint32_t pc) { (void)c; if (!rec.exc_count++) { rec.exc_code = code; rec.exc_pc = pc; } }

static uint32_t rd(uint32_t va)
{
	int err = 0;
	return soc.cpu.bus.read(soc.cpu.bus.ctx, va & 0x1FFFFFFFu, 4, 0, &err);
}

static void wr(uint32_t va, uint32_t v)
{
	int err = 0;
	soc.cpu.bus.write(soc.cpu.bus.ctx, va & 0x1FFFFFFFu, v, 4, &err);
}

static void put(uint32_t off, uint32_t w)
{
	flash[off] = (uint8_t)w; flash[off + 1] = (uint8_t)(w >> 8); flash[off + 2] = (uint8_t)(w >> 16); flash[off + 3] = (uint8_t)(w >> 24);
}

static void setup(void)
{
	pic32mx_board board = { NULL, b_port_write, b_port_read, b_uart_tx, NULL, b_unmapped, b_exception };
	memset(&rec, 0, sizeof(rec));
	memset(flash, 0, sizeof(flash));
	put(0x1000, 0x1000FFFFu);
	pic32mx_init(&soc, &board, flash, sizeof(flash));
}

static int ifs(int irq) { return (rd(0xBF881030u + (uint32_t)(irq >> 5) * 0x10) >> (irq & 31)) & 1; }

static void port_set_clr_inv(void)
{
	setup();
	wr(0xBF886000u, 0);
	wr(0xBF886028u, 0x0005);
	wr(0xBF886024u, 0x0001);
	wr(0xBF88602Cu, 0x0006);
	CHECK(rd(0xBF886020u) == 0x0002);
	CHECK(rd(0xBF886010u) == 0x0002);
	wr(0xBF886018u, 0x0008);
	CHECK(rd(0xBF886020u) == 0x000A);
	CHECK(rec.port_last == PIC32MX_PORTA && rec.port_lat == 0x000A);
}

static void port_input_mix(void)
{
	setup();
	wr(0xBF886040u, 0x00F0);
	wr(0xBF886060u, 0xFFFF);
	rec.port_in = 0x0050;
	CHECK(rd(0xBF886050u) == 0xFF5F);
}

static void timer_period(void)
{
	setup();
	wr(0xBF800820u, 99);
	wr(0xBF800800u, 0x8000);
	pic32mx_run(&soc, 1050);
	CHECK(ifs(8));
	CHECK(rd(0xBF800810u) == 50);
}

static void timer_prescale_pbdiv(void)
{
	setup();
	wr(0xBF80F000u, (rd(0xBF80F000u) & ~(3u << 19)) | (2u << 19));
	wr(0xBF800820u, 9);
	wr(0xBF800800u, 0x8030);
	pic32mx_run(&soc, 700);
	CHECK(ifs(8));
	CHECK(rd(0xBF800810u) == 1);
}

static void timer_t32(void)
{
	setup();
	wr(0xBF800820u, 0x0000);
	wr(0xBF800A20u, 0x0001);
	wr(0xBF800A00u, 0x8000);
	wr(0xBF800800u, 0x8008);
	pic32mx_run(&soc, 100);
	CHECK(!ifs(12));
	pic32mx_run(&soc, 69900);
	CHECK(ifs(12));
	CHECK(!ifs(8));
	CHECK(rd(0xBF800810u) == 70000 - 65537);
	CHECK(rd(0xBF800A10u) == 0);
}

static void intc_priority(void)
{
	setup();
	wr(0xBF881008u, 1u << 12);
	wr(0xBF881068u, (1u << 8) | (1u << 12));
	wr(0xBF8810B0u, (3u << 2) | 1u);
	wr(0xBF8810C0u, (3u << 2) | 2u);
	wr(0xBF881038u, (1u << 8) | (1u << 12));
	CHECK(soc.cpu.eic_ripl == 3 && soc.cpu.eic_vector == 12 && soc.cpu.eic_srs == 0);
	wr(0xBF8810B0u, (5u << 2));
	CHECK(soc.cpu.eic_ripl == 5 && soc.cpu.eic_vector == 8);
	wr(0xBF881004u, 1u << 12);
	CHECK(soc.cpu.eic_ripl == 5 && soc.cpu.eic_vector == 0);
	wr(0xBF8810B0u, (7u << 2));
	CHECK(soc.cpu.eic_ripl == 7 && soc.cpu.eic_srs == 1);
	wr(0xBF881034u, (1u << 8) | (1u << 12));
	CHECK(soc.cpu.eic_ripl == 0);
}

static void core_timer_level(void)
{
	setup();
	soc.cpu.compare = 100;
	pic32mx_run(&soc, 300);
	CHECK(ifs(0));
	wr(0xBF881034u, 1u);
	pic32mx_run(&soc, 10);
	CHECK(ifs(0));
	soc.cpu.compare = 100000;
	soc.cpu.cause &= ~0x40000000u;
	wr(0xBF881034u, 1u);
	pic32mx_run(&soc, 10);
	CHECK(!ifs(0));
}

static void uart_tx_rx(void)
{
	int i;
	setup();
	wr(0xBF806000u, 0x8000);
	wr(0xBF806010u, (1u << 10) | (1u << 12));
	wr(0xBF806020u, 'A');
	CHECK(rec.tx_count == 1 && rec.tx_uart == 1 && rec.tx_byte == 'A');
	CHECK(ifs(28));
	pic32mx_uart_rx(&soc, 0, 'x');
	CHECK((rd(0xBF806010u) & 1) == 1);
	CHECK(ifs(27));
	CHECK(rd(0xBF806030u) == 'x');
	CHECK((rd(0xBF806010u) & 1) == 0);
	for (i = 0; i < 9; i++) pic32mx_uart_rx(&soc, 0, (uint8_t)i);
	CHECK(rd(0xBF806010u) & 2u);
	wr(0xBF806800u, 0x8000);
	wr(0xBF806810u, 1u << 10);
	wr(0xBF806820u, 'B');
	CHECK(rec.tx_uart == 2 && rec.tx_byte == 'B');
	CHECK(ifs(42));
}

static void i2c_timing_nack(void)
{
	setup();
	wr(0xBF805340u, 10);
	wr(0xBF805300u, 0x8000);
	wr(0xBF805308u, 1u);
	CHECK(!ifs(31));
	CHECK(rd(0xBF805300u) & 1u);
	pic32mx_run(&soc, 30);
	CHECK(ifs(31));
	CHECK(!(rd(0xBF805300u) & 1u));
	CHECK(rd(0xBF805310u) & (1u << 3));
	wr(0xBF881034u, 1u << 31);
	wr(0xBF805350u, 0xA0);
	CHECK(rd(0xBF805310u) & (1u << 14));
	pic32mx_run(&soc, 100);
	CHECK(!ifs(31));
	pic32mx_run(&soc, 200);
	CHECK(ifs(31));
	CHECK(rd(0xBF805310u) & (1u << 15));
	CHECK(!(rd(0xBF805310u) & (1u << 14)));
}

static void unmapped_sfr(void)
{
	setup();
	CHECK(rd(0xBF800200u) == 0);
	CHECK(rd(0xBF800200u) == 0);
	wr(0xBF800200u, 0x1234);
	CHECK(rd(0xBF800200u) == 0);
	CHECK(rec.unmapped == 1);
	CHECK(soc.logged[0x200 >> 4] == 1);
}

static void reserved_instruction(void)
{
	setup();
	put(0x1000, 0x0000003Fu);
	pic32mx_run(&soc, 20);
	CHECK(rec.exc_count > 0);
	CHECK(rec.exc_code == MIPS32_EXC_RI && rec.exc_pc == 0x9D001000u);
}

int main(void)
{
	port_set_clr_inv();
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
	printf("soc: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

`tests/pinheck/pic32mx/check.sh`:

```sh
#!/bin/sh
if [ -n "$PINHECK_UPDATE_DIR" ]; then PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2; fi
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic -I$S/cpu/pic32mx"
mkdir -p $B
fail=0
cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only $S/cpu/pic32mx/pic32mx.c || { echo "C89 FAIL pic32mx.c"; fail=$((fail + 1)); }
cc $CF -o $B/soc_test soc_test.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c || exit 2
./$B/soc_test || fail=$((fail + 1))
if [ -f boot.c ]; then
	if [ -n "$PINHECK_UPDATE_DIR" ]; then
		cc $CF -o $B/pic32boot boot.c linkstub.c $S/wpc/pinheck/eeprom.c $S/cpu/pic32mx/pic32mx.c $S/cpu/mips32/mips32.c || exit 2
		./$B/pic32boot -boots 2 -c 800000000 "$PINHECK_UPDATE_DIR/DOM_V006.PRG" > $B/boot.txt || { echo "BOOT FAIL: CPU exceptions during boot"; fail=$((fail + 1)); }
		python3 banner.py $B/boot.txt || fail=$((fail + 1))
		if [ -f pins.py ]; then python3 pins.py "$PINHECK_UPDATE_DIR/DOM_V006.PRG" || fail=$((fail + 1)); fi
	elif [ "$PINHECK_SKIP_FIRMWARE" != 1 ]; then
		echo "FIRMWARE MISSING: set PINHECK_UPDATE_DIR to the unzipped Domino's update, or PINHECK_SKIP_FIRMWARE=1"
		fail=$((fail + 1))
	fi
fi
echo "pic32mx: $fail failed"
[ $fail -eq 0 ]
```

Make it executable: `chmod +x tests/pinheck/pic32mx/check.sh`

`tests/pinheck/pic32mx/.gitignore`:

```text
build/
```

- [ ] **Step 3: Run it to verify it fails**

Run: `tests/pinheck/pic32mx/check.sh`
Expected: exit 2, with
```text
cc1: fatal error: ../../../src/cpu/pic32mx/pic32mx.c: No such file or directory
C89 FAIL pic32mx.c
```

- [ ] **Step 4: Write the SoC**

Addresses, IRQ numbers and vectors come from Microchip's device family pack for this part (`PIC32MX_DFP` 1.7.380: `p32mx795f512l.h` and `PIC32MX795F512L.atdf`). UART bases are **not** in numeric order (UART2 is at `0xBF806800`, UART4 at `0xBF806200`), and I2C1 is at `0xBF805300`.

`src/cpu/pic32mx/pic32mx.c`:

```c
#include "pic32mx.h"
#include <string.h>

#define SFR(p, off)     ((p)->sfr[(off) >> 2])
#define OFF_INTCON      0x81000u
#define OFF_INTSTAT     0x81010u
#define OFF_IFS0        0x81030u
#define OFF_IEC0        0x81060u
#define OFF_IPC0        0x81090u
#define OFF_OSCCON      0x0F000u
#define OFF_RCON        0x0F600u
#define OFF_TRISA       0x86000u
#define INTCON_MVEC     (1u << 12)
#define CON_ON          (1u << 15)

static const uint8_t irq_vector[PIC32MX_IRQS] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 23, 23,
	24, 24, 24, 25, 25, 25, 26, 27, 28, 29, 30, 31, 31, 31, 32, 32, 32, 33, 33, 33, 34, 35, 36, 37,
	38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 5, 9, 13, 17, 21, 28, 49, 49, 49, 50, 50, 50, 51, 51, 51
};

static const uint32_t uart_base[PIC32MX_UARTS] = { 0x6000, 0x6800, 0x6400, 0x6200, 0x6A00, 0x6600 };
static const uint8_t uart_irq[PIC32MX_UARTS] = { 26, 40, 37, 67, 73, 70 };
static const uint32_t i2c_base[PIC32MX_I2CS] = { 0x5300, 0x5400, 0x5000, 0x5100, 0x5200 };
static const uint8_t i2c_irq[PIC32MX_I2CS] = { 29, 43, 26, 37, 40 };
static const uint8_t timer_irq[5] = { 4, 8, 12, 16, 20 };
static const uint16_t tckps_a[4] = { 1, 8, 64, 256 };
static const uint16_t tckps_b[8] = { 1, 2, 4, 8, 16, 32, 64, 256 };

static const struct { uint32_t lo, hi; } known[] = {
	{ 0x00000, 0x00010 }, { 0x00600, 0x01000 }, { 0x05000, 0x05500 }, { 0x06000, 0x06C00 },
	{ 0x09000, 0x09100 }, { 0x0F000, 0x0F800 }, { 0x81000, 0x82000 }, { 0x82000, 0x82100 },
	{ 0x84000, 0x84100 }, { 0x86000, 0x86200 }
};

int pic32mx_irq_vector(int irq) { return irq >= 0 && irq < PIC32MX_IRQS ? irq_vector[irq] : -1; }

static void update_eic(pic32mx *p)
{
	int irq, best_pri = 0, best_sub = -1, best_vec = 0, mvec = (SFR(p, OFF_INTCON) & INTCON_MVEC) != 0;

	for (irq = 0; irq < PIC32MX_IRQS; irq++) {
		uint32_t bit = 1u << (irq & 31), w = (uint32_t)(irq >> 5) * 0x10;
		if (SFR(p, OFF_IFS0 + w) & SFR(p, OFF_IEC0 + w) & bit) {
			int vec = irq_vector[irq];
			uint32_t ipc = SFR(p, OFF_IPC0 + (uint32_t)(vec >> 2) * 0x10) >> ((vec & 3) * 8);
			int pri = (int)((ipc >> 2) & 7), sub = (int)(ipc & 3);
			if (pri > best_pri || (pri == best_pri && pri && sub > best_sub)) {
				best_pri = pri;
				best_sub = sub;
				best_vec = vec;
			}
		}
	}
	mips32_set_eic(&p->cpu, best_pri, mvec ? best_vec : 0, best_pri == 7 ? 1 : 0);
}

void pic32mx_set_irq(pic32mx *p, int irq)
{
	SFR(p, OFF_IFS0 + (uint32_t)(irq >> 5) * 0x10) |= 1u << (irq & 31);
	update_eic(p);
}

static uint32_t pbdiv(const pic32mx *p) { return 1u << ((SFR(p, OFF_OSCCON) >> 19) & 3); }

static int timer_t32(const pic32mx *p, int i)
{
	return (i == 1 || i == 3) && (SFR(p, 0x0600u + (uint32_t)i * 0x200) & 8u);
}

static int timer_slave(const pic32mx *p, int i) { return (i == 2 || i == 4) && timer_t32(p, i - 1); }

static void timer_cfg(const pic32mx *p, int i, uint32_t *div, uint32_t *pr, uint32_t *max)
{
	uint32_t base = 0x0600u + (uint32_t)i * 0x200, con = SFR(p, base);
	*div = (i == 0 ? tckps_a[(con >> 4) & 3] : tckps_b[(con >> 4) & 7]) * pbdiv(p);
	if (timer_t32(p, i)) {
		*pr = (SFR(p, base + 0x20) & 0xFFFFu) | (SFR(p, base + 0x220) << 16);
		*max = 0xFFFFFFFFu;
	} else {
		*pr = SFR(p, base + 0x20) & 0xFFFFu;
		*max = 0xFFFFu;
	}
}

static uint32_t steps_to_match(uint32_t t, uint32_t pr, uint32_t max)
{
	return t <= pr ? pr - t + 1 : (max - t) + pr + 2;
}

static void timer_sync(pic32mx *p, int i)
{
	pic32mx_timer *t = &p->timer[i];
	uint32_t div, pr, max, s;
	uint64_t n = p->cpu.cycles - t->last, ticks, events = 0;

	t->last = p->cpu.cycles;
	if (!(SFR(p, 0x0600u + (uint32_t)i * 0x200) & CON_ON) || timer_slave(p, i) || !n) return;
	timer_cfg(p, i, &div, &pr, &max);
	ticks = (n + t->frac) / div;
	t->frac = (uint32_t)((n + t->frac) % div);
	s = steps_to_match(t->tmr, pr, max);
	if (ticks >= s) {
		ticks -= s;
		t->tmr = 0;
		events = 1 + ticks / ((uint64_t)pr + 1);
		ticks %= (uint64_t)pr + 1;
	}
	t->tmr = (uint32_t)((t->tmr + ticks) & max);
	if (events) pic32mx_set_irq(p, timer_irq[timer_t32(p, i) ? i + 1 : i]);
}

static void timers_sync(pic32mx *p)
{
	int i;
	for (i = 0; i < 5; i++) timer_sync(p, i);
}

static uint64_t next_timer_event(const pic32mx *p)
{
	uint64_t best = ~(uint64_t)0;
	uint32_t div, pr, max;
	int i;

	for (i = 0; i < 5; i++) {
		const pic32mx_timer *t = &p->timer[i];
		uint64_t c;
		if (!(SFR(p, 0x0600u + (uint32_t)i * 0x200) & CON_ON) || timer_slave(p, i)) continue;
		timer_cfg(p, i, &div, &pr, &max);
		c = (uint64_t)steps_to_match(t->tmr, pr, max) * div - t->frac;
		if (c < best) best = c;
	}
	return best;
}

static int timer_reg(uint32_t off, int *i, uint32_t *reg)
{
	if (off < 0x0600u || off >= 0x1000u) return 0;
	*i = (int)((off - 0x0600u) >> 9);
	*reg = (off - 0x0600u) & 0x1F0u;
	return 1;
}

static uint32_t timer_read(pic32mx *p, int i, uint32_t reg)
{
	if (reg == 0x10) {
		timers_sync(p);
		if (timer_slave(p, i)) return p->timer[i - 1].tmr >> 16;
		return p->timer[i].tmr & 0xFFFFu;
	}
	return SFR(p, 0x0600u + (uint32_t)i * 0x200 + reg);
}

static void uart_update_rx_irq(pic32mx *p, int u)
{
	if (p->uart[u].rx_count) pic32mx_set_irq(p, uart_irq[u] + 1);
}

void pic32mx_uart_rx(pic32mx *p, int uart, uint8_t byte)
{
	pic32mx_uart *u = &p->uart[uart];
	uint32_t base = uart_base[uart];

	if (!(SFR(p, base) & CON_ON) || !(SFR(p, base + 0x10) & (1u << 12))) return;
	if (u->rx_count == PIC32MX_RXFIFO) { SFR(p, base + 0x10) |= 2u; return; }
	u->rx[(u->rx_head + u->rx_count) % PIC32MX_RXFIFO] = byte;
	u->rx_count++;
	uart_update_rx_irq(p, uart);
}

static int uart_reg(uint32_t off, int *u, uint32_t *reg)
{
	int i;
	for (i = 0; i < PIC32MX_UARTS; i++)
		if (off >= uart_base[i] && off < uart_base[i] + 0x50) { *u = i; *reg = (off - uart_base[i]) & 0xF0u; return 1; }
	return 0;
}

static uint32_t uart_read(pic32mx *p, int u, uint32_t reg)
{
	uint32_t base = uart_base[u];
	pic32mx_uart *q = &p->uart[u];

	if (reg == 0x10)
		return (SFR(p, base + 0x10) & 0xFCE2u) | (1u << 8) | (1u << 4) | (q->rx_count ? 1u : 0u);
	if (reg == 0x30) {
		uint32_t v = 0;
		if (q->rx_count) {
			v = q->rx[q->rx_head];
			q->rx_head = (q->rx_head + 1) % PIC32MX_RXFIFO;
			q->rx_count--;
			uart_update_rx_irq(p, u);
		}
		return v;
	}
	return SFR(p, base + reg);
}

static void uart_write(pic32mx *p, int u, uint32_t reg, uint32_t old, uint32_t v)
{
	uint32_t base = uart_base[u];

	if (reg == 0x20) {
		if ((SFR(p, base) & CON_ON) && (SFR(p, base + 0x10) & (1u << 10))) {
			if (p->board.uart_tx) p->board.uart_tx(p->board.ctx, u + 1, (uint8_t)v, p->cpu.cycles);
			pic32mx_set_irq(p, uart_irq[u] + 2);
		}
	} else if (reg == 0x10 && !(old & (1u << 10)) && (v & (1u << 10)))
		pic32mx_set_irq(p, uart_irq[u] + 2);
}

static int i2c_pins(pic32mx *p, int m, int scl, int sda)
{
	return p->board.i2c_pins ? p->board.i2c_pins(p->board.ctx, m + 1, scl, sda, p->cpu.cycles) : sda;
}

static int i2c_reg(uint32_t off, int *m, uint32_t *reg)
{
	int i;
	for (i = 0; i < PIC32MX_I2CS; i++)
		if (off >= i2c_base[i] && off < i2c_base[i] + 0x70) { *m = i; *reg = (off - i2c_base[i]) & 0xF0u; return 1; }
	return 0;
}

static void i2c_finish_later(pic32mx *p, int m, int bits, uint32_t con_clear, uint32_t stat_set, uint32_t stat_clear)
{
	pic32mx_i2c *q = &p->i2c[m];
	uint32_t brg = SFR(p, i2c_base[m] + 0x40) & 0xFFFu;
	q->pending = 1;
	q->done_at = p->cpu.cycles + (uint64_t)bits * 2 * (brg + 2) * pbdiv(p);
	q->con_clear = con_clear;
	q->stat_set = stat_set;
	q->stat_clear = stat_clear;
	p->cpu.stop = 1;
}

static void i2c_complete(pic32mx *p, int m)
{
	pic32mx_i2c *q = &p->i2c[m];
	uint32_t base = i2c_base[m];
	q->pending = 0;
	SFR(p, base) &= ~q->con_clear;
	SFR(p, base + 0x10) = (SFR(p, base + 0x10) & ~q->stat_clear) | q->stat_set;
	if (q->stat_set & 2u) SFR(p, base + 0x60) = q->rcv;
	pic32mx_set_irq(p, i2c_irq[m] + 2);
}

static void i2c_sync(pic32mx *p)
{
	int m;
	for (m = 0; m < PIC32MX_I2CS; m++)
		if (p->i2c[m].pending && p->cpu.cycles >= p->i2c[m].done_at) i2c_complete(p, m);
}

static uint64_t next_i2c_event(const pic32mx *p)
{
	uint64_t best = ~(uint64_t)0;
	int m;
	for (m = 0; m < PIC32MX_I2CS; m++)
		if (p->i2c[m].pending) {
			uint64_t c = p->i2c[m].done_at > p->cpu.cycles ? p->i2c[m].done_at - p->cpu.cycles : 0;
			if (c < best) best = c;
		}
	return best;
}

static void i2c_write(pic32mx *p, int m, uint32_t reg, uint32_t v)
{
	uint32_t base = i2c_base[m], con = SFR(p, base);
	int i, b, ack;

	if (!(con & CON_ON) || p->i2c[m].pending) return;
	if (reg == 0x50) {
		for (i = 7; i >= 0; i--) {
			b = (int)((v >> i) & 1);
			i2c_pins(p, m, 0, b); i2c_pins(p, m, 1, b); i2c_pins(p, m, 0, b);
		}
		i2c_pins(p, m, 0, 1);
		ack = i2c_pins(p, m, 1, 1);
		i2c_pins(p, m, 0, 1);
		SFR(p, base + 0x10) |= (1u << 14) | 1u;
		i2c_finish_later(p, m, 9, 0, ack ? (1u << 15) : 0, (1u << 14) | 1u | (ack ? 0 : (1u << 15)));
		return;
	}
	if (reg != 0x00) return;
	if (con & 1u) {
		i2c_pins(p, m, 1, 1); i2c_pins(p, m, 1, 0); i2c_pins(p, m, 0, 0);
		i2c_finish_later(p, m, 1, 1u, 1u << 3, 1u << 4);
	} else if (con & 2u) {
		i2c_pins(p, m, 0, 1); i2c_pins(p, m, 1, 1); i2c_pins(p, m, 1, 0); i2c_pins(p, m, 0, 0);
		i2c_finish_later(p, m, 1, 2u, 1u << 3, 0);
	} else if (con & 4u) {
		i2c_pins(p, m, 0, 0); i2c_pins(p, m, 1, 0); i2c_pins(p, m, 1, 1);
		i2c_finish_later(p, m, 1, 4u, 1u << 4, 1u << 3);
	} else if (con & 8u) {
		uint32_t byte = 0;
		for (i = 0; i < 8; i++) {
			i2c_pins(p, m, 0, 1);
			byte = (byte << 1) | (uint32_t)i2c_pins(p, m, 1, 1);
		}
		i2c_pins(p, m, 0, 1);
		p->i2c[m].rcv = byte;
		i2c_finish_later(p, m, 8, 8u, 2u, 0);
	} else if (con & 16u) {
		b = (con & 32u) ? 1 : 0;
		i2c_pins(p, m, 0, b); i2c_pins(p, m, 1, b); i2c_pins(p, m, 0, b);
		i2c_finish_later(p, m, 1, 16u, 0, 0);
	}
}

static uint32_t port_read(pic32mx *p, uint32_t off)
{
	int port = (int)((off - OFF_TRISA) >> 6);
	uint32_t reg = (off - OFF_TRISA) & 0x30u, tris = SFR(p, OFF_TRISA + (uint32_t)port * 0x40);
	if (reg == 0x10) {
		uint32_t in = p->board.port_read ? p->board.port_read(p->board.ctx, port, p->cpu.cycles) : 0xFFFFu;
		return (SFR(p, OFF_TRISA + (uint32_t)port * 0x40 + 0x20) & ~tris) | (in & tris);
	}
	return SFR(p, off & ~0xFu);
}

static void port_write(pic32mx *p, uint32_t off, uint32_t old, uint32_t v)
{
	int port = (int)((off - OFF_TRISA) >> 6);
	uint32_t base = OFF_TRISA + (uint32_t)port * 0x40;

	if (((off - OFF_TRISA) & 0x30u) == 0x30 || old == v) return;
	if (p->board.port_write)
		p->board.port_write(p->board.ctx, port, SFR(p, base + 0x20), SFR(p, base), p->cpu.cycles);
}

static int is_known(uint32_t off)
{
	unsigned i;
	for (i = 0; i < sizeof(known) / sizeof(known[0]); i++)
		if (off >= known[i].lo && off < known[i].hi) return 1;
	return 0;
}

static void log_unmapped(pic32mx *p, uint32_t off, int write)
{
	if (p->logged[off >> 4]) return;
	p->logged[off >> 4] = 1;
	if (p->board.unmapped) p->board.unmapped(p->board.ctx, PIC32MX_SFR_BASE + off, write);
}

static uint32_t sfr_read(pic32mx *p, uint32_t off)
{
	int i;
	uint32_t reg;

	if (!is_known(off)) { log_unmapped(p, off, 0); return 0; }
	if (off & 0xCu) return 0;
	if (timer_reg(off, &i, &reg)) return timer_read(p, i, reg);
	if (uart_reg(off, &i, &reg)) return uart_read(p, i, reg);
	if (i2c_reg(off, &i, &reg) && reg == 0x60) SFR(p, i2c_base[i] + 0x10) &= ~2u;
	if (off >= OFF_TRISA && off < OFF_TRISA + PIC32MX_PORTS * 0x40) return port_read(p, off);
	return SFR(p, off);
}

static void sfr_write(pic32mx *p, uint32_t off, uint32_t v)
{
	uint32_t reg = off & ~0xFu, old, nv;
	int i;
	uint32_t sub;

	if (!is_known(off)) { log_unmapped(p, off, 1); return; }
	if (timer_reg(reg, &i, &sub)) { timers_sync(p); p->cpu.stop = 1; }
	if (reg >= OFF_TRISA && reg < OFF_TRISA + PIC32MX_PORTS * 0x40 && (reg & 0x30u) == 0x10) reg += 0x10;
	old = SFR(p, reg);
	switch (off & 0xCu) {
	case 0x0: nv = v; break;
	case 0x4: nv = old & ~v; break;
	case 0x8: nv = old | v; break;
	default:  nv = old ^ v; break;
	}
	if (reg >= OFF_TRISA && reg < OFF_TRISA + PIC32MX_PORTS * 0x40) {
		SFR(p, reg) = nv;
		port_write(p, reg, old, nv);
		return;
	}
	if (reg == OFF_INTSTAT || reg == OFF_RCON + 0x0) { SFR(p, reg) = nv; return; }
	SFR(p, reg) = nv;
	if (timer_reg(reg, &i, &sub)) {
		if (sub == 0x10) {
			if (timer_slave(p, i)) p->timer[i - 1].tmr = (p->timer[i - 1].tmr & 0xFFFFu) | (nv << 16);
			else p->timer[i].tmr = timer_t32(p, i) ? (p->timer[i].tmr & 0xFFFF0000u) | (nv & 0xFFFFu) : nv & 0xFFFFu;
		}
		return;
	}
	if (uart_reg(reg, &i, &sub)) { uart_write(p, i, sub, old, nv); return; }
	if (i2c_reg(reg, &i, &sub)) { i2c_write(p, i, sub, nv); return; }
	if (reg >= OFF_INTCON && reg < OFF_INTCON + 0x1000) update_eic(p);
}

static uint8_t *mem_ptr(pic32mx *p, uint32_t pa, int size, int write)
{
	if (pa < PIC32MX_RAM_SIZE && PIC32MX_RAM_SIZE - pa >= (uint32_t)size) return p->ram + pa;
	if (write) return NULL;
	if (pa >= 0x1D000000u && pa - 0x1D000000u < PIC32MX_FLASH_SIZE) {
		uint32_t o = pa - 0x1D000000u;
		return o + (uint32_t)size <= p->flash_size ? (uint8_t *)p->flash + o : NULL;
	}
	if (pa >= 0x1FC00000u && pa - 0x1FC00000u + (uint32_t)size <= PIC32MX_BOOT_SIZE) return p->boot + (pa - 0x1FC00000u);
	return NULL;
}

static uint32_t bus_read(void *ctx, uint32_t pa, int size, int fetch, int *err)
{
	pic32mx *p = (pic32mx *)ctx;
	uint8_t *m;
	uint32_t v = 0;
	int i;

	(void)fetch;
	if (pa >= PIC32MX_SFR_BASE && pa - PIC32MX_SFR_BASE < PIC32MX_SFR_SIZE) {
		uint32_t off = pa - PIC32MX_SFR_BASE;
		return (sfr_read(p, off & ~3u) >> ((off & 3) * 8)) & (size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1);
	}
	m = mem_ptr(p, pa, size, 0);
	if (!m) {
		if (pa >= 0x1D000000u && pa - 0x1D000000u < PIC32MX_FLASH_SIZE) return 0xFFFFFFFFu;
		*err = 1;
		return 0;
	}
	for (i = 0; i < size; i++) v |= (uint32_t)m[i] << (8 * i);
	return v;
}

static void bus_write(void *ctx, uint32_t pa, uint32_t data, int size, int *err)
{
	pic32mx *p = (pic32mx *)ctx;
	uint8_t *m;
	int i;

	if (pa >= PIC32MX_SFR_BASE && pa - PIC32MX_SFR_BASE < PIC32MX_SFR_SIZE) {
		uint32_t off = pa - PIC32MX_SFR_BASE, sh = (off & 3) * 8;
		if (size < 4) {
			uint32_t mask = ((1u << (size * 8)) - 1) << sh;
			if (!(off & 0xCu)) data = (SFR(p, off & ~0xFu) & ~mask) | ((data << sh) & mask);
			else data = (data << sh) & mask;
		}
		sfr_write(p, off & ~3u, data);
		return;
	}
	m = mem_ptr(p, pa, size, 1);
	if (!m) { *err = 1; return; }
	for (i = 0; i < size; i++) m[i] = (uint8_t)(data >> (8 * i));
}

static int exc_hook(void *ctx, mips32_state *s, int code)
{
	pic32mx *p = (pic32mx *)ctx;
	if (code != MIPS32_EXC_INT) {
		p->exc_count++;
		if (p->board.exception) p->board.exception(p->board.ctx, code, s->cur_pc);
	}
	return MIPS32_HOOK_DELIVER;
}

static void irq_taken(void *ctx, int vector)
{
	pic32mx *p = (pic32mx *)ctx;
	if (vector >= 0 && vector < PIC32MX_VECTORS) p->vec_count[vector]++;
	SFR(p, OFF_INTSTAT) = ((uint32_t)p->cpu.eic_ripl << 8) | (uint32_t)vector;
}

void pic32mx_reset(pic32mx *p)
{
	static const uint32_t stub[4] = { 0x3C1A9D00u, 0x375A1000u, 0x03400008u, 0x00000000u };
	int i, j;

	memset(p->sfr, 0, sizeof(p->sfr));
	memset(p->timer, 0, sizeof(p->timer));
	memset(p->uart, 0, sizeof(p->uart));
	memset(p->i2c, 0, sizeof(p->i2c));
	memset(p->boot, 0xFF, sizeof(p->boot));
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++) p->boot[i * 4 + j] = (uint8_t)(stub[i] >> (8 * j));
	SFR(p, OFF_OSCCON) = 0x00053320u;
	SFR(p, OFF_RCON) = 0x00000003u;
	for (i = 0; i < PIC32MX_PORTS; i++) SFR(p, OFF_TRISA + (uint32_t)i * 0x40) = 0xFFFFu;
	for (i = 0; i < PIC32MX_UARTS; i++) SFR(p, uart_base[i] + 0x10) = 0x110u;
	mips32_reset(&p->cpu);
}

void pic32mx_init(pic32mx *p, const pic32mx_board *board, const uint8_t *flash, uint32_t flash_size)
{
	mips32_bus bus;

	memset(p, 0, sizeof(*p));
	if (board) p->board = *board;
	p->flash = flash;
	p->flash_size = flash_size > PIC32MX_FLASH_SIZE ? PIC32MX_FLASH_SIZE : flash_size;
	bus.ctx = p;
	bus.read = bus_read;
	bus.write = bus_write;
	bus.exc_hook = exc_hook;
	bus.irq_taken = irq_taken;
	mips32_init(&p->cpu, &bus, 2, 0x00018700u);
	pic32mx_reset(p);
}

int pic32mx_run(pic32mx *p, int cycles)
{
	uint64_t start = p->cpu.cycles, end = start + (uint64_t)(cycles > 0 ? cycles : 0);

	while (p->cpu.cycles < end) {
		uint64_t slice = end - p->cpu.cycles, ev = next_timer_event(p), e2 = next_i2c_event(p);
		if (e2 < ev) ev = e2;
		if (ev < slice) slice = ev ? ev : 1;
		mips32_run(&p->cpu, (int)slice);
		timers_sync(p);
		i2c_sync(p);
		if (mips32_timer_irq(&p->cpu)) pic32mx_set_irq(p, 0);
	}
	return (int)(p->cpu.cycles - start);
}

uint32_t pic32mx_sfr_peek(const pic32mx *p, uint32_t va)
{
	uint32_t off = (va & 0x1FFFFFFFu) - PIC32MX_SFR_BASE;
	return off < PIC32MX_SFR_SIZE ? p->sfr[off >> 2] : 0;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `tests/pinheck/pic32mx/check.sh`
Expected: `soc: ok` and `pic32mx: 0 failed`.

- [ ] **Step 6: Prove the tests catch the bring-up bugs**

Keep a copy in the git-ignored build directory: `cp src/cpu/pic32mx/pic32mx.c tests/pinheck/pic32mx/build/pic32mx.c.keep`. Then, for each mutation: apply it to `src/cpu/pic32mx/pic32mx.c`, run `tests/pinheck/pic32mx/check.sh`, confirm the named failure, and restore with `cp tests/pinheck/pic32mx/build/pic32mx.c.keep src/cpu/pic32mx/pic32mx.c`.

| Mutation (replace → with) | Must fail |
|---|---|
| `reg += 0x10;` → `reg += 0;` | `soc_test.c:60: rd(0xBF886020u) == 0x000A` |
| `q->done_at = p->cpu.cycles + (uint64_t)bits * 2 * (brg + 2) * pbdiv(p);` → `q->done_at = p->cpu.cycles + (uint64_t)bits * 0 * (brg + 2);` | `soc_test.c:184: !ifs(31)` |
| `(pri == best_pri && pri && sub > best_sub)` → `(pri == best_pri && pri && sub > best_sub && 0)` | `soc_test.c:118: soc.cpu.eic_ripl == 3 && …` |
| `return (i == 2 \|\| i == 4) && timer_t32(p, i - 1);` → `return (i == 2 \|\| i == 4) && timer_t32(p, i - 1) && 0;` | `soc_test.c:102: !ifs(12)` |
| `if (p->logged[off >> 4]) return;` → `if (p->logged[off >> 4] && 0) return;` | `soc_test.c:198: rec.unmapped == 1` |

Each mutation keeps every variable in use, so it compiles under the suite's `-Werror` and fails at its test rather than at the compiler.

- [ ] **Step 7: Commit**

```bash
git add src/cpu/pic32mx/pic32mx.h src/cpu/pic32mx/pic32mx.c tests/pinheck/pic32mx
git commit -m "pic32mx: PIC32MX795F512L SoC model with unit tests"
```

### Task 2: Headless firmware boot to the banner (machine check 1)

**Files:**
- Create: `tests/pinheck/pic32mx/linkstub.h`, `linkstub.c`, `boot.c`, `banner.py`

**Interfaces:**
- Consumes: the Task 1 SoC; **Plan 3's** `cat24m01_init`/`cat24m01_update` from `src/wpc/pinheck/eeprom.h` (see Prerequisites).
- Produces: `tests/pinheck/pic32mx/build/pic32boot [-v] [-c cycles] [-boots n] DOM_V006.PRG`. It writes UART1 to stdout, each boot introduced by `=== BOOT n ===`. It keeps both EEPROM images across boots, and exits 1 if the CPU took any exception. It also produces `banner.py OUTPUT`.

What the firmware does before the banner, all observed on `DOM_V006.PRG`:
1. **Sync.** It sends command `0x23` with `0xAA` in byte 0, waits 500 ms and exchanges again. It expects bytes 0–13 = `'0'..'='` and bytes 14–15 = `0xAA` in the reply, and retries forever, printing a dot each time.
2. **Name.** It reads and writes Propeller EEPROM words at addresses `0x80`–`0x83` to store the game name.
   - `writeEEPROM` = command `0x20`, word address in bytes 0–1, value in bytes 4–7. It polls with `0xFF` until the reply's byte 15 is `0x42`, sends `0x27`, then verifies with `readEEPROM`.
   - `readEEPROM` = command `0x21`, sequence number in byte 14. The reply has the value in bytes 0–3, `seq|0x80` in byte 14 and `0xAD` in byte 15.
3. **Settings.** It reads and writes U13 (CAT24M01 at `0x50` on I²C1). The DS1340 at `0x68` is probed and NACKed; the firmware tolerates that.
4. **Banner.** It prints the banner only if the settings byte loaded from U13 is not `0xFF`, so on a blank EEPROM the banner comes on the second boot.

- [ ] **Step 1: Confirm Plan 3's EEPROM model is present**

Run: `test -f src/wpc/pinheck/eeprom.c && grep -c "cat24m01_update" src/wpc/pinheck/eeprom.h`
Expected: `1`. If the file is missing, stop: this task waits for Plan 3.

- [ ] **Step 2: Write the banner assertion**

The firmware prints the game name in four `readEEPROM` fragments with its own trace lines between them. `banner.py` removes that trace as a unit before matching.

`tests/pinheck/pic32mx/banner.py`:

```python
#!/usr/bin/env python3
import re
import sys

TRACE = re.compile(r'READ EEPROM\r?\n(Checksum Number: \d+\r?\n)?(Timeout Counter: \d+\r?\n)*(Read OK!\r?\n)?')


def boots(text):
    parts = re.split(r'=== BOOT (\d+) ===\n', text)
    return {int(parts[i]): TRACE.sub('', parts[i + 1]).replace('\r', '') for i in range(1, len(parts), 2)}


def main():
    b = boots(open(sys.argv[1], encoding='latin-1').read())
    want = [
        (1, 'PROPELLER SYNC CHECKOK'),
        (1, 'NAMING GAME'),
        (2, 'NAME ALREADY EXISTS'),
        (2, 'pinHeck System 2011-2016'),
        (2, 'Game: DOM - DOMINOS'),
        (2, 'Version: 006'),
    ]
    bad = [(n, s) for n, s in want if s not in b.get(n, '')]
    for n, s in bad:
        print('BANNER FAIL boot %d lacks %r' % (n, s))
    if 'pinHeck System' in b.get(1, ''):
        print('BANNER FAIL boot 1 printed the banner on a blank EEPROM')
        bad.append((1, 'banner'))
    print('banner: %s' % ('FAIL' if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
```

Make it executable: `chmod +x tests/pinheck/pic32mx/banner.py`

- [ ] **Step 3: Write the link stub and the boot runner**

`tests/pinheck/pic32mx/linkstub.h`:

```c
#ifndef PINHECK_LINKSTUB_H
#define PINHECK_LINKSTUB_H

#include <stdint.h>

#define LINKSTUB_WORDS 0x4000

typedef struct linkstub {
	int clk, bit, rf13, packets;
	uint8_t rx[16], reply[16];
	uint32_t words[LINKSTUB_WORDS];
} linkstub;

void linkstub_init(linkstub *l);
void linkstub_reset_link(linkstub *l);
void linkstub_portf_write(linkstub *l, uint32_t latf);
uint32_t linkstub_portf_read(const linkstub *l);

#endif
```

`tests/pinheck/pic32mx/linkstub.c`:

```c
#include "linkstub.h"
#include <string.h>

#define RF5  (1u << 5)
#define RF12 (1u << 12)
#define RF13 (1u << 13)

void linkstub_reset_link(linkstub *l)
{
	l->clk = 0;
	l->bit = 0;
	l->rf13 = 1;
	memset(l->rx, 0, sizeof(l->rx));
	memset(l->reply, 0, sizeof(l->reply));
}

void linkstub_init(linkstub *l)
{
	memset(l, 0, sizeof(*l));
	memset(l->words, 0xFF, sizeof(l->words));
	linkstub_reset_link(l);
}

static void packet(linkstub *l)
{
	uint8_t *rx = l->rx;
	uint32_t a = (uint32_t)(rx[0] | rx[1] << 8) % LINKSTUB_WORDS, w;
	int i;

	l->packets++;
	if (rx[15] != 0xFF) memset(l->reply, 0, sizeof(l->reply));
	if (rx[15] == 0x23 && rx[0] == 0xAA) {
		for (i = 0; i < 14; i++) l->reply[i] = (uint8_t)(0x30 + i);
		l->reply[14] = l->reply[15] = 0xAA;
	} else if (rx[15] == 0x21) {
		w = l->words[a];
		for (i = 0; i < 4; i++) l->reply[i] = (uint8_t)(w >> (8 * i));
		l->reply[14] = (uint8_t)(rx[14] | 0x80);
		l->reply[15] = 0xAD;
	} else if (rx[15] == 0x20) {
		l->words[a] = (uint32_t)rx[4] | (uint32_t)rx[5] << 8 | (uint32_t)rx[6] << 16 | (uint32_t)rx[7] << 24;
		l->reply[15] = 0x42;
	}
	memset(l->rx, 0, sizeof(l->rx));
}

void linkstub_portf_write(linkstub *l, uint32_t latf)
{
	int clk = (latf & RF12) != 0;
	if (l->clk && !clk) {
		int k = l->bit++;
		if (latf & RF5) l->rx[k >> 3] |= (uint8_t)(1u << (k & 7));
		l->rf13 = (l->reply[k >> 3] >> (k & 7)) & 1;
		if (l->bit == 128) { l->bit = 0; packet(l); }
	}
	l->clk = clk;
}

uint32_t linkstub_portf_read(const linkstub *l)
{
	return (0xFFFFu & ~RF13) | (l->rf13 ? RF13 : 0);
}
```

`tests/pinheck/pic32mx/boot.c`:

```c
#include "pic32mx.h"
#include "linkstub.h"
#include "../../../src/wpc/pinheck/eeprom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pic32mx soc;
static linkstub link;
static cat24m01 u13;
static uint8_t u13mem[131072];
static uint8_t flash[PIC32MX_FLASH_SIZE];
static int verbose;

static void port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	(void)ctx; (void)tris; (void)cycle;
	if (port == PIC32MX_PORTF) linkstub_portf_write(&link, lat);
}

static uint32_t port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	return port == PIC32MX_PORTF ? linkstub_portf_read(&link) : 0xFFFFu;
}

static void uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (uart == 1) fputc(byte, stdout);
}

static int i2c_pins(void *ctx, int module, int scl, int sda, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	return module == 1 ? cat24m01_update(&u13, scl, sda) : sda;
}

static void unmapped(void *ctx, uint32_t pa, int write)
{
	(void)ctx;
	if (verbose) fprintf(stderr, "unmodelled SFR %s %08x\n", write ? "write" : "read", (unsigned)(pa | 0xA0000000u));
}

static void exception(void *ctx, int code, uint32_t pc)
{
	(void)ctx;
	fprintf(stderr, "exception %d at %08x\n", code, (unsigned)pc);
}

int main(int argc, char **argv)
{
	pic32mx_board board = { NULL, port_write, port_read, uart_tx, i2c_pins, unmapped, exception };
	unsigned long long cycles = 800000000ull;
	int boots = 1, b, i;
	const char *path = NULL;
	FILE *f;
	size_t n;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) verbose = 1;
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) cycles = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "-boots") && i + 1 < argc) boots = atoi(argv[++i]);
		else path = argv[i];
	}
	if (!path) { fprintf(stderr, "usage: pic32boot [-v] [-c cycles] [-boots n] DOM_V006.PRG\n"); return 2; }
	if (!(f = fopen(path, "rb"))) { perror(path); return 2; }
	n = fread(flash, 1, sizeof(flash), f);
	fclose(f);
	memset(u13mem, 0xFF, sizeof(u13mem));
	linkstub_init(&link);
	pic32mx_init(&soc, &board, flash, (uint32_t)n);
	for (b = 1; b <= boots; b++) {
		unsigned long long done = 0;
		printf("=== BOOT %d ===\n", b);
		if (b > 1) pic32mx_reset(&soc);
		linkstub_reset_link(&link);
		cat24m01_init(&u13, u13mem, 0);
		while (done < cycles) done += (unsigned long long)pic32mx_run(&soc, 1000000);
		printf("\n");
	}
	fflush(stdout);
	if (verbose) {
		fprintf(stderr, "pc=%08x exceptions=%llu link packets=%d\n", (unsigned)soc.cpu.pc, (unsigned long long)soc.exc_count, link.packets);
		for (i = 0; i < PIC32MX_VECTORS; i++)
			if (soc.vec_count[i]) fprintf(stderr, "vector %d: %llu\n", i, (unsigned long long)soc.vec_count[i]);
	}
	return soc.exc_count ? 1 : 0;
}
```

- [ ] **Step 4: Watch the check fail on a single boot**

Run:
```sh
cd tests/pinheck/pic32mx && mkdir -p build && \
cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/pic32mx -o build/pic32boot boot.c linkstub.c ../../../src/wpc/pinheck/eeprom.c ../../../src/cpu/pic32mx/pic32mx.c ../../../src/cpu/mips32/mips32.c && \
./build/pic32boot -boots 1 -c 800000000 "$PINHECK_UPDATE_DIR/DOM_V006.PRG" > build/one.txt; python3 banner.py build/one.txt; cd -
```
Expected:
```text
BANNER FAIL boot 2 lacks 'NAME ALREADY EXISTS'
BANNER FAIL boot 2 lacks 'pinHeck System 2011-2016'
BANNER FAIL boot 2 lacks 'Game: DOM - DOMINOS'
BANNER FAIL boot 2 lacks 'Version: 006'
banner: FAIL
```

- [ ] **Step 5: Run the full check**

Run: `PINHECK_UPDATE_DIR=/path/to/update tests/pinheck/pic32mx/check.sh`
Expected: `soc: ok`, `banner: ok`, `pic32mx: 0 failed`. It takes about 15 s: two boots of 800M cycles at about 108M cycles/s.

For inspection, `./build/pic32boot -v -boots 2 -c 800000000 …` reports `exceptions=0` and interrupt counts on vectors 0 (core timer), 8 (Timer2), 12 (Timer3) and 25 (I²C1).

- [ ] **Step 6: Commit**

```bash
git add tests/pinheck/pic32mx/linkstub.h tests/pinheck/pic32mx/linkstub.c tests/pinheck/pic32mx/boot.c tests/pinheck/pic32mx/banner.py
git commit -m "pic32mx: headless firmware boot to the UART1 banner (machine check 1)"
```

### Task 3: PinMAME integration: CPU `PIC32MX`, driver `PINHECK`, game `dominos`

**Files:**
- Create: `src/cpu/pic32mx/pic32mxcpu.h`, `pic32mxcpu.c`, `src/wpc/pinheck.h`, `pinheck.c`, `pinheckgames.c`, `tests/pinheck/pic32mx/pinmame_check.sh`
- Modify (by the Step 4 script): `src/cpuintrf.h`, `src/cpuintrf.c`, `src/pinmame.h`, `src/pinmame.mak`, `src/rules.mak`, `src/wpc/gen.h`, `src/wpc/driver.c`. Also `cmake/{libpinmame,pinmame,pinmame32,sdl3pinmame,vpinmame,xpinmame}/CMakeLists*.txt` (the 10 that define `HAS_AT91=1`), and `vcproj/{PinMAME_VC2012,PinMAME32_VC2012,VPinMAME_VC2012,LibPinMAME_VC2015}.vcxproj` with their `.filters`.

**Interfaces:**
- Consumes: the Task 1 SoC; `mips32_dasm`/`mips32_translate` from Milestone 1.
- Produces:
  - **CPU and game:** `CPU_PIC32MX`, the `pic32cpu_*` cpuintrf functions, `GEN_PINHECK` (bit 52), `MACHINE_DRIVER_EXTERN(PINHECK)` / `gl_mPINHECK`, `PINMAME_VIDEO_UPDATE(pinheck_video)`, and game `dominos` (`GAME_NOT_WORKING`).
  - **For later milestones:** `void pic32cpu_set_board(const pic32mx_board *)` and `pic32mx *pic32cpu_soc(void)`.
  - **Test hook:** env `PINHECK_UART1_LOG=<file>` receives UART1.

Adapter rules, following `src/p2k/p2k_cpuintrf.cpp`:
- `get_context` returns a non-zero size, because `cpuintrf_init_cpu` rejects zero. There is one SoC instance, so `set_context` is a no-op.
- `execute` returns `cycles - pic32cpu_ICount`, because `cpuexec.c` derives the cycles run from that value.
- PinMAME's memory map is a 4-byte NOP range: the SoC owns its bus.

- [ ] **Step 1: Write the failing check**

`tests/pinheck/pic32mx/pinmame_check.sh`:

```sh
#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms || exit 2
(cd "$PINHECK_UPDATE_DIR" && zip -q -j "$OLDPWD/$B/roms/dominos.zip" DOM_V006.PRG PRP_V008.BIN) || exit 2
(cd $B && PINHECK_UART1_LOG=$PWD/uart1.log "$SDL3PINMAME" dominos -rompath roms -headless -frames_to_run 180 -skip_gamewarnings -nothrottle > run.out 2>&1) || { echo "PINMAME FAIL: sdl3pinmame exited $?"; tail -5 $B/run.out; exit 1; }
if grep -q "PROPELLER SYNC CHECK\.\.\." $B/uart1.log; then echo "pinmame: ok"; exit 0; fi
echo "PINMAME FAIL: UART1 log lacks 'PROPELLER SYNC CHECK...':"
cat -v $B/uart1.log
exit 1
```

Make it executable: `chmod +x tests/pinheck/pic32mx/pinmame_check.sh`

- [ ] **Step 2: Run it against a build without the driver**

Run (any current `sdl3pinmame` build): `PINHECK_UPDATE_DIR=/path/to/update SDL3PINMAME=build-dbg/sdl3pinmame tests/pinheck/pic32mx/pinmame_check.sh`
Expected: exit 1, with
```text
PINMAME FAIL: sdl3pinmame exited 1
fuzzy name compare, running domino2
ERROR: required files are missing, the game cannot be run.
```
(Without the driver, PinMAME fuzzy-matches `dominos` to the unrelated bingo set `domino2`.)

- [ ] **Step 3: Write the adapter, the driver and the game**

`src/cpu/pic32mx/pic32mxcpu.h`:

```c
#ifndef PIC32MXCPU_H
#define PIC32MXCPU_H

#include "pic32mx.h"

enum {
	PIC32CPU_PC = 1, PIC32CPU_R0, PIC32CPU_R31 = PIC32CPU_R0 + 31,
	PIC32CPU_HI, PIC32CPU_LO, PIC32CPU_STATUS, PIC32CPU_CAUSE, PIC32CPU_EPC
};

extern int pic32cpu_ICount;

void pic32cpu_init(void);
void pic32cpu_reset(void *param);
void pic32cpu_exit(void);
int pic32cpu_execute(int cycles);
unsigned pic32cpu_get_context(void *dst);
void pic32cpu_set_context(void *src);
unsigned pic32cpu_get_reg(int regnum);
void pic32cpu_set_reg(int regnum, unsigned val);
void pic32cpu_set_irq_line(int irqline, int state);
void pic32cpu_set_irq_callback(int (*callback)(int irqline));
const char *pic32cpu_info(void *context, int regnum);
unsigned pic32cpu_dasm(char *buffer, unsigned pc);

void pic32cpu_set_board(const pic32mx_board *board);
pic32mx *pic32cpu_soc(void);

#endif
```

`src/cpu/pic32mx/pic32mxcpu.c`:

```c
#include "driver.h"
#include "cpuintrf.h"
#include "pic32mxcpu.h"
#include <stdio.h>
#include <string.h>

int pic32cpu_ICount;

static pic32mx soc;
static pic32mx_board board;
static int have_board;

static void load_flash(void)
{
	pic32mx_init(&soc, have_board ? &board : NULL, memory_region(REGION_CPU1), memory_region_length(REGION_CPU1));
}

void pic32cpu_set_board(const pic32mx_board *b)
{
	board = *b;
	have_board = 1;
	soc.board = board;
}

pic32mx *pic32cpu_soc(void) { return &soc; }

void pic32cpu_init(void) { load_flash(); }
void pic32cpu_reset(void *param) { (void)param; load_flash(); }
void pic32cpu_exit(void) { have_board = 0; }

int pic32cpu_execute(int cycles)
{
	pic32cpu_ICount = cycles;
	while (pic32cpu_ICount > 0) pic32cpu_ICount -= pic32mx_run(&soc, pic32cpu_ICount);
	return cycles - pic32cpu_ICount;
}

unsigned pic32cpu_get_context(void *dst) { (void)dst; return sizeof(void *); }
void pic32cpu_set_context(void *src) { (void)src; }

unsigned pic32cpu_get_reg(int regnum)
{
	switch (regnum) {
	case REG_PC: case PIC32CPU_PC: return soc.cpu.pc;
	case REG_PREVIOUSPC: return soc.cpu.cur_pc;
	case REG_SP: return mips32_regs(&soc.cpu)[29];
	case PIC32CPU_HI: return soc.cpu.hi;
	case PIC32CPU_LO: return soc.cpu.lo;
	case PIC32CPU_STATUS: return soc.cpu.status;
	case PIC32CPU_CAUSE: return soc.cpu.cause;
	case PIC32CPU_EPC: return soc.cpu.epc;
	}
	if (regnum >= PIC32CPU_R0 && regnum <= PIC32CPU_R31) return mips32_regs(&soc.cpu)[regnum - PIC32CPU_R0];
	return 0;
}

void pic32cpu_set_reg(int regnum, unsigned val)
{
	switch (regnum) {
	case REG_PC: case PIC32CPU_PC: soc.cpu.pc = val; soc.cpu.npc = val + 4; soc.cpu.delay = 0; return;
	case REG_SP: mips32_regs(&soc.cpu)[29] = val; return;
	case PIC32CPU_HI: soc.cpu.hi = val; return;
	case PIC32CPU_LO: soc.cpu.lo = val; return;
	}
	if (regnum > PIC32CPU_R0 && regnum <= PIC32CPU_R31) mips32_regs(&soc.cpu)[regnum - PIC32CPU_R0] = val;
}

void pic32cpu_set_irq_line(int irqline, int state) { (void)irqline; (void)state; }
void pic32cpu_set_irq_callback(int (*callback)(int irqline)) { (void)callback; }

const char *pic32cpu_info(void *context, int regnum)
{
	static char buf[32];
	(void)context;
	switch (regnum) {
	case CPU_INFO_NAME: return "PIC32MX";
	case CPU_INFO_FAMILY: return "MIPS32 M4K";
	case CPU_INFO_VERSION: return "1.0";
	case CPU_INFO_FILE: return __FILE__;
	case CPU_INFO_CREDITS: return "PinMAME pinHeck";
	case CPU_INFO_REG + PIC32CPU_PC: sprintf(buf, "PC:%08X", soc.cpu.pc); return buf;
	}
	if (regnum >= CPU_INFO_REG + PIC32CPU_R0 && regnum <= CPU_INFO_REG + PIC32CPU_R31) {
		int r = regnum - CPU_INFO_REG - PIC32CPU_R0;
		sprintf(buf, "R%d:%08X", r, mips32_regs(&soc.cpu)[r]);
		return buf;
	}
	return "";
}

unsigned pic32cpu_dasm(char *buffer, unsigned pc)
{
	uint32_t pa;
	int err = 0;
	uint32_t op;
	if (!mips32_translate(&soc.cpu, pc, &pa)) { sprintf(buffer, "???"); return 4; }
	op = soc.cpu.bus.read(soc.cpu.bus.ctx, pa, 4, 1, &err);
	if (err) { sprintf(buffer, "???"); return 4; }
	return mips32_dasm(buffer, pc, op);
}
```

`src/wpc/pinheck.h`:

```c
#ifndef INC_PINHECK
#define INC_PINHECK

#include "core.h"
#include "sim.h"

#define PINHECK_CPUREGION  REGION_CPU1
#define PINHECK_PROPREGION REGION_USER1

#define PINHECK_INPUT_PORTS_START(name, balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls)

#define PINHECK_INPUT_PORTS_END INPUT_PORTS_END

#define PINHECK_ROMSTART(name, prg, prgsize, prghash, prp, prphash) \
  ROM_START(name) \
    ROM_REGION(0x80000, PINHECK_CPUREGION, ROMREGION_ERASEFF) \
      ROM_LOAD(prg, 0x0000, prgsize, prghash) \
    ROM_REGION(0x8000, PINHECK_PROPREGION, 0) \
      ROM_LOAD(prp, 0x0000, 0x8000, prphash)

#define PINHECK_ROMEND ROM_END

extern PINMAME_VIDEO_UPDATE(pinheck_video);
extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK

#endif
```

`src/wpc/pinheck.c`:

```c
#include "driver.h"
#include "core.h"
#include "cpu/pic32mx/pic32mxcpu.h"
#include "pinheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PINHECK_CLOCK 80000000
#define PINHECK_LOG_MAX 64

static struct {
	FILE *uart1;
	uint32_t logged[PINHECK_LOG_MAX];
	int nlogged;
} locals;

static void pinheck_uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (uart == 1 && locals.uart1) fputc(byte, locals.uart1);
}

static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx; (void)port; (void)cycle;
	return 0xFFFFu;
}

static void pinheck_unmapped(void *ctx, uint32_t pa, int write)
{
	(void)ctx;
	logerror("pinheck: unmodelled SFR %s %08x\n", write ? "write" : "read", (unsigned)(pa | 0xA0000000u));
}

static void pinheck_exception(void *ctx, int code, uint32_t pc)
{
	uint32_t key = pc ^ ((uint32_t)code << 27);
	int i;
	(void)ctx;
	for (i = 0; i < locals.nlogged; i++)
		if (locals.logged[i] == key) return;
	if (locals.nlogged < PINHECK_LOG_MAX) locals.logged[locals.nlogged++] = key;
	logerror("pinheck: PIC32 exception %d at %08x\n", code, (unsigned)pc);
}

PINMAME_VIDEO_UPDATE(pinheck_video)
{
	(void)layout;
	fillbitmap(bitmap, 0, cliprect);
}

static INTERRUPT_GEN(pinheck_vblank)
{
	core_updateSw(0);
}

static MACHINE_INIT(pinheck)
{
	pic32mx_board board = { NULL, NULL, pinheck_port_read, pinheck_uart_tx, NULL, pinheck_unmapped, pinheck_exception };
	const char *log = getenv("PINHECK_UART1_LOG");

	if (locals.uart1) fclose(locals.uart1);
	memset(&locals, 0, sizeof(locals));
	if (log && (locals.uart1 = fopen(log, "wb")) != NULL) setvbuf(locals.uart1, NULL, _IONBF, 0);
	pic32cpu_set_board(&board);
}

static MACHINE_STOP(pinheck)
{
	if (locals.uart1) fclose(locals.uart1);
	locals.uart1 = NULL;
}

static MEMORY_READ32_START(pinheck_readmem)
	{ 0x00000000, 0x00000003, MRA32_NOP },
MEMORY_END

static MEMORY_WRITE32_START(pinheck_writemem)
	{ 0x00000000, 0x00000003, MWA32_NOP },
MEMORY_END

MACHINE_DRIVER_START(PINHECK)
	MDRV_IMPORT_FROM(PinMAME)
	MDRV_CORE_INIT_RESET_STOP(pinheck, NULL, pinheck)
	MDRV_CPU_ADD_TAG("mcpu", PIC32MX, PINHECK_CLOCK)
	MDRV_CPU_MEMORY(pinheck_readmem, pinheck_writemem)
	MDRV_CPU_VBLANK_INT(pinheck_vblank, 1)
	MDRV_SCREEN_SIZE(128, 32)
	MDRV_VISIBLE_AREA(0, 127, 0, 31)
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_RGB_DIRECT)
MACHINE_DRIVER_END
```

`src/wpc/pinheckgames.c`:

```c
#include "driver.h"
#include "sim.h"
#include "sndbrd.h"
#include "pinheck.h"

static core_tLCDLayout pinheck_disp[] = {
  {0, 0, 32, 128, CORE_VIDEO, (genf *)pinheck_video, NULL}, {0}
};

#define INIT_PINHECK(name, balls) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
static core_tGameData name##GameData = { GEN_PINHECK, pinheck_disp, {0, 0, 0, 0, SNDBRD_NONE, 0, 0} }; \
static void init_##name(void) { core_gameData = &name##GameData; }

/*-------------------------------------------------------------------
/ Domino's Spectacular Pinball Adventure (2016)
/-------------------------------------------------------------------*/
INIT_PINHECK(dominos, 3)
PINHECK_ROMSTART(dominos, "DOM_V006.PRG", 0x31990, CRC(750e27a4) SHA1(3fbebce7f885563e61dd2e4f5d7f0a8a54c53b23),
                 "PRP_V008.BIN", CRC(a51ee28d) SHA1(0556d88b6f0c7cb15e648e1f76f4d47890ddb858))
PINHECK_ROMEND
CORE_GAMEDEFNV(dominos, "Domino's Spectacular Pinball Adventure", 2016, "Spooky Pinball", gl_mPINHECK, GAME_NOT_WORKING)
```

- [ ] **Step 4: Register the CPU, driver and sources in every build**

Run from the repository root. The script asserts that every anchor occurs exactly once. It preserves each file's line endings (several files mix CRLF and LF), and it is idempotent: a second run changes nothing.

```sh
python3 - <<'PYEOF'
#!/usr/bin/env python3
import glob
import sys

CPU_SRC = ['src/cpu/mips32/mips32.c', 'src/cpu/mips32/mips32.h', 'src/cpu/mips32/mips32dasm.c',
           'src/cpu/pic32mx/pic32mx.c', 'src/cpu/pic32mx/pic32mx.h',
           'src/cpu/pic32mx/pic32mxcpu.c', 'src/cpu/pic32mx/pic32mxcpu.h']
DRV_SRC = ['src/wpc/pinheck.c', 'src/wpc/pinheck.h', 'src/wpc/pinheckgames.c']
changed = []


def edit(path, anchor, insert, after=True):
    s = open(path, newline='').read()
    if insert.strip() in s:
        return
    if s.count(anchor) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(anchor), anchor))
    s = s.replace(anchor, anchor + insert if after else insert + anchor, 1)
    open(path, 'w', newline='').write(s)
    changed.append(path)


def line_span(s, i):
    start = s.rfind('\n', 0, i) + 1
    end = s.find('\n', i)
    end = len(s) if end < 0 else end + 1
    nl = '\r\n' if s[start:end].endswith('\r\n') else '\n'
    return start, end, nl


def line_after(path, needle, lines):
    s = open(path, newline='').read()
    if s.count(needle) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(needle), needle))
    i = s.find(needle)
    start, end, nl = line_span(s, i)
    indent = s[start:i]
    block = ''.join(indent + l + nl for l in lines)
    if block in s:
        return
    s = s[:end] + block + s[end:]
    open(path, 'w', newline='').write(s)
    changed.append(path)


edit('src/cpuintrf.h', '#if defined(PINMAME) && (HAS_MEDIAGX)\n\tCPU_MEDIAGX,\n#endif\n',
     '#if defined(PINMAME) && (HAS_PIC32MX)\n\tCPU_PIC32MX,\n#endif\n')
edit('src/cpuintrf.c', '#include "p2k/p2k_cpuintrf.h"\n#endif\n',
     '#if defined(PINMAME) && (HAS_PIC32MX)\n#include "cpu/pic32mx/pic32mxcpu.h"\n#endif\n')
edit('src/cpuintrf.c', '\tCPU0(MEDIAGX,  mediagx,\t 2,  0,1.00,32,32ledw, 0,32,LE,1,15\t),\n#endif\n',
     '#if defined(PINMAME) && (HAS_PIC32MX)\n\tCPU0(PIC32MX,  pic32cpu,\t 1,  0,1.00,32,32ledw, 0,32,LE,4, 4\t),\n#endif\n')
edit('src/pinmame.h', '#define HAS_AT91     1\n', '#define HAS_PIC32MX  1\n')
edit('src/wpc/gen.h', '#define GEN_P2K         U64(0x8000000000000) /* Midway Pinball 2000 */\n',
     '#define GEN_PINHECK     U64(0x10000000000000) /* Spooky pinHeck */\n')
edit('src/wpc/driver.c', '\n#endif /* DRIVER_RECURSIVE */',
     '\n\n// ------------------\n// SPOOKY PINBALL GAMES\n// ------------------\n'
     'DRIVERNV(dominos)       //pinHeck 07/16 Domino\'s Spectacular Pinball Adventure\n', after=False)
edit('src/pinmame.mak', 'CPUS += AT91@\n', 'CPUS += PIC32MX@\n')
edit('src/pinmame.mak', 'DRVLIBS += $(PINOBJ)/recel.o $(PINOBJ)/recelsnd.o\n', 'DRVLIBS += $(PINOBJ)/pinheck.o\n')
edit('src/pinmame.mak', 'PINGAMES += $(PINOBJ)/recelgames.o\n', 'PINGAMES += $(PINOBJ)/pinheckgames.o\n')
edit('src/rules.mak', 'CPUDEFS += -DHAS_AT91=0\nendif\n',
     '\nCPU=$(strip $(findstring PIC32MX@,$(CPUS)))\nifneq ($(CPU),)\n'
     'OBJDIRS += $(OBJ)/cpu/mips32 $(OBJ)/cpu/pic32mx\nCPUDEFS += -DHAS_PIC32MX=1\n'
     'CPUOBJS += $(OBJ)/cpu/mips32/mips32.o $(OBJ)/cpu/pic32mx/pic32mx.o $(OBJ)/cpu/pic32mx/pic32mxcpu.o\n'
     'DBGOBJS += $(OBJ)/cpu/mips32/mips32dasm.o\n'
     '$(OBJ)/cpu/mips32/mips32.o: src/cpu/mips32/mips32.c src/cpu/mips32/mips32.h\n'
     '$(OBJ)/cpu/pic32mx/pic32mx.o: src/cpu/pic32mx/pic32mx.c src/cpu/pic32mx/pic32mx.h src/cpu/mips32/mips32.h\n'
     '$(OBJ)/cpu/pic32mx/pic32mxcpu.o: src/cpu/pic32mx/pic32mxcpu.c src/cpu/pic32mx/pic32mxcpu.h src/cpu/pic32mx/pic32mx.h\n'
     'else\nCPUDEFS += -DHAS_PIC32MX=0\nendif\n')

for path in sorted(glob.glob('cmake/*/CMakeLists*.txt')):
    if 'HAS_AT91=1' not in open(path).read():
        continue
    line_after(path, 'HAS_AT91=1', ['HAS_PIC32MX=1'])
    line_after(path, 'src/cpu/at91/at91dasm.c', CPU_SRC)
    line_after(path, 'src/wpc/recelsnd.c', DRV_SRC)

for path in sorted(glob.glob('vcproj/*.vcxproj')):
    s = open(path).read()
    if 'at91dasm.c' not in s:
        continue
    win = lambda p: '..\\' + p.replace('/', '\\')
    line_after(path, '<ClCompile Include="..\\src\\cpu\\at91\\at91dasm.c" />',
               ['<ClCompile Include="%s" />' % win(p) for p in CPU_SRC if p.endswith('.c')])
    line_after(path, '<ClInclude Include="..\\src\\cpu\\at91\\at91.h" />',
               ['<ClInclude Include="%s" />' % win(p) for p in CPU_SRC + DRV_SRC if p.endswith('.h')])
    line_after(path, '<ClCompile Include="..\\src\\wpc\\recelgames.c" />',
               ['<ClCompile Include="%s" />' % win(p) for p in DRV_SRC if p.endswith('.c')])

for path in sorted(glob.glob('vcproj/*.vcxproj.filters')):
    s = open(path, newline='').read()
    if 'at91dasm.c' not in s:
        continue
    anchor = '<ClCompile Include="..\\src\\wpc\\recelgames.c">'
    if s.count(anchor) != 1:
        sys.exit('%s: anchor found %d times: %r' % (path, s.count(anchor), anchor))
    start, end, nl = line_span(s, s.find(anchor))
    win = lambda p: '..\\' + p.replace('/', '\\')
    items = ''.join('    <ClCompile Include="%s">%s      <Filter>Source Files\\PinMAME</Filter>%s    </ClCompile>%s'
                    % (win(p), nl, nl, nl) for p in CPU_SRC + DRV_SRC if p.endswith('.c'))
    if items in s:
        continue
    s = s[:start] + items + s[start:]
    open(path, 'w', newline='').write(s)
    changed.append(path)

for p in sorted(set(changed)):
    print('updated', p)
PYEOF
```

Expected: 25 lines `updated <path>`. Run it again; expected: no output.

Then check the VS projects:
```sh
python3 - <<'PYEOF'
import glob, subprocess, xml.dom.minidom as m
def err(text):
    try:
        m.parseString(text)
        return None
    except Exception as e:
        return str(e)
for f in sorted(glob.glob('vcproj/*VC201*.vcxproj*')):
    old = subprocess.run(['git', 'show', 'HEAD:' + f], capture_output=True, text=True).stdout
    new = open(f).read()
    print('%-45s %s' % (f, 'ok' if err(new) == err(old) else 'CHANGED: ' + str(err(new))))
PYEOF
```
Expected: every line `ok`. The four `.filters` files already fail to parse on `HEAD`, because of an orphaned `<UniqueIdentifier>` near line 122–126. That is an upstream defect this plan does not touch; `ok` means the edit left the first parse error exactly where it was.

- [ ] **Step 5: Build `sdl3pinmame`**

Run:
```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -DPLATFORM=linux -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -B build-dbg
cmake --build build-dbg -j"$(nproc)" 2>&1 | tee build-dbg/pinheck-build.log | tail -1
grep -cE '(pic32mx|pic32mxcpu|mips32|pinheck|pinheckgames)\.(c|h):[0-9]+:[0-9]+: (warning|error)' build-dbg/pinheck-build.log
```
Expected: `[100%] Built target sdl3pinmame`, then `0`.

- [ ] **Step 6: Run the check**

Run: `PINHECK_UPDATE_DIR=/path/to/update SDL3PINMAME=build-dbg/sdl3pinmame tests/pinheck/pic32mx/pinmame_check.sh`
Expected: `pinmame: ok`. The UART1 log reads `PROPELLER SYNC CHECK....`: the real firmware is running inside PinMAME and waiting for a Propeller, which Milestone 5 adds. The run is 180 frames, about 2.6 s of wall time for 3 emulated seconds.

- [ ] **Step 7: Build libpinmame too**

Run:
```sh
cp cmake/libpinmame/CMakeLists.txt CMakeLists.txt
cmake -DPLATFORM=linux -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -B build-libpinmame
cmake --build build-libpinmame -j"$(nproc)" > build-libpinmame/build.log 2>&1; echo "exit=$?"
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
```
Expected: `exit=0`. (With a parallel build, the order of the final `Built target` lines varies.) This proves a second, independent cmake list. The Windows lists and VS projects follow the same edit pattern but cannot be built on Linux.

- [ ] **Step 8: Commit**

```bash
git add src/cpu/pic32mx/pic32mxcpu.h src/cpu/pic32mx/pic32mxcpu.c src/wpc/pinheck.h src/wpc/pinheck.c src/wpc/pinheckgames.c \
        tests/pinheck/pic32mx/pinmame_check.sh src/cpuintrf.h src/cpuintrf.c src/pinmame.h src/pinmame.mak src/rules.mak \
        src/wpc/gen.h src/wpc/driver.c cmake vcproj
git commit -m "pinheck: PIC32MX CPU, PINHECK driver and dominos romset in PinMAME"
```
(`CMakeLists.txt` at the root is the untracked copy the build uses; it is not committed.)

### Task 4: Resolve spec open item 1 (link pins) and record it

**Files:**
- Create: `tests/pinheck/pic32mx/pins.py`
- Modify: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§4.2 UART line, §5.3, §7 machine check 1, §9 item 1)

**Interfaces:**
- Consumes: `DOM_V006.PRG`'s chipKIT tables: pin → port index bytes at `0x9D02FBA0`, pin → bit-mask halfwords at `0x9D02CAC0`, port index → `TRISx` pointer words at `0x9D0314B4`. `digitalWrite` (`0x9D02B8F4`) and `digitalRead` (`0x9D02DE94`) index them, and the link exchange (`0x9D02AF60`) calls them with pins 16, 15 and 14.
- Produces: the resolved mapping for Plan 5 (RF5 / RF12 / RF13, no return UART), enforced by `check.sh`.

- [ ] **Step 1: Write the pin check**

`tests/pinheck/pic32mx/pins.py`:

```python
#!/usr/bin/env python3
import struct
import sys

BASE = 0x9D000000
PORT_OF_PIN = 0x9D02FBA0
MASK_OF_PIN = 0x9D02CAC0
TRIS_OF_PORT = 0x9D0314B4
PINS = 87
TRIS = {0xBF886000 + 0x40 * i: 'ABCDEFG'[i] for i in range(7)}
LINK = {14: ('F', 13, 'COMM_IN_TX, Propeller -> PIC32 data'),
        15: ('F', 12, 'COMM_CLK_RX, PIC32 -> Propeller clock'),
        16: ('F', 5, 'COMM_OUT, PIC32 -> Propeller data')}


def main():
    d = open(sys.argv[1], 'rb').read()
    b = lambda va: d[va - BASE]
    h = lambda va: struct.unpack_from('<H', d, va - BASE)[0]
    w = lambda va: struct.unpack_from('<I', d, va - BASE)[0]
    bad = 0
    pins = {}
    for n in range(PINS):
        port = b(PORT_OF_PIN + n)
        if not port:
            continue
        tris = w(TRIS_OF_PORT + 4 * port)
        mask = h(MASK_OF_PIN + 2 * n)
        if tris not in TRIS or mask == 0 or mask & (mask - 1):
            print('PINS FAIL pin %d: port pointer %08x mask %04x is not a single port bit' % (n, tris, mask))
            bad += 1
            continue
        pins[n] = (TRIS[tris], mask.bit_length() - 1)
    for n, (port, bit, what) in sorted(LINK.items()):
        got = pins.get(n)
        print('logical %d -> R%s%d  %s' % (n, got[0], got[1], what) if got else 'logical %d -> unmapped' % n)
        if got != (port, bit):
            print('PINS FAIL logical %d is %r, expected R%s%d' % (n, got, port, bit))
            bad += 1
    print('pins: %s (%d logical pins resolved)' % ('FAIL' if bad else 'ok', len(pins)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
```

Make it executable: `chmod +x tests/pinheck/pic32mx/pins.py`

- [ ] **Step 2: Watch it fail on a corrupted table**

Run:
```sh
mkdir -p tests/pinheck/pic32mx/build && python3 - <<'PYEOF'
import os
d = bytearray(open(os.environ['PINHECK_UPDATE_DIR'] + '/DOM_V006.PRG', 'rb').read())
for n in (14, 15, 16):
    d[0x2FBA0 + n] = 0
open('tests/pinheck/pic32mx/build/corrupt.prg', 'wb').write(d)
PYEOF
python3 tests/pinheck/pic32mx/pins.py tests/pinheck/pic32mx/build/corrupt.prg
```
Expected: three `PINS FAIL logical N is None, expected RF…` lines, then `pins: FAIL (84 logical pins resolved)`.

- [ ] **Step 3: Run it on the real firmware**

Run: `PINHECK_UPDATE_DIR=/path/to/update tests/pinheck/pic32mx/check.sh`
Expected, among the output:
```text
logical 14 -> RF13  COMM_IN_TX, Propeller -> PIC32 data
logical 15 -> RF12  COMM_CLK_RX, PIC32 -> Propeller clock
logical 16 -> RF5  COMM_OUT, PIC32 -> Propeller data
pins: ok (87 logical pins resolved)
pic32mx: 0 failed
```

- [ ] **Step 4: Record the findings in the spec**

These corrections come from the firmware:
- The only UART the firmware enables is UART1, the console.
- The return path is RF13, sampled inside the PIC32's own clocked exchange.
- The banner is a second-boot message.

Run from the repository root:

```sh
python3 - <<'PYEOF'
import sys
p = 'docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md'
s = open(p).read()
edits = [
('- **UART1–6**: TX to a byte sink with timestamps; RX from an edge-decoded line.',
 '- **UART1–6**: TX to a byte sink with timestamps; RX bytes injected by the board. The firmware enables only UART1, the service console (BRG 42: 115,200 baud nominal).'),
('''- at every periodic quantum timer, whose period is below one character time of the return UART at the baud the firmware configures

Propeller output-pin edges are timestamped. The PIC32 UART decodes RX bytes from those edges at the programmed baud and raises its interrupts at the decoded times, which are never later than one quantum after the true time.''',
 '''- at every periodic quantum timer, so the Propeller keeps producing video and audio while the PIC32 is not reading it

There is no Propeller→PIC32 UART. The return path is RF13 (COMM_IN_TX, Propeller P24), which the PIC32 samples with `digitalRead(14)` after each falling edge of the clock it drives on RF12, during the same 16-byte exchange that shifts its own packet out on RF5. The first rule above therefore makes the return path exact.'''),
('1. The PIC32 prints its banner (`pinHeck System 2011-2016`, `Game: DOM - DOMINOS`, `Version:`).',
 '1. The PIC32 prints its banner (`pinHeck System 2011-2016`, `Game: DOM - DOMINOS`, `Version:`) on the second boot. The firmware prints it only once U13 holds its settings, which the first boot on a blank EEPROM stores. Reaching it needs the Propeller\'s sync reply and its `readEEPROM`/`writeEEPROM` service; until milestone 5 the test stub `tests/pinheck/pic32mx/linkstub.c` provides them.'),
('1. **chipKIT logical pins → PIC32 port bits** (needed by milestone 5). Read the board variant\'s `digital_pin_to_port` / `digital_pin_to_bit_mask` tables referenced by `digitalWrite` (`0x9D02B8F4`), and confirm which of COMM_OUT / COMM_IN_TX / COMM_CLK_RX are pins 15 and 16. This also gives the Propeller→PIC32 UART and its baud (the image contains the constants 19200, 230400 and 1000000).',
 '1. **chipKIT logical pins → PIC32 port bits** (resolved by milestone 2; checked by `tests/pinheck/pic32mx/pins.py`). Logical 16 = RF5 = COMM_OUT (PIC32→Propeller data), 15 = RF12 = COMM_CLK_RX (clock, driven by the PIC32), 14 = RF13 = COMM_IN_TX (Propeller→PIC32 data). The link is a full-duplex, bit-banged exchange of 16 bytes, LSB first, byte 15 the command; the Propeller stages its reply from the previous packet. There is no Propeller→PIC32 UART: the firmware enables only UART1.'),
]
for old, new in edits:
    if new in s:
        continue
    if s.count(old) != 1:
        sys.exit('spec text not found once: %r' % old[:60])
    s = s.replace(old, new, 1)
open(p, 'w').write(s)
print('spec updated')
PYEOF
```

Expected: `spec updated`. Then `git diff --stat docs/` shows `1 file changed, 5 insertions(+), 5 deletions(-)`.

- [ ] **Step 5: Commit**

```bash
git add tests/pinheck/pic32mx/pins.py docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md
git commit -m "pinheck: resolve open item 1 (link pins RF5/RF12/RF13, no return UART)"
```
