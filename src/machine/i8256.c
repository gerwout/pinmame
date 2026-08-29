/**********************************************************************************************
 *
 *   Intel 8256 / 8256AH MUART -- see i8256.h for provenance.
 *
 **********************************************************************************************/

#include "driver.h"
#include "i8256.h"

/*-- register file indices (identical for read and write, different meanings) --*/
#define R_CMD1     0
#define R_CMD2     1
#define R_CMD3     2
#define R_MODE     3
#define R_PORT1C   4
#define R_SETINT   5   /* write: set interrupt enables;   read: interrupt enable mask */
#define R_RSTINT   6   /* write: clear interrupt enables; read: interrupt address (level*4) */
#define R_BUFFER   7   /* write: transmit buffer;         read: receive buffer */
#define R_PORT1    8
#define R_PORT2    9
#define R_TIMER1  10
#define R_TIMER5  14
#define R_STATUS  15   /* write: modification register;   read: status */

/*-- CMD1 --*/
#define CMD1_FRQ(c)   ((c) & 0x01)   /* 0 = 16 kHz timer base, 1 = 1 kHz */
#define CMD1_8086(c)  (((c) >> 1) & 1)
#define CMD1_BITI(c)  (((c) >> 2) & 1)   /* 1 = level 1 is P17 edge, 0 = timer 2 */

/*-- CMD3 (set/reset register) --*/
#define CMD3_SET   0x80
#define CMD3_RxE   0x40
#define CMD3_IAE   0x20
#define CMD3_NIE   0x10
#define CMD3_END   0x08
#define CMD3_RST   0x01

/*-- MODE --*/
#define MODE_T35(m)  (((m) >> 7) & 1)   /* cascade timers 3+5 */
#define MODE_T24(m)  (((m) >> 6) & 1)   /* cascade timers 2+4 */

/*-- STATUS --*/
#define ST_INT  0x80
#define ST_RBF  0x40   /* receive buffer full */
#define ST_TBE  0x20   /* transmit buffer empty */
#define ST_TRE  0x10   /* transmitter empty */

/* timer index (0..4 = timers 1..5) -> interrupt level */
static const UINT8 timer_level[5] = {
  I8256_INT_TIMER1, I8256_INT_TIMER2, I8256_INT_TIMER3,
  I8256_INT_TIMER4, I8256_INT_TIMER5
};

static struct {
  const I8256interface *intf;
  UINT8 reg[16];        /* shadow of the last value written -- reads of the
                           command registers must return it, several routines
                           do read-modify-write on CMD1 and the ports */
  UINT8 timer[5];       /* live down counters */
  UINT8 inten;          /* interrupt enable mask, one bit per level */
  UINT8 pending;        /* latched interrupt requests */
  UINT8 inservice;      /* levels acknowledged but not yet ended */
  UINT8 status;
  UINT8 p1_latch, p2_latch;
  UINT8 p1_pins;        /* last seen state of the Port 1 input pins */
  int   extint;
  int   curlevel;       /* level presented at the last acknowledge, -1 if none */
  int   intline;        /* current state of the INT pin */
  int   prescale;       /* 16 kHz -> 1 kHz divider when CMD1.FRQ is set */
  int   initialised;
} i8256;

/*-------------------------------------------------------------------------
/  Interrupt controller
/-------------------------------------------------------------------------*/

/* Highest priority (= lowest numbered) level that is requesting and enabled. */
static int i8256_top_request(void) {
  int i;
  UINT8 active = i8256.pending & i8256.inten;
  for (i = 0; i < 8; i++)
    if (active & (1 << i)) return i;
  return -1;
}

/* Highest priority level currently in service, or 8 if none. */
static int i8256_top_inservice(void) {
  int i;
  for (i = 0; i < 8; i++)
    if (i8256.inservice & (1 << i)) return i;
  return 8;
}

static void i8256_update_int(void) {
  int req = i8256_top_request();
  int want = 0;

  if (req >= 0) {
    if (!i8256.inservice)
      want = 1;                                    /* nothing in service */
    else if (i8256.reg[R_CMD3] & CMD3_NIE)
      want = (req < i8256_top_inservice());        /* nested: higher priority only */
    /* without NIE a level in service blocks everything until END */
  }
  if (want != i8256.intline) {
    i8256.intline = want;
    if (want) i8256.status |=  ST_INT;
    else      i8256.status &= ~ST_INT;
    if (i8256.intf && i8256.intf->int_out) i8256.intf->int_out(want);
  }
}

static void i8256_request(int level) {
  i8256.pending |= (1 << level);
  i8256_update_int();
}

/* Shared by the INTA cycle and by reading the interrupt address register --
   the datasheet says a read of that register has the same effect as INTA. */
static int i8256_acknowledge(void) {
  int level = i8256_top_request();
  if (level < 0) return -1;
  i8256.pending   &= ~(1 << level);
  i8256.inservice |=  (1 << level);
  i8256.curlevel   = level;
  i8256_update_int();
  return level;
}

int i8256_inta(void) {
  int level = i8256_acknowledge();
  if (level < 0) level = 7;      /* spurious: answer with the lowest priority */
  if (CMD1_8086(i8256.reg[R_CMD1]))
    return 0x40 + level;         /* 8086 mode: vector 40H..47H */
  return 0xc7 | (level << 3);    /* 8085 mode: RST n opcode */
}

/* CMD3.END -- end of interrupt, clears the highest level in service. */
static void i8256_end_of_interrupt(void) {
  int top = i8256_top_inservice();
  if (top < 8) i8256.inservice &= ~(1 << top);
  else         i8256.inservice = 0;
  i8256_update_int();
}

/*-------------------------------------------------------------------------
/  Counter/timers
/
/  All five run continuously from a common time base selected by CMD1.FRQ
/  (16 kHz or 1 kHz).  An interrupt is generated on the 1 -> 0 transition of a
/  single timer, or of the low half of a cascaded pair.  The counters wrap and
/  keep running; software reloads them by writing the register, and the Sport
/  2000 timer-1 handler relies on being able to read the live value back.
/-------------------------------------------------------------------------*/
static void i8256_tick(int dummy) {
  int i;

  if (!i8256.initialised) return;

  if (CMD1_FRQ(i8256.reg[R_CMD1])) {          /* 1 kHz base: divide by 16 */
    if (++i8256.prescale < 16) return;
    i8256.prescale = 0;
  }

  for (i = 0; i < 5; i++) {
    int t24 = MODE_T24(i8256.reg[R_MODE]);
    int t35 = MODE_T35(i8256.reg[R_MODE]);
    int level, high;

    /* The high half of a cascaded pair is stepped by its low half, not here. */
    if ((i == 3 && t24) || (i == 4 && t35)) continue;

    if (i8256.timer[i]--) continue;           /* no underflow this tick */

    /* Which level does this underflow report on?
       Cascaded 2+4 reports on level 6 and vacates level 1; cascaded 3+5
       reports on level 3 and vacates level 7 (datasheet priority table). */
    if (i == 1 && t24) {
      high = 3; level = I8256_INT_TIMER4;
    } else if (i == 2 && t35) {
      high = 4; level = I8256_INT_TIMER3;
    } else {
      high = -1; level = timer_level[i];
    }

    if (high >= 0) {
      if (i8256.timer[high]--) continue;      /* 16 bit pair not exhausted yet */
    } else if (i == 1 && CMD1_BITI(i8256.reg[R_CMD1])) {
      /* CMD1.BITI gives level 1 to the Port 1 P17 edge input instead, so an
         uncascaded timer 2 generates no interrupt at all in this mode.  Sport
         2000 and Mephisto both run with BITI set, and level 1 is their
         power-fail handler -- letting timer 2 drive it resets the machine. */
      continue;
    }
    i8256_request(level);
  }
}

/*-------------------------------------------------------------------------
/  Ports
/-------------------------------------------------------------------------*/
static UINT8 i8256_port1_read(void) {
  UINT8 dir = i8256.reg[R_PORT1C];              /* 1 = output */
  UINT8 pins = i8256.intf && i8256.intf->p1_in ? i8256.intf->p1_in() : 0xff;
  return (UINT8)((i8256.p1_latch & dir) | (pins & ~dir));
}

static void i8256_port1_write(UINT8 data) {
  i8256.p1_latch = data;
  if (i8256.intf && i8256.intf->p1_out)
    i8256.intf->p1_out((UINT8)(data & i8256.reg[R_PORT1C]));
}

void i8256_set_p1_pin(int bit, int state) {
  UINT8 mask = (UINT8)(1 << bit);
  UINT8 old  = i8256.p1_pins;
  if (state) i8256.p1_pins |= mask; else i8256.p1_pins &= ~mask;
  /* P17 is the edge triggered interrupt input; a low-to-high transition
     raises level 1 when CMD1.BITI selects it instead of timer 2 */
  if (bit == 7 && CMD1_BITI(i8256.reg[R_CMD1]) && !(old & 0x80) && (i8256.p1_pins & 0x80))
    i8256_request(I8256_INT_TIMER2);
}

void i8256_set_extint(int state) {
  int old = i8256.extint;
  i8256.extint = state ? 1 : 0;
  if (!old && i8256.extint) i8256_request(I8256_INT_EXTINT);
}

/*-------------------------------------------------------------------------
/  Serial (minimal -- the sound link is wired up in a later phase)
/-------------------------------------------------------------------------*/
void i8256_receive(UINT8 data) {
  i8256.reg[R_BUFFER] = data;
  i8256.status |= ST_RBF;
  if (i8256.reg[R_CMD3] & CMD3_RxE) i8256_request(I8256_INT_RX);
}

/*-------------------------------------------------------------------------
/  Software reset (CMD3.RST)
/
/  Much narrower than the hardware reset on pin 12.  It clears the interrupt
/  mask, request and service registers and puts the status register back to
/  "transmitter idle", but leaves the command registers, the mode and port
/  control registers, the parallel port latches and the counter/timers alone.
/  Both ROMs rely on that: Sport 2000 issues RST at 0x059B and then keeps
/  addressing the chip in 8086 mode, which only works if CMD1 survives.
/-------------------------------------------------------------------------*/
static void i8256_soft_reset(void) {
  i8256.inten     = 0;
  i8256.pending   = 0;
  i8256.inservice = 0;
  i8256.curlevel  = -1;
  i8256.status    = ST_TBE | ST_TRE;
  i8256_update_int();
}

/*-------------------------------------------------------------------------
/  Bus interface
/-------------------------------------------------------------------------*/
static int i8256_decode(offs_t offset, int *reg) {
  if (CMD1_8086(i8256.reg[R_CMD1])) {
    if (offset & 1) return 0;            /* A0 is a second chip select */
    *reg = (offset >> 1) & 0x0f;
  } else {
    *reg = offset & 0x0f;
  }
  return 1;
}

WRITE_HANDLER(i8256_w) {
  int reg;
  if (!i8256_decode(offset, &reg)) return;

  switch (reg) {
    case R_CMD3:
      if (data & CMD3_SET) {
        i8256.reg[R_CMD3] |= (data & (CMD3_RxE | CMD3_IAE | CMD3_NIE));
        if (data & CMD3_RST) i8256_soft_reset();
        if (data & CMD3_END) i8256_end_of_interrupt();
      } else {
        i8256.reg[R_CMD3] &= ~(data & (CMD3_RxE | CMD3_IAE | CMD3_NIE));
      }
      return;

    case R_SETINT:                       /* set interrupt enables */
      i8256.inten |= data;
      i8256_update_int();
      return;

    case R_RSTINT:                       /* clear interrupt enables */
      i8256.inten &= ~data;
      i8256.pending &= ~data;
      i8256_update_int();
      return;

    case R_BUFFER:
      i8256.reg[R_BUFFER] = data;
      if (i8256.intf && i8256.intf->txd_out) i8256.intf->txd_out(data);
      /* no real bit timing yet: report the byte gone straight away */
      i8256.status |= (ST_TBE | ST_TRE);
      i8256_request(I8256_INT_TX);
      return;

    case R_PORT1:
      i8256.reg[R_PORT1] = data;
      i8256_port1_write(data);
      return;

    case R_PORT2:
      i8256.reg[R_PORT2] = data;
      i8256.p2_latch = data;
      if (i8256.intf && i8256.intf->p2_out) i8256.intf->p2_out(data);
      return;

    case R_TIMER1: case R_TIMER1+1: case R_TIMER1+2: case R_TIMER1+3: case R_TIMER5:
      i8256.reg[reg] = data;
      i8256.timer[reg - R_TIMER1] = data;
      return;

    case R_PORT1C:
      i8256.reg[R_PORT1C] = data;
      i8256_port1_write(i8256.p1_latch);   /* direction change re-drives the pins */
      return;

    default:
      i8256.reg[reg] = data;
      return;
  }
}

READ_HANDLER(i8256_r) {
  int reg;
  if (!i8256_decode(offset, &reg)) return 0xff;

  switch (reg) {
    case R_SETINT:
      return i8256.inten;

    case R_RSTINT: {                     /* interrupt address: level * 4 */
      int level = i8256_acknowledge();   /* reading it acknowledges, like INTA */
      if (level < 0) level = i8256.curlevel < 0 ? 0 : i8256.curlevel;
      return (UINT8)(level * 4);
    }

    case R_BUFFER:
      i8256.status &= ~ST_RBF;
      return i8256.reg[R_BUFFER];

    case R_PORT1:
      return i8256_port1_read();

    case R_PORT2:
      if (i8256.intf && i8256.intf->p2_in) return i8256.intf->p2_in();
      return i8256.p2_latch;

    case R_TIMER1: case R_TIMER1+1: case R_TIMER1+2: case R_TIMER1+3: case R_TIMER5:
      return i8256.timer[reg - R_TIMER1];

    case R_STATUS:
      return i8256.status;

    case R_CMD3:
      /* bits 0, 3 and 7 always read back as zero */
      return (UINT8)(i8256.reg[R_CMD3] & 0x76);

    default:
      return i8256.reg[reg];
  }
}

int i8256_is_8086_mode(void) { return CMD1_8086(i8256.reg[R_CMD1]); }

/*-------------------------------------------------------------------------
/  Init / reset
/-------------------------------------------------------------------------*/
void i8256_reset(void) {
  const I8256interface *intf = i8256.intf;
  int init = i8256.initialised;
  memset(&i8256, 0, sizeof(i8256));
  i8256.intf = intf;
  i8256.initialised = init;
  i8256.curlevel = -1;
  i8256.status = ST_TBE | ST_TRE;      /* transmitter idle */
  i8256.p1_pins = 0x00;   /* P17 (power fail) rests low -- see the driver */
  if (intf && intf->int_out) intf->int_out(0);
}

void i8256_init(const I8256interface *intf) {
  i8256_reset();
  i8256.intf = intf;
  i8256.initialised = 1;
  /* Common 16 kHz time base; CMD1.FRQ divides it down to 1 kHz in software. */
  timer_pulse(TIME_IN_HZ(16000), 0, i8256_tick);
}
