# Milestone 1: `mips32` core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone MIPS32 Release 2 (M4K) interpreter core with disassembler, proven against QEMU's `4KEc` model and against hand-derived privileged-architecture tests.

**Architecture:** `src/cpu/mips32/` is pure C99 with no PinMAME dependency: memory, exceptions and the EIC interrupt input are reached through a callback struct, so the PIC32MX SoC (Milestone 2) can wrap it and the tests can drive it headless. Correctness comes from differential testing: the same MIPS ELF runs under `qemu-mipsel -cpu 4KEc` (Linux user mode) and under a small ELF runner around our core that services `write`/`exit` itself, and the two stdout byte streams must match. Privileged behaviour QEMU user mode cannot referee (CP0, exceptions, EIC, shadow sets, `WAIT`) is covered by golden assembly tests and a cycle-level C unit test.

**Tech Stack:** C99 (gcc/clang, MSVC-compatible), clang 21 integrated assembler (`--target=mipsel-linux-gnu -march=mips32r2`), `ld.lld`, `qemu-mipsel` (qemu-user 11.1), Python 3, POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§4.1, §6, §7 core level, §8 milestone 1). Roadmap of all plans: `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md`.

## Global Constraints

- `src/cpu/mips32/` includes only `<stdint.h>`, `<string.h>`, `<stdio.h>`, `<stdarg.h>` and its own header; no PinMAME headers, no compiler builtins (PinMAME also builds with MSVC).
- Every C file compiles warning-free with `-std=c99 -Wall -Wextra -Werror -pedantic`.
- ISA: MIPS32r2 without FPU. COP1/COP1X/LWC1/LDC1/SWC1/SDC1 raise CpU with CE=1, COP2 forms CE=2. No TLB: fixed-mapping translation. No EJTAG: `Debug`/`DEPC` read 0, `sdbbp` raises RI.
- Fixed mapping: kuseg `VA + 0x40000000` when `Status.ERL=0`, identity when `ERL=1`; kseg0/kseg1 `VA & 0x1FFFFFFF`; kseg2/3 identity; kseg access in user mode raises AdEL/AdES.
- Timing: 1 cycle per instruction, `MUL` +1, `DIV`/`DIVU` +34. `Count` increments every other cycle. `mips32_run` returns early when `Cause.TI` rises.
- Shadow register sets: 1–8 (caller-supplied); interrupts switch to the EIC-supplied set, other exceptions to `SRSCtl.ESS`, only when `Status.EXL=0` and `BEV=0`.
- `PRId` is supplied by the caller. `Config` resets to `0x80000582` (AR=1, MT=3 FMT, K0=2; only K0 writable), `Config1`/`Config2` read `0x80000000`, `Config3` reads `0x00000060` (VEIC, VInt).
- QEMU, clang and lld are external test tools only; nothing under `tests/` is linked into PinMAME.
- Code comments stay short; no comment blocks.

## Review Focus

These inputs are implied by the spec and occur in the real firmware; each is pinned by a test named in its owning task.

- **An interrupt arrives while the instruction about to run is a branch-delay slot**, which the PIC32 firmware hits constantly: `EPC` must be the branch, `Cause.BD=1`, and after `eret` the branch re-executes with its delay slot. Test `irq_in_delay_slot` in Task 4.
- **A fault inside an exception or interrupt handler (`Status.EXL=1`)**: `EPC` and `BD` stay as they were, the vector is `EBase+0x180`, and no shadow-set switch happens. Test `exception_with_exl_set` in Task 4.
- **`Count` jumps past `Compare` inside one multi-cycle instruction** (`DIV` advances `Count` by 17): `Cause.TI` must still rise. `Compare == Count` at the moment of writing must not fire until the counter wraps. Test `compare_crossed_by_div` in Task 4.
- **User mode (`Status.UM=1`, `EXL=ERL=0`)**: kuseg maps to `VA+0x40000000`, a kseg load or store raises AdEL/AdES with `BadVAddr`, CP0 access raises CpU with CE=0. The firmware runs in kernel mode, so nothing else exercises this. Test `user_mode` in Task 4.
- **Mixed-sign multiply-accumulate** (`MADD` vs `MADDU`, `MSUB` vs `MSUBU` with negative operands and carries across the 64-bit accumulator): a signedness bug survived 204 of 206 tests before `diff/mdu_signs.S` (Task 2) was added.

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/mips32/mips32.h` | public API: state, bus callbacks, run/EIC/timer/translate/CP0/dasm entry points |
| `src/cpu/mips32/mips32.c` | interpreter: decode/execute, delay slots, CP0, exceptions, EIC entry, shadow sets, Count/Compare, WAIT |
| `src/cpu/mips32/mips32dasm.c` | disassembler, one instruction to text |
| `tests/pinheck/mips32/check.sh` | builds everything and runs every test; exits non-zero on any failure |
| `tests/pinheck/mips32/run.c` | ELF runner: loads PT_LOAD segments, services `write`/`exit`, EIC test registers |
| `tests/pinheck/mips32/common.h` | assembler macros shared by all test sources |
| `tests/pinheck/mips32/diff/*.S` | directed differential tests (QEMU vs us) |
| `tests/pinheck/mips32/gen.py` | seeded random program generator for differential tests |
| `tests/pinheck/mips32/cmpdump.py` | names the register or scratch word that differs |
| `tests/pinheck/mips32/golden/*.S`, `*.expect` | privileged-architecture tests with hand-derived results |
| `tests/pinheck/mips32/unit_test.c` | cycle-level tests through the C API |
| `tests/pinheck/mips32/dasm_test.c` | disassembler table test |
| `tests/pinheck/mips32/bench.S`, `bench.sh` | throughput measurement |
| `tests/pinheck/mips32/README.md`, `.gitignore` | how to run; ignore `build/` |

Nothing outside these paths changes in this milestone; wiring the core into PinMAME's build is Milestone 2.

---
### Task 1: Toolchain, API header and differential harness

**Files:**
- Create: `src/cpu/mips32/mips32.h`
- Create: `tests/pinheck/mips32/common.h`, `run.c`, `check.sh`, `.gitignore`, `diff/smoke.S`

**Interfaces:**
- Consumes: nothing.
- Produces: the whole public API in `mips32.h`, used unchanged by every later task and by Milestone 2: `mips32_init(mips32_state *s, const mips32_bus *bus, int shadow_sets, uint32_t prid)`, `mips32_reset`, `int mips32_run(mips32_state *s, int cycles)` (returns cycles executed), `mips32_set_eic(s, ripl, vector, srs)`, `int mips32_timer_irq(const mips32_state *s)`, `int mips32_soft_irq(const mips32_state *s)`, `int mips32_translate(const mips32_state *s, uint32_t va, uint32_t *pa)` (0 = address error), `uint32_t mips32_get_cp0(const mips32_state *s, int reg, int sel)`, `unsigned mips32_dasm(char *buf, uint32_t pc, uint32_t op)` (returns 4; `buf` at least 64 bytes). Bus: `read(ctx, pa, size, fetch, *err)`, `write(ctx, pa, data, size, *err)` with `size` 1/2/4 and `*err = 1` for a bus error; `exc_hook(ctx, s, exccode)` returning `MIPS32_HOOK_DELIVER`/`SKIP`/`STOP`; `irq_taken(ctx, vector)`. The runner `tests/pinheck/mips32/build/mips32run [-x] [-e] [-n cycles] prog.elf`.

- [ ] **Step 1: Install the test toolchain**

Run: `sudo apt-get install -y clang lld llvm qemu-user`
Then: `qemu-mipsel -cpu help | grep -w 4KEc`
Expected: a line containing `4KEc`.

- [ ] **Step 2: Write the API header**

`src/cpu/mips32/mips32.h`:

```c
#ifndef MIPS32_H
#define MIPS32_H

#include <stdint.h>

enum {
	MIPS32_EXC_INT  = 0,
	MIPS32_EXC_ADEL = 4,
	MIPS32_EXC_ADES = 5,
	MIPS32_EXC_IBE  = 6,
	MIPS32_EXC_DBE  = 7,
	MIPS32_EXC_SYS  = 8,
	MIPS32_EXC_BP   = 9,
	MIPS32_EXC_RI   = 10,
	MIPS32_EXC_CPU  = 11,
	MIPS32_EXC_OV   = 12,
	MIPS32_EXC_TR   = 13
};

enum { MIPS32_HOOK_DELIVER = 0, MIPS32_HOOK_SKIP = 1, MIPS32_HOOK_STOP = 2 };

typedef struct mips32_state mips32_state;

typedef struct mips32_bus {
	void *ctx;
	uint32_t (*read)(void *ctx, uint32_t pa, int size, int fetch, int *err);
	void (*write)(void *ctx, uint32_t pa, uint32_t data, int size, int *err);
	int (*exc_hook)(void *ctx, mips32_state *s, int exccode);
	void (*irq_taken)(void *ctx, int vector);
} mips32_bus;

struct mips32_state {
	uint32_t gpr[8][32];
	uint32_t *r;
	uint32_t pc, npc, hi, lo;
	int delay;
	uint32_t cur_pc, skip_pc;
	int cur_delay;
	int llbit;
	int waiting;
	int stop;
	int shadow_sets;
	uint32_t status, cause, epc, errorepc, badvaddr, count, compare, ebase;
	uint32_t intctl, srsctl, srsmap, hwrena, config0, prid;
	int count_half;
	int eic_ripl, eic_vector, eic_srs;
	uint64_t cycles;
	mips32_bus bus;
};

void mips32_init(mips32_state *s, const mips32_bus *bus, int shadow_sets, uint32_t prid);
void mips32_reset(mips32_state *s);
int mips32_run(mips32_state *s, int cycles);
void mips32_set_eic(mips32_state *s, int ripl, int vector, int srs);
int mips32_timer_irq(const mips32_state *s);
int mips32_soft_irq(const mips32_state *s);
int mips32_translate(const mips32_state *s, uint32_t va, uint32_t *pa);
uint32_t mips32_get_cp0(const mips32_state *s, int reg, int sel);
unsigned mips32_dasm(char *buf, uint32_t pc, uint32_t op);

#endif
```

- [ ] **Step 3: Write the shared assembler macros**

`WRITE_EXIT` issues the Linux o32 `write(1, buf, len)` then `exit(0)` syscalls, which both QEMU and our runner service. `ST` appends a register to the output buffer through `$23`. `RDPGPR`/`WRPGPR` are emitted as words because LLVM rejects those mnemonics under `-march=mips32r2`.

`tests/pinheck/mips32/common.h`:

```c
#define SYS_EXIT  4001
#define SYS_WRITE 4004
#define MMIO_EIC  0xA0FFFFF0
#define MMIO_CT   0xA0FFFFF4

	.set noreorder
	.set noat

#define WRITE_EXIT(buf, len) \
	li $2, SYS_WRITE; li $4, 1; la $5, buf; li $6, len; syscall; \
	li $2, SYS_EXIT; li $4, 0; syscall

#define RDPGPR(rd, rt) .word (0x41400000 | ((rt) << 16) | ((rd) << 11))
#define WRPGPR(rd, rt) .word (0x41C00000 | ((rt) << 16) | ((rd) << 11))

#define ST(r) sw r, 0($23); addiu $23, $23, 4
```

- [ ] **Step 4: Write the ELF runner**

`tests/pinheck/mips32/run.c`:

```c
#include "mips32.h"
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMSIZE  (16u << 20)
#define MMIO_EIC 0x00FFFFF0u
#define MMIO_CT  0x00FFFFF4u

static uint8_t *mem;
static int hexout, allow_exc, exit_status = -1;
static uint32_t mmio_eic, ct_flag;
static uint8_t hexbuf[4];
static int hexfill;
static mips32_state cpu;

static void update_eic(void)
{
	if (ct_flag) mips32_set_eic(&cpu, 1, 0, 0);
	else mips32_set_eic(&cpu, (int)(mmio_eic & 0x3F), (int)((mmio_eic >> 8) & 0x3F), (int)((mmio_eic >> 16) & 7));
}

static uint32_t bus_read(void *ctx, uint32_t pa, int size, int fetch, int *err)
{
	uint32_t v = 0;
	int i;
	(void)ctx; (void)fetch;
	if (pa >= MEMSIZE || MEMSIZE - pa < (uint32_t)size) { *err = 1; return 0; }
	for (i = 0; i < size; i++) v |= (uint32_t)mem[pa + i] << (8 * i);
	return v;
}

static void bus_write(void *ctx, uint32_t pa, uint32_t data, int size, int *err)
{
	int i;
	(void)ctx;
	if (pa == MMIO_EIC && size == 4) { mmio_eic = data; update_eic(); return; }
	if (pa == MMIO_CT && size == 4) { ct_flag = 0; update_eic(); return; }
	if (pa >= MEMSIZE || MEMSIZE - pa < (uint32_t)size) { *err = 1; return; }
	for (i = 0; i < size; i++) mem[pa + i] = (uint8_t)(data >> (8 * i));
}

static void out_byte(uint8_t b)
{
	if (!hexout) { fputc(b, stdout); return; }
	hexbuf[hexfill++] = b;
	if (hexfill == 4) {
		printf("%08x\n", (unsigned)(hexbuf[0] | hexbuf[1] << 8 | hexbuf[2] << 16 | (uint32_t)hexbuf[3] << 24));
		hexfill = 0;
	}
}

static int exc_hook(void *ctx, mips32_state *s, int code)
{
	(void)ctx;
	if (code == MIPS32_EXC_SYS) {
		uint32_t nr = s->r[2], i, pa;
		if (nr == 4004) {
			for (i = 0; i < s->r[6]; i++) {
				if (!mips32_translate(s, s->r[5] + i, &pa) || pa >= MEMSIZE) {
					fprintf(stderr, "write: bad buffer %08x\n", (unsigned)(s->r[5] + i));
					exit_status = 98;
					return MIPS32_HOOK_STOP;
				}
				out_byte(mem[pa]);
			}
			s->r[2] = s->r[6];
			s->r[7] = 0;
			return MIPS32_HOOK_SKIP;
		}
		if (nr == 4001) { exit_status = (int)(s->r[4] & 0xFF); return MIPS32_HOOK_STOP; }
		if (allow_exc) return MIPS32_HOOK_DELIVER;
		fprintf(stderr, "unsupported syscall %u at %08x\n", (unsigned)nr, (unsigned)s->cur_pc);
		exit_status = 99;
		return MIPS32_HOOK_STOP;
	}
	if (allow_exc) return MIPS32_HOOK_DELIVER;
	fprintf(stderr, "unexpected exception %d at %08x\n", code, (unsigned)s->cur_pc);
	exit_status = 97;
	return MIPS32_HOOK_STOP;
}

static int load_elf(const char *path, uint32_t *entry)
{
	FILE *f = fopen(path, "rb");
	Elf32_Ehdr eh;
	Elf32_Phdr ph;
	int i;

	if (!f) { perror(path); return 0; }
	if (fread(&eh, sizeof(eh), 1, f) != 1 || memcmp(eh.e_ident, ELFMAG, SELFMAG) ||
	    eh.e_ident[EI_CLASS] != ELFCLASS32 || eh.e_ident[EI_DATA] != ELFDATA2LSB || eh.e_machine != EM_MIPS) {
		fprintf(stderr, "%s: not a little-endian MIPS32 ELF\n", path);
		fclose(f);
		return 0;
	}
	for (i = 0; i < eh.e_phnum; i++) {
		uint32_t pa;
		if (fseek(f, (long)(eh.e_phoff + (uint32_t)i * eh.e_phentsize), SEEK_SET) || fread(&ph, sizeof(ph), 1, f) != 1) break;
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		if (!mips32_translate(&cpu, ph.p_vaddr, &pa) || pa >= MEMSIZE || MEMSIZE - pa < ph.p_memsz) {
			fprintf(stderr, "%s: segment %08x outside memory\n", path, (unsigned)ph.p_vaddr);
			fclose(f);
			return 0;
		}
		memset(mem + pa, 0, ph.p_memsz);
		if (ph.p_filesz && (fseek(f, (long)ph.p_offset, SEEK_SET) || fread(mem + pa, ph.p_filesz, 1, f) != 1)) {
			fprintf(stderr, "%s: short read\n", path);
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	*entry = eh.e_entry;
	return 1;
}

int main(int argc, char **argv)
{
	mips32_bus bus = { NULL, bus_read, bus_write, exc_hook, NULL };
	uint64_t limit = 100000000;
	uint32_t entry;
	const char *path = NULL;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-x")) hexout = 1;
		else if (!strcmp(argv[i], "-e")) allow_exc = 1;
		else if (!strcmp(argv[i], "-n") && i + 1 < argc) limit = strtoull(argv[++i], NULL, 0);
		else path = argv[i];
	}
	if (!path) { fprintf(stderr, "usage: mips32run [-x] [-e] [-n cycles] prog.elf\n"); return 2; }
	mem = calloc(1, MEMSIZE);
	if (!mem) return 2;
	mips32_init(&cpu, &bus, 2, 0x00018700u);
	if (!load_elf(path, &entry)) return 2;
	cpu.pc = entry;
	cpu.npc = entry + 4;
	while (exit_status < 0 && cpu.cycles < limit) {
		mips32_run(&cpu, 100000);
		if (mips32_timer_irq(&cpu) && !ct_flag) { ct_flag = 1; update_eic(); }
	}
	fflush(stdout);
	if (exit_status < 0) { fprintf(stderr, "cycle limit reached at %08x\n", (unsigned)cpu.pc); return 124; }
	return exit_status;
}
```

- [ ] **Step 5: Write the test driver**

Diff tests link at `0x400000` (kuseg; the core starts in the reset state with `ERL=1`, so kuseg is identity-mapped). Golden tests link at `0x80400000` with the `.exc` section at `0x80000180`, because they clear `ERL` and must keep executing from kseg0.

`tests/pinheck/mips32/check.sh`:

```sh
#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
SEEDS=${SEEDS:-200}
ASM="clang --target=mipsel-linux-gnu -march=mips32r2 -mno-abicalls -fno-pic -I."
mkdir -p $B/diff $B/golden $B/random
cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/mips32 -o $B/mips32run run.c ../../../src/cpu/mips32/mips32.c || exit 2
if [ -f dasm_test.c ]; then
	cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/mips32 -o $B/dasm_test dasm_test.c ../../../src/cpu/mips32/mips32dasm.c || exit 2
fi
if [ -f unit_test.c ]; then
	cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/mips32 -o $B/unit_test unit_test.c ../../../src/cpu/mips32/mips32.c || exit 2
fi
fail=0 pass=0

run_diff() {
	n=$(basename "$1" .S) o=$2/$(basename "$1" .S)
	if ! $ASM -c "$1" -o $o.o || ! ld.lld -m elf32ltsmip -static -e __start -Ttext=0x400000 $o.o -o $o.elf; then
		echo "BUILD FAIL $1"; fail=$((fail + 1)); return
	fi
	qemu-mipsel -cpu 4KEc $o.elf > $o.qemu || { echo "QEMU FAIL $1"; fail=$((fail + 1)); return; }
	$B/mips32run $o.elf > $o.ours || { echo "RUN FAIL $1 (exit $?)"; fail=$((fail + 1)); return; }
	if cmp -s $o.qemu $o.ours; then pass=$((pass + 1)); else echo "MISMATCH $1"; python3 cmpdump.py $o.qemu $o.ours; fail=$((fail + 1)); fi
}

[ -x $B/dasm_test ] && { ./$B/dasm_test || fail=$((fail + 1)); }
[ -x $B/unit_test ] && { ./$B/unit_test || fail=$((fail + 1)); }
for f in diff/*.S; do [ -e "$f" ] && run_diff "$f" $B/diff; done
if [ -f gen.py ]; then
	rm -rf $B/random && python3 gen.py --out $B/random --count "$SEEDS"
	for f in $B/random/*.S; do run_diff "$f" $B/random; done
fi
for f in golden/*.S; do
	[ -e "$f" ] || continue
	n=$(basename "$f" .S) o=$B/golden/$(basename "$f" .S)
	if ! $ASM -c "$f" -o $o.o || ! ld.lld -m elf32ltsmip -static -e __start -Ttext=0x80400000 --section-start=.exc=0x80000180 $o.o -o $o.elf; then
		echo "BUILD FAIL $f"; fail=$((fail + 1)); continue
	fi
	$B/mips32run -x -e $o.elf > $o.out
	if diff -u golden/$n.expect $o.out > $o.diff; then pass=$((pass + 1)); else echo "GOLDEN FAIL $f"; cat $o.diff; fail=$((fail + 1)); fi
done
echo "mips32: $pass passed, $fail failed"
[ $fail -eq 0 ]
```

Make it executable: `chmod +x tests/pinheck/mips32/check.sh`

`tests/pinheck/mips32/.gitignore`:

```text
build/
```

- [ ] **Step 6: Write the smoke test**

`tests/pinheck/mips32/diff/smoke.S`:

```asm
#include "common.h"
	.text
	.globl __start
__start:
	lui $8, 0x1234
	ori $8, $8, 0x5678
	ext $9, $8, 4, 8
	ins $8, $9, 24, 4
	wsbh $10, $8
	seb $11, $8
	clz $12, $9
	la $4, out
	sw $8, 0($4)
	sw $9, 4($4)
	sw $10, 8($4)
	sw $11, 12($4)
	sw $12, 16($4)
	WRITE_EXIT(out, 20)
	.data
	.align 4
out:	.space 64
```

- [ ] **Step 7: Run it and confirm it fails**

Run: `tests/pinheck/mips32/check.sh`
Expected: exit status 2 with `fatal error: ../../../src/cpu/mips32/mips32.c: No such file or directory`.

- [ ] **Step 8: Confirm the oracle side alone works**

Run:
```sh
cd tests/pinheck/mips32 && mkdir -p build && \
clang --target=mipsel-linux-gnu -march=mips32r2 -mno-abicalls -fno-pic -I. -c diff/smoke.S -o build/smoke.o && \
ld.lld -m elf32ltsmip -static -e __start -Ttext=0x400000 build/smoke.o -o build/smoke.elf && \
qemu-mipsel -cpu 4KEc build/smoke.elf | xxd; cd -
```
Expected:
```text
00000000: 7856 3417 6700 0000 5678 1734 7800 0000  xV4.g...Vx.4x...
00000010: 1900 0000                                ....
```
(`ext` = 0x67, `ins` → 0x17345678, `wsbh` → 0x34177856, `seb` → 0x78, `clz(0x67)` = 25.)

- [ ] **Step 9: Commit**

```bash
git add src/cpu/mips32/mips32.h tests/pinheck/mips32
git commit -m "mips32: API header and QEMU differential harness"
```

### Task 2: Interpreter core with directed differential tests

**Files:**
- Create: `tests/pinheck/mips32/diff/alu_edges.S`, `branches.S`, `loadstore.S`, `mdu_signs.S`
- Create: `src/cpu/mips32/mips32.c`

**Interfaces:**
- Consumes: `mips32.h` from Task 1.
- Produces: the implementation of every function declared in `mips32.h` except `mips32_dasm` (Task 5).

- [ ] **Step 1: Write the directed differential tests**

Each program stores results through `ST` and writes its whole output buffer. No expected values are written down: QEMU is the oracle.

`tests/pinheck/mips32/diff/alu_edges.S`:

```asm
#include "common.h"
	.text
	.globl __start
__start:
	la $23, out
	li $8, 0x80000000
	li $9, 0xffffffff
	li $10, 0x7fffffff
	li $11, 33
	li $12, 0x00008080
	li $13, 1
	sll $2, $9, 31
	ST($2)
	srl $2, $8, 31
	ST($2)
	sra $2, $8, 31
	ST($2)
	sra $2, $10, 0
	ST($2)
	rotr $2, $8, 0
	ST($2)
	rotr $2, $8, 31
	ST($2)
	sllv $2, $9, $11
	ST($2)
	rotrv $2, $10, $11
	ST($2)
	srav $2, $8, $11
	ST($2)
	slt $2, $8, $10
	ST($2)
	sltu $2, $8, $10
	ST($2)
	slti $2, $8, -1
	ST($2)
	sltiu $2, $10, -1
	ST($2)
	clz $2, $0
	ST($2)
	clo $2, $9
	ST($2)
	clz $2, $8
	ST($2)
	seb $2, $12
	ST($2)
	seh $2, $12
	ST($2)
	wsbh $2, $8
	ST($2)
	ext $2, $9, 0, 32
	ST($2)
	ext $2, $8, 31, 1
	ST($2)
	move $2, $9
	ins $2, $0, 0, 32
	ST($2)
	move $2, $0
	ins $2, $9, 31, 1
	ST($2)
	li $2, 7
	movz $2, $9, $0
	ST($2)
	li $2, 7
	movn $2, $9, $0
	ST($2)
	mult $8, $9
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	multu $9, $9
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	mthi $0
	mtlo $9
	maddu $13, $13
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	mthi $0
	mtlo $0
	msub $13, $13
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	mthi $8
	mtlo $0
	madd $8, $9
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	div $zero, $8, $13
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	li $14, -7
	li $15, 2
	div $zero, $14, $15
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	divu $zero, $14, $15
	mfhi $2
	ST($2)
	mflo $2
	ST($2)
	mul $2, $8, $9
	ST($2)
	addiu $2, $10, 1
	ST($2)
	lui $2, 0xffff
	ST($2)
	nor $2, $0, $0
	ST($2)
	WRITE_EXIT(out, 256)
	.data
	.align 4
out:	.space 256
```

`tests/pinheck/mips32/diff/branches.S`:

```asm
#include "common.h"
	.text
	.globl __start
__start:
	la $23, out
	li $16, 0
	li $9, 1
	beq $0, $0, 1f
	addiu $16, $16, 1
	addiu $16, $16, 100
1:	ST($16)
	bne $0, $0, 2f
	addiu $16, $16, 2
	addiu $16, $16, 4
2:	ST($16)
	beql $0, $9, 3f
	addiu $16, $16, 8
	addiu $16, $16, 16
3:	ST($16)
	bnel $0, $9, 4f
	addiu $16, $16, 32
	addiu $16, $16, 64
4:	ST($16)
	bltzal $9, 5f
	nop
5:	ST($31)
	bgezall $0, 6f
	addiu $16, $16, 1
	addiu $16, $16, 1000
6:	ST($16)
	ST($31)
	bltzall $9, 7f
	addiu $16, $16, 3
	addiu $16, $16, 5
7:	ST($16)
	ST($31)
	jal 8f
	addiu $16, $16, 1
	addiu $16, $16, 1000
8:	ST($16)
	ST($31)
	la $1, 9f
	jalr $30, $1
	addiu $16, $16, 1
	addiu $16, $16, 1000
9:	ST($16)
	ST($30)
	la $1, 10f
	jr $1
	addiu $16, $16, 1
	addiu $16, $16, 1000
10:	ST($16)
	li $17, 5
	li $18, 0
11:	addiu $18, $18, 3
	addiu $17, $17, -1
	bgtz $17, 11b
	addiu $18, $18, 1
	ST($18)
	li $17, 3
	li $18, 0
12:	addiu $17, $17, -1
	bgtzl $17, 12b
	addiu $18, $18, 10
	ST($18)
	li $19, -1
	blez $19, 13f
	addiu $16, $16, 1
	addiu $16, $16, 1000
13:	ST($16)
	bltz $0, 14f
	addiu $16, $16, 1
	addiu $16, $16, 2
14:	ST($16)
	bgezl $19, 15f
	addiu $16, $16, 1000
	addiu $16, $16, 7
15:	ST($16)
	blezl $9, 16f
	addiu $16, $16, 1000
	addiu $16, $16, 9
16:	ST($16)
	WRITE_EXIT(out, 256)
	.data
	.align 4
out:	.space 256
```

`tests/pinheck/mips32/diff/loadstore.S`:

```asm
#include "common.h"
	.text
	.globl __start
__start:
	la $23, out
	la $20, buf
	li $9, 0xaabbccdd
	li $2, 0x01020304
	lwl $2, 0($20)
	ST($2)
	li $2, 0x01020304
	lwl $2, 1($20)
	ST($2)
	li $2, 0x01020304
	lwl $2, 2($20)
	ST($2)
	li $2, 0x01020304
	lwl $2, 3($20)
	ST($2)
	li $2, 0x01020304
	lwr $2, 0($20)
	ST($2)
	li $2, 0x01020304
	lwr $2, 1($20)
	ST($2)
	li $2, 0x01020304
	lwr $2, 2($20)
	ST($2)
	li $2, 0x01020304
	lwr $2, 3($20)
	ST($2)
	lwr $2, 1($20)
	lwl $2, 4($20)
	ST($2)
	swl $9, 16($20)
	swl $9, 21($20)
	swl $9, 26($20)
	swl $9, 31($20)
	swr $9, 32($20)
	swr $9, 37($20)
	swr $9, 42($20)
	swr $9, 47($20)
	lb $2, 3($20)
	ST($2)
	lbu $2, 3($20)
	ST($2)
	lh $2, 2($20)
	ST($2)
	lhu $2, 2($20)
	ST($2)
	lh $2, 6($20)
	ST($2)
	sb $9, 49($20)
	sh $9, 50($20)
	ll $2, 52($20)
	addiu $2, $2, 1
	sc $2, 52($20)
	ST($2)
	lw $3, 52($20)
	ST($3)
	WRITE_EXIT(out, 256)
	.data
	.align 4
out:	.space 128
buf:	.byte 0x11, 0x22, 0x33, 0x84, 0x55, 0x66, 0x77, 0x88
	.byte 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
	.space 36
	.word 0x7ffffffe
	.space 72
```

`mdu_signs.S` pins Review Focus item 5 (mixed-sign accumulation):

`tests/pinheck/mips32/diff/mdu_signs.S`:

```asm
#include "common.h"

#define ACC(op, a, b, h, l) \
	li $1, h; mthi $1; li $1, l; mtlo $1; li $8, a; li $9, b; op $8, $9; mfhi $2; ST($2); mflo $2; ST($2)

	.text
	.globl __start
__start:
	la $23, out
	ACC(mult,  -3, 7, 0, 0)
	ACC(multu, -3, 7, 0, 0)
	ACC(madd,  -3, 7, 0, 5)
	ACC(maddu, -3, 7, 0, 5)
	ACC(msub,  -3, 7, 0, 5)
	ACC(msubu, -3, 7, 0, 5)
	ACC(madd,  0x80000000, 0x80000000, 0xffffffff, 0xffffffff)
	ACC(msub,  0x80000000, -1, 0x7fffffff, 0)
	ACC(maddu, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff)
	ACC(msubu, 0xffffffff, 2, 0, 1)
	ACC(madd,  0x7fffffff, -2, 1, 0x80000000)
	ACC(msub,  -1, -1, 0, 0)
	WRITE_EXIT(out, 96)
	.data
	.align 4
out:	.space 128
```

- [ ] **Step 2: Run to confirm they fail**

Run: `tests/pinheck/mips32/check.sh`
Expected: still exit 2, `mips32.c: No such file or directory`.

- [ ] **Step 3: Write the core**

Key points, all visible in the code:
- `step()` records `cur_pc`, `cur_delay` and `skip_pc`, then does the default advance (`pc = npc; npc += 4`) *before* executing. Branches only adjust `npc` and set `delay`; a not-taken likely branch skips its slot by advancing again.
- `take_exception()` uses `cur_pc`/`cur_delay` for `EPC`/`BD`, so a fault in a delay slot reports the branch. It does nothing to `EPC`, `BD` or the shadow set when `EXL` is already 1.
- Stores to unaligned `SWL`/`SWR` targets are done as byte writes, so the bus never sees a read-modify-write.
- `DIV` of `INT32_MIN` by −1 is special-cased (C UB). Division by zero leaves HI/LO unchanged (UNPREDICTABLE in the ISA; never compared against QEMU).

`src/cpu/mips32/mips32.c`:

```c
#include "mips32.h"
#include <string.h>

#define ST_IE    0x00000001u
#define ST_EXL   0x00000002u
#define ST_ERL   0x00000004u
#define ST_UM    0x00000010u
#define ST_BEV   0x00400000u
#define ST_CU0   0x10000000u
#define ST_WMASK 0x1A48FF17u
#define ST_IPL(v) (((v) >> 10) & 0x3F)

#define CA_BD    0x80000000u
#define CA_TI    0x40000000u
#define CA_IV    0x00800000u
#define CA_WMASK 0x08C00300u

#define RS(op)    (((op) >> 21) & 31)
#define RT(op)    (((op) >> 16) & 31)
#define RD(op)    (((op) >> 11) & 31)
#define SA(op)    (((op) >> 6) & 31)
#define FUNCT(op) ((op) & 63)
#define SIMM(op)  ((uint32_t)(int32_t)(int16_t)((op) & 0xFFFF))
#define UIMM(op)  ((op) & 0xFFFFu)

#define USER(s) (((s)->status & (ST_UM | ST_EXL | ST_ERL)) == ST_UM)
#define SET(n, v) do { uint32_t v_ = (v); int n_ = (n); if (n_) s->r[n_] = v_; } while (0)

static uint32_t ror32(uint32_t x, unsigned n) { n &= 31; return n ? (x >> n) | (x << (32 - n)) : x; }
static uint32_t sra32(uint32_t x, unsigned n) { n &= 31; return n ? (x >> n) | ((x & 0x80000000u) ? ~(0xFFFFFFFFu >> n) : 0) : x; }
static uint32_t mask32(unsigned size) { return size >= 32 ? 0xFFFFFFFFu : (1u << size) - 1; }

static uint32_t clz32(uint32_t x)
{
	uint32_t n = 0;
	if (!x) return 32;
	while (!(x & 0x80000000u)) { x <<= 1; n++; }
	return n;
}

int mips32_translate(const mips32_state *s, uint32_t va, uint32_t *pa)
{
	if (va < 0x80000000u) {
		*pa = (s->status & ST_ERL) ? va : va + 0x40000000u;
		return 1;
	}
	if (USER(s)) return 0;
	*pa = va < 0xC0000000u ? (va & 0x1FFFFFFFu) : va;
	return 1;
}

static void take_exception(mips32_state *s, int code, int ce)
{
	int is_int = code == MIPS32_EXC_INT;
	uint32_t off = 0x180, base;

	if (s->bus.exc_hook) {
		int h = s->bus.exc_hook(s->bus.ctx, s, code);
		if (h == MIPS32_HOOK_SKIP) { s->pc = s->skip_pc; s->npc = s->pc + 4; s->delay = 0; return; }
		if (h == MIPS32_HOOK_STOP) { s->stop = 1; return; }
	}
	if (!(s->status & ST_EXL)) {
		s->epc = s->cur_delay ? s->cur_pc - 4 : s->cur_pc;
		if (s->cur_delay) s->cause |= CA_BD; else s->cause &= ~CA_BD;
		if (is_int && (s->cause & CA_IV))
			off = (s->status & ST_BEV) ? 0x200 : 0x200 + (uint32_t)s->eic_vector * (((s->intctl >> 5) & 0x1F) << 5);
		if (s->shadow_sets > 1 && !(s->status & ST_BEV)) {
			uint32_t css = s->srsctl & 15;
			uint32_t nss = (is_int ? (uint32_t)s->eic_srs : (s->srsctl >> 12)) & 7;
			s->srsctl = (s->srsctl & ~0x3CFu) | (css << 6) | nss;
			s->r = s->gpr[nss];
		}
	}
	s->cause = (s->cause & ~0x3000007Cu) | ((uint32_t)code << 2) | ((uint32_t)ce << 28);
	if (is_int) s->cause = (s->cause & ~0xFC00u) | ((uint32_t)s->eic_ripl << 10);
	s->status |= ST_EXL;
	base = (s->status & ST_BEV) ? 0xBFC00200u : (s->ebase & 0xFFFFF000u);
	s->pc = base + off;
	s->npc = s->pc + 4;
	s->delay = 0;
	s->waiting = 0;
	if (is_int && s->bus.irq_taken) s->bus.irq_taken(s->bus.ctx, s->eic_vector);
}

static int load(mips32_state *s, uint32_t va, int size, uint32_t *out)
{
	uint32_t pa;
	int err = 0;
	if ((va & (uint32_t)(size - 1)) || !mips32_translate(s, va, &pa)) {
		s->badvaddr = va;
		take_exception(s, MIPS32_EXC_ADEL, 0);
		return 0;
	}
	*out = s->bus.read(s->bus.ctx, pa, size, 0, &err);
	if (err) { take_exception(s, MIPS32_EXC_DBE, 0); return 0; }
	return 1;
}

static int store(mips32_state *s, uint32_t va, uint32_t v, int size)
{
	uint32_t pa;
	int err = 0;
	if ((va & (uint32_t)(size - 1)) || !mips32_translate(s, va, &pa)) {
		s->badvaddr = va;
		take_exception(s, MIPS32_EXC_ADES, 0);
		return 0;
	}
	s->bus.write(s->bus.ctx, pa, v, size, &err);
	if (err) { take_exception(s, MIPS32_EXC_DBE, 0); return 0; }
	return 1;
}

uint32_t mips32_get_cp0(const mips32_state *s, int reg, int sel)
{
	switch (reg * 8 + sel) {
	case 7 * 8:      return s->hwrena;
	case 8 * 8:      return s->badvaddr;
	case 9 * 8:      return s->count;
	case 11 * 8:     return s->compare;
	case 12 * 8:     return s->status;
	case 12 * 8 + 1: return s->intctl;
	case 12 * 8 + 2: return s->srsctl | ((uint32_t)(s->shadow_sets - 1) << 26) | (((uint32_t)s->eic_srs & 15) << 18);
	case 12 * 8 + 3: return s->srsmap;
	case 13 * 8:     return s->cause;
	case 14 * 8:     return s->epc;
	case 15 * 8:     return s->prid;
	case 15 * 8 + 1: return s->ebase;
	case 16 * 8:     return s->config0;
	case 16 * 8 + 1: return 0x80000000u;
	case 16 * 8 + 2: return 0x80000000u;
	case 16 * 8 + 3: return 0x00000060u;
	case 30 * 8:     return s->errorepc;
	}
	return 0;
}

static void set_cp0(mips32_state *s, int reg, int sel, uint32_t v)
{
	switch (reg * 8 + sel) {
	case 7 * 8:      s->hwrena = v & 0xFu; break;
	case 9 * 8:      s->count = v; s->count_half = 0; break;
	case 11 * 8:     s->compare = v; s->cause &= ~CA_TI; break;
	case 12 * 8:     s->status = (s->status & ~ST_WMASK) | (v & ST_WMASK); break;
	case 12 * 8 + 1: s->intctl = v & 0x3E0u; break;
	case 12 * 8 + 2: s->srsctl = (s->srsctl & ~0xF3C0u) | (v & 0xF3C0u); break;
	case 12 * 8 + 3: s->srsmap = v; break;
	case 13 * 8:     s->cause = (s->cause & ~CA_WMASK) | (v & CA_WMASK); break;
	case 14 * 8:     s->epc = v; break;
	case 15 * 8 + 1: s->ebase = 0x80000000u | (v & 0x3FFFF000u); break;
	case 16 * 8:     s->config0 = (s->config0 & ~7u) | (v & 7u); break;
	case 30 * 8:     s->errorepc = v; break;
	}
}

static void branch(mips32_state *s, int cond, uint32_t op)
{
	if (cond) s->npc = s->cur_pc + 4 + (SIMM(op) << 2);
	s->delay = 1;
}

static void branch_likely(mips32_state *s, int cond, uint32_t op)
{
	if (cond) {
		s->npc = s->cur_pc + 4 + (SIMM(op) << 2);
		s->delay = 1;
	} else {
		s->pc = s->npc;
		s->npc += 4;
	}
}

static void jump(mips32_state *s, uint32_t target)
{
	s->npc = target;
	s->delay = 1;
}

static void eret(mips32_state *s)
{
	if (s->status & ST_ERL) {
		s->pc = s->errorepc;
		s->status &= ~ST_ERL;
	} else {
		s->pc = s->epc;
		s->status &= ~ST_EXL;
		if (s->shadow_sets > 1 && !(s->status & ST_BEV)) {
			uint32_t pss = (s->srsctl >> 6) & 7;
			s->srsctl = (s->srsctl & ~15u) | pss;
			s->r = s->gpr[pss];
		}
	}
	s->npc = s->pc + 4;
	s->delay = 0;
	s->llbit = 0;
}

static void exec_special(mips32_state *s, uint32_t op)
{
	uint32_t rs = s->r[RS(op)], rt = s->r[RT(op)], res;
	int64_t p;
	uint64_t acc;

	switch (FUNCT(op)) {
	case 0x00: SET(RD(op), rt << SA(op)); break;
	case 0x02: SET(RD(op), (op & (1u << 21)) ? ror32(rt, SA(op)) : rt >> SA(op)); break;
	case 0x03: SET(RD(op), sra32(rt, SA(op))); break;
	case 0x04: SET(RD(op), rt << (rs & 31)); break;
	case 0x06: SET(RD(op), (op & (1u << 6)) ? ror32(rt, rs) : rt >> (rs & 31)); break;
	case 0x07: SET(RD(op), sra32(rt, rs)); break;
	case 0x08: jump(s, rs); break;
	case 0x09: SET(RD(op), s->cur_pc + 8); jump(s, rs); break;
	case 0x0A: if (!rt) SET(RD(op), rs); break;
	case 0x0B: if (rt) SET(RD(op), rs); break;
	case 0x0C: take_exception(s, MIPS32_EXC_SYS, 0); break;
	case 0x0D: take_exception(s, MIPS32_EXC_BP, 0); break;
	case 0x0F: break;
	case 0x10: SET(RD(op), s->hi); break;
	case 0x11: s->hi = rs; break;
	case 0x12: SET(RD(op), s->lo); break;
	case 0x13: s->lo = rs; break;
	case 0x18:
		p = (int64_t)(int32_t)rs * (int32_t)rt;
		s->lo = (uint32_t)p; s->hi = (uint32_t)((uint64_t)p >> 32);
		break;
	case 0x19:
		acc = (uint64_t)rs * rt;
		s->lo = (uint32_t)acc; s->hi = (uint32_t)(acc >> 32);
		break;
	case 0x1A:
		if (rt) {
			if (rs == 0x80000000u && rt == 0xFFFFFFFFu) { s->lo = 0x80000000u; s->hi = 0; }
			else { s->lo = (uint32_t)((int32_t)rs / (int32_t)rt); s->hi = (uint32_t)((int32_t)rs % (int32_t)rt); }
		}
		s->cycles += 34;
		break;
	case 0x1B:
		if (rt) { s->lo = rs / rt; s->hi = rs % rt; }
		s->cycles += 34;
		break;
	case 0x20:
		res = rs + rt;
		if (~(rs ^ rt) & (rs ^ res) & 0x80000000u) take_exception(s, MIPS32_EXC_OV, 0);
		else SET(RD(op), res);
		break;
	case 0x21: SET(RD(op), rs + rt); break;
	case 0x22:
		res = rs - rt;
		if ((rs ^ rt) & (rs ^ res) & 0x80000000u) take_exception(s, MIPS32_EXC_OV, 0);
		else SET(RD(op), res);
		break;
	case 0x23: SET(RD(op), rs - rt); break;
	case 0x24: SET(RD(op), rs & rt); break;
	case 0x25: SET(RD(op), rs | rt); break;
	case 0x26: SET(RD(op), rs ^ rt); break;
	case 0x27: SET(RD(op), ~(rs | rt)); break;
	case 0x2A: SET(RD(op), (int32_t)rs < (int32_t)rt); break;
	case 0x2B: SET(RD(op), rs < rt); break;
	case 0x30: if ((int32_t)rs >= (int32_t)rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x31: if (rs >= rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x32: if ((int32_t)rs < (int32_t)rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x33: if (rs < rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x34: if (rs == rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x36: if (rs != rt) take_exception(s, MIPS32_EXC_TR, 0); break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void exec_regimm(mips32_state *s, uint32_t op)
{
	int32_t rs = (int32_t)s->r[RS(op)];
	uint32_t imm = SIMM(op);
	int cond;

	switch (RT(op)) {
	case 0x00: branch(s, rs < 0, op); break;
	case 0x01: branch(s, rs >= 0, op); break;
	case 0x02: branch_likely(s, rs < 0, op); break;
	case 0x03: branch_likely(s, rs >= 0, op); break;
	case 0x08: if (rs >= (int32_t)imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x09: if ((uint32_t)rs >= imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x0A: if (rs < (int32_t)imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x0B: if ((uint32_t)rs < imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x0C: if ((uint32_t)rs == imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x0E: if ((uint32_t)rs != imm) take_exception(s, MIPS32_EXC_TR, 0); break;
	case 0x10: cond = rs < 0; SET(31, s->cur_pc + 8); branch(s, cond, op); break;
	case 0x11: cond = rs >= 0; SET(31, s->cur_pc + 8); branch(s, cond, op); break;
	case 0x12: cond = rs < 0; SET(31, s->cur_pc + 8); branch_likely(s, cond, op); break;
	case 0x13: cond = rs >= 0; SET(31, s->cur_pc + 8); branch_likely(s, cond, op); break;
	case 0x1F: break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void exec_cop0(mips32_state *s, uint32_t op)
{
	uint32_t v;

	if (USER(s) && !(s->status & ST_CU0)) { take_exception(s, MIPS32_EXC_CPU, 0); return; }
	if (op & (1u << 25)) {
		switch (FUNCT(op)) {
		case 0x18: eret(s); break;
		case 0x20: s->waiting = 1; break;
		default: take_exception(s, MIPS32_EXC_RI, 0); break;
		}
		return;
	}
	switch (RS(op)) {
	case 0x00: SET(RT(op), mips32_get_cp0(s, RD(op), op & 7)); break;
	case 0x04: set_cp0(s, RD(op), op & 7, s->r[RT(op)]); break;
	case 0x0A: SET(RD(op), s->gpr[(s->srsctl >> 6) & 7][RT(op)]); break;
	case 0x0B:
		v = s->status;
		if (op & 0x20) s->status |= ST_IE; else s->status &= ~ST_IE;
		SET(RT(op), v);
		break;
	case 0x0E: if (RD(op)) s->gpr[(s->srsctl >> 6) & 7][RD(op)] = s->r[RT(op)]; break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void exec_special2(mips32_state *s, uint32_t op)
{
	uint32_t rs = s->r[RS(op)], rt = s->r[RT(op)];
	uint64_t acc = ((uint64_t)s->hi << 32) | s->lo;

	switch (FUNCT(op)) {
	case 0x00: acc += (uint64_t)((int64_t)(int32_t)rs * (int32_t)rt); break;
	case 0x01: acc += (uint64_t)rs * rt; break;
	case 0x02: SET(RD(op), (uint32_t)((int64_t)(int32_t)rs * (int32_t)rt)); s->cycles += 1; return;
	case 0x04: acc -= (uint64_t)((int64_t)(int32_t)rs * (int32_t)rt); break;
	case 0x05: acc -= (uint64_t)rs * rt; break;
	case 0x20: SET(RD(op), clz32(rs)); return;
	case 0x21: SET(RD(op), clz32(~rs)); return;
	default: take_exception(s, MIPS32_EXC_RI, 0); return;
	}
	s->lo = (uint32_t)acc;
	s->hi = (uint32_t)(acc >> 32);
}

static void exec_special3(mips32_state *s, uint32_t op)
{
	uint32_t rs = s->r[RS(op)], rt = s->r[RT(op)], m;
	unsigned lsb = SA(op), msb = RD(op);

	switch (FUNCT(op)) {
	case 0x00: SET(RT(op), (rs >> lsb) & mask32(msb + 1)); break;
	case 0x04:
		m = mask32(msb - lsb + 1) << lsb;
		SET(RT(op), (rt & ~m) | ((rs << lsb) & m));
		break;
	case 0x20:
		switch (SA(op)) {
		case 0x02: SET(RD(op), ((rt & 0x00FF00FFu) << 8) | ((rt >> 8) & 0x00FF00FFu)); break;
		case 0x10: SET(RD(op), (uint32_t)(int32_t)(int8_t)rt); break;
		case 0x18: SET(RD(op), (uint32_t)(int32_t)(int16_t)rt); break;
		default: take_exception(s, MIPS32_EXC_RI, 0); break;
		}
		break;
	case 0x3B:
		if (USER(s) && !(s->status & ST_CU0) && !(s->hwrena & (1u << RD(op)))) { take_exception(s, MIPS32_EXC_RI, 0); break; }
		switch (RD(op)) {
		case 0: SET(RT(op), s->ebase & 0x3FFu); break;
		case 1: SET(RT(op), 0); break;
		case 2: SET(RT(op), s->count); break;
		case 3: SET(RT(op), 2); break;
		default: take_exception(s, MIPS32_EXC_RI, 0); break;
		}
		break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void exec_mem(mips32_state *s, uint32_t op)
{
	uint32_t ea = s->r[RS(op)] + SIMM(op), rt = s->r[RT(op)], v;
	unsigned b = ea & 3, i, sh;

	switch (op >> 26) {
	case 0x20: if (load(s, ea, 1, &v)) SET(RT(op), (uint32_t)(int32_t)(int8_t)v); break;
	case 0x21: if (load(s, ea, 2, &v)) SET(RT(op), (uint32_t)(int32_t)(int16_t)v); break;
	case 0x22:
		if (load(s, ea & ~3u, 4, &v)) {
			sh = (3 - b) * 8;
			SET(RT(op), sh ? (rt & ((1u << sh) - 1)) | (v << sh) : v);
		}
		break;
	case 0x23: if (load(s, ea, 4, &v)) SET(RT(op), v); break;
	case 0x24: if (load(s, ea, 1, &v)) SET(RT(op), v & 0xFFu); break;
	case 0x25: if (load(s, ea, 2, &v)) SET(RT(op), v & 0xFFFFu); break;
	case 0x26:
		if (load(s, ea & ~3u, 4, &v)) {
			sh = b * 8;
			SET(RT(op), sh ? (rt & ~(0xFFFFFFFFu >> sh)) | (v >> sh) : v);
		}
		break;
	case 0x28: store(s, ea, rt & 0xFFu, 1); break;
	case 0x29: store(s, ea, rt & 0xFFFFu, 2); break;
	case 0x2A:
		for (i = 0; i <= b; i++)
			if (!store(s, (ea & ~3u) + i, (rt >> (8 * (3 - b + i))) & 0xFFu, 1)) break;
		break;
	case 0x2B: store(s, ea, rt, 4); break;
	case 0x2E:
		for (i = 0; i < 4 - b; i++)
			if (!store(s, ea + i, (rt >> (8 * i)) & 0xFFu, 1)) break;
		break;
	case 0x30: if (load(s, ea, 4, &v)) { SET(RT(op), v); s->llbit = 1; } break;
	case 0x38:
		if (s->llbit) { if (store(s, ea, rt, 4)) SET(RT(op), 1); }
		else SET(RT(op), 0);
		break;
	}
}

static void execute(mips32_state *s, uint32_t op)
{
	uint32_t rs = s->r[RS(op)], rt = s->r[RT(op)], res;

	switch (op >> 26) {
	case 0x00: exec_special(s, op); break;
	case 0x01: exec_regimm(s, op); break;
	case 0x02: jump(s, ((s->cur_pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2)); break;
	case 0x03: SET(31, s->cur_pc + 8); jump(s, ((s->cur_pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2)); break;
	case 0x04: branch(s, rs == rt, op); break;
	case 0x05: branch(s, rs != rt, op); break;
	case 0x06: branch(s, (int32_t)rs <= 0, op); break;
	case 0x07: branch(s, (int32_t)rs > 0, op); break;
	case 0x08:
		res = rs + SIMM(op);
		if (~(rs ^ SIMM(op)) & (rs ^ res) & 0x80000000u) take_exception(s, MIPS32_EXC_OV, 0);
		else SET(RT(op), res);
		break;
	case 0x09: SET(RT(op), rs + SIMM(op)); break;
	case 0x0A: SET(RT(op), (int32_t)rs < (int32_t)SIMM(op)); break;
	case 0x0B: SET(RT(op), rs < SIMM(op)); break;
	case 0x0C: SET(RT(op), rs & UIMM(op)); break;
	case 0x0D: SET(RT(op), rs | UIMM(op)); break;
	case 0x0E: SET(RT(op), rs ^ UIMM(op)); break;
	case 0x0F: SET(RT(op), UIMM(op) << 16); break;
	case 0x10: exec_cop0(s, op); break;
	case 0x11: case 0x13: case 0x31: case 0x35: case 0x39: case 0x3D:
		take_exception(s, MIPS32_EXC_CPU, 1); break;
	case 0x12: case 0x32: case 0x36: case 0x3A: case 0x3E:
		take_exception(s, MIPS32_EXC_CPU, 2); break;
	case 0x14: branch_likely(s, rs == rt, op); break;
	case 0x15: branch_likely(s, rs != rt, op); break;
	case 0x16: branch_likely(s, (int32_t)rs <= 0, op); break;
	case 0x17: branch_likely(s, (int32_t)rs > 0, op); break;
	case 0x1C: exec_special2(s, op); break;
	case 0x1F: exec_special3(s, op); break;
	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
	case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2E: case 0x30: case 0x38:
		exec_mem(s, op); break;
	case 0x2F:
		if (USER(s) && !(s->status & ST_CU0)) take_exception(s, MIPS32_EXC_CPU, 0);
		break;
	case 0x33: break;
	default: take_exception(s, MIPS32_EXC_RI, 0); break;
	}
}

static void step(mips32_state *s)
{
	uint32_t pa, op;
	int err = 0;

	s->cur_pc = s->pc;
	s->cur_delay = s->delay;
	s->skip_pc = s->npc;
	s->cycles++;
	if ((s->pc & 3) || !mips32_translate(s, s->pc, &pa)) {
		s->badvaddr = s->pc;
		take_exception(s, MIPS32_EXC_ADEL, 0);
		return;
	}
	op = s->bus.read(s->bus.ctx, pa, 4, 1, &err);
	if (err) { take_exception(s, MIPS32_EXC_IBE, 0); return; }
	s->pc = s->npc;
	s->npc += 4;
	s->delay = 0;
	execute(s, op);
}

static void tick(mips32_state *s, uint64_t n)
{
	uint64_t t = n + (uint64_t)s->count_half;
	uint32_t inc = (uint32_t)(t >> 1), old;

	s->count_half = (int)(t & 1);
	if (!inc) return;
	old = s->count;
	s->count += inc;
	if (s->compare - old - 1 < inc && !(s->cause & CA_TI)) {
		s->cause |= CA_TI;
		s->stop = 1;
	}
}

static int irq_pending(const mips32_state *s)
{
	return s->eic_ripl > (int)ST_IPL(s->status) && (s->status & (ST_IE | ST_EXL | ST_ERL)) == ST_IE;
}

int mips32_run(mips32_state *s, int cycles)
{
	uint64_t start = s->cycles, end = start + (uint64_t)(cycles > 0 ? cycles : 0);

	s->stop = 0;
	while (s->cycles < end && !s->stop) {
		uint64_t c0 = s->cycles;
		if (irq_pending(s)) {
			s->cur_pc = s->pc;
			s->cur_delay = s->delay;
			s->skip_pc = s->pc;
			take_exception(s, MIPS32_EXC_INT, 0);
			if (s->stop) break;
		}
		if (s->waiting) {
			uint64_t burn = end - s->cycles;
			if (!(s->cause & CA_TI)) {
				uint64_t need = 2 * (uint64_t)(uint32_t)(s->compare - s->count);
				need = need > (uint64_t)s->count_half ? need - (uint64_t)s->count_half : 0;
				if (need && need < burn) burn = need;
			}
			s->cycles += burn;
		} else
			step(s);
		tick(s, s->cycles - c0);
	}
	return (int)(s->cycles - start);
}

void mips32_set_eic(mips32_state *s, int ripl, int vector, int srs)
{
	s->eic_ripl = ripl;
	s->eic_vector = vector;
	s->eic_srs = srs;
}

int mips32_timer_irq(const mips32_state *s) { return (s->cause & CA_TI) != 0; }
int mips32_soft_irq(const mips32_state *s) { return (int)((s->cause >> 8) & 3); }

void mips32_reset(mips32_state *s)
{
	s->r = s->gpr[0];
	s->pc = 0xBFC00000u;
	s->npc = s->pc + 4;
	s->delay = 0;
	s->status = ST_BEV | ST_ERL;
	s->cause = 0;
	s->srsctl = 0;
	s->srsmap = 0;
	s->intctl = 0;
	s->hwrena = 0;
	s->ebase = 0x80000000u;
	s->config0 = 0x80000582u;
	s->llbit = 0;
	s->waiting = 0;
	s->count_half = 0;
	s->eic_ripl = 0;
	s->eic_vector = 0;
	s->eic_srs = 0;
}

void mips32_init(mips32_state *s, const mips32_bus *bus, int shadow_sets, uint32_t prid)
{
	memset(s, 0, sizeof(*s));
	s->bus = *bus;
	s->shadow_sets = shadow_sets < 1 ? 1 : shadow_sets > 8 ? 8 : shadow_sets;
	s->prid = prid;
	mips32_reset(s);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `tests/pinheck/mips32/check.sh`
Expected: last line `mips32: 5 passed, 0 failed`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/cpu/mips32/mips32.c tests/pinheck/mips32/diff
git commit -m "mips32: MIPS32r2 interpreter core with directed QEMU diff tests"
```

### Task 3: Randomised differential testing

**Files:**
- Create: `tests/pinheck/mips32/gen.py`, `tests/pinheck/mips32/cmpdump.py`

**Interfaces:**
- Consumes: `build/mips32run` and `check.sh` from Task 1 (`check.sh` runs the generator when `gen.py` exists).
- Produces: `python3 gen.py --out DIR [--count N] [--first SEED] [--length L]` writes `rNNNNN.S`; a seed always produces the same program. Output layout (392 bytes): 256 scratch bytes, `$0..$31`, `HI`, `LO`.

Generator constraints, each for a stated reason:
- `$23` is the scratch base and is never a destination. `$1` is the divide temporary.
- Divisors are `(rt >> 1) | 1`, never 0 or −1 (both UNPREDICTABLE).
- No trapping `ADD`/`ADDI`/`SUB` (QEMU user mode would die with SIGFPE; Task 4 covers `Ov`).
- `LL`/`SC` only as adjacent pairs (QEMU implements `SC` by value compare, the M4K by LLbit; they agree only then).
- Link branches never use `$31` as the condition register.
- Delay slots only hold non-branch instructions.

- [ ] **Step 1: Write the generator and the dump comparator**

`tests/pinheck/mips32/gen.py`:

```python
#!/usr/bin/env python3
import argparse
import os
import random

BASE = 23
TEMP = 1
DEST = [r for r in range(32) if r != BASE]
DEST_NZ = [r for r in DEST if r not in (0, TEMP)]
SRC = list(range(32))

R3 = ['addu', 'subu', 'and', 'or', 'xor', 'nor', 'slt', 'sltu', 'movn', 'movz', 'mul']
RV = ['sllv', 'srlv', 'srav', 'rotrv']
R2S = ['clz', 'clo']
R2T = ['seb', 'seh', 'wsbh']
SH = ['sll', 'srl', 'sra', 'rotr']
IS = ['addiu', 'slti', 'sltiu']
IU = ['andi', 'ori', 'xori']
MD = ['mult', 'multu', 'madd', 'maddu', 'msub', 'msubu']
BR2 = ['beq', 'bne', 'beql', 'bnel']
BR1 = ['blez', 'bgtz', 'bltz', 'bgez', 'blezl', 'bgtzl', 'bltzl', 'bgezl']
BRAL = ['bltzal', 'bgezal', 'bltzall', 'bgezall']


def reg(n):
    return '$%d' % n


class Gen:
    def __init__(self, rnd):
        self.r = rnd
        self.label = 0
        self.out = []

    def emit(self, s):
        self.out.append('\t' + s)

    def d(self):
        return reg(self.r.choice(DEST))

    def s(self):
        return reg(self.r.choice(SRC))

    def simple(self):
        r = self.r
        k = r.randrange(9)
        if k == 0:
            return '%s %s, %s, %s' % (r.choice(R3), self.d(), self.s(), self.s())
        if k == 1:
            return '%s %s, %s, %s' % (r.choice(RV), self.d(), self.s(), self.s())
        if k == 2:
            return '%s %s, %s' % (r.choice(R2S), self.d(), self.s())
        if k == 3:
            return '%s %s, %s' % (r.choice(R2T), self.d(), self.s())
        if k == 4:
            return '%s %s, %s, %d' % (r.choice(SH), self.d(), self.s(), r.randrange(32))
        if k == 5:
            return '%s %s, %s, %d' % (r.choice(IS), self.d(), self.s(), r.randrange(-32768, 32768))
        if k == 6:
            return '%s %s, %s, 0x%x' % (r.choice(IU), self.d(), self.s(), r.randrange(65536))
        if k == 7:
            pos = r.randrange(32)
            size = r.randrange(1, 33 - pos)
            return '%s %s, %s, %d, %d' % (r.choice(['ext', 'ins']), self.d(), self.s(), pos, size)
        return 'lui %s, 0x%x' % (self.d(), r.randrange(65536))

    def hilo(self):
        r = self.r
        k = r.randrange(3)
        if k == 0:
            self.emit('%s %s, %s' % (r.choice(MD), self.s(), self.s()))
        elif k == 1:
            self.emit('%s %s' % (r.choice(['mfhi', 'mflo']), self.d()))
        else:
            self.emit('%s %s' % (r.choice(['mthi', 'mtlo']), self.s()))

    def div(self):
        rt = self.s()
        self.emit('srl %s, %s, 1' % (reg(TEMP), rt))
        self.emit('ori %s, %s, 1' % (reg(TEMP), reg(TEMP)))
        self.emit('%s $0, %s, %s' % (self.r.choice(['div', 'divu']), self.s(), reg(TEMP)))

    def mem(self):
        r = self.r
        k = r.randrange(5)
        if k == 0:
            op = r.choice(['lw', 'sw'])
            off = r.randrange(0, 256, 4)
        elif k == 1:
            op = r.choice(['lh', 'lhu', 'sh'])
            off = r.randrange(0, 256, 2)
        elif k == 2:
            op = r.choice(['lb', 'lbu', 'sb'])
            off = r.randrange(256)
        elif k == 3:
            op = r.choice(['lwl', 'lwr', 'swl', 'swr'])
            off = r.randrange(256)
        else:
            off = r.randrange(0, 256, 4)
            self.emit('ll %s, %d(%s)' % (self.d(), off, reg(BASE)))
            self.emit('sc %s, %d(%s)' % (self.d(), off, reg(BASE)))
            return
        rt = self.s() if op.startswith('s') else self.d()
        self.emit('%s %s, %d(%s)' % (op, rt, off, reg(BASE)))

    def block(self, head):
        lab = 'L%d' % self.label
        self.label += 1
        for h in head(lab):
            self.emit(h)
        self.emit(self.simple())
        for _ in range(self.r.randrange(4)):
            self.emit(self.simple())
        self.out.append(lab + ':')

    def branch(self):
        r = self.r
        k = r.randrange(6)
        if k == 0:
            self.block(lambda l: ['%s %s, %s, %s' % (r.choice(BR2), self.s(), self.s(), l)])
        elif k == 1:
            self.block(lambda l: ['%s %s, %s' % (r.choice(BR1), self.s(), l)])
        elif k == 2:
            src = reg(r.choice([x for x in SRC if x != 31]))
            self.block(lambda l: ['%s %s, %s' % (r.choice(BRAL), src, l)])
        elif k == 3:
            self.block(lambda l: ['%s %s' % (r.choice(['j', 'jal']), l)])
        elif k == 4:
            rd = reg(r.choice(DEST_NZ))
            self.block(lambda l: ['la %s, %s' % (reg(TEMP), l), 'jalr %s, %s' % (rd, reg(TEMP))])
        else:
            self.block(lambda l: ['la %s, %s' % (reg(TEMP), l), 'jr %s' % reg(TEMP)])

    def program(self, count):
        r = self.r
        self.out += ['#include "common.h"', '\t.text', '\t.globl __start', '__start:']
        self.emit('la %s, scratch' % reg(BASE))
        for n in (1, 2):
            v = r.getrandbits(32)
            self.emit('lui $1, 0x%x' % (v >> 16))
            self.emit('ori $1, $1, 0x%x' % (v & 0xFFFF))
            self.emit('mthi $1' if n == 1 else 'mtlo $1')
        for n in range(1, 32):
            if n == BASE:
                continue
            v = r.getrandbits(32)
            self.emit('lui %s, 0x%x' % (reg(n), v >> 16))
            self.emit('ori %s, %s, 0x%x' % (reg(n), reg(n), v & 0xFFFF))
        for _ in range(count):
            k = r.randrange(10)
            if k < 4:
                self.emit(self.simple())
            elif k < 6:
                self.mem()
            elif k < 7:
                self.hilo()
            elif k < 8:
                self.div()
            else:
                self.branch()
        for n in range(32):
            self.emit('sw %s, %d(%s)' % (reg(n), 256 + 4 * n, reg(BASE)))
        self.emit('mfhi $1')
        self.emit('sw $1, 384(%s)' % reg(BASE))
        self.emit('mflo $1')
        self.emit('sw $1, 388(%s)' % reg(BASE))
        self.emit('WRITE_EXIT(scratch, 392)')
        self.out += ['\t.data', '\t.align 4', 'scratch:']
        data = [r.randrange(256) for _ in range(256)]
        for i in range(0, 256, 16):
            self.emit('.byte ' + ', '.join(str(b) for b in data[i:i + 16]))
        self.emit('.space 136')
        return '\n'.join(self.out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--count', type=int, default=200)
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--length', type=int, default=300)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for seed in range(a.first, a.first + a.count):
        with open(os.path.join(a.out, 'r%05d.S' % seed), 'w') as f:
            f.write(Gen(random.Random(seed)).program(a.length))


if __name__ == '__main__':
    main()
```

`tests/pinheck/mips32/cmpdump.py`:

```python
#!/usr/bin/env python3
import struct
import sys

NAMES = ['scratch+%d' % (4 * i) for i in range(64)] + ['$%d' % i for i in range(32)] + ['hi', 'lo']


def words(path):
    d = open(path, 'rb').read()
    return struct.unpack('<%dI' % (len(d) // 4), d[:len(d) // 4 * 4])


a, b = words(sys.argv[1]), words(sys.argv[2])
if len(a) != len(b):
    print('  length differs: qemu %d words, ours %d words' % (len(a), len(b)))
for i, (x, y) in enumerate(zip(a, b)):
    if x != y:
        print('  %-12s qemu %08x  ours %08x' % (NAMES[i] if i < len(NAMES) else 'word %d' % i, x, y))
```

Make them executable: `chmod +x tests/pinheck/mips32/gen.py tests/pinheck/mips32/cmpdump.py`

- [ ] **Step 2: Run the suite**

Run: `tests/pinheck/mips32/check.sh`
Expected: `mips32: 205 passed, 0 failed` (5 directed + 200 random), about 15–20 s.

If any `MISMATCH` appears, `cmpdump.py` prints lines like `  $12          qemu 00000019  ours 00000018`. Reproduce a single seed with `python3 gen.py --out /tmp/one --first SEED --count 1`.

- [ ] **Step 3: Commit**

```bash
git add tests/pinheck/mips32/gen.py tests/pinheck/mips32/cmpdump.py
git commit -m "mips32: seeded random differential tests against QEMU 4KEc"
```

### Task 4: Privileged architecture: golden and unit tests

**Files:**
- Create: `tests/pinheck/mips32/golden/exceptions.S`, `exceptions.expect`, `cp0.S`, `cp0.expect`, `eic.S`, `eic.expect`
- Create: `tests/pinheck/mips32/unit_test.c`

**Interfaces:**
- Consumes: the core (Task 2), the runner's `-x -e` mode and its test registers `0xA0FFFFF0` (EIC: `ripl | vector << 8 | srs << 16`) and `0xA0FFFFF4` (clear latched timer interrupt; the runner raises RIPL 1 vector 0 set 0 while latched).
- Produces: regression coverage for §4.1's privileged behaviour; no new API.

How each expectation was derived:
- **`exceptions`**: each triple is `Cause`, `EPC − trigger address` and `BadVAddr`. `Cause` = `ExcCode << 2`, plus bit 31 for BD and `CE << 28` for CpU. `BadVAddr` is only written by address errors, so it keeps its last value (`0x80500002`) through the later tests. `0x00005555` shows that the overflowing `add` did not write its destination. The final word is `Status` after the last `eret`.
- **`cp0`**: reset/read-only values and writable masks from the Global Constraints: `Status` mask `0x1A48FF17`, `IntCtl` `0x3E0`, `SRSCtl` `0xF3C0` plus HSS=1 → `0x0400F3C0`, `Cause` `0x08C00300`, `HWREna` `0xF`, `EBase` `0x3FFFF000` plus fixed bit 31, `Config` K0. `Count`: written as 100 and read back immediately (100); three instructions later it reads 102, because it advances one per two cycles. `RDHWR $3` (CCRes) is 2. `ei`/`di` return the previous `Status`. `WRPGPR`/`RDPGPR` go through `PSS=1` without touching the current set's `$t0` (`0x77`).
- **`eic`**: RIPL 3 is held off by IPL 5, then taken when IPL drops to 2. In the handler, shadow set 1's `$t0` holds `0x1111`, `Cause = IV | RIPL<<10` = `0x00800C00`, and `SRSCtl = HSS | EICSS 1 | PSS 0 | CSS 1` = `0x04040001`. `RDPGPR` reads set 0's `$t1` = `0x2222`. After `eret` the current set is back to 0 (`0x04000000`). `WAIT` sleeps until `Count` reaches `Compare` = 20; the timer interrupt's `Cause` = `TI | IV | RIPL 1` = `0x40800400`.

- [ ] **Step 1: Write the golden tests**

`tests/pinheck/mips32/golden/exceptions.S`:

```asm
#include "common.h"

#define ARM(res, trig) \
	la $8, res; la $9, resume; sw $8, 0($9); la $8, trig; la $9, trigger; sw $8, 0($9)

	.section .exc, "ax"
	mfc0 $26, $13
	sw $26, 0($22)
	mfc0 $26, $14
	la $27, trigger
	lw $27, 0($27)
	subu $26, $26, $27
	sw $26, 4($22)
	mfc0 $26, $8
	sw $26, 8($22)
	addiu $22, $22, 12
	la $27, resume
	lw $27, 0($27)
	mtc0 $27, $14
	ehb
	eret

	.text
	.globl __start
__start:
	mtc0 $0, $12
	ehb
	la $22, out
	li $2, 0
	ARM(1f, 2f)
2:	syscall
1:	ARM(1f, 2f)
2:	break
1:	ARM(1f, 2f)
2:	teq $0, $0
1:	li $10, 0x7fffffff
	li $11, 1
	li $12, 0x5555
	ARM(1f, 2f)
2:	add $12, $10, $11
1:	sw $12, 0($22)
	addiu $22, $22, 4
	ARM(1f, 2f)
2:	.word 0x0000003f
1:	li $10, 0x80500001
	ARM(1f, 2f)
2:	lw $11, 0($10)
1:	li $10, 0x80500002
	ARM(1f, 2f)
2:	sw $11, 0($10)
1:	ARM(1f, 2f)
2:	.word 0x44000000
1:	li $10, 0xa1000000
	ARM(1f, 2f)
2:	lw $11, 0($10)
1:	ARM(1f, 2f)
2:	beq $0, $0, 1f
	break
	nop
1:	li $10, 0x80400003
	ARM(1f, 3f)
	la $9, trigger
	sw $10, 0($9)
	jr $10
	nop
3:	nop
1:	mfc0 $12, $12
	sw $12, 0($22)
	addiu $22, $22, 4
	WRITE_EXIT(out, 140)

	.data
	.align 4
resume:	.word 0
trigger: .word 0
out:	.space 256
```

`tests/pinheck/mips32/golden/exceptions.expect`:

```text
00000020
00000000
00000000
00000024
00000000
00000000
00000034
00000000
00000000
00000030
00000000
00000000
00005555
00000028
00000000
00000000
00000010
00000000
80500001
00000014
00000000
80500002
1000002c
00000000
80500002
0000001c
00000000
80500002
80000024
00000000
80500002
00000010
00000000
80400003
00000000
```

`tests/pinheck/mips32/golden/cp0.S`:

```asm
#include "common.h"
	.section .exc, "ax"
	li $2, SYS_EXIT
	li $4, 1
	syscall

	.text
	.globl __start
__start:
	la $23, out
	mfc0 $2, $12, 0
	ST($2)
	mfc0 $2, $15, 0
	ST($2)
	mfc0 $2, $15, 1
	ST($2)
	mfc0 $2, $16, 0
	ST($2)
	mfc0 $2, $16, 1
	ST($2)
	mfc0 $2, $16, 2
	ST($2)
	mfc0 $2, $16, 3
	ST($2)
	mfc0 $2, $12, 2
	ST($2)
	li $9, -1
	mtc0 $9, $12, 1
	mfc0 $2, $12, 1
	ST($2)
	mtc0 $0, $12, 1
	mtc0 $9, $12, 2
	mfc0 $2, $12, 2
	ST($2)
	mtc0 $0, $12, 2
	mtc0 $9, $13, 0
	mfc0 $2, $13, 0
	ST($2)
	mtc0 $0, $13, 0
	mtc0 $9, $7, 0
	mfc0 $2, $7, 0
	ST($2)
	mtc0 $9, $15, 1
	mfc0 $2, $15, 1
	ST($2)
	li $10, 0x80000000
	mtc0 $10, $15, 1
	mtc0 $9, $16, 0
	mfc0 $2, $16, 0
	ST($2)
	mtc0 $9, $12, 0
	mfc0 $2, $12, 0
	ST($2)
	mtc0 $0, $12, 0
	ehb
	li $10, 100
	mtc0 $10, $9
	mfc0 $2, $9
	nop
	nop
	nop
	mfc0 $3, $9
	ST($2)
	ST($3)
	rdhwr $2, $3
	ST($2)
	ei $2
	ST($2)
	mfc0 $2, $12
	ST($2)
	di $2
	ST($2)
	mfc0 $2, $12
	ST($2)
	li $8, 0x77
	li $10, 0x40
	mtc0 $10, $12, 2
	ehb
	li $11, 0x1234
	WRPGPR(8, 11)
	RDPGPR(2, 8)
	ST($2)
	ST($8)
	mtc0 $0, $12, 2
	WRITE_EXIT(out, 96)

	.data
	.align 4
out:	.space 256
```

`tests/pinheck/mips32/golden/cp0.expect`:

```text
00400004
00018700
80000000
80000582
80000000
80000000
00000060
04000000
000003e0
0400f3c0
08c00300
0000000f
bffff000
80000587
1a48ff17
00000064
00000066
00000002
00000000
00000001
00000001
00000000
00001234
00000077
```

`tests/pinheck/mips32/golden/eic.S`:

```asm
#include "common.h"

#define LOG(r) la $24, logptr; lw $25, 0($24); sw r, 0($25); addiu $25, $25, 4; sw $25, 0($24)

	.section .exc, "ax"
	li $2, SYS_EXIT
	li $4, 1
	syscall
	.org 0x80
	j isr0
	nop
	.org 0xe0
	j isr3
	nop

	.text
isr3:
	la $26, logptr
	lw $27, 0($26)
	sw $8, 0($27)
	mfc0 $8, $13
	sw $8, 4($27)
	mfc0 $8, $12, 2
	sw $8, 8($27)
	RDPGPR(8, 9)
	sw $8, 12($27)
	addiu $27, $27, 16
	sw $27, 0($26)
	li $8, MMIO_EIC
	sw $0, 0($8)
	eret

isr0:
	mfc0 $26, $13
	la $27, isr0cause
	sw $26, 0($27)
	mtc0 $0, $11
	li $27, MMIO_CT
	sw $0, 0($27)
	eret

	.globl __start
__start:
	mtc0 $0, $12
	ehb
	li $8, 0x20
	mtc0 $8, $12, 1
	li $8, 0x00800000
	mtc0 $8, $13
	li $8, 0x40
	mtc0 $8, $12, 2
	ehb
	li $9, 0x1111
	WRPGPR(8, 9)
	mtc0 $0, $12, 2
	ehb
	li $9, 0x2222
	la $8, logptr
	la $10, out
	sw $10, 0($8)
	li $8, 0x1401
	mtc0 $8, $12
	ehb
	li $10, MMIO_EIC
	li $11, 0x10303
	sw $11, 0($10)
	nop
	nop
	li $12, 0xaaaa0001
	LOG($12)
	li $8, 0x0801
	mtc0 $8, $12
	ehb
	nop
	li $12, 0xaaaa0002
	LOG($12)
	mfc0 $12, $12, 2
	LOG($12)
	mtc0 $0, $12
	mtc0 $0, $9
	li $8, 20
	mtc0 $8, $11
	li $8, 1
	mtc0 $8, $12
	wait
	li $12, 0xaaaa0003
	LOG($12)
	la $8, isr0cause
	lw $12, 0($8)
	LOG($12)
	mtc0 $0, $12
	WRITE_EXIT(out, 36)

	.data
	.align 4
logptr:	.word 0
isr0cause: .word 0
out:	.space 256
```

`tests/pinheck/mips32/golden/eic.expect`:

```text
aaaa0001
00001111
00800c00
04040001
00002222
aaaa0002
04000000
aaaa0003
40800400
```

- [ ] **Step 2: Write the unit tests (Review Focus items 1–4)**

`run(1)` executes one instruction. When an interrupt is taken, the handler's first instruction also executes in that same call, which is why the handler here is a `nop` before `eret`.

`tests/pinheck/mips32/unit_test.c`:

```c
#include "mips32.h"
#include <stdio.h>
#include <string.h>

static uint8_t kmem[0x10000], umem[0x10000];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("UNIT FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t *map(uint32_t pa, int size)
{
	if (pa < sizeof(kmem) - (unsigned)size) return kmem + pa;
	if (pa >= 0x40000000u && pa - 0x40000000u < sizeof(umem) - (unsigned)size) return umem + (pa - 0x40000000u);
	return NULL;
}

static uint32_t rd(void *ctx, uint32_t pa, int size, int fetch, int *err)
{
	uint8_t *p = map(pa, size);
	uint32_t v = 0;
	int i;
	(void)ctx; (void)fetch;
	if (!p) { *err = 1; return 0; }
	for (i = 0; i < size; i++) v |= (uint32_t)p[i] << (8 * i);
	return v;
}

static void wr(void *ctx, uint32_t pa, uint32_t v, int size, int *err)
{
	uint8_t *p = map(pa, size);
	int i;
	(void)ctx;
	if (!p) { *err = 1; return; }
	for (i = 0; i < size; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put(uint8_t *m, uint32_t off, uint32_t w)
{
	m[off] = (uint8_t)w; m[off + 1] = (uint8_t)(w >> 8); m[off + 2] = (uint8_t)(w >> 16); m[off + 3] = (uint8_t)(w >> 24);
}

static void setup(mips32_state *s, uint32_t pc, uint32_t status)
{
	mips32_bus bus = { NULL, rd, wr, NULL, NULL };
	memset(kmem, 0, sizeof(kmem));
	memset(umem, 0, sizeof(umem));
	mips32_init(s, &bus, 2, 0x00018700u);
	s->status = status;
	s->pc = pc;
	s->npc = pc + 4;
}

static void irq_in_delay_slot(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0x00000001u);
	put(kmem, 0x1000, 0x10000002u);
	put(kmem, 0x1004, 0x25080001u);
	put(kmem, 0x1008, 0x25080064u);
	put(kmem, 0x100C, 0x00000000u);
	put(kmem, 0x0184, 0x42000018u);
	mips32_run(&s, 1);
	mips32_set_eic(&s, 1, 0, 0);
	mips32_run(&s, 1);
	CHECK(s.epc == 0x80001000u);
	CHECK(s.cause & 0x80000000u);
	CHECK(s.pc == 0x80000184u);
	mips32_set_eic(&s, 0, 0, 0);
	mips32_run(&s, 1);
	CHECK(s.pc == 0x80001000u);
	mips32_run(&s, 3);
	CHECK(s.r[8] == 1);
	CHECK(s.pc == 0x80001010u);
}

static void exception_with_exl_set(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0x00000002u);
	s.epc = 0x11111111u;
	s.srsctl = 0x1000u;
	put(kmem, 0x1000, 0x10000001u);
	put(kmem, 0x1004, 0x0000000Du);
	mips32_run(&s, 2);
	CHECK(s.epc == 0x11111111u);
	CHECK(!(s.cause & 0x80000000u));
	CHECK(((s.cause >> 2) & 31) == MIPS32_EXC_BP);
	CHECK(s.pc == 0x80000180u);
	CHECK((s.srsctl & 15) == 0);
}

static void compare_crossed_by_div(void)
{
	mips32_state s;
	setup(&s, 0x80001000u, 0);
	s.compare = 10;
	s.r[9] = 3;
	put(kmem, 0x1000, 0x0109001Au);
	mips32_run(&s, 1);
	CHECK(s.count == 17);
	CHECK(mips32_timer_irq(&s));

	setup(&s, 0x80001000u, 0);
	s.count = 100;
	s.compare = 100;
	mips32_run(&s, 2);
	CHECK(s.count == 101);
	CHECK(!mips32_timer_irq(&s));
}

static void user_mode(void)
{
	mips32_state s;
	uint32_t pa = 0;

	setup(&s, 0x00001000u, 0x00000010u);
	CHECK(mips32_translate(&s, 0x00001000u, &pa) && pa == 0x40001000u);
	CHECK(!mips32_translate(&s, 0x80000000u, &pa));
	s.r[9] = 0x80000000u;
	put(umem, 0x1000, 0x8D280000u);
	mips32_run(&s, 1);
	CHECK(((s.cause >> 2) & 31) == MIPS32_EXC_ADEL);
	CHECK(s.badvaddr == 0x80000000u);
	CHECK(s.epc == 0x00001000u);
	CHECK(s.pc == 0x80000180u);

	setup(&s, 0x00001000u, 0x00000010u);
	s.r[9] = 0xA0000000u;
	put(umem, 0x1000, 0xAD280000u);
	mips32_run(&s, 1);
	CHECK(((s.cause >> 2) & 31) == MIPS32_EXC_ADES);

	setup(&s, 0x00001000u, 0x00000010u);
	put(umem, 0x1000, 0x40086000u);
	mips32_run(&s, 1);
	CHECK(((s.cause >> 2) & 31) == MIPS32_EXC_CPU);
	CHECK(((s.cause >> 28) & 3) == 0);

	setup(&s, 0x00001000u, 0x00000014u);
	CHECK(mips32_translate(&s, 0x00001000u, &pa) && pa == 0x00001000u);
}

int main(void)
{
	irq_in_delay_slot();
	exception_with_exl_set();
	compare_crossed_by_div();
	user_mode();
	printf("unit: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

- [ ] **Step 3: Run the suite**

Run: `tests/pinheck/mips32/check.sh`
Expected: `unit: ok` and `mips32: 208 passed, 0 failed`.

- [ ] **Step 4: Prove the new tests can fail**

For each mutation, apply it to `src/cpu/mips32/mips32.c`, run `tests/pinheck/mips32/check.sh`, confirm the named failure, then restore with `git checkout src/cpu/mips32/mips32.c`:

| Mutation (replace → with) | Must fail |
|---|---|
| `s->epc = s->cur_delay ? s->cur_pc - 4 : s->cur_pc;` → `s->epc = s->cur_pc;` | `UNIT FAIL ... s.pc == 0x80001000u` |
| `if (s->compare - old - 1 < inc && ` → `if (s->count == s->compare && ` | `UNIT FAIL ... mips32_timer_irq(&s)` |
| `if (!(s->status & ST_EXL)) {` → `if (1) {` | `UNIT FAIL ... s.epc == 0x11111111u` |
| `s->srsctl = (s->srsctl & ~15u) \| pss;` and the next line → `(void)pss;` | `GOLDEN FAIL golden/eic.S` |

Then `git status --short src/` must print nothing.

- [ ] **Step 5: Commit**

```bash
git add tests/pinheck/mips32/golden tests/pinheck/mips32/unit_test.c
git commit -m "mips32: golden CP0/exception/EIC tests and cycle-level unit tests"
```

### Task 5: Disassembler

**Files:**
- Create: `tests/pinheck/mips32/dasm_test.c`
- Create: `src/cpu/mips32/mips32dasm.c`

**Interfaces:**
- Consumes: `mips32_dasm` declaration from Task 1.
- Produces: `unsigned mips32_dasm(char *buf, uint32_t pc, uint32_t op)` (Milestone 2 uses it for PinMAME's debugger). Format: mnemonic left-aligned in 8 columns, then comma-separated operands with no spaces. Registers use ABI names (`$a0`); CP0 registers are `$reg,sel`; arithmetic immediates and memory offsets are signed decimal; logical immediates are hex; branch and jump targets are absolute 8-digit hex. Unknown encodings print `.word   0x%08x`.

Every encoding in the table below came from assembling the mnemonic with clang and reading it back with `llvm-objdump -d`, except `rdpgpr`/`wrpgpr`/`rdhwr`, which LLVM does not accept or print and which were encoded from the MIPS32 ISA manual.

- [ ] **Step 1: Write the failing test**

`tests/pinheck/mips32/dasm_test.c`:

```c
#include "mips32.h"
#include <stdio.h>
#include <string.h>

static const struct { uint32_t pc, op; const char *text; } cases[] = {
	{ 0x9D001000, 0x00000000, "nop" },
	{ 0x9D001004, 0x00851021, "addu    $v0,$a0,$a1" },
	{ 0x9D001008, 0x000940C0, "sll     $t0,$t1,3" },
	{ 0x9D00100C, 0x002941C2, "rotr    $t0,$t1,7" },
	{ 0x9D001010, 0x018B5007, "srav    $t2,$t3,$t4" },
	{ 0x9D001014, 0x018B5046, "rotrv   $t2,$t3,$t4" },
	{ 0x9D001018, 0x03E00008, "jr      $ra" },
	{ 0x9D00101C, 0x0320F809, "jalr    $ra,$t9" },
	{ 0x9D001020, 0x00001810, "mfhi    $v1" },
	{ 0x9D001024, 0x00850018, "mult    $a0,$a1" },
	{ 0x9D001028, 0x00C7001B, "divu    $a2,$a3" },
	{ 0x9D00102C, 0x00000034, "teq     $zero,$zero" },
	{ 0x9D001030, 0x0000000C, "syscall" },
	{ 0x9D001034, 0x27BDFFE0, "addiu   $sp,$sp,-32" },
	{ 0x9D001038, 0x3128FF00, "andi    $t0,$t1,0xff00" },
	{ 0x9D00103C, 0x3C1A9D00, "lui     $k0,0x9d00" },
	{ 0x9D001040, 0x8FA2FFF8, "lw      $v0,-8($sp)" },
	{ 0x9D001044, 0xA3800003, "sb      $zero,3($gp)" },
	{ 0x9D001048, 0x88880003, "lwl     $t0,3($a0)" },
	{ 0x9D00104C, 0xC0890000, "ll      $t1,0($a0)" },
	{ 0x9D001050, 0x10850003, "beq     $a0,$a1,0x9d001060" },
	{ 0x9D001054, 0x5480FFFD, "bnel    $a0,$zero,0x9d00104c" },
	{ 0x9D001058, 0x06110001, "bgezal  $s0,0x9d001060" },
	{ 0x9D00105C, 0x0B40048D, "j       0x9d001234" },
	{ 0x9D001060, 0x40086002, "mfc0    $t0,$12,2" },
	{ 0x9D001064, 0x409A7000, "mtc0    $k0,$14,0" },
	{ 0x9D001068, 0x42000018, "eret" },
	{ 0x9D00106C, 0x41686000, "di      $t0" },
	{ 0x9D001070, 0x41606020, "ei" },
	{ 0x9D001074, 0x415DE800, "rdpgpr  $sp,$sp" },
	{ 0x9D001078, 0x41CA4800, "wrpgpr  $t1,$t2" },
	{ 0x9D00107C, 0x712A4002, "mul     $t0,$t1,$t2" },
	{ 0x9D001080, 0x70850000, "madd    $a0,$a1" },
	{ 0x9D001084, 0x712C6020, "clz     $t4,$t1" },
	{ 0x9D001088, 0x7D093900, "ext     $t1,$t0,4,8" },
	{ 0x9D00108C, 0x7D28DE04, "ins     $t0,$t1,24,4" },
	{ 0x9D001090, 0x7C0850A0, "wsbh    $t2,$t0" },
	{ 0x9D001094, 0x7C085C20, "seb     $t3,$t0" },
	{ 0x9D001098, 0x7C085E20, "seh     $t3,$t0" },
	{ 0x9D00109C, 0x7C03183B, "rdhwr   $v1,$3" },
	{ 0x9D0010A0, 0x000000C0, "ehb" },
	{ 0x9D0010A4, 0xBC940000, "cache   0x14,0($a0)" },
	{ 0x9D0010A8, 0x42000020, "wait" },
	{ 0x9D0010AC, 0x0488FFFB, "tgei    $a0,-5" },
	{ 0x9D0010B0, 0xFC000000, ".word   0xfc000000" }
};

int main(void)
{
	char buf[80];
	unsigned i, bad = 0;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(buf, 0, sizeof(buf));
		if (mips32_dasm(buf, cases[i].pc, cases[i].op) != 4 || strcmp(buf, cases[i].text)) {
			printf("DASM FAIL %08x: got \"%s\", want \"%s\"\n", (unsigned)cases[i].op, buf, cases[i].text);
			bad++;
		}
	}
	printf("dasm: %u/%u\n", i - bad, i);
	return bad != 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `tests/pinheck/mips32/check.sh`
Expected: exit 2, `mips32dasm.c: No such file or directory`.

- [ ] **Step 3: Write the disassembler**

`src/cpu/mips32/mips32dasm.c`:

```c
#include "mips32.h"
#include <stdarg.h>
#include <stdio.h>

static const char *const rn[32] = {
	"$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
	"$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
	"$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
	"$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
};

#define RS(op)    (((op) >> 21) & 31)
#define RT(op)    (((op) >> 16) & 31)
#define RD(op)    (((op) >> 11) & 31)
#define SA(op)    (((op) >> 6) & 31)
#define FUNCT(op) ((op) & 63)
#define SIMM(op)  ((int)(int16_t)((op) & 0xFFFF))
#define UIMM(op)  ((unsigned)((op) & 0xFFFF))
#define BTARGET(pc, op) ((unsigned)((pc) + 4 + ((uint32_t)(int32_t)(int16_t)((op) & 0xFFFF) << 2)))

static void f(char *buf, const char *m, const char *fmt, ...)
{
	va_list ap;
	int n = sprintf(buf, "%-8s", m);
	va_start(ap, fmt);
	vsprintf(buf + n, fmt, ap);
	va_end(ap);
}

static const char *const special_r3[64] = {
	[0x20] = "add", [0x21] = "addu", [0x22] = "sub", [0x23] = "subu", [0x24] = "and", [0x25] = "or",
	[0x26] = "xor", [0x27] = "nor", [0x2A] = "slt", [0x2B] = "sltu", [0x0A] = "movz", [0x0B] = "movn"
};
static const char *const special_trap[8] = { "tge", "tgeu", "tlt", "tltu", "teq", NULL, "tne", NULL };
static const char *const loadstore[64] = {
	[0x20] = "lb", [0x21] = "lh", [0x22] = "lwl", [0x23] = "lw", [0x24] = "lbu", [0x25] = "lhu", [0x26] = "lwr",
	[0x28] = "sb", [0x29] = "sh", [0x2A] = "swl", [0x2B] = "sw", [0x2E] = "swr", [0x30] = "ll", [0x38] = "sc"
};
static const char *const regimm[32] = {
	[0x00] = "bltz", [0x01] = "bgez", [0x02] = "bltzl", [0x03] = "bgezl",
	[0x10] = "bltzal", [0x11] = "bgezal", [0x12] = "bltzall", [0x13] = "bgezall"
};
static const char *const regimm_trap[8] = { "tgei", "tgeiu", "tlti", "tltiu", "teqi", NULL, "tnei", NULL };

static void word(char *buf, uint32_t op) { sprintf(buf, "%-8s0x%08x", ".word", (unsigned)op); }

static void dasm_special(char *buf, uint32_t op)
{
	unsigned fn = FUNCT(op);

	if (special_r3[fn]) { f(buf, special_r3[fn], "%s,%s,%s", rn[RD(op)], rn[RS(op)], rn[RT(op)]); return; }
	if (fn >= 0x30 && fn <= 0x37 && special_trap[fn - 0x30]) { f(buf, special_trap[fn - 0x30], "%s,%s", rn[RS(op)], rn[RT(op)]); return; }
	switch (fn) {
	case 0x00:
		if (op == 0) sprintf(buf, "nop");
		else if (op == 0x40) sprintf(buf, "ssnop");
		else if (op == 0xC0) sprintf(buf, "ehb");
		else f(buf, "sll", "%s,%s,%u", rn[RD(op)], rn[RT(op)], SA(op));
		break;
	case 0x02: f(buf, (op & (1u << 21)) ? "rotr" : "srl", "%s,%s,%u", rn[RD(op)], rn[RT(op)], SA(op)); break;
	case 0x03: f(buf, "sra", "%s,%s,%u", rn[RD(op)], rn[RT(op)], SA(op)); break;
	case 0x04: f(buf, "sllv", "%s,%s,%s", rn[RD(op)], rn[RT(op)], rn[RS(op)]); break;
	case 0x06: f(buf, (op & (1u << 6)) ? "rotrv" : "srlv", "%s,%s,%s", rn[RD(op)], rn[RT(op)], rn[RS(op)]); break;
	case 0x07: f(buf, "srav", "%s,%s,%s", rn[RD(op)], rn[RT(op)], rn[RS(op)]); break;
	case 0x08: f(buf, "jr", "%s", rn[RS(op)]); break;
	case 0x09: f(buf, "jalr", "%s,%s", rn[RD(op)], rn[RS(op)]); break;
	case 0x0C: sprintf(buf, "syscall"); break;
	case 0x0D: sprintf(buf, "break"); break;
	case 0x0F: sprintf(buf, "sync"); break;
	case 0x10: f(buf, "mfhi", "%s", rn[RD(op)]); break;
	case 0x11: f(buf, "mthi", "%s", rn[RS(op)]); break;
	case 0x12: f(buf, "mflo", "%s", rn[RD(op)]); break;
	case 0x13: f(buf, "mtlo", "%s", rn[RS(op)]); break;
	case 0x18: f(buf, "mult", "%s,%s", rn[RS(op)], rn[RT(op)]); break;
	case 0x19: f(buf, "multu", "%s,%s", rn[RS(op)], rn[RT(op)]); break;
	case 0x1A: f(buf, "div", "%s,%s", rn[RS(op)], rn[RT(op)]); break;
	case 0x1B: f(buf, "divu", "%s,%s", rn[RS(op)], rn[RT(op)]); break;
	default: word(buf, op); break;
	}
}

static void dasm_cop0(char *buf, uint32_t op)
{
	if (op & (1u << 25)) {
		if (FUNCT(op) == 0x18) sprintf(buf, "eret");
		else if (FUNCT(op) == 0x20) sprintf(buf, "wait");
		else word(buf, op);
		return;
	}
	switch (RS(op)) {
	case 0x00: f(buf, "mfc0", "%s,$%u,%u", rn[RT(op)], RD(op), (unsigned)(op & 7)); break;
	case 0x04: f(buf, "mtc0", "%s,$%u,%u", rn[RT(op)], RD(op), (unsigned)(op & 7)); break;
	case 0x0A: f(buf, "rdpgpr", "%s,%s", rn[RD(op)], rn[RT(op)]); break;
	case 0x0B:
		if (RT(op)) f(buf, (op & 0x20) ? "ei" : "di", "%s", rn[RT(op)]);
		else sprintf(buf, "%s", (op & 0x20) ? "ei" : "di");
		break;
	case 0x0E: f(buf, "wrpgpr", "%s,%s", rn[RD(op)], rn[RT(op)]); break;
	default: word(buf, op); break;
	}
}

static void dasm_special2(char *buf, uint32_t op)
{
	switch (FUNCT(op)) {
	case 0x00: f(buf, "madd", "%s,%s", rn[RS(op)], rn[RT(op)]); break;
	case 0x01: f(buf, "maddu", "%s,%s", rn[RS(op)], rn[RT(op)]); break;
	case 0x02: f(buf, "mul", "%s,%s,%s", rn[RD(op)], rn[RS(op)], rn[RT(op)]); break;
	case 0x04: f(buf, "msub", "%s,%s", rn[RS(op)], rn[RT(op)]); break;
	case 0x05: f(buf, "msubu", "%s,%s", rn[RS(op)], rn[RT(op)]); break;
	case 0x20: f(buf, "clz", "%s,%s", rn[RD(op)], rn[RS(op)]); break;
	case 0x21: f(buf, "clo", "%s,%s", rn[RD(op)], rn[RS(op)]); break;
	default: word(buf, op); break;
	}
}

static void dasm_special3(char *buf, uint32_t op)
{
	switch (FUNCT(op)) {
	case 0x00: f(buf, "ext", "%s,%s,%u,%u", rn[RT(op)], rn[RS(op)], SA(op), RD(op) + 1); break;
	case 0x04: f(buf, "ins", "%s,%s,%u,%u", rn[RT(op)], rn[RS(op)], SA(op), RD(op) - SA(op) + 1); break;
	case 0x20:
		if (SA(op) == 0x02) f(buf, "wsbh", "%s,%s", rn[RD(op)], rn[RT(op)]);
		else if (SA(op) == 0x10) f(buf, "seb", "%s,%s", rn[RD(op)], rn[RT(op)]);
		else if (SA(op) == 0x18) f(buf, "seh", "%s,%s", rn[RD(op)], rn[RT(op)]);
		else word(buf, op);
		break;
	case 0x3B: f(buf, "rdhwr", "%s,$%u", rn[RT(op)], RD(op)); break;
	default: word(buf, op); break;
	}
}

unsigned mips32_dasm(char *buf, uint32_t pc, uint32_t op)
{
	unsigned o = op >> 26;

	if (loadstore[o]) { f(buf, loadstore[o], "%s,%d(%s)", rn[RT(op)], SIMM(op), rn[RS(op)]); return 4; }
	switch (o) {
	case 0x00: dasm_special(buf, op); break;
	case 0x01:
		if (regimm[RT(op)]) f(buf, regimm[RT(op)], "%s,0x%08x", rn[RS(op)], BTARGET(pc, op));
		else if (RT(op) >= 8 && RT(op) <= 15 && regimm_trap[RT(op) - 8]) f(buf, regimm_trap[RT(op) - 8], "%s,%d", rn[RS(op)], SIMM(op));
		else if (RT(op) == 0x1F) f(buf, "synci", "%d(%s)", SIMM(op), rn[RS(op)]);
		else word(buf, op);
		break;
	case 0x02: f(buf, "j", "0x%08x", (unsigned)(((pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2))); break;
	case 0x03: f(buf, "jal", "0x%08x", (unsigned)(((pc + 4) & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2))); break;
	case 0x04: case 0x05: case 0x14: case 0x15:
		f(buf, o == 4 ? "beq" : o == 5 ? "bne" : o == 0x14 ? "beql" : "bnel", "%s,%s,0x%08x", rn[RS(op)], rn[RT(op)], BTARGET(pc, op));
		break;
	case 0x06: case 0x07: case 0x16: case 0x17:
		f(buf, o == 6 ? "blez" : o == 7 ? "bgtz" : o == 0x16 ? "blezl" : "bgtzl", "%s,0x%08x", rn[RS(op)], BTARGET(pc, op));
		break;
	case 0x08: f(buf, "addi", "%s,%s,%d", rn[RT(op)], rn[RS(op)], SIMM(op)); break;
	case 0x09: f(buf, "addiu", "%s,%s,%d", rn[RT(op)], rn[RS(op)], SIMM(op)); break;
	case 0x0A: f(buf, "slti", "%s,%s,%d", rn[RT(op)], rn[RS(op)], SIMM(op)); break;
	case 0x0B: f(buf, "sltiu", "%s,%s,%d", rn[RT(op)], rn[RS(op)], SIMM(op)); break;
	case 0x0C: f(buf, "andi", "%s,%s,0x%x", rn[RT(op)], rn[RS(op)], UIMM(op)); break;
	case 0x0D: f(buf, "ori", "%s,%s,0x%x", rn[RT(op)], rn[RS(op)], UIMM(op)); break;
	case 0x0E: f(buf, "xori", "%s,%s,0x%x", rn[RT(op)], rn[RS(op)], UIMM(op)); break;
	case 0x0F: f(buf, "lui", "%s,0x%x", rn[RT(op)], UIMM(op)); break;
	case 0x10: dasm_cop0(buf, op); break;
	case 0x1C: dasm_special2(buf, op); break;
	case 0x1F: dasm_special3(buf, op); break;
	case 0x2F: f(buf, "cache", "0x%x,%d(%s)", RT(op), SIMM(op), rn[RS(op)]); break;
	case 0x33: f(buf, "pref", "%u,%d(%s)", RT(op), SIMM(op), rn[RS(op)]); break;
	default: word(buf, op); break;
	}
	return 4;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tests/pinheck/mips32/check.sh`
Expected: `dasm: 45/45`, `unit: ok`, `mips32: 208 passed, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/cpu/mips32/mips32dasm.c tests/pinheck/mips32/dasm_test.c
git commit -m "mips32: disassembler"
```

### Task 6: Throughput baseline and test documentation

**Files:**
- Create: `tests/pinheck/mips32/bench.S`, `bench.sh`, `README.md`

**Interfaces:**
- Consumes: `build/mips32run`.
- Produces: `tests/pinheck/mips32/bench.sh` printing `bench: match, N M instr/s`. Milestone 9 compares against this number. The PIC32 needs about 80 M instr/s for real time.

- [ ] **Step 1: Write the benchmark**

180,000,012 instructions of mixed ALU, load/store and branch work. The final checksum is compared with QEMU so a fast-but-wrong core cannot pass.

`tests/pinheck/mips32/bench.S`:

```asm
#include "common.h"
	.text
	.globl __start
__start:
	li $8, 20000000
	li $9, 0
	la $10, buf
1:	addu $9, $9, $8
	sll $11, $9, 3
	xor $9, $9, $11
	lw $12, 0($10)
	addu $12, $12, $9
	sw $12, 0($10)
	addiu $8, $8, -1
	bnez $8, 1b
	nop
	WRITE_EXIT(buf, 4)
	.data
	.align 4
buf:	.word 0
```

`tests/pinheck/mips32/bench.sh`:

```sh
#!/bin/sh
cd "$(dirname "$0")" || exit 2
mkdir -p build
cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I../../../src/cpu/mips32 -o build/mips32run run.c ../../../src/cpu/mips32/mips32.c || exit 2
clang --target=mipsel-linux-gnu -march=mips32r2 -mno-abicalls -fno-pic -I. -c bench.S -o build/bench.o || exit 2
ld.lld -m elf32ltsmip -static -e __start -Ttext=0x400000 build/bench.o -o build/bench.elf || exit 2
python3 - <<'PY'
import subprocess, time
t = time.time()
ours = subprocess.run(['./build/mips32run', '-n', '1000000000', 'build/bench.elf'], capture_output=True).stdout
dt = time.time() - t
ref = subprocess.run(['qemu-mipsel', '-cpu', '4KEc', 'build/bench.elf'], capture_output=True).stdout
print('bench: %s, %.1f M instr/s' % ('match' if ours == ref else 'MISMATCH', (20000000 * 9 + 12) / dt / 1e6))
PY
```

Make it executable: `chmod +x tests/pinheck/mips32/bench.sh`

- [ ] **Step 2: Run it and record the number**

Run: `tests/pinheck/mips32/bench.sh`
Expected: `bench: match, …`. The reference machine measured 106–118 M instr/s with `-O2`. Put the measured figure in the commit message.

- [ ] **Step 3: Write the README**

`tests/pinheck/mips32/README.md`:

```markdown
# mips32 core tests

`./check.sh` runs everything; `SEEDS=N ./check.sh` changes the random corpus size (default 200). `./bench.sh` reports interpreter throughput.

Needs `clang`, `ld.lld` and `qemu-mipsel` (`apt install clang lld qemu-user`).

- `diff/*.S` and generated `build/random/*.S` run under `qemu-mipsel -cpu 4KEc` and under `build/mips32run`; the bytes each writes to stdout must match. `cmpdump.py` names the differing register.
- `golden/*.S` cover exceptions, CP0 and EIC, which QEMU user mode cannot referee. They run under `mips32run -x -e` and are compared with `golden/*.expect`.
- `unit_test.c` drives the core API cycle by cycle; `dasm_test.c` checks the disassembler.
- `run.c` loads an ELF, services the Linux `write`/`exit` syscalls, and exposes two test registers: a write to `0xA0FFFFF0` sets the EIC input (`ripl | vector << 8 | srs << 16`), a write to `0xA0FFFFF4` clears the latched timer interrupt.
```

- [ ] **Step 4: Full clean run**

Run: `rm -rf tests/pinheck/mips32/build && tests/pinheck/mips32/check.sh`
Expected: `dasm: 45/45`, `unit: ok`, `mips32: 208 passed, 0 failed`, exit 0. This is Milestone 1's exit criterion (spec §8).

- [ ] **Step 5: Commit**

```bash
git add tests/pinheck/mips32/bench.S tests/pinheck/mips32/bench.sh tests/pinheck/mips32/README.md
git commit -m "mips32: throughput benchmark (<measured> M instr/s) and test README"
```
