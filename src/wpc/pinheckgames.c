#include "driver.h"
#include "sim.h"
#include "sndbrd.h"
#include "pinheck.h"

static core_tLCDLayout pinheck_disp[] = {
  {0, 0, 32, 128, CORE_VIDEO, (genf *)pinheck_video, NULL}, {0}
};

#define INIT_PINHECK(name, balls) \
PINHECK_INPUT_PORTS_START(name, balls) PINHECK_INPUT_PORTS_END \
static core_tGameData name##GameData = { GEN_PINHECK, pinheck_disp, {0, 0, 0, 0, SNDBRD_NONE, 0, 0} }; \
static void init_##name(void) { core_gameData = &name##GameData; }

/*-------------------------------------------------------------------
/ Domino's Spectacular Pinball Adventure (2016)
/-------------------------------------------------------------------*/
INIT_PINHECK(dominos, 3)
PINHECK_ROMSTART(dominos, "DOM_V006.PRG", 0x31990, CRC(750e27a4) SHA1(3fbebce7f885563e61dd2e4f5d7f0a8a54c53b23),
                 "PRP_V008.BIN", CRC(a51ee28d) SHA1(0556d88b6f0c7cb15e648e1f76f4d47890ddb858))
PINHECK_ROMEND
CORE_GAMEDEFNV(dominos, "Domino's Spectacular Pinball Adventure", 2016, "Spooky Pinball", gl_mPINHECK, GAME_NOT_WORKING)
