// license:BSD-3-Clause

/************************************************************************************************
 Recreativos Franco (Spain)
 --------------------------
   Hardware (from the factory manual, CPU board ref. 53/3291, and confirmed
   against a disassembly of the game ROM):

     CPU:     Intel 8085A @ 5.0688 MHz (X1)
     ROM:     27128 (16K) at IC19, mapped 0x0000-0x3FFF.
              IC14, a second program socket, is unpopulated.
     RAM:     5517 (2K x 8) at IC11, mapped 0xC000-0xC7FF, battery backed
     SOUND:   Intel 8035 @ XTAL/2 (taken from 8085 pin 37, CLK OUT) with its own
              2532 (4K) at IC4, plus 2 x AY-3-8910 @ XTAL/6 (from 8035 pin 1, T0)
              and an LM380 output stage
     LATCH:   4 x Intel 8212. Two of them form the bidirectional command/ack
              path between the two CPUs at 0x8000.
     I/O:     serial. The 8085's SOD pin drives the display chain, its SID pin
              reads the playfield switches back, and any OUT instruction
              generates the shared shift clock.

   Interrupts:
     TRAP    - mains phase detection (zero cross). Non-maskable. NOTE: this is
               load bearing at boot - see the comment on the reset path below.
     RST5.5  - 8212 latch, raised when the sound CPU has taken a command
     RST6.5  - periodic housekeeping
     RST7.5  - display/lamp refresh tick

   Status: work in progress. Boot, memory map, serial switch input and the
   sound handshake are implemented. Display decoding, lamps and solenoids are
   not yet mapped - see the TODOs below.

   Sets:
     supstarf  "Super Star" set 1 (m31-a-01187.ic19). 9 operator adjustment
               zones; this is the revision the factory manual documents.
     supstarfa "Super Star" set 2 (27c128.ic19). The NEWER firmware despite the
               set ordering inherited from MAME: it carries 25 adjustment zones,
               set 1's nine unchanged and sixteen more, and reserves an extra
               0x30 bytes of NVRAM (its stack base drops from C7FF to C7CF).
 ************************************************************************************************/

#include "driver.h"
#include "cpu/i8085/i8085.h"
#include "cpu/i8039/i8039.h"
#include "core.h"
#include "rfranco.h"
#include "sndbrd.h"

#define RFRANCO_CPUFREQ 5068800 /* 5.0688 MHz crystal */

/* The mains phase detector feeds TRAP. Spain runs at 50 Hz and the detector
   sees both half cycles, so the interrupt arrives at 100 Hz. */
#define RFRANCO_TRAPFREQ 100

/* Housekeeping / refresh ticks. Both are driven from the same phase reference
   on the real board via the driver-board dividers; rates still to be measured
   against hardware. */
#define RFRANCO_RST75FREQ 400

/* Trigger used to model the 8212's READY handshake - see rfranco_sound_w. */
#define RFRANCO_SOUND_TRIGGER 1701

/*----------------
/  Local variables
/-----------------*/
static struct {
  int    vblankCount;
  UINT32 solenoids;
  core_tSeg segments;

  /* Serial switch chain: two 74165s on the driver board (IC5/IC6) are loaded
     with the playfield contacts and clocked out into SID one bit at a time.
     shiftIn holds the word still to be shifted; shiftPos counts bits. */
  UINT16 swShift;
  int    swShiftPos;

  /* Serial display chain: SOD feeds a 74164 (IC1) on the display board which
     in turn drives the 8279. The game clocks 9 bits per frame. */
  UINT16 dispShift;
  int    dispShiftPos;
  int    sodState;

  /* 8212 command/ack latches between the two CPUs at 0x8000. */
  UINT8  soundCmd;      /* main -> sound, latched in IC6 */
  UINT8  soundReply;    /* sound -> main, latched in IC5 */
  int    soundPending;
  UINT8  scpuP1;        /* 8035 port 1 latch */
  UINT8  scpuP2;        /* 8035 port 2 latch - bit 7 selects the 8212s */
} locals;

/*------------------------------------
/  Serial switch input on the SID pin
/-------------------------------------*/
/* The game reloads the shift registers by pulsing the clock with the parallel
   load asserted, then reads 16 bits MSB first, inverting as it goes because the
   contacts are active low (see the loop at 0x18A6 in the game ROM). We present
   the current playfield state and let it clock through. */
static int rfranco_sid_r(void) {
  int bit;
  if (locals.swShiftPos <= 0) {
    /* reload from the switch matrix at the start of each 16 bit pass */
    locals.swShift = (coreGlobals.swMatrix[1] << 8) | coreGlobals.swMatrix[2];
    locals.swShiftPos = 16;
  }
  bit = (locals.swShift & 0x8000) ? 1 : 0;
  return bit;
}

/*-------------------------------------
/  Serial display output on the SOD pin
/--------------------------------------*/
static void rfranco_sod_w(int state) {
  locals.sodState = state ? 1 : 0;
}

/*-------------------------------------------------
/  Shared shift clock: any OUT instruction pulses it
/--------------------------------------------------*/
/* The 8085 has no dedicated clock output for the serial chains, so the game
   uses an OUT to any port as the strobe (0x00 inside the switch read loop at
   0x18B3, 0xFF inside the display write loop at 0x241C - the port number is
   irrelevant, the whole I/O space is one decode). Each pulse advances both the
   switch shift register and the display shift register. */
static WRITE_HANDLER(rfranco_clk_w) {
  /* advance the switch chain */
  if (locals.swShiftPos > 0) {
    locals.swShift <<= 1;
    locals.swShiftPos--;
  }
  /* advance the display chain, sampling whatever SOD currently holds */
  locals.dispShift = (locals.dispShift << 1) | locals.sodState;
  locals.dispShiftPos++;
  if (locals.dispShiftPos >= 9) {
    /* TODO(phase 3): a complete 9 bit word has arrived at the 74164/8279.
       Decode it into coreGlobals.segments once the display protocol is
       established. */
    locals.dispShiftPos = 0;
  }
}

/*------------------------------
/  8212 latches at 0x8000
/-------------------------------*/
/* Writing sends a command to the sound CPU and raises its interrupt; reading
   takes the reply and clears RST5.5 on the main CPU. The game's handshake is
   at 0x196C: store the command, unmask RST5.5 with SIM, EI, then HALT until
   the latch answers. */
static WRITE_HANDLER(rfranco_sound_w) {
  locals.soundCmd = data;
  locals.soundPending = 1;
  cpu_set_irq_line(RFRANCO_SCPU, 0, ASSERT_LINE);

  /* The 8212 holds the 8085 in wait states through its READY input until the
     sound CPU has taken the byte. That flow control is not optional: the bulk
     transfer at 0x19F3 pushes 19 bytes back to back with no handshake of its
     own, and the 8035 - which polls the INT pin with JNI at 0x00FB rather than
     taking an interrupt - is far too slow to keep up. Without READY the 8085
     simply overwrites the latch and the transfer is lost.

     MAME's skeleton has the same wiring noted but commented out:
        //m_soundlatch[1]->int_wr_callback().append_inputline(maincpu, READY)

     PinMAME's 8085 core has no READY line, so stall the main CPU on a trigger
     instead and let the sound CPU release it when it reads the latch. The
     timed trigger is a safety net: if the sound CPU has masked its interrupt
     and will never read, we must not deadlock. */
  cpu_spinuntil_trigger(RFRANCO_SOUND_TRIGGER);
  cpu_triggertime(TIME_IN_USEC(500), RFRANCO_SOUND_TRIGGER);
}

static READ_HANDLER(rfranco_sound_r) {
  cpu_set_irq_line(RFRANCO_CPU, I8085_RST55_LINE, CLEAR_LINE);
  return locals.soundReply;
}

/* TODO(phase 5): 0x4000 is read once, at 0x18BD, immediately after the 16 bit
   switch shift completes. MAME's skeleton does not map it at all. Until its
   function is established, return 0xFF (idle/pulled up) rather than open bus so
   the behaviour is at least deterministic. */
static READ_HANDLER(rfranco_4000_r) {
  return 0xff;
}

/*-------------------
/  Sound CPU (8035)
/--------------------*/
/* The 8035 reaches everything through MOVX, which PinMAME's MCS-48 core routes
   into the port space with the 8 bit address taken from R0/R1. P2.7 picks the
   target: low selects the 8212 latch pair, high leaves the PSGs selected.

   From the sound ROM's external interrupt handler:
       0028: MOV A,#$7F / OUTL P2,A    ; P2.7 low - select the latches
       002B: MOVX A,@R1                ; read the command, clears INT35
       ...
       007E: ORL P2,#$FF / ANL P2,#$7F ; P2.7 low again
       0082: MOVX @R1,A                ; write the reply, raises INT5.5

   The 8212s are edge devices: strobing one asserts its INT, reading it clears
   it. IC6 carries main->sound (INT35), IC5 carries sound->main (RST5.5). */
#define RFRANCO_LATCH_SELECTED(p2) (((p2) & 0x80) == 0)

static READ_HANDLER(rfranco_scpu_movx_r) {
  if (RFRANCO_LATCH_SELECTED(locals.scpuP2)) {
    /* reading IC6 takes the command and drops the sound CPU's interrupt */
    cpu_set_irq_line(RFRANCO_SCPU, 0, CLEAR_LINE);
    locals.soundPending = 0;
    /* releases the main CPU from its READY stall */
    cpu_trigger(RFRANCO_SOUND_TRIGGER);
    return locals.soundCmd;
  }
  /* TODO(phase 6): PSG read path. 0xFF reads as "no command" to the ROM's
     idle test at 0x002C (INC A / JZ), which is the safe default. */
  return 0xff;
}

static WRITE_HANDLER(rfranco_scpu_movx_w) {
  if (RFRANCO_LATCH_SELECTED(locals.scpuP2)) {
    /* strobing IC5 is the ack the main CPU is halted waiting for */
    locals.soundReply = data;
    cpu_set_irq_line(RFRANCO_CPU, I8085_RST55_LINE, ASSERT_LINE);
    return;
  }
  /* TODO(phase 6): PSG write path (BDIR/BC1 come from P1). */
}

static WRITE_HANDLER(rfranco_scpu_p1_w) {
  locals.scpuP1 = data;
  /* TODO(phase 6): P1 carries BDIR/BC1 for the two AY-3-8910s. */
}

static WRITE_HANDLER(rfranco_scpu_p2_w) {
  locals.scpuP2 = data;
}

/*-- AY-3-8910 --*/
/* PSG1 port A/B read the operator switch bank I1 and connector JO; PSG2's
   ports drive lamp columns through the driver board's 4028 decoders. */
static WRITE_HANDLER(rfranco_ay0_porta_w) { coreGlobals.tmpLampMatrix[0] = data; }
static WRITE_HANDLER(rfranco_ay0_portb_w) { coreGlobals.tmpLampMatrix[1] = data; }
static READ_HANDLER(rfranco_ay1_porta_r)  { return core_getDip(0); }
static READ_HANDLER(rfranco_ay1_portb_r)  { return core_getDip(1); }

struct AY8910interface RFRANCO_ay8910Int = {
  2,                        /* 2 chips */
  RFRANCO_CPUFREQ / 6,      /* clocked from 8035 T0 = XTAL/6 */
  { 30, 30 },               /* volume */
  { 0, rfranco_ay1_porta_r },
  { 0, rfranco_ay1_portb_r },
  { rfranco_ay0_porta_w, 0 },
  { rfranco_ay0_portb_w, 0 },
};

/*-----------
/  Interrupts
/------------*/
/* TRAP carries the mains zero cross. It is not optional: on a machine with
   invalid NVRAM the reset path at 0x0000 tests C000 for the magic byte 0x55
   and, failing it, executes RST 0 - which lands back on 0x0000. Nothing in
   that loop ever writes the magic. It is the TRAP handler at 0x1800 that
   detects the bad magic, sends sound command 0xBB, seeds C000 with 0x55 and
   resets. So without TRAP running the machine simply never comes up. */
static INTERRUPT_GEN(rfranco_trap) {
  cpu_set_irq_line(RFRANCO_CPU, IRQ_LINE_NMI, PULSE_LINE);
}

static INTERRUPT_GEN(rfranco_rst75) {
  cpu_set_irq_line(RFRANCO_CPU, I8085_RST75_LINE, ASSERT_LINE);
}

/*-------------------------------
/  copy local data to interface
/--------------------------------*/
static INTERRUPT_GEN(rfranco_vblank) {
  locals.vblankCount++;

  /*-- lamps --*/
  memcpy((void*)coreGlobals.lampMatrix, (void*)coreGlobals.tmpLampMatrix,
         sizeof(coreGlobals.tmpLampMatrix));
  /*-- solenoids --*/
  coreGlobals.solenoids = locals.solenoids;
  if ((locals.vblankCount % RFRANCO_SOLSMOOTH) == 0)
    locals.solenoids = 0;
  /*-- display --*/
  if ((locals.vblankCount % RFRANCO_DISPLAYSMOOTH) == 0)
    memcpy(coreGlobals.segments, locals.segments, sizeof(locals.segments));
}

static SWITCH_UPDATE(RFRANCO) {
  if (inports)
    CORE_SETKEYSW(inports[RFRANCO_COMINPORT], 0xff, 0);
}

/*----------------
/  Memory handlers
/-----------------*/
static MEMORY_READ_START(rfranco_readmem)
  {0x0000, 0x3fff, MRA_ROM},
  {0x4000, 0x4000, rfranco_4000_r},
  {0x8000, 0x8000, rfranco_sound_r},
  {0xc000, 0xc7ff, MRA_RAM},
MEMORY_END

static MEMORY_WRITE_START(rfranco_writemem)
  {0x0000, 0x3fff, MWA_ROM},
  {0x8000, 0x8000, rfranco_sound_w},
  {0xc000, 0xc7ff, MWA_RAM, &generic_nvram, &generic_nvram_size},
MEMORY_END

static PORT_WRITE_START(rfranco_writeport)
  {0x00, 0xff, rfranco_clk_w},
PORT_END

/*-- sound CPU --*/
static MEMORY_READ_START(rfranco_scpu_readmem)
  {0x0000, 0x0fff, MRA_ROM},
MEMORY_END

static MEMORY_WRITE_START(rfranco_scpu_writemem)
  {0x0000, 0x0fff, MWA_ROM},
MEMORY_END

static PORT_READ_START(rfranco_scpu_readport)
  {0x00, 0xff, rfranco_scpu_movx_r},
MEMORY_END

static PORT_WRITE_START(rfranco_scpu_writeport)
  {0x00, 0xff, rfranco_scpu_movx_w},
  {I8039_p1, I8039_p1, rfranco_scpu_p1_w},
  {I8039_p2, I8039_p2, rfranco_scpu_p2_w},
MEMORY_END

/* The 2532 at IC4 has its data pins wired to the 8035's AD0-AD7 in reverse
   order: EPROM D7 lands on AD0 and D0 on AD7 (see the CPU board schematic,
   manual sheet 1 of ref. 53/3291). Read straight off the chip the image is
   meaningless as MCS-48 code - 5 RET opcodes in 2K, and no jump at either the
   reset or the external interrupt vector. Reversing each byte turns it into an
   ordinary program: 172 JMP, 88 CALL, 28 RET, and the expected vector layout
       000: DIS I / JMP $09F
       003: SEL RB0 / MOV R7,A / JMP $028   (external interrupt)
       007: STOP TCNT                       (timer)
   Do it once, after the ROMs are loaded. */
static void rfranco_unscramble_sound_rom(void) {
  static int done = 0;
  UINT8 *rom = memory_region(RFRANCO_MEMREG_SCPU);
  int i;
  if (done || !rom) return;
  done = 1;
  for (i = 0; i < 0x1000; i++) {
    UINT8 b = rom[i];
    b = (UINT8)(((b & 0x01) << 7) | ((b & 0x02) << 5) | ((b & 0x04) << 3) | ((b & 0x08) << 1) |
                ((b & 0x10) >> 1) | ((b & 0x20) >> 3) | ((b & 0x40) >> 5) | ((b & 0x80) >> 7));
    rom[i] = b;
  }
}

static MACHINE_INIT(RFRANCO) {
  memset(&locals, 0, sizeof locals);
  rfranco_unscramble_sound_rom();
  /* RIM must sample SID at the instant it executes, because the switch data is
     being clocked in a bit at a time - a value pushed in ahead of time would be
     stale. */
  i8085_set_SID_callback(rfranco_sid_r);
  i8085_set_SOD_callback(rfranco_sod_w);
}

MACHINE_DRIVER_START(RFRANCO)
  MDRV_IMPORT_FROM(PinMAME)
  MDRV_CPU_ADD_TAG("mcpu", 8085A, RFRANCO_CPUFREQ)
  MDRV_CPU_MEMORY(rfranco_readmem, rfranco_writemem)
  MDRV_CPU_PORTS(NULL, rfranco_writeport)
  MDRV_CPU_VBLANK_INT(rfranco_vblank, 1)
  MDRV_CPU_PERIODIC_INT(rfranco_trap, RFRANCO_TRAPFREQ)

  /* PinMAME's MCS-48 core wants the machine cycle rate, not the pin frequency:
     the 8035 divides its clock by 15 internally. The sound CPU is fed from the
     8085's CLK OUT, i.e. XTAL/2. */
  MDRV_CPU_ADD_TAG("scpu", I8035, RFRANCO_CPUFREQ / 2 / 15)
  MDRV_CPU_MEMORY(rfranco_scpu_readmem, rfranco_scpu_writemem)
  MDRV_CPU_PORTS(rfranco_scpu_readport, rfranco_scpu_writeport)

  MDRV_INTERLEAVE(500)
  MDRV_CORE_INIT_RESET_STOP(RFRANCO, NULL, NULL)
  MDRV_DIPS(16)
  MDRV_NVRAM_HANDLER(generic_0fill)
  MDRV_SWITCH_UPDATE(RFRANCO)
  MDRV_SOUND_ADD(AY8910, RFRANCO_ay8910Int)
MACHINE_DRIVER_END
