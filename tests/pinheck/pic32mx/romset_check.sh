#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/romset
rm -rf $B && mkdir -p $B/bios $B/nomedia $B/nvram || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j bios/pinheck.zip p8x32a.rom && cp bios/pinheck.zip nomedia/ && rm p8x32a.rom) || exit 2
(cd "$PINHECK_UPDATE_DIR" && zip -q -0 "$OLDPWD/$B/nomedia/dominos.zip" DOM_V006.PRG PRP_V008.BIN) || exit 2
fail=0
run() {
	(cd $B && timeout 600 "$SDL3PINMAME" "$1" -rompath "$2" -nvram_directory nvram -headless -frames_to_run 120 -skip_gamewarnings -nothrottle > "$3.out" 2>&1)
	echo $?
}
rc=$(run pinheck bios bios)
if [ "$rc" -gt 128 ] || ! grep -qF "pinheck: 'pinheck' is the pinHeck system set" $B/bios.out; then
	echo "ROMSET FAIL: 'pinheck' alone exited $rc"; tail -3 $B/bios.out; fail=1
fi
rc=$(run dominos nomedia nomedia)
if [ "$rc" -gt 128 ] || ! grep -qF "pinheck: the SD card from dominos.zip has no DMD/ and no SFX/" $B/nomedia.out; then
	echo "ROMSET FAIL: dominos without media exited $rc"; tail -3 $B/nomedia.out; fail=1
fi
[ $fail -eq 0 ] && echo "romset: ok"
exit $fail
