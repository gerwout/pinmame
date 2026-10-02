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
	uint64_t ltail, ltail_ret; // the same for a lazy cog's blocks
	uint64_t edge; // run_local's loop_edge search step for a backward jump (called)
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

inline bool is_jump(unsigned op) { return op == OP_JMP || op >= OP_DJNZ; }

// a fixed word run_local() would run as a local instruction (PAR and CNT the only special sources); with outa, also
// a write to OUTA or a hub read without WC (a lazy cog's)
bool supported(uint32_t i, int outa)
{
	unsigned op = op_of(i);
	if (outa && op <= 2) return fwr(i) && !fwc(i) && dst_of(i) < 0x1F0 && (fim(i) || src_of(i) < 0x1F0);
	// a hub read or write that ends a block (as run_local's dec jh); one that never runs is a no-op
	if (op <= 2) return !fwc(i) && (fim(i) || src_of(i) <= 0x1F0) && (!fwr(i) || dst_of(i) < 0x1F0);
	if (!op_ok(op)) return false;
	if (!fim(i) && src_of(i) >= 0x1F0 && (src_of(i) > 0x1F1 || is_jump(op))) return false;
	if (fwr(i) && dst_of(i) >= 0x1F0)
		return outa && dst_of(i) == 0x1F4 && ((op >= OP_ROR && op <= OP_SAR) || (op >= OP_MOVS && op <= OP_MOVI) || (op >= OP_AND && op <= OP_SUB) || op == OP_MOV);
	return true;
}

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
	bool outa;
	Label res; // the block's resume entry
	uint64_t edge;
	bool writes; // the block writes cog RAM (st.inv may be set)

	// the tail's work for a run of cnt instructions ending here, on into the block at the next address (pxc, or ecx
	// when -1) if it fits: the count, a backward jump's loop search step, the link; else to slow (accounted).
	// word: the next instruction's word (a register other than rax, rcx, rdx, rdi, rsi, or st.nix when none)
	void chain(unsigned cnt, int pxc, bool backward, const x86::Gp *word, Label slow)
	{
		a.add(TOTAL, cnt);
		a.lea(T2, x86::ptr(T2, (int)(4 * cnt)));
		a.sub(BUDGET, cnt);
		a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
		a.add(x86::word_ptr(x86::rdi, (int)offsetof(p8x32a_loop, nins)), cnt);
		if (pxc >= 0) a.mov(x86::ecx, pxc);
		if (backward) {
			Label fwd = a.new_label();
			if (pxc < 0) {
				a.cmp(x86::ecx, (base + cnt) & 511);
				a.jae(fwd);
			}
			a.mov(x86::rsi, edge);
			a.call(x86::rsi);
			a.test(x86::eax, x86::eax);
			a.jnz(slow);
			a.bind(fwd);
		}
		if (writes) {
			a.cmp(stf(offsetof(p8x32a_jst, inv)), 0);
			a.jne(slow);
		}
		if (pxc < 0) {
			a.cmp(x86::ecx, 511);
			a.je(slow);
		} else if (pxc == 511) {
			a.jmp(slow);
			return;
		}
		a.shl(x86::ecx, 5);
		a.add(x86::rcx, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, link)));
		a.mov(x86::eax, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, len)));
		a.cmp(x86::eax, BUDGET);
		a.ja(slow);
		if (word) a.mov(x86::eax, *word);
		else a.mov(x86::eax, stf(offsetof(p8x32a_jst, nix)));
		a.mov(x86::edx, x86::eax);
		a.xor_(x86::edx, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, word)));
		a.and_(x86::edx, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, mask)));
		a.jnz(slow);
		a.mov(stf(offsetof(p8x32a_jst, ix)), x86::eax);
		a.jmp(x86::qword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, body)));
	}
	// the run of cnt instructions ends with a sequential fetch of the slot at address next (word in wreg): on into the
	// next block, else back to run_local with the exit state
	void seq_exit(unsigned cnt, unsigned next, const x86::Gp &wreg)
	{
		Label slow = a.new_label();
		chain(cnt, (int)next, false, &wreg, slow);
		a.bind(slow);
		a.mov(stf(offsetof(p8x32a_jst, pc)), next);
		a.mov(stf(offsetof(p8x32a_jst, px)), next);
		a.mov(stf(offsetof(p8x32a_jst, nix)), wreg);
		a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		a.mov(x86::rsi, tail_ret);
		a.jmp(x86::rsi);
	}

	// the next instruction at st.latch + 7 after a hub read or write counted as instruction k of the run (the tail
	// adds k + 1): T2 and the budget from there
	void after_hub(unsigned k)
	{
		Label fit = a.new_label();
		a.mov(x86::rax, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, latch)));
		a.lea(x86::rsi, x86::ptr(x86::rax, 7));
		a.lea(T2, x86::ptr(x86::rax, (int)(7 - 4 * (k + 1))));
		a.mov(BUDGET, k + 1);
		a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tl)));
		a.cmp(x86::rsi, x86::rdi);
		a.ja(fit);
		a.sub(x86::rdi, x86::rsi);
		a.shr(x86::rdi, 2);
		a.lea(BUDGET, x86::ptr(x86::rdi, (int)(k + 2)));
		a.bind(fit);
	}
	// the hub read or write i at h = st.latch + 2 (rax: st.latch), s = ecx, d = edx, run as event_run runs it;
	// FL updated. A read is done here, a write through st.hubfn
	void hub_access(uint32_t i)
	{
		if (fwr(i)) {
			unsigned op = op_of(i), sz = op == 2 ? 4 : op == 1 ? 2 : 1, dst = dst_of(i);
			Label same = a.new_label(), first = a.new_label(), nochk = a.new_label();
			a.lea(x86::rsi, x86::ptr(x86::rax, 2));
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, pnow)));
			a.mov(x86::qword_ptr(x86::rdi), x86::rsi);
			a.and_(x86::ecx, 0x10000 - sz);
			// test builds: the time-order check of the read
			a.cmp(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, chkfn)), 0);
			a.je(nochk);
			a.push(x86::r8);
			a.push(x86::r9);
			a.push(x86::r10);
			a.push(x86::rcx);
			a.mov(x86::rdi, ST);
			a.mov(x86::esi, x86::ecx);
			a.mov(x86::edx, sz);
			a.sub(x86::rsp, 8);
			a.call(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, chkfn)));
			a.add(x86::rsp, 8);
			a.pop(x86::rcx);
			a.pop(x86::r10);
			a.pop(x86::r9);
			a.pop(x86::r8);
			a.bind(nochk);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hub)));
			if (op == 0) a.movzx(x86::eax, x86::byte_ptr(x86::rdi, x86::rcx));
			else if (op == 1) a.movzx(x86::eax, x86::word_ptr(x86::rdi, x86::rcx));
			else a.mov(x86::eax, x86::dword_ptr(x86::rdi, x86::rcx));
			// the write to D, as a block's instruction writes
			a.cmp(cog(dst), x86::eax);
			a.je(same);
			a.mov(x86::esi, cog(dst));
			a.mov(cog(dst), x86::eax);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
			a.mov(x86::byte_ptr(x86::rdi, (int)offsetof(p8x32a_loop, dirty)), 1);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, code)));
			a.bt(x86::dword_ptr(x86::rdi, (int)((dst >> 5) * 4)), dst & 31);
			a.jnc(same);
			a.cmp(stf(offsetof(p8x32a_jst, inv)), 0);
			a.je(first);
			a.mov(stf(offsetof(p8x32a_jst, inv)), -1);
			a.jmp(same);
			a.bind(first);
			a.mov(stf(offsetof(p8x32a_jst, inv_old)), x86::esi);
			a.mov(stf(offsetof(p8x32a_jst, inv)), dst + 1);
			a.bind(same);
			if (fwz(i)) {
				a.xor_(x86::esi, x86::esi);
				a.test(x86::eax, x86::eax);
				a.sete(x86::sil);
				a.and_(FL, ~1u);
				a.or_(FL, x86::esi);
			}
			return;
		}
		hub_call(i);
	}
	// hubfn(st, s = ecx, d = edx, i, FL) into FL; r8-r10 kept (the stack is 16-byte aligned after three pushes)
	void hub_call(uint32_t i)
	{
		a.push(x86::r8);
		a.push(x86::r9);
		a.push(x86::r10);
		a.mov(x86::rdi, ST);
		a.mov(x86::esi, x86::ecx);
		a.mov(x86::ecx, i);
		a.call(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hubfn)));
		a.pop(x86::r10);
		a.pop(x86::r9);
		a.pop(x86::r8);
		a.mov(FL, x86::eax);
	}
	// k instructions ran in this block; the last one's exit state is in st
	void leave(unsigned k)
	{
		a.mov(x86::eax, k);
		a.mov(x86::rsi, tail);
		a.jmp(x86::rsi);
	}
	// stop before slot k: the run so far ends with a sequential fetch of slot k; back to run_local if ret, else on
	// through the tail
	void stop_before(unsigned k, const x86::Gp &word, bool ret = false)
	{
		if (!outa && !ret && k && (word == NEXTW || word == CURW)) {
			seq_exit(k, (base + k) & 511, word);
			return;
		}
		a.mov(stf(offsetof(p8x32a_jst, pc)), (base + k) & 511);
		a.mov(stf(offsetof(p8x32a_jst, px)), (base + k) & 511);
		a.mov(stf(offsetof(p8x32a_jst, nix)), word);
		a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
		a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
		if (!ret) {
			leave(k);
			return;
		}
		// the tail's count of a run without a jump, then its return
		a.add(TOTAL, k);
		a.lea(T2, x86::ptr(T2, (int)(4 * k)));
		a.sub(BUDGET, k);
		a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
		a.add(x86::word_ptr(x86::rdi, (int)offsetof(p8x32a_loop, nins)), k);
		a.mov(x86::rsi, tail_ret);
		a.jmp(x86::rsi);
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
		if (!dyn && !imm && src_of(i) == 0x1F1) {
			// a CNT source: the cog's loop is not idle, whether the instruction runs or not
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
			a.mov(x86::byte_ptr(x86::rdi, (int)offsetof(p8x32a_loop, dirty)), 1);
		}
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
			else if (src_of(i) == 0x1F0) a.mov(x86::ecx, stf(offsetof(p8x32a_jst, par)));
			else if (src_of(i) == 0x1F1) {
				// CNT: this instruction's time
				a.lea(x86::rax, x86::ptr(T2, (int)(4 * k)));
				a.mov(x86::ecx, x86::eax);
			} else a.mov(x86::ecx, cog(src_of(i)));
			a.mov(x86::edx, cog(dst_of(i)));
		}
		if (op <= 2 && !outa && cond != 0) {
			// a hub read or write ending the block: its slot as next_slot finds it
			Label sb = a.new_label(), got = a.new_label(), wait = a.new_label();
			a.lea(x86::rax, x86::ptr(T2, (int)(4 * k + 1)));
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, slot)));
			a.cmp(x86::rax, x86::rdi);
			a.jbe(sb);
			a.sub(x86::rax, x86::rdi);
			a.add(x86::rax, 15);
			a.and_(x86::rax, -16);
			a.add(x86::rax, x86::rdi);
			a.jmp(got);
			a.bind(sb);
			a.mov(x86::rax, x86::rdi);
			a.bind(got);
			// run now as event_run would: h = latch + 2 by t, its key below lim, latch + 5 before dis, gen unchanged
			a.lea(x86::rsi, x86::ptr(x86::rax, 2));
			a.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, t)));
			a.ja(wait);
			a.shl(x86::rsi, 4);
			a.mov(x86::edi, stf(offsetof(p8x32a_jst, n)));
			a.or_(x86::rsi, x86::rdi);
			a.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, lim)));
			a.jae(wait);
			a.lea(x86::rsi, x86::ptr(x86::rax, 5));
			a.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, dis)));
			a.jae(wait);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, pgen)));
			a.mov(x86::esi, x86::dword_ptr(x86::rdi));
			a.cmp(x86::esi, stf(offsetof(p8x32a_jst, gen)));
			a.jne(wait);
			a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, latch)), x86::rax);
			hub_access(i);
			after_hub(k);
			a.jmp(skip);
			a.bind(wait);
			// it waits: the block stops before it
			a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hlatch)), x86::rax);
			a.mov(stf(offsetof(p8x32a_jst, hs)), x86::ecx);
			a.mov(stf(offsetof(p8x32a_jst, hd)), x86::edx);
			a.mov(stf(offsetof(p8x32a_jst, hiss)), 1);
			a.lea(x86::rax, x86::ptr(res));
			a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hres)), x86::rax);
			a.mov(x86::esi, i);
			stop_before(k, x86::esi, true);
			a.bind(skip);
			goto end;
		}
		switch (op) {
		case 0: case 1: case 2: {
			// a lazy cog's hub read: it waits for the cog's slot (next_slot), which must come by st.tl
			Label late = a.new_label(), sb = a.new_label(), got = a.new_label(), on = a.new_label();
			a.lea(x86::rax, x86::ptr(T2, (int)(4 * k + 1)));
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, slot)));
			a.cmp(x86::rax, x86::rdi);
			a.jbe(sb);
			a.sub(x86::rax, x86::rdi);
			a.add(x86::rax, 15);
			a.and_(x86::rax, -16);
			a.add(x86::rax, x86::rdi);
			a.jmp(got);
			a.bind(sb);
			a.mov(x86::rax, x86::rdi);
			a.bind(got);
			a.lea(x86::rdi, x86::ptr(x86::rax, 2));
			a.cmp(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tl)));
			a.jbe(on);
			a.bind(late);
			a.mov(x86::esi, i);
			stop_before(k, x86::esi);
			a.bind(on);
			// a long with a journal entry: the interpreter reads it
			a.mov(x86::esi, x86::ecx);
			a.and_(x86::esi, 0xFFFF);
			a.shr(x86::esi, 2);
			a.mov(x86::r11, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, jmap)));
			a.bt(x86::dword_ptr(x86::r11), x86::esi);
			a.jc(late);
			// the next instruction starts at latch + 7
			a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, latch)), x86::rax);
			a.lea(T2, x86::ptr(x86::rax, (int)(7 - 4 * (k + 1))));
			a.and_(x86::ecx, 0xFFFF);
			a.mov(x86::rdi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, hub)));
			if (op == 0) a.movzx(x86::eax, x86::byte_ptr(x86::rdi, x86::rcx));
			else if (op == 1) { a.and_(x86::ecx, 0xFFFE); a.movzx(x86::eax, x86::word_ptr(x86::rdi, x86::rcx)); }
			else { a.and_(x86::ecx, 0xFFFC); a.mov(x86::eax, x86::dword_ptr(x86::rdi, x86::rcx)); }
			break;
		}
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
		if (jump && !outa) {
			// the word at the target is fetched before the write; the rest of the exit state after it
			a.mov(x86::esi, x86::ecx);
			a.and_(x86::esi, 511);
			a.mov(x86::esi, cogr(x86::esi));
			a.mov(stf(offsetof(p8x32a_jst, nix)), x86::esi);
		} else if (jump) {
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
		if (!dyn && fwr(i) && dst_of(i) == 0x1F4) {
			// a lazy cog's OUTA write: its time (this instruction's + 2) and value go to st's list
			a.mov(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, ot)));
			a.mov(x86::edi, stf(offsetof(p8x32a_jst, on)));
			a.lea(x86::r11, x86::ptr(T2, (int)(4 * k + 2)));
			a.mov(x86::qword_ptr(x86::rsi, x86::rdi, 3), x86::r11);
			a.mov(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, ov)));
			a.mov(x86::dword_ptr(x86::rsi, x86::rdi, 2), x86::eax);
			a.inc(stf(offsetof(p8x32a_jst, on)));
		}
		if (jump && !outa) {
			Label slow = a.new_label(), go = a.new_label();
			unsigned pc = (base + k + 1) & 511;
			bool fixed = imm && !dyn;
			// a jump that did not jump: its exit state, through the tail (it returns: the next instruction is cancelled)
			if (op != OP_JMP) {
				if (op == OP_DJNZ) a.cmp(x86::edx, 1);
				else a.test(x86::edx, x86::edx);
				if (op == OP_TJZ) a.je(go);
				else a.jne(go);
				a.mov(stf(offsetof(p8x32a_jst, s)), x86::ecx);
				a.mov(stf(offsetof(p8x32a_jst, d)), x86::edx);
				a.mov(x86::esi, x86::ecx);
				a.and_(x86::esi, 511);
				a.mov(stf(offsetof(p8x32a_jst, px)), x86::esi);
				a.mov(stf(offsetof(p8x32a_jst, jc)), 1);
				a.mov(stf(offsetof(p8x32a_jst, jmp)), 1);
				a.mov(stf(offsetof(p8x32a_jst, pc)), pc);
				if (dyn) a.mov(stf(offsetof(p8x32a_jst, w)), CURW);
				else a.mov(stf(offsetof(p8x32a_jst, w)), i);
				leave(k + 1);
				a.bind(go);
			}
			// s and d kept for the slow path (the chain uses eax, ecx, edx, rsi, rdi)
			a.mov(stf(offsetof(p8x32a_jst, s)), x86::ecx);
			a.mov(stf(offsetof(p8x32a_jst, d)), x86::edx);
			if (!fixed) a.and_(x86::ecx, 511);
			chain(k + 1, fixed ? (int)(src_of(i) & 511) : -1, !fixed || (src_of(i) & 511) < pc, NULL, slow);
			a.bind(slow);
			a.mov(x86::esi, stf(offsetof(p8x32a_jst, s)));
			a.and_(x86::esi, 511);
			a.mov(stf(offsetof(p8x32a_jst, px)), x86::esi);
			a.mov(stf(offsetof(p8x32a_jst, pc)), pc);
			if (dyn) a.mov(stf(offsetof(p8x32a_jst, w)), CURW);
			else a.mov(stf(offsetof(p8x32a_jst, w)), i);
			a.mov(stf(offsetof(p8x32a_jst, jmp)), 1);
			a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
			a.mov(x86::rsi, tail_ret);
			a.jmp(x86::rsi);
		} else if (jump) leave(k + 1);
		a.bind(skip);
	end:
		if (last) stop_before(k + 1, NEXTW);
		if (dyn) {
			Label over = a.new_label();
			a.jmp(over);
			a.bind(before);
			if (k == 0) {
				// a block reached from another's exit: the run so far ends with a sequential fetch of this slot
				a.mov(stf(offsetof(p8x32a_jst, pc)), base);
				a.mov(stf(offsetof(p8x32a_jst, px)), base);
				a.mov(stf(offsetof(p8x32a_jst, nix)), CURW);
				a.mov(stf(offsetof(p8x32a_jst, jmp)), 0);
				a.mov(stf(offsetof(p8x32a_jst, jc)), 0);
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
bool build_tail(P8Jit *j, bool lazy)
{
	CodeHolder code;
	code.init(j->rt.environment());
	x86::Assembler a(&code);
	FuncDetail func;
	FuncFrame frame;
	make_frame(func, frame, code.environment());
	frame.finalize();
	Label ret = a.new_label(), no_edge = a.new_label(), diff = a.new_label(), reset = a.new_label();
	const x86::Gp L = x86::rdi;

	a.add(TOTAL, x86::eax);
	a.lea(T2, x86::ptr(T2, x86::rax, 2));
	a.sub(BUDGET, x86::eax);
	if (lazy) {
		// a block that stopped before its first slot, at its time limit
		a.test(x86::eax, x86::eax);
		a.jz(ret);
	}
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
	if (lazy) {
		a.cmp(stf(offsetof(p8x32a_jst, on)), P8X32A_JOUT - P8X32A_JMAX);
		a.jae(ret);
	}
	a.mov(x86::ecx, stf(offsetof(p8x32a_jst, px)));
	a.cmp(x86::ecx, 511);
	a.je(ret);
	a.shl(x86::ecx, 5);
	a.add(x86::rcx, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, link)));
	a.mov(x86::eax, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, len)));
	a.cmp(x86::eax, BUDGET);
	a.ja(ret);
	a.mov(x86::edx, stf(offsetof(p8x32a_jst, nix)));
	a.mov(x86::eax, x86::edx);
	a.xor_(x86::edx, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, word)));
	a.and_(x86::edx, x86::dword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, mask)));
	a.jnz(ret);
	a.mov(stf(offsetof(p8x32a_jst, ix)), x86::eax);
	a.jmp(x86::qword_ptr(x86::rcx, (int)offsetof(p8x32a_jlink, body)));
	a.bind(ret);
	a.mov(stf(offsetof(p8x32a_jst, fl)), FL);
	a.mov(x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, t2)), T2);
	a.mov(stf(offsetof(p8x32a_jst, budget)), BUDGET);
	a.mov(x86::eax, TOTAL);
	a.emit_epilog(frame);
	void *fn = NULL;
	if (j->rt.add(&fn, &code) != kErrorOk) return false;
	if (lazy) {
		j->ltail = (uint64_t)(uintptr_t)fn;
		j->ltail_ret = j->ltail + code.label_offset(ret);
	} else {
		j->tail = (uint64_t)(uintptr_t)fn;
		j->tail_ret = j->tail + code.label_offset(ret);
	}
	return true;
}

// run_local's loop_edge search step for a backward jump to ecx with the next instruction at T2 (called from a block's
// exit): returns eax = 1 with st.edge = 1 when run_local must pass it to loop_edge; uses rax, rdx, rdi
bool build_edge(P8Jit *j)
{
	CodeHolder code;
	code.init(j->rt.environment());
	x86::Assembler a(&code);
	Label ret = a.new_label(), diff = a.new_label(), reset = a.new_label(), edge = a.new_label();
	const x86::Gp L = x86::rdi;
	a.mov(L, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, loop)));
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
	a.jmp(edge);
	a.bind(diff);
	a.cmp(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.jne(reset);
	a.cmp(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), P8X32A_PAT / 2);
	a.ja(reset);
	a.cmp(x86::edx, 0xFFFF);
	a.jne(ret);
	a.bind(reset);
	a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, head)), x86::cx);
	a.mov(x86::qword_ptr(L, (int)offsetof(p8x32a_loop, head_t)), x86::rax);
	a.mov(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, dirty)), 0);
	a.mov(x86::byte_ptr(L, (int)offsetof(p8x32a_loop, hub)), 0);
	a.mov(x86::word_ptr(L, (int)offsetof(p8x32a_loop, nins)), 0);
	a.bind(ret);
	a.xor_(x86::eax, x86::eax);
	a.ret();
	a.bind(edge);
	a.mov(stf(offsetof(p8x32a_jst, edge)), 1);
	a.mov(x86::eax, 1);
	a.ret();
	void *fn = NULL;
	if (j->rt.add(&fn, &code) != kErrorOk) return false;
	j->edge = (uint64_t)(uintptr_t)fn;
	return true;
}

} // namespace

extern "C" void *p8x32a_jit_new(void)
{
	P8Jit *j = new (std::nothrow) P8Jit();
	if (j && (!build_tail(j, false) || !build_tail(j, true) || !build_edge(j))) { delete j; j = NULL; }
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

extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var, int outa)
{
	P8Jit *j = (P8Jit *)jit;
	p8x32a_jblk *b = old;
	unsigned len = 0, k;
	bool hubslot;

	if (!b) {
		b = (p8x32a_jblk *)calloc(1, sizeof(*b));
		if (!b) return NULL;
		// out of memory: no block (the interpreter runs the code); nothing throws into C
		try {
			j->blocks.push_back(b);
		} catch (...) {
			free(b);
			return NULL;
		}
	} else if (b->fn) {
		j->rt.release(b->fn);
	}
	b->fn = NULL;
	b->len = 0;
	b->valid = 1;
	b->dyn = 0;
	b->part = outa != 0;
	b->words[0] = ix;
	// slot 0's S and D fields may change even when it is not translated (a block of none): jit_get compares the rest
	if (var[a]) b->dyn = 1;
	// the run: translatable words below the special registers, ending at an unconditional jump or a slot whose D field may point
	// anywhere; a slot whose op, flags or condition have changed is not translated
	while (len < P8X32A_JMAX && a + len < 0x1F0) {
		uint32_t w = len ? ram[a + len] : ix, v = var[a + len];
		bool dyn = v != 0;
		if ((v & ~P8X32A_JDYN) || !(op_ok(op_of(w)) || (op_of(w) <= 2 && !dyn))) break;
		if (!dyn && !supported(w, outa)) break;
		b->words[len] = w;
		if (dyn) b->dyn |= 1u << len;
		len++;
		hubslot = !outa && op_of(w) <= 2 && cond_of(w) != 0;
		if ((is_jump(op_of(w)) && cond_of(w) == 15) || (dyn && fwr(w)) || hubslot) break;
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
	Label res = as.new_label();
	bool writes = false;
	for (k = 0; k < len; k++) writes = writes || fwr(b->words[k]) || (b->dyn >> k & 1);
	Emit e = { as, frame, a, outa ? j->ltail : j->tail, outa ? j->ltail_ret : j->tail_ret, outa != 0, res, j->edge, writes };
	for (k = 0; k < len; k++) {
		bool last = k + 1 == len, next_dyn = !last && (b->dyn >> (k + 1) & 1);
		if (outa) {
			// a lazy cog's block runs while its instructions start by st.tl
			Label go = as.new_label();
			as.lea(x86::rsi, x86::ptr(T2, (int)(4 * k)));
			as.cmp(x86::rsi, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, tl)));
			as.jbe(go);
			if (b->dyn >> k & 1) e.stop_before(k, NEXTW);
			else {
				as.mov(x86::esi, b->words[k]);
				e.stop_before(k, x86::esi);
			}
			as.bind(go);
		}
		e.insn(k, b->words[k], (b->dyn >> k & 1) != 0, last, last || next_dyn);
	}
	// a block that ends with a hub read or write: an entry that completes it once due (run_local, EV_HUB)
	k = len - 1;
	bool hub = !outa && !(b->dyn >> k & 1) && op_of(b->words[k]) <= 2 && cond_of(b->words[k]) != 0;
	if (hub) {
		as.bind(res);
		as.emit_prolog(frame);
		as.emit_args_assignment(frame, args);
		as.mov(RAM, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, ram)));
		as.mov(FL, stf(offsetof(p8x32a_jst, fl)));
		as.xor_(TOTAL, TOTAL);
		as.mov(NEXTW, stf(offsetof(p8x32a_jst, nix)));
		as.mov(x86::ecx, stf(offsetof(p8x32a_jst, hs)));
		as.mov(x86::edx, stf(offsetof(p8x32a_jst, hd)));
		as.mov(x86::rax, x86::qword_ptr(ST, (int)offsetof(p8x32a_jst, latch)));
		e.hub_access(b->words[k]);
		// counted as one instruction of a run that goes on at the next address
		e.after_hub(0);
		e.seq_exit(1, (a + len) & 511, NEXTW);
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
extern "C" p8x32a_jblk *p8x32a_jit_build(void *jit, p8x32a_jblk *old, unsigned a, uint32_t ix, const uint32_t *ram, const uint32_t *var, int outa)
{
	(void)jit; (void)old; (void)a; (void)ix; (void)ram; (void)var; (void)outa;
	return NULL;
}
#endif
