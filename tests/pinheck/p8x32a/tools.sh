#!/bin/sh
cd "$(dirname "$0")" || exit 2
TOOLS=${TOOLS:-$PWD/build/tools}
HERE=$PWD
mkdir -p "$TOOLS"

fetch() {
	[ -d "$TOOLS/$1/.git" ] || git clone -q "$2" "$TOOLS/$1" || exit 2
	git -C "$TOOLS/$1" checkout -q "$3" || exit 2
}

fetch openspin https://github.com/parallaxinc/OpenSpin.git d1991aa821179209cbd21396dfd1d88f226eb99d
fetch spinsim https://github.com/parallaxinc/spinsim.git 77f7331974de88e18c69a90f450feb70a979979c
fetch p1 https://github.com/parallaxinc/Propeller_1_Design.git 499b248c62bf09191ff11ba5f762fd68d13151fd

make -C "$TOOLS/openspin" > "$TOOLS/openspin.log" 2>&1 || { echo "openspin build failed, see $TOOLS/openspin.log"; exit 2; }
grep -q SPINSIM_DUMP "$TOOLS/spinsim/spinsim.c" || (cd "$TOOLS/spinsim" && sed -i 's/\r$//' spinsim.c && patch -s spinsim.c < "$HERE/spinsim-dump.patch") || exit 2
make -C "$TOOLS/spinsim" > "$TOOLS/spinsim.log" 2>&1 || { echo "spinsim build failed, see $TOOLS/spinsim.log"; exit 2; }
R=$TOOLS/p1/P8X32A_DE2_115
D=$HERE/../../../src/wpc/pinheck
[ -f "$HERE/rtl/tb.cpp" ] && [ -f "$D/eeprom.c" ] || { echo "tools ready in $TOOLS (p1rtl needs rtl/tb.cpp and eeprom.c)"; exit 0; }
rm -rf "$TOOLS/p1rtl"
verilator --cc --exe --build -j 8 -O3 -Wno-fatal --x-initial 0 --x-assign 0 --public-flat-rw -I"$R" \
	-CFLAGS "-O2 -I$D" -LDFLAGS -lz "$R/dig.v" "$HERE/rtl/tb.cpp" "$D/eeprom.c" "$D/sd.c" "$D/vfat.c" "$D/zipsrc.c" --Mdir "$TOOLS/p1rtl" -o p1rtl \
	> "$TOOLS/p1rtl.log" 2>&1 || { echo "p1rtl build failed, see $TOOLS/p1rtl.log"; exit 2; }
echo "tools ready in $TOOLS"
