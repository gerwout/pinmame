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
		uint32_t nr = mips32_regs(s)[2], i, pa;
		if (nr == 4004) {
			for (i = 0; i < mips32_regs(s)[6]; i++) {
				if (!mips32_translate(s, mips32_regs(s)[5] + i, &pa) || pa >= MEMSIZE) {
					fprintf(stderr, "write: bad buffer %08x\n", (unsigned)(mips32_regs(s)[5] + i));
					exit_status = 98;
					return MIPS32_HOOK_STOP;
				}
				out_byte(mem[pa]);
			}
			mips32_regs(s)[2] = mips32_regs(s)[6];
			mips32_regs(s)[7] = 0;
			return MIPS32_HOOK_SKIP;
		}
		if (nr == 4001) { exit_status = (int)(mips32_regs(s)[4] & 0xFF); return MIPS32_HOOK_STOP; }
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
	if (!getenv("MIPS32RUN_BUS")) mips32_direct(&cpu, 0, 0, MMIO_EIC & ~0xFFFu, mem, mem); /* all but the MMIO page */
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
