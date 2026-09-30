// Translates runs of a cog's local instructions to x86-64 code with asmjit (see p8x32a.h, p8x32a_jblk). Each
// instruction does exactly what run_local() does for it; a run leaves after a jump that runs, before a slot whose
// word no longer fits, and ends before any instruction run_local() would not run and before cog address 511.
#include "p8x32ajit.h"
#include <cstddef>

#if defined(__x86_64__) || defined(_M_X64)
#include <asmjit/x86.h>
#include <cstdlib>
#include <new>
#include <vector>

using namespace asmjit;

namespace {

struct P8Jit {
	JitRuntime rt;
	std::vector<p8x32a_jblk *> blocks;
	uint64_t tail, tail_ret; // the code a block leaves through, and its return to run_local
};

enum { OP_ROR = 0x08, OP_ROL, OP_SHR, OP_SHL, OP_RCR, OP_RCL, OP_SAR, OP_MOVS = 0x14, OP_MOVD, OP_MOVI, OP_JMP,
       OP_AND, OP_ANDN, OP_OR, OP_XOR, OP_MUXC, OP_MUXNC, OP_MUXZ, OP_MUXNZ, OP_ADD, OP_SUB, OP_MOV = 0x28,
       OP_CMPS = 0x30, OP_DJNZ = 0x39, OP_TJNZ, OP_TJZ };

inline unsigned op_of(uint32_t i) { return i >> 26; }
inline unsigned dst_of(uint32_t i) { return (i >> 9) & 511; }
inline unsigned src_of(uint32_t i) { return i & 511; }
inline bool fim(uint32_t i) { return (i >> 22) & 1; }
inline bool fwr(uint32_t i) { return (i >> 23) & 1; }
inline bool fwc(uint32_t i) { return (i >> 24) & 1; }
inline bool fwz(uint32_t i) { return (i >> 25) & 1; }
inline unsigned cond_of(uint32_t i) { return (i >> 18) & 15; }

bool op_ok(unsigned op)
{
	return (op >= OP_ROR && op <= OP_SAR) || (op >= OP_MOVS && op <= OP_SUB) || op == OP_MOV || op == OP_CMPS ||
	       (op >= OP_DJNZ && op <= OP_TJZ);
}

// a fixed word run_local() would run as a local instruction
bool supported(uint32_t i)
{
	if (!op_ok(op_of(i))) return false;
	if (!fim(i) && src_of(i) >= 0x1F0) return false;
	if (fwr(i) && dst_of(i) >= 0x1F0) return false;
	return true;
}

inline bool is_jump(unsigned op) { return op == OP_JMP || op >= OP_DJNZ; }

// registers: r10 st, r9 ram, r8d fl, ecx s, edx d, eax result, r11d carry out, esi/edi scratch,
// ebx the word fetched for the next slot, r12d the word of this slot, r13d its D address (run-time slots),
// r14 the time of the block's first instruction, r15d the budget left at it, ebp instructions run so far
const x86::Gp ST = x86::r10, RAM = x86::r9, FL = x86::r8d, NEXTW = x86::ebx, CURW = x86::r12d, DADR = x86::r13d,
              T2 = x86::r14, BUDGET = x86::r15d, TOTAL = x86::ebp;

// every block and the tail share this frame, so a block can continue in another
void make_frame(FuncDetail &func, FuncFrame &frame, const Environment &env)
{
	func.init(FuncSignature::build<uint32_t, p8x32a_jst *>(), env);
	frame.init(func);
	frame.add_dirty_regs(x86::rax, x86::rcx, x86::rdx, x86::rsi, x86::rdi, x86::r8, x86::r9, x86::r10, x86::r11,
	                     x86::rbx, x86::rbp, x86::r12, x86::r13, x86::r14, x86::r15);
}

x86::Mem cog(unsigned a) { return x86::dword_ptr(RAM, (int)(a * 4)); }
x86::Mem stf(size_t off) { return x86::dword_ptr(ST, (int)off); }
x86::Mem cogr(const x86::Gp &r) { return x86::dword_ptr(RAM, r.r64(), 2); }

void parity_to_r11(x86::Assembler &a)
{
	a.mov(x86::esi, x86::eax);
	a.shr(x86::esi, 16);
	a.xor_(x86::esi, x86::eax);
	a.mov(x86::edi, x86::esi);
	a.shr(x86::edi, 8);
	a.xor_(x86::esi, x86::edi);
	a.xor_(x86::r11d, x86::r11d);
	a.test(x86::sil, x86::sil);
	a.setnp(x86::r11b);
}

struct Emit {
	x86::Assembler &a;
	const FuncFrame &frame;
	unsigned base;
	uint64_t tail, tail_ret;

	// k instructions ran in this block; the last one's exit state is in st
	void leave(unsigned k)
	{
		a.mov(x86::eax, k);
		a.mov(x86::rsi, tail);
		a.jmp(x86::rsi);
	}
	// stop before slot k: the run so far ends with a sequential fetch of slot k
	void stop_before(unsigned k, const x86::Gp &word)
	{
		a.mov(stf(offsetof(p8x32a_jst, pc)), (base + k) & 511);
		a.mov(stf(offsetof(p8x32a_jst, px)), (base + k) & 511);
		a.mov(stf(offsetof(p8x32a_jst, nix)), word);
		a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		leave(k);
	}

	// slot k with word i (fixed, or the S/D fields read at run time from CURW when dyn); next_dyn: slot k+1 is read
	// at run time (or k is the last), so its word is fetched into NEXTW here, before this instruction writes
	void insn(unsigned k, uint32_t i, bool dyn, bool last, bool prefetch_next)
	{
		unsigned op = op_of(i), cond = cond_of(i), addr = base + k, pc = (addr + 1) & 511;
		bool imm = fim(i), jump = is_jump(op);
		Label skip = a.new_label(), before = a.new_label();

		if (dyn) {
			// the word this slot was fetched with: ix for slot 0, else fetched during the previous slot
			if (k == 0) a.mov(CURW, stf(offsetof(p8x32a_jst, ix)));
			else a.mov(CURW, NEXTW);
			a.mov(x86::eax, CURW);
			a.and_(x86::eax, ~P8X32A_JDYN);
			a.cmp(x86::eax, i & ~P8X32A_JDYN);
			a.jne(before);
			if (!imm) {
				a.mov(x86::esi, CURW);
				a.and_(x86::esi, 511);
				a.cmp(x86::esi, 0x1F0);
				a.jae(before);
			}
			a.mov(DADR, CURW);
			a.shr(DADR, 9);
			a.and_(DADR, 511);
			if (fwr(i)) {
				a.cmp(DADR, 0x1F0);
				a.jae(before);
			}
		}
		if (prefetch_next) a.mov(NEXTW, cog(pc));
		if (cond == 0) goto end;
		if (cond != 15) {
			a.mov(x86::eax, cond);
			a.bt(x86::eax, FL);
			a.jnc(skip);
		}
		if (dyn) {
			if (imm) { a.mov(x86::ecx, CURW); a.and_(x86::ecx, 511); }
			else a.mov(x86::ecx, cogr(x86::esi));
			a.mov(x86::edx, cogr(DADR));
		} else {
			if (imm) a.mov(x86::ecx, src_of(i));
			else a.mov(x86::ecx, cog(src_of(i)));
			a.mov(x86::edx, cog(dst_of(i)));
		}
		switch (op) {
		case OP_ROR: a.mov(x86::eax, x86::edx); a.ror(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.and_(x86::r11d, 1); break;
		case OP_ROL: a.mov(x86::eax, x86::edx); a.rol(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.shr(x86::r11d, 31); break;
		case OP_SHR: a.mov(x86::eax, x86::edx); a.shr(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.and_(x86::r11d, 1); break;
		case OP_SHL: a.mov(x86::eax, x86::edx); a.shl(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.shr(x86::r11d, 31); break;
		case OP_SAR: a.mov(x86::eax, x86::edx); a.sar(x86::eax, x86::cl); a.mov(x86::r11d, x86::edx); a.and_(x86::r11d, 1); break;
		case OP_RCR: case OP_RCL:
			a.mov(x86::esi, -1);
			if (op == OP_RCR) a.shr(x86::esi, x86::cl); else a.shl(x86::esi, x86::cl);
			a.not_(x86::esi);
			a.bt(FL, 1);
			a.sbb(x86::edi, x86::edi);
			a.and_(x86::esi, x86::edi);
			a.mov(x86::eax, x86::edx);
			if (op == OP_RCR) a.shr(x86::eax, x86::cl); else a.shl(x86::eax, x86::cl);
			a.or_(x86::eax, x86::esi);
			a.mov(x86::r11d, x86::edx);
			if (op == OP_RCR) a.and_(x86::r11d, 1); else a.shr(x86::r11d, 31);
			break;
		case OP_MOVS: case OP_MOVD: case OP_MOVI: case OP_JMP:
			a.mov(x86::eax, x86::ecx);
			if (op == OP_MOVS) { a.and_(x86::eax, 511); a.mov(x86::esi, x86::edx); a.and_(x86::esi, 0xFFFFFE00); }
			else if (op == OP_MOVD) { a.and_(x86::eax, 511); a.shl(x86::eax, 9); a.mov(x86::esi, x86::edx); a.and_(x86::esi, 0xFFFC01FF); }
			else if (op == OP_MOVI) { a.shl(x86::eax, 23); a.mov(x86::esi, x86::edx); a.and_(x86::esi, 0x007FFFFF); }
			else { a.mov(x86::eax, pc); a.mov(x86::esi, x86::edx); a.and_(x86::esi, 0xFFFFFE00); }
			a.or_(x86::eax, x86::esi);
			a.xor_(x86::r11d, x86::r11d);
			a.cmp(x86::edx, x86::ecx);
			a.setb(x86::r11b);
			break;
		case OP_AND: a.mov(x86::eax, x86::edx); a.and_(x86::eax, x86::ecx); break;
		case OP_ANDN: a.mov(x86::eax, x86::ecx); a.not_(x86::eax); a.and_(x86::eax, x86::edx); break;
		case OP_OR: a.mov(x86::eax, x86::edx); a.or_(x86::eax, x86::ecx); break;
		case OP_XOR: a.mov(x86::eax, x86::edx); a.xor_(x86::eax, x86::ecx); break;
		case OP_MUXC: case OP_MUXNC: case OP_MUXZ: case OP_MUXNZ:
			a.mov(x86::eax, x86::edx);
			a.or_(x86::eax, x86::ecx);
			a.mov(x86::esi, x86::ecx);
			a.not_(x86::esi);
			a.and_(x86::esi, x86::edx);
			a.bt(FL, (op == OP_MUXC || op == OP_MUXNC) ? 1 : 0);
			if (op == OP_MUXC || op == OP_MUXZ) a.cmovnc(x86::eax, x86::esi); else a.cmovc(x86::eax, x86::esi);
			break;
		case OP_ADD: a.xor_(x86::r11d, x86::r11d); a.mov(x86::eax, x86::edx); a.add(x86::eax, x86::ecx); a.setc(x86::r11b); break;
		case OP_SUB: a.xor_(x86::r11d, x86::r11d); a.mov(x86::eax, x86::edx); a.sub(x86::eax, x86::ecx); a.setc(x86::r11b); break;
		case OP_CMPS: a.xor_(x86::r11d, x86::r11d); a.mov(x86::eax, x86::edx); a.cmp(x86::edx, x86::ecx); a.setl(x86::r11b); a.sub(x86::eax, x86::ecx); break;
		case OP_MOV: a.mov(x86::eax, x86::ecx); a.mov(x86::r11d, x86::ecx); a.shr(x86::r11d, 31); break;
		case OP_DJNZ: a.xor_(x86::r11d, x86::r11d); a.test(x86::edx, x86::edx); a.sete(x86::r11b); a.lea(x86::eax, x86::ptr(x86::rdx, -1)); break;
		case OP_TJNZ: case OP_TJZ: a.xor_(x86::r11d, x86::r11d); a.mov(x86::eax, x86::edx); break;
		}
		if (op >= OP_AND && op <= OP_MUXNZ && fwc(i)) parity_to_r11(a);
		if (jump) {
			// the word at the target is fetched before the write
			a.mov(stf(offsetof(p8x32a_jst, s)), x86::ecx);
			a.mov(stf(offsetof(p8x32a_jst, d)), x86::edx);
			a.mov(x86::esi, x86::ecx);
			a.and_(x86::esi, 511);
			a.mov(stf(offsetof(p8x32a_jst, px)), x86::esi);
			a.mov(x86::esi, cogr(x86::esi));
			a.mov(stf(offsetof(p8x32a_jst, nix)), x86::esi);
			a.xor_(x86::esi, x86::esi);
			if (op == OP_DJNZ) { a.cmp(x86::edx, 1); a.sete(x86::sil); }
			else if (op == OP_TJNZ) { a.test(x86::edx, x86::edx); a.sete(x86::sil); }
			else if (op == OP_TJZ) { a.test(x86::edx, x86::edx); a.setne(x86::sil); }
			a.mov(stf(offsetof(p8x32a_jst, jc)), x86::esi);
			a.mov(stf(offsetof(p8x32a_jst, jmp)), 1);
			a.mov(stf(offsetof(p8x32a_jst, pc)), pc);
			if (dyn) a.mov(stf(offsetof(p8x32a_jst, w)), CURW);
			else a.mov(stf(offsetof(p8x32a_jst, w)), i);
		}
		if (fwr(i)) {
			Label same = a.new_label(), first = a.new_label();
			x86::Mem m = dyn ? cogr(DADR) : cog(dst_of(i));
			a.cmp(m, x86::eax);
			a.je(same);
			a.mov(x86::esi, m);
			a.mov(m, x86::eax);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
			a.mov(x86::byte_ptr(x86::rdi, (int)offsetof(p8x32a_loop, dirty)), 1);
			// a fixed code slot changed: report it
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, code)));
			if (dyn) a.bt(x86::dword_ptr(x86::rdi), DADR);
			else a.bt(x86::dword_ptr(x86::rdi, (int)((dst_of(i) >> 5) * 4)), dst_of(i) & 31);
			a.jnc(same);
			a.cmp(stf(offsetof(p8x32a_jst, inv)), 0);
			a.je(first);
			a.mov(stf(offsetof(p8x32a_jst, inv)), -1);
			a.jmp(same);
			a.bind(first);
			a.mov(stf(offsetof(p8x32a_jst, inv_old)), x86::esi);
			if (dyn) { a.lea(x86::esi, x86::ptr(DADR.r64(), 1)); a.mov(stf(offsetof(p8x32a_jst, inv)), x86::esi); }
			else a.mov(stf(offsetof(p8x32a_jst, inv)), dst_of(i) + 1);
			a.bind(same);
		}
		if (fwz(i)) {
			a.xor_(x86::esi, x86::esi);
			a.test(x86::eax, x86::eax);
			a.sete(x86::sil);
			a.and_(FL, ~1u);
			a.or_(FL, x86::esi);
		}
		if (fwc(i)) {
			a.and_(FL, ~2u);
			a.mov(x86::esi, x86::r11d);
			a.add(x86::esi, x86::esi);
			a.or_(FL, x86::esi);
		}
		if (jump) leave(k + 1);
		a.bind(skip);
	end:
		if (last) stop_before(k + 1, NEXTW);
		if (dyn) {
			Label over = a.new_label();
			a.jmp(over);
			a.bind(before);
			if (k == 0) {
				a.mov(x86::rsi, tail_ret);
				a.jmp(x86::rsi);
			} else
				stop_before(k, CURW);
			a.bind(over);
		}
	}
};

// After a block: account for the k (eax) instructions it ran, do run_local's loop_edge search step for a backward
// jump, and continue in the block at the next address when nothing needs run_local.
bool build_tail(P8Jit *j)
{
	CodeHolder code;
	code.init(j->rt.environment());
	x86::Assembler a(&code);
	FuncDetail func;
	FuncFrame frame;
	make_frame(func, frame, code.environment());
	frame.finalize();
	Label ret = a.new_label(), no_edge = a.new_label(), diff = a.new_label(), reset = a.new_label(), full = a.new_label();
	const x86::Gp L = x86::rdi;

	a.add(TOTAL, x86::eax);
	a.lea(T2, x86::ptr(T2, x86::rax, 2));
	a.sub(BUDGET, x86::eax);
	a.mov(L, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
	a.add(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), x86::ax);
	// a backward jump: the loop search state (loop_edge in SEARCH)
	a.cmp(stf(offsetof(p8x32a_jst, jmp)), 0);
	a.je(no_edge);
	a.cmp(stf(offsetof(p8x32a_jst, jc)), 0);
	a.jne(no_edge);
	a.mov(x86::ecx, stf(offsetof(p8x32a_jst, px)));
	a.cmp(x86::ecx, stf(offsetof(p8x32a_jst, pc)));
	a.jae(no_edge);
	a.movzx(x86::edx, x86::word_ptr(L, (int)offsetof(p8x32a_loop, head)));
	a.lea(x86::rax, x86::ptr(T2, -3));
	a.cmp(x86::ecx, x86::edx);
	a.jne(diff);
	a.cmp(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.jne(reset);
	a.cmp(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), P8X32A_PAT / 2);
	a.ja(reset);
	a.cmp(x86::rax, x86::qword_ptr(L, (int)offsetof(p8x32a_loop, head_t)));
	a.jbe(reset);
	a.mov(stf(offsetof(p8x32a_jst, edge)), 1);
	a.jmp(ret);
	a.bind(diff);
	a.cmp(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.jne(reset);
	a.cmp(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), P8X32A_PAT / 2);
	a.ja(reset);
	a.cmp(x86::edx, 0xFFFF);
	a.jne(no_edge);
	a.bind(reset);
	a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, head)), x86::cx);
	a.mov(x86::qword_ptr(L, (int)offsetof(p8x32a_loop, head_t)), x86::rax);
	a.mov(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.mov(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, hub)), 0);
	a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), 0);
	a.bind(no_edge);
	// continue unless a code slot changed, the next instruction is cancelled, or the next block does not fit
	a.cmp(stf(offsetof(p8x32a_jst, inv)), 0);
	a.jne(ret);
	a.cmp(stf(offsetof(p8x32a_jst, jc)), 0);
	a.jne(ret);
	a.mov(x86::ecx, stf(offsetof(p8x32a_jst, px)));
	a.cmp(x86::ecx, 511);
	a.je(ret);
	a.mov(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tab)));
	a.mov(x86::rsi, x86::qword_ptr(x86::rsi, x86::rcx, 3));
	a.test(x86::rsi, x86::rsi);
	a.jz(ret);
	a.cmp(x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, valid)), 0);
	a.je(ret);
	a.mov(x86::eax, x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, len)));
	a.test(x86::eax, x86::eax);
	a.jz(ret);
	a.cmp(x86::eax, BUDGET);
	a.ja(ret);
	a.mov(x86::edx, stf(offsetof(p8x32a_jst, nix)));
	a.xor_(x86::edx, x86::dword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, words)));
	a.test(x86::byte_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, dyn)), 1);
	a.jz(full);
	a.and_(x86::edx, ~P8X32A_JDYN);
	a.bind(full);
	a.test(x86::edx, x86::edx);
	a.jnz(ret);
	a.mov(x86::eax, stf(offsetof(p8x32a_jst, nix)));
	a.mov(stf(offsetof(p8x32a_jst, ix)), x86::eax);
	a.jmp(x86::qword_ptr(x86::rsi, (int)offsetof(p8x32a_jblk, body)));
	a.bind(ret);
	a.mov(stf(offsetof(p8x32a_jst, fl)), FL);
	a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, t2)), T2);
	a.mov(stf(offsetof(p8x32a_jst, budget)), BUDGET);
	a.mov(x86::eax, TOTAL);
	a.emit_epilog(frame);
	void *fn = NULL;
	if (j->rt.add(&fn, &code) != kErrorOk) return false;
	j->tail = (uint64_t)(uintptr_t)fn;
	j->tail_ret = j->tail + code.label_offset(ret);
	return true;
}

} // namespace

extern "C" void *p8x32a_jit_new(void)
{
	P8Jit *j = new (std::nothrow) P8Jit();
	if (j && !build_tail(j)) { delete j; j = NULL; }
	return j;
}

extern "C" void p8x32a_jit_free(void *jit)
{
	P8Jit *j = (P8Jit *)jit;
	if (!j) return;
	for (size_t k = 0; k < j->blocks.size(); k++) {
		if (j->blocks[k]->fn) j->rt.release(j->blocks[k]->fn);
		free(j->blocks[k]);
	}
	delete j;
}

extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var)
{
	P8Jit *j = (P8Jit *)jit;
	p8x32a_jblk *b = old;
	unsigned len = 0, k;

	if (!b) {
		b = (p8x32a_jblk *)calloc(1, sizeof(*b));
		if (!b) return NULL;
		j->blocks.push_back(b);
	} else if (b->fn) {
		j->rt.release(b->fn);
	}
	b->fn = NULL;
	b->len = 0;
	b->valid = 1;
	b->dyn = 0;
	b->words[0] = ix;
	// the run: translatable words below the special registers, ending at an unconditional jump or a slot whose D field may point
	// anywhere; a slot whose op, flags or condition have changed is not translated
	while (len < P8X32A_JMAX && a + len < 0x1F0) {
		uint32_t w = len ? ram[a + len] : ix, v = var[a + len];
		bool dyn = v != 0;
		if ((v & ~P8X32A_JDYN) || !op_ok(op_of(w))) break;
		if (!dyn && !supported(w)) break;
		b->words[len] = w;
		if (dyn) b->dyn |= 1u << len;
		len++;
		if ((is_jump(op_of(w)) && cond_of(w) == 15) || (dyn && fwr(w))) break;
	}
	// a fixed instruction must not rewrite a later fixed slot other than the next, whose word it already fetched
	// (run-time slots are fetched as the run goes)
	for (k = 0; k < len; k++) {
		uint32_t w = b->words[k];
		unsigned d = dst_of(w);
		if (!(b->dyn >> k & 1) && fwr(w) && d >= a + k + 2 && d < a + len && !(b->dyn >> (d - a) & 1)) len = d - a;
	}
	if (!len) return b;

	CodeHolder code;
	code.init(j->rt.environment());
	x86::Assembler as(&code);
	FuncDetail func;
	FuncFrame frame;
	make_frame(func, frame, code.environment());
	FuncArgsAssignment args(&func);
	args.assign_all(ST);
	args.update_func_frame(frame);
	frame.finalize();
	Label body = as.new_label();
	as.emit_prolog(frame);
	as.emit_args_assignment(frame, args);
	as.mov(RAM, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, ram)));
	as.mov(FL, stf(offsetof(p8x32a_jst, fl)));
	as.mov(T2, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, t2)));
	as.mov(BUDGET, stf(offsetof(p8x32a_jst, budget)));
	as.xor_(TOTAL, TOTAL);
	as.bind(body);
	Emit e = { as, frame, a, j->tail, j->tail_ret };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);
		e.insn(k, b->words[k], (b->dyn >> k & 1) != 0, last, last || next_dyn);
	}
	uint32_t (*fn)(p8x32a_jst *) = NULL;
	if (j->rt.add(&fn, &code) != kErrorOk) return b;
	b->fn = fn;
	b->body = (const void *)((uintptr_t)fn + code.label_offset(body));
	b->len = len;
	return b;
}
#else
extern "C" void *p8x32a_jit_new(void) { return NULL; }
extern "C" void p8x32a_jit_free(void *jit) { (void)jit; }
extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var)
{
	(void)jit; (void)old; (void)a; (void)ix; (void)ram; (void)var;
	return NULL;
}
#endif
