#ifndef INC_PINHECK
#define INC_PINHECK

#include "core.h"
#include "sim.h"

#define PINHECK_CPUREGION  REGION_CPU1
#define PINHECK_PROPREGION REGION_USER1
#define PINHECK_BIOSREGION REGION_USER2

#define PINHECK_SWLFLIP  4
#define PINHECK_SWRFLIP  3
#define PINHECK_CUSTSOLS 14

#define PINHECK_COMPORTS \
  PORT_START /* 0 */ \
    COREPORT_BITDEF(  0x0001, IPT_TILT,          KEYCODE_INSERT) \
    COREPORT_BITDEF(  0x0002, IPT_COIN1,         IP_KEY_DEFAULT) \
    COREPORT_BITDEF(  0x0004, IPT_START1,        IP_KEY_DEFAULT) \
    COREPORT_BITTOG(  0x0008, "Coin Door",       KEYCODE_END) \
    COREPORT_BIT(     0x0010, "Back",            KEYCODE_7) \
    COREPORT_BIT(     0x0020, "Enter",           KEYCODE_0) \
    COREPORT_BIT(     0x0040, "User",            KEYCODE_9)

#define PINHECK_INPUT_PORTS_START(name, balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls) \
    PINHECK_COMPORTS

#define PINHECK_INPUT_PORTS_END INPUT_PORTS_END

#define PINHECK_BIOS_ROMSTART(name) \
  ROM_START(name) \
    ROM_REGION(0x8000, PINHECK_BIOSREGION, 0) \
      ROM_LOAD("p8x32a.rom", 0x0000, 0x8000, CRC(f99b3070) SHA1(b7b4fdf4f096db7d18bda6355725cb42ae4a9378))

#define PINHECK_ROMSTART(name, prg, prgsize, prghash, prp, prphash) \
  PINHECK_BIOS_ROMSTART(name) \
    ROM_REGION(0x80000, PINHECK_CPUREGION, ROMREGION_ERASEFF) \
      ROM_LOAD(prg, 0x0000, prgsize, prghash) \
    ROM_REGION(0x8000, PINHECK_PROPREGION, 0) \
      ROM_LOAD(prp, 0x0000, 0x8000, prphash)

#define PINHECK_ROMEND ROM_END

extern PINMAME_VIDEO_UPDATE(pinheck_video);
extern int pinheck_getsol(int solNo);
extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK

#endif
