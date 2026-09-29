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
