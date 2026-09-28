#ifndef INC_PINHECK
#define INC_PINHECK

#include "core.h"
#include "sim.h"

#define PINHECK_CPUREGION  REGION_CPU1
#define PINHECK_PROPREGION REGION_USER1

#define PINHECK_INPUT_PORTS_START(name, balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls)

#define PINHECK_INPUT_PORTS_END INPUT_PORTS_END

#define PINHECK_ROMSTART(name, prg, prgsize, prghash, prp, prphash) \
  ROM_START(name) \
    ROM_REGION(0x80000, PINHECK_CPUREGION, ROMREGION_ERASEFF) \
      ROM_LOAD(prg, 0x0000, prgsize, prghash) \
    ROM_REGION(0x8000, PINHECK_PROPREGION, 0) \
      ROM_LOAD(prp, 0x0000, 0x8000, prphash)

#define PINHECK_ROMEND ROM_END

extern PINMAME_VIDEO_UPDATE(pinheck_video);
extern MACHINE_DRIVER_EXTERN(PINHECK);
#define gl_mPINHECK PINHECK

#endif
