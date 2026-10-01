# The game a machine check runs, sourced by the checks: PINHECK_GAME (default dominos), whose romset is
# PINHECK_ZIP and whose unzipped update is PINHECK_UPDATE_DIR.
#   PRG         the PIC32 image in the update
#   PRP         the Propeller image in the update
#   PROGRAMMED  bytes the first-boot update flashes
#   UPDATE_END  how the update hands the PIC32 back: leave (STK500v2 LEAVE_PROGMODE), or stopped in
#               programming mode (the Propeller asks for a restart and the PIC32 waits for it)
#   REBOOTS     Propeller reboots (CLKSET $80) before the first PIC32 sync of a normal start
#   UPDATED_AT  emulated seconds by which the Propeller shows PLEASE RESTART on a first boot
#   STORED      the version word the firmware prints after the sync check (version << 24 | $BAFA, hex)
#   BANNER      the UART1 banner lines checked (| separated)
#   CLIP        a .VID clip the display check plays ([V00<CLIP>])
#   LOOK        the module's look: menu (the 128x32 module's 14-byte config packets), or none (The Jetsons'
#               128x64 module: 12-byte packets, drawn as sent)
#   SIM         the simulator check (tests/pinheck/sim)
#   TROUGH1     the switch a ball in trough 1 (the eject position) closes
GAME=${PINHECK_GAME:-dominos}
case $GAME in
dominos) PRG=DOM_V006.PRG PRP=PRP_V008.BIN PROGRAMMED=204288 UPDATE_END=leave REBOOTS=0 UPDATED_AT=138 STORED=600BAFA LOOK=menu CLIP=LT5 SIM=sim.py TROUGH1=12
	BANNER='pinHeck System 2011-2016|Game: DOM - DOMINOS|Version: 006' ;;
rzspook) PRG=RZO_V026.PRG PRP=PRP_V008.BIN PROGRAMMED=309248 UPDATE_END=leave REBOOTS=0 UPDATED_AT=184 STORED=1A00BAFA LOOK=menu CLIP=DMB SIM=rzsim.py TROUGH1=12
	BANNER='pinHeck System 2011-2016|Game: RZO - SPOOK SHOW' ;;
jetsons) PRG=JET_V004.PRG PRP=PRP_V002.BIN PROGRAMMED=197632 UPDATE_END='stopped in programming mode' REBOOTS=1 UPDATED_AT=104 STORED=400BAFA LOOK=none CLIP=TL1 SIM=jetsim.py TROUGH1=92
	BANNER='pinHeck System 2011-2016|Game: JET - JETSONS' ;;
*) echo "PINHECK_GAME: no machine checks for '$GAME'"; exit 2 ;;
esac
