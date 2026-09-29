#include "driver.h"
#include <ctype.h>
#include "core.h"
#include "cpu/pic32mx/pic32mxcpu.h"
#include "pinheck/prop.h"
#include "pinheck/rtc.h"
#include "pinheck/bootldr.h"
#include "pinheck/sd.h"
#include "pinheck/zipsrc.h"
#include "pinheck/audio.h"
#include "pinheck/display.h"
#include "pinheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PINHECK_CLOCK 80000000
#define PINHECK_LOG_MAX 64
#define PINHECK_ZIP_CACHE (64u << 20)
#define RF5  (1u << 5)
#define RF12 (1u << 12)
#define RF13 (1u << 13)

static pinheck_prop prop;
static cat24m01 u13;
static ds1340 rtc;
static pic32_boot boot;
static zipsrc zip;
static vfat vol;
static sd_card card;
static uint8_t u13mem[131072], propmem[131072];
static int reset_done, opened;

static struct {
	FILE *uart1, *proplog;
	const char *send;
	uint64_t send_at, rtc_at;
	double reset_at;
	int have_zip, have_vol, idle;
	uint32_t logged[PINHECK_LOG_MAX];
	int nlogged;
} locals;

static void pinheck_uart_tx(void *ctx, int uart, uint8_t byte, uint64_t cycle)
{
	(void)ctx; (void)cycle;
	if (uart == 1 && locals.uart1) fputc(byte, locals.uart1);
}

static void pinheck_port_write(void *ctx, int port, uint32_t lat, uint32_t tris, uint64_t cycle)
{
	uint32_t drv = lat & ~tris;
	(void)ctx;
	if (port == PIC32MX_PORTF)
		prop_pic_pins(&prop, cycle, (drv & RF12 ? 1u << 25 : 0) | (drv & RF5 ? 1u << 26 : 0));
}

static uint32_t pinheck_port_read(void *ctx, int port, uint64_t cycle)
{
	(void)ctx;
	if (port != PIC32MX_PORTF) return 0xFFFFu;
	return (0xFFFFu & ~RF13) | (prop_p24(&prop, cycle) ? RF13 : 0);
}

static int pinheck_i2c_pins(void *ctx, int module, int scl, int sda, uint64_t cycle)
{
	(void)ctx;
	if (module != 1) return sda;
	ds1340_tick(&rtc, cycle - locals.rtc_at);
	locals.rtc_at = cycle;
	return cat24m01_update(&u13, scl, sda) & ds1340_update(&rtc, scl, sda);
}

static void pinheck_unmapped(void *ctx, uint32_t pa, int write)
{
	(void)ctx;
	logerror("pinheck: unmodelled SFR %s %08x\n", write ? "write" : "read", (unsigned)(pa | 0xA0000000u));
}

static void pinheck_exception(void *ctx, int code, uint32_t pc)
{
	uint32_t key = pc ^ ((uint32_t)code << 27);
	int i;
	(void)ctx;
	for (i = 0; i < locals.nlogged; i++)
		if (locals.logged[i] == key) return;
	if (locals.nlogged < PINHECK_LOG_MAX) locals.logged[locals.nlogged++] = key;
	logerror("pinheck: PIC32 exception %d at %08x\n", code, (unsigned)pc);
}

static void pinheck_prop_log(void *ctx, const char *msg)
{
	(void)ctx;
	logerror("pinheck: %s\n", msg);
	if (locals.proplog) fprintf(locals.proplog, "%s\n", msg);
}

static int pinheck_blk_read(void *ctx, uint32_t lba, uint8_t *buf) { (void)ctx; return vfat_read(&vol, lba, buf); }
static int pinheck_spi(void *ctx, int cs, int sclk, int mosi) { (void)ctx; return sd_update(&card, cs, sclk, mosi); }
static void pinheck_boot_tx(void *ctx, uint64_t t, int level) { (void)ctx; prop_pic_pins(&prop, t, level ? 1u << 24 : 0); }
static void pinheck_prop_tx(void *ctx, uint64_t t, int level) { (void)ctx; boot_rx(&boot, t, level); }

static uint64_t pinheck_hold(void *ctx, uint64_t cycle)
{
	(void)ctx;
	prop_catch_up(&prop, cycle);
	boot_advance(&boot, cycle);
	return boot_hold(&boot, cycle);
}

static void pinheck_warn(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	logerror("%s\n", msg);
}

static int pinheck_starts(const char *n, const char *p)
{
	while (*p) if (toupper((unsigned char)*n++) != *p++) return 0;
	return 1;
}

static void pinheck_check_card(const vfat_source *s)
{
	char msg[160];
	int i, dmd = 0, sfx = 0;
	for (i = 0; i < s->count; i++) {
		const char *n = s->name(s->ctx, i);
		if (pinheck_starts(n, "DMD/")) dmd = 1;
		if (pinheck_starts(n, "SFX/")) sfx = 1;
	}
	if (dmd && sfx) return;
	sprintf(msg, "pinheck: the SD card from %.16s.zip has no %s%s%s; display and sound stay blank", Machine->gamedrv->name,
	        dmd ? "" : "DMD/", !dmd && !sfx ? " and no " : "", sfx ? "" : "SFX/");
	pinheck_warn(msg);
}

static void pinheck_open_card(void)
{
	char path[1024];
	int i, n = osd_get_path_count(FILETYPE_ROM);
	sd_blockdev dev;

	for (i = 0; i < n && !locals.have_zip; i++) {
		sprintf(path, "%.1000s/%.16s.zip", osd_get_path(FILETYPE_ROM, i), Machine->gamedrv->name);
		if (zipsrc_open(&zip, path, PINHECK_ZIP_CACHE) == 0) locals.have_zip = 1;
	}
	if (!locals.have_zip) {
		sprintf(path, "pinheck: no %.16s.zip on the ROM path, the SD card is empty", Machine->gamedrv->name);
		pinheck_warn(path);
		return;
	}
	pinheck_check_card(zipsrc_source(&zip));
	if (vfat_init(&vol, zipsrc_source(&zip)) != 0) { pinheck_warn("pinheck: cannot build the SD volume"); return; }
	locals.have_vol = 1;
	dev.ctx = NULL;
	dev.sectors = vfat_sectors(&vol);
	dev.read = pinheck_blk_read;
	sd_init(&card, &dev);
	prop_attach_sd(&prop, pinheck_spi, NULL);
}

static void pinheck_in_service(uint8_t *mem, uint32_t version)
{
	uint32_t w0 = version << 24 | 0xBAFAu, w1 = 0xABBA0002u;
	int i;
	for (i = 0; i < 4; i++) {
		mem[0x8000 + i] = (uint8_t)(w0 >> (8 * i));
		mem[0x8004 + i] = (uint8_t)(w1 >> (8 * i));
	}
}

static int64_t pinheck_local_now(void)
{
	time_t t = time(NULL);
	struct tm u = *gmtime(&t);
	u.tm_isdst = -1;
	return (int64_t)t + (int64_t)difftime(t, mktime(&u));
}

static void pinheck_tick(int param)
{
	pic32mx *soc = pic32cpu_soc();
	(void)param;
	if (locals.idle) return;
	prop_catch_up(&prop, soc->cpu.cycles);
	if (locals.reset_at > 0.0 && timer_get_time() >= locals.reset_at) {
		locals.reset_at = 0.0;
		reset_done = 1;
		machine_reset();
		return;
	}
	if (locals.send && *locals.send == '~' && soc->cpu.cycles >= locals.send_at) {
		locals.send_at = soc->cpu.cycles + PINHECK_CLOCK;
		locals.send++;
	}
	if (locals.send && *locals.send && soc->cpu.cycles >= locals.send_at) {
		int n = soc->uart[0].rx_count;
		pic32mx_uart_rx(soc, 0, (uint8_t)*locals.send);
		if (soc->uart[0].rx_count > n) locals.send++;
	}
}

static display disp;
static uint8_t disp_shown[DISPLAY_FRAME], disp_cfg[DISPLAY_CFG_MAX];
static int disp_cfg_n, disp_opened;
static FILE *disp_log;
static UINT32 disp_rgb32[256];
static UINT16 disp_rgb15[256];

static void pinheck_disp_frame(void *ctx, const uint8_t *frame, uint64_t t)
{
	uint8_t stamp[20];
	uint64_t pic = pic32cpu_soc()->cpu.cycles;
	uint32_t at = 0xFFFFFFFFu, a;
	int k;
	(void)ctx;
	memcpy(disp_shown, frame, DISPLAY_FRAME);
	if (!disp_log) return;
	for (a = 0; a + DISPLAY_FRAME <= 0x8000; a++)
		if (prop.chip.hub[a] == frame[0] && !memcmp(prop.chip.hub + a, frame, DISPLAY_FRAME)) { at = a; break; }
	for (k = 0; k < 8; k++) stamp[k] = (uint8_t)(t >> (8 * k));
	for (k = 0; k < 8; k++) stamp[8 + k] = (uint8_t)(pic >> (8 * k));
	for (k = 0; k < 4; k++) stamp[16 + k] = (uint8_t)(at >> (8 * k));
	fwrite(stamp, 1, 20, disp_log);
	fwrite(frame, 1, DISPLAY_FRAME, disp_log);
}

static void pinheck_disp_config(void *ctx, const uint8_t *bytes, int n, uint64_t t)
{
	char msg[16 + 3 * DISPLAY_CFG_MAX];
	int k, len;
	(void)ctx; (void)t;
	if (n == disp_cfg_n && !memcmp(bytes, disp_cfg, (size_t)n)) return;
	memcpy(disp_cfg, bytes, (size_t)n);
	disp_cfg_n = n;
	len = sprintf(msg, "display: config");
	for (k = 0; k < n; k++) len += sprintf(msg + len, " %02x", bytes[k]);
	pinheck_prop_log(NULL, msg);
}

static void pinheck_disp_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir)
{
	(void)ctx;
	display_pins(&disp, t, out, dir);
}

static void pinheck_disp_init(void)
{
	const char *path = getenv("PINHECK_FRAME_LOG");
	int v;
	for (v = 0; v < 256; v++) {
		int r = ((v >> 5) & 7) * 255 / 7, g = ((v >> 2) & 7) * 255 / 7, b = (v & 3) * 255 / 3;
		disp_rgb32[v] = MAKE_RGB(r, g, b);
		disp_rgb15[v] = (UINT16)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
	}
	if (disp_log) fclose(disp_log);
	disp_log = path ? fopen(path, disp_opened ? "ab" : "wb") : NULL;
	disp_opened = 1;
	prop_set_pins(&prop, pinheck_disp_pins, NULL);
}

static void pinheck_disp_reset(void)
{
	display_init(&disp, NULL, pinheck_disp_frame, pinheck_disp_config, pinheck_prop_log);
	memset(disp_shown, 0, sizeof(disp_shown));
	disp_cfg_n = 0;
}

static void pinheck_disp_stop(void)
{
	if (disp_log) fclose(disp_log);
	disp_log = NULL;
}

PINMAME_VIDEO_UPDATE(pinheck_video)
{
	int x, y;
	(void)layout; (void)cliprect;
	for (y = 0; y < DISPLAY_H && y < bitmap->height; y++)
		for (x = 0; x < DISPLAY_W && x < bitmap->width; x++) {
			const uint8_t v = disp_shown[y * DISPLAY_W + x];
			if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y])[x] = disp_rgb32[v];
			else ((UINT16 *)bitmap->line[y])[x] = disp_rgb15[v];
		}
}

static INTERRUPT_GEN(pinheck_vblank)
{
	core_updateSw(0);
}

static int pinheck_system_only(void)
{
	return !memory_region(PINHECK_CPUREGION) || !memory_region(PINHECK_PROPREGION);
}

/* sound: Propeller DUTY counters on P15/P14 integrated by audio.c */
static audio snd;
static struct {
	int started, rate;
	uint64_t samples;
	FILE *wav, *lag;
	uint32_t wav_bytes;
} sndl;

static void pinheck_snd_ctr(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq) { (void)ctx; audio_ctr(&snd, t, cog, ctr, ctr_reg, frq); }
static void pinheck_snd_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir) { (void)ctx; audio_pins(&snd, t, out, dir); }
static void pinheck_snd_log(void *ctx, const char *msg) { pinheck_prop_log(ctx, msg); }

static void pinheck_wav_le(FILE *f, uint32_t v, int n)
{
	while (n--) { fputc((int)(v & 0xFF), f); v >>= 8; }
}

static void pinheck_wav_header(FILE *f, uint32_t rate, uint32_t bytes)
{
	fseek(f, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, f); pinheck_wav_le(f, 36 + bytes, 4); fwrite("WAVEfmt ", 1, 8, f);
	pinheck_wav_le(f, 16, 4); pinheck_wav_le(f, 1, 2); pinheck_wav_le(f, 2, 2); pinheck_wav_le(f, rate, 4);
	pinheck_wav_le(f, rate * 4, 4); pinheck_wav_le(f, 4, 2); pinheck_wav_le(f, 16, 2);
	fwrite("data", 1, 4, f); pinheck_wav_le(f, bytes, 4);
	fseek(f, 0, SEEK_END);
}

static void pinheck_snd_update(int param, INT16 **buffer, int length)
{
	int16_t tmp[2 * 512];
	int done = 0, i;
	uint64_t now, t0, t1;
	(void)param;
	if (locals.idle) {
		memset(buffer[0], 0, length * sizeof(INT16));
		memset(buffer[1], 0, length * sizeof(INT16));
		return;
	}
	/* render up to the current emulated time; the mixer's sample count per frame need not be rate/fps */
	now = pic32cpu_soc()->cpu.cycles;
	prop_catch_up(&prop, now);
	t0 = snd.t_render;
	t1 = prop_time(&prop, now);
	if (t1 < t0) t1 = t0;
	while (done < length) {
		int n = length - done > 512 ? 512 : length - done;
		audio_render(&snd, tmp, n, t0 + (t1 - t0) * (uint64_t)(done + n) / (uint64_t)length);
		for (i = 0; i < n; i++) {
			buffer[0][done + i] = tmp[2 * i];
			buffer[1][done + i] = tmp[2 * i + 1];
			if (sndl.wav) { pinheck_wav_le(sndl.wav, (uint16_t)tmp[2 * i], 2); pinheck_wav_le(sndl.wav, (uint16_t)tmp[2 * i + 1], 2); }
		}
		if (sndl.wav) sndl.wav_bytes += 4 * (uint32_t)n;
		sndl.samples += (uint64_t)n;
		done += n;
	}
	if (sndl.lag) fprintf(sndl.lag, "%llu %lld\n", (unsigned long long)now, (long long)(t1 - snd.t_render));
}

static int pinheck_sh_start(const struct MachineSound *msound)
{
	const char *names[] = { "Propeller Left", "Propeller Right" };
	const int vol[2] = { MIXER(100, MIXER_PAN_LEFT), MIXER(100, MIXER_PAN_RIGHT) };
	const char *wav = getenv("PINHECK_WAV"), *lag = getenv("PINHECK_SND_LAG");
	(void)msound;
	memset(&sndl, 0, sizeof(sndl));
	if (Machine->sample_rate <= 0) return 0;
	sndl.rate = Machine->sample_rate;
	audio_init(&snd, sndl.rate, pinheck_snd_log, NULL);
	if (wav && (sndl.wav = fopen(wav, "wb")) != NULL) pinheck_wav_header(sndl.wav, (uint32_t)sndl.rate, 0);
	if (lag) sndl.lag = fopen(lag, "w");
	sndl.started = 1;
	return stream_init_multi(2, names, vol, sndl.rate, 0, pinheck_snd_update) < 0;
}

static void pinheck_sh_stop(void)
{
	if (sndl.wav) {
		pinheck_wav_header(sndl.wav, (uint32_t)sndl.rate, sndl.wav_bytes);
		fclose(sndl.wav);
		sndl.wav = NULL;
	}
	if (sndl.lag) fclose(sndl.lag);
	sndl.lag = NULL;
	sndl.started = 0;
}

static struct CustomSound_interface pinheck_sndInt = { pinheck_sh_start, pinheck_sh_stop, 0 };

static MACHINE_INIT(pinheck)
{
	pic32mx_board board = { NULL, pinheck_port_write, pinheck_port_read, pinheck_uart_tx, pinheck_i2c_pins, pinheck_unmapped, pinheck_exception, pinheck_hold };
	const char *log = getenv("PINHECK_UART1_LOG"), *plog = getenv("PINHECK_PROP_LOG");

	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
	memset(&locals, 0, sizeof(locals));
	if (log && (locals.uart1 = fopen(log, opened ? "ab" : "wb")) != NULL) setvbuf(locals.uart1, NULL, _IONBF, 0);
	if (plog && (locals.proplog = fopen(plog, opened ? "a" : "w")) != NULL) setvbuf(locals.proplog, NULL, _IONBF, 0);
	opened = 1;
	locals.reset_at = !reset_done && getenv("PINHECK_RESET_AT") ? atof(getenv("PINHECK_RESET_AT")) : 0.0;
	if (pinheck_system_only()) {
		locals.idle = 1;
		fprintf(stderr, "pinheck: '%s' is the pinHeck system set, not a game; run a game such as dominos\n", Machine->gamedrv->name);
		logerror("pinheck: '%s' is the pinHeck system set, not a game\n", Machine->gamedrv->name);
		return;
	}
	memcpy(propmem, memory_region(PINHECK_PROPREGION), 0x8000);
	prop_init(&prop, memory_region(PINHECK_BIOSREGION), propmem);
	prop_set_log(&prop, pinheck_prop_log, NULL);
	prop_set_tx(&prop, pinheck_prop_tx, NULL);
	if (sndl.started) prop_set_sound(&prop, pinheck_snd_ctr, pinheck_snd_pins, NULL);
	if (sndl.started && getenv("PINHECK_SND_SELFTEST")) {
		audio_pins(&snd, 0, 0, 1u << AUDIO_PIN_L);
		audio_ctr(&snd, 0, 7, 0, (2u << 26) | AUDIO_PIN_L, 0);
		audio_ctr(&snd, 0, 7, 0, 0, 0);
		audio_pins(&snd, 0, 0, 0);
	}
	boot_init(&boot, memory_region(PINHECK_CPUREGION), memory_region_length(PINHECK_CPUREGION), pinheck_boot_tx, NULL);
	boot_set_log(&boot, pinheck_prop_log, NULL);
	pinheck_open_card();
	pinheck_disp_init();
	pic32cpu_set_board(&board);
}

static MACHINE_RESET(pinheck)
{
	const char *at = getenv("PINHECK_UART1_SEND_AT");
	if (locals.idle) return;
	prop_reset(&prop, 0);
	boot_reset(&boot, 0);
	pinheck_disp_reset();
	cat24m01_init(&u13, u13mem, 0);
	ds1340_init(&rtc, pinheck_local_now(), PINHECK_CLOCK);
	locals.rtc_at = 0;
	locals.send = getenv("PINHECK_UART1_SEND");
	locals.send_at = (uint64_t)((at ? atof(at) : 0.0) * PINHECK_CLOCK);
}

static NVRAM_HANDLER(pinheck)
{
	const int first = !read_or_write && !file;
	if (locals.idle) return;
	core_nvram(file, read_or_write, u13mem, sizeof(u13mem), 0xFF);
	core_nvram(file, read_or_write, propmem + 0x8000, sizeof(propmem) - 0x8000, 0xFF);
	if (first && getenv("PINHECK_INSERVICE")) pinheck_in_service(propmem, core_gameData->hw.gameSpecific1);
}

static MACHINE_STOP(pinheck)
{
	if (locals.uart1) fclose(locals.uart1);
	if (locals.proplog) fclose(locals.proplog);
	locals.uart1 = locals.proplog = NULL;
	if (locals.have_vol) vfat_free(&vol);
	if (locals.have_zip) zipsrc_close(&zip);
	locals.have_vol = locals.have_zip = 0;
	pinheck_disp_stop();
}

static MEMORY_READ32_START(pinheck_readmem)
	{ 0x00000000, 0x00000003, MRA32_NOP },
MEMORY_END

static MEMORY_WRITE32_START(pinheck_writemem)
	{ 0x00000000, 0x00000003, MWA32_NOP },
MEMORY_END

MACHINE_DRIVER_START(PINHECK)
	MDRV_IMPORT_FROM(PinMAME)
	MDRV_CORE_INIT_RESET_STOP(pinheck, pinheck, pinheck)
	MDRV_CPU_ADD_TAG("mcpu", PIC32MX, PINHECK_CLOCK)
	MDRV_CPU_MEMORY(pinheck_readmem, pinheck_writemem)
	MDRV_CPU_VBLANK_INT(pinheck_vblank, 1)
	MDRV_TIMER_ADD(pinheck_tick, 1000)
	MDRV_NVRAM_HANDLER(pinheck)
	MDRV_SOUND_ADD(CUSTOM, pinheck_sndInt)
	MDRV_SOUND_ATTRIBUTES(SOUND_SUPPORTS_STEREO)
	MDRV_SCREEN_SIZE(128, 32)
	MDRV_VISIBLE_AREA(0, 127, 0, 31)
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_RGB_DIRECT)
MACHINE_DRIVER_END
