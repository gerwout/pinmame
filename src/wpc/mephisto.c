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
  int   lampSel;        /* a lamp column has just been selected and the row
                           byte for it has not arrived yet -- see ic20_pb_w */
  int   lampPrev;       /* last lamp column that actually received row data,
                           so the sweep wrap can be spotted in ic20_pb_w */
  int   swCol;          /* IC20 PA0-3 -> IC30 (7445) -> switch columns CC0-CC9 */
  UINT8 shiftFrame[8];  /* one pass through the 4094 display chain, 6 or 8
                           bytes long -- see cirsa_frameLen() */
  int   shiftPos;
  UINT8 lastKeys;       /* previous cabinet key state, for edge-only updates */
  UINT8 lastPlayKeys;   /* coin 1/2/3 + start, previous state */
  UINT8 lastTrough;     /* ball trough toggle, previous state */
  int   ppcero;         /* PPCERO, the mains zero-cross pulse: MUART P11,
                           and (through IC26/IC24) the MUART's EXTINT pin */
  /*-- phase 0 instrumentation state --*/
  int   lastrep;
  char  lastline[192];
  UINT8 qcState;        /* live quick-contact inputs, swMatrix[12] bits 0-7 */
  UINT8 qcLatch;        /* IC11 (74LS373) held value, read back on IC9 PB */
  int   qcTransparent;  /* IC9 PC5 high -> latch follows input */
  UINT8 sndToSnd;       /* last byte the MUART sent, latched for the 8051 */
  UINT8 p2Out;          /* last value written to MUART port 2, for edge detect */
  UINT8 muartP1Out;     /* MUART port 1 output latch; bit 6 is the 8051's P3.2 */
  UINT8 sndP1;          /* 8051 port 1 output latch = the AY-3-8910 data bus */
  UINT8 sndP3;          /* 8051 port 3 output latch; bits 4/5 = BDIR/BC1 */
  UINT8 sndP2;          /* 8051 port 2 output latch = XRAM address bits 8-15 */
  UINT8 ayPortA;        /* AY-3-8910 IOA output latch -- the audio board's own
                           column strobe and pulsed outputs, see ay8910_porta_w */
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
/  CORRECTION (fix round 2): f[0]/f[1]/f[3]/f[4] are NOT four independent
/  7-digit player displays.  They are two pairs of high/low shift-frame
/  bytes driving the two 7-character LA8041R-11B alphanumeric rows
/  (cirsa_disp positions 0..13); f[2] and f[5] are the two real 7-digit
/  LTS 3401 rows (manual: DIS15-21, DIS27-33; cirsa_disp positions
/  14..27) that the prior mapping wrongly called unassigned.  The credit/
/  match/extra-ball digits 28..32, fed entirely by f[6] sliced across its
/  five active columns (0-1, 2, 3-4), are unaffected and correct.
/
/  DISPLAY ROUND: the mapping above is now implemented, for both games,
/  not just diagnosed.  docs/findings/2026-09-02-alphanumeric-segments.md
/  settled the one piece fix round 2 left open -- which byte of each
/  high/low pair is low vs high, and what CORE_SEG* type the two
/  alphanumeric rows need -- by disassembling the ROM's own
/  character-staging routine and decoding two independently-captured live
/  buffers ("NO AUDIO", "SPORT 2000") byte-exact through the resulting
/  font table.  Mephisto's own chain -- five segment groups, no
/  alphanumeric units, credit board first then the four player boards --
/  was independently characterised the same way (docs/findings/
/  2026-09-01-mephisto-display-chain.md) and its gate (see
/  cirsa_shift_frame) is now open, having reproduced its own boot-time
/  "NO AUDIO" message.  See cirsa_shift_frame's own comment for the exact
/  byte-to-segment mapping for both games.
/
/  Not established by any of this: the specific left-to-right identity of
/  the two alphanumeric rows or the two numeric rows on Sport 2000 (which
/  is literally "Display 1" versus "Display 2", or which cabinet position
/  that is), nor which physical row is upper/lower for Mephisto's four
/  player boards.  A wrong row order still renders readable text, just in
/  the wrong place -- settling it needs a live multi-player display test,
/  which needs the switch matrix wired.  Left open deliberately; do not
/  guess it.
/
/  SEGMENT REMAP ROUND: the bytes placed at each of these positions are,
/  as of this round, no longer written raw.  Correct character data was
/  landing in coreGlobals.segments[] with the ROM's own segment bit order
/  (a=3 b=7 c=5 d=4 e=1 f=2 g=6, plus the 16-segment font's own low-byte
/  strokes), which is not PinMAME's -- so it drew as scrambled strokes,
/  not the intended letters/digits.  docs/findings/
/  2026-09-03-low-byte-strokes.md sec 5 solved both translation tables
/  (mechanically, from core.c's own core_ascii2seg16/core_bcd2seg7/
/  segSize1[0]) and cirsa_seg16()/cirsa_seg8d() (defined below, just
/  ahead of cirsa_shift_frame -- see their own block comment) now apply
/  them on write.  coreGlobals.segments[] holds PinMAME-order values from
/  here on.
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

/*-- Segment bit-order remap -----------------------------------------------
/  The ROM's own segment bit order (both the 7-segment numeric font at
/  0xC143 and the low/high halves of the 16-segment alphanumeric font at
/  0xBA08) is NOT PinMAME's.  Before this remap, coreGlobals.segments[]
/  held correct character *data* drawn with the wrong strokes -- see
/  docs/findings/2026-09-03-low-byte-strokes.md sec 5 (derived
/  mechanically from core.c's own core_ascii2seg16, segSize1[0] and
/  core_bcd2seg7) for the full derivation.  Applied here, on write, so
/  coreGlobals.segments[] ends up holding PinMAME-order values -- what
/  core_seg_video_update() and any front end actually expect.
/
/  Numeric groups (CORE_SEG8D -- all of Mephisto's five, Sport 2000's two
/  7-digit rows and its credit/match/EB digits): ROM bit -> segment ->
/  core_bcd2seg7 bit, one-to-one, per the finding's sec 5.2.  ROM bit 0
/  (dp) is dropped -- not because CORE_SEG8D lacks a period bit (it has
/  one, bit 7, per segSize1C[4] at core.c:354) but because neither ROM
/  font ever needs it: the digit font at 0xC143 never sets bit 0, and
/  where a numeric row is fed from the alphanumeric font's high byte,
/  bit 0 is D2 (a D1 duplicate), not a dot -- mapping it would light a
/  spurious period.
/
/  Alphanumeric groups (CORE_SEG16N, Sport 2000 only): 16 ROM bits carry
/  14 independent strokes plus two hard-wired duplicates -- A2 (low bit 2)
/  always mirrors A1 (high bit 3), D2 (high bit 0) always mirrors D1 (high
/  bit 4) -- corroborated by the manual's own A1/A2 and D1/D2 pin pairs on
/  Plate 15.  Both members of each pair are mapped to the SAME PinMAME
/  bit (a harmless redundant OR); every other bit is an independent stroke
/  mapped one-to-one to its PinMAME bit, per the finding's sec 5.1.
/
/  One deliberate departure from that section's own prose: it suggested
/  also forcing high bit 6 (G, the middle bar) to set PinMAME bit 11 (the
/  right half of PinMAME's own split middle bar) whenever G fires, on top
/  of low bit 7 (R) separately targeting bit 11, reasoning that the real
/  chip's G pin is a single full-width segment.  Checked against the
/  oracle that section itself recommends -- coreGlobals.segments[] should
/  equal core_ascii2seg16[] for a given character -- that forced OR is
/  wrong: it makes 'F' (the ROM's one letter that sets G without R) light
/  a right-hand nub PinMAME's own 'F' does not have, while the plain
/  one-bit-to-one-bit mapping below reproduces PinMAME's 'F' exactly,
/  because the ROM already lights R alongside G on every other letter
/  that wants a full-width bar.  Kept as two independent single-bit
/  mappings instead.
/
/  Low-byte bit 7 (R) is settled, not moderate confidence.  Plate 15's
/  DIS1 symbol lists 16 named segment pins in two 8-pin lanes -- 8
/  outer-ring (A1 A2 B C D1 D2 E F) and 6 inner-cross (H J K M N P) --
/  leaving only G and R to cover the middle bar, so a part that already
/  splits A1/A2 and D1/D2 must split the middle bar too.  An exhaustive
/  search over all 16 PinMAME bits (plus "drop") for this bit's target
/  scores bit 11 at 24/26 on A-Z, a six-point margin over every other
/  candidate (17-18); ROM R agrees with PinMAME bit 11 on 35/36 letters,
/  the same rate G gets against bit 6 -- nobody hedges G.  See
/  docs/findings/2026-09-03-low-byte-strokes.md sec 4/5.1 and
/  .superpowers/segment-remap-review.md sec 1-2.
/
/  Two font-design mismatches are known and are NOT remap bugs -- do not
/  chase them by editing these tables:
/    - 'E': the ROM's 'E' sets both G and R (a full-width middle bar).
/      Both candidate mappings for R (bit 11 alone, or forced-OR with G
/      per the rejected sec 5.1 prose above) produce the identical
/      remapped word for 'E', so 'E' is evidence about neither one -- it
/      differs from core_ascii2seg16['E'] only because PinMAME's
/      Rockwell-derived font draws 'E' (and 'F') with a half-width
/      crossbar, left half only, where the ROM (and the real hardware)
/      draws a full-width bar.
/    - 'K': the ROM's 'K' has high byte 0x00 -- it lights no outer-ring
/      stroke at all -- while core_ascii2seg16['K'] lights e and f.  No
/      permutation of the low byte can create or destroy an outer-ring
/      segment, so this is provably a font-drawing difference too, not a
/      low-byte decode error.  Neither letter is exercised by the four
/      verified strings (NO AUDIO / SPORT 2000 / GAME OVER / UNIDESA
/      CIRSA).
/  Every other bit is pinned by intersecting constraints with zero
/  contradictions across the solid 0x20-0x5A range.
/---------------------------------------------------------------------*/

/* Numeric font bit -> core_bcd2seg7 bit.  ROM order a=3 b=7 c=5 d=4 e=1
   f=2 g=6 dp=0 (scripts/decode_display.py, re-verified docs/findings/
   2026-09-03-low-byte-strokes.md sec 5.2); core_bcd2seg7 (core.c:137) is
   the classic a=0 b=1 c=2 d=3 e=4 f=5 g=6, no dp bit. */
static const UINT8 cirsa_seg8dBit[8] = {
  0,      /* 0: dp -- CORE_SEG8D's period is bit 7 (segSize1C[4],
             core.c:354), but neither ROM font ever needs it: the digit
             font never sets bit 0, and in the alphanumeric high byte
             bit 0 is D2, not dp -- dropped, not unmapped */
  1 << 4, /* 1: e */
  1 << 5, /* 2: f */
  1 << 0, /* 3: a */
  1 << 3, /* 4: d */
  1 << 2, /* 5: c */
  1 << 6, /* 6: g */
  1 << 1, /* 7: b */
};

static UINT8 cirsa_seg8d(UINT8 v) {
  UINT8 out = 0;
  int b;
  for (b = 0; b < 8; b++)
    if (v & (1 << b)) out |= cirsa_seg8dBit[b];
  return out;
}

/* Alphanumeric font, high byte bit -> CORE_SEG16N bit (docs/findings/
   2026-09-03-low-byte-strokes.md sec 5.1). Bits 1-7 are the same a-g
   outline as the numeric font, same positions; bit 0 is D2, the mirrored
   twin of D1 (bit 4) -- see the block comment above. */
static const UINT16 cirsa_seg16HiBit[8] = {
  1 << 3,  /* 0: D2 -> d  (mirrors D1) */
  1 << 4,  /* 1: E  -> e */
  1 << 5,  /* 2: F  -> f */
  1 << 0,  /* 3: A1 -> a */
  1 << 3,  /* 4: D1 -> d */
  1 << 2,  /* 5: C  -> c */
  1 << 6,  /* 6: G  -> g */
  1 << 1,  /* 7: B  -> b */
};

/* Alphanumeric font, low byte bit -> CORE_SEG16N bit.  Bit 2 is A2, the
   mirrored twin of A1 (hi bit 3); the rest are the six-stroke internal
   cross plus R (bit 7, settled -- see the block comment above). */
static const UINT16 cirsa_seg16LoBit[8] = {
  1 << 12, /* 0: M */
  1 << 14, /* 1: K */
  1 << 0,  /* 2: A2 -> a  (mirrors A1) */
  1 << 8,  /* 3: H */
  1 << 13, /* 4: L */
  1 << 10, /* 5: J */
  1 << 9,  /* 6: I */
  1 << 11, /* 7: R  -- settled, see block comment above */
};

static UINT16 cirsa_seg16(UINT8 lo, UINT8 hi) {
  UINT16 out = 0;
  int b;
  for (b = 0; b < 8; b++) {
    if (hi & (1 << b)) out |= cirsa_seg16HiBit[b];
    if (lo & (1 << b)) out |= cirsa_seg16LoBit[b];
  }
  return out;
}

static void cirsa_shift_frame(const UINT8 *f, int len) {
  const UINT8 *colFromBit = core_gameData->hw.gameSpecific1
                              ? colFromBitMephisto : colFromBitSport2k;
  UINT8 sel = (UINT8)~f[len - 1];
  int col, bit;

  /* Exactly one of the seven digit drivers is on at a time, so the column
     byte must have exactly one clear bit.  0xFF (nothing selected) and
     anything with two or more clear bits are both rejected: a byte that is
     not a valid column select means the frame is not a frame, and a blank
     display is a far better symptom of that than a plausible digit. */
  if (!sel || (sel & (sel - 1))) return;
  for (bit = 0; !(sel & (1 << bit)); bit++) ;
  if (colFromBit[bit] == 0) return;               /* bit unused as a column */
  col = colFromBit[bit] - 1;                      /* 0..6 digit position */

  /* Which of the shift-frame bytes feeds which physical group -- both
     games now characterised (docs/findings/
     2026-09-01-mephisto-display-chain.md, docs/findings/
     2026-09-02-alphanumeric-segments.md), and every byte pair below now
     goes through cirsa_seg16()/cirsa_seg8d() (defined above, see their
     block comment) rather than being written raw, so coreGlobals.
     segments[] holds PinMAME-order strokes, not ROM-order ones:

       Sport 2000 (7 segment groups + column). f[0]/f[1]/f[3]/f[4] are NOT
       four independent 7-digit displays (see docs/findings/
       2026-08-31-display-groups.md's fix-round-2 retraction for why that
       reading was wrong); they are two high/low byte pairs, each driving
       one 7-character LA8041R-11B alphanumeric row:
         f[3] (low) / f[4] (high) -> segments[0..6],   CORE_SEG16N, via cirsa_seg16()
         f[0] (low) / f[1] (high) -> segments[7..13],  CORE_SEG16N, via cirsa_seg16()
         f[5]                     -> segments[14..20], CORE_SEG8D (LTS 3401), via cirsa_seg8d()
         f[2]                     -> segments[21..27], CORE_SEG8D (LTS 3401), via cirsa_seg8d()
         f[6], columns 0-4        -> segments[28..32] (credit/match/EB), via cirsa_seg8d()

       Mephisto (5 segment groups + column). Its panel has no alphanumeric
       units -- 33 identical LTS 3401 seven-segment digits, all CORE_SEG8D
       (mephisto_disp), all via cirsa_seg8d(). Chain order is credit board
       first (plate 11: J23 = DATA IN from the control board), then the
       four player boards (Plate 1's harness trace: CREDITOS -> JUG.4 ->
       JUG.3 -> JUG.2 -> JUG.1), which puts credit/column last in the
       frame and gives the natural 1-4 order for the rest -- corroborated,
       not just inferred from the trace, by the boot-time "NO AUDIO"
       message reading correctly left to right across f[0] then f[1]:
         f[0] -> segments[0..6]   (Player 1)
         f[1] -> segments[7..13]  (Player 2)
         f[2] -> segments[14..20] (Player 3)
         f[3] -> segments[21..27] (Player 4)
         f[4], columns 0-4        -> segments[28..32] (credit/match/EB)
                                      -- ASSUMED, see below

     f[4]'s column slice is carried over from Sport 2000's f[6] pattern
     (below), NOT independently established for Mephisto: no findings doc
     identifies which five of the credit board's seven columns carry its
     five LTS 3401s -- Plate 11 lists DP1..DP5 against TR1..TR7 with no
     stated correspondence, and the only column this task observed f[4]
     non-zero at is column 2 (the credit digit in the "NO AUDIO" capture).
     Sport 2000's f[6] slice, by contrast, is independently established
     (docs/findings/2026-08-29-display-buffer.md's addendum traced actual
     writers to offsets 42-46, i.e. columns 0-4, and found none for 5-6).

     Not established for either game: which physical row is upper/lower on
     the cabinet (which alphanumeric or numeric row is "first", which
     player position is physically where) -- a wrong row order still
     renders readable text, just in the wrong place, and settling it needs
     a live multi-player display test once the switch matrix is wired. Do
     not guess it here. */
  if (core_gameData->hw.gameSpecific1) {
    coreGlobals.segments[0 * 7 + col].w = cirsa_seg8d(f[0]);
    coreGlobals.segments[1 * 7 + col].w = cirsa_seg8d(f[1]);
    coreGlobals.segments[2 * 7 + col].w = cirsa_seg8d(f[2]);
    coreGlobals.segments[3 * 7 + col].w = cirsa_seg8d(f[3]);
    if (col < 5) coreGlobals.segments[28 + col].w = cirsa_seg8d(f[4]);  /* credit/match/EB, cols 0-4 */
    return;
  }

  coreGlobals.segments[0 * 7 + col].w = cirsa_seg16(f[3], f[4]);
  coreGlobals.segments[1 * 7 + col].w = cirsa_seg16(f[0], f[1]);
  coreGlobals.segments[2 * 7 + col].w = cirsa_seg8d(f[5]);
  coreGlobals.segments[3 * 7 + col].w = cirsa_seg8d(f[2]);
  if (col < 5) coreGlobals.segments[28 + col].w = cirsa_seg8d(f[6]);  /* credit/match/EB, cols 0-4 */
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

/*-- PPCERO, and what actually drives EXTINT -----------------------------
/  Traced on the scanned plates at 600 dpi, and the SAME circuit with the
/  same designators on both boards -- Sport 2000 plate 5 (PDF p.28) and
/  Mephisto plate 4 (PDF p.24), IC25 LM393 / IC26 74HC02 / IC24 74LS14:
/
/      LM393 IC25 (comparator B, pins 5,6 -> 7, open collector, pulled up
/      by 4K7 to 5V on Sport 2000 / 1K on Mephisto)
/          |
/          +--> MUART Port 1 pin 38 = P11, labelled PPCERO on the Sport
/          |    2000 plate ("paso por cero", mains zero crossing)
/          |
/          +--> 74HC02 IC26 pin 6  ..+
/                                    :  NOR gate 2 -> pin 4 -> 74LS14 IC24
/      MUART P12 (pin 37, an OUTPUT) :                         pin 9 -> pin 8
/          -> 74LS14 IC24 pin 3      :                               |
/             -> pin 4 -> IC26 pin 5 ..+                             v
/                                                        8256 pin 16 EXTINT
/
/  i.e. EXTINT = PPCERO OR NOT(P12).  Every wire in that chain was followed
/  by pixel-tracing the plate, not read off the transcription: the EXTINT
/  lane leaves pin 16, runs straight down and turns left into IC24's pin 8
/  output; P11's lane ends on the continuous horizontal that runs from the
/  IC25/IC26 node across to it; P12's lane runs down to IC24's pin 3.
/
/  Two ROM-side facts corroborate it.  Both level-2 (EXTINT) ISRs read P11
/  as the very first thing they do -- sport2k 0x0937 `test [0xA010],2`,
/  mephisto 0x143B `test [0x2010],2` -- which only makes sense if EXTINT and
/  P11 carry the same signal; and Mephisto uses that bit to decide whether
/  to update the INH LF / INH FLIP / INH L.C. outputs on MUART port 2
/  (0x145C-0x14BC), i.e. it re-times the mains-switched loads on the zero
/  crossing, which is exactly what PPCERO is for.
/
/  Sport 2000 goes further and gates the interrupt with it: its ISR writes
/  RSTINT = 4 (disable level 2) when it finds P11 high (0x093E) and SETINT
/  = 4 (enable) when it finds it low (0x0973), and the main loop re-arms the
/  level the same way (0x0894/0x089B and 0x08F6/0x08FD).  So it takes at
/  most one level-2 interrupt per half cycle by construction.
/
/  NOT modelled, deliberately: the NOT(P12) half of the OR.  Both ROMs
/  clear P12 (with P13) immediately before their first `sti` -- sport2k
/  0x05B9 and mephisto 0x0783, both `and Port1, 0xF3` -- and neither ever
/  sets it again, so on the real board that term holds EXTINT asserted for
/  the whole run and the interrupt rate is set purely by the ROM's own
/  SETINT/RSTINT gating above.  i8256.c raises a request only on a 0->1
/  transition of the pin ("if (!old && i8256.extint)"), so feeding it a
/  permanently-asserted level would yield exactly one level-2 interrupt for
/  the entire session.  Driving the pin with PPCERO instead reproduces the
/  rate the ROM is written around, and leaves i8256.c's shared semantics
/  alone.  If i8256.c ever grows a true level-sensitive EXTINT that
/  re-requests after EOI, this is the place to revisit.
/
/  The 100 Hz figure is 50 Hz Spanish mains, full-wave rectified.
/
/  PPCERO IS A NARROW HIGH PULSE, NOT A SQUARE WAVE, and that matters --
/  it gates Sport 2000's whole coil pipeline.  Read the comparator off the
/  same plate: R16 1K feeds node A from +12 and Z2, a 3 V zener, clamps it;
/  R17 10K takes node A to IC25 pin 6 (inverting), while R18 4K7 / R19 1K
/  divide 5 V down to 0.877 V on pin 5 (non-inverting).  The open-collector
/  output therefore sits LOW for as long as node A is above 0.877 V and is
/  released HIGH by R13 only in the sliver either side of the crossing
/  where the unsmoothed rail falls below it -- asin(0.877/~17 V) each way,
/  about 6 degrees of the 180 degree half cycle, i.e. ~0.33 ms of every
/  10 ms.  CIRSA_ZC_PULSE_US is that sliver.
/
/  Why it is load bearing.  Sport 2000 runs its coils from a four phase
/  round robin (0x08B3), phase 3 of which -- 0xC3CA, the pulse/hold state
/  machine that turns SolenoidOn requests into the frame bytes -- is the
/  only writer of [0x67F]/[0x686]/[0x68D].  The phase counter [0x31] is
/  forced back to 0 by EVERY pass that finds P11 high (0x08AA, 0x090C, and
/  the level-2 ISR at 0x095A), and it only advances on alternate passes of
/  a ~727 Hz timer-1 ISR, so reaching phase 3 needs ~8.3 ms of unbroken
/  P11-low.  With the 50 % square this code used to emit, the low half was
/  5 ms: [0x31] never once got past 2 in an eight minute game, phase 3
/  never ran, the frame stayed 'C0 81 82 83 84 85 86 87' with no data bit,
/  and coreGlobals.solenoids never left 0.  Measured across a sweep, same
/  build, same stimulus, coin+START then four seconds of sampling:
/
/     high time  50us 150us 300us 700us 1500us | 3000us | 5000us (old)
/     phase 3 in  20   20    20    20    20    |   8    |    0    of 20
/     coils        y    y     y     y     y    |   y    |   none
/
/  so anything from a hairline pulse up to ~1.5 ms behaves identically and
/  the shipped 5 ms was the one value that broke it.  The plateau is wide
/  because the reset does not depend on a main-loop pass happening to land
/  inside the pulse -- the level-2 ISR is entered on the rising edge and
/  reads P11 as its first instruction, so a crossing narrower than one
/  timer-1 tick still resets the counter.  It is not airtight either way:
/  at 300 us [0x31] samples 4..8 about a quarter of the time, i.e. the odd
/  crossing does go unnoticed and the round robin simply runs an extra lap
/  of do-nothing phases.  That is harmless -- phases 4+ fall straight
/  through to the increment -- and 0..3 still complete every crossing that
/  is seen.
/
/  Mephisto needs the pulse for the opposite reason -- its level-2 ISR
/  (0x143B) updates INH LF / INH FLIP / INH L.C. only when it finds P11
/  HIGH, so resting the pin low would freeze those outputs.  Its coils are
/  not gated on P11 at all (the only Port 1 reads in that ROM are 0x096E,
/  0x0FAD and the ISR itself), which is why this bug was Sport 2000 only.
/----------------------------------------------------------------------*/
#define CIRSA_ZC_HZ 100          /* mains zero crossings per second */
#define CIRSA_ZC_PULSE_US 300    /* PPCERO high time per crossing, see above */

static void cirsa_zc_off(int dummy) {
  locals.ppcero = 0;
  i8256_set_extint(0);
}

static void cirsa_zc_tick(int dummy) {
  locals.ppcero = 1;
  i8256_set_extint(1);
  timer_set(TIME_IN_USEC(CIRSA_ZC_PULSE_US), 0, cirsa_zc_off);
}

static UINT8 cirsa_p1_in(void) {
  /* Port 1 inputs (PORT1C = 5C leaves P10, P11, P15 and P17 as inputs):
       P10 S.C.MAT  - the lamp-matrix fault sense; see below
       P11 PPCERO   - mains zero crossing, see the block comment above
       P15          - the sound board's BUSY line, driven by the 8051's P3.3;
                      it arrives through i8256_set_p1_pin(5,...) from
                      cirsa_sndp3_w, not from here, so this callback must
                      leave it alone (i8256_port1_read ORs the two sources)
       P17 F.T.     - "falta de tension", power failure

     P17 must rest LOW.  CMD1.BITI routes it to interrupt level 1 and the
     datasheet is explicit that a low-to-high transition is what signals the
     fault, so a pin stuck high reads as a permanent power failure and the
     ROM restarts.

     P10 rests HIGH -- "no matrix fault".  Each ROM reads Port 1 bit 0 in
     exactly one place, the lamp scan, and in both it is a health check with
     HIGH = healthy:

       sport2k  0x0B080  mov al,[0xA010] / and al,1 / mov [0x523],al
                0x0B0A3  test [0x523],1 -> set  -> per-column counter := 0
                         else -> counter++, and at 6 -> [bx+0x583] := 1, the
                         column's "lamp failed" flag
       mephisto 0x00FAD  mov ax,[0x2010] / and al,1
                0x00FB2  set -> [bx+0x96] := 0, else ++ and at 7 -> [0x8A]
                         := 0xFF, the global lamp-fault flag, after which
                         0x0FD3 turns the whole matrix off

     With the old `return 0x00` stub the pin was permanently low, so both
     tests condemned every lamp: Mephisto printed FALLO LUCES n. 00..63 and
     Sport 2000's matrix went dark a few seconds into attract.  This is
     measured, not assumed -- an A/B with P10 driven and P11 driven is in
     docs/findings/2026-09-01-extint-and-port1.md section 4; P11 alone
     changes nothing about the lamps, P10 alone fixes both games.

     Both plates label the net S.C. MAT and the ROM's use of it -- sampled
     with the lamp rows switched off, HIGH meaning "nothing is drawing
     current that should not be" -- reads as a matrix short/overload sense.
     That reading is inference; what is measured is only the polarity the
     ROM demands. */
  return (UINT8)(0x01 | (locals.ppcero ? 0x02 : 0x00));
}

static void cirsa_p1_out(UINT8 data) {
  /* P14 = CL-WD (watchdog kick), P12/P13 not yet used.

     P16 is the other half of the sound link's byte handshake and it is the
     one thing the sound board cannot run without.  It goes to the 8051's
     P3.2 (INT0), and the sound firmware's byte transmitter is

         024E: SETB P3.2         ; release the pin
         0250: LCALL 024D        ; (a bare RET -- a delay)
         0253: JNB  P3.2, 0250h  ; spin until the main CPU drives it HIGH
         0256: CLR  21h.0
         0258: MOV  SBUF,A

     (Mephisto has the identical routine at 0x0222/0x0227.)  JNB on a port
     bit reads the pin, not the latch, so it lands in cirsa_sndp3_r below.
     While this stub did nothing that read returned 0 and the loop never
     exited: docs/findings/2026-08-31-sound-firmware.md section 6 counted
     the sound CPU entering 0x0250 1,475,198 times in 34.65 s and reaching
     0x0256 zero times, so it never executed a single one of the commands
     it had already received and checksummed.

     The main ROM drives it per byte, not as a level: 0x08EE raises it
     ("I am ready to receive"), 0x0915 lowers it again, and ISR_SerialRX
     clears it at 0xC9A1 on every byte that arrives.  i8256_port1_write()
     hands us the value already masked by PORT1C (0x5C, so P16 is an
     output), which is why nothing in i8256.c has to change. */
  locals.muartP1Out = data;
}

/*-- MUART Port 2 (plate 5) ---------------------------------------------
/  P20 EG1, P21 EG2, P22 TEST, P23 AVANCE are inputs, buffered through
/  74HC240s from J4.2 and J6 with 10K pull-ups, so a pressed button reads
/  as 1.  P24 RST ASIN (sound board reset), P25 INH LF, P26 INH FLIP and
/  P27 INH L.C. are outputs -- these are the bits the ROM sets and clears
/  around 0x0A29, not display strobes as first assumed.
/----------------------------------------------------------------------*/
static void cirsa_p2_out(UINT8 data) {
  /* P24 = RST ASIN, the sound board's reset line.  Both manuals draw the same
     circuit at the audio-board end -- Sport 2000 plate 11 (PDF p.34) and
     Mephisto plate 9 (PDF p.29), same designators in both: T1, a BC237 NPN
     wired as an emitter follower.  Collector to +5V, emitter straight to the
     8051's RST pin, base to RST ASIN with R19 3K3 pulling it up to +5V; R18
     1K from RST to ground and C29 1uF from RST to +5V are the 8051's ordinary
     power-on reset network.  There is no inverter in the path, and the 8051's
     RST is active high, so the line is non-inverting: RST ASIN HIGH holds the
     sound CPU in reset, LOW releases it.  The 3K3 pull-up also means the board
     sits in reset until something drives the line low, which is exactly what
     both ROMs' boot sequence assumes -- OR P2,0x10 / short delay /
     AND P2,0xEF / long delay / poll STATUS.RBF for the sound CPU's power-on
     0xA5 (sport2k 0x05EF-0x0603).

     Edge triggered on purpose.  The ROM writes port 2 for the three inhibit
     lines as well, and cpu_set_reset_line() schedules its work through
     timer_set(), so re-asserting on every write would keep re-suspending the
     8051.  scpu is CPU 1: mcpu is added first in MACHINE_DRIVER_START(mephisto).

     P25 INH LF, P26 INH FLIP and P27 INH L.C. still have no consumer. */
  if ((data ^ locals.p2Out) & 0x10)
    cpu_set_reset_line(1, (data & 0x10) ? ASSERT_LINE : CLEAR_LINE);
  locals.p2Out = data;
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
/  byte lands is what would pull it through cirsa_snd_rx() into SBUF and
/  raise RI -- same pattern as capcoms.c's send_data_to_8752() -- once the
/  firmware actually reaches IE.ES=1, which today it never does: this is
/  wiring, not a working handshake.  check_interrupts() in i8051.c
/  dispatches Timer 0/1 and External 0/1 off the raw flag bit plus the
/  global EA alone, without each source's own individual enable bit in
/  IE, so the boot ROM's bare SETB EA takes a spurious Timer-1 interrupt
/  into a trampoline that never executes RETI and permanently locks the
/  interrupt subsystem before MOV IE,#092h (the instruction that would set
/  ES) is ever reached.  That is a defect in the shared i8051 core, not in
/  this driver; see docs/findings/2026-09-01-sound-link.md for the traced
/  PCs and the fix this needs.  The case does not look at the line state
/  at all, so a bare ASSERT_LINE per byte is enough; there is nothing to
/  clear afterwards.  scpu (the 8051) is cpu 1 here -- mcpu is added first
/  in MACHINE_DRIVER_START(mephisto).
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
/  rows back through IC31, a 74HC14 -- ONE inverting stage, not two.
/
/  Polarity, and why it matters: plate 6 (PDF p. 29) shows the six CF row
/  lines resting high through AR9 (4K7 to +5V), entering IC31's inputs
/  (7414 pins 13/9/3/11/1/5) through the AR7 10K series array, with IC31's
/  outputs (pins 12/8/4/10/2/6 -- the bubbled ends in the drawing) wired
/  straight to IC20 PC0-PC5 (package pins 37/38/39/1/2/5).  IC30 (7445)
/  pulls the selected column LOW, a closed switch pulls its row LOW with
/  it, and the single inversion turns that into a HIGH at Port C.  So a
/  closed switch reads as 1 here, and this handler must NOT invert.
/  Mephisto's manual names the same IC31 74HC14 on its own switch rows.
/
/  The ROM agrees, three ways over: 0x0CD31 copies the Port C byte into the
/  debounced level array at [0x72B] with no NOT of its own and latches an
/  event on each 0->1 transition, so an event has to mean a closure; the
/  cabinet buttons on MUART port 2 (cirsa_p2_in, non-inverting) and the
/  quick contacts on IC9 Port B (ic9_pb_r, likewise) already go through
/  that identical debouncer; and SWITCH TEST 5-PHASE displays an event
/  whenever 0x0CF57 finds its level bit set, which the manual describes as
/  showing "the switch being activated".
/
/  Inverting here made every column's level byte read 0x3F at rest, i.e.
/  all 60 switches permanently closed.  That saturated the event array at
/  [0x737] to 0x3F as well, moved every event to the release edge, and left
/  the coin debouncer at 0x068CA seeing all three chutes stuck closed --
/  which trips its jam path (15 consecutive closed samples: [0x2EE] >= 0x0F
/  -> [0x2ED] = 0x64, return) before it ever services chutes 2 and 3.  Coins
/  credited only in the narrow window where that counter had just wrapped:
/  measured 1 of 12 pulses at a 1.2 s closure and 0 of 18 at shorter ones,
/  against 18 of 18 after this fix.  See docs/findings/2026-09-02-coins.md.
/----------------------------------------------------------------------*/
static WRITE_HANDLER(ic20_pa_w) {
  int col = (data >> 4) & 0x07;
  /* A change in PA4-6 re-points IC29, so the next Port B write is the row
     byte for the newly selected column.  ic20_pb_w consumes that flag; the
     sweep-wrap latch lives there too, because Mephisto passes through
     column 0 on its way to every column (see below) and latching here
     would fire eight times a sweep instead of once. */
  if (col != locals.lampCol) locals.lampSel = 1;
  locals.swCol   = data & 0x0f;
  locals.lampCol = col;
}

/*-- IC20 Port B: the lamp rows, and why only one write per column counts --
/  Both ROMs write Port B more than once per lamp column, and only the
/  write that follows the column select carries lamp data.  Traced live
/  with a temporary logerror in this handler (Sport 2000, one lamp lit in
/  LAMP TEST 3-PHASE, cycle counts from activecpu_gettotalcycles64):
/
/    PA 5a  select column 5, PA7 low        cyc T
/    PB xx  the row data          (0xB062)  T +     50
/    PA da  PA7 back high         (0xB065)  T +     56
/    PB 00  rows off              (0xB07B)  T +  8,220   <- fault test
/    PB ff  ~"failed lamp" mask   (0xB093)  T +  8,317   <- fault test
/    PB xx  the row data again    (0xB13C)  T +  8,877
/    PB 00  rows off              (0xB03D)  T + 16,389   <- next pass
/    PA 6a  select column 6                 T + 16,492
/
/  0xB06B's fault test samples MUART P10 with the rows off and then drives
/  every not-yet-failed row for ~560 cycles.  With no failed lamps that
/  mask is 0xFF, so on a healthy machine every column is handed 0xFF once
/  per pass -- and core_setLamp ORs into tmpLampMatrix, so the old handler
/  reported all 64 lamps on, permanently, whatever the game was doing.
/  That is exactly what /api/info showed: FFFFFFFFFFFFFFFF, unchanging,
/  with a single lamp selected in the ROM's own LAMP TEST.
/
/  The transient is real on the board but it is not a lit lamp: 560 cycles
/  against the 8,220 a genuinely-driven row gets in the same slot, i.e. a
/  15:1 duty ratio (0.43 % vs 6.3 % of the whole eight-column sweep).  A
/  matrix of on/off bits cannot express that, so this handler takes the row
/  byte the ROM sets up for the column and ignores the fault test's
/  re-writes.  It does NOT special-case 0xFF -- a column legitimately
/  driving all eight rows still reports all eight.
/
/  Mephisto (0x0FDE) writes Port B twice per column, and again only the
/  second one is data:
/
/    PA xor 0x80   PA7 toggles, column unchanged        (0x0FDE)
/    PB 00         rows off                             (0x0FE8)
/    PA and 0x8F   column momentarily 0                 (0x0FF3)
/    PA or  al     the real column                      (0x0FF8)
/    PB xx         the row data                         (0x1000)
/
/  Its read-modify-write of PA is what forces the sweep-wrap latch to live
/  here rather than in ic20_pa_w: 0x0FF3 drops the column field to 0 on the
/  way to every column, so a "col == 0" test in the PA handler fired once
/  per column, latching and clearing tmpLampMatrix eight times a sweep and
/  leaving coreGlobals.lampMatrix holding a single column.  That is why
/  Mephisto's game-start lamp state read 0000009C00000000 -- one non-zero
/  byte -- rather than a whole matrix.  lampPrev tracks the last column
/  that actually received data, so the wrap is detected on real sweeps
/  only.
/----------------------------------------------------------------------*/
static WRITE_HANDLER(ic20_pb_w) {
  if (!locals.lampSel) return;   /* a fault-test re-write, not lamp data */
  locals.lampSel = 0;
  /* The lamp columns are strobed 0..7 in order, so accumulate a whole
     sweep and latch it when the sweep wraps.  Latching on the video frame
     instead chops the sweep and drops whichever columns straddle the
     boundary. */
  if (locals.lampCol == 0 && locals.lampPrev != 0) {
    memcpy((void *)coreGlobals.lampMatrix, (void *)coreGlobals.tmpLampMatrix,
           sizeof(coreGlobals.tmpLampMatrix));
    memset((void *)coreGlobals.tmpLampMatrix, 0, sizeof(coreGlobals.tmpLampMatrix));
  }
  locals.lampPrev = locals.lampCol;
  core_setLamp(coreGlobals.tmpLampMatrix, 1 << locals.lampCol, data);
}

static READ_HANDLER(ic20_pc_r) {
  /* IC30 decodes 0-9 only; for 10-15 no column is pulled low, so no switch
     can pull a row low and every buffered row reads back as "open" -- 0
     under this active-high convention. */
  if (locals.swCol > 9) return 0x00;
  return coreGlobals.swMatrix[locals.swCol + 1] & 0x3f;
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
/  Derived by running each ROM's own COILS TEST and correlating with the
/  coil number it displays.  Mephisto's write-up is in this fork, at
/  docs/findings/2026-08-31-mephisto-coils.md; Sport 2000's is
/  docs/findings/2026-08-30-coil-encoding.md in the workspace repo this
/  driver is developed alongside (there is no such path here).
/
/  THE TWO GAMES NUMBER THE POSITIONS IN OPPOSITE DIRECTIONS.  Same wires,
/  same connectors, same frame -- but Sport 2000 puts coil N at position
/  N%8 while Mephisto puts it at 7-(N%8), so a decode written for one game
/  mislabels 6 of every 8 coils on the other (only N%8 == 3 and 4 survive).
/  It is not an inference from the schematic, it is what each ROM does:
/
/    Sport 2000  SolenoidOn(N) (0xC7D8, table 0xC81E) sets mask 1<<(N%8)
/                in the group byte, and the frame builder emits group-byte
/                bit p at position p  ->  position N%8.
/    Mephisto    COIL TEST's element routine (0x3178, mephist1 0x2E01 --
/                byte-identical) does `mov al,0x80 / ror al,cl` with
/                cl = N%8, i.e. mask 0x80>>(N%8), and its frame builder
/                (0x126C, mephist1 0x103F) emits group-byte bit p at
/                position p  ->  position 7-(N%8).
/
/  Confirmed live on the machine, all 24 coils and both ROM revisions, by
/  walking Mephisto's own COIL TEST ("TEST / BOBINAS / -4-FASE / N nn")
/  one element at a time and reading the displayed number against the PA
/  byte on the bus.  The four group boundaries, verbatim from that walk
/  (bit 6 is the heartbeat and rides along at positions 0 and 3, which is
/  why some bytes read 0xC../0xD../0xE.. rather than 0x8./0x9./0xA.):
/
/    N 00 -> 0x8F  N 07 -> 0xC8   (PA3, group 0, positions 7 .. 0)
/    N 08 -> 0x97  N 15 -> 0xD0   (PA4, group 1, positions 7 .. 0)
/    N 16 -> 0xA7  N 23 -> 0xE0   (PA5, group 2, positions 7 .. 0)
/
/  The bus transaction itself is the same for both games, instruction for
/  instruction -- Mephisto at 0x130D, Sport 2000 at 0xC277: select the
/  position, write PA, mask PC to 000, pulse PA bit 7 low then high,
/  restore PC to 111 -- and both memory maps route their IC9 through this
/  same handler.  It was only ever the numbering that differed.
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

  /* Both games are decoded here now.  The gate that used to sit at this
     point ("Sport 2000 only -- Mephisto's coil numbering is not
     established") is gone because the numbering IS established: see the
     block comment above for the per-game position order and the live
     COIL TEST walk that settled it.  What is still missing for Mephisto is
     upstream of this handler, not in it -- nothing drives the MUART's
     EXTINT pin for Mephisto, and the ISR that builds and sends the coil
     frame is its interrupt level 2 (vector type 0x42 -> 0x1416), so with
     the driver as it stands Mephisto's ROM only ever writes the watchdog
     byte here (0x00/0x40/0xC0: position 0, all three data bits clear) and
     coreGlobals.solenoids correctly stays 0.  The decode below is what
     runs the moment that pin is connected. */
  for (blk = 0; blk < 3; blk++) {
    /* coil number == mask bit; Mephisto counts the position the other way
       round (7-pos), see the block comment. */
    const int coil = blk * 8 +
                     (core_gameData->hw.gameSpecific1 ? (7 - pos) : pos);
    const UINT32 bit = 1u << coil;
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
  if (!core_gameData->hw.gameSpecific1)              /* Sport 2000 only */
    coreGlobals.solenoids |= (locals.qcState & 0x0e); /* coils 1-3 only  */

  /* Feed the PWM integrator with the same 24-bit state, including the
     quick-contact OR above -- this is what MACHINE_INIT(CIRSA)'s
     core_set_pwm_output_type(CORE_MODOUT_SOL0, 24, ...) call is for. This
     handler runs on every hardware write (~280/s), not once a vblank, so
     it is a far better source for the integrator than sampling
     coreGlobals.solenoids from cirsa_vblank would be -- see p2k.c:2013-2018
     on why a once-a-frame sample loses short pulses the integrator needs
     to see.  Both games now: Mephisto reaches this with all-zero data
     until its EXTINT is connected, which is an honest "everything off",
     not an unfed integrator. */
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
/  coreGlobals.swMatrix[12] bits 0-7 carry the live contact state.  This is
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

static READ32_HANDLER(cirsa_eram_addr);   /* defined with the sound ports below */

/* The ball trough: a whole column's worth of switches held closed by the balls.
   Used both by MACHINE_INIT, which seeds them, and by SWITCH_UPDATE's toggle. */
static const UINT8 cirsaTrough[2] = {6, 0x3c};   /* BALL TROUGH 1-4  */
static const UINT8 mephTrough[2]  = {7, 0x0e};   /* elements 38,39,40 */

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
     honestly. Both games: Mephisto's numbering on this same bus is now
     established too (see ic9_pa_w's block comment), it has the same 24
     coils on the same three connectors, and ic9_pa_w feeds the integrator
     for it on exactly the same schedule. */
  /* Put the balls in the trough, which is where they are on a machine someone
     just switched on.  These switches are a level held closed by the balls, not
     an event, and with them open both ROMs park in BALL WAITING (sport2k) or
     no bola (mephisto) -- a state that is not attract, does not poll the
     service check, and cannot be left by any cabinet button.  Seeding here is
     what lets either game be started straight from the command line instead of
     needing a wrapper to close them first.

     The "Ball Trough" toggle in SWITCH_UPDATE still works: locals.lastTrough is
     0 after the memset above and the port bit reads 0, so the two agree, and
     the first press of the key sets what is already set.  A second press
     removes the balls, which is the useful direction for testing an empty
     trough. */
  {
    const UINT8 *t = core_gameData->hw.gameSpecific1 ? mephTrough : cirsaTrough;
    coreGlobals.swMatrix[t[0]] |= t[1];
  }

  coreGlobals.nSolenoids = 24;
  core_set_pwm_output_type(CORE_MODOUT_SOL0, 24, CORE_MODOUT_SOL_2_STATE);

  i8256_init(&cirsa_i8256);
  /* PPCERO: a narrow 100 Hz pulse on MUART P11 and, through IC26/IC24,
     on the MUART's EXTINT pin.  One timer per crossing; the falling edge
     is scheduled by cirsa_zc_tick itself.  See cirsa_p1_in's block
     comment for why the width matters. */
  timer_pulse(TIME_IN_HZ(CIRSA_ZC_HZ), 0, cirsa_zc_tick);
  i8155_init(&cirsa_i8155);
  cpu_set_irq_callback(0, cirsa_irq_callback);

  /* Setup serial line callbacks, needs to be set before CPU reset by
     design -- see nuova.c's uboat65 init for the same warning. */
  i8051_set_serial_tx_callback(cirsa_snd_tx);
  i8051_set_serial_rx_callback(cirsa_snd_rx);
  i8051_set_eram_iaddr_callback(cirsa_eram_addr);
}

/* Coin 1/2/3 and Start, as {swMatrix index, bit mask}, measured through each
   ROM's own SWITCH TEST -- see docs/reference/switch-matrices.md.  Sport 2000
   takes all three chutes and Start on one column; Mephisto spreads them, and
   its chute order comes from the ROM's own tables at 0x1C49 / 0x1C4C. */
static const UINT8 cirsaCoinSw[4][2] = {
  /* Coin 1 is the centre chute on purpose, not the 25 chute.  The chutes are
     priced differently -- 25 chute takes TWO coins per credit, centre gives 1,
     100 gives 3 -- and key 5 is the one a person reaches for first, so it has
     to be the one that visibly does something on a single press.  Putting the
     two-for-one chute there makes the machine look broken. */
  {7, 0x10},  /* Coin 1 -> centre chute, 1 credit      */
  {7, 0x08},  /* Coin 2 -> 25 chute, 2 coins = 1 credit */
  {7, 0x20},  /* Coin 3 -> 100 chute, 3 credits        */
  {7, 0x04}   /* Start                                 */
};
static const UINT8 mephCoinSw[4][2] = {
  {6, 0x10},  /* Coin 1 -> chute 0, 1 credit  */
  {4, 0x20},  /* Coin 2 -> chute 1, 2 credits */
  {7, 0x10},  /* Coin 3 -> chute 2, 5 credits */
  {4, 0x04}   /* Start                        */
};

static SWITCH_UPDATE(CIRSA) {
  /* Write a bit only when the key behind it has actually changed.
     Rewriting the whole row every frame stamps out anything else that set
     one of these switches -- a front end, or the remote debugger -- before
     the ROM has had a chance to poll it.  Same reasoning as rfranco.c. */
  if (inports) {
    const int meph = core_gameData->hw.gameSpecific1;
    const UINT8 (*coin)[2] = meph ? mephCoinSw : cirsaCoinSw;
    const UINT8 *trough    = meph ? mephTrough : cirsaTrough;
    UINT8 keys    = (UINT8)((inports[CORE_COREINPORT] >> 8) & 0x0f);
    UINT8 changed = (UINT8)(keys ^ locals.lastKeys);
    UINT8 now, diff;
    int i;

    if (changed) {
      coreGlobals.swMatrix[0] = (UINT8)((coreGlobals.swMatrix[0] & ~changed) |
                                        (keys & changed));
      locals.lastKeys = keys;
    }

    /* Coin 1/2/3 and Start, same change-only discipline, one bit each. */
    now  = (UINT8)(inports[CORE_COREINPORT] & 0x0f);
    diff = (UINT8)(now ^ locals.lastPlayKeys);
    for (i = 0; i < 4; i++)
      if (diff & (1 << i)) {
        const UINT8 idx = coin[i][0], msk = coin[i][1];
        if (now & (1 << i)) coreGlobals.swMatrix[idx] |=  msk;
        else                coreGlobals.swMatrix[idx] &= ~msk;
      }
    locals.lastPlayKeys = now;

    /* Ball trough -- a toggle, so follow its level rather than its edge. */
    now  = (UINT8)((inports[CORE_COREINPORT] >> 4) & 1);
    if (now != locals.lastTrough) {
      if (now) coreGlobals.swMatrix[trough[0]] |=  trough[1];
      else     coreGlobals.swMatrix[trough[0]] &= ~trough[1];
      locals.lastTrough = now;
    }
  }
}

static INTERRUPT_GEN(cirsa_vblank) {
  core_updateSw(TRUE);
  {
    /* All EIGHT bits, and for BOTH games.  Each ROM numbers eight quick
       contacts as switch-test elements 60-67 and reads them off IC9 Port B:
       Sport 2000 through the event table at 0xD11E (offset 0x0A, bits 0-7),
       whose SWITCH TEST reference table at 0x4F23 names them by IC21's J7
       pins 03, 04, 07, 05, 08, 09, 06, 10; Mephisto through [0xDA], filled
       at 0x103D and tested for elements 60-67 at 0x31F4.  Both were
       measured live through their own SWITCH TEST after this widening:
       swMatrix[12] bit N reads back as element 60+N, 8 of 8, on sport2k and
       on mephisto/mephist1.

       Two things were wrong here before.  The mask was 0x1f, so bits 5-7
       (elements 65, 66, 67) could not be stimulated at all -- Sport 2000's
       manual lists only five populated quick contacts, but the ROM carries
       eight, exactly as it carries switch columns 8 and 9 that the manual
       does not populate either.  And the whole update was gated on
       hw.gameSpecific1, which left all eight of Mephisto's unreachable.

       Nothing downstream widens with them: ic9_pa_w still ORs only bits 1-3
       back into the coil bus, and still only for Sport 2000. */
    const UINT8 qc = coreGlobals.swMatrix[12];
    if (qc != locals.qcState) {
      locals.qcState = qc;
      if (locals.qcTransparent) locals.qcLatch = qc;   /* '373 is transparent */
      /* The quick contacts used to raise EXTINT from here, as a stand-in
         for a source nobody had identified.  They do not drive it on the
         real board: EXTINT comes off IC26/IC24 and carries PPCERO (see
         cirsa_p1_in's block comment), and the contacts reach the CPU only
         through IC11, the 74LS373 latched here in locals.qcLatch, which
         ISR_QuickContacts reads back over IC9 Port B.  With PPCERO wired
         that ISR runs 100 times a second and polls the latch on every one
         of them, so a contact held for a whole frame cannot be missed, and
         it costs nothing to re-read an unchanged latch: the ROM's consumer
         at 0xC8E7 is an edge detector on its own mirror of the byte
         (`xchg [0x6b3],al / xor / and`), so a value that has not changed
         raises no event no matter how often it is sampled. */
    }
  }
}

/*-- The AY-3-8910's own I/O ports (plate 11 / Mephisto plate 9) ---------
/  IOA is an OUTPUT and IOB an INPUT: the sequencer's register-7 special
/  case forces exactly that on every write ((v & 0x3F) | 0x40, sport2k
/  0x0F50, mephisto 0x0862), and no score can override it.
/
/  They are not the playfield lamp or switch matrix.  They are the audio
/  board's own connectors: both manuals letter J17 "T COLUMN 1..5, T ROW
/  1..3", J18 "A COLUMN 3/4/5, A ROW 4/5", J19 "COUNTER 1/2" and J20
/  "COIN 1/2/3, COIN INHIBIT 1/2" (Sport 2000 plate 11, PDF p.34;
/  Mephisto plate 9, PDF p.29 -- identical lists).
/
/  Mephisto's firmware uses them and shows the layout directly.  0x05E6
/  writes IOA from IRAM 0x43 on every 100 Hz tick, and 0x05A6 reads IOB
/  back for the column just strobed and stores it at XRAM 0x0074+col:
/
/      05A6: MOV P1,#0Fh          ; AY register 15 = port B
/      05AF: MOV P1,#FFh          ; release the 8051's bus
/      05B2: ORL P3,#20h          ; BC1 alone = READ DATA
/      05B5: MOV 44h,P1
/      05C1: MOV A,#74h / ADD A,41h / MOV R0,A / MOVX @R0,A
/
/  The column pattern comes from a five-entry table at 0x05E1, which reads
/  08 10 20 40 80 -- one bit each for IOA bits 3..7, five columns.  IRAM
/  0x41 cycles 0..4, so the strobe is a walking bit at 100/5 = 20 Hz per
/  column; measured live it is exactly that, 5,785 IOA writes in 59.4 s
/  cycling 10 20 40 80 08.  Bits 1 and 2 are separate: 0x05F9 masks 0x43
/  with 0x06 and sets them from two down-counters (IRAM 0x29/0x2A, loaded
/  by the 0xA4/0xA8 immediate commands) every 8 ticks, i.e. two pulsed
/  outputs -- COUNTER 1 and COUNTER 2, or the two coin inhibits.
/
/  Sport 2000 has the same connectors but does not use them: it writes IOA
/  exactly once, at 0x0485 from the boot path with IRAM 0x41 = 0x07, and
/  never reads IOB at all (measured: 1 IOA write and 0 IOB reads in 60 s).
/  Its coin door and switch matrix are on the CPU board instead.
/
/  What was here before -- IOA -> coreGlobals.tmpLampMatrix[0] and IOB <-
/  coreGlobals.swMatrix[0]/[1] -- is a MAME placeholder and is wrong twice
/  over.  tmpLampMatrix[0] IS lamp column 0 (ic20_pb_w fills it through
/  core_setLamp), so on Mephisto the column strobe overwrote that column
/  100 times a second with 08/10/20/40/80; and feeding the playfield
/  switch matrix back in as the audio board's rows put phantom closures on
/  its coin and keypad inputs.  Neither game's audio board drives a lamp.
/
/  IOB now reads 0xFF, i.e. no closure.  The rows sit on the 10K pull-up
/  networks AR1-AR4 and the columns are driven through IC6, a ULN2064
/  Darlington array, so a closure pulls a row LOW; MAME's own mephisto
/  driver returns 0xFF here for the same reason.  [INFERRED] -- the row
/  polarity is read off the part types, not off a traced net, and nothing
/  in either firmware consumes XRAM 0x0074-0x0078, so no behaviour
/  distinguishes 0x00 from 0xFF today.
/----------------------------------------------------------------------*/
static READ_HANDLER(ay8910_porta_r)   { return locals.ayPortA; }
static READ_HANDLER(ay8910_portb_r)   { return 0xff; }
static WRITE_HANDLER(ay8910_porta_w)  { locals.ayPortA = data; }
static WRITE_HANDLER(ay8910_portb_w)  { }

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

/*-- Sound ROM banking (plate 11, PDF p.34) ------------------------------
/  IC20, a 74LS373, latches the bank byte.  Three of its outputs drive
/  IC22 (74LS138) A/B/C, whose Y0-Y7 are CST0-CST7, the chip selects of the
/  eight EPROM sockets -- in board order IC14, IC13, IC12, IC11, IC16, IC17,
/  IC18, IC19.  A fourth output leaves the latch as the net A15F and goes to
/  pin 1 of every socket, with a 10K pull-up (R16, 8x10K).  There is no
/  inverter on that path, so bit 3 reaches A15 directly: 0 = low half.
/
/  Pin 1 is Vpp on a 27256 (harmlessly held at +5V by the pull-up) and A15 on
/  a 27512.  The sockets are silkscreened "27256-(27512)" for exactly that
/  reason, and the two games populate them differently:
/
/    Mephisto  eight 27256 (0x8000 each), CST0..CST7 -> 0x00000..0x38000.
/              A15F is Vpp here and does nothing to the address, so bit 3
/              must be MASKED OFF: chip = data & 7.
/    Sport2000 five 27512 (0x10000 each) on CST0..CST4.  bits 0-2 pick the
/              chip, bit 3 picks the 32K half within it.
/
/  Mephisto's firmware settles its own arithmetic.  Its PCM descriptor table
/  is at ic15_02 0x073E, three bytes per entry (start page, page count,
/  bank), 24 entries, and every bank byte in it is 0x08-0x0F -- bit 3 always
/  set, because on a 27256 pin 1 is Vpp and has to sit at +5V for the part
/  to read at all.  Under the old `data * 0x8000` those select 0x40000 to
/  0x78000 inside a REGION_SOUND1 that is 0x40000 long: all 24 descriptors
/  out of bounds, and measured live, every bank write the firmware makes is
/  out of range (18 of 18 in a 60 s run driven with sample commands).  With
/  bit 3 masked off all 24 land inside the region on data that is
/  unmistakably 8-bit unsigned PCM -- median mean-absolute-first-difference
/  10.5 against 83.9 for uniform random, 79% of bytes within 0x50-0xB0 of
/  mid-scale.  MAME's own mephisto driver encodes the same conclusion from
/  the other side: it loads the eight ROMs at 0x40000-0x7FFFF of a 0x80000
/  region and banks with `data & 0xf`, i.e. bank 0x08 = ic14_s0.
/
/  The old flat arithmetic was right for Mephisto and wrong for Sport 2000,
/  whose descriptor table uses 0x00-0x04 and 0x08-0x0C.  Under data*0x8000
/  those ran off the end of a 0x50000 region -- 0x0B -> 0x58000 and
/  0x0C -> 0x60000, both out of bounds, 11,771 times in 60 s of attract --
/  and the most common value, 0x09, silently addressed the wrong chip.
/
/  Bit 3's polarity is confirmed twice over: the schematic path above, and a
/  measurement -- DAC discontinuity across a bank change is 2.73x the
/  within-bank step size non-inverted against 3.55x inverted.
/
/  cirsa_cst[] is the other half, and it is not a re-ordering of anything
/  the schematic says: it is which physical chip sits in which socket.  The
/  five Sport 2000 27512s go in the same descending sockets Mephisto uses --
/  CST0 = IC14, CST1 = IC13, CST2 = IC12, CST3 = IC11, CST4 = IC16 -- but
/  their dump names count the other way, s1 in IC11 through s4 in IC14 and
/  s5 in IC16, so CST0..CST4 hold s411, s311, s211, s117, s511.  ROM_START
/  loads them in name order, which is upstream's and MAME's order and is
/  left alone; this table maps the CST index onto it.
/
/  Derived from the sample set, not guessed.  The descriptor table tiles
/  each 32K bank, and the pages it does not claim must be the pages the
/  EPROM does not program.  Scoring all 5! x 2 candidate maps (chip order x
/  bit-3 polarity) against the 49 fully-0xFF pages in the 320K set leaves
/  exactly one that claims none of them; the runner-up claims 2 and the
/  order used before this change claims 41.  The same map is the only one
/  whose per-bank unclaimed tail matches the block's erased tail for all
/  ten bank values (15, 12, 12, 5, 3, 1, 1 pages and three full banks), and
/  it puts 89% of descriptor first and last bytes within 8 counts of
/  mid-scale against 60% for the old one -- samples that start and end in
/  silence.
/
/  Live confirmation, sport2k, 60 s headless with the five sample commands
/  whose scripts reach those erased pages (0x6B, 0x6D, 0x73, 0x74, 0x7C):
/  DAC writes at full scale 0xFF fall from 75,771 to 360.  Under the old
/  order 13% of every sample played was erased EPROM held at full scale.
/-----------------------------------------------------------------------*/
static WRITE_HANDLER(bank_w) {
  /* CST0..CST4 -> which 64K chip of REGION_SOUND1, in ROM_START's load order
     s117, s211, s311, s411, s511.  CST5..CST7 are unfitted on this board and
     never appear in the descriptor table; they fold onto chip 0. */
  static const UINT8 cirsa_cst[8] = { 3, 2, 1, 0, 4, 0, 0, 0 };
  UINT32 off;

  if (core_gameData->hw.gameSpecific1)          /* Mephisto: 8 x 27256 */
    off = (data & 0x07) * 0x8000;
  else                                          /* Sport 2000: 5 x 27512 */
    off = cirsa_cst[data & 0x07] * 0x10000 + (((data >> 3) & 1) * 0x8000);

  cpu_setbank(1, memory_region(REGION_SOUND1) + off);
  logerror("SND BANK %x:%02x -> %05x\n", offset, data, off);
}

static READ_HANDLER(port_r) {
  logerror("SND PORT %x READ\n", offset);
  return 0;
}

static WRITE_HANDLER(port_w) {
  logerror("SND PORT %x:%02x\n", offset, data);
}

/*-- The sound CPU's ports 1 and 3 (plate 11 / Mephisto plate 9) ----------
/  Port 1 is the AY-3-8910's 8-bit data bus and port 3 bits 4 and 5 are its
/  BDIR and BC1.  The driver used to map port 1 straight to
/  AY8910_control_port_0_w and port 3 to AY8910_write_port_0_w, which is not
/  a near miss but the wrong pins: the AY then received the four states of
/  the handshake port -- c7, cf, df, ff -- as its register data and never a
/  music byte.  Measured on the tree as it stood, sport2k over 40 s: 14,353
/  data writes carrying exactly those four values, into whichever register a
/  stray P1 write had last selected -- 13,503 of them into register 0 and
/  320 into the two I/O port registers 14 and 15.  After this change the
/  same run makes 300 data writes and they are the score.
/
/  The firmware's output primitive (sport2k 0x0F40, Mephisto 0x0862) is the
/  AY's own BDIR/BC1 sequence written out longhand:
/
/      0F40: MOV P1,R1            ; register number on the data bus
/      0F44: ORL P3,#30h          ; BDIR=1 BC1=1 -> LATCH ADDRESS
/      0F47: ANL P3,#CFh          ; both low     -> INACTIVE
/      0F4C: MOV A,R3             ; the value
/      0F4D: CJNE R1,#07h,0F54h
/      0F50: ANL A,#3Fh / ORL A,#40h   ; register 7 -> IOA out, IOB in
/      0F54: MOV P1,A
/      0F58: ORL P3,#10h          ; BDIR=1 BC1=0 -> WRITE DATA
/      0F5B: ANL P3,#CFh
/
/  so bit 4 is BDIR and bit 5 is BC1.  Mephisto pins that down from the
/  other side at 0x05A6, which is the only port 1 *read* in either ROM:
/  MOV P1,#FFh releases the bus, ORL P3,#20h selects BC1 alone = READ DATA,
/  and MOV 44h,P1 takes the byte.
/
/  The strobes are acted on when the state changes, so the read-modify-write
/  P3 accesses the firmware makes for the two handshake bits (SETB P3.2,
/  MOV P3.3,C) cannot re-trigger a latch: they leave bits 4-5 at 0.
/----------------------------------------------------------------------*/
static WRITE_HANDLER(cirsa_sndp1_w) {
  locals.sndP1 = data;                  /* just the data-bus latch */
}

static READ_HANDLER(cirsa_sndp1_r) {
  /* Only while BC1 alone is asserted does the AY drive the bus; the rest of
     the time the 8051's own quasi-bidirectional latch is what a read sees. */
  if ((locals.sndP3 & 0x30) == 0x20) return AY8910_read_port_0_r(0);
  return locals.sndP1;
}

static WRITE_HANDLER(cirsa_sndp3_w) {
  UINT8 prev = locals.sndP3;
  locals.sndP3 = data;
  if ((data ^ prev) & 0x30) {
    if ((data & 0x30) == 0x30)      AY8910_control_port_0_w(0, locals.sndP1);
    else if ((data & 0x30) == 0x10) AY8910_write_port_0_w(0, locals.sndP1);
  }
  /* P3.3 (INT1, an output here) is the sound board's BUSY line, raised on
     entry to the serial ISR at 0x0375 and the 100 Hz tick at 0x0450 and
     restored from flag 20h.6 on the way out.  It lands on MUART Port 1
     bit 5, which the main ROM tests at 0xCB41 before transmitting. */
  if ((data ^ prev) & 0x08)
    i8256_set_p1_pin(5, (data & 0x08) ? 1 : 0);
}

static WRITE_HANDLER(cirsa_sndp2_w) {
  locals.sndP2 = data;                  /* the XRAM page -- see cirsa_eram_addr */
}

/*-- MOVX @Ri needs Port 2 for the high address byte ---------------------
/  The firmware pages its 2K of XRAM and sets P2 explicitly every time (it
/  even keeps a shadow of P2 in IRAM 26h and restores it): page 0 for the
/  serial packet buffer (0x010B, 0x0186), page 1 for the FM voice state
/  (0x0545), page 2 for the OPL2 register shadow (0x0626, 0x0967, 0x098F)
/  and page 3 for the note timers (0x0D00).  PinMAME's MOVX @Ri asks the
/  driver for the full address through this callback and, with none
/  registered, falls back to the bare 8-bit offset -- so all four pages
/  landed on page 0.  That is what made the OPL2 silence sweep read its
/  shadow out of the packet buffer, and what kept the FM voice engine at
/  0x0578 reading the serial packet buffer instead of voice state
/  (docs/findings/2026-08-31-sound-firmware.md sections 3 and 8).
/
/  Two things this must get right.  The callback is shared with MOVX @DPTR
/  (i8051ops.c:621,640), which already has all 16 bits and must be returned
/  untouched -- mem_mask tells the two apart, 0xFF for @Ri and 0xFFFF for
/  @DPTR -- and getting that wrong would take the DAC at 0x1000, the OPL2 at
/  0x1800 and the ROM bank latch at 0x0800 off the map.  And P2 has to come
/  from a shadow kept here rather than from i8051_internal_r(0xA0): that
/  routes through sfr_read(), which for a port with RWM clear (MOVX does not
/  set it) does not return the latch but calls IN(2), i.e. reads the port
/  back through this same driver.  Measured: with the shadow reading 0x01
/  and 0x02, i8051_internal_r(0xA0) returned 0x00 every time.  spinb.c's
/  dmd_eram_address keeps its own P2 copy for the same reason.
/----------------------------------------------------------------------*/
static READ32_HANDLER(cirsa_eram_addr) {
  if (mem_mask > 0xff) return offset;   /* MOVX @DPTR -- already complete */
  return (UINT32)((locals.sndP2 << 8) | (offset & 0xff));
}

static READ_HANDLER(cirsa_sndp3_r) {
  /* P3.2 (INT0) is an input driven by MUART Port 1 bit 6 -- see
     cirsa_p1_out().  Every other pin of this port is either an output or
     unconnected, and reads low, exactly as it did through port_r before. */
  return (UINT8)((locals.muartP1Out & 0x40) ? 0x04 : 0x00);
}

static MEMORY_READ_START(cirsa_readsnd)
  { 0x00000, 0x07fff, MRA_ROM },
  { 0x08000, 0x0ffff, MRA_BANKNO(1) },
  { 0x10000, 0x107ff, MRA_RAM },
  { 0x11800, 0x11800, YM3812_status_port_0_r },   /* OPL2 status; see the write map */
MEMORY_END

static MEMORY_WRITE_START(cirsa_writesnd)
  { 0x00000, 0x07fff, MWA_ROM },
  { 0x10000, 0x107ff, MWA_RAM },
  { 0x10800, 0x10800, bank_w },
  /* The same latch, reached through a second address.  Still [INFERRED]:
     the schematic trace has not been read off the plate, and section 5 of
     docs/findings/2026-08-31-sound-firmware.md is the argument.

     The Timer 0 ISR services two independent PCM streams and writes a bank
     number immediately before each one's MOVC: stream 1 to xdata 0x0F00
     (0x0C2C-0x0C2F, which reaches DPTR 0x0F00 by DEC DPH from 0x1000) and
     stream 2 to xdata 0x0800 (0x0C42-0x0C46).  Only the second was mapped,
     so every one of stream 1's bank writes was dropped.

     There is exactly one banked window -- the 8051's code space is 64K,
     0x0000-0x7FFF is the firmware EPROM, and both streams read from
     0x8000-0xFFFF (the descriptor's start page is loaded as table[0] + 0x80
     at 0x0CC2 and 0x0CDA) -- so there is one latch to drive it.  Plate 11
     (PDF p.34) has a 74LS373 feeding IC22, a 74LS138 whose Y0..Y7 are
     CST0..CST7; a decode of the 0x0800-0x0FFF block that ignores A8-A10
     aliases that latch across the whole block, 0x0800 and 0x0F00 included.
     Mephisto's firmware writes only 0x0800, which is why it makes no
     unmapped access and why this line changes nothing for it.

     The falsifying observation section 5 proposed -- the two streams
     running at once from different banks -- turns out not to be one.
     Because each stream rewrites the bank immediately before its own MOVC,
     one shared latch and two independent latches produce identical results
     for this firmware, so the two hypotheses are not distinguishable by
     observing it, and mapping 0x0F00 here is right under either.  What can
     be said is that the two addresses now demonstrably carry the same kind
     of value: measured over 40 emulated seconds of sport2k attract on the
     deterministic build, 0x0800 takes 288,212 writes carrying 00, 01, 03,
     09, 0B and 0C, and 0x0F00 takes 95,444 carrying 00, 03, 0B and 0C -- a
     subset, drawn from the same descriptor field.  (On a6a8274a both
     addresses took 40,704 writes and every one was 0x00, which is why
     section 5 could not test anything: the board was rebooting before it
     ever played a sample.)

     A bug this exposes, deliberately not fixed here.  bank_w computes
     data * 0x8000, and banks 0x0B and 0x0C now really occur, which points
     it at 0x58000 and 0x60000 inside a REGION_SOUND1 that is 0x50000 long
     -- an out-of-bounds base for cpu_setbank.  The decode is wrong for
     sport2k: the descriptor table at 0x5624+3n uses exactly ten values
     over its 63 sounds, 0x00-0x04 and 0x08-0x0C, with 5, 6 and 7 never
     appearing, which is five 27512s selected by bits 0-2 (CST0..CST7, only
     five fitted) and bit 3 choosing which 32K half of the selected chip --
     not a linear bank index.  Mephisto is the other case and is fine as
     is: eight 27256s, one 32K bank each, bank number = chip number.  A fix
     therefore has to be per-game and needs the polarity of bit 3 settled,
     which nothing here does. */
  { 0x10f00, 0x10f00, bank_w },
  { 0x11000, 0x11000, DAC_0_data_w },
  /* The YM3812 (OPL2) the manual puts on this board with its own 14.318 MHz
     crystal.  MACHINE_DRIVER_START(cirsa) has always added the chip, but
     nothing was ever mapped for the 8051 to reach it, so every register write
     the sound ROM made was silently discarded and the OPL2 never produced a
     note.  Address/data pair, A0 selecting between them.

     Identified from the ROM's own behaviour rather than guessed: a 40 s
     headless run logs unmapped writes to 0x11800 carrying exactly the OPL2
     register map, gaps included -- 0x01-0x08 (test/timers/CSM), 0x20-0x25,
     0x28-0x2D, 0x30-0x35 (the 18 operators), the matching 0x40/0x60/0x80
     blocks, 0xA0-0xA8 and 0xB0-0xB8 plus 0xBD (9 channels), 0xC0-0xC8, and
     0xE0-0xF5 (waveform select) -- each followed by a write to 0x11801.
     No other chip has that register layout. */
  { 0x11800, 0x11800, YM3812_control_port_0_w },
  { 0x11801, 0x11801, YM3812_write_port_0_w },
MEMORY_END

/* Mephisto shares the map above and has no OPL2, but its sound ROM never
   touches 0x11800/0x11801 -- a 35 s run logs no unmapped access by cpu 1 at
   all, before this change -- so the two handlers are unreachable there and a
   separate map would buy nothing.  Measured: with and without this mapping,
   all three games' audio hashes and every interrupt counter are identical. */

static PORT_READ_START(cirsa_readsndport)
  { 1, 1, cirsa_sndp1_r },
  { 3, 3, cirsa_sndp3_r },
  { 0, 3, port_r },
PORT_END

static PORT_WRITE_START(cirsa_writesndport)
  { 1, 1, cirsa_sndp1_w },
  { 2, 2, cirsa_sndp2_w },
  { 3, 3, cirsa_sndp3_w },
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

/*-- Input ports (CORE_COREINPORT, i.e. port 2) --------------------------
/  Bits 0x0100-0x0800 are the four cabinet buttons on the service bracket.
/
/  Bits 0x0001-0x0040 are coin, start and the ball trough.  Those are all
/  playfield matrix switches on the real machine, not cabinet wiring, so
/  before this they were reachable only through core.c's generic
/  column+row entry (Q..I plus A..K) -- workable but obscure, and it is
/  not what the rest of PinMAME does.  capcom.h, atari.h and gp.h all bind
/  coin and start on their own port and translate to the matrix in their
/  SWITCH_UPDATE; this follows that pattern, and uses the conventional
/  keys: 5 = coin, 1 = start.
/
/  The trough is a BITTOG because it is a level, not an event: the balls
/  sit on those switches and hold them closed for the whole game.  Held on
/  a momentary key you would have to keep a finger down to play at all.
/
/  Which matrix bit each one lands on differs per game and is set out in
/  cirsaCoinSw/mephCoinSw below.
/----------------------------------------------------------------------*/
/* A coin is an event, not a level, and both ROMs enforce that.  Their coin
   debouncers count consecutive closed samples and treat a long one as a jammed
   mech: measured on sport2k, a closure credits 3/3 at every hold from 0.05 s to
   0.50 s, 1/3 at 0.60 s and 0/3 at 0.80 s and beyond, with the knee at ~0.58 s
   where 0x068EE compares the run length against 0x0F.  A plain momentary key
   therefore fails for anyone who presses it deliberately rather than tapping,
   which is a trap for a human and was one for this project's own test harness.

   IPF_IMPULSE holds the bit for a fixed number of frames however long the key
   is actually held, so the ROM sees the same closure every time.  8 frames at
   the 60 FPS this machine runs at is 133 ms: comfortably above the ~50 ms the
   debouncer needs to see it at all, and comfortably below the ~580 ms that
   makes it a jam.  COREPORT_BITIMP would be 1 frame, 17 ms, which is under the
   debounce tick (~26 Hz) and can be missed entirely.

   Start is deliberately left momentary -- it has no jam timeout, and holding
   it is what the real button does. */
#define CIRSA_COIN(mask, type) \
  PORT_BITX(mask, IP_ACTIVE_HIGH, (type) | IPF_IMPULSE | (8<<8), \
            IP_NAME_DEFAULT, IP_KEY_DEFAULT, IP_JOY_DEFAULT)

INPUT_PORTS_START(cirsa)
  CORE_PORTS
  SIM_PORTS(1)
  PORT_START /* CORE_COREINPORT */
    /* Coins and start take MAME's own input types, so they inherit its default
       keys and joystick codes, appear under their standard names in the Tab
       menu, and are remappable the way a player expects: 5/6/7 for the three
       coin chutes and 1 for start.  Coins are additionally IPF_IMPULSE-limited
       to 8 frames -- see the block comment above for why a held coin is refused
       by the ROM. */
    CIRSA_COIN(       0x0001, IPT_COIN1)
    CIRSA_COIN(       0x0002, IPT_COIN2)
    CIRSA_COIN(       0x0004, IPT_COIN3)
    COREPORT_BITDEF(  0x0008, IPT_START1, IP_KEY_DEFAULT)
    /* The trough has no MAME equivalent -- it is a level held by the balls, not
       a control -- so it keeps an explicit key and a descriptive name. */
    COREPORT_BITTOG(  0x0010, "Ball Trough", KEYCODE_B)
    /* The four buttons on the service bracket.  IPT_SERVICE1..4 give MAME's
       service defaults (9, 0, minus, equals) and keep these clear of the coin
       keys, which they previously collided with: Test was on 7 (IPT_COIN3's
       default) and Advance on 8 (IPT_COIN4's).  The names stay descriptive
       because "Service 1" says nothing about what the button does. */
    PORT_BITX(0x0100, IP_ACTIVE_HIGH, IPT_SERVICE1, "Test",    IP_KEY_DEFAULT, IP_JOY_DEFAULT)
    PORT_BITX(0x0200, IP_ACTIVE_HIGH, IPT_SERVICE2, "Advance", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
    PORT_BITX(0x0400, IP_ACTIVE_HIGH, IPT_SERVICE3, "EG1",     IP_KEY_DEFAULT, IP_JOY_DEFAULT)
    PORT_BITX(0x0800, IP_ACTIVE_HIGH, IPT_SERVICE4, "EG2",     IP_KEY_DEFAULT, IP_JOY_DEFAULT)
INPUT_PORTS_END

/* Positions 0..6 and 7..13 are the two 7-character LA8041R-11B alphanumeric
   rows -- CORE_SEG16N, not CORE_SEG8D (docs/findings/
   2026-09-02-alphanumeric-segments.md: the high byte is a-g in the same
   bit positions as the numeric font, the low byte carries the rest of the
   alphabet, and no CORE_SEG16* variant reinterprets bits beyond that, so
   the "without commas" one that adds nothing unobserved is the right
   declaration). Positions 14..20 and 21..27 stay the real 7-digit LTS 3401
   numeric rows, CORE_SEG8D.

   CORE_SEG16N controls pixel geometry only, NOT bit interpretation, and
   the ROM's segment bit order was never PinMAME's own: the ROM's font bit
   order is a=3 b=7 c=5 d=4 e=1 f=2 g=6 dp=0 (docs/findings/
   2026-09-02-alphanumeric-segments.md sec 5), but core.c:187's
   core_ascii2seg16 -- PinMAME's own canonical 16-segment bit assignment --
   puts segment a at bit 0. As of the segment-remap round, this is no
   longer a live problem: cirsa_shift_frame() runs every byte pair through
   cirsa_seg16() (16-segment groups) or cirsa_seg8d() (7-segment groups,
   including these two numeric rows) before it ever reaches
   coreGlobals.segments[] -- see cirsa_seg16()/cirsa_seg8d()'s own block
   comment, just above cirsa_shift_frame, for the per-bit table and
   docs/findings/2026-09-03-low-byte-strokes.md sec 5 for its derivation.
   coreGlobals.segments[] now holds PinMAME-order strokes throughout.

   Column positions were checked, not just carried over: core.c's
   segData[] table gives CORE_SEG8D and CORE_SEG16N the identical {20,15}
   cell (cols=15 either way), and core_seg_video_update() advances `left`
   by the same segData[type].cols+1 per character regardless of type. A
   16-segment character is drawn in the same size cell as a 7-segment one
   in this renderer -- it isn't literally wider here -- so the existing
   {0,16} / {0,16} spacing (7 characters at 2 left-units each = 14, next
   group at 16, same 2-unit gap CORE_SEG8D had) still fits with no overlap
   and needs no adjustment. */
static core_tLCDLayout cirsa_disp[] = {
  {0, 0, 0, 7,CORE_SEG16N}, {0,16, 7, 7,CORE_SEG16N},
  {3, 0,14, 7,CORE_SEG8D}, {3,16,21, 7,CORE_SEG8D},
  {6, 8,28, 2,CORE_SEG8D}, {6,14,30, 1,CORE_SEG8D}, {6,18,31, 2,CORE_SEG8D},
  {0}
};
/* Mephisto's panel is NOT Sport 2000's -- 33 identical LTS 3401
   seven-segment digits (no alphanumeric units at all), grouped 7+7+7+7+5
   across the four player boards and the credit/match board (docs/findings/
   2026-09-01-mephisto-display-chain.md). Same row/column grid as
   cirsa_disp for visual consistency; every group stays CORE_SEG8D. */
static core_tLCDLayout mephisto_disp[] = {
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
   mephistoGameData now sets it to 1 as well.  Mephisto has the same eight
   quick contacts on the same wires: its switch scan reads IC9 Port B into
   [0xDA] at 0x103D, its SWITCH TEST element routine tests that byte for
   elements 60-67 (0x31F4: `sub bx,0x3c / mov al,1 / rol al,cl / test
   al,[0xda]`), and its level-2 ISR loads the same byte straight into the
   group-0 coil byte at 0x14D0.  cirsa_vblank used to gate the qcState
   update on hw.gameSpecific1, so those eight positions could not be
   stimulated at all; both games share the path now.  Nothing changes
   unless something writes swMatrix[12], which nothing does by default --
   ic9_pb_r returns 0 either way. */
/* hw.gameSpecific1 (7th field of the hw sub-struct: flippers, swCol, lampCol,
   custSol, soundBoard, display, gameSpecific1) is the Sport-2000-vs-Mephisto
   switch: 0 = Sport 2000 (default), 1 = Mephisto/mephist1.  Every remaining
   consumer is a selector between two implemented paths -- none of them is
   still a "write nothing, unestablished" gate:

     - cirsa_frameLen()'s frame length (8 bytes vs 6).
     - cirsa_shift_frame()'s per-game selector -- the column-mask table and
       which segment-group mapping to write.
     - ic9_pa_w's coil-bus POSITION order, pos vs 7-pos.  Both orders are
       measured; a1707f52 replaced the old gate with the per-game decode.
     - ic9_pa_w's quick-contact OR into coils 1-3, which is a real Sport
       2000-only hardware path.
     - cirsa_readsnd/cirsa_writesnd's sound-ROM banking.

   The last two gates went in the matrix round: cirsa_vblank's quick-contact
   gate (Mephisto has the same eight contacts on the same wires -- see just
   above mephistoGameData) and, before it, the display one. */
static core_tGameData cirsaGameData    = {0,cirsa_disp,{FLIP_SW(FLIP_L),1,8}};
static core_tGameData mephistoGameData = {0,mephisto_disp,{FLIP_SW(FLIP_L),1,8,0,0,0,1}};
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
CORE_GAMEDEFNV(mephisto,"Mephisto (rev. 1.2)",1986,"Stargame",mephisto,GAME_IMPERFECT_SOUND)

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
CORE_CLONEDEFNV(mephist1,mephisto,"Mephisto (rev. 1.1)",1986,"Stargame",mephisto,GAME_IMPERFECT_SOUND)

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
CORE_GAMEDEFNV(sport2k,"Sport 2000",1988,"Cirsa",cirsa,GAME_IMPERFECT_SOUND)
