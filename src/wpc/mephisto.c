// license:BSD-3-Clause

#include "driver.h"
#include "core.h"
#include "cpu/i86/i88intf.h"
#include "cpu/i8051/i8051.h"
#include "machine/i8256.h"
#include "machine/i8155.h"


/* Phase 0 tracing: set to 1 to log every I/O access with its decoded meaning. */
#define CIRSA_VERBOSE 0

static struct {
  int   lampCol;        /* IC20 PA4-6 -> IC29 (7445) -> lamp columns LC0-LC7 */
  int   swCol;          /* IC20 PA0-3 -> IC30 (7445) -> switch columns CC0-CC9 */
  UINT8 shiftFrame[8];  /* one pass through the 4094 display chain, 6 or 8
                           bytes long -- see cirsa_frameLen() */
  int   shiftPos;
  UINT8 lastKeys;       /* previous cabinet key state, for edge-only updates */
  /*-- phase 0 instrumentation state --*/
  int   lastrep;
  char  lastline[192];
  UINT8 qcState;        /* live quick-contact inputs, swMatrix[12] bits 0-4 */
  UINT8 qcLatch;        /* IC11 (74LS373) held value, read back on IC9 PB */
  int   qcTransparent;  /* IC9 PC5 high -> latch follows input */
  UINT8 sndToSnd;       /* last byte the MUART sent, latched for the 8051 */
} locals;

/*-------------------------------------------------------------------------
/  Phase 0 instrumentation -- observation only.
/
/  Every handler still returns exactly what it returned before (0, which is
/  also this core's unmapped-read value), so machine behaviour is unchanged.
/  The point is to check the static analysis of the Sport 2000 ROM against a
/  live trace: which registers the game really touches, in which order, and
/  in which addressing mode.
/
/  Identical consecutive lines are collapsed, otherwise the watchdog kick
/  (a single XOR on MUART P1.4) buries everything else.
/-------------------------------------------------------------------------*/
#if CIRSA_VERBOSE
static void iolog(const char *msg) {
  if (!strcmp(msg, locals.lastline)) { locals.lastrep++; return; }
  if (locals.lastrep) {
    logerror("        ... previous line repeated %d more time(s)\n", locals.lastrep);
    locals.lastrep = 0;
  }
  strncpy(locals.lastline, msg, sizeof(locals.lastline)-1);
  locals.lastline[sizeof(locals.lastline)-1] = '\0';
  logerror("%s", msg);
}

static const char *muart_regname(int reg) {
  static const char * const n[16] = {
    "CMD1", "CMD2", "CMD3", "MODE", "PORT1C", "SETINT", "RSTINT/INTADR",
    "BUFFER", "PORT1", "PORT2", "TIMER1", "TIMER2", "TIMER3", "TIMER4",
    "TIMER5", "STATUS/MODIF" };
  return n[reg & 15];
}

/* levels per the 8256AH datasheet; L1 and L3/L6/L7 depend on CMD1/MODE bits */
static void muart_levels(char *buf, int mask) {
  static const char * const src[8] = {
    "L0=Tmr1", "L1=Tmr2/P17", "L2=EXTINT", "L3=Tmr3", "L4=RX", "L5=TX",
    "L6=Tmr4", "L7=Tmr5" };
  int i; buf[0] = '\0';
  for (i = 0; i < 8; i++)
    if (mask & (1 << i)) { if (buf[0]) strcat(buf, ","); strcat(buf, src[i]); }
  if (!buf[0]) strcpy(buf, "none");
}

static void muart_decode(char *buf, int reg, int data) {
  char tmp[128];
  buf[0] = '\0';
  switch (reg) {
    case 0: /* CMD1 */
      sprintf(buf, "FRQ=%d(%s) 8086=%d BITI=%d(L1=%s) BRKI=%d stop=%d len=%d",
              data & 1, (data & 1) ? "1kHz" : "16kHz",
              (data >> 1) & 1, (data >> 2) & 1,
              ((data >> 2) & 1) ? "P17 edge" : "Timer2",
              (data >> 3) & 1, (data >> 4) & 3, 8 - ((data >> 6) & 3));
      break;
    case 1: /* CMD2 */
      sprintf(buf, "baud=%d clkdiv=%d parity=%s",
              data & 15, (data >> 4) & 3,
              (data & 0x80) ? ((data & 0x40) ? "even" : "odd") : "none");
      break;
    case 2: /* CMD3 -- set/reset register */
      sprintf(buf, "%s:", (data & 0x80) ? "SET" : "RESET");
      if (data & 0x40) strcat(buf, " RxE");
      if (data & 0x20) strcat(buf, " IAE");
      if (data & 0x10) strcat(buf, " NIE");
      if (data & 0x08) strcat(buf, " END(EOI)");
      if (data & 0x04) strcat(buf, " SBRK");
      if (data & 0x02) strcat(buf, " TBRK");
      if (data & 0x01) strcat(buf, " RST");
      break;
    case 3: /* MODE */
      sprintf(buf, "T35=%d T24=%d T5C=%d CT3=%d CT2=%d P2C=%d",
              (data >> 7) & 1, (data >> 6) & 1, (data >> 5) & 1,
              (data >> 4) & 1, (data >> 3) & 1, data & 7);
      break;
    case 4: /* PORT1C -- 1 = output */
      sprintf(buf, "P1 dir out=%02x in=%02x", data, (~data) & 0xff);
      break;
    case 5: muart_levels(tmp, data); sprintf(buf, "enable %s", tmp); break;
    case 6: muart_levels(tmp, data); sprintf(buf, "disable %s", tmp); break;
    default: break;
  }
}

#endif  /* CIRSA_VERBOSE */

static WRITE_HANDLER(ic4_w) {
#if CIRSA_VERBOSE
  char msg[192], dec[128];
  int reg, mode8086 = i8256_is_8086_mode();
  if (mode8086 && (offset & 1)) {
    sprintf(msg, "IC4  8256 W off=%02x  IGNORED (odd offset in 8086 mode)  PC=%05x\n",
            offset, activecpu_get_pc());
    iolog(msg);
  } else {
    reg = mode8086 ? ((offset >> 1) & 15) : (offset & 15);
    muart_decode(dec, reg, data);
    sprintf(msg, "IC4  8256 W off=%02x reg%-2d %-13s = %02x  %s%s  PC=%05x\n",
            offset, reg, muart_regname(reg), data,
            mode8086 ? "" : "[8085 mode] ", dec, activecpu_get_pc());
    iolog(msg);
  }
#endif
  i8256_w(offset, data);
}

static READ_HANDLER(ic4_r) {
  int val = i8256_r(offset);
#if CIRSA_VERBOSE
  {
    char msg[192];
    int mode8086 = i8256_is_8086_mode();
    int reg = mode8086 ? ((offset >> 1) & 15) : (offset & 15);
    if (!(mode8086 && (offset & 1))) {
      sprintf(msg, "IC4  8256 R off=%02x reg%-2d %-13s -> %02x  PC=%05x\n",
              offset, reg, muart_regname(reg), val, activecpu_get_pc());
      iolog(msg);
    }
  }
#endif
  return val;
}

#if CIRSA_VERBOSE
static const char *i8155_regname(int reg) {
  static const char * const n[8] = {
    "CMD/STATUS", "PA", "PB", "PC", "TIMER_LO", "TIMER_HI", "reg6", "reg7" };
  return n[reg & 7];
}

static void i8155_decode(char *buf, int reg, int data) {
  static const char * const tm[4] = { "NOP", "STOP", "STOP-AT-TC", "START" };
  /* Command bits 3-2 are PC2,PC1 and the four modes are NOT in numeric
     order: 00 = ALT1 (all input), 01 = ALT3, 10 = ALT4, 11 = ALT2 (all
     output) -- Intel's table, and what i8155.c's own decode implements. */
  static const char * const pc[4] = { "1", "3", "4", "2" };
  buf[0] = '\0';
  if (reg == 0)
    sprintf(buf, "PA=%s PB=%s PC=ALT%s intA=%d intB=%d timer=%s",
            (data & 1) ? "out" : "in", (data & 2) ? "out" : "in",
            pc[(data >> 2) & 3], (data >> 4) & 1, (data >> 5) & 1,
            tm[(data >> 6) & 3]);
  else if (reg == 5)
    sprintf(buf, "count_hi=%d mode=%d", data & 0x3f, (data >> 6) & 3);
}

static void log8155(const char *chip, int rw, int offset, int data) {
  char msg[192], dec[128];
  int reg = offset & 7;
  i8155_decode(dec, reg, data);
  if (rw)
    sprintf(msg, "%-4s 8155 W off=%x  %-10s = %02x  %s  PC=%05x\n",
            chip, offset, i8155_regname(reg), data, dec, activecpu_get_pc());
  else
    sprintf(msg, "%-4s 8155 R off=%x  %-10s       PC=%05x\n",
            chip, offset, i8155_regname(reg), activecpu_get_pc());
  iolog(msg);
}

#endif  /* CIRSA_VERBOSE */

#define I8155_IC9   0
#define I8155_IC20  1

static WRITE_HANDLER(ic20_w) {
#if CIRSA_VERBOSE
  log8155("IC20", 1, offset, data);
#endif
  i8155_w(I8155_IC20, offset, data);
}
static READ_HANDLER(ic20_r) {
  UINT8 val = i8155_r(I8155_IC20, offset);
#if CIRSA_VERBOSE
  log8155("IC20", 0, offset, val);
#endif
  return val;
}
static WRITE_HANDLER(ic9_w) {
#if CIRSA_VERBOSE
  log8155("IC9", 1, offset, data);
#endif
  i8155_w(I8155_IC9, offset, data);
}
static READ_HANDLER(ic9_r) {
  UINT8 val = i8155_r(I8155_IC9, offset);
#if CIRSA_VERBOSE
  log8155("IC9", 0, offset, val);
#endif
  return val;
}

/* Display data on its way to the 4094 chain -- decoded in the next phase. */
/*-- Display: the 74LS165 at 0x2E000 feeds the 4094 chain -----------------
/  The CPU writes a byte to IC2 (74165); hardware shifts it out on QH,
/  clocked by CLK SHT (IC9's TIMER OUT via IC13), into the daisy-chained
/  4094s on the display boards.  One frame = one byte per 4094, and the two
/  games do NOT have the same number of them, so the frame length is per
/  game (hw.gameSpecific1, same selector as the column-mask table):
/
/    Sport 2000  8 bytes -- 7 segment groups + the column select
/    Mephisto    6 bytes -- 5 segment groups + the column select
/
/  Mephisto's six are accounted for by its manual: one 4094 on each of the
/  four player display boards (plate 13) and two on the match/credit board
/  (plate 11, IC2 and IC3), one of which drives COLUMNA through the 74HC240
/  into the seven TIP116 digit drivers.
/
/  Measured over a 12 s run, scoring each candidate period on "last byte of
/  the frame has exactly one clear bit":
/
/    sport2k   33608 bytes   period 8: 100.0%   period 6:  25.9%
/    mephisto  29418 bytes   period 6: 100.0%   period 8:  33.3%
/    mephist1  29316 bytes   period 6: 100.0%   period 8:  33.3%
/
/  and by the writing PCs: Mephisto writes five bytes from 0x00F47 and one
/  from 0x00F65 (24515 : 4903, exactly 5:1); mephist1 the same from 0x00D26
/  and 0x00D44; Sport 2000 writes eight bytes from eight consecutive PCs
/  0x0B6A9..0x0B712, 4201 each.
/
/  The last byte of each frame is the digit column select -- active low,
/  exactly one bit clear, walking across seven columns, which matches the
/  seven TIP116 digit drivers.  The rest is segment data.  Because the first
/  byte shifted in travels furthest down the chain, the last byte written
/  sits in the 4094 nearest the CPU and byte 0 in the one furthest away.
/
/  For Sport 2000 the four 7-digit player displays occupy segments 0..27 in
/  cirsa_disp and the credit/match digits 28..32; the mapping of the last
/  three groups onto those five digits still needs a frame where the game
/  actually lights them.
/----------------------------------------------------------------------*/
/* The column-select mask table's byte values -- and which bit each one
   clears -- are NOT shared between the two ROM sets this driver serves.
   Both were confirmed by an exhaustive byte-scan of the ROM image for "8
   bytes where each XORs with 0xFF to 0 or a single bit", which returns
   exactly one hit per ROM, at the exact address each game's own display
   routine reads from via CS:B[BX+addr]:

     Sport 2000        ROM 0xB730:  FF FD FB F7 7F BF EF DF
                          col 1-7 -> bits 1,2,3,7,6,4,5 (bit 0 never used)
     Mephisto (rev 1.2) ROM 0x0F82:  FF FE FD FB F7 BF DF EF
                          col 1-7 -> bits 0,1,2,3,6,5,4 (bit 7 never used)

   Mephisto rev 1.1 (mephist1) has the identical 8 bytes at ROM 0x0D61 (a
   different address -- the surrounding code is extensively reshuffled
   between revisions -- but byte-for-byte the same table, read by a
   structurally identical routine), so it shares Mephisto's inverted table
   below rather than needing a third one.

   Each table inverts its ROM bytes -- indexed by cleared bit, giving the
   1-7 ROM column, with 0 meaning "no column" -- and is selected per game via
   core_gameData->hw.gameSpecific1 (0 = Sport 2000, 1 = Mephisto/mephist1;
   see cirsaGameData/mephistoGameData below). Mephisto's table is derived
   only from its ROM's mask table -- unlike Sport 2000's, it has not been
   cross-checked against a located RAM display buffer, since Mephisto's
   buffer address is not established. */
static const UINT8 colFromBitSport2k[8]  = { 0, 1, 2, 3, 6, 7, 5, 4 };
static const UINT8 colFromBitMephisto[8] = { 1, 2, 3, 4, 7, 6, 5, 0 };

/* Bytes per pass through the chain -- one per 4094 on the display boards. */
static int cirsa_frameLen(void) {
  return core_gameData->hw.gameSpecific1 ? 6 : 8;
}

static void cirsa_shift_frame(const UINT8 *f, int len) {
  const UINT8 *colFromBit = core_gameData->hw.gameSpecific1
                              ? colFromBitMephisto : colFromBitSport2k;
  UINT8 sel = (UINT8)~f[len - 1];
  int col, g, bit;

  /* Exactly one of the seven digit drivers is on at a time, so the column
     byte must have exactly one clear bit.  0xFF (nothing selected) and
     anything with two or more clear bits are both rejected: a byte that is
     not a valid column select means the frame is not a frame, and a blank
     display is a far better symptom of that than a plausible digit. */
  if (!sel || (sel & (sel - 1))) return;
  for (bit = 0; !(sel & (1 << bit)); bit++) ;
  if (colFromBit[bit] == 0) return;               /* bit unused as a column */
  col = colFromBit[bit] - 1;                      /* 0..6 digit position */

  /* Sport 2000 only.  Mephisto's five segment groups are NOT characterised:
     its match/credit board is first in the chain (plate 11: J23 carries DATA
     IN from the control board, J24 daisy-chains onward), which fixes the
     column register and the match/credit segments as the last two bytes of
     the frame -- but nothing establishes which of the remaining four bytes
     is which player display, which of the seven columns light that board's
     five digits, or even that Mephisto's segment bit order is Sport 2000's.
     Nor does cirsa_disp describe Mephisto's panel: it was laid out for Sport
     2000, whose 33 units are 14 alphanumeric plus 19 seven-segment, where
     Mephisto has 33 identical LTS 3401 digits.  Writing this frame into that
     layout would put invented positions on the screen and in /api/info, so
     until a service-mode display test settles it, write nothing. */
  if (core_gameData->hw.gameSpecific1) return;

  for (g = 0; g < 4; g++) {                   /* the four player displays */
    UINT8 seg = f[6 - g];
    coreGlobals.segments[g * 7 + col].w = seg;
  }
  if (col < 2) coreGlobals.segments[28 + col].w = f[2];
  if (col < 1) coreGlobals.segments[30].w       = f[1];
  if (col < 2) coreGlobals.segments[31 + col].w = f[0];
}

static WRITE_HANDLER(shift_w) {
  int len = cirsa_frameLen();
  locals.shiftFrame[locals.shiftPos++] = data;
  if (locals.shiftPos == len) {
    locals.shiftPos = 0;
    cirsa_shift_frame(locals.shiftFrame, len);
  }
#if CIRSA_VERBOSE
  {
    char msg[192];
    sprintf(msg, "SHIFT W off=%x = %02x  PC=%05x\n", offset, data, activecpu_get_pc());
    iolog(msg);
  }
#endif
}

/* Read side of the same address (74LS165 parallel load).  The ROM has one
   such read at 0x6A88, on a path it does not currently take. */
static READ_HANDLER(shift_r) {
#if CIRSA_VERBOSE
  char msg[192];
  sprintf(msg, "SHIFT R off=%x  (74LS165)  PC=%05x\n", offset, activecpu_get_pc());
  iolog(msg);
#endif
  return 0;
}

static WRITE_HANDLER(diag_w) {
#if CIRSA_VERBOSE
  char msg[192];
  sprintf(msg, "DIAG W off=%x = %02x  PC=%05x\n", offset, data, activecpu_get_pc());
  iolog(msg);
#endif
}

static MEMORY_WRITE_START(mephisto_writemem)
  {0x10000,0x107ff, MWA_RAM, &generic_nvram, &generic_nvram_size},
  {0x12000,0x1201f, ic4_w},
  {0x13000,0x130ff, MWA_RAM},
  {0x13800,0x13807, ic20_w},
  {0x14000,0x140ff, MWA_RAM},
  {0x14800,0x14807, ic9_w},
  {0x16000,0x16000, shift_w},
  {0x17000,0x17001, diag_w},
MEMORY_END

static MEMORY_READ_START(mephisto_readmem)
  {0x00000,0x0ffff, MRA_ROM},
  {0x10000,0x107ff, MRA_RAM},
  {0x12000,0x1201f, ic4_r},
  {0x13000,0x130ff, MRA_RAM},
  {0x13800,0x13807, ic20_r},
  {0x14000,0x140ff, MRA_RAM},
  {0x14800,0x14807, ic9_r},
  {0x16000,0x16000, shift_r},
  {0xf8000,0xfffff, MRA_ROM},
MEMORY_END

static MEMORY_WRITE_START(cirsa_writemem)
  {0x20000,0x21fff, MWA_RAM, &generic_nvram, &generic_nvram_size},
  {0x2a000,0x2a01f, ic4_w},
  {0x2b000,0x2b0ff, MWA_RAM},
  {0x2b800,0x2b807, ic20_w},
  {0x2c000,0x2c0ff, MWA_RAM},
  {0x2c800,0x2c807, ic9_w},
  {0x2e000,0x2e000, shift_w},
  {0x2f000,0x2f000, diag_w},
MEMORY_END

static MEMORY_READ_START(cirsa_readmem)
  {0x00000,0x0ffff, MRA_ROM},
  {0x20000,0x21fff, MRA_RAM},
  {0x2a000,0x2a01f, ic4_r},
  {0x2b000,0x2b0ff, MRA_RAM},
  {0x2b800,0x2b807, ic20_r},
  {0x2c000,0x2c0ff, MRA_RAM},
  {0x2c800,0x2c807, ic9_r},
  {0x2e000,0x2e000, shift_r},
  {0xf8000,0xfffff, MRA_ROM},
MEMORY_END

/*-- 8256 MUART wiring --*/
static void cirsa_muart_int(int state) {
  cpu_set_irq_line(0, 0, state ? ASSERT_LINE : CLEAR_LINE);
}

/* The MUART answers the acknowledge cycle with its own vector (40H + level in
   8086 mode), which is why the whole machine is dead without it: the ROM's
   interrupt table only has real handlers at types 40H..47H. */
static int cirsa_irq_callback(int irqline) {
  return i8256_inta();
}

static UINT8 cirsa_p1_in(void) {
  /* Port 1 inputs (PORT1C = 5C leaves P10, P11, P15 and P17 as inputs):
       P10 S.C.MAT  - the CONT contact line, inverted
       P11 PPCERO   - mains zero crossing
       P15          - baud generator / timer 5 trigger
       P17 F.T.     - "falta de tension", power failure
     P17 must rest LOW.  CMD1.BITI routes it to interrupt level 1 and the
     datasheet is explicit that a low-to-high transition is what signals the
     fault, so a pin stuck high reads as a permanent power failure and the
     ROM restarts.  The real sources are connected in a later phase. */
  return 0x00;
}

static void cirsa_p1_out(UINT8 data) {
  /* P14 = CL-WD (watchdog kick), P12/P13/P16 not yet used */
}

/*-- MUART Port 2 (plate 5) ---------------------------------------------
/  P20 EG1, P21 EG2, P22 TEST, P23 AVANCE are inputs, buffered through
/  74HC240s from J4.2 and J6 with 10K pull-ups, so a pressed button reads
/  as 1.  P24 RST ASIN (sound board reset), P25 INH LF, P26 INH FLIP and
/  P27 INH L.C. are outputs -- these are the bits the ROM sets and clears
/  around 0x0A29, not display strobes as first assumed.
/----------------------------------------------------------------------*/
static void cirsa_p2_out(UINT8 data) {
  /* RST ASIN and the three inhibit lines; nothing consumes them yet. */
}

static UINT8 cirsa_p2_in(void) {
  UINT8 ded = coreGlobals.swMatrix[0];
  UINT8 v = 0;
  if (ded & 0x01) v |= 0x04;      /* TEST    -> P22 */
  if (ded & 0x02) v |= 0x08;      /* AVANCE  -> P23 */
  if (ded & 0x04) v |= 0x01;      /* EG1     -> P20 */
  if (ded & 0x08) v |= 0x02;      /* EG2     -> P21 */
  return v;
}

/*-- The MUART <-> 8051 serial link (plate 5, J23) -----------------------
/  Byte level, not bit level: the 8256's TxD callback hands us a byte, the
/  8051's RxD callback collects it, and anything the 8051 transmits goes
/  back through i8256_receive() which raises interrupt level 4 --
/  ISR_SerialRX at 0x0C994, which reads the buffer at [0xA00E].
/
/  The boot handshake at 0x05BF pulses a line, waits, tests 0x2A01E bit 6
/  for RX-ready and compares [0x2A00E] against 0xA5.  Until this link
/  existed the test could not pass and the machine displayed "NO AUDIO".
/
/  i8051.c's I8051_RX_LINE case is the *only* place serial_rx_callback is
/  ever invoked -- there is no polling path, and reaching that case does
/  not by itself deliver anything unless the firmware also has ES and REN
/  set.  A driver that only installs the callback (as this one did before
/  this fix) gets silence: the byte sits in locals.sndToSnd forever and
/  the 8051 never knows it arrived.  Asserting the line each time a fresh
/  byte lands is what actually pulls it through cirsa_snd_rx() into SBUF
/  and raises RI -- same pattern as capcoms.c's send_data_to_8752().  The
/  case does not look at the line state at all, so a bare ASSERT_LINE per
/  byte is enough; there is nothing to clear afterwards.  scpu (the 8051)
/  is cpu 1 here -- mcpu is added first in MACHINE_DRIVER_START(mephisto).
/----------------------------------------------------------------------*/
static void cirsa_txd_out(UINT8 data) {
  locals.sndToSnd = data;
  cpu_set_irq_line(1, I8051_RX_LINE, ASSERT_LINE);
}

static int cirsa_snd_rx(void) {
  return locals.sndToSnd;
}

static void cirsa_snd_tx(int data) {
  i8256_receive((UINT8)data);
}

static const I8256interface cirsa_i8256 = {
  cirsa_muart_int,
  cirsa_p1_in, cirsa_p1_out,
  cirsa_p2_in, cirsa_p2_out,
  cirsa_txd_out
};

/*-- IC20: the lamp and switch matrices (plate 6) ------------------------
/  PA0-3 select a switch column through IC30 (7445), PA4-6 select a lamp
/  column through IC29 (7445), and PA7 strobes the WD LUCES lamp watchdog.
/  PB drives the lamp rows through IC32 (UDN6118-A).  PC reads the switch
/  rows back through two stages of 74HC14 off pulled-up lines, so a closed
/  switch reads as 0.
/----------------------------------------------------------------------*/
static WRITE_HANDLER(ic20_pa_w) {
  int col = (data >> 4) & 0x07;
  /* The lamp columns are strobed 0..7 in order and each one is blanked
     again before the next is selected, so accumulate a whole sweep and
     latch it when the sweep wraps.  Latching on the video frame instead
     chops the sweep and drops whichever columns straddle the boundary. */
  if (col == 0 && locals.lampCol != 0) {
    memcpy((void *)coreGlobals.lampMatrix, (void *)coreGlobals.tmpLampMatrix,
           sizeof(coreGlobals.tmpLampMatrix));
    memset((void *)coreGlobals.tmpLampMatrix, 0, sizeof(coreGlobals.tmpLampMatrix));
  }
  locals.swCol   = data & 0x0f;
  locals.lampCol = col;
}

static WRITE_HANDLER(ic20_pb_w) {
  core_setLamp(coreGlobals.tmpLampMatrix, 1 << locals.lampCol, data);
}

static READ_HANDLER(ic20_pc_r) {
  if (locals.swCol > 9) return 0x3f;
  return ~coreGlobals.swMatrix[locals.swCol + 1] & 0x3f;
}

/*-- IC9 Port A: the coil bus (plate 9) -----------------------------------
/  One 8-byte frame carries all 24 coils.  PA0-2 select a position 0-7 that
/  is shared by the three 74HC259 addressable latches; PA3, PA4 and PA5 are
/  the data bit for J11 (coils 0-7), J12 (8-15) and J13 (16-23)
/  respectively -- three parallel data lines, not a one-hot block select,
/  which is why 24 coils need only eight transfers.  PA7 is the strobe and
/  PA6 a global enable that is only meaningful at position 0.
/
/  Coils are level-held: the ROM re-sends the whole frame about 280 times a
/  second and never issues an "off", so this must be idempotent -- it sets
/  and clears the three bits for the addressed position on every write.
/
/  Derived by running the ROM's own COILS TEST 4-PHASE and correlating with
/  the coil number displayed; see the workspace repo's
/  docs/findings/2026-08-30-coil-encoding.md (that is the repo this driver
/  is developed alongside, not this fork -- there is no such path here).
/
/  Not modelled here: PA6, the global enable described above, is read back
/  and tested but never latched into coreGlobals -- only the strobe (PA7)
/  and the three data bits actually move a coil; coils 1-3 (sorting ramp
/  ejector, kickback, bumper) ARE on this same bus -- the ROM itself
/  re-derives their PA-frame bits from the quick-contact latch on every
/  pass (0xC2AB, 0xC2A5, 0xC2B1/0xC2B6/0xC2BD; see ic9_pa_w below) rather
/  than writing them as ordinary phase-cycled data, which is why COILS
/  TEST 4-PHASE never showed coil 3 as an assertable value in this
/  stream; and "coil 24" (general illumination) is MUART Port 2 bit 7,
/  not on this bus.
/----------------------------------------------------------------------*/
static WRITE_HANDLER(ic9_pa_w) {
  const int pos = data & 0x07;
  int blk;

  /* Sport 2000 only.  The bus protocol itself IS established for Mephisto:
     its ROM drives IC9 Port A with the same transaction idiom, instruction
     for instruction, at 0x130D (disasm/mephisto.bin) as Sport 2000 uses at
     0xC277 (disasm/sport2k.bin) -- select position, write PA, mask PC to
     000, strobe PA bit 7 low then high, restore PC to 111 -- and Mephisto
     also reads PA back and tests bit 6 at 0x12F6-0x1303, mirroring Sport
     2000's own PA readback.  mephisto_readmem/mephisto_writemem route the
     identical 0x14800-0x14807 range through this same ic9_r/ic9_w, so this
     handler fires for Mephisto and mephist1 too, and it is the same bus.

     What is genuinely unestablished is Mephisto's coil NUMBERING and
     CONNECTOR MAP.  The 24-coil, three-connector (J11/J12/J13) assignment
     above came solely from Sport 2000's own COILS TEST 4-PHASE, and
     Mephisto's board has several documented IC9/IC20-area differences from
     Sport 2000's (different switch matrix, different address decoder,
     lamp/switch drive on the CPU board instead of a separate distribution
     board) that bear on which physical coil sits at which bus position.
     Running Mephisto's Port A writes through this decode would populate
     coreGlobals.solenoids with fictitious coil numbers even though the bus
     underneath them is real.  Same precedent as cirsa_shift_frame's
     Mephisto gate just above: writing nothing is honest, writing a
     plausible-looking but invented bitmask is not.  Re-enable once
     Mephisto's own coil numbering is confirmed -- the 0x12F6-0x1303
     readback is a good place to start walking it the way COILS TEST
     4-PHASE did for Sport 2000. */
  if (core_gameData->hw.gameSpecific1) return;

  for (blk = 0; blk < 3; blk++) {
    const UINT32 bit = 1u << (blk * 8 + pos);   /* coil number == mask bit */
    if (data & (0x08 << blk)) coreGlobals.solenoids |=  bit;
    else                      coreGlobals.solenoids &= ~bit;
  }

  /* Coils 1-3 (SORTING RAMP EJECTOR, KICKBACK, BUMPER) DO come through
     this same bus -- the ROM re-derives their PA-frame bits from the
     quick-contact latch on every pass: 0xC2AB `or [0x661],8` (coil 1),
     0xC2A5 `or [0x662],8` (coil 2), and for coil 3 0xC2B1
     `and [0x663],0xf7` / 0xC2B6 `test [0x71e],8` / 0xC2BD `or [0x663],8`,
     cleared and re-derived every pass. The coil-encoding findings' "coil
     3 never appears in this stream" was about COILS TEST 4-PHASE never
     asserting it as an ordinary phase-cycled value, not about the CPU
     being absent from the path -- lines 527-535 above already say this
     bus carries the transaction for both games; the error was in this
     paragraph, not that one. Those re-derive paths are gameplay-only,
     though: [0x663] measured constant at 0xA3 through every attract-mode
     closure, so nothing on this bus tracks the contact while the ROM is
     in attract. The OR below is what covers that gap in software -- the
     real machine's contact wiring does not care which mode the ROM is in
     and fires the coil regardless. Coils 0 and 4 (the two ball ejectors)
     are deliberately EXCLUDED from this: the ROM genuinely drives those
     two itself, on its own timed schedule (0xC3CA, see ball-serve.md),
     so asserting them from raw contact state would fight the ROM's own
     control of them -- confirmed live as a real regression (contact 60
     held during gameplay kept solenoid bit 0 asserted for the whole 4 s
     hold, which the ROM itself never does). Only bits 1-3 of
     locals.qcState (live, un-latched contact state updated every vblank by
     cirsa_vblank -- contact N's bit is coil N-60's bit by hardware
     coincidence) are ORed back in here, every call, so this sweep's own
     clear of those three bits can never win while the contact is actually
     closed: that models a hardware path the CPU cannot override, without
     touching the two coils the CPU already owns.

     This is deliberately one-directional -- it can only ever ADD a bit,
     never block a genuine CPU-commanded write. The rising edge is
     immediate and confirmed reliable over many repeated trials
     (coreGlobals.solenoids goes 0->1 the same vblank the contact closes).
     The falling edge is NOT uniform across coils 1-3, and the mechanism is
     not "this sweep revisits the position" -- the full 8-byte PA frame
     goes out ~280 times/second regardless, so every position, including
     1-3, is rewritten every ~3.5 ms. What actually differs is what the ROM
     puts in that byte:
       - Coil 3 (BUMPER) is re-derived from Port B on every scan pass
         (`test byte [0x71e], 8`), so it tracks the live contact almost
         immediately either way -- measured at ~21 ms to clear, same order
         as coils 0/4.
       - Coils 1-2 (SORTING RAMP EJECTOR, KICKBACK) are one-shot `or`s the
         ROM never explicitly clears bit-by-bit; they only reset when its
         periodic coil-table recompute (0xC512/0xC14D/0xC1E6) overwrites
         the whole frame. THAT recompute's cadence is the real bound on
         their release, not the PA sweep -- measured at 0.3-1.5 s during
         active gameplay, but under 30 ms at idle in attract, so the delay
         itself is state-dependent, not a fixed driver latency. */
  coreGlobals.solenoids |= (locals.qcState & 0x0e);  /* coils 1-3 only */

  /* Feed the PWM integrator with the same 24-bit state, including the
     quick-contact OR above -- this is what MACHINE_INIT(CIRSA)'s
     core_set_pwm_output_type(CORE_MODOUT_SOL0, 24, ...) call is for. This
     handler runs on every hardware write (~280/s), not once a vblank, so
     it is a far better source for the integrator than sampling
     coreGlobals.solenoids from cirsa_vblank would be -- see p2k.c:2013-2018
     on why a once-a-frame sample loses short pulses the integrator needs
     to see. Sport 2000 only, same gate as the rest of this handler. */
  core_write_pwm_output_8b(CORE_MODOUT_SOL0,      (UINT8)(coreGlobals.solenoids      & 0xff));
  core_write_pwm_output_8b(CORE_MODOUT_SOL0 +  8, (UINT8)((coreGlobals.solenoids >> 8)  & 0xff));
  core_write_pwm_output_8b(CORE_MODOUT_SOL0 + 16, (UINT8)((coreGlobals.solenoids >> 16) & 0xff));
}

/*-- IC9: general I/O.  PB reads the B0-B7 bus that also feeds the quick
/  contact comparators and PC5 strobes the IC11 (74LS373) latch that
/  snapshots it.  The quick-contact return path itself is implemented just
/  below.
/----------------------------------------------------------------------*/

/*-- IC9 PB/PC5: the quick-contact return path (plate 5) -----------------
/  The five quick contacts (60 RAMP HOLE, 61 SORTING RAMP, 62 KICKBACK,
/  63 BUMPER, 64 BRIDGE ENTRY) fire their coils in hardware -- that is what
/  makes them quick -- and notify the CPU separately.  A contact is captured
/  in IC11, a 74LS373, and signalled on EXTINT (level 2, vector 0x42).
/
/  ISR_QuickContacts at 0x0931 drives IC9 PC5 high then low and then reads
/  Port B.  A '373 is transparent while its latch-enable is high and holds on
/  the falling edge, so PC5 high makes qcLatch follow qcState and the high-to-
/  low edge freezes it.
/
/  coreGlobals.swMatrix[12] bits 0-4 carry the live contact state.  This is
/  NOT the ROM's own column numbering -- that was this driver's original,
/  wrong argument (contacts sit at the ROM's event-table column 10, so
/  "naturally" swMatrix[11]).  Quick contacts arrive over IC9 Port B, never
/  through the IC20 matrix scan (ic20_pc_r rejects any swCol > 9), so which
/  swMatrix slot carries them is a free driver-side plumbing choice, not
/  something derived from the ROM at all.  Column 11 is wrong regardless:
/  it is PinMAME's own CORE_FLIPPERSWCOL (core.h:334), and core_updateSw()
/  unconditionally overwrites its bits 0x02/0x08 from the (absent) flipper-
/  button state on every single frame, before this driver ever reads them --
/  confirmed live, contacts 61 and 63 never reached ISR_QuickContacts because
/  of exactly this collision.  Column 12 is the first CUSTOM switch column
/  (CORE_STDSWCOLS == 12) and needs hw.swCol == 1 on cirsaGameData below so
/  core.c's custom-column loops (core.c:2000, :2460) stay in bounds -- see
/  that field's own comment for why it was deliberately 0 before this.
/----------------------------------------------------------------------*/
static READ_HANDLER(ic9_pb_r) {
  return locals.qcLatch;
}

static WRITE_HANDLER(ic9_pc_w) {
  const int pc5 = (data & 0x20) ? 1 : 0;
  if (pc5) locals.qcLatch = locals.qcState;   /* transparent */
  locals.qcTransparent = pc5;                 /* falling edge freezes it */
}

static i8155_interface cirsa_i8155 = {
  2,                              /* IC9 = chip 0, IC20 = chip 1 */
  {0, 0}, {ic9_pb_r, 0}, {0, ic20_pc_r},
  {ic9_pa_w, ic20_pa_w}, {0, ic20_pb_w}, {ic9_pc_w, 0},
  {0, 0}
};

/* Not declared in any header; core.c defines it at file scope. */
extern int g_fHandleKeyboard;

static MACHINE_INIT(CIRSA) {
  memset(&locals, 0, sizeof(locals));

  /* A cabinet button physically held at power-on is already down when the
     ROM first reads MUART port 2, a few hundred instructions into the boot
     -- that is how the real machine enters its test mode.  The core clears
     swMatrix at reset, so sample the inputs here rather than waiting for
     the first core_updateSw, which arrives a whole frame too late.

     Only sample here when the driver itself owns the keyboard.
     core_updateSw passes SWITCH_UPDATE a NULL input port array whenever
     g_fHandleKeyboard is clear -- VPinMAME clears m_fHandleKeyboard and
     libpinmame clears g_fHandleKeyboard so the front end can own the
     switches instead (see rfranco.c) -- and seeding from input ports the
     front end believes it owns would fight it. */
  if (g_fHandleKeyboard) {
    UINT8 keys = (UINT8)((readinputport(CORE_COREINPORT) >> 8) & 0x0f);
    coreGlobals.swMatrix[0] = (UINT8)((coreGlobals.swMatrix[0] & ~0x0f) | keys);
    locals.lastKeys = keys;
  }

  /* coreGlobals.nSolenoids was deliberately left at 0 here for both games
     until now -- commit d8547e62, "stop advertising nSolenoids without
     feeding the PWM integrator". p2k.c:2008-2010 spells out why: every
     other driver that sets the count follows with
     core_set_pwm_output_type(CORE_MODOUT_SOL0, n, CORE_MODOUT_SOL_2_STATE)
     and writes through core_write_pwm_output*(); declaring the count
     without also feeding the integrator makes core_getSol() read
     physicOutputState[] as soon as options.usemodsol is set, and an
     integrator nobody feeds reports every output as permanently off --
     nSolenoids alone makes things worse, not better. This is that same
     decision completed, not reversed: ic9_pa_w now feeds the integrator
     on every hardware write (see below), so the count can be advertised
     honestly. Sport 2000 only -- Mephisto's coil numbering on this same
     bus is still uncharacterised (see ic9_pa_w's gate), so it must not
     advertise solenoids it cannot drive. */
  if (!core_gameData->hw.gameSpecific1) {
    coreGlobals.nSolenoids = 24;
    core_set_pwm_output_type(CORE_MODOUT_SOL0, 24, CORE_MODOUT_SOL_2_STATE);
  }

  i8256_init(&cirsa_i8256);
  i8155_init(&cirsa_i8155);
  cpu_set_irq_callback(0, cirsa_irq_callback);

  /* Setup serial line callbacks, needs to be set before CPU reset by
     design -- see nuova.c's uboat65 init for the same warning. */
  i8051_set_serial_tx_callback(cirsa_snd_tx);
  i8051_set_serial_rx_callback(cirsa_snd_rx);
}

static SWITCH_UPDATE(CIRSA) {
  /* Write a cabinet bit only when the key behind it has actually changed.
     Rewriting the whole row every frame stamps out anything else that set
     one of these switches -- a front end, or the remote debugger -- before
     the ROM has had a chance to poll it.  Same reasoning as rfranco.c. */
  if (inports) {
    UINT8 keys    = (UINT8)((inports[CORE_COREINPORT] >> 8) & 0x0f);
    UINT8 changed = (UINT8)(keys ^ locals.lastKeys);
    if (changed) {
      coreGlobals.swMatrix[0] = (UINT8)((coreGlobals.swMatrix[0] & ~changed) |
                                        (keys & changed));
      locals.lastKeys = keys;
    }
  }
}

static INTERRUPT_GEN(cirsa_vblank) {
  core_updateSw(TRUE);
  if (!core_gameData->hw.gameSpecific1) {
    const UINT8 qc = coreGlobals.swMatrix[12] & 0x1f;
    if (qc != locals.qcState) {
      const UINT8 newlyClosed = qc & ~locals.qcState;
      locals.qcState = qc;
      if (locals.qcTransparent) locals.qcLatch = qc;   /* '373 is transparent */
      /* i8256_set_extint() only raises a request on a 0->1 transition of
         the pin (i8256.c: "if (!old && i8256.extint)") -- it is genuinely
         level sensitive, not level triggered on every sample, so while one
         contact is already held closed, a second contact closing changes
         qc but not the 0/1 level and would otherwise request nothing (the
         ROM's own ISR_QuickContacts count then misses the second contact
         for as long as the first stays down). i8256_set_extint()'s
         semantics are intentionally left alone -- it is a shared device --
         so model each newly-closed contact as its own falling/rising edge
         on the driver side instead: drop the line and immediately restate
         it, which is a genuine 0->1 transition by the callee's own rule
         whenever the line is meant to be up. This only runs inside the
         qc != locals.qcState branch, i.e. once per actual change in the
         debounced contact state, so holding a single contact for the full
         ~1.2 s closure produces exactly one pulse -- not a storm -- and a
         contact release with no new contact closing (newlyClosed == 0)
         is left as a plain level update, same as before. */
      if (newlyClosed) i8256_set_extint(0);
      i8256_set_extint(qc ? 1 : 0);
    }
  }
}

static READ_HANDLER(ay8910_porta_r)   { return coreGlobals.swMatrix[0]; }
static READ_HANDLER(ay8910_portb_r)   { return coreGlobals.swMatrix[1]; }
static WRITE_HANDLER(ay8910_porta_w)  { coreGlobals.tmpLampMatrix[0] = data; }
static WRITE_HANDLER(ay8910_portb_w)  { coreGlobals.tmpLampMatrix[1] = data; }

static void ym3812_irq(int irq) {
  cpu_set_irq_line(1, 0, irq ? ASSERT_LINE : CLEAR_LINE);
}

static struct AY8910interface cirsa_ay8910Int = {
  1,			/* 1 chip */
  1500000,		/* 1.5 MHz */
  { 50 },		/* Volume */
  { ay8910_porta_r },
  { ay8910_portb_r },
  { ay8910_porta_w },
  { ay8910_portb_w },
};

static struct YM3812interface cirsa_ym3812Int = {
  1,						/* 1 chip */
  3579545,					/* NTSC clock */
  { 50 },					/* volume */
  { ym3812_irq },			/* IRQ Callback */
};

static struct DACinterface cirsa_dacInt = { 1, { 50 }};

static WRITE_HANDLER(bank_w) {
  cpu_setbank(1, memory_region(REGION_SOUND1) + data * 0x8000);
  logerror("SND BANK %x:%02x\n", offset, data);
}

static READ_HANDLER(port_r) {
  logerror("SND PORT %x READ\n", offset);
  return 0;
}

static WRITE_HANDLER(port_w) {
  logerror("SND PORT %x:%02x\n", offset, data);
}

static MEMORY_READ_START(cirsa_readsnd)
  { 0x00000, 0x07fff, MRA_ROM },
  { 0x08000, 0x0ffff, MRA_BANKNO(1) },
  { 0x10000, 0x107ff, MRA_RAM },
MEMORY_END

static MEMORY_WRITE_START(cirsa_writesnd)
  { 0x00000, 0x07fff, MWA_ROM },
  { 0x10000, 0x107ff, MWA_RAM },
  { 0x10800, 0x10800, bank_w },
  { 0x11000, 0x11000, DAC_0_data_w },
MEMORY_END

static PORT_READ_START(cirsa_readsndport)
  { 1, 1, AY8910_read_port_0_r },
  { 0, 3, port_r },
PORT_END

static PORT_WRITE_START(cirsa_writesndport)
  { 1, 1, AY8910_control_port_0_w },
  { 3, 3, AY8910_write_port_0_w },
  { 0, 3, port_w },
PORT_END

MACHINE_DRIVER_START(mephisto)
  MDRV_IMPORT_FROM(PinMAME)
  MDRV_CORE_INIT_RESET_STOP(CIRSA,NULL,NULL)
  MDRV_CPU_ADD_TAG("mcpu", I88, 6000000)
  MDRV_CPU_MEMORY(mephisto_readmem, mephisto_writemem)
  MDRV_CPU_VBLANK_INT(cirsa_vblank, 1)
  MDRV_NVRAM_HANDLER(generic_0fill)
  MDRV_SWITCH_UPDATE(CIRSA)

  MDRV_CPU_ADD_TAG("scpu", I8051, 12000000)
  MDRV_CPU_MEMORY(cirsa_readsnd, cirsa_writesnd)
  MDRV_CPU_PORTS(cirsa_readsndport, cirsa_writesndport)
  MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
  MDRV_SOUND_ADD(AY8910, cirsa_ay8910Int)
  MDRV_SOUND_ADD(DAC, cirsa_dacInt)
MACHINE_DRIVER_END

MACHINE_DRIVER_START(cirsa)
  MDRV_IMPORT_FROM(mephisto)
  MDRV_CPU_MODIFY("mcpu")
  MDRV_CPU_MEMORY(cirsa_readmem, cirsa_writemem)
  /* Sport 2000's audio board has a YM3812 (OPL2) with its own 14.318 MHz
     crystal; Mephisto's has neither -- just the 8051, an AY-3-8910 and a
     DAC-08.  MAME agrees: only its sport2k() config adds one. */
  MDRV_SOUND_ADD(YM3812, cirsa_ym3812Int)
MACHINE_DRIVER_END

INPUT_PORTS_START(cirsa)
  CORE_PORTS
  SIM_PORTS(1)
  PORT_START /* 0 */
    COREPORT_BIT(     0x0100, "Test",    KEYCODE_7)
    COREPORT_BIT(     0x0200, "Advance", KEYCODE_8)
    COREPORT_BIT(     0x0400, "EG1",     KEYCODE_9)
    COREPORT_BIT(     0x0800, "EG2",     KEYCODE_0)
INPUT_PORTS_END

static core_tLCDLayout cirsa_disp[] = {
  {0, 0, 0, 7,CORE_SEG8D}, {0,16, 7, 7,CORE_SEG8D},
  {3, 0,14, 7,CORE_SEG8D}, {3,16,21, 7,CORE_SEG8D},
  {6, 8,28, 2,CORE_SEG8D}, {6,14,30, 1,CORE_SEG8D}, {6,18,31, 2,CORE_SEG8D},
  {0}
};
/* hw.swCol counts CUSTOM switch columns beyond CORE_STDSWCOLS (12), not the
   game's hardware column count -- and coreGlobals.swMatrix is only
   CORE_MAXSWCOL (16) entries. Both games read swMatrix[1..10] via
   ic20_pc_r, which is inside the standard range, so neither needs a custom
   column for THAT path. A previous value of 10 made core.c:2000 read 22
   entries from a 16-entry array; Sport 2000's 8 (schematic 10) and
   Mephisto's 7 hardware columns are NOT this field, and that mistake is
   why it was reset to 0 for both games.

   cirsaGameData now sets it to 1: the quick-contact return path (see the
   IC9 PB/PC5 comment above ic9_pb_r) needs a swMatrix slot of its own,
   deliberately NOT column 11 -- that is core.h's CORE_FLIPPERSWCOL, and
   core_updateSw() unconditionally overwrites two of its bits every frame
   regardless of what a driver declares. Column 12 is the first genuinely
   free (CUSTOM) column, and hw.swCol == 1 is exactly what makes core.c's
   two CORE_STDSWCOLS+hw.swCol loops (core.c:2000, :2460) include it without
   going past CORE_MAXSWCOL -- 12+1=13, still inside the 16-entry array.
   mephistoGameData stays at 0: Mephisto has no quick-contact path (gated
   off by hw.gameSpecific1 in cirsa_vblank), so it needs no custom column. */
/* hw.gameSpecific1 (7th field of the hw sub-struct: flippers, swCol, lampCol,
   custSol, soundBoard, display, gameSpecific1) is the Sport-2000-vs-Mephisto
   switch: 0 = Sport 2000 (default), 1 = Mephisto/mephist1. It has five
   consumers, all gating Sport-2000-only decodes that are not established
   for Mephisto's board: cirsa_frameLen()'s frame length, cirsa_shift_frame()'s
   column-mask table select and its Mephisto write gate, ic9_pa_w's
   coil-bus decode, and cirsa_vblank's quick-contact gate (Mephisto has no
   quick-contact path modelled, see the comment just above mephistoGameData).
   It is not just the display's column-mask select -- characterising
   Mephisto's own 4094 chain removes one consumer, not all of them, and in
   particular does not touch the coil-bus gate that commit 20f52134 added
   to keep ic9_pa_w from populating coreGlobals.solenoids with fictitious
   Mephisto coil numbers, nor the quick-contact gate that a later commit
   added to cirsa_vblank. */
static core_tGameData cirsaGameData    = {0,cirsa_disp,{FLIP_SW(FLIP_L),1,8}};
static core_tGameData mephistoGameData = {0,cirsa_disp,{FLIP_SW(FLIP_L),0,8,0,0,0,1}};
static void init_cirsa(void) {
  core_gameData = &cirsaGameData;
}
static void init_mephisto(void) {
  core_gameData = &mephistoGameData;
}

ROM_START(mephisto)
  NORMALREGION(0x1000000, REGION_CPU1)
    ROM_LOAD("cpu_ver1.2", 0x00000, 0x8000, CRC(845c8eb4) SHA1(2a705629990950d4e2d3a66a95e9516cf112cc88))
      ROM_RELOAD(0x08000, 0x8000)
      ROM_RELOAD(0xf8000, 0x8000)
  NORMALREGION(0x20000, REGION_CPU2)
    ROM_LOAD("ic15_02", 0x00000, 0x8000, CRC(2accd446) SHA1(7297e4825c33e7cf23f86fe39a0242e74874b1e2))
  NORMALREGION(0x40000, REGION_SOUND1)
    ROM_LOAD("ic14_s0", 0x00000, 0x8000, CRC(7cea3018) SHA1(724fe7a4456cbf2ac01466d946668ee86f4410ae))
    ROM_LOAD("ic13_s1", 0x08000, 0x8000, CRC(5a9e0f1d) SHA1(dbfd307706c51f8809f4867a199b4b62beb64379))
    ROM_LOAD("ic12_s2", 0x10000, 0x8000, CRC(b3cc962a) SHA1(521376cab7e917a5d5f5f183bccb21bd13327c48))
    ROM_LOAD("ic11_s3", 0x18000, 0x8000, CRC(8aaa21ec) SHA1(29f17249cac62128fd8b0eee415ce399ee2ec672))
    ROM_LOAD("ic16_c",  0x20000, 0x8000, CRC(5f12b4f4) SHA1(73fbdb57fca0dbc918e6665a6cb949e741f2720a))
    ROM_LOAD("ic17_d",  0x28000, 0x8000, CRC(d17e18a8) SHA1(372eaf209ea5d26f3c096aadd7d028ef68bfb68e))
    ROM_LOAD("ic18_e",  0x30000, 0x8000, CRC(eac6dbba) SHA1(f4971c8b0aa3a72c396b943a0ee3094afb902ec1))
    ROM_LOAD("ic19_f",  0x38000, 0x8000, CRC(cc4bb629) SHA1(db46be2a8034bbd106b7dd80f50988c339684b5e))
ROM_END
/* init_mephisto is a real function (above) that selects mephistoGameData,
   not init_cirsa -- Mephisto uses its own column-mask table. */
#define input_ports_mephisto input_ports_cirsa
CORE_GAMEDEFNV(mephisto,"Mephisto (rev. 1.2)",1986,"Stargame",mephisto,GAME_NOT_WORKING)

ROM_START(mephist1)
  NORMALREGION(0x1000000, REGION_CPU1)
    ROM_LOAD("cpu_ver1.1", 0x00000, 0x8000, CRC(ce584902) SHA1(dd05d008bbd9b6588cb204e8d901537ffe7ddd43))
      ROM_RELOAD(0x08000, 0x8000)
      ROM_RELOAD(0xf8000, 0x8000)
  NORMALREGION(0x20000, REGION_CPU2)
    ROM_LOAD("ic15_02", 0x00000, 0x8000, CRC(2accd446) SHA1(7297e4825c33e7cf23f86fe39a0242e74874b1e2))
  NORMALREGION(0x40000, REGION_SOUND1)
    ROM_LOAD("ic14_s0", 0x00000, 0x8000, CRC(7cea3018) SHA1(724fe7a4456cbf2ac01466d946668ee86f4410ae))
    ROM_LOAD("ic13_s1", 0x08000, 0x8000, CRC(5a9e0f1d) SHA1(dbfd307706c51f8809f4867a199b4b62beb64379))
    ROM_LOAD("ic12_s2", 0x10000, 0x8000, CRC(b3cc962a) SHA1(521376cab7e917a5d5f5f183bccb21bd13327c48))
    ROM_LOAD("ic11_s3", 0x18000, 0x8000, CRC(8aaa21ec) SHA1(29f17249cac62128fd8b0eee415ce399ee2ec672))
    ROM_LOAD("ic16_c",  0x20000, 0x8000, CRC(5f12b4f4) SHA1(73fbdb57fca0dbc918e6665a6cb949e741f2720a))
    ROM_LOAD("ic17_d",  0x28000, 0x8000, CRC(d17e18a8) SHA1(372eaf209ea5d26f3c096aadd7d028ef68bfb68e))
    ROM_LOAD("ic18_e",  0x30000, 0x8000, CRC(eac6dbba) SHA1(f4971c8b0aa3a72c396b943a0ee3094afb902ec1))
    ROM_LOAD("ic19_f",  0x38000, 0x8000, CRC(cc4bb629) SHA1(db46be2a8034bbd106b7dd80f50988c339684b5e))
ROM_END
/* mephist1 (rev 1.1) has the identical column-mask table content as
   mephisto (rev 1.2), confirmed by a byte scan of its ROM -- see the
   comment above colFromBitMephisto -- so it reuses init_mephisto rather
   than init_cirsa. */
#define init_mephist1 init_mephisto
#define input_ports_mephist1 input_ports_cirsa
CORE_CLONEDEFNV(mephist1,mephisto,"Mephisto (rev. 1.1)",1986,"Stargame",mephisto,GAME_NOT_WORKING)

ROM_START(sport2k)
  NORMALREGION(0x1000000, REGION_CPU1)
    ROM_LOAD("u1_256.bin", 0x00000, 0x8000, CRC(403f9000) SHA1(376dc17355c9569bd1ed9b19dbc322bfd69bf938))
    ROM_LOAD("u2_256.bin", 0x08000, 0x8000, CRC(4a88cc10) SHA1(591568dc60c40cc058f45a144c098faccb4e970c))
      ROM_RELOAD(0xf8000, 0x8000)
  NORMALREGION(0x20000, REGION_CPU2)
    ROM_LOAD("c541_256.bin", 0x00000, 0x8000, CRC(7ca4a952) SHA1(6b01f7f79fa88c4ae71a6a19341760fa256b9958))
  NORMALREGION(0x50000, REGION_SOUND1)
    ROM_LOAD("s117_512.bin", 0x00000, 0x10000, CRC(035d302e) SHA1(f207ea239e5a34839366cc19a569ab5f3d1e1a60))
    ROM_LOAD("s211_512.bin", 0x10000, 0x10000, CRC(61cf84f9) SHA1(4c5680fbf48f30fbe0e15f4194ab708955df7721))
    ROM_LOAD("s311_512.bin", 0x20000, 0x10000, CRC(162cd1ff) SHA1(4d9ad7a839cc16e74abfc77c92674608ccba8cc3))
    ROM_LOAD("s411_512.bin", 0x30000, 0x10000, CRC(4deffaa0) SHA1(98a20a01437ea060ac5c6fb52f4da892fee1fb75))
    ROM_LOAD("s511_512.bin", 0x40000, 0x10000, CRC(ca9afa80) SHA1(6f219bdc1ad06e340b2930610897b70369a43684))
ROM_END
#define init_sport2k init_cirsa
#define input_ports_sport2k input_ports_cirsa
CORE_GAMEDEFNV(sport2k,"Sport 2000",1988,"Cirsa",cirsa,GAME_NOT_WORKING)
