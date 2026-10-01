# The game a machine check runs, sourced by the checks: PINHECK_GAME (default dominos), whose romset is
# PINHECK_ZIP and whose unzipped update is PINHECK_UPDATE_DIR.
#   PRG         the PIC32 image in the update
#   PROGRAMMED  bytes the first-boot update flashes
#   UPDATED_AT  emulated seconds by which the Propeller shows PLEASE RESTART on a first boot
#   STORED      the version word the firmware prints after the sync check (version << 24 | $BAFA, hex)
#   BANNER      the UART1 banner lines checked (| separated)
#   CLIP        a .VID clip the display check plays ([V00<CLIP>])
#   SIM         the simulator check (tests/pinheck/sim)
GAME=${PINHECK_GAME:-dominos}
case $GAME in
dominos) PRG=DOM_V006.PRG PROGRAMMED=204288 UPDATED_AT=138 STORED=600BAFA CLIP=LT5 SIM=sim.py
	BANNER='pinHeck System 2011-2016|Game: DOM - DOMINOS|Version: 006' ;;
rzspook) PRG=RZO_V026.PRG PROGRAMMED=309248 UPDATED_AT=184 STORED=1A00BAFA CLIP=DMB SIM=rzsim.py
	BANNER='pinHeck System 2011-2016|Game: RZO - SPOOK SHOW' ;;
*) echo "PINHECK_GAME: no machine checks for '$GAME'"; exit 2 ;;
esac
