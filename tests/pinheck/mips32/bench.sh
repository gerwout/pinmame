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
