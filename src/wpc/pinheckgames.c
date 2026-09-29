#include "driver.h"
#include "sim.h"
#include "sndbrd.h"
#include "pinheck.h"

static core_tLCDLayout pinheck_disp[] = {
  {0, 0, 32, 128, CORE_VIDEO, (genf *)pinheck_video, NULL}, {0}
};

#define INIT_PINHECK(name, balls, version) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
static core_tGameData name##GameData = { GEN_PINHECK, pinheck_disp, {FLIP_SWNO(PINHECK_SWLFLIP, PINHECK_SWRFLIP), 0, 1, PINHECK_CUSTSOLS, SNDBRD_NONE, 0, version, 0, pinheck_getsol} }; \
static void init_##name(void) { core_gameData = &name##GameData; }

/*-------------------------------------------------------------------
/ pinHeck system: the Parallax Propeller mask ROM (not a game)
/-------------------------------------------------------------------*/
INIT_PINHECK(pinheck, 3, 0)
PINHECK_BIOS_ROMSTART(pinheck)
PINHECK_ROMEND
GAMEX(2014,pinheck,0,PINHECK,pinheck,pinheck,ROT0,"Spooky Pinball","pinHeck System",NOT_A_DRIVER)

/*-------------------------------------------------------------------
/ Domino's Spectacular Pinball Adventure (2016)
/-------------------------------------------------------------------*/
INIT_PINHECK(dominos, 3, 6)
PINHECK_ROMSTART(dominos, "DOM_V006.PRG", 0x31990, CRC(750e27a4) SHA1(3fbebce7f885563e61dd2e4f5d7f0a8a54c53b23),
                 "PRP_V008.BIN", CRC(a51ee28d) SHA1(0556d88b6f0c7cb15e648e1f76f4d47890ddb858))
PINHECK_ROMEND
CORE_CLONEDEFNV(dominos, pinheck, "Domino's Spectacular Pinball Adventure", 2016, "Spooky Pinball", gl_mPINHECK, GAME_NOT_WORKING)
