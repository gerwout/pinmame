# mips32 core tests

`./check.sh` runs everything; `SEEDS=N ./check.sh` changes the random corpus size (default 200). `./bench.sh` reports interpreter throughput.

Needs `clang`, `ld.lld` and `qemu-mipsel` (`apt install clang lld qemu-user`).

- `diff/*.S` and generated `build/random/*.S` run under `qemu-mipsel -cpu 4KEc` and under `build/mips32run`; the bytes each writes to stdout must match. `cmpdump.py` names the differing register.
- `golden/*.S` cover exceptions, CP0 and EIC, which QEMU user mode cannot referee. They run under `mips32run -x -e` and are compared with `golden/*.expect`.
- `unit_test.c` drives the core API cycle by cycle; `dasm_test.c` checks the disassembler.
- `run.c` loads an ELF, services the Linux `write`/`exit` syscalls, and exposes two test registers: a write to `0xA0FFFFF0` sets the EIC input (`ripl | vector << 8 | srs << 16`), a write to `0xA0FFFFF4` clears the latched timer interrupt.
