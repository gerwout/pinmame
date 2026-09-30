# pinHeck / Domino's: display (Milestone 6) and audio (Milestone 7) — design addendum

Status: approved design, pre-implementation
Date: 2026-09-29
Parent spec: `2026-09-28-pinheck-dominos-design.md` (binding where this addendum is silent)

## 1. Goal

Show the machine's 128×32 colour display and play its stereo sound in standalone `sdl3pinmame`, both produced by the emulated Propeller running `PRP_V008.BIN` unmodified. VPX / VPinMAME / libpinmame export is out of scope for these milestones.

## 2. Findings this design rests on

Measured by booting `PRP_V008.BIN` headless with the SD card attached (Milestone 4 code, no PIC32, 420 M cycles). The Propeller reaches its update path, draws its update screen and blocks at the bootloader sign-on about 0.5 s in.

### 2.1 Display

The display is not driven by the Propeller's video generator. No `WAITVID` executed, and nothing was logged as unmodelled. The firmware sends finished frames to a display module over a clocked serial link, and only when the picture changes:

| Pin | Connector | Role |
|---|---|---|
| P22 | DMD_1 | serial clock (about 376 kHz observed); data valid on the rising edge |
| P21 | DMD_3 | serial data, MSB first |
| P20 | DMD_5 | strobe: pulses before each frame |
| P17 | DMD_11 | pulses once before a config packet |

- **Frames:** each is exactly 4096 bytes, 128 × 32 pixels, one byte per pixel, RGB332. That's the same format as the `.VID` files. Decoded, the last frame of the probe reads "FLASHING TO: / CONNECT PIC32: FATAL / PLEASE RESTART", which is correct for a run without a PIC32.
- **Config packet:** observed once, 14 bytes: `00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e`. It contains 128 (`80`) and 32 (`20`), and the service-menu display settings (`PIXEL SHAPE` ROUND/SQUARE/HIGHREZ, `BRIGHTNESS`, `POSITION`, `BAR BRIGHT`). The encoding is in the M8/M9 addendum §5.
- **Firmware framebuffer:** the Propeller also keeps a readable 128×32 RGB332 framebuffer in hub RAM at about `$57EE`–`$67ED`, which Plan 5 recorded.

### 2.2 Audio

- **Board path:** P15 → 4.7 kΩ → jack tip (left), and P14 → 4.7 kΩ → jack ring (right). There is no capacitor on the board, so any filtering is in the cabinet amplifier, whose AC coupling is the only part assumed here.
- **Counter modes:** the firmware programs counters in DUTY single mode on P14/P15 (Plan 4). Plan 4 does not drive DUTY onto the pins by design, since audio is computed from the counter state.

## 3. Decisions

| Decision | Choice | Reason |
|---|---|---|
| Target | standalone `sdl3pinmame` | user's target; YAGNI for VPX paths |
| Display model | pin-level model of the receiving display module | exact; sits at the real hardware boundary; cheap |
| Video generator | not used for the display; dropped from the display path | the firmware does not use it (§2.1) |
| Display look | exact pixels; the config packet is decoded, logged and kept but not applied | the module's rendering of the settings is unknown; applied since the display-look plan (M8/M9 addendum §5) |
| Audio model | exact time-weighted duty integration from a counter-state sink, plus a DC-blocking high-pass | spec §5.4; correct for any Propeller mixing rate |
| Verification | offline, sample- and pixel-accurate | emulation runs below real time until Milestone 9 |

## 4. Components

### 4.1 Milestone 6

**`src/wpc/pinheck/display.[ch]`** is pure C, C89-syntax-clean, with no PinMAME headers.

- `display_init(display *d, void *ctx, void (*on_frame)(void *ctx, const uint8_t *frame4096, uint64_t t), void (*on_config)(void *ctx, const uint8_t *bytes, int n, uint64_t t), void (*log)(void *ctx, const char *msg))`
- `display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir)` is fed from the Propeller device's pin-output stream (Plan 5).
- Bits are sampled on each rising edge of P22 while P20/P17 are idle, MSB first.
- A P20 pulse starts a frame, which completes at 4096 bytes. A P17 pulse starts a config packet, which ends at the next strobe or pulse.

**`src/wpc/pinheck.c` (display part)**

- A `CORE_VIDEO` layout of 256×64 in the PinMAME and VPinMAME windows, each dot drawn 2×2 in the display look (M8/M9 addendum §5); libpinmame exports the 128×32 panel as sent (Milestone 10).
- A 256-entry palette set to the RGB332 colours, so frame bytes are pixel values.
- The renderer draws the last completed frame. Config packets are logged when they change, and the latest is kept.

### 4.2 Milestone 7

**`src/cpu/p8x32a/` counter-state sink**

- A new `p8x32a_bus` member, `void (*ctr_state)(void *ctx, uint64_t t, int cog, int ctr, uint32_t ctr_reg, uint32_t frq)`, with `ctr` 0 = A and 1 = B.
- It is called at the cycle each `CTRx` or `FRQx` change takes effect, using the write-timing rule of Plan 4.
- It is NULL-safe: a missing callback changes nothing else.

**`src/wpc/pinheck/audio.[ch]`** is pure C, C89-syntax-clean.

- **Tracking:** it follows which counter drives P15 (left) and P14 (right) in DUTY single mode.
- **Integration:** each channel's level over time is `FRQ / 2³²`.
- **Rendering:** `audio_render(audio *a, int16_t *out_stereo, int n, uint64_t t_end)` produces `n` stereo frames ending at `t_end`, each the exact time-weighted mean over its interval, then a first-order DC-blocking high-pass (corner 10 Hz) and scaling to int16.
- **Clock:** Propeller cycles convert to time through Plan 5's clock map, so the RC clock before `CLKSET $6F` is handled.
- **PHS:** a `PHS` write shifts only the phase, which has no effect on the long-run mean. Its sub-sample effect is ignored.

**`src/wpc/pinheck.c` (sound part)**

- A stereo stream at PinMAME's sample rate.
- Its update function first advances the Propeller to the current emulated time, then calls `audio_render`.

## 5. Failure behaviour

- **Display**
  - A frame cut short by a new strobe, or overrunning 4096 bytes before the next strobe, is discarded. The previous complete frame stays shown, and the event is logged once with the byte count.
  - Config packets of another length or header are logged in full, kept, and never interpreted.
  - Before the first frame the screen is black.
- **Audio**
  - A counter driving P14/P15 in any mode other than DUTY single gives silence on that channel, logged once with the mode.
  - Two drivers on one audio pin are combined as the pin's OR would be: exact for disjoint pulses, an approximation otherwise, logged once.
  - When emulation runs below real time, the stream stays correct in emulated time. Samples are never dropped or invented.

## 6. Verification

### Unit level (headless)

- **`display`:** bit order, the frame strobe, the config packet, a frame cut short, and an overrunning frame, each with exact expected output.
- **`audio`:**
  - FRQ step sequences, including a step inside one sample, with exact expected per-sample means.
  - The DC blocker's step response against its formula.
  - The unexpected-mode path.
- **Counter-state sink timing against the Parallax RTL.** The RTL drives DUTY onto P14/P15. The duty measured from its pin trace over fixed windows must equal what `audio` computes from the sink's events.

### Machine level

- **Parent spec check 3:** `[V00ABC]` is injected on UART1 of the running machine, with conditions chosen so no text is drawn over the video (for example attract video off via `[E96000]`). The decoded frames must equal that `.VID`'s frames pixel for pixel. Each decoded frame must also equal the firmware framebuffer at `$57EE` at the moment it is sent.
- **Parent spec check 4:** `[F00ABC]` is injected, and the emulated stream is captured offline. Each channel's normalised cross-correlation with that `.wav`, resampled from 22,050 Hz, must be at least 0.95.
- **In-game confirmation:** a longer attract-mode run confirms the game keeps using the same frame protocol, and records the counter modes and frame rates it uses.

## 7. Build order and ownership

Plans 6 and 7 are written and executed in parallel once Plan 5 is merged.

- **Plan 6 owns:**
  - `src/wpc/pinheck/display.[ch]`
  - the display part of `src/wpc/pinheck.c`
  - `tests/pinheck/display/`
  - parent-spec §4.3, §5.5 and §9 item 2 edits: the display is a serial frame link, not the video generator; open item 2 is resolved
- **Plan 7 owns:**
  - the counter-state sink in `src/cpu/p8x32a/`
  - `src/wpc/pinheck/audio.[ch]`
  - the sound part of `src/wpc/pinheck.c`
  - `tests/pinheck/audio/`
  - parent-spec §5.4 edits
- **Shared:** both add their new files to the build files, and the merge resolves those additions.

## 8. Out of scope

- Applying the config packet (display look): done by the display-look plan (M8/M9 addendum §5).
- Real-time audio quality: Milestone 9.
- VPX, VPinMAME, libpinmame and external-DMD export.
