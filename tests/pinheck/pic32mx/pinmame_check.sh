#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms || exit 2
(cd "$PINHECK_UPDATE_DIR" && zip -q -j "$OLDPWD/$B/roms/dominos.zip" DOM_V006.PRG PRP_V008.BIN) || exit 2
(cd $B && PINHECK_UART1_LOG=$PWD/uart1.log "$SDL3PINMAME" dominos -rompath roms -headless -frames_to_run 180 -skip_gamewarnings -nothrottle > run.out 2>&1) || { echo "PINMAME FAIL: sdl3pinmame exited $?"; tail -5 $B/run.out; exit 1; }
if grep -q "PROPELLER SYNC CHECK\.\.\." $B/uart1.log; then echo "pinmame: ok"; exit 0; fi
echo "PINMAME FAIL: UART1 log lacks 'PROPELLER SYNC CHECK...':"
cat -v $B/uart1.log
exit 1
