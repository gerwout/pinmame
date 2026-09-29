#!/bin/sh
cd "$(dirname "$0")/../../.." || exit 2
B=tests/pinheck/pic32mx/build/mklink
rm -rf $B && mkdir -p $B
objs=$(sed -n '/findstring PIC32MX@/,/^else/p' src/rules.mak | sed -n 's/^CPUOBJS += //p')
[ -n "$objs" ] || { echo "makefile link: no PIC32MX CPUOBJS in src/rules.mak"; exit 1; }
drv=$(sed -n 's/^DRVLIBS += //p' src/pinmame.mak | tr ' ' '\n' | grep -E 'pinheck|p8x32a')
for o in $objs $drv; do
	src=$(echo "$o" | sed 's|\$(PINOBJ)/|src/wpc/|; s|\$(OBJ)/|src/|; s|\.o$|.c|')
	cc -c -std=gnu99 -DPINMAME -DMAMEVER=7300 -DLSB_FIRST '-DINLINE=static __inline__' -DPI=M_PI -DHAS_PIC32MX=1 -DHAS_CUSTOM=1 -Isrc -Isrc/wpc -Isrc/unix -Isrc/unix/sysdep -o $B/$(echo "$src" | tr / _ | sed 's/\.c$/.o/') "$src" || { echo "makefile link: cannot compile $src"; exit 1; }
done
nm --defined-only $B/*.o | awk 'NF == 3 { print $3 }' | sort -u > $B/defined
nm -u $B/*.o | awk '{ print $NF }' | grep -E '^(mips32_|pic32mx_|pinheck_board_|pic32cpu_|prop_|cat24m01_|p8x32a_|sd_|vfat_|zipsrc_)' | sort -u > $B/needed
missing=$(comm -23 $B/needed $B/defined)
if [ -n "$missing" ]; then echo "makefile link: FAIL, pinHeck objects leave undefined: $missing"; exit 1; fi
echo "makefile link: ok"
