#!/bin/sh
# VPX support without the firmware: Domino's names, the pinHeck sources in every build list, the display size
# libpinmame exports, and (with LIBPINMAME and PINMAME_ROMS) libpinmame's display list and getters on a
# segment, a DMD and a video game
[ -z "$LIBPINMAME" ] || LIBPINMAME=$(realpath "$LIBPINMAME") || exit 2
[ -z "$PINMAME_ROMS" ] || PINMAME_ROMS=$(realpath "$PINMAME_ROMS") || exit 2
cd "$(dirname "$0")" || exit 2
S=../../../src
B=build
mkdir -p $B || exit 2
fail=0
python3 names.py check $S || fail=$((fail + 1))
printf '#include "pinheck_names.h"\nint main(void) { return pinheck_dominos_switch_names[0].num != 1; }\n' > $B/names.c
cc -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$S/wpc $B/names.c -o $B/names && ./$B/names || { echo "NAMES FAIL: pinheck_names.h"; fail=$((fail + 1)); }
srcs() { grep -o 'src/\(wpc/pinheck\|wpc/sims/pinheck\|cpu/mips32\|cpu/pic32mx\|cpu/p8x32a\)[^ )"]*' "$1" | sort -u; }
srcs ../../../cmake/libpinmame/CMakeLists.txt > $B/srcs.ref
n=0
for f in ../../../cmake/*/CMakeLists*.txt; do
	case $f in *instvpm*) continue ;; esac
	srcs "$f" | cmp -s - $B/srcs.ref || { echo "BUILD FAIL: $f lists other pinHeck sources than cmake/libpinmame"; fail=$((fail + 1)); }
	n=$((n + 1))
done
echo "builds: $n build lists carry the same $(wc -l < $B/srcs.ref) pinHeck sources"
scale() { cc -E -dM -x c "$@" -I$S -I$S/wpc -I$S/unix -I$S/unix/sysdep $S/wpc/pinheck.h | sed -n 's/^#define PINHECK_VIDEO_SCALE //p'; }
if [ "$(scale)" = 2 ] && [ "$(scale -DLIBPINMAME)" = 1 ]; then
	echo "scale: 2x2 dots in PinMAME and VPinMAME windows, the 128x32 panel as sent to libpinmame hosts"
else
	echo "SCALE FAIL: PINHECK_VIDEO_SCALE is $(scale) without LIBPINMAME and $(scale -DLIBPINMAME) with it"; fail=$((fail + 1))
fi
if [ -n "$LIBPINMAME" ] && [ -n "$PINMAME_ROMS" ]; then
	c++ -std=c++20 -O1 -Wall -Wextra -Werror -I$S/libpinmame host.cpp "$LIBPINMAME" -Wl,-rpath,"$(dirname "$LIBPINMAME")" -pthread -o $B/host || exit 2
	run() {
		rm -rf $B/$1 && mkdir -p $B/$1/nvram $B/$1/cfg && ln -s "$PINMAME_ROMS" $B/$1/roms || exit 2
		(cd $B/$1 && timeout -k 30 300 ../host -x $2 $1 180 . > run.out 2>&1) || { echo "VPX FAIL: $1 exited $?"; fail=$((fail + 1)); }
	}
	run pb_l5            # outputs as a legacy host reads them
	run tz_94h -P        # physical outputs
	run babypac -P
	python3 vpx.py displays $B/pb_l5 $B/tz_94h $B/babypac || fail=$((fail + 1))
	python3 vpx.py probe $B/pb_l5 $B/tz_94h $B/babypac || fail=$((fail + 1))
else
	echo "vpx: LIBPINMAME or PINMAME_ROMS not set: libpinmame's display list and getters not run"
fi
echo "vpx: $fail failed"
[ $fail -eq 0 ]
