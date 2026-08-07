// license:BSD-3-Clause

#include "driver.h"
#include "gen.h"
#include "sim.h"
#include "rfranco.h"
#include "sndbrd.h"

#define GEN_RFRANCO 0

#define INITGAME(name, disptype, balls, lamps) \
  RFRANCO_INPUT_PORTS_START(name, balls) RFRANCO_INPUT_PORTS_END \
  static core_tGameData name##GameData = \
    {GEN_RFRANCO, disptype, {FLIP_SW(FLIP_L), 0, lamps, 0, SNDBRD_NONE}}; \
  static void init_##name(void) { \
    core_gameData = &name##GameData; \
  }

/* TODO(phase 3): placeholder layout. The display board (ref. 53/3307) carries
   30 HDSP-3400 digits driven by a 74159 digit select and two 7447 segment
   decoders behind an 8279. The manual's test mode refers to four player
   displays plus a credits display; the exact digit ordering has to come out of
   the display protocol trace before this can be made accurate. */
static core_tLCDLayout rfrancoDisp[] = {
  { 0, 0, 0, 6, CORE_SEG7},
  { 0,16, 6, 6, CORE_SEG7},
  { 3, 0,12, 6, CORE_SEG7},
  { 3,16,18, 6, CORE_SEG7},
  { 6, 8,24, 2, CORE_SEG7},
  { 6,14,26, 2, CORE_SEG7},
  {0}
};

/*-------------------------------------------------------------------
/ Super Star (1986)
/-------------------------------------------------------------------*/
INITGAME(supstarf, rfrancoDisp, 5, 4)
RFRANCO_ROMSTART(supstarf,
  "m31-a-01187.ic19", CRC(ab8b1148) SHA1(496d3c9664386ae64e94462db2fdd36811a68a87),
  "2532.ic4",         CRC(d6d7eee2) SHA1(60e497c8845320eea01662d894d0b16349ebb7e4))
RFRANCO_ROMEND
CORE_GAMEDEFNV(supstarf, "Super Star", 1986, "Recreativos Franco (Spain)", gl_mRFRANCO, 0)

/* Set 2 is the newer firmware of the two despite the naming, which follows
   MAME's set ordering. It extends the operator menu from 9 adjustment zones to
   25. Its sound ROM is the same 2532 image: the dump taken alongside this game
   ROM had data line D5 stuck high, and clearing that bit reproduces the good
   dump exactly across all 4096 bytes, so the physical part held this content.
   MAME still carries its own copy flagged BAD_DUMP. */
INITGAME(supstarfa, rfrancoDisp, 5, 4)
RFRANCO_ROMSTART(supstarfa,
  "27c128.ic19", CRC(9a440461) SHA1(e2f8dcf95084f755d3a34d77ba2649602687a610),
  "2532.ic4",    CRC(d6d7eee2) SHA1(60e497c8845320eea01662d894d0b16349ebb7e4))
RFRANCO_ROMEND
CORE_CLONEDEFNV(supstarfa, supstarf, "Super Star (set 2)", 1986, "Recreativos Franco (Spain)", gl_mRFRANCO, 0)
