#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic"
mkdir -p $B
fail=0
cc -std=c99 -pedantic-errors -Werror=declaration-after-statement -Wno-long-long -fsyntax-only $S/wpc/pinheck/board.c || { echo "C FAIL board.c"; fail=$((fail + 1)); }
cc $CF -I$S/wpc/pinheck -o $B/board_test board_test.c $S/wpc/pinheck/board.c || exit 2
./$B/board_test || fail=$((fail + 1))
printf '#include "libpinmame.h"\n#include "gen.h"\nstatic_assert(PINMAME_HARDWARE_GEN_PINHECK == GEN_PINHECK, "PINHECK");\n' > $B/gen.cpp
c++ -std=c++17 -fsyntax-only -I$S/libpinmame -I$S/wpc -I$S $B/gen.cpp || { echo "GEN FAIL: libpinmame's PINMAME_HARDWARE_GEN lacks PINHECK"; fail=$((fail + 1)); }
python3 check5.py selftest || fail=$((fail + 1))
echo "board: $fail failed"
[ $fail -eq 0 ]
