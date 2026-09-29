#include "board.h"
#include <stdio.h>
#include <string.h>

#define HZ 80000000u
#define TICK 10001u /* lamp timer period: PR2 = 10000 at 80 MHz */
enum { PA, PB, PC, PD, PE, PF, PG };

static pinheck_board b;
static uint64_t now;
static uint32_t lat[BOARD_PORTS], tris[BOARD_PORTS];
static uint8_t closed[8];
static uint16_t cab;
static int fails;
static struct {
	int lamps, sols, gi, start, rgb, servo;
	uint8_t cols, rows;
	uint32_t sol;
	uint16_t gi_v;
	int start_v;
	uint64_t t;
	int rgb_chain[8], rgb_led[8];
	uint8_t rgb_v[8][3];
	uint64_t rgb_t;
	int servo_n;
	uint32_t pulse;
} rec;

#define CHECK(c) do { if (!(c)) { printf("BOARD FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t io_sw_col(void *c, int col) { (void)c; return closed[col]; }
static uint16_t io_cabinet(void *c) { (void)c; return cab; }
static void io_lamps(void *c, uint64_t t, uint8_t cols, uint8_t rows) { (void)c; rec.lamps++; rec.cols = cols; rec.rows = rows; rec.t = t; }
static void io_sols(void *c, uint64_t t, uint32_t s) { (void)c; rec.sols++; rec.sol = s; rec.t = t; }
static void io_gi(void *c, uint64_t t, uint16_t g) { (void)c; rec.gi++; rec.gi_v = g; rec.t = t; }
static void io_start(void *c, uint64_t t, int on) { (void)c; rec.start++; rec.start_v = on; rec.t = t; }
static void io_rgb(void *c, uint64_t t, int chain, int led, uint8_t r, uint8_t g, uint8_t bl)
{
	(void)c;
	if (rec.rgb < 8) {
		rec.rgb_chain[rec.rgb] = chain; rec.rgb_led[rec.rgb] = led;
		rec.rgb_v[rec.rgb][0] = r; rec.rgb_v[rec.rgb][1] = g; rec.rgb_v[rec.rgb][2] = bl;
	}
	rec.rgb++;
	rec.rgb_t = t;
}
static void io_servo(void *c, uint64_t t, int s, uint32_t pulse) { (void)c; rec.servo++; rec.servo_n = s; rec.pulse = pulse; rec.t = t; }

static void reset(void)
{
	pinheck_board_io io = { NULL, io_sw_col, io_cabinet, io_lamps, io_sols, io_gi, io_start, io_rgb, io_servo };
	pinheck_board_init(&b, &io, HZ);
	memset(&rec, 0, sizeof(rec));
	memset(lat, 0, sizeof(lat));
	memset(tris, 0, sizeof(tris));
	memset(closed, 0, sizeof(closed));
	tris[PD] = 0x00FFu;
	tris[PF] = 0x0001u;
	cab = 0;
	now = 1000;
}

static void write_port(int port, uint32_t v)
{
	lat[port] = v;
	pinheck_board_port(&b, port, lat[port], tris[port], now);
}

static void pin(int port, int bit, int v)
{
	write_port(port, v ? lat[port] | (1u << bit) : lat[port] & ~(1u << bit));
}

static void step(int port, int bit, int v, uint64_t dt)
{
	pin(port, bit, v);
	now += dt;
}

/* The firmware's T2 lamp interrupt, one call per timer period (DOM_V006 0x9D026470). */
static struct { int tick, col, phase; uint8_t level[64]; } fw;

static void fw_lamp_isr(void)
{
	int r;
	if (++fw.tick == 2) write_port(PB, 0);
	else if (fw.tick == 3) {
		uint32_t rows = 0;
		fw.tick = 0;
		for (r = 0; r < 8; r++)
			if (fw.phase < fw.level[fw.col * 8 + r]) rows |= 1u << r;
		write_port(PB, rows << 8 | 1u << fw.col);
		if (++fw.col == 8) { fw.col = 0; fw.phase = (fw.phase + 1) & 7; }
	}
	now += TICK;
}

/* Lamp 21 (column 2, row 5) at each brightness 0-7: lit for exactly level x 2 periods per 8 frames. */
static void lamps_levels(void)
{
	int level, i;
	for (level = 0; level < 8; level++) {
		uint64_t on = 0, since = 0;
		int lit = 0, other = 0;
		reset();
		memset(&fw, 0, sizeof(fw));
		fw.level[21] = (uint8_t)level;
		fw.level[20] = 7;
		for (i = 0; i < 8 * 24; i++) {
			int was = rec.lamps;
			fw_lamp_isr();
			if (rec.lamps == was) continue;
			if (lit) { on += rec.t - since; lit = 0; }
			if ((rec.cols & 4u) && (rec.rows & 0x20u)) { lit = 1; since = rec.t; }
			if ((rec.cols & ~4u) && rec.rows) other = 1;
		}
		CHECK(on == (uint64_t)level * 2 * TICK);
		CHECK(!other);
	}
	reset();
	tris[PB] = 0xFFFFu;
	write_port(PB, 0xFFFFu);
	CHECK(rec.lamps == 0);
}

static const char *sol_pins[BOARD_SOLS] = {
	"C2", "C3", "E5", "F3", "E6", "E7", "E8", "C14", "E9", "C4", "A2", "A0",
	"A1", "A4", "A5", "C13", "G1", "G0", "A6", "A7", "G14", "G12", "G13", "A9"
};

static void kick(void)
{
	step(PG, 15, 1, 10);
	step(PG, 15, 0, 10);
}

/* Coils need the HC123 watchdog: a falling PIC_ENABLE (RG15) arms it for 0.45 x 25k x 100u = 1.125 s. */
static void sols_watchdog(void)
{
	int i;
	uint64_t t0;
	reset();
	step(PG, 1, 1, 10);
	CHECK(rec.sols == 0);
	t0 = now;
	kick();
	CHECK(rec.sols == 1 && rec.sol == 1u << 16 && rec.t == t0 + 10);
	pinheck_board_tick(&b, t0 + 10 + HZ / 8 * 9 - 1);
	CHECK(rec.sols == 1);
	pinheck_board_tick(&b, t0 + 10 + HZ / 8 * 9);
	CHECK(rec.sols == 2 && rec.sol == 0 && rec.t == t0 + 10 + HZ / 8 * 9);
	now = t0 + 10 + HZ;
	kick();
	CHECK(rec.sols == 3 && rec.sol == 1u << 16);
	step(PG, 1, 0, 10);
	CHECK(rec.sols == 4 && rec.sol == 0);
	for (i = 0; i < BOARD_SOLS; i++) {
		int port = sol_pins[i][0] - 'A', bit = sol_pins[i][1] - '0';
		if (sol_pins[i][2]) bit = bit * 10 + sol_pins[i][2] - '0';
		step(port, bit, 1, 10);
		CHECK(rec.sol == 1u << i);
		step(port, bit, 0, 10);
		CHECK(rec.sol == 0);
	}
	reset();
	tris[PG] = 1u << 15;
	kick();
	step(PG, 1, 1, 10);
	CHECK(rec.sols == 0);
}

/* The firmware's cabinet scan (DOM_V006 0x9D01A2A8): per bit, GI data out, CAB_CLK up and down,
   then read CAB_SWITCH_IN (low = closed); after 16 bits CAB_LAT low then high. */
static uint16_t fw_cab_bits(uint16_t gi)
{
	uint16_t in = 0;
	int i;
	for (i = 0; i < 16; i++) {
		in = (uint16_t)(in << 1);
		step(PG, 7, gi & 1u, 10);
		gi >>= 1;
		step(PE, 0, 1, 10);
		step(PE, 0, 0, 10);
		if (!(pinheck_board_read(&b, PF, now) & 1u)) in |= 1u;
	}
	return in;
}

static uint16_t fw_cab_scan(uint16_t gi)
{
	uint16_t in = fw_cab_bits(gi);
	step(PG, 8, 0, 10);
	step(PG, 8, 1, 10);
	return in;
}

/* Cabinet input i (U12 D0-7, U11 D0-7) is firmware cabinet switch i+1; U11 D7 is never read
   and switch 0 reads U12's unconnected serial input (open). */
static void cabinet_chain(void)
{
	int i, k;
	for (i = 0; i < 16; i++) {
		uint16_t in;
		reset();
		cab = (uint16_t)(1u << i);
		fw_cab_scan(0);
		in = fw_cab_scan(0);
		for (k = 0; k < 16; k++) CHECK(((in >> k) & 1u) == (unsigned)(i < 15 && k == i + 1));
	}
	reset();
	cab = 0x0001u;
	CHECK((pinheck_board_read(&b, PF, now) & 1u) == 1u);
	cab = 0x8000u;
	CHECK((pinheck_board_read(&b, PF, now) & 1u) == 0u);
}

/* GI word bit n (shifted first) ends in U5 Q7: GI_(15-n), latched when CAB_LAT falls. */
static void gi_chain(void)
{
	int n;
	for (n = 0; n < 16; n++) {
		reset();
		step(PG, 8, 1, 10);
		fw_cab_bits((uint16_t)(1u << n));
		CHECK(rec.gi == 0);
		step(PG, 8, 0, 10);
		CHECK(rec.gi == 1 && rec.gi_v == (uint16_t)(0x8000u >> n));
		step(PG, 8, 1, 10);
		CHECK(rec.gi == 1);
	}
	reset();
	step(PG, 8, 1, 10);
	fw_cab_scan(0x00F1u);
	CHECK(rec.gi == 1 && rec.gi_v == 0x8F00u);
	fw_cab_scan(0x00F1u);
	CHECK(rec.gi == 1);
	step(PG, 8, 0, 10);
	CHECK(rec.gi == 1);
}

static void ws_bytes(int dport, int dbit, int cport, int cbit, const uint8_t *v, int n)
{
	int i, k;
	for (i = 0; i < n; i++)
		for (k = 7; k >= 0; k--) {
			step(cport, cbit, 0, 5);
			step(dport, dbit, (v[i] >> k) & 1, 5);
			step(cport, cbit, 1, 5);
		}
}

/* WS2801: MSB first on the rising clock, 24 bits per LED in chain order, latched after 500 us low. */
static void ws2801(void)
{
	static const uint8_t two[6] = { 0x12, 0x34, 0x56, 0xFF, 0x00, 0x80 }, one[3] = { 0x01, 0x80, 0x7E };
	uint64_t last;
	reset();
	ws_bytes(PE, 1, PE, 2, two, 6);
	step(PE, 2, 0, 5);
	last = now - 10;
	pinheck_board_tick(&b, last + HZ / 2000);
	CHECK(rec.rgb == 0);
	pinheck_board_tick(&b, last + HZ / 2000 + 1);
	CHECK(rec.rgb == 2 && rec.rgb_t == last + HZ / 2000 && b.ws_leds[BOARD_RGB_ONBOARD] == 2);
	CHECK(rec.rgb_chain[0] == BOARD_RGB_ONBOARD && rec.rgb_led[0] == 0 && rec.rgb_v[0][0] == 0x12 && rec.rgb_v[0][1] == 0x34 && rec.rgb_v[0][2] == 0x56);
	CHECK(rec.rgb_led[1] == 1 && rec.rgb_v[1][0] == 0xFF && rec.rgb_v[1][1] == 0x00 && rec.rgb_v[1][2] == 0x80);
	reset();
	ws_bytes(PG, 6, PC, 1, one, 3);
	now += HZ / 1000;
	ws_bytes(PG, 6, PC, 1, two, 3);
	CHECK(rec.rgb == 1 && rec.rgb_chain[0] == BOARD_RGB_EXTERNAL && rec.rgb_led[0] == 0);
	CHECK(rec.rgb_v[0][0] == 0x01 && rec.rgb_v[0][1] == 0x80 && rec.rgb_v[0][2] == 0x7E);
	pinheck_board_tick(&b, now + HZ);
	CHECK(rec.rgb == 2 && rec.rgb_v[1][0] == 0x12 && b.ws_leds[BOARD_RGB_EXTERNAL] == 1);
}

/* Servo pulse = rising to falling edge on SERVO_0-4 (RF1, RA10, RF4, RE3, RE4). */
static void servos(void)
{
	static const int port[BOARD_SERVOS] = { PF, PA, PF, PE, PE }, bit[BOARD_SERVOS] = { 1, 10, 4, 3, 4 };
	int i;
	reset();
	step(PF, 1, 0, 100);
	CHECK(rec.servo == 0);
	for (i = 0; i < BOARD_SERVOS; i++) {
		step(port[i], bit[i], 1, 93200 + (uint64_t)i);
		step(port[i], bit[i], 0, 100);
		CHECK(rec.servo == i + 1 && rec.servo_n == i && rec.pulse == 93200u + (uint32_t)i);
	}
	reset();
	step(PF, 1, 1, 43500);
	step(PF, 1, 0, 100);
	pinheck_board_tick(&b, 1000 + HZ * 3 / 50 - 1);
	CHECK(rec.servo == 1);
	pinheck_board_tick(&b, 1000 + HZ * 3 / 50);
	CHECK(rec.servo == 2 && rec.servo_n == 0 && rec.pulse == 0 && rec.t == 1000 + HZ * 3 / 50);
	pinheck_board_tick(&b, 1000 + HZ);
	CHECK(rec.servo == 2);
}

/* The firmware's T3 switch scan (DOM_V006 0x9D0199C0): LATD = 0xFEFF << column, read PORTD,
   rows low = closed; LATD = 0 afterwards. */
static void switch_scan(void)
{
	int c;
	uint8_t got[8], all = 0;
	reset();
	for (c = 0; c < 8; c++) { closed[c] = (uint8_t)(1u << c | 0x80u >> c); all |= closed[c]; }
	write_port(PD, 0xFFFFu);
	CHECK((pinheck_board_read(&b, PD, now) & 0xFFu) == 0xFFu);
	for (c = 0; c < 8; c++) {
		write_port(PD, (0xFEFFu << c) & 0xFFFFu);
		now += 40;
		got[c] = (uint8_t)~pinheck_board_read(&b, PD, now);
		write_port(PD, 0);
		now += 40000;
	}
	for (c = 0; c < 8; c++) CHECK(got[c] == closed[c]);
	CHECK((uint8_t)~pinheck_board_read(&b, PD, now) == all);
	tris[PD] |= 0x0100u;
	write_port(PD, 0xFEFFu);
	CHECK((pinheck_board_read(&b, PD, now) & 0xFFu) == 0xFFu);
}

static void start_lamp(void)
{
	reset();
	step(PA, 3, 1, 10);
	CHECK(rec.start == 1 && rec.start_v == 1);
	step(PA, 2, 1, 10);
	CHECK(rec.start == 1);
	step(PA, 3, 0, 10);
	CHECK(rec.start == 2 && rec.start_v == 0);
}

int main(void)
{
	lamps_levels();
	sols_watchdog();
	cabinet_chain();
	gi_chain();
	ws2801();
	servos();
	switch_scan();
	start_lamp();
	printf("board: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
