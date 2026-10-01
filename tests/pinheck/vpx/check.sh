#!/bin/sh
# VPX support without the firmware: Domino's names, the pinHeck sources in every build list, the display size
# libpinmame exports, the media check's frame rule on synthetic runs, and (with LIBPINMAME and PINMAME_ROMS)
# libpinmame's display list and getters on a segment, a DMD and a video game
[ -z "$LIBPINMAME" ] || LIBPINMAME=$(realpath "$LIBPINMAME") || exit 2
[ -z "$PINMAME_ROMS" ] || PINMAME_ROMS=$(realpath "$PINMAME_ROMS") || exit 2
cd "$(dirname "$0")" || exit 2
S=../../../src
B=build
mkdir -p $B || exit 2
fail=0
python3 names.py check $S || fail=$((fail + 1))
python3 names.py check $S rzspook || fail=$((fail + 1))
python3 names.py check $S jetsons || fail=$((fail + 1))
if command -v x86_64-w64-mingw32-gcc > /dev/null; then python3 mingw_check.py $S/libpinmame/libpinmame.cpp x86_64-w64-mingw32-gcc || fail=$((fail + 1)); else echo "vpx: MinGW compiler missing, strcasecmp check skipped"; fi
printf '#include "pinheck_names.h"\nint main(void) { return pinheck_dominos_switch_names[0].num != 1 || pinheck_rzspook_switch_names[0].num != 1 || pinheck_jetsons_switch_names[0].num != 1; }\n' > $B/names.c
cc -std=c89 -pedantic-errors -Wall -Wextra -Werror -I$S/wpc $B/names.c -o $B/names && ./$B/names || { echo "NAMES FAIL: pinheck_names.h"; fail=$((fail + 1)); }
# per-game data: Domino's values only for the system set and Domino's; every other game spells out its own
dd=$(grep -l 'PINHECK_DOMINOS_DATA' $S/wpc/*.c $S/wpc/sims/pinheck/*.c | sed 's|.*/src/||' | tr '\n' ' ')
if [ "$dd" = "wpc/pinheckgames.c wpc/sims/pinheck/dominos.c " ] && grep -q 'define INIT_PINHECK(name, balls, version, data)' $S/wpc/pinheckgames.c &&
	! grep -q 'PINHECK_GAME_DEFAULTS' $S/wpc/*.[ch] $S/wpc/sims/pinheck/*.c; then
	echo "gamedata: Domino's values only in the system set and dominos.c ($(grep -l 'pinheck_tGameData' $S/wpc/sims/pinheck/*.c | wc -l) games defined)"
else
	echo "GAMEDATA FAIL: Domino's values (PINHECK_DOMINOS_DATA) in '$dd', or INIT_PINHECK without a data argument"; fail=$((fail + 1))
fi
for g in rzspook jetsons; do
	python3 names.py vbs $S $g | head -1 | grep -q "$g.vbs" && echo "vbs: the $g script names $g.vbs" || { echo "VBS FAIL: the $g script does not name $g.vbs"; fail=$((fail + 1)); }
done
srcs() { grep -o 'src/\(wpc/pinheck\|wpc/sims/pinheck\|cpu/mips32\|cpu/pic32mx\|cpu/p8x32a\)[^ )"]*' "$1" | sort -u; }
srcs ../../../cmake/libpinmame/CMakeLists.txt > $B/srcs.ref
n=0
for f in ../../../cmake/*/CMakeLists*.txt; do
	case $f in *instvpm*) continue ;; esac
	srcs "$f" | cmp -s - $B/srcs.ref || { echo "BUILD FAIL: $f lists other pinHeck sources than cmake/libpinmame"; fail=$((fail + 1)); }
	n=$((n + 1))
done
echo "builds: $n build lists carry the same $(wc -l < $B/srcs.ref) pinHeck sources"
python3 vpx.py selftest $B/selftest || fail=$((fail + 1))
scale() { cc -E -dM -x c "$@" -I$S -I$S/wpc -I$S/unix -I$S/unix/sysdep $S/wpc/pinheck.h | sed -n 's/^#define PINHECK_VIDEO_SCALE //p'; }
if [ "$(scale)" = 2 ] && [ "$(scale -DLIBPINMAME)" = 1 ]; then
	echo "scale: 2x2 dots in PinMAME and VPinMAME windows, the panel (128x32, The Jetsons' 128x64) as sent to libpinmame hosts"
else
	echo "SCALE FAIL: PINHECK_VIDEO_SCALE is $(scale) without LIBPINMAME and $(scale -DLIBPINMAME) with it"; fail=$((fail + 1))
fi
if [ -n "$LIBPINMAME" ] && [ -n "$PINMAME_ROMS" ]; then
	c++ -std=c++20 -O1 -Wall -Wextra -Werror -I$S/libpinmame host.cpp "$LIBPINMAME" -Wl,-rpath,"$(dirname "$LIBPINMAME")" -pthread -o $B/host || exit 2
	# the host's message API from two threads, under ThreadSanitizer where the compiler has it
	if c++ -std=c++20 -O1 -g -fsanitize=thread -Wall -Wextra -Werror -I$S/libpinmame host.cpp "$LIBPINMAME" -Wl,-rpath,"$(dirname "$LIBPINMAME")" -pthread -o $B/host_tsan 2> /dev/null; then
		TSAN_OPTIONS=exitcode=66 ./$B/host_tsan -T > $B/tsan.out 2>&1 && head -1 $B/tsan.out || { echo "VPX FAIL: ThreadSanitizer: $(grep -m1 WARNING $B/tsan.out)"; fail=$((fail + 1)); }
	else
		echo "vpx: no ThreadSanitizer: the host's message API not run from two threads"
	fi
	run() {
		rm -rf $B/$1 && mkdir -p $B/$1/nvram $B/$1/cfg && ln -s "$PINMAME_ROMS" $B/$1/roms || exit 2
		(cd $B/$1 && timeout -k 30 300 ../host -x $2 $1 180 . > run.out 2>&1) || { echo "VPX FAIL: $1 exited $?"; fail=$((fail + 1)); }
	}
	run pb_l5            # outputs as a legacy host reads them
	run tz_94h -P        # physical outputs
	run babypac -P
	python3 vpx.py displays $B/pb_l5 $B/tz_94h $B/babypac || fail=$((fail + 1))
	python3 vpx.py probe $B/pb_l5 $B/tz_94h $B/babypac || fail=$((fail + 1))
	# two sessions through the plugin message API, as VPX's reset does
	rm -rf $B/restart && mkdir -p $B/restart/nvram $B/restart/cfg && ln -s "$PINMAME_ROMS" $B/restart/roms || exit 2
	(cd $B/restart && timeout -k 30 300 ../host -p -R tz_94h 180 . > run.out 2>&1) || { echo "VPX FAIL: restart exited $?"; fail=$((fail + 1)); }
	python3 vpx.py sessions $B/restart || fail=$((fail + 1))
else
	echo "vpx: LIBPINMAME or PINMAME_ROMS not set: libpinmame's display list and getters not run"
fi
echo "vpx: $fail failed"
[ $fail -eq 0 ]
