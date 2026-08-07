// license:BSD-3-Clause
#pragma once

#include "core.h"
#include "sim.h"

/*-- CPUs --*/
#define RFRANCO_CPU     0
#define RFRANCO_SCPU    1

/*-- Memory regions --*/
#define RFRANCO_MEMREG_CPU  REGION_CPU1
#define RFRANCO_MEMREG_SCPU REGION_CPU2

#define RFRANCO_DISPLAYSMOOTH 2 /* Smooth the display over this number of VBLANKS */
#define RFRANCO_SOLSMOOTH     4 /* Smooth the solenoids over this number of VBLANKS */

/*-- Standard input ports --
   The playfield switches arrive as two bytes shifted in serially on SID (see
   rfranco.c); these ports carry the cabinet inputs and the operator switches
   that sit on the door, described in the manual under "INSTRUCCIONES PARA
   AJUSTES, TEST Y VISUALIZACION DE RAM". */
#define RFRANCO_COMPORTS \
  PORT_START /* 0 */ \
    COREPORT_BITDEF(  0x0001, IPT_START1,  IP_KEY_DEFAULT) \
    COREPORT_BITDEF(  0x0002, IPT_COIN1,   IP_KEY_DEFAULT) \
    COREPORT_BITDEF(  0x0004, IPT_COIN2,   KEYCODE_3) \
    COREPORT_BIT(     0x0008, "Falta (Tilt)",       KEYCODE_INSERT) \
    COREPORT_BIT(     0x0010, "Caida de bola",      KEYCODE_HOME) \
  PORT_START /* 1 */ \
    COREPORT_DIPNAME( 0x0003, 0x0000, "Door switches") \
      COREPORT_DIPSET(0x0000, "Juego (both down)" ) \
      COREPORT_DIPSET(0x0001, "Test de luces / RAM" ) \
      COREPORT_DIPSET(0x0002, "Borrado display y creditos" ) \
      COREPORT_DIPSET(0x0003, "Ajustes de tanteo y test" )

#define RFRANCO_INPUT_PORTS_START(name, balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls) \
    RFRANCO_COMPORTS

#define RFRANCO_INPUT_PORTS_END INPUT_PORTS_END

#define RFRANCO_COMINPORT CORE_COREINPORT

/*-- ROM loading --
   IC19 holds the 8085 game program (16K 27128); IC14, the second program
   socket, is unpopulated on every known board. IC4 holds the 8035 sound
   program (4K 2532). */
#define RFRANCO_ROMSTART(name, n1, chk1, n2, chk2) \
  ROM_START(name) \
    NORMALREGION(0x10000, RFRANCO_MEMREG_CPU) \
      ROM_LOAD(n1, 0x0000, 0x4000, chk1) \
    NORMALREGION(0x1000, RFRANCO_MEMREG_SCPU) \
      ROM_LOAD(n2, 0x0000, 0x1000, chk2)

#define RFRANCO_ROMEND ROM_END

extern MACHINE_DRIVER_EXTERN(RFRANCO);

#define gl_mRFRANCO RFRANCO
