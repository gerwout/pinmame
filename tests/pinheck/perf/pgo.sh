#!/bin/sh
# Profile-guided sdl3pinmame (GCC or Clang). Run from the repository root with cmake/sdl3pinmame/CMakeLists.txt
# copied to CMakeLists.txt: build/pgo-gen is instrumented and trained on bench.sh's attract and video workloads,
# then build/pgo-use is built with the profile in build/pgo-profile. Arguments pass to cmake (-DPLATFORM=linux ...).
# Needs P8X32A_ROM and PINHECK_ZIP as bench.sh does; PGO_JOBS (default 6) builds in parallel, LLVM_PROFDATA names
# Clang's llvm-profdata.
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
grep -q 'project(sdl3pinmame' CMakeLists.txt 2> /dev/null || { echo "pgo: run from the repository root with cmake/sdl3pinmame/CMakeLists.txt copied to CMakeLists.txt"; exit 2; }
P=$PWD/build/pgo-profile
J=${PGO_JOBS:-6}
mkdir -p build && rm -rf "$P"
for m in gen use; do
	M=$(echo $m | tr a-z A-Z)
	cmake -S . -B build/pgo-$m -DCMAKE_BUILD_TYPE=Release -DPINMAME_PGO=$M -DPINMAME_PGO_DIR="$P" "$@" > build/pgo-$m.cmake.log 2>&1 || { tail -20 build/pgo-$m.cmake.log; exit 1; }
	# the profile is no dependency of the objects: the build with it starts clean
	c=
	[ $m = use ] && c=--clean-first
	cmake --build build/pgo-$m -j$J $c > build/pgo-$m.log 2>&1 || { tail -20 build/pgo-$m.log; exit 1; }
	[ $m = use ] && break
	SDL3PINMAME=build/pgo-gen/sdl3pinmame tests/pinheck/perf/bench.sh attract video > build/pgo-train.log 2>&1
	st=$?
	sed 's/^/pgo training: /' build/pgo-train.log
	[ $st -eq 0 ] || { echo "pgo: the training run failed"; exit 1; }
	if ls "$P"/*.profraw > /dev/null 2>&1; then
		${LLVM_PROFDATA:-llvm-profdata} merge -o "$P/default.profdata" "$P"/*.profraw || exit 1
	fi
	[ -n "$(ls -A "$P" 2> /dev/null)" ] || { echo "pgo: the training run wrote no profile to $P"; exit 1; }
done
echo "pgo: build/pgo-use/sdl3pinmame"
