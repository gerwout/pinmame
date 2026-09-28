#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src/wpc/pinheck
CC="cc -O2 -std=c99 -Wall -Wextra -Werror -pedantic -I$S"
fail=0
mkdir -p $B
for c in $S/sd.c $S/vfat.c $S/zipsrc.c; do
	[ -e "$c" ] || continue
	cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only -I$S "$c" || { echo "C89 FAIL $c"; fail=$((fail + 1)); }
done
python3 mkzip.py $B/test.zip || exit 2
if [ -f zipsrc_test.c ]; then
	$CC -o $B/zipsrc_test zipsrc_test.c $S/zipsrc.c -lz || exit 2
	./$B/zipsrc_test $B/test.zip || fail=$((fail + 1))
	python3 mkzip.py --prefix dominos/ $B/prefixed.zip || exit 2
	./$B/zipsrc_test $B/prefixed.zip || { echo "PREFIX FAIL: entries under one folder are not presented as the flat zip's"; fail=$((fail + 1)); }
fi
if [ -f vfat_test.c ]; then
	$CC -o $B/vfat_test vfat_test.c $S/vfat.c || exit 2
	./$B/vfat_test || fail=$((fail + 1))
	$CC -o $B/vfatimg vfatimg.c $S/vfat.c $S/zipsrc.c -lz || exit 2
	./$B/vfatimg $B/test.zip volume $B/flat.img > /dev/null && ./$B/vfatimg $B/prefixed.zip volume $B/prefixed.img > /dev/null &&
		cmp -s $B/flat.img $B/prefixed.img || { echo "PREFIX FAIL: a zip under one folder gives a different card"; fail=$((fail + 1)); }
	for z in $B/test.zip ${DOMINOS_ZIP:+"$DOMINOS_ZIP"}; do
		./$B/vfatimg "$z" volume $B/vol.img > /dev/null || { fail=$((fail + 1)); continue; }
		if fsck.vfat -n $B/vol.img > $B/fsck.log 2>&1; then echo "fsck: clean ($z)"; else echo "FSCK FAIL $z"; cat $B/fsck.log; fail=$((fail + 1)); fi
		rm -rf $B/extract && mkdir $B/extract
		MTOOLS_SKIP_CHECK=1 mcopy -s -n -i $B/vol.img '::/*' $B/extract/ 2> /dev/null
		python3 cmpzip.py "$z" $B/extract || fail=$((fail + 1))
	done
fi
if [ -f sd_test.c ]; then
	$CC -o $B/sd_test sd_test.c $S/sd.c || exit 2
	./$B/sd_test || fail=$((fail + 1))
fi
echo "storage: $fail failed"
[ $fail -eq 0 ]
