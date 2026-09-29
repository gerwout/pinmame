# Milestone 7: audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Domino's plays its real stereo sound in standalone `sdl3pinmame`, computed exactly from the emulated Propeller's DUTY counters on P14/P15.

**Architecture:** The Propeller core gains a `ctr_state` bus callback reporting every change of a cog's `CTRx`/`FRQx` at the cycle it takes effect. `prop.c` forwards it, and the pin-output stream, to a new pure-C `audio.c`, which keeps each audio pin's level over time and renders each output sample as the exact time-weighted mean of that level, through a 10 Hz DC blocker. The driver adds a stereo PinMAME stream whose update catches the Propeller up to the sample boundaries first. Correctness is proven three ways: a golden list of sink events, Parallax's RTL driving the real DUTY bitstream (per-window high time must match within 2 cycles), and in PinMAME the captured stream of a firmware-played effect must correlate at 0.95 or better with its `.wav`.

**Tech Stack:** C99 (C89-syntax-clean for the new pure-C files), PinMAME 0.37 streams (`stream_init_multi`), OpenSpin + Parallax P1 RTL under Verilator (oracle tools from Plan 3/4), Python 3 with numpy, POSIX sh.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md` (§4.2, §5, §6, §7; binding), parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§5.1, §5.4).

## Prerequisites

- Plans 4, 5 and 6 executed and merged: `pinheck` at `85336dc7` (Merge Milestone 6) or later. Proven and replayed on exactly that commit. This plan edits Plan 5's `prop.[ch]`, `pinheck.c`, `tests/pinheck/link/register_build.py` and `tests/pinheck/pic32mx/makefile_link_check.sh`, and Plan 3's `tests/pinheck/p8x32a/run.c`.
- Plan 6 already gave `prop.c` one pin listener (`prop_set_pins`, used by the display) and the `prop_pins_fn` type. The sound forwarding reuses that type in a second, independent slot (`prop_set_sound`), so the display and the audio both receive every pin change. All edits to shared files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it; never force an edit.
- Oracle tools (OpenSpin, `p1rtl`) as built by Plan 3/4's `tests/pinheck/p8x32a/tools.sh` into `tests/pinheck/p8x32a/build/tools`.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools`; `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip (optional, saves rebuilding it); `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`. Every command below uses these.

## Global Constraints

- `src/wpc/pinheck/audio.[ch]` are pure C: only `<stdint.h>`, `<string.h>`, `<stdio.h>`, `<math.h>`; no PinMAME headers; `extern "C"` guards; C89-syntax-clean (`-std=c89 -pedantic-errors -Wno-long-long`) and warning-free under `-std=c99 -Wall -Wextra -Werror -pedantic`.
- Sink (addendum §4.2): `void (*ctr_state)(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq)` appended to `p8x32a_bus` (existing positional initialisers stay valid), `ctr` 0 = A, 1 = B. Called only when the cog's (CTR, FRQ) pair changes: at `e` (write cycle + 1, the cycle the write takes effect) for CTR/FRQ writes, at the stop cycle on COGSTOP, at `t` on chip reset. NULL-safe.
- Audio pins: P15 left, P14 right. Level: 0 without DIRA, 1 with OUTA, else the sum of `FRQ / 2^32` of DUTY-single (mode 6) counters whose APIN is the pin, capped at 1; two or more drivers logged once; any other mode aimed at the pin (APIN, or BPIN for the differential modes 5 and 7) gives silence, logged once. Counters on other pins must not trigger any work.
- Output: each sample the exact time-weighted mean of the level; DC blocker `y[n] = x[n] − x[n−1] + R·y[n−1]`, `R = exp(−2π·10/fs)`; int16 = `floor(y·32767 + 0.5)` clamped.
- Time: sample boundaries in PIC32 cycles `(samples − base) · 80 000 000 / rate`, `base` rebased at every machine reset, clamped to the PIC32's current cycle, converted with `prop_time()` (Plan 5 clock map). Propeller times stay in Propeller cycles inside `audio.c`.
- Test hooks, inert unless set (as Plan 5's): `PINHECK_WAV=file` writes the stream's exact output as 16-bit stereo PCM WAV; a `~` in `PINHECK_UART1_SEND` waits one emulated second before sending the rest.
- The machine plays 22,050 Hz files at 104 MHz / 4,716 = 22,052.59 Hz (measured, §5.4 of the parent spec as updated by Task 5); machine check 4 resamples the reference at that rate. Headless PinMAME needs `-samplefreq 48000 -fakesound` to run the sound system without an output device.
- Code comments stay short.

## Review Focus

- **The SD and display drivers' counter storm.** Cogs 3 and 6 re-program NCO counters about 6.9 million times in 20 s of play; those events must cost `audio.c` nothing and never change its output. `other_pins_ignored` in Task 2 (a CTR event on P1 must not even run the level update).
- **Launching the `pinheck` system set on its own** (Plan 6's `romset_check.sh`, run by `pinmame_check.sh`): the driver skips all machine setup, but PinMAME still runs the sound stream every frame. The stream must output silence instead of asking the never-initialised Propeller clock map for a time; otherwise the run dies with SIGFPE (exit 136), as it did before the `locals.idle` guard. `pinmame_check.sh` in Task 4 step 7.
- **A machine reset between boots.** Counters are reported off, the sample-to-time mapping rebases, the stream continues. `pinmame_audio.sh` (Task 4) resets at 10 s and plays the effect in the second boot.
- **The real playback rate.** The firmware's sample period is 4,716 cycles, not 104 MHz / 22,050. Against the nominal rate the correlation is only 0.825. Task 4 step 6 shows both.
- **Unexpected counter modes on an audio pin**, including differential modes reaching it through BPIN: silence plus one log line. `unexpected_mode` and `differential_on_bpin` in Task 2.
- **Sink timing against the hardware.** The golden event list (Task 1) pins exact cycles; the RTL oracle (Task 3) proves those cycles are where the real chip's DUTY output changes, within the 1-cycle carry-phase quantisation.

## File Structure

| File | Responsibility |
|---|---|
| `src/cpu/p8x32a/p8x32a.[ch]` | `ctr_state` sink (modified) |
| `tests/pinheck/p8x32a/run.c` | `-ctrlog FILE` records sink events (modified) |
| `src/wpc/pinheck/audio.[ch]` | pin-level timeline, exact integration, DC blocker, int16 stereo render |
| `src/wpc/pinheck/prop.[ch]` | `prop_set_sound()` forwards `ctr_state` and pin output (modified) |
| `src/wpc/pinheck.c` | stereo stream, `PINHECK_WAV`, `~` send pause (modified) |
| `tests/pinheck/link/register_build.py`, build files | `audio.c` in every build (modified / regenerated) |
| `tests/pinheck/pic32mx/makefile_link_check.sh` | `-DHAS_CUSTOM=1`, as the makefile builds define (modified) |
| `tests/pinheck/audio/check.sh` | the audio suite, staged so it works after every task |
| `tests/pinheck/audio/duty.spin`, `duty.ctr.expect` | DUTY stimulus and its golden sink events |
| `tests/pinheck/audio/audio_test.c` | unit tests with exact expected values |
| `tests/pinheck/audio/dutywin.c` | RTL oracle: per-window high time vs `audio.c` |
| `tests/pinheck/audio/corr.py`, `pinmame_audio.sh` | machine check 4 in PinMAME |
| `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` | §5.4 (modified) |

---
### Task 1: Counter-state sink

**Files:**
- Create: `tests/pinheck/audio/duty.spin`, `duty.ctr.expect`, `check.sh`, `.gitignore`
- Modify: `src/cpu/p8x32a/p8x32a.h`, `src/cpu/p8x32a/p8x32a.c`, `tests/pinheck/p8x32a/run.c`

**Interfaces:**
- Consumes: Plan 3's core and runner; `mkrom.py`, OpenSpin and `p1rtl` from Plan 3/4.
- Produces: `p8x32a_bus.ctr_state` (Global Constraints); `p8run -ctrlog FILE` writing lines `C <cycle> <cog> <ctr> <ctr_reg hex8> <frq hex8>`.

`duty.spin` runs in cog 0: DUTY single on P15 (counter A) and P14 (counter B), then FRQ steps (`$4000_0000`, `$0100_0000`, `$FFFF_FFFF`, `$8000_0000`, `$1234_5678`, 0), counter A off with OUTA forcing P14 high, P15's DIRA dropped and restored, `$3000_0000` on both, then COGSTOP. `duty.ctr.expect` is the sink's output for it: every event at the cycle the write takes effect, plus both counters reported off at the stop.

- [ ] **Step 1: Write the stimulus, its golden events and the suite**

`tests/pinheck/audio/duty.spin`:

```text
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     dira, pins
        mov     ctra, dutyl
        mov     frqa, f0
        mov     ctrb, dutyr
        mov     frqb, f1
        mov     t, cnt
        add     t, d1
        waitcnt t, d1
        mov     frqa, f2
        mov     frqb, f3
        waitcnt t, d1
        mov     frqa, f4
        mov     frqb, #0
        waitcnt t, d1
        mov     ctra, #0
        or      outa, p14
        waitcnt t, d1
        mov     ctra, dutyl
        andn    outa, p14
        andn    dira, p15
        waitcnt t, d1
        or      dira, p15
        mov     frqa, f5
        mov     frqb, f5
        waitcnt t, d1
        cogid   t
        cogstop t
t       long    0
d1      long    3000
pins    long    (1 << 14) | (1 << 15)
p14     long    1 << 14
p15     long    1 << 15
dutyl   long    %00110 << 26 | 15
dutyr   long    %00110 << 26 | 14
f0      long    $4000_0000
f1      long    $0100_0000
f2      long    $FFFF_FFFF
f3      long    $8000_0000
f4      long    $1234_5678
f5      long    $3000_0000
        long    $C0DEE0D0
```

`tests/pinheck/audio/duty.ctr.expect`:

```text
C 8212 0 0 1800000f 00000000
C 8216 0 0 1800000f 40000000
C 8220 0 1 1800000e 00000000
C 8224 0 1 1800000e 01000000
C 11233 0 0 1800000f ffffffff
C 11237 0 1 1800000e 80000000
C 14233 0 0 1800000f 12345678
C 14237 0 1 1800000e 00000000
C 17233 0 0 00000000 12345678
C 20233 0 0 1800000f 12345678
C 23237 0 0 1800000f 30000000
C 23241 0 1 1800000e 30000000
C 26264 0 0 00000000 30000000
C 26264 0 1 00000000 30000000
```

`tests/pinheck/audio/check.sh`:

```sh
#!/bin/sh
cd "$(dirname "$0")" || exit 2
B=build
S=../../../src
TOOLS=${TOOLS:-$PWD/../p8x32a/build/tools}
CF="-O2 -std=c99 -Wall -Wextra -Werror -pedantic"
mkdir -p $B
fail=0
cc $CF -I$S/cpu/p8x32a -I$S/wpc/pinheck -o $B/p8run ../p8x32a/run.c $S/cpu/p8x32a/p8x32a.c $S/wpc/pinheck/eeprom.c $S/wpc/pinheck/sd.c $S/wpc/pinheck/vfat.c $S/wpc/pinheck/zipsrc.c -lz || exit 2
[ -x "$TOOLS/openspin/build/openspin" ] || { echo "TOOLS MISSING: build them with ../p8x32a/tools.sh or set TOOLS"; exit 2; }
$TOOLS/openspin/build/openspin -q duty.spin -o $B/duty.binary > $B/duty.clog 2>&1 || { echo "COMPILE FAIL duty.spin"; cat $B/duty.clog; exit 2; }
python3 ../p8x32a/mkrom.py $B/duty.binary $B/duty.rom $B/duty.ram
rm -f $B/duty.ctr
./$B/p8run -rom $B/duty.rom -ram $B/duty.ram -halt -cycles 400000 -ctrlog $B/duty.ctr > $B/duty.our 2> $B/duty.log
if cmp -s duty.ctr.expect $B/duty.ctr; then echo "sink: ok"; else echo "SINK MISMATCH"; diff duty.ctr.expect $B/duty.ctr | head -6; fail=$((fail + 1)); fi
if [ -f audio_test.c ]; then
	cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only -I$S/wpc/pinheck $S/wpc/pinheck/audio.c || { echo "C89 FAIL audio.c"; fail=$((fail + 1)); }
	cc $CF -I$S/wpc/pinheck -o $B/audio_test audio_test.c $S/wpc/pinheck/audio.c -lm || exit 2
	./$B/audio_test || fail=$((fail + 1))
fi
if [ -f dutywin.c ]; then
	cc $CF -I$S/wpc/pinheck -o $B/dutywin dutywin.c $S/wpc/pinheck/audio.c -lm || exit 2
	[ -s $B/duty.rtl ] || timeout 600 $TOOLS/p1rtl/p1rtl -rom $B/duty.rom -ram $B/duty.ram -halt -cycles 400000 -dump $B/duty.rtlhub > $B/duty.rtl
	./$B/dutywin $B/duty.rtl $B/duty.our $B/duty.ctr 500 || fail=$((fail + 1))
fi
echo "audio: $fail failed"
[ $fail -eq 0 ]
```

`chmod +x tests/pinheck/audio/check.sh`

`tests/pinheck/audio/.gitignore`:

```text
build/
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `tests/pinheck/audio/check.sh`
Expected (exit 1; `p8run` rejects the unknown `-ctrlog`):
```text
SINK MISMATCH
diff: build/duty.ctr: No such file or directory
audio: 1 failed
```

- [ ] **Step 3: Add the sink to the core and `-ctrlog` to the runner**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:60])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/cpu/p8x32a/p8x32a.h', [
    ('\tvoid (*log)(void *ctx, const char *msg);\n} p8x32a_bus;',
     '\tvoid (*log)(void *ctx, const char *msg);\n'
     '\tvoid (*ctr_state)(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq); /* ctr: 0 = A, 1 = B; t = cycle the change takes effect */\n'
     '} p8x32a_bus;'),
    ('\tuint64_t phs_t_old[2], ctr_at[2];\n',
     '\tuint64_t phs_t_old[2], ctr_at[2];\n\tuint32_t ctr_seen[2], frq_seen[2];\n'),
])
edit('src/cpu/p8x32a/p8x32a.c', [
    ('static void special_write(',
     'static void ctr_notify(p8x32a *p, int n, int k, uint64_t t)\n'
     '{\n'
     '\tp8x32a_cog *c = &p->cog[n];\n'
     '\tif (c->ctr[k] == c->ctr_seen[k] && c->frq[k] == c->frq_seen[k]) return;\n'
     '\tc->ctr_seen[k] = c->ctr[k];\n'
     '\tc->frq_seen[k] = c->frq[k];\n'
     '\tif (p->bus.ctr_state) p->bus.ctr_state(p->bus.ctx, t, n, k, c->ctr[k], c->frq[k]);\n'
     '}\n\nstatic void special_write('),
    ('\tcase 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; ctr_check(p, v); break;\n',
     '\tcase 0x1F8: case 0x1F9: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->ctr[k] = v; ctr_check(p, v); ctr_notify(p, n, k, e); break;\n'),
    ('\tcase 0x1FA: case 0x1FB: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->frq[k] = v; break;\n',
     '\tcase 0x1FA: case 0x1FB: ctr_save(p, c, k, e); ctr_rebase(c, k, e); c->frq[k] = v; ctr_notify(p, n, k, e); break;\n'),
    ('\tc->ctr[0] = c->ctr[1] = 0;\n}',
     '\tc->ctr[0] = c->ctr[1] = 0;\n\tctr_notify(p, n, 0, d);\n\tctr_notify(p, n, 1, d);\n}'),
    ('\t\tuint32_t ram[512];\n\t\tmemcpy(ram, c->ram, sizeof(ram));',
     '\t\tuint32_t ram[512];\n\t\tc->ctr[0] = c->ctr[1] = 0;\n\t\tctr_notify(p, n, 0, t);\n\t\tctr_notify(p, n, 1, t);\n\t\tmemcpy(ram, c->ram, sizeof(ram));'),
])
edit('tests/pinheck/p8x32a/run.c', [
    ('static void clkset(void *ctx, uint64_t t, uint8_t cfg)\n',
     'static FILE *ctrlog;\n\n'
     'static void ctr_state(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq)\n'
     '{\n'
     '\t(void)ctx;\n'
     '\tif (ctrlog) fprintf(ctrlog, "C %llu %d %d %08x %08x\\n", (unsigned long long)t, cog, ctr, (unsigned)ctr_reg, (unsigned)frq);\n'
     '}\n\n'
     'static void clkset(void *ctx, uint64_t t, uint8_t cfg)\n'),
    ('\tp8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg };',
     '\tp8x32a_bus bus = { NULL, pins_in, pins_next, pins_out, cog_start, clkset, logmsg, ctr_state };'),
    ('\t\telse if (!strcmp(argv[i], "-notrace")) notrace = 1;\n',
     '\t\telse if (!strcmp(argv[i], "-notrace")) notrace = 1;\n'
     '\t\telse if (!strcmp(argv[i], "-ctrlog") && i + 1 < argc) { if (!(ctrlog = fopen(argv[++i], "w"))) { perror(argv[i]); return 2; } }\n'),
    ('[-notrace] [-uart', '[-notrace] [-ctrlog f] [-uart'),
])
EOF
```

- [ ] **Step 4: Run it and confirm it passes**

Run: `tests/pinheck/audio/check.sh`
Expected:
```text
sink: ok
audio: 0 failed
```

- [ ] **Step 5: The Propeller suite still passes**

Run (about 15 minutes the first time, while the RTL references build; the timeout keeps a hang from blocking): `timeout 2400 tests/pinheck/p8x32a/check.sh 2>&1 | tail -5`
Expected: `boot ext=0: 655460 trace lines match`, `boot ext=80000000: 655460 trace lines match`, `sdboot: 804329 trace lines and hub RAM match the RTL to cycle 48100000 (P14/P15 audio DUTY masked)`, `p8x32a: 215 passed, 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/cpu/p8x32a/p8x32a.h src/cpu/p8x32a/p8x32a.c tests/pinheck/p8x32a/run.c tests/pinheck/audio
git commit -m "p8x32a: counter-state sink reporting CTR/FRQ changes at their effect cycle"
```

### Task 2: `audio.c`

**Files:**
- Create: `tests/pinheck/audio/audio_test.c`, `src/wpc/pinheck/audio.h`, `src/wpc/pinheck/audio.c`

**Interfaces:**
- Consumes: sink semantics from Task 1 (events per counter, in Propeller cycles).
- Produces: `audio_init(a, sample_rate, log, ctx)`, `audio_reset(a, t)`, `audio_ctr(a, t, cog, ctr, ctr_reg, frq)`, `audio_pins(a, t, out, dir)`, `double audio_level(a, ch, t0, t1)` (mean level over `[t0, t1)`, consuming history before `t0`; ch 0 = P15 left, 1 = P14 right), `audio_render(a, int16_t *out_stereo, n, t_end)` (n interleaved L/R frames splitting `[last t_end, t_end)` evenly). Events arriving out of order are clamped to the last event time, which the RTL oracle bounds at 2 cycles per 500.

- [ ] **Step 1: Write the failing tests**

`tests/pinheck/audio/audio_test.c`:

```c
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define L (1u << 15)
#define R (1u << 14)
#define DUTY(pin) ((6u << 26) | (pin))

static int fails;
static char last_log[128];
static int nlog;

#define CHECK(c) do { if (!(c)) { printf("AUDIO FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void logmsg(void *ctx, const char *msg) { (void)ctx; strncpy(last_log, msg, sizeof(last_log) - 1); nlog++; }

static void fresh(audio *a)
{
	audio_init(a, 48000.0, logmsg, NULL);
	nlog = 0;
	last_log[0] = 0;
}

static void duty_level(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L | R);
	audio_ctr(&a, 0, 3, 0, DUTY(15), 0x40000000u);
	CHECK(audio_level(&a, 0, 0, 1000) == 0.25);
	CHECK(audio_level(&a, 1, 0, 1000) == 0.0);
}

static void step_inside_sample(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L);
	audio_ctr(&a, 0, 3, 0, DUTY(15), 0);
	audio_ctr(&a, 300, 3, 0, DUTY(15), 0x80000000u);
	CHECK(audio_level(&a, 0, 0, 1000) == 0.35);
	CHECK(audio_level(&a, 0, 1000, 2000) == 0.5);
}

static void outa_and_dira(void)
{
	audio a;
	fresh(&a);
	audio_ctr(&a, 0, 3, 0, DUTY(15), 0x40000000u);
	audio_pins(&a, 0, 0, 0);
	CHECK(audio_level(&a, 0, 0, 100) == 0.0);
	audio_pins(&a, 100, L, L);
	CHECK(audio_level(&a, 0, 100, 200) == 1.0);
	audio_pins(&a, 200, 0, L);
	CHECK(audio_level(&a, 0, 200, 300) == 0.25);
}

static void dc_blocker_step(void)
{
	audio a;
	int16_t out[2 * 6];
	double r = exp(-2.0 * 3.14159265358979323846 * 10.0 / 48000.0);
	int j;
	fresh(&a);
	audio_pins(&a, 0, L, L);
	audio_render(&a, out, 6, 6000);
	for (j = 0; j < 6; j++) {
		CHECK(out[2 * j] == (int16_t)floor(pow(r, j) * 32767.0 + 0.5));
		CHECK(out[2 * j + 1] == 0);
	}
}

static void unexpected_mode(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L);
	audio_ctr(&a, 0, 2, 1, (4u << 26) | 15, 0x40000000u);
	CHECK(audio_level(&a, 0, 0, 100) == 0.0);
	CHECK(nlog == 1);
	CHECK(strstr(last_log, "counter mode 4 on P15") != NULL);
	audio_ctr(&a, 100, 2, 1, (4u << 26) | 15, 0x50000000u);
	CHECK(nlog == 1);
}

static void two_drivers(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, R);
	audio_ctr(&a, 0, 1, 0, DUTY(14), 0x40000000u);
	audio_ctr(&a, 0, 5, 1, DUTY(14), 0x40000000u);
	CHECK(audio_level(&a, 1, 0, 100) == 0.5);
	CHECK(nlog == 1);
	CHECK(strstr(last_log, "2 counters drive P14") != NULL);
}

static void differential_on_bpin(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L);
	audio_ctr(&a, 0, 2, 0, (7u << 26) | (15u << 9) | 3, 0x40000000u);
	CHECK(audio_level(&a, 0, 0, 100) == 0.0);
	CHECK(strstr(last_log, "counter mode 7 on P15") != NULL);
}

static void other_pins_ignored(void)
{
	audio a;
	fresh(&a);
	audio_pins(&a, 0, 0, L);
	audio_ctr(&a, 0, 4, 0, DUTY(15), 0x40000000u);
	audio_ctr(&a, 50, 3, 0, (4u << 26) | 1, 0x01000000u);
	CHECK(a.last_t == 0);
	CHECK(audio_level(&a, 0, 0, 100) == 0.25);
	CHECK(nlog == 0);
}

int main(void)
{
	differential_on_bpin();
	other_pins_ignored();
	duty_level();
	step_inside_sample();
	outa_and_dira();
	dc_blocker_step();
	unexpected_mode();
	two_drivers();
	printf("audio unit: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `tests/pinheck/audio/check.sh`
Expected: exit 2 after `C89 FAIL audio.c` and `fatal error: audio.h: No such file or directory`.

- [ ] **Step 3: Write `audio.[ch]`**

`src/wpc/pinheck/audio.h`:

```c
#ifndef PINHECK_AUDIO_H
#define PINHECK_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PIN_L 15
#define AUDIO_PIN_R 14
#define AUDIO_POINTS 4096

typedef void (*audio_log_fn)(void *ctx, const char *msg);

typedef struct audio_point { uint64_t t; double level; } audio_point;

typedef struct audio_chan {
	audio_point pt[AUDIO_POINTS];
	int head, count;
	double level, x1, y1;
	unsigned logged;
} audio_chan;

typedef struct audio {
	uint32_t ctr[8][2], frq[8][2];
	uint32_t out, dir;
	uint64_t last_t, t_render;
	double r;
	audio_chan ch[2];
	audio_log_fn log;
	void *log_ctx;
} audio;

void audio_init(audio *a, double sample_rate, audio_log_fn log, void *ctx);
void audio_reset(audio *a, uint64_t t);
void audio_ctr(audio *a, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq);
void audio_pins(audio *a, uint64_t t, uint32_t out, uint32_t dir);
double audio_level(audio *a, int ch, uint64_t t0, uint64_t t1);
void audio_render(audio *a, int16_t *out_stereo, int n, uint64_t t_end);

#ifdef __cplusplus
}
#endif

#endif
```

`src/wpc/pinheck/audio.c`:

```c
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MODE(c)  (((c) >> 26) & 31)
#define APIN(c)  ((c) & 31)
#define BPIN(c)  (((c) >> 9) & 31)
#define DIFF(m)  ((m) == 5 || (m) == 7)
#define DUTY1    6
#define LOG_MODE 1
#define LOG_TWO  2
#define LOG_FULL 4

static const int chan_pin[2] = { AUDIO_PIN_L, AUDIO_PIN_R };

static void log_once(audio *a, int ch, unsigned bit, const char *msg)
{
	if (a->ch[ch].logged & bit) return;
	a->ch[ch].logged |= bit;
	if (a->log) a->log(a->log_ctx, msg);
}

static double pin_level(audio *a, int ch)
{
	int pin = chan_pin[ch], n, k, drivers = 0;
	double sum = 0.0;
	char msg[64];
	if (!((a->dir >> pin) & 1)) return 0.0;
	if ((a->out >> pin) & 1) return 1.0;
	for (n = 0; n < 8; n++)
		for (k = 0; k < 2; k++) {
			uint32_t c = a->ctr[n][k];
			if (MODE(c) == 0 || (APIN(c) != (uint32_t)pin && !(DIFF(MODE(c)) && BPIN(c) == (uint32_t)pin))) continue;
			if (MODE(c) != DUTY1 || APIN(c) != (uint32_t)pin) {
				sprintf(msg, "audio: counter mode %u on P%d not modelled, silence", (unsigned)MODE(c), pin);
				log_once(a, ch, LOG_MODE, msg);
				return 0.0;
			}
			sum += a->frq[n][k] / 4294967296.0;
			drivers++;
		}
	if (drivers > 1) {
		sprintf(msg, "audio: %d counters drive P%d, combined as OR", drivers, pin);
		log_once(a, ch, LOG_TWO, msg);
	}
	return sum > 1.0 ? 1.0 : sum;
}

static int on_audio_pin(uint32_t c)
{
	uint32_t m = MODE(c);
	if (!m) return 0;
	if (APIN(c) == AUDIO_PIN_L || APIN(c) == AUDIO_PIN_R) return 1;
	return DIFF(m) && (BPIN(c) == AUDIO_PIN_L || BPIN(c) == AUDIO_PIN_R);
}

static void push(audio *a, int ch, uint64_t t, double level)
{
	audio_chan *c = &a->ch[ch];
	audio_point *last;
	if (c->count) {
		last = &c->pt[(c->head + c->count - 1) % AUDIO_POINTS];
		if (last->level == level) return;
		if (last->t == t) { last->level = level; return; }
	} else if (c->level == level) return;
	if (c->count == AUDIO_POINTS) {
		log_once(a, ch, LOG_FULL, "audio: level history full, oldest change folded");
		c->level = c->pt[c->head].level;
		c->head = (c->head + 1) % AUDIO_POINTS;
		c->count--;
	}
	c->pt[(c->head + c->count) % AUDIO_POINTS].t = t;
	c->pt[(c->head + c->count) % AUDIO_POINTS].level = level;
	c->count++;
}

static void update(audio *a, uint64_t t)
{
	int ch;
	if (t < a->last_t) t = a->last_t;
	a->last_t = t;
	for (ch = 0; ch < 2; ch++) push(a, ch, t, pin_level(a, ch));
}

void audio_init(audio *a, double sample_rate, audio_log_fn log, void *ctx)
{
	memset(a, 0, sizeof(*a));
	a->r = exp(-2.0 * 3.14159265358979323846 * 10.0 / sample_rate);
	a->log = log;
	a->log_ctx = ctx;
}

void audio_reset(audio *a, uint64_t t)
{
	memset(a->ctr, 0, sizeof(a->ctr));
	memset(a->frq, 0, sizeof(a->frq));
	a->out = a->dir = 0;
	if (t > a->last_t) a->last_t = t;
	update(a, t);
}

void audio_ctr(audio *a, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq)
{
	int was = on_audio_pin(a->ctr[cog & 7][ctr & 1]);
	a->ctr[cog & 7][ctr & 1] = ctr_reg;
	a->frq[cog & 7][ctr & 1] = frq;
	if (was || on_audio_pin(ctr_reg)) update(a, t);
}

void audio_pins(audio *a, uint64_t t, uint32_t out, uint32_t dir)
{
	uint32_t m = (1u << AUDIO_PIN_L) | (1u << AUDIO_PIN_R);
	if ((out & m) == (a->out & m) && (dir & m) == (a->dir & m)) return;
	a->out = out;
	a->dir = dir;
	update(a, t);
}

double audio_level(audio *a, int ch, uint64_t t0, uint64_t t1)
{
	audio_chan *c = &a->ch[ch];
	double acc = 0.0, level = c->level;
	uint64_t t = t0;
	while (c->count && c->pt[c->head].t <= t0) {
		c->level = level = c->pt[c->head].level;
		c->head = (c->head + 1) % AUDIO_POINTS;
		c->count--;
	}
	while (c->count && c->pt[c->head].t < t1) {
		acc += level * (double)(c->pt[c->head].t - t);
		t = c->pt[c->head].t;
		c->level = level = c->pt[c->head].level;
		c->head = (c->head + 1) % AUDIO_POINTS;
		c->count--;
	}
	acc += level * (double)(t1 - t);
	return t1 > t0 ? acc / (double)(t1 - t0) : level;
}

void audio_render(audio *a, int16_t *out_stereo, int n, uint64_t t_end)
{
	uint64_t t0 = a->t_render, span = t_end > t0 ? t_end - t0 : 0;
	int j, ch;
	for (j = 0; j < n; j++) {
		uint64_t s0 = t0 + span * (uint64_t)j / (uint64_t)n, s1 = t0 + span * (uint64_t)(j + 1) / (uint64_t)n;
		for (ch = 0; ch < 2; ch++) {
			audio_chan *c = &a->ch[ch];
			double x = audio_level(a, ch, s0, s1), y = x - c->x1 + a->r * c->y1;
			long v = (long)floor(y * 32767.0 + 0.5);
			c->x1 = x;
			c->y1 = y;
			out_stereo[2 * j + ch] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
	}
	if (t_end > a->t_render) a->t_render = t_end;
}
```

- [ ] **Step 4: Run them and confirm they pass**

Run: `tests/pinheck/audio/check.sh`
Expected:
```text
sink: ok
audio unit: ok
audio: 0 failed
```

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck/audio.h src/wpc/pinheck/audio.c tests/pinheck/audio/audio_test.c
git commit -m "pinheck: exact DUTY integration and DC blocker for the Propeller audio pins"
```

### Task 3: RTL oracle for the sink and the integration

**Files:**
- Create: `tests/pinheck/audio/dutywin.c`

**Interfaces:**
- Consumes: `p1rtl` (drives the real DUTY bitstream on P14/P15), `p8run -ctrlog`, `audio_level`.
- Produces: `dutywin RTLTRACE OURTRACE CTRLOG WINDOW` exits 0 when every window's RTL high-cycle count equals `audio_level × WINDOW` within 2 cycles, on both channels.

- [ ] **Step 1: Write the oracle**

`tests/pinheck/audio/dutywin.c`:

```c
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dutywin RTLTRACE OURTRACE CTRLOG WINDOW: per-window high cycles on P15/P14, RTL vs audio.c */
typedef struct ev { unsigned long long t; int kind; unsigned a, b, c, d; } ev;

static ev *load(const char *path, char tag, int *n)
{
	FILE *f = fopen(path, "r");
	char line[256];
	ev *v = NULL;
	int cap = 0;
	*n = 0;
	if (!f) { perror(path); exit(2); }
	while (fgets(line, sizeof(line), f)) {
		ev e;
		memset(&e, 0, sizeof(e));
		if (line[0] != tag) continue;
		if (tag == 'P' && sscanf(line + 2, "%llu %x %x", &e.t, &e.a, &e.b) != 3) continue;
		if (tag == 'C' && sscanf(line + 2, "%llu %u %u %x %x", &e.t, &e.a, &e.b, &e.c, &e.d) != 5) continue;
		if (tag == 'E' && sscanf(line + 2, "%llu", &e.t) != 1) continue;
		e.kind = tag;
		if (*n == cap) { cap = cap ? 2 * cap : 1024; v = realloc(v, cap * sizeof(ev)); }
		v[(*n)++] = e;
	}
	fclose(f);
	return v;
}

int main(int argc, char **argv)
{
	int nr, no, nc, ne, i, j, k, ch, bad = 0, worst = 0;
	ev *rtl, *our, *ctr, *end;
	unsigned long long w, tend, t;
	audio a;
	if (argc != 5) { fprintf(stderr, "usage: dutywin rtl our ctrlog window\n"); return 2; }
	rtl = load(argv[1], 'P', &nr);
	our = load(argv[2], 'P', &no);
	ctr = load(argv[3], 'C', &nc);
	end = load(argv[1], 'E', &ne);
	w = strtoull(argv[4], NULL, 0);
	tend = ne ? end[0].t : rtl[nr - 1].t;
	audio_init(&a, 48000.0, NULL, NULL);
	for (i = j = 0; i < no || j < nc;) {
		if (j >= nc || (i < no && our[i].t <= ctr[j].t)) { audio_pins(&a, our[i].t, our[i].a, our[i].b); i++; }
		else { audio_ctr(&a, ctr[j].t, (int)ctr[j].a, (int)ctr[j].b, ctr[j].c, ctr[j].d); j++; }
	}
	for (ch = 0; ch < 2; ch++) {
		int pin = ch ? AUDIO_PIN_R : AUDIO_PIN_L;
		unsigned long long w0;
		k = 0;
		for (w0 = 0; w0 + w <= tend; w0 += w) {
			long long high = 0, diff;
			unsigned long long lvl_t = w0, cur;
			int level = 0;
			for (k = 0; k < nr && rtl[k].t <= w0; k++) level = (rtl[k].a >> pin) & (rtl[k].b >> pin) & 1;
			cur = w0;
			for (; k < nr && rtl[k].t < w0 + w; k++) {
				if (level) high += (long long)(rtl[k].t - cur);
				cur = rtl[k].t;
				level = (rtl[k].a >> pin) & (rtl[k].b >> pin) & 1;
			}
			if (level) high += (long long)(w0 + w - cur);
			(void)lvl_t;
			diff = high - (long long)(audio_level(&a, ch, w0, w0 + w) * (double)w + 0.5);
			if (diff < 0) diff = -diff;
			if (diff > worst) worst = (int)diff;
			if (diff > 2) { if (bad < 8) printf("P%d window %llu: rtl %lld audio %.1f\n", pin, w0, high, (double)high - (double)diff); bad++; }
		}
	}
	t = tend;
	printf("dutywin: %d windows off by more than 2 cycles, worst %d (window %llu, %llu cycles)\n", bad, worst, w, t);
	return bad != 0;
}
```

- [ ] **Step 2: Run it**

Run: `tests/pinheck/audio/check.sh`
Expected:
```text
sink: ok
audio unit: ok
dutywin: 0 windows off by more than 2 cycles, worst 1 (window 500, 26264 cycles)
audio: 0 failed
```

- [ ] **Step 3: Prove it bites**

Delay every FRQ report by 100 cycles, run the suite, then restore:

```sh
cp src/cpu/p8x32a/p8x32a.c tests/pinheck/audio/build/p8x32a.c.keep
sed -i 's/c->frq\[k\] = v; ctr_notify(p, n, k, e); break;/c->frq[k] = v; ctr_notify(p, n, k, e + 100); break;/' src/cpu/p8x32a/p8x32a.c
tests/pinheck/audio/check.sh | grep -E "MISMATCH|dutywin|audio:"
cp tests/pinheck/audio/build/p8x32a.c.keep src/cpu/p8x32a/p8x32a.c
```
Expected: `SINK MISMATCH`, `dutywin: 7 windows off by more than 2 cycles, worst 92 (window 500, 26264 cycles)`, `audio: 2 failed`. Afterwards `git diff --stat src/cpu/p8x32a` shows nothing.

- [ ] **Step 4: Commit**

```bash
git add tests/pinheck/audio/dutywin.c
git commit -m "pinheck: RTL oracle for the counter-state sink and DUTY integration"
```

### Task 4: Sound in PinMAME, machine check 4

**Files:**
- Create: `tests/pinheck/audio/corr.py`, `tests/pinheck/audio/pinmame_audio.sh`
- Modify: `src/wpc/pinheck/prop.h`, `src/wpc/pinheck/prop.c`, `src/wpc/pinheck.c`, `tests/pinheck/link/register_build.py`, `tests/pinheck/pic32mx/makefile_link_check.sh`; regenerated: `src/pinmame.mak`, `cmake/*/CMakeLists*.txt`, `vcproj/*.vcxproj(.filters)`

**Interfaces:**
- Consumes: `audio.[ch]` (Task 2); Plan 5's `prop_catch_up`, `prop_time`, `pic32cpu_soc()`, `PINHECK_INSERVICE`, `PINHECK_RESET_AT`, `PINHECK_UART1_SEND[_AT]`, `PINHECK_UART1_LOG`, `PINHECK_PROP_LOG`.
- Produces: `prop_set_sound(p, prop_ctr_fn, prop_pins_fn, ctx)` (Propeller-cycle times); the driver's `CUSTOM` stereo stream; `PINHECK_WAV`; the `~` pause; `corr.py capture.wav reference.wav [playback_rate]` printing `corr: L … R …` and exiting 0 at ≥ 0.95 on both channels.

The check boots the in-service machine, resets at 10 s (the first application boot on a blank PIC-side EEPROM only stores defaults and ignores console commands), then 11.5 s into the second boot sends `[F00Z00]` (stop the attract music), waits 3 s, and sends `[F00ATD]` (a 2.89 s effect from `SFX/_FA/ATD.wav`).

- [ ] **Step 1: Write the correlation tool and the check**

`tests/pinheck/audio/corr.py`:

```python
#!/usr/bin/env python3
import sys
import wave

import numpy as np


def load(path):
    w = wave.open(path, 'rb')
    rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    d = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64).reshape(-1, ch)
    w.close()
    return rate, d


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit('usage: corr.py capture.wav reference.wav [playback_rate]')
    rate, cap = load(sys.argv[1])
    rrate, ref = load(sys.argv[2])
    if len(sys.argv) == 4:
        rrate = float(sys.argv[3])
    n = int(len(ref) * rate / rrate)
    t = np.arange(n) * (rrate / rate)
    ref = np.stack([np.interp(t, np.arange(len(ref)), ref[:, c]) for c in range(2)], axis=1)
    if len(cap) < n:
        sys.exit('corr: capture shorter than the reference')
    a, b = cap.sum(axis=1), ref.sum(axis=1)
    m = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    x = np.fft.irfft(np.fft.rfft(a, m) * np.conj(np.fft.rfft(b, m)), m)[:len(a) - n + 1]
    lag = int(np.argmax(np.abs(x)))
    out = []
    for c in range(2):
        v = ref[:, c] - ref[:, c].mean()
        best = -1.0
        for d in range(max(0, lag - 100), min(len(cap) - n, lag + 100) + 1):
            u = cap[d:d + n, c] - cap[d:d + n, c].mean()
            best = max(best, float(np.dot(u, v) / np.sqrt(np.dot(u, u) * np.dot(v, v))))
        out.append(best)
    print('corr: L %.4f R %.4f at %.3f s (%d samples at %d Hz, source played at %.2f Hz)' % (out[0], out[1], lag / rate, n, rate, rrate))
    sys.exit(0 if min(out) >= 0.95 else 1)


if __name__ == '__main__':
    main()
```

`tests/pinheck/audio/pinmame_audio.sh`:

```sh
#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${PINHECK_UPDATE_DIR:?set PINHECK_UPDATE_DIR to the unzipped Domino's update}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
PINHECK_UPDATE_DIR=$(realpath "$PINHECK_UPDATE_DIR") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
[ -n "$PINHECK_ZIP" ] && { PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2; }
SFX=${PINHECK_SFX:-ATD}
cd "$(dirname "$0")" || exit 2
B=build/pinmame
rm -rf $B && mkdir -p $B/roms $B/nvram || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
if [ -n "$PINHECK_ZIP" ]; then
	ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
else
	(cd "$PINHECK_UPDATE_DIR" && zip -q -0 -r "$OLDPWD/$B/roms/dominos.zip" DOM_V006.PRG PRP_V008.BIN DMD SFX) || exit 2
fi
(cd $B && PINHECK_INSERVICE=1 PINHECK_RESET_AT=10 PINHECK_UART1_LOG=$PWD/uart.log PINHECK_PROP_LOG=$PWD/prop.log PINHECK_WAV=$PWD/capture.wav \
	PINHECK_UART1_SEND_AT=11.5 PINHECK_UART1_SEND="[F00Z00]~~~[F00$SFX]" timeout 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram \
	-headless -frames_to_run 1740 -skip_gamewarnings -nothrottle -samplefreq 48000 -fakesound > run.out 2>&1) || { echo "AUDIO FAIL: PinMAME exited $?"; tail -5 $B/run.out; exit 1; }
fail=0
grep -aq "PROPELLER SYNC CHECK\.*OK" $B/uart.log || { echo "AUDIO FAIL: no sync check"; fail=1; }
grep -aqF "Playing SFX" $B/uart.log || { echo "AUDIO FAIL: firmware did not echo Playing SFX"; fail=1; }
grep -aqF "not modelled" $B/prop.log && { echo "AUDIO FAIL: unmodelled audio path:"; grep -aF "not modelled" $B/prop.log; fail=1; }
python3 corr.py $B/capture.wav "$PINHECK_UPDATE_DIR/SFX/_F$(echo $SFX | cut -c1)/$SFX.wav" 22052.59 || fail=1
[ $fail -eq 0 ] && echo "pinmame audio: ok"
[ $fail -eq 0 ]
```

`chmod +x tests/pinheck/audio/corr.py tests/pinheck/audio/pinmame_audio.sh`

- [ ] **Step 2: Build PinMAME without the sound and confirm the check fails**

Build as Plan 5 did (Release, `PLATFORM=linux`, `ARCH=x64`) into a directory of your choice, never the user's `build-dbg`, with a timeout so a hang fails fast:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" 2>&1 | tail -1
SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/audio/pinmame_audio.sh
```
Expected: `[100%] Built target sdl3pinmame`, then (exit 1; the driver has no stream yet, so no capture is written):
```text
Traceback (most recent call last):
  ...
FileNotFoundError: [Errno 2] No such file or directory: 'build/pinmame/capture.wav'
```

- [ ] **Step 3: Forward the sink, add the stream and register `audio.c`**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:60])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


SOUND = '/* sound: Propeller DUTY counters on P15/P14 integrated by audio.c */\nstatic audio snd;\nstatic struct {\n\tint started, rate;\n\tuint64_t samples, base;\n\tFILE *wav;\n\tuint32_t wav_bytes;\n} sndl;\n\nstatic void pinheck_snd_ctr(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq) { (void)ctx; audio_ctr(&snd, t, cog, ctr, ctr_reg, frq); }\nstatic void pinheck_snd_pins(void *ctx, uint64_t t, uint32_t out, uint32_t dir) { (void)ctx; audio_pins(&snd, t, out, dir); }\nstatic void pinheck_snd_log(void *ctx, const char *msg) { (void)ctx; logerror("pinheck: %s\\n", msg); }\n\nstatic void pinheck_wav_le(FILE *f, uint32_t v, int n)\n{\n\twhile (n--) { fputc((int)(v & 0xFF), f); v >>= 8; }\n}\n\nstatic void pinheck_wav_header(FILE *f, uint32_t rate, uint32_t bytes)\n{\n\tfseek(f, 0, SEEK_SET);\n\tfwrite("RIFF", 1, 4, f); pinheck_wav_le(f, 36 + bytes, 4); fwrite("WAVEfmt ", 1, 8, f);\n\tpinheck_wav_le(f, 16, 4); pinheck_wav_le(f, 1, 2); pinheck_wav_le(f, 2, 2); pinheck_wav_le(f, rate, 4);\n\tpinheck_wav_le(f, rate * 4, 4); pinheck_wav_le(f, 4, 2); pinheck_wav_le(f, 16, 2);\n\tfwrite("data", 1, 4, f); pinheck_wav_le(f, bytes, 4);\n\tfseek(f, 0, SEEK_END);\n}\n\nstatic void pinheck_snd_update(int param, INT16 **buffer, int length)\n{\n\tint16_t tmp[2 * 512];\n\tint done = 0, i;\n\t(void)param;\n\tif (locals.idle) {\n\t\tmemset(buffer[0], 0, length * sizeof(INT16));\n\t\tmemset(buffer[1], 0, length * sizeof(INT16));\n\t\treturn;\n\t}\n\twhile (done < length) {\n\t\tint n = length - done > 512 ? 512 : length - done;\n\t\tuint64_t pic = (uint64_t)((double)(sndl.samples + n - sndl.base) * PINHECK_CLOCK / sndl.rate);\n\t\tuint64_t now = pic32cpu_soc()->cpu.cycles;\n\t\tif (pic > now) pic = now;\n\t\tprop_catch_up(&prop, pic);\n\t\taudio_render(&snd, tmp, n, prop_time(&prop, pic));\n\t\tfor (i = 0; i < n; i++) {\n\t\t\tbuffer[0][done + i] = tmp[2 * i];\n\t\t\tbuffer[1][done + i] = tmp[2 * i + 1];\n\t\t\tif (sndl.wav) { pinheck_wav_le(sndl.wav, (uint16_t)tmp[2 * i], 2); pinheck_wav_le(sndl.wav, (uint16_t)tmp[2 * i + 1], 2); }\n\t\t}\n\t\tif (sndl.wav) sndl.wav_bytes += 4 * (uint32_t)n;\n\t\tsndl.samples += (uint64_t)n;\n\t\tdone += n;\n\t}\n}\n\nstatic int pinheck_sh_start(const struct MachineSound *msound)\n{\n\tconst char *names[] = { "Propeller Left", "Propeller Right" };\n\tconst int vol[2] = { MIXER(100, MIXER_PAN_LEFT), MIXER(100, MIXER_PAN_RIGHT) };\n\tconst char *wav = getenv("PINHECK_WAV");\n\t(void)msound;\n\tmemset(&sndl, 0, sizeof(sndl));\n\tif (Machine->sample_rate <= 0) return 0;\n\tsndl.rate = Machine->sample_rate;\n\taudio_init(&snd, sndl.rate, pinheck_snd_log, NULL);\n\tif (wav && (sndl.wav = fopen(wav, "wb")) != NULL) pinheck_wav_header(sndl.wav, (uint32_t)sndl.rate, 0);\n\tsndl.started = 1;\n\treturn stream_init_multi(2, names, vol, sndl.rate, 0, pinheck_snd_update) < 0;\n}\n\nstatic void pinheck_sh_stop(void)\n{\n\tif (sndl.wav) {\n\t\tpinheck_wav_header(sndl.wav, (uint32_t)sndl.rate, sndl.wav_bytes);\n\t\tfclose(sndl.wav);\n\t\tsndl.wav = NULL;\n\t}\n\tsndl.started = 0;\n}\n\nstatic struct CustomSound_interface pinheck_sndInt = { pinheck_sh_start, pinheck_sh_stop, 0 };\n\n'

PAUSE = "\tif (locals.send && *locals.send == '~' && soc->cpu.cycles >= locals.send_at) {\n\t\tlocals.send_at = soc->cpu.cycles + PINHECK_CLOCK;\n\t\tlocals.send++;\n\t}\n"

edit('src/wpc/pinheck/prop.h', [
    ('typedef void (*prop_tx_fn)(void *ctx, uint64_t pic_cycle, int level);\n',
     'typedef void (*prop_tx_fn)(void *ctx, uint64_t pic_cycle, int level);\n'
     'typedef void (*prop_ctr_fn)(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq);\n'),
    ('\tprop_tx_fn tx;\n\tvoid *tx_ctx;\n\tint tx_level;\n',
     '\tprop_tx_fn tx;\n\tvoid *tx_ctx;\n\tint tx_level;\n\tprop_ctr_fn snd_ctr;\n\tprop_pins_fn snd_pins;\n\tvoid *snd_ctx;\n'),
    ('void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx);\n',
     'void prop_set_tx(pinheck_prop *p, prop_tx_fn fn, void *ctx);\n'
     'void prop_set_sound(pinheck_prop *p, prop_ctr_fn ctr, prop_pins_fn pins, void *ctx);\n'),
])
edit('src/wpc/pinheck/prop.c', [
    ('\tp->ee_bits = PIN_SCL | (cat24m01_update(&p->eeprom, scl, sda) ? PIN_SDA : 0);\n',
     '\tif (p->snd_pins) p->snd_pins(p->snd_ctx, t, out, dir);\n'
     '\tp->ee_bits = PIN_SCL | (cat24m01_update(&p->eeprom, scl, sda) ? PIN_SDA : 0);\n'),
    ('static void clkset(void *ctx, uint64_t t, uint8_t cfg)\n',
     'static void ctr_state(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq)\n'
     '{\n'
     '\tpinheck_prop *p = (pinheck_prop *)ctx;\n'
     '\tif (p->snd_ctr) p->snd_ctr(p->snd_ctx, t, cog, ctr, ctr_reg, frq);\n'
     '}\n\n'
     'static void clkset(void *ctx, uint64_t t, uint8_t cfg)\n'),
    ('\tbus.log = logmsg;\n', '\tbus.log = logmsg;\n\tbus.ctr_state = ctr_state;\n'),
    ('void prop_reset(pinheck_prop *p, uint64_t pic_cycle)\n',
     'void prop_set_sound(pinheck_prop *p, prop_ctr_fn ctr, prop_pins_fn pins, void *ctx)\n'
     '{\n'
     '\tp->snd_ctr = ctr;\n'
     '\tp->snd_pins = pins;\n'
     '\tp->snd_ctx = ctx;\n'
     '}\n\n'
     'void prop_reset(pinheck_prop *p, uint64_t pic_cycle)\n'),
])
edit('src/wpc/pinheck.c', [
    ('#include "pinheck/zipsrc.h"\n', '#include "pinheck/zipsrc.h"\n#include "pinheck/audio.h"\n'),
    ('\tif (locals.send && *locals.send && soc->cpu.cycles >= locals.send_at) {\n',
     PAUSE + '\tif (locals.send && *locals.send && soc->cpu.cycles >= locals.send_at) {\n'),
    ('static MACHINE_INIT(pinheck)\n', SOUND + 'static MACHINE_INIT(pinheck)\n'),
    ('\tprop_set_tx(&prop, pinheck_prop_tx, NULL);\n',
     '\tprop_set_tx(&prop, pinheck_prop_tx, NULL);\n\tif (sndl.started) prop_set_sound(&prop, pinheck_snd_ctr, pinheck_snd_pins, NULL);\n'),
    ('\tprop_reset(&prop, 0);\n', '\tprop_reset(&prop, 0);\n\tsndl.base = sndl.samples;\n'),
    ('\tMDRV_NVRAM_HANDLER(pinheck)\n',
     '\tMDRV_NVRAM_HANDLER(pinheck)\n\tMDRV_SOUND_ADD(CUSTOM, pinheck_sndInt)\n\tMDRV_SOUND_ATTRIBUTES(SOUND_SUPPORTS_STEREO)\n'),
])
edit('tests/pinheck/link/register_build.py', [
    ("     'DRVLIBS += $(PINOBJ)/pinheck/sd.o $(PINOBJ)/pinheck/vfat.o $(PINOBJ)/pinheck/zipsrc.o\\n'),\n",
     "     'DRVLIBS += $(PINOBJ)/pinheck/sd.o $(PINOBJ)/pinheck/vfat.o $(PINOBJ)/pinheck/zipsrc.o\\n'),\n"
     "    (['src/wpc/pinheck/audio.c', 'src/wpc/pinheck/audio.h'],\n"
     "     'DRVLIBS += $(PINOBJ)/pinheck/audio.o\\n'),\n"),
])
edit('tests/pinheck/pic32mx/makefile_link_check.sh', [
    ('-DHAS_PIC32MX=1 -Isrc', '-DHAS_PIC32MX=1 -DHAS_CUSTOM=1 -Isrc'),
])
EOF
```

Then: `python3 tests/pinheck/link/register_build.py`
Expected: 19 `updated …` lines (`cmake/*` ×10, `src/pinmame.mak`, `vcproj/*` ×8); a second run prints nothing.

- [ ] **Step 4: Rebuild**

The build reads the root `CMakeLists.txt`, a copy that predates the registration, so copy it again first:

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/m7.log 2>&1; tail -1 build/sdl3pinmame/m7.log
grep -i warning build/sdl3pinmame/m7.log | grep -cE 'audio\.c|prop\.c|pinheck\.c'
```
Expected: `[100%] Built target sdl3pinmame` and `0`. (Without the fresh copy the link fails with `undefined reference to 'audio_init'`.)

- [ ] **Step 5: Machine check 4**

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/audio/pinmame_audio.sh` (about 260 s)
Expected:
```text
corr: L 0.9937 R 0.9931 at 24.528 s (138780 samples at 48000 Hz, source played at 22052.59 Hz)
pinmame audio: ok
```
and `tests/pinheck/audio/build/pinmame/uart.log` contains `Stopping Music` and `Playing SFX`.

- [ ] **Step 6: The playback rate matters**

Run: `python3 tests/pinheck/audio/corr.py tests/pinheck/audio/build/pinmame/capture.wav $PINHECK_UPDATE_DIR/SFX/_FA/ATD.wav; echo "exit=$?"`
Expected: `corr: L 0.8253 R 0.8252 at 24.528 s (138797 samples at 48000 Hz, source played at 22050.00 Hz)` and `exit=1`: at the nominal 22,050 Hz the effect drifts against the capture by about one sample per 200 ms.

- [ ] **Step 7: Nothing else regressed**

Run, each with `timeout 3300` and `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame`: `tests/pinheck/pic32mx/check.sh`, `tests/pinheck/link/check.sh`, `tests/pinheck/pic32mx/pinmame_check.sh`.
Expected: `makefile link: ok` and `pic32mx: 0 failed`; `link: 0 failed`; `romset: ok` then `pinmame: ok` (without the `locals.idle` guard in `pinheck_snd_update` this fails with `ROMSET FAIL: 'pinheck' alone exited 136`). Also run Plan 6's `timeout 3300 tests/pinheck/display/pinmame_display.sh`; expected `pinmame display: ok`.

- [ ] **Step 8: Commit**

```bash
git add src/wpc/pinheck/prop.h src/wpc/pinheck/prop.c src/wpc/pinheck.c tests/pinheck/link/register_build.py tests/pinheck/pic32mx/makefile_link_check.sh src/pinmame.mak cmake vcproj tests/pinheck/audio/corr.py tests/pinheck/audio/pinmame_audio.sh
git commit -m "pinheck: Propeller stereo sound in PinMAME (machine check 4)"
```

### Task 5: Record the findings, prove the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` (§5.4)

- [ ] **Step 1: Update §5.4**

```sh
python3 - <<'EOF'
p = 'docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md'
s = open(p).read()
OLD = 'For each counter in a DUTY mode on P14/P15, the device integrates `FRQx / 2^32` over time. Every value change of `FRQx` or `CTRx` closes a segment. Each output sample at the stream rate (44.1 kHz stereo) is the time-weighted average over its interval, scaled to ±1 and DC-centred. Other counter modes on these pins are logged and output silence.\n\n'
NEW = "The Propeller core reports every change of a cog's `CTRx`/`FRQx` pair through the `ctr_state` bus callback, at the cycle the write takes effect; a COGSTOP and a chip reset report the counter as off. `prop.c` forwards these and the pin-output stream, in Propeller cycles, to `audio.c`.\n\nFor each audio pin (P15 left, P14 right) `audio.c` keeps the pin's level over time: 0 while no cog drives the pin as an output, 1 while OUTA holds it high, otherwise the sum of `FRQ / 2^32` of the DUTY-single counters whose APIN is that pin, capped at 1 (more than one driver is logged once). Any other counter mode aimed at an audio pin, including the differential modes through BPIN, is logged once and outputs silence. Counters on other pins are ignored without cost, which matters because the SD and display drivers re-program their NCO counters millions of times a minute.\n\nEach output sample, at PinMAME's sample rate, is the exact time-weighted mean of that level over its interval, followed by a first-order DC blocker `y[n] = x[n] − x[n−1] + R·y[n−1]` with `R = exp(−2π·10 Hz / fs)`, then scaling to int16. Sample boundaries are PIC32 cycles derived from the stream's sample count (rebased on every machine reset) and converted to Propeller cycles through the clock map (§5.1).\n\nMeasured in game (`PRP_V008.BIN`): cog 4 drives P14 with counter A and P15 with counter B, both DUTY single, silent at `FRQ = $8000_0000`. It writes new samples every 4,720, 4,720, 4,720, 4,704 cycles, a period of exactly 4,716 cycles: the machine plays its 22,050 Hz files at 104 MHz / 4,716 = 22,052.59 Hz, 0.012 % fast. Within each period P14 (right) is written a median 4,080 cycles after P15 (left).\n\n"
assert s.count(OLD) == 1
open(p, 'w').write(s.replace(OLD, NEW, 1))
EOF
```

- [ ] **Step 2: Mutations**

Apply each mutation, run `tests/pinheck/audio/check.sh`, confirm the named failure, then restore the file with `git checkout <file>`:

| File | Replace → with | Must show |
|---|---|---|
| `src/cpu/p8x32a/p8x32a.c` | the two `ctr_notify(p, n, …, d);` lines at the end of `stop_cog` → nothing | `SINK MISMATCH`, `audio: 1 failed` |
| `src/wpc/pinheck/audio.c` | `acc += level * (double)(c->pt[c->head].t - t);` → `acc += c->pt[c->head].level * (double)(c->pt[c->head].t - t);` | `AUDIO FAIL audio_test.c:42`, `dutywin: 11 windows off by more than 2 cycles, worst 237` |
| `src/wpc/pinheck/audio.c` | `* 10.0 / sample_rate` → `* 20.0 / sample_rate` | `AUDIO FAIL audio_test.c:69` |
| `src/wpc/pinheck/audio.c` | `if (was \|\| on_audio_pin(ctr_reg)) update(a, t);` → `(void)was; update(a, t);` | `AUDIO FAIL audio_test.c:116: a.last_t == 0` |
| `src/wpc/pinheck/audio.c` | the line `if (!((a->dir >> pin) & 1)) return 0.0;` → nothing | `AUDIO FAIL audio_test.c:52`, `dutywin: 7 windows off by more than 2 cycles, worst 36` |

Then `git status --short src` prints nothing.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md
git commit -m "Spec §5.4: counter-state sink, audio level model and the measured 22,052.59 Hz playback"
```
