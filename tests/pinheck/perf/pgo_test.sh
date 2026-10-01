#!/bin/sh
# cmake/pgo.cmake on a small project (a static library and a program that links it), and pgo.sh with stubs.
cd "$(dirname "$0")" || exit 2
R=$(cd ../../.. && pwd)
T=$PWD/build/pgo_test
rm -rf "$T" && mkdir -p "$T/src" || exit 2
fail=0
cat > "$T/src/CMakeLists.txt" <<CM
cmake_minimum_required(VERSION 3.16)
project(pgotest C)
include($R/cmake/pgo.cmake)
add_library(lib STATIC lib.c)
pinmame_enable_pgo(lib)
add_executable(prog main.c)
target_link_libraries(prog PRIVATE lib)
CM
echo 'int f(int x) { return x & 1 ? 3 * x + 1 : x / 2; }' > "$T/src/lib.c"
printf '#include <stdio.h>\nint f(int);\nint main(void) { int i, s = 0; for (i = 0; i < 1000; i++) s += f(i); printf("%%d\\n", s); return 0; }\n' > "$T/src/main.c"

# build NAME COMPILER MODE [cmake arguments]: configure and build $T/NAME
build() {
	n=$1 cc=$2 m=$3
	shift 3
	cmake -S "$T/src" -B "$T/$n" -DCMAKE_C_COMPILER=$cc -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DPINMAME_PGO=$m -DPINMAME_PGO_DIR="$T/prof-$cc" "$@" > "$T/$n.log" 2>&1 && cmake --build "$T/$n" >> "$T/$n.log" 2>&1
}

for cc in gcc clang; do
	command -v $cc > /dev/null || { echo "pgo: $cc missing, skipped"; continue; }
	echo 'int f(int x) { return x & 1 ? 3 * x + 1 : x / 2; }' > "$T/src/lib.c"
	# a program links the instrumented static library
	if build gen-$cc $cc GEN && "$T/gen-$cc/prog" > /dev/null; then
		[ $cc = clang ] && llvm-profdata merge -o "$T/prof-$cc/default.profdata" "$T/prof-$cc"/*.profraw
		[ -n "$(ls "$T/prof-$cc")" ] || { echo "PGO FAIL $cc: the instrumented program wrote no profile"; fail=1; }
	else echo "PGO FAIL $cc: the program does not link against the instrumented static library"; tail -5 "$T/gen-$cc.log"; fail=1; fi
	build use-$cc $cc USE || { echo "PGO FAIL $cc: the build with the profile fails"; tail -5 "$T/use-$cc.log"; fail=1; }
	# a stale profile warns
	echo 'int f(int x) { int i, r = 0; for (i = 0; i < x; i++) r += i & 3; return r; }' > "$T/src/lib.c"
	cmake --build "$T/use-$cc" >> "$T/use-$cc.log" 2>&1 || { echo "PGO FAIL $cc: the build with a stale profile fails"; tail -5 "$T/use-$cc.log"; fail=1; }
done
# a GCC without -fprofile-prefix-path gets no -fprofile-prefix-path
build noprefix gcc GEN -DPINMAME_PGO_HAVE_PREFIX_PATH=0 || { echo "PGO FAIL: the build without -fprofile-prefix-path fails"; tail -5 "$T/noprefix.log"; fail=1; }
grep -q 'fprofile-prefix-path' "$T/noprefix/compile_commands.json" && { echo "PGO FAIL: -fprofile-prefix-path used where the compiler lacks it"; fail=1; }
# clang-cl gets Clang's options, not MSVC's (configured only: no Windows libraries here)
if command -v clang-cl > /dev/null && command -v lld-link > /dev/null && command -v llvm-lib > /dev/null; then
	for m in GEN USE; do
		cmake -S "$T/src" -B "$T/cl-$m" -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=clang-cl -DCMAKE_LINKER=lld-link \
			-DCMAKE_AR=llvm-lib -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
			-DPINMAME_PGO=$m -DPINMAME_PGO_DIR="$T/prof-cl" > "$T/cl-$m.log" 2>&1 || { echo "PGO FAIL clang-cl $m: configure fails"; tail -5 "$T/cl-$m.log"; fail=1; continue; }
		flags=$(cat "$T/cl-$m/compile_commands.json"; find "$T/cl-$m" -name link.txt -exec cat {} +)
		echo "$flags" | grep -q 'PROFILE:PGD' && { echo "PGO FAIL clang-cl $m: MSVC's profile options"; fail=1; }
		echo "$flags" | grep -q -- "-fprofile-$(echo $m | sed 's/GEN/generate/; s/USE/use/')" || { echo "PGO FAIL clang-cl $m: no -fprofile option"; fail=1; }
		[ $m = USE ] || echo "$flags" | grep -q 'LIBPATH:.*/lib/windows' || { echo "PGO FAIL clang-cl GEN: no /LIBPATH to clang_rt.profile.lib"; fail=1; }
	done
else echo "pgo: clang-cl, lld-link or llvm-lib missing, skipped"; fi

# pgo.sh: a failed training run stops it, and the build with the profile is a clean one
mkdir -p "$T/repo/tests/pinheck/perf" "$T/bin" || exit 2
cp pgo.sh "$T/repo/tests/pinheck/perf/" || exit 2
echo 'project(sdl3pinmame)' > "$T/repo/CMakeLists.txt"
printf '#!/bin/sh\necho "$@" >> "%s/cmake.log"\n' "$T" > "$T/bin/cmake"
printf '#!/bin/sh\nmkdir -p build/pgo-profile && echo p > build/pgo-profile/x.gcda\necho "bench run"\nexit $STUB_BENCH_EXIT\n' > "$T/repo/tests/pinheck/perf/bench.sh"
chmod +x "$T/bin/cmake" "$T/repo/tests/pinheck/perf/bench.sh"
for e in 1 0; do
	: > "$T/cmake.log"
	(cd "$T/repo" && PATH="$T/bin:$PATH" STUB_BENCH_EXIT=$e P8X32A_ROM=x PINHECK_ZIP=x sh tests/pinheck/perf/pgo.sh > "$T/pgo$e.out" 2>&1)
	st=$?
	if [ $e = 1 ]; then
		[ $st -ne 0 ] && ! grep -q 'pgo-use' "$T/cmake.log" || { echo "PGO FAIL pgo.sh: a failed training run is not an error"; cat "$T/pgo$e.out"; fail=1; }
	else
		[ $st -eq 0 ] && grep -q -- '--build build/pgo-use.*--clean-first' "$T/cmake.log" || { echo "PGO FAIL pgo.sh: the build with the profile is not a clean build"; cat "$T/cmake.log"; fail=1; }
	fi
done
[ $fail -eq 0 ] && echo "pgo: static library, stale profile, flag checks, clang-cl and pgo.sh ok"
[ -n "$PGO_TEST_KEEP" ] || rm -rf "$T"
exit $fail
