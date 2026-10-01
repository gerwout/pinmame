// the translator reports running out of memory as no block instead of throwing into C
#include "p8x32ajit.h"
#include <cstdio>
#include <cstdlib>
#include <new>

static bool fail_new;

void *operator new(std::size_t n)
{
	void *p = fail_new ? NULL : std::malloc(n ? n : 1);
	if (!p) throw std::bad_alloc();
	return p;
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

int main()
{
	static uint32_t ram[512], var[512];
	void *jit = p8x32a_jit_new();
	p8x32a_jblk *b = (p8x32a_jblk *)1;
	int bad = 0;
	if (!jit) { std::printf("jit_oom: no translator\n"); return 1; }
	fail_new = true;
	try {
		b = p8x32a_jit_build(jit, NULL, 0, 0xA0BC0000u, ram, var, 0);
	} catch (...) {
		bad = 1;
	}
	fail_new = false;
	if (bad || b) std::printf("jit_oom: FAIL (%s)\n", bad ? "threw" : "returned a block");
	else std::printf("jit_oom: ok\n");
	p8x32a_jit_free(jit);
	return bad || b;
}
