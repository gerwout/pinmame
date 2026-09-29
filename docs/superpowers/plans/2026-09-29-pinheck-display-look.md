# Display look Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Domino's display shows the look its service-menu settings select (`PIXEL SHAPE`, `BRIGHTNESS`, `POSITION`, `BAR BRIGHT`) in the `sdl3pinmame` and VPinMAME window, decoded from the config packet the Propeller sends to the display module, while libpinmame keeps exporting the frame as sent.

**Architecture:** The encoding was derived from `DOM_V006.PRG` and proven by stepping every value of every setting in the emulated service menu with `-key_script`: the packet is seven big-endian 16-bit words. `display.c` (pure C) gains `pinheck_display_look`, which decodes a config packet, and `pinheck_display_render`, which draws a 128×32 RGB332 frame into the 256×64 window (2×2 pixels per dot) in that look. The driver's `CORE_VIDEO` renderer uses them outside libpinmame. `look.py` is an independent Python model of both, cross-checked byte for byte against `display.c`; it predicts every config packet of a full menu sweep and computes the golden image for each screen snapshot of the new look check. The Milestone 6 snapshot check (`render.py`) judges its snapshot through the same model.

**Tech Stack:** C99 (C89-syntax-clean for `display.c`), PinMAME 0.37 `CORE_VIDEO` driver, Python 3 (no third-party modules), POSIX sh, `-key_script`, `-snapshot_directory`.

**Spec:** `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` §5 (binding), with `docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md` §2.1 and §5, and the parent `docs/superpowers/specs/2026-09-28-pinheck-dominos-design.md` where both are silent.

## Prerequisites

- `pinheck` at `3d9971c3` or later (Plans 1–8a merged, Plan 8b written). Proven and replayed on exactly that commit. Plan 8b runs concurrently on `pinheck-m8b`; see "Merge with Plan 8b" for the anchors both plans touch.
- All edits to existing files go through anchored edit scripts that assert each anchor exists exactly once; if one fires, stop and rule on it, never force an edit.
- Before starting, from the repository root, export: `TOOLS=$PWD/tests/pinheck/p8x32a/build/tools`; `P8X32A_ROM` = the user-supplied 32 KB mask ROM (CRC32 `f99b3070`); `PINHECK_UPDATE_DIR` = the unzipped Domino's update; `PINHECK_ZIP` = the romset zip; `DOMINOS_PRP=$PINHECK_UPDATE_DIR/PRP_V008.BIN`; `DOMINOS_ZIP=$PINHECK_ZIP`.
- Build PinMAME into the ignored `build/sdl3pinmame` (Release, `PLATFORM=linux`, `ARCH=x64`) as the earlier plans do, never into `build-dbg`, and never commit the root `CMakeLists.txt` copy.
- The emulation runs at about 0.1–0.2× real time (another agent profiles Milestone 9 on the same machine, so timings vary): the new look check takes 20–40 minutes, `pinmame_display.sh` 5–10. Every PinMAME run has a `timeout`.

## Global Constraints

- `display.[ch]` stay pure C: only `<stdint.h>`, `<stdio.h>`, `<string.h>`; no PinMAME headers; every new public name prefixed `pinheck_display_`; C89-syntax-clean (`cc -std=c89 -pedantic-errors -Wno-long-long -fsyntax-only`) and warning-free under `-std=c99 -Wall -Wextra -Werror -pedantic`. Code comments stay short.
- No new source files in the PinMAME build: the look lives in `display.c`, so `register_build.py` and the generated build files do not change.
- The exported frame contract stays: the `CORE_VIDEO` layout is `PINHECK_VIDEO_W` × `PINHECK_VIDEO_H` (256×64), drawn at the layout's top left, black elsewhere; VPinMAME shows that window as it is, so it shows the look.
- libpinmame exports the frame as sent (Milestone 10 gives it native 128×32): the look is compiled only when `LIBPINMAME` is undefined and `PINHECK_VIDEO_SCALE` is 2.
- Config packets that are not the known 14-byte form (words 4–5 = 128, 32) are logged in full, kept, and never interpreted (M6/M7 addendum §5): the renderer then draws the exact pixels.
- Nothing from the firmware is committed: golden images are computed by `look.py` from the decoded frames at check time, never stored.
- Every PinMAME launch in a check uses private `-nvram_directory`, `-cfg_directory` and `timeout -k 30`.
- Every existing suite stays green: `tests/pinheck/{storage,link,pic32mx,board,display,audio}/check.sh`, `pinmame_check.sh`, `romset_check.sh`, `pinmame_display.sh`, `pinmame_audio.sh`, `pinmame_board.sh`.

## Findings this plan rests on

From `DOM_V006.PRG` (flash image at `0x9D000000`, `$gp` = `0xA0008200`) and a sweep of every value in the emulated service menu.

- **The menu.** `MAIN MENU` (Enter `0`, right flipper = next, left = previous): `TEST: SWITCH EDGE`, `AUDIO/MUSIC`, `SOLENOID`, `SERVO`, `LAMP`, `RGB LIGHTING`, `CHANGE: COIL SETTINGS`, `CHANGE: MAIN SETTINGS`, `CHANGE: GAME SETTINGS`, `VIEW: GAME AUDITS`, `OPERATOR SETTINGS`. `MAIN SETTINGS` (`0x9D017990`) opens at item 1 (`FREE PLAY`) and has 18 items; the right flipper steps to the next, Enter steps the value (the screen's `<- CHANGE ->` notwithstanding), Back (`7`) leaves. Items 13–16 are `PIXEL SHAPE`, `BRIGHTNESS`, `POSITION`, `BAR BRIGHT`.
- **The values** (the Enter handlers at `0x9D017D68`, `0x9D017BF0`, `0x9D017BBC`, `0x9D017C24`): `PIXEL SHAPE` 0 `ROUND`, 1 `SQUARE`, 2 `HIGHREZ` (three values: the strings at `0x9D00EC34`–`0x9D00EC44`; the spec's parenthesis names two); `BRIGHTNESS` += 5, from 255 wraps to 175; `POSITION` += 1, from 500 wraps to 300; `BAR BRIGHT` += 2, from 62 wraps to 0.
- **The packet.** Each Enter calls `0x9D02D46C`, which packs seven halfwords and a flag byte little-endian at `0xA000261C` and sends them as link command `0x11` (flag 0 while stepping, `0x80` from Back). The Propeller forwards the seven words to the module most significant byte first, without the flag, flagged by the P17 pulse:

| Bytes | Word | Meaning | Values |
|---|---|---|---|
| 0–1 | 0 | not a menu setting (`0xA00026FA`) | 177 in the Propeller's start-up packet, 250 in every PIC32 packet |
| 2–3 | 1 | `POSITION` (`$gp−0x5B04`) | 300–500; start-up 340 |
| 4–5 | 2 | `PIXEL SHAPE` (`$gp−0x7AA0`) | 0 `ROUND`, 1 `SQUARE`, 2 `HIGHREZ`; start-up 0 |
| 6–7 | 3 | `BRIGHTNESS` (`$gp−0x5B02`) | 175–255 in steps of 5; start-up 255 |
| 8–9 | 4 | width | 128 |
| 10–11 | 5 | height | 32 |
| 12–13 | 6 | `BAR BRIGHT` (`$gp−0x5AFC`) | 0–62 in steps of 2; start-up 62 |

- **Proof of the encoding.** Stepping `PIXEL SHAPE` 3 times, `BRIGHTNESS` 17, `POSITION` 201 and `BAR BRIGHT` 32 times (every value once, back to the start) logged 254 config packets: the start-up packet `00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e`, then one per Enter, each exactly the table's encoding of the menu state. Leaving with Back logged no further packet (the driver logs a packet only when it differs from the last one).
- **Start-up.** The PIC32 does not send the settings at start-up (all five calls of `0x9D02D46C` are in the `MAIN SETTINGS` loop); the Propeller sends its own packet. It keeps `POSITION`, `BRIGHTNESS` and `BAR BRIGHT` across a restart (in its EEPROM, which the driver keeps in NVRAM), but not `PIXEL SHAPE`: after stepping to `HIGHREZ`, 180, 341, 0, leaving with Back and restarting, the start-up packet is `00 b1 01 55 00 00 00 b4 00 80 00 20 00 00`.
- **Unknown:** the display module's firmware and panel. Nothing in either firmware says how the module draws a dot, what `BAR BRIGHT` lights, or what a `POSITION` step measures.

## Rulings

Decisions the spec left open, taken on the evidence named (the user's standing instruction: take the recommended option and record it). Rulings 3–8 are the approximation the spec's caveat names.

1. **`PIXEL SHAPE` has three values**, `ROUND` (0, the start-up value), `SQUARE` (1) and `HIGHREZ` (2) — why: the firmware's menu handler and string table (the spec names two) — cost if wrong: none, the packets carry exactly these three.
2. **The look is drawn at 2×2 window pixels per dot**, inside the existing 256×64 `CORE_VIDEO` layout — why: the exported frame contract stays (VPinMAME shows the window as it is) — cost if wrong: a finer shape would need a larger layout, which changes what VPinMAME shows.
3. **`SQUARE` is the exact picture**: each dot fills its 2×2 cell, as Milestone 6 draws it — why: square dots touching is what a 2-pixel pitch can show of a square dot — cost if wrong: none measurable at this pitch.
4. **`ROUND` is PinMAME's own DMD dot look at a 2-pixel pitch** (`core_dmd_render_internal`): the dot is the top-left pixel of its cell, and the three gap pixels are lit with the mean of the dots around them (two for the side gaps, four for the corner; outside the frame counts as black) — why: a round dot centred in its 2×2 cell covers each pixel equally (π/4), so a coverage model shows no shape at all; PinMAME's dot grid is how every other DMD game looks at this size — cost if wrong: the dot grid is darker than the real module's round dots.
5. **`BAR BRIGHT` is the light between round dots**: the gap pixels of rule 4 take `BAR BRIGHT`/124 of the mean, so the start-up 62 gives one half (PinMAME's default `dmd_antialias` of 50 %) and 0 gives black gaps; it has no effect on `SQUARE` and `HIGHREZ`, which have no gaps — why: the firmware lets `BAR BRIGHT` reach 0 while it keeps `BRIGHTNESS` at 175 or more, so `BAR BRIGHT` cannot be the light of the picture itself (the operator would black out the menu being used), and "bar" fits the dark bars of a dot grid — cost if wrong: `BAR BRIGHT` may be a backlight or an unrelated light; a user who lowers it sees sharper dots, not a darker panel.
6. **`HIGHREZ` is Scale2x** (EPX, neighbours clamped at the frame's edges) — why: a smoothed picture without a dot grid is what "high resolution" can mean at 2 pixels per dot, and Scale2x keeps the frame's colours exactly, so golden images stay exact — cost if wrong: the module may filter differently (bilinear or wider).
7. **`BRIGHTNESS` b scales every colour channel by b/255** (truncating), after the shape — why: 255 is the start-up value and must be the exact picture; the menu floor 175 keeps it readable — cost if wrong: a non-linear panel curve would look different.
8. **`POSITION` p moves the picture down by ⌊(p − 340)/4⌋ window pixels**, clipped to the window, black where it leaves (300 → 10 up, 500 → 40 down) — why: the start-up 340 must be the aligned picture; 128 dots across a 1,024-pixel panel is 8 panel lines per dot, 4 per window pixel, and 340 + 32 × 8 = 596 fits a 600-line panel — cost if wrong: the real step or direction differs (it could be horizontal); the window then moves by a different amount.
9. **Word 0 (177 or 250) is not rendered** — why: no menu item sets it and the menu shows nothing for it — cost if wrong: an unknown module parameter is ignored.
10. **The emulated module keeps no settings of its own**: it draws the latest packet. After a restart the start-up packet carries all four stored settings, `PIXEL SHAPE` included (corrected in the review fix pass: a sweep ending on `SQUARE` restarts with shape 1; the proof above ended on `ROUND`) — why: the packet is the only evidence of what the module is told — cost if wrong: none observed; the window shows what the menu reads.
11. **The look is standalone/VPinMAME only**: compiled when `LIBPINMAME` is undefined and `PINHECK_VIDEO_SCALE` is 2 — why: libpinmame hosts draw their own dots and Milestone 10 gives them the native 128×32 frame — cost if wrong: libpinmame hosts would have to decode the packet themselves to show the look.
12. **`render.py` (Milestone 6's snapshot check) judges its snapshot in the look** of the last config packet in `prop1.log` (the start-up `ROUND` look) instead of the exact 2×2 dots; its size, black-surround and panel checks are unchanged — why: the start-up look is `ROUND`, so the exact picture is no longer what the window shows — cost if wrong: none; `SQUARE` still gives the exact check through the same code.
13. **Golden images are computed, not stored**: `look.py` renders the expected window from the frame in the frame log, and is itself held to `display.c` byte for byte (`crosscheck`) — why: the frames are the firmware's own pictures, which must not be committed — cost if wrong: a common error in both would pass; the unit tests pin `display.c` to hand-computed pixels to prevent that.

## Review Focus

- **Config packets the decoder must not interpret** (another firmware, a corrupt transfer: another length, or words 4–5 not 128 and 32): the renderer keeps the exact picture and the driver logs the packet and one `display: unknown config packet, exact pixels` line. `look_decode` in Task 1 (13 bytes, width 0x0180, no packet).
- **Values outside the menu's ranges** (shape 3, brightness above 255, bar 64, position 0 or 65,535): clamped or clipped, and the renderer never writes outside its 256×64×3 buffer. `look_decode` and `look_position` in Task 1 (a canary byte after the buffer).
- **Dots at the frame's edges**: `ROUND` gaps right of column 127 and below row 31 blend with black, `HIGHREZ` clamps its neighbours, `POSITION` clips. `look_round` (the red dot at 127,31), `look_highrez` and the byte-for-byte `crosscheck` in Task 1.
- **Settings after a restart**: the start-up packet carries all four stored settings (the review fix pass ends the sweep on `SQUARE`), and the window must show exactly that, not `ROUND`. Launch 3 of the look check in Task 2.
- **libpinmame's exported frame**: it must stay the frame as sent. The preprocessor check in `display/check.sh` (Task 2): the render call is in `pinheck.c` without `LIBPINMAME` and absent with it.

## File Structure

| File | Responsibility |
|---|---|
| `src/wpc/pinheck/display.[ch]` | `display_look`, `pinheck_display_look` (packet → look), `pinheck_display_render` (frame → 256×64 RGB in the look) (modified) |
| `tests/pinheck/display/display_test.c` | decoding and rendering unit tests with hand-computed pixels (modified) |
| `tests/pinheck/display/lookdump.c` | renders a pseudo-random frame in given looks, for `look.py crosscheck` |
| `tests/pinheck/display/look.py` | Python model of the decoding and the rendering; `crosscheck`; the look check's key plan and verification |
| `tests/pinheck/display/check.sh` | runs the cross-check and the libpinmame preprocessor check (modified) |
| `src/wpc/pinheck.c` | display block: the look from each config packet, the renderer draws it outside libpinmame (modified) |
| `tests/pinheck/display/render.py`, `pinmame_display.sh` | the Milestone 6 snapshot judged in the look of the last config packet (modified) |
| `tests/pinheck/display/pinmame_look.sh` | the look check: every value of every display setting, golden snapshots, a restart |
| `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md`, `2026-09-29-pinheck-m6-m7-display-audio-design.md`, `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` | the encoding, the rendering, the caveat (modified) |

## Merge with Plan 8b

Plan 8b edits `pinheck.c` (the board block, the servo callback, the port-write callback, the RTC seeding), `pinheck.h`, `pinheckgames.c`, `render.py`, the M8/M9 spec §3.3, §3.4, §4 and the roadmap's m8b row and "Carried into Plan 9". This plan's anchors and where they could meet 8b's:

- `pinheck.c`: only the display block (`disp_rgb15` statics, `pinheck_disp_frame`, `pinheck_disp_config`, `pinheck_disp_reset`, `pinheck_video`). No 8b anchor is in or next to it.
- `render.py`: this plan inserts a line after `from frames import …` (line 13) and edits `frame_matches` (62–63), the `--last` argument line (70–71) and the `hit =` line (80). 8b edits the docstring through `import struct` (line 8), the `SCREEN_H`/`TOL` block (17–18) and the `bad =`/`pens =` panel lines (90–93). The nearest pair is four lines apart, so git merges them; if a hunk ever conflicts, keep both sides.
- Roadmap: this plan rewrites the `| look |` row; 8b rewrites the `| m8b |` row one row above the `| m9 |` row that separates them.
- M8/M9 spec: this plan edits §5 (the caveat line); 8b edits §3.3, §3.4 and §4. No shared lines.
- Not touched here: `pinheck.h`, `pinheckgames.c`, the build files, `register_build.py`, `check5.py`.

---
### Task 1: Decoding and rendering the look in `display.c`

**Files:**
- Modify: `src/wpc/pinheck/display.h`, `src/wpc/pinheck/display.c`
- Create: `tests/pinheck/display/lookdump.c`, `tests/pinheck/display/look.py`
- Test: `tests/pinheck/display/display_test.c`, `tests/pinheck/display/check.sh`

**Interfaces:**
- Consumes: Plan 6's `display.h` (`DISPLAY_W`, `DISPLAY_H`, `DISPLAY_FRAME`), `display_test.c`'s `CHECK` and `fails`, `check.sh`'s `$B`, `$S`, `$CF`.
- Produces: `display_look` (`int shape, brightness, position, bar`), `DISPLAY_ROUND` 0, `DISPLAY_SQUARE` 1, `DISPLAY_HIGHREZ` 2, `DISPLAY_LOOK_W` 256, `DISPLAY_LOOK_H` 64; `int pinheck_display_look(display_look *look, const uint8_t *cfg, int n)` — 1 and the packet's look for a 14-byte packet whose words 4–5 are 128 and 32 (shape above 2 → `SQUARE`, brightness capped at 255, bar at 62, position as sent), otherwise 0 and the exact look (`SQUARE`, 255, 340, 62); `NULL` is allowed; `void pinheck_display_render(const display_look *look, const uint8_t *frame, uint8_t *rgb)` — `frame` 4,096 RGB332 bytes → `rgb` 256 × 64 × 3 bytes (R, G, B), every byte written. `look.py`: `parse(bytes)`, `render(look, frame)` (64 rows of 256 `(r, g, b)`), `name(look)`, `EXACT`, `ROUND`/`SQUARE`/`HIGHREZ`, `crosscheck(prog)`.

The rendering is Rulings 3–8: for window pixel (x, y), dot (x/2, y/2), sub-pixel (x&1, y&1). `SQUARE`: the dot's colour. `ROUND`: sub-pixel (0,0) is the dot's colour; any other is the sum of the dots at (x/2 .. x/2 + sx, y/2 .. y/2 + sy) (outside the frame black) × bar / (124 × count), truncated. `HIGHREZ`: Scale2x on the RGB332 values. Then every channel × brightness / 255, truncated, and the row moves to y + ⌊(position − 340)/4⌋, dropped outside 0–63. RGB332 expands as Milestone 6's palette: r = r3 × 255 / 7, g = g3 × 255 / 7, b = b2 × 255 / 3.

- [ ] **Step 1: Write the failing tests**

`tests/pinheck/display/lookdump.c`:

```c
/* lookdump shape,brightness,position,bar ...: a pseudo-random frame, then its rendering in each look, raw to stdout */
#include "display.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	static const uint8_t colours[4] = { 0x00, 0xFF, 0xE0, 0x49 };
	static uint8_t frame[DISPLAY_FRAME], rgb[DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3];
	uint32_t seed = 1;
	int i;
	for (i = 0; i < DISPLAY_FRAME; i++) {
		seed = seed * 1103515245u + 12345u;
		frame[i] = colours[(seed >> 16) & 3];
	}
	fwrite(frame, 1, sizeof(frame), stdout);
	for (i = 1; i < argc; i++) {
		display_look lk;
		if (sscanf(argv[i], "%d,%d,%d,%d", &lk.shape, &lk.brightness, &lk.position, &lk.bar) != 4) return 2;
		pinheck_display_render(&lk, frame, rgb);
		fwrite(rgb, 1, sizeof(rgb), stdout);
	}
	return 0;
}
```

`tests/pinheck/display/look.py` (the independent Python model; Task 2 adds the machine check to it):

```python
#!/usr/bin/env python3
"""The display module's look (spec M8/M9 addendum 5): a model of display.c's config decoding and rendering.
  look.py crosscheck BIN   the model against display.c through BIN (lookdump)"""
import subprocess
import sys

W, H = 128, 32
LW, LH = 2 * W, 2 * H
ROUND, SQUARE, HIGHREZ = 0, 1, 2
SHAPES = ('ROUND', 'SQUARE', 'HIGHREZ')
EXACT = {'shape': SQUARE, 'brightness': 255, 'position': 340, 'bar': 62}


def parse(cfg):
    """14 bytes -> (look, interpreted), as pinheck_display_look"""
    if cfg is None or len(cfg) != 14:
        return dict(EXACT), False
    w = [cfg[2 * i] << 8 | cfg[2 * i + 1] for i in range(7)]
    if (w[4], w[5]) != (W, H):
        return dict(EXACT), False
    return {'shape': w[2] if w[2] <= HIGHREZ else SQUARE, 'brightness': min(w[3], 255),
            'position': w[1], 'bar': min(w[6], 62)}, True


def rgb332(v):
    return (((v >> 5) & 7) * 255 // 7, ((v >> 2) & 7) * 255 // 7, (v & 3) * 255 // 3)


def scale2x(f, x, y, sx, sy):
    p = f[y * W + x]
    up = f[(y - 1) * W + x] if y > 0 else p
    down = f[(y + 1) * W + x] if y < H - 1 else p
    left = f[y * W + x - 1] if x > 0 else p
    right = f[y * W + x + 1] if x < W - 1 else p
    v, h = (down if sy else up), (right if sx else left)
    v2, h2 = (up if sy else down), (left if sx else right)
    return v if v == h and h != v2 and v != h2 else p


def render(look, f):
    """128x32 RGB332 frame -> 64 rows of 256 (r, g, b), as pinheck_display_render"""
    img = [[(0, 0, 0)] * LW for _ in range(LH)]
    dy = (look['position'] - 340) // 4
    pal = [rgb332(v) for v in range(256)]
    for y in range(LH):
        if not 0 <= y + dy < LH:
            continue
        row, Y, sy = img[y + dy], y >> 1, y & 1
        for x in range(LW):
            X, sx = x >> 1, x & 1
            if look['shape'] == HIGHREZ:
                c = pal[scale2x(f, X, Y, sx, sy)]
            elif look['shape'] == SQUARE or not (sx or sy):
                c = pal[f[Y * W + X]]
            else:
                dots = [pal[f[b * W + a]] for b in range(Y, Y + sy + 1) for a in range(X, X + sx + 1) if a < W and b < H]
                m = (1 + sx) * (1 + sy)
                c = tuple(sum(d[k] for d in dots) * look['bar'] // (124 * m) for k in range(3))
            row[x] = tuple(v * look['brightness'] // 255 for v in c)
    return img


def name(look):
    return '%s brightness %d position %d bar %d' % (SHAPES[look['shape']], look['brightness'], look['position'], look['bar'])


CROSS = ((ROUND, 255, 340, 62), (ROUND, 175, 340, 0), (ROUND, 200, 343, 30), (SQUARE, 255, 340, 62),
         (SQUARE, 180, 339, 62), (HIGHREZ, 255, 340, 62), (HIGHREZ, 230, 500, 62), (HIGHREZ, 255, 300, 10))


def crosscheck(prog):
    """this model against display.c (lookdump) on a pseudo-random frame, byte for byte"""
    out = subprocess.run([prog] + ['%d,%d,%d,%d' % c for c in CROSS], stdout=subprocess.PIPE, check=True).stdout
    frame, n, fail = out[:W * H], LW * LH * 3, 0
    for k, (shape, brightness, position, bar) in enumerate(CROSS):
        look = {'shape': shape, 'brightness': brightness, 'position': position, 'bar': bar}
        want = bytes(v for row in render(look, frame) for p in row for v in p)
        got = out[W * H + k * n:W * H + (k + 1) * n]
        if got != want:
            i = next(i for i in range(n) if i >= len(got) or got[i] != want[i])
            print('LOOK FAIL: display.c and look.py differ in %s at pixel (%d,%d)' % (name(look), i // 3 % LW, i // 3 // LW))
            fail += 1
    if not fail:
        print('look: display.c equals look.py in %d looks' % len(CROSS))
    return fail


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'crosscheck':
        sys.exit(1 if crosscheck(sys.argv[2]) else 0)
    else:
        sys.exit(__doc__)
```

`chmod +x tests/pinheck/display/look.py`

The unit tests (`look_decode`, `look_square`, `look_round`, `look_highrez`, `look_brightness`, `look_position`, each pixel value computed by hand from the rules above) and the cross-check in the suite:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/display_test.c', [
    (r'''int main(void)
{
	frame_and_bit_order();
''',
     r'''static const uint8_t cfg_prop[14] = { 0x00, 0xb1, 0x01, 0x54, 0x00, 0x00, 0x00, 0xff, 0x00, 0x80, 0x00, 0x20, 0x00, 0x3e };
static uint8_t frame[DISPLAY_FRAME], img[DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3 + 16];

static int is(int x, int y, int r, int g, int b)
{
	const uint8_t *p = img + (y * DISPLAY_LOOK_W + x) * 3;
	if (p[0] == r && p[1] == g && p[2] == b) return 1;
	printf("  pixel (%d,%d) = %d,%d,%d, want %d,%d,%d\n", x, y, p[0], p[1], p[2], r, g, b);
	return 0;
}

static void draw(int shape, int brightness, int position, int bar)
{
	display_look lk;
	lk.shape = shape;
	lk.brightness = brightness;
	lk.position = position;
	lk.bar = bar;
	memset(img, 0xA5, sizeof(img));
	pinheck_display_render(&lk, frame, img);
}

static void look_decode(void)
{
	static const uint8_t pic[14] = { 0x00, 0xfa, 0x01, 0xf4, 0x00, 0x02, 0x00, 0xaf, 0x00, 0x80, 0x00, 0x20, 0x00, 0x00 };
	uint8_t odd[14];
	display_look lk;
	CHECK(pinheck_display_look(&lk, cfg_prop, 14) == 1);
	CHECK(lk.shape == DISPLAY_ROUND && lk.position == 340 && lk.brightness == 255 && lk.bar == 62);
	CHECK(pinheck_display_look(&lk, pic, 14) == 1);
	CHECK(lk.shape == DISPLAY_HIGHREZ && lk.position == 500 && lk.brightness == 175 && lk.bar == 0);
	CHECK(pinheck_display_look(&lk, cfg_prop, 13) == 0);
	CHECK(lk.shape == DISPLAY_SQUARE && lk.position == 340 && lk.brightness == 255 && lk.bar == 62);
	memcpy(odd, pic, 14);
	odd[8] = 0x01;
	CHECK(pinheck_display_look(&lk, odd, 14) == 0 && lk.shape == DISPLAY_SQUARE && lk.brightness == 255);
	CHECK(pinheck_display_look(&lk, NULL, 0) == 0 && lk.shape == DISPLAY_SQUARE);
	memcpy(odd, pic, 14);
	odd[2] = odd[3] = 0; odd[5] = 3; odd[6] = 2; odd[13] = 64;
	CHECK(pinheck_display_look(&lk, odd, 14) == 1);
	CHECK(lk.shape == DISPLAY_SQUARE && lk.position == 0 && lk.brightness == 255 && lk.bar == 62);
}

static void look_square(void)
{
	int i, x, y, ok = 1;
	for (i = 0; i < DISPLAY_FRAME; i++) frame[i] = (uint8_t)(i * 7 + 1);
	frame[0] = 0xFF; frame[1] = 0xE0; frame[2] = 0x1C; frame[3] = 0x03; frame[4] = 0x49;
	draw(DISPLAY_SQUARE, 255, 340, 62);
	CHECK(is(0, 0, 255, 255, 255) && is(1, 1, 255, 255, 255) && is(2, 0, 255, 0, 0) && is(4, 1, 0, 255, 0));
	CHECK(is(7, 0, 0, 0, 255) && is(8, 0, 72, 72, 85) && is(9, 1, 72, 72, 85));
	for (y = 0; y < DISPLAY_LOOK_H; y++)
		for (x = 0; x < DISPLAY_LOOK_W; x++) {
			const uint8_t v = frame[(y / 2) * DISPLAY_W + x / 2], *p = img + (y * DISPLAY_LOOK_W + x) * 3;
			ok &= p[0] == ((v >> 5) & 7) * 255 / 7 && p[1] == ((v >> 2) & 7) * 255 / 7 && p[2] == (v & 3) * 255 / 3;
		}
	CHECK(ok);
	CHECK(img[sizeof(img) - 16] == 0xA5);
}

static void look_round(void)
{
	memset(frame, 0, sizeof(frame));
	frame[5 * DISPLAY_W + 10] = 0xFF;
	frame[DISPLAY_FRAME - 1] = 0xE0;
	draw(DISPLAY_ROUND, 255, 340, 62);
	CHECK(is(20, 10, 255, 255, 255) && is(21, 10, 63, 63, 63) && is(20, 11, 63, 63, 63) && is(21, 11, 31, 31, 31));
	CHECK(is(19, 10, 63, 63, 63) && is(19, 9, 31, 31, 31) && is(22, 10, 0, 0, 0) && is(18, 10, 0, 0, 0));
	CHECK(is(254, 62, 255, 0, 0) && is(255, 62, 63, 0, 0) && is(254, 63, 63, 0, 0) && is(255, 63, 31, 0, 0));
	draw(DISPLAY_ROUND, 255, 340, 0);
	CHECK(is(20, 10, 255, 255, 255) && is(21, 10, 0, 0, 0) && is(21, 11, 0, 0, 0) && is(19, 9, 0, 0, 0));
	CHECK(img[sizeof(img) - 16] == 0xA5);
}

static void look_highrez(void)
{
	memset(frame, 0, sizeof(frame));
	frame[1] = 0xFF;
	frame[DISPLAY_W] = 0xFF;
	draw(DISPLAY_HIGHREZ, 255, 340, 62);
	CHECK(is(0, 0, 0, 0, 0) && is(1, 0, 0, 0, 0) && is(0, 1, 0, 0, 0) && is(1, 1, 255, 255, 255));
	CHECK(is(2, 0, 255, 255, 255) && is(3, 1, 255, 255, 255) && is(0, 2, 255, 255, 255) && is(1, 3, 255, 255, 255));
	CHECK(is(2, 2, 255, 255, 255) && is(3, 2, 0, 0, 0) && is(2, 3, 0, 0, 0) && is(3, 3, 0, 0, 0));
	memset(frame, 0x49, sizeof(frame));
	draw(DISPLAY_HIGHREZ, 255, 340, 62);
	CHECK(is(0, 0, 72, 72, 85) && is(255, 63, 72, 72, 85) && is(101, 30, 72, 72, 85));
}

static void look_brightness(void)
{
	memset(frame, 0, sizeof(frame));
	frame[0] = 0xFF;
	frame[1] = 0x49;
	draw(DISPLAY_SQUARE, 175, 340, 62);
	CHECK(is(0, 0, 175, 175, 175) && is(1, 1, 175, 175, 175) && is(2, 0, 49, 49, 58));
	draw(DISPLAY_ROUND, 175, 340, 62);
	CHECK(is(0, 0, 175, 175, 175) && is(1, 0, 55, 55, 58));
}

static void look_position(void)
{
	int x, ok = 1;
	memset(frame, 0, sizeof(frame));
	memset(frame, 0xFF, DISPLAY_W);
	draw(DISPLAY_SQUARE, 255, 344, 62);
	CHECK(is(0, 0, 0, 0, 0) && is(0, 1, 255, 255, 255) && is(255, 2, 255, 255, 255) && is(0, 3, 0, 0, 0));
	draw(DISPLAY_SQUARE, 255, 343, 62);
	CHECK(is(0, 0, 255, 255, 255) && is(0, 1, 255, 255, 255) && is(0, 2, 0, 0, 0));
	draw(DISPLAY_SQUARE, 255, 339, 62);
	CHECK(is(0, 0, 255, 255, 255) && is(0, 1, 0, 0, 0) && is(0, 63, 0, 0, 0));
	draw(DISPLAY_SQUARE, 255, 500, 62);
	CHECK(is(0, 39, 0, 0, 0) && is(0, 40, 255, 255, 255) && is(0, 41, 255, 255, 255) && is(0, 42, 0, 0, 0));
	CHECK(img[sizeof(img) - 16] == 0xA5);
	draw(DISPLAY_SQUARE, 255, 300, 62);
	for (x = 0; x < DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3; x++) ok &= img[x] == 0;
	CHECK(ok);
	draw(DISPLAY_SQUARE, 255, 65535, 62);
	for (x = 0; x < DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3; x++) ok &= img[x] == 0;
	CHECK(ok && img[sizeof(img) - 16] == 0xA5);
}

int main(void)
{
	frame_and_bit_order();
'''),
    (r'''	latch_clock_and_undriven();
	printf("display: %s\n", fails ? "FAIL" : "ok");
''',
     r'''	latch_clock_and_undriven();
	look_decode();
	look_square();
	look_round();
	look_highrez();
	look_brightness();
	look_position();
	printf("display: %s\n", fails ? "FAIL" : "ok");
'''),
])
edit('tests/pinheck/display/check.sh', [
    (r'''./$B/display_test || fail=$((fail + 1))
''',
     r'''./$B/display_test || fail=$((fail + 1))
cc $CF -I$S/wpc/pinheck -o $B/lookdump lookdump.c $S/wpc/pinheck/display.c || exit 2
python3 look.py crosscheck ./$B/lookdump || fail=$((fail + 1))
'''),
])
EOF
```

- [ ] **Step 2: Run the suite to see it fail**

Run: `PINHECK_SKIP_FIRMWARE=1 timeout 300 tests/pinheck/display/check.sh 2>&1 | grep -m1 error`
Expected (the suite exits 2):
```text
display_test.c:151:42: error: ‘DISPLAY_LOOK_W’ undeclared here (not in a function); did you mean ‘DISPLAY_W’?
```

- [ ] **Step 3: Implement the decoding and the rendering**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck/display.h', [
    (r'''typedef void (*display_frame_fn)(void *ctx, const uint8_t *frame4096, uint64_t t);
''',
     r'''/* the look: the module's service-menu settings, from its 14-byte config packet */
#define DISPLAY_LOOK_W  (2 * DISPLAY_W)
#define DISPLAY_LOOK_H  (2 * DISPLAY_H)
#define DISPLAY_ROUND   0
#define DISPLAY_SQUARE  1
#define DISPLAY_HIGHREZ 2

typedef struct display_look {
	int shape;      /* PIXEL SHAPE: DISPLAY_ROUND, DISPLAY_SQUARE or DISPLAY_HIGHREZ */
	int brightness; /* BRIGHTNESS: 175-255 in the menu, 255 = the colours as sent */
	int position;   /* POSITION: 300-500 in the menu, 340 = aligned */
	int bar;        /* BAR BRIGHT: 0-62 in the menu, the light between round dots */
} display_look;

typedef void (*display_frame_fn)(void *ctx, const uint8_t *frame4096, uint64_t t);
'''),
    (r'''void pinheck_display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir);
''',
     r'''void pinheck_display_pins(display *d, uint64_t t, uint32_t out, uint32_t dir);
int pinheck_display_look(display_look *look, const uint8_t *cfg, int n);
void pinheck_display_render(const display_look *look, const uint8_t *frame, uint8_t *rgb);
'''),
])
edit('src/wpc/pinheck/display.c', [
    (r'''		d->nbits++;
	}
}
''',
     r'''		d->nbits++;
	}
}

static int word(const uint8_t *b, int i)
{
	return b[2 * i] << 8 | b[2 * i + 1];
}

/* 7 big-endian words: ?, POSITION, PIXEL SHAPE, BRIGHTNESS, width, height, BAR BRIGHT.
   Anything else keeps the exact look (square dots, as sent) and returns 0. */
int pinheck_display_look(display_look *look, const uint8_t *cfg, int n)
{
	look->shape = DISPLAY_SQUARE;
	look->brightness = 255;
	look->position = 340;
	look->bar = 62;
	if (!cfg || n != 14 || word(cfg, 4) != DISPLAY_W || word(cfg, 5) != DISPLAY_H) return 0;
	look->position = word(cfg, 1);
	look->shape = word(cfg, 2) <= DISPLAY_HIGHREZ ? word(cfg, 2) : DISPLAY_SQUARE;
	look->brightness = word(cfg, 3) < 255 ? word(cfg, 3) : 255;
	look->bar = word(cfg, 6) < 62 ? word(cfg, 6) : 62;
	return 1;
}

static void rgb332(uint8_t v, int *c)
{
	c[0] = ((v >> 5) & 7) * 255 / 7;
	c[1] = ((v >> 2) & 7) * 255 / 7;
	c[2] = (v & 3) * 255 / 3;
}

/* adds dot (x, y)'s colour to c; outside the frame is black */
static void add_dot(const uint8_t *f, int x, int y, int *c)
{
	int d[3];
	if (x >= DISPLAY_W || y >= DISPLAY_H) return;
	rgb332(f[y * DISPLAY_W + x], d);
	c[0] += d[0];
	c[1] += d[1];
	c[2] += d[2];
}

/* Scale2x: sub-pixel (sx, sy) of dot (x, y), neighbours clamped at the edges */
static uint8_t scale2x(const uint8_t *f, int x, int y, int sx, int sy)
{
	uint8_t p = f[y * DISPLAY_W + x];
	uint8_t up = y > 0 ? f[(y - 1) * DISPLAY_W + x] : p, down = y < DISPLAY_H - 1 ? f[(y + 1) * DISPLAY_W + x] : p;
	uint8_t left = x > 0 ? f[y * DISPLAY_W + x - 1] : p, right = x < DISPLAY_W - 1 ? f[y * DISPLAY_W + x + 1] : p;
	uint8_t v = sy ? down : up, h = sx ? right : left, v2 = sy ? up : down, h2 = sx ? left : right;
	return v == h && h != v2 && v != h2 ? v : p;
}

/* frame (128x32 RGB332) -> rgb (256x64, 3 bytes per pixel) in the look */
void pinheck_display_render(const display_look *look, const uint8_t *frame, uint8_t *rgb)
{
	int x, y, k, dy = look->position - 340;
	dy = dy >= 0 ? dy / 4 : -((3 - dy) / 4);
	memset(rgb, 0, DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3);
	for (y = 0; y < DISPLAY_LOOK_H; y++) {
		const int dotY = y >> 1, sy = y & 1;
		uint8_t *out;
		if (y + dy < 0 || y + dy >= DISPLAY_LOOK_H) continue;
		out = rgb + (y + dy) * DISPLAY_LOOK_W * 3;
		for (x = 0; x < DISPLAY_LOOK_W; x++) {
			const int dotX = x >> 1, sx = x & 1;
			int c[3] = { 0, 0, 0 };
			if (look->shape == DISPLAY_HIGHREZ)
				rgb332(scale2x(frame, dotX, dotY, sx, sy), c);
			else if (look->shape == DISPLAY_SQUARE || !(sx | sy))
				add_dot(frame, dotX, dotY, c);
			else {
				/* a gap between round dots: the mean of the dots around it, times bar / 124 */
				const int m = (1 + sx) * (1 + sy);
				add_dot(frame, dotX, dotY, c);
				if (sx) add_dot(frame, dotX + 1, dotY, c);
				if (sy) add_dot(frame, dotX, dotY + 1, c);
				if (sx && sy) add_dot(frame, dotX + 1, dotY + 1, c);
				for (k = 0; k < 3; k++) c[k] = c[k] * look->bar / (124 * m);
			}
			for (k = 0; k < 3; k++) *out++ = (uint8_t)(c[k] * look->brightness / 255);
		}
	}
}
'''),
])
EOF
```

- [ ] **Step 4: Run the suite to see it pass**

Run: `PINHECK_SKIP_FIRMWARE=1 timeout 300 tests/pinheck/display/check.sh`
Expected:
```text
display: ok
look: display.c equals look.py in 8 looks
display: 0 failed
```

- [ ] **Step 5: Commit**

```bash
git add src/wpc/pinheck/display.h src/wpc/pinheck/display.c tests/pinheck/display/display_test.c tests/pinheck/display/lookdump.c tests/pinheck/display/look.py tests/pinheck/display/check.sh
git commit -m "pinheck: decode the display config packet and render its look"
```

### Task 2: The look in PinMAME, the look check

**Files:**
- Modify: `src/wpc/pinheck.c`, `tests/pinheck/display/look.py`, `tests/pinheck/display/render.py`, `tests/pinheck/display/pinmame_display.sh`, `tests/pinheck/display/check.sh`
- Create: `tests/pinheck/display/pinmame_look.sh`

**Interfaces:**
- Consumes: Task 1's `display_look`, `pinheck_display_look`, `pinheck_display_render`, `DISPLAY_LOOK_W`/`H`, `look.py`'s `parse`, `render`, `name`, `EXACT`; Plan 6's `pinheck_disp_*` block (`disp_shown`, `disp_cfg`, `pinheck_prop_log`, `PINHECK_FRAME_LOG`, `PINHECK_PROP_LOG` lines `display: config xx …`) and `frames.py`'s `read_log`; Plan 5's `PINHECK_INSERVICE`.
- Produces: the `CORE_VIDEO` window in the look of the latest config packet (exact before any packet and after an unknown one, which also logs `display: unknown config packet, exact pixels`); `render.py --config HEX`; `look.py plan DIR` (writes `keys.txt`, `frames`, `snap3.txt`) and `look.py verify DIR`; `pinmame_look.sh` printing `pinmame look: ok`.

The look check runs three launches on one NVRAM directory. Launch 1 is the first boot. Launch 2 opens `MAIN MENU` at 10.5 s, walks to `CHANGE: MAIN SETTINGS` and on to `PIXEL SHAPE`, then presses Enter through every value of the four settings (3, 18, 202 and 33 presses: every value, and one more of the three the Propeller stores, ending at `ROUND`, 175, 341, 0), taking a screen snapshot (F12) at the start, after each shape press, and at brightness 175, position 500 and 300 and bar 0, and leaves with Back. Every logged config packet must be the one `look.py` predicts from the menu semantics (the table in "Findings"), and every snapshot's 256×64 window must equal, within the 15-bit screen's rounding, `look.py`'s rendering of one of the last 8 decoded frames in that look (and, unless the look is the exact one, differ from the exact rendering). Launch 3 restarts: its one config packet must be the stored settings (`00 b1 01 55 00 00 00 af 00 80 00 20 00 00`) and its snapshot must be in that look.

- [ ] **Step 1: Write the checks**

The look check in `look.py`:

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/look.py', [
    (r'''"""The display module's look (spec M8/M9 addendum 5): a model of display.c's config decoding and rendering.
  look.py crosscheck BIN   the model against display.c through BIN (lookdump)"""
import subprocess
import sys
''',
     r'''"""The display module's look (spec M8/M9 addendum 5): a model of display.c's config decoding and rendering,
and the look check, which steps the service menu's display settings through every value.
  look.py crosscheck BIN   the model against display.c through BIN (lookdump)
  look.py plan DIR         write DIR/keys.txt and DIR/frames for launch 2
  look.py verify DIR       check DIR/prop2.log's config packets and DIR/snap/*.png against the model"""
import os
import struct
import subprocess
import sys
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the tests
from frames import read_log  # noqa: E402
'''),
    (r'''EXACT = {'shape': SQUARE, 'brightness': 255, 'position': 340, 'bar': 62}
''',
     r'''EXACT = {'shape': SQUARE, 'brightness': 255, 'position': 340, 'bar': 62}
PROP_DEFAULT = '00 b1 01 54 00 00 00 ff 00 80 00 20 00 3e'   # the Propeller's own packet at start-up
SAVED = '00 b1 01 55 00 00 00 af 00 80 00 20 00 00'          # at the next start-up: POSITION, BRIGHTNESS, BAR BRIGHT kept
FPS = 60
TOL = 7                   # 8 -> 5 bit -> 8 bit rounding of a 15 bpp screen
'''),
    (r'''CROSS = ((ROUND, 255, 340, 62),''',
     r'''def near(p, q):
    return all(abs(a - b) <= TOL for a, b in zip(p, q))


def matches(rows, img):
    return all(near(rows[y][x], img[y][x]) for y in range(LH) for x in range(LW))


def packet(st):
    """the config packet the Propeller sends for the PIC32's menu state st"""
    words = (250, st['position'], st['shape'], st['brightness'], W, H, st['bar'])
    return ' '.join('%02x %02x' % (v >> 8, v & 255) for v in words)


def step(st, item):
    """one Enter on a MAIN SETTINGS display item, as DOM_V006 (0x9D017BBC-0x9D017D8C)"""
    st = dict(st)
    if item == 'shape':
        st['shape'] = (st['shape'] + 1) % 3
    elif item == 'brightness':
        st['brightness'] = st['brightness'] + 5 if st['brightness'] + 5 < 256 else 175
    elif item == 'position':
        st['position'] = st['position'] + 1 if st['position'] + 1 < 501 else 300
    else:
        st['bar'] = st['bar'] + 2 if st['bar'] + 2 < 63 else 0
    return st


def key_plan():
    """(frame, key, hold, packet expected after it or None, look of a snapshot or None)"""
    ev, t = [], 630
    st = {'shape': ROUND, 'brightness': 255, 'position': 340, 'bar': 62}

    def tap(key, dt, exp=None, snap=None, hold=6):
        nonlocal t
        ev.append((t, key, hold, exp, snap))
        t += dt
    tap('0', 60)                                    # main menu: SWITCH EDGE
    for _ in range(7):
        tap('RSHIFT', 30)                           # ... CHANGE: MAIN SETTINGS
    tap('0', 60)                                    # MAIN SETTINGS: FREE PLAY
    for _ in range(12):
        tap('RSHIFT', 30)                           # ... PIXEL SHAPE
    tap('F12', 36, snap=dict(st))                   # the start-up look
    # every value; then one more of the stored three (175, 341, 0), with the shape back at ROUND
    for item, n in (('shape', 3), ('brightness', 18), ('position', 202), ('bar', 33)):
        for _ in range(n):
            st = step(st, item)
            if (item == 'shape' or (item == 'brightness' and st['brightness'] == 175) or
                    (item == 'position' and st['position'] in (500, 300)) or (item == 'bar' and st['bar'] == 0)):
                tap('0', 24, packet(st))
                tap('F12', 12, snap=dict(st))
            else:
                tap('0', 15, packet(st))
        tap('RSHIFT', 30)                           # the next item
    tap('7', 60)                                    # Back: leave MAIN SETTINGS, which stores the settings
    return ev, t


def plan(d):
    ev, end = key_plan()
    open(os.path.join(d, 'keys.txt'), 'w').write(''.join('%d tap %d KEYCODE_%s\n' % (t, hold, key) for t, key, hold, _, _ in ev))
    open(os.path.join(d, 'frames'), 'w').write('%d\n' % (end + 30))
    open(os.path.join(d, 'snap3.txt'), 'w').write('1190 tap 2 KEYCODE_F12\n')


def read_png(path):
    d = open(path, 'rb').read()
    i, idat = 8, b''
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i + 8])
        if t == b'IHDR':
            w, h = struct.unpack('>II', d[i + 8:i + 16])
        elif t == b'IDAT':
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), 3 * w, [], bytearray(3 * w)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - 3] if x >= 3 else 0
            b, c = prev[x], prev[x - 3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[3 * x:3 * x + 3]) for x in range(w)])
        prev = line
    return rows


def verify(d):
    fail = 0
    log = open(os.path.join(d, 'prop2.log'), errors='replace').read().splitlines()
    got = [l.split(' ', 2)[2] for l in log if l.startswith('display: config ')]
    ev, _ = key_plan()
    want = [PROP_DEFAULT] + [e[3] for e in ev if e[3]]
    bad = next((k for k in range(max(len(got), len(want))) if k >= len(got) or k >= len(want) or got[k] != want[k]), None)
    if bad is not None:
        print('LOOK FAIL: config packet %d is %s, expected %s' % (bad, got[bad] if bad < len(got) else 'missing',
                                                                  want[bad] if bad < len(want) else 'none'))
        fail += 1
    else:
        print('look: %d config packets, one per menu step, as predicted' % len(got))
    if 'display: unknown config packet, exact pixels' in log:
        print('LOOK FAIL: the driver did not interpret a config packet')
        fail += 1
    for p in got:
        if not parse(bytes.fromhex(p))[1]:
            print('LOOK FAIL: packet %s does not decode' % p)
            fail += 1
    frames = [(pic / 80e6, f) for _, pic, _, f in read_log(os.path.join(d, 'frames2.bin'))]
    snaps = [e for e in ev if e[4]]
    files = ['dominos.png'] + ['domi%04d.png' % k for k in range(len(snaps) - 1)]
    for (t, _, _, _, st), fn in zip(snaps, files):
        path = os.path.join(d, 'snap', fn)
        if not os.path.exists(path):
            print('LOOK FAIL: no snapshot %s (%s)' % (fn, name(st)))
            fail += 1
            continue
        rows = read_png(path)
        recent = [f for ft, f in frames if ft <= t / FPS][-8:]
        hit = next((f for f in reversed(recent) if matches(rows, render(st, f))), None)
        if hit is None:
            print('LOOK FAIL: %s shows none of the last %d frames in the look %s' % (fn, len(recent), name(st)))
            fail += 1
        elif st != EXACT and matches(rows, render(EXACT, hit)):
            print('LOOK FAIL: %s (%s) equals the exact square rendering' % (fn, name(st)))
            fail += 1
        else:
            print('look: %s = %s' % (fn, name(st)))
    got = [l.split(' ', 2)[2] for l in open(os.path.join(d, 'prop3.log'), errors='replace').read().splitlines()
           if l.startswith('display: config ')]
    if got != [SAVED]:
        print('LOOK FAIL: after the restart the config packets are %s, expected %s' % (got, SAVED))
        fail += 1
    else:
        st = parse(bytes.fromhex(SAVED))[0]
        rows = read_png(os.path.join(d, 'snap3', 'dominos.png'))
        recent = [f for _, pic, _, f in read_log(os.path.join(d, 'frames3.bin')) if pic / 80e6 <= 1190 / FPS][-8:]
        if not any(matches(rows, render(st, f)) for f in recent):
            print('LOOK FAIL: after the restart the snapshot shows none of the last %d frames in the look %s' % (len(recent), name(st)))
            fail += 1
        else:
            print('look: after the restart %s' % name(st))
    print('look: %d failed' % fail)
    return fail


CROSS = ((ROUND, 255, 340, 62),'''),
    (r'''    if len(sys.argv) == 3 and sys.argv[1] == 'crosscheck':
        sys.exit(1 if crosscheck(sys.argv[2]) else 0)
''',
     r'''    if len(sys.argv) == 3 and sys.argv[1] == 'crosscheck':
        sys.exit(1 if crosscheck(sys.argv[2]) else 0)
    elif len(sys.argv) == 3 and sys.argv[1] == 'plan':
        plan(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'verify':
        sys.exit(1 if verify(sys.argv[2]) else 0)
'''),
])
EOF
```

`tests/pinheck/display/pinmame_look.sh`:

```sh
#!/bin/sh
: "${SDL3PINMAME:?set SDL3PINMAME to the built sdl3pinmame binary}"
: "${P8X32A_ROM:?set P8X32A_ROM to the 32 KB Propeller mask ROM (crc32 f99b3070)}"
: "${PINHECK_ZIP:?set PINHECK_ZIP to a Domino's romset zip}"
SDL3PINMAME=$(realpath "$SDL3PINMAME") || exit 2
P8X32A_ROM=$(realpath "$P8X32A_ROM") || exit 2
PINHECK_ZIP=$(realpath "$PINHECK_ZIP") || exit 2
cd "$(dirname "$0")" || exit 2
B=build/look
rm -rf $B && mkdir -p $B/roms $B/nvram $B/cfg $B/snap $B/snap3 || exit 2
cp "$P8X32A_ROM" $B/p8x32a.rom && (cd $B && zip -q -j roms/pinheck.zip p8x32a.rom && rm p8x32a.rom) || exit 2
ln -s "$PINHECK_ZIP" $B/roms/dominos.zip || exit 2
python3 look.py plan $B || exit 2
launch() {
	(cd $B && PINHECK_INSERVICE=6 PINHECK_FRAME_LOG=$PWD/frames$1.bin PINHECK_PROP_LOG=$PWD/prop$1.log \
		timeout -k 30 3000 "$SDL3PINMAME" dominos -rompath roms -nvram_directory nvram -cfg_directory cfg -headless -frames_to_run $2 -skip_gamewarnings -nothrottle $3 > run$1.out 2>&1) \
		|| { echo "LOOK FAIL: launch $1 exited $?"; tail -5 $B/run$1.out; exit 1; }
}
# launch 1: first boot on blank NVRAM; launch 2 steps the display settings through every value;
# launch 3 restarts with the stored settings
launch 1 1200
launch 2 "$(cat $B/frames)" "-key_script keys.txt -snapshot_directory snap"
launch 3 1200 "-key_script snap3.txt -snapshot_directory snap3"
python3 look.py verify $B || exit 1
echo "pinmame look: ok"
```

`chmod +x tests/pinheck/display/pinmame_look.sh`

`render.py` judges its snapshot in the look of the last config packet, and the suite gains the libpinmame check (the render call must be in `pinheck.c` without `LIBPINMAME` and absent with it):

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('tests/pinheck/display/render.py', [
    (r'''from frames import FRAME, read_log  # noqa: E402
''',
     r'''from frames import FRAME, read_log  # noqa: E402
from look import parse as parse_look, render as render_look  # noqa: E402
'''),
    (r'''def frame_matches(rows, f):
    return all(near(rows[y][x], rgb332(f[(y // SCALE) * W + x // SCALE])) for y in range(H * SCALE) for x in range(W * SCALE))
''',
     r'''def frame_matches(rows, f, look):
    """the frame region shows f in the module's look (look.py; the exact look draws each dot SCALE x SCALE)"""
    img = render_look(look, f)
    return all(near(rows[y][x], img[y][x]) for y in range(H * SCALE) for x in range(W * SCALE))
'''),
    (r'''    ap.add_argument('--last', type=int, default=8, help='the snapshot must show one of the last N logged frames')
    a = ap.parse_args()
''',
     r'''    ap.add_argument('--last', type=int, default=8, help='the snapshot must show one of the last N logged frames')
    ap.add_argument('--config', help='the last config packet (hex bytes) before the snapshot; none = the exact look')
    a = ap.parse_args()
    look, _ = parse_look(bytes.fromhex(a.config) if a.config else None)
'''),
    (r'''    hit = next((k for k in range(len(frames) - 1, -1, -1) if frame_matches(rows, frames[k])), None)
''',
     r'''    hit = next((k for k in range(len(frames) - 1, -1, -1) if frame_matches(rows, frames[k], look)), None)
'''),
])
edit('tests/pinheck/display/check.sh', [
    (r'''python3 look.py crosscheck ./$B/lookdump || fail=$((fail + 1))
''',
     r'''python3 look.py crosscheck ./$B/lookdump || fail=$((fail + 1))
calls() { cc -E "$@" -I$S -I$S/wpc -I$S/unix -I$S/unix/sysdep $S/wpc/pinheck.c | grep -c 'pinheck_display_render(&disp_look'; }
if [ "$(calls)" = 1 ] && [ "$(calls -DLIBPINMAME)" = 0 ]; then
	echo "look: sdl3pinmame renders the look, libpinmame gets the frame as sent"
else
	echo "LOOK FAIL: the look must be in the standalone renderer and not in libpinmame's"; fail=$((fail + 1))
fi
'''),
])
edit('tests/pinheck/display/pinmame_display.sh', [
    (r'''python3 render.py $B/snap/dominos.png $B/frames1.bin || fail=1
''',
     r'''python3 render.py $B/snap/dominos.png $B/frames1.bin --config "$(grep '^display: config ' $B/prop1.log | tail -1 | cut -d' ' -f3-)" || fail=1
'''),
])
EOF
```

- [ ] **Step 2: See the checks fail**

Build PinMAME as it stands (Task 1's `display.c` is in, the renderer is unchanged):

```sh
cp cmake/sdl3pinmame/CMakeLists.txt CMakeLists.txt
cmake -S . -B build/sdl3pinmame -DCMAKE_BUILD_TYPE=Release -DPLATFORM=linux -DARCH=x64 > /dev/null
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/look.log 2>&1; tail -1 build/sdl3pinmame/look.log
PINHECK_SKIP_FIRMWARE=1 timeout 300 tests/pinheck/display/check.sh
SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/display/pinmame_display.sh
SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/display/pinmame_look.sh
```
Expected: `[100%] Built target sdl3pinmame`; the suite exits 1:
```text
display: ok
look: display.c equals look.py in 8 looks
LOOK FAIL: the look must be in the standalone renderer and not in libpinmame's
display: 1 failed
```
`pinmame_display.sh` exits 1 (about 6 minutes):
```text
frames: 394 frames from 1.68s to 20.02s, 21.4 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $5870 when latched
render: FAIL, the frame region shows none of the last 8 decoded frames
render: 256x256 visible, black outside the frame apart from the core panel (4 pens, none a frame colour)
frames: 75 frames from 12.02s to 14.99s, 24.9 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $5870 when latched
frames: LT5.VID: frames 1..33 of 34 shown pixel-exact, contiguous and in order (log frames 1..33)
```
`pinmame_look.sh` exits 1 (20–40 minutes, depending on load; the packets are right, the window is not):
```text
look: 257 config packets, one per menu step, as predicted
LOOK FAIL: dominos.png shows none of the last 8 frames in the look ROUND brightness 255 position 340 bar 62
look: domi0000.png = SQUARE brightness 255 position 340 bar 62
LOOK FAIL: domi0001.png shows none of the last 8 frames in the look HIGHREZ brightness 255 position 340 bar 62
LOOK FAIL: domi0002.png shows none of the last 8 frames in the look ROUND brightness 255 position 340 bar 62
LOOK FAIL: domi0003.png shows none of the last 8 frames in the look ROUND brightness 175 position 340 bar 62
LOOK FAIL: domi0004.png shows none of the last 8 frames in the look ROUND brightness 175 position 340 bar 62
LOOK FAIL: domi0005.png shows none of the last 8 frames in the look ROUND brightness 175 position 500 bar 62
LOOK FAIL: domi0006.png shows none of the last 8 frames in the look ROUND brightness 175 position 300 bar 62
LOOK FAIL: domi0007.png shows none of the last 8 frames in the look ROUND brightness 175 position 341 bar 0
LOOK FAIL: domi0008.png shows none of the last 8 frames in the look ROUND brightness 175 position 341 bar 0
LOOK FAIL: after the restart the snapshot shows none of the last 8 frames in the look ROUND brightness 175 position 341 bar 0
look: 10 failed
```

- [ ] **Step 3: Draw the look in the driver**

The display block of `pinheck.c` only: the look is set from each config packet (and to exact on reset), and the renderer draws `pinheck_display_render`'s image outside libpinmame, re-rendering only after a new frame or packet.

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('src/wpc/pinheck.c', [
    (r'''static UINT32 disp_rgb32[256];
static UINT16 disp_rgb15[256];
''',
     r'''static UINT32 disp_rgb32[256];
static UINT16 disp_rgb15[256];
static display_look disp_look;
static uint8_t disp_img[DISPLAY_LOOK_W * DISPLAY_LOOK_H * 3];
static int disp_dirty;
'''),
    (r'''	memcpy(disp_shown, frame, DISPLAY_FRAME);
	if (!disp_log) return;
''',
     r'''	memcpy(disp_shown, frame, DISPLAY_FRAME);
	disp_dirty = 1;
	if (!disp_log) return;
'''),
    (r'''	for (k = 0; k < n; k++) len += sprintf(msg + len, " %02x", bytes[k]);
	pinheck_prop_log(NULL, msg);
}
''',
     r'''	for (k = 0; k < n; k++) len += sprintf(msg + len, " %02x", bytes[k]);
	pinheck_prop_log(NULL, msg);
	if (!pinheck_display_look(&disp_look, bytes, n)) pinheck_prop_log(NULL, "display: unknown config packet, exact pixels");
	disp_dirty = 1;
}
'''),
    (r'''	memset(disp_shown, 0, sizeof(disp_shown));
	disp_cfg_n = 0;
}
''',
     r'''	memset(disp_shown, 0, sizeof(disp_shown));
	disp_cfg_n = 0;
	pinheck_display_look(&disp_look, NULL, 0);
	disp_dirty = 1;
}
'''),
    (r'''PINMAME_VIDEO_UPDATE(pinheck_video)
{
	const int s = PINHECK_VIDEO_SCALE, x0 = layout->left, y0 = layout->top;
	int x, y;
	/* the core's visible area is larger than the panel: clear it so nothing stale shows */
	fillbitmap(bitmap, get_black_pen(), cliprect);
	for (y = 0; y < DISPLAY_H * s && y0 + y < bitmap->height; y++)
		for (x = 0; x < DISPLAY_W * s && x0 + x < bitmap->width; x++) {
			const uint8_t v = disp_shown[(y / s) * DISPLAY_W + x / s];
			if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y0 + y])[x0 + x] = disp_rgb32[v];
			else ((UINT16 *)bitmap->line[y0 + y])[x0 + x] = disp_rgb15[v];
		}
}
''',
     r'''PINMAME_VIDEO_UPDATE(pinheck_video)
{
	const int x0 = layout->left, y0 = layout->top;
	int x, y;
	/* the core's visible area is larger than the panel: clear it so nothing stale shows */
	fillbitmap(bitmap, get_black_pen(), cliprect);
#if !defined(LIBPINMAME) && PINHECK_VIDEO_SCALE == 2
	/* the module's look (dot shape, brightness, position); libpinmame hosts get the frame as sent */
	if (disp_dirty) pinheck_display_render(&disp_look, disp_shown, disp_img);
	disp_dirty = 0;
	for (y = 0; y < DISPLAY_LOOK_H && y0 + y < bitmap->height; y++)
		for (x = 0; x < DISPLAY_LOOK_W && x0 + x < bitmap->width; x++) {
			const uint8_t *p = disp_img + (y * DISPLAY_LOOK_W + x) * 3;
			if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y0 + y])[x0 + x] = MAKE_RGB(p[0], p[1], p[2]);
			else ((UINT16 *)bitmap->line[y0 + y])[x0 + x] = (UINT16)(((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
		}
#else
	for (y = 0; y < DISPLAY_H * PINHECK_VIDEO_SCALE && y0 + y < bitmap->height; y++)
		for (x = 0; x < DISPLAY_W * PINHECK_VIDEO_SCALE && x0 + x < bitmap->width; x++) {
			const uint8_t v = disp_shown[(y / PINHECK_VIDEO_SCALE) * DISPLAY_W + x / PINHECK_VIDEO_SCALE];
			if (bitmap->depth == 32) ((UINT32 *)bitmap->line[y0 + y])[x0 + x] = disp_rgb32[v];
			else ((UINT16 *)bitmap->line[y0 + y])[x0 + x] = disp_rgb15[v];
		}
#endif
}
'''),
])
EOF
```

- [ ] **Step 4: Rebuild**

```sh
timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)" > build/sdl3pinmame/look.log 2>&1; tail -1 build/sdl3pinmame/look.log
grep -i warning build/sdl3pinmame/look.log | grep -cE 'display\.c|pinheck\.c'
```
Expected: `[100%] Built target sdl3pinmame` and `0`.

- [ ] **Step 5: The display suite passes**

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/display/check.sh` (runs `dispboot` and `pinmame_display.sh` too; about 8 minutes)
Expected:
```text
display: ok
look: display.c equals look.py in 8 looks
look: sdl3pinmame renders the look, libpinmame gets the frame as sent
dispboot: 4 frames (0 uniform), 1 config packets, framebuffer $5870, 0 frames not in hub RAM
frames: 394 frames from 1.68s to 20.02s, 21.4 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $5870 when latched
render: frame region equals decoded frame 7 of the last 8, 2x2 per pixel
render: 256x256 visible, black outside the frame apart from the core panel (4 pens, none a frame colour)
frames: 75 frames from 12.02s to 14.99s, 24.9 per second
frames: every non-uniform frame equals the firmware framebuffer at hub $5870 when latched
frames: LT5.VID: frames 1..33 of 34 shown pixel-exact, contiguous and in order (log frames 1..33)
pinmame display: ok
display: 0 failed
```

- [ ] **Step 6: The look check passes**

Run: `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame timeout 3300 tests/pinheck/display/pinmame_look.sh` (20–40 minutes, depending on load)
Expected:
```text
look: 257 config packets, one per menu step, as predicted
look: dominos.png = ROUND brightness 255 position 340 bar 62
look: domi0000.png = SQUARE brightness 255 position 340 bar 62
look: domi0001.png = HIGHREZ brightness 255 position 340 bar 62
look: domi0002.png = ROUND brightness 255 position 340 bar 62
look: domi0003.png = ROUND brightness 175 position 340 bar 62
look: domi0004.png = ROUND brightness 175 position 340 bar 62
look: domi0005.png = ROUND brightness 175 position 500 bar 62
look: domi0006.png = ROUND brightness 175 position 300 bar 62
look: domi0007.png = ROUND brightness 175 position 341 bar 0
look: domi0008.png = ROUND brightness 175 position 341 bar 0
look: after the restart ROUND brightness 175 position 341 bar 0
look: 0 failed
pinmame look: ok
```

- [ ] **Step 7: Nothing else regressed**

Run, each with `timeout 3300` and `SDL3PINMAME=$PWD/build/sdl3pinmame/sdl3pinmame`: `tests/pinheck/storage/check.sh`, `tests/pinheck/link/check.sh`, `tests/pinheck/pic32mx/check.sh`, `tests/pinheck/board/check.sh`, `tests/pinheck/audio/check.sh`, `tests/pinheck/pic32mx/pinmame_check.sh`, `tests/pinheck/pic32mx/romset_check.sh`, `tests/pinheck/audio/pinmame_audio.sh`, `tests/pinheck/board/pinmame_board.sh` (they can run in parallel; `pinmame_board.sh` takes about 25 minutes).
Expected: the last lines `storage: 0 failed`, `link: 0 failed`, `pic32mx: 0 failed`, `board: 0 failed`, `audio: 0 failed`, `pinmame: ok`, `romset: ok`, `pinmame audio: ok`, and:
```text
timing: 74937 edge pairs less than 1 ms apart, 0 with the wrong PinMAME time
levels: lamp 11 at brightness 1-7 reads 0.169 0.322 0.455 0.580 0.722 0.831 0.949 (median, 0-1)
check5: 106 uart commands, 266 service-test steps, 0 failures
pinmame board: ok
```

- [ ] **Step 8: Commit**

```bash
git add src/wpc/pinheck.c tests/pinheck/display/look.py tests/pinheck/display/render.py tests/pinheck/display/pinmame_display.sh tests/pinheck/display/check.sh tests/pinheck/display/pinmame_look.sh
git commit -m "pinheck: show the display look in the window, look check"
```

### Task 3: Record the encoding, prove the tests bite

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md` (§5), `docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md` (§2.1, §3, §8), `docs/superpowers/plans/2026-09-28-pinheck-roadmap.md` (the look row)

- [ ] **Step 1: Update the specs and the roadmap**

```sh
python3 - <<'EOF'
def edit(path, pairs):
    s = open(path).read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b, 1)
    open(path, 'w').write(s)


edit('docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md', [
    ('- **Caveat:** the real display module\'s pixel rendering is unknown, so the shape rendering is labelled an approximation.\n',
     '- **Caveat:** the real display module\'s pixel rendering is unknown, so the shape rendering is labelled an approximation. At 2×2 window pixels per dot, the dot shapes, the light between dots and the scale of `POSITION` are rulings of the display-look plan, not measurements of the module.\n'
     '\n'
     'Established by the display-look plan, from `DOM_V006.PRG` and a sweep of every value in the emulated service menu (`CHANGE: MAIN SETTINGS`):\n'
     '\n'
     '- **Packet.** Seven 16-bit words, most significant byte first:\n'
     '\n'
     '| Bytes | Word | Meaning | Values |\n'
     '|---|---|---|---|\n'
     '| 0–1 | 0 | not a menu setting | 177 in the Propeller\'s start-up packet, 250 in every packet the PIC32 has sent |\n'
     '| 2–3 | 1 | `POSITION` | 300–500 in steps of 1, 500 wraps to 300; start-up 340 |\n'
     '| 4–5 | 2 | `PIXEL SHAPE` | 0 `ROUND`, 1 `SQUARE`, 2 `HIGHREZ`; start-up 0 |\n'
     '| 6–7 | 3 | `BRIGHTNESS` | 175–255 in steps of 5, 255 wraps to 175; start-up 255 |\n'
     '| 8–9 | 4 | width | 128 |\n'
     '| 10–11 | 5 | height | 32 |\n'
     '| 12–13 | 6 | `BAR BRIGHT` | 0–62 in steps of 2, 62 wraps to 0; start-up 62 |\n'
     '\n'
     '- **When it is sent.** The Propeller sends its own packet at start-up. Each Enter on one of the four items steps the value and makes the PIC32 send the seven words (little-endian, plus a flag byte: 0, or 0x80 when Back leaves the menu) as link command 0x11; the Propeller passes the words on to the module at once, without the flag. After the menu is left with Back, a restart\'s start-up packet carries the new `POSITION`, `BRIGHTNESS` and `BAR BRIGHT` (the Propeller keeps them in its EEPROM), with word 0 = 177 and `PIXEL SHAPE` = `ROUND`.\n'
     '- **Rendering** (`display.c`; the `sdl3pinmame` and VPinMAME window, 256×64; libpinmame gets the frame as sent). `SQUARE` fills each dot\'s 2×2 cell: the exact pixels of Milestone 6. `ROUND` draws each dot as the top-left pixel of its cell and lights the three gap pixels with the mean of the dots around them times `BAR BRIGHT`/124. `HIGHREZ` is Scale2x. `BRIGHTNESS` b scales every colour by b/255. `POSITION` p moves the picture down by ⌊(p − 340)/4⌋ pixels inside the window, black where it leaves. A packet of another length, or whose words 4–5 are not 128 and 32, keeps the exact look.\n'),
])
edit('docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md', [
    ('presumably the service-menu display settings (`PIXEL SHAPE` SQUARE/HIGHREZ, `BRIGHTNESS`, `POSITION`, `BAR BRIGHT`). Its encoding is not yet known.\n',
     'the service-menu display settings (`PIXEL SHAPE` ROUND/SQUARE/HIGHREZ, `BRIGHTNESS`, `POSITION`, `BAR BRIGHT`). The encoding is in the M8/M9 addendum §5.\n'),
    ('| Display look | exact pixels; the config packet is decoded, logged and kept but not applied | the module\'s rendering of the settings is unknown; applying it is a later follow-up |\n',
     '| Display look | exact pixels; the config packet is decoded, logged and kept but not applied | the module\'s rendering of the settings is unknown; applied since the display-look plan (M8/M9 addendum §5) |\n'),
    ('- Applying the config packet (display look): a later follow-up, once in-game packets per service-menu setting have been captured.\n',
     '- Applying the config packet (display look): done by the display-look plan (M8/M9 addendum §5).\n'),
])
edit('docs/superpowers/plans/2026-09-28-pinheck-roadmap.md', [
    ('| look | display look: decode and render the config packet (`PIXEL SHAPE`, `BRIGHTNESS`, `POSITION`, `BAR BRIGHT`) | Plans 6, 8 | none | packet decoding tests and golden images per setting; shape rendering labelled an approximation | after Plan 8 (parallel with 9) |\n',
     '| `2026-09-29-pinheck-display-look.md` | display look: config packet decoded (seven big-endian words) and rendered in the `CORE_VIDEO` window (`ROUND`/`SQUARE`/`HIGHREZ`, `BRIGHTNESS`, `POSITION`, `BAR BRIGHT`); libpinmame keeps the frame as sent | Plans 6, 8a | none | packet decoding and rendering unit tests; the look check: every value of every setting gives the predicted packet, golden images per setting, stored settings after a restart | written |\n'),
])
EOF
```

- [ ] **Step 2: Mutations**

Apply each mutation, run the named check, confirm the named failure line (other failure lines may follow), then restore the file with `git checkout <file>`. Unit-level (seconds each; `PINHECK_SKIP_FIRMWARE=1 tests/pinheck/display/check.sh`):

| File | Replace → with | Must show |
|---|---|---|
| `src/wpc/pinheck/display.c` | `word(cfg, 4) != DISPLAY_W` → `word(cfg, 4) != DISPLAY_H` | `DISPLAY FAIL display_test.c:177: pinheck_display_look(&lk, cfg_prop, 14) == 1` |
| `src/wpc/pinheck/display.c` | `look->brightness = word(cfg, 3) < 255 ? word(cfg, 3) : 255;` → `look->brightness = word(cfg, 3);` | `DISPLAY FAIL display_test.c:190: lk.shape == DISPLAY_SQUARE && lk.position == 0 && lk.brightness == 255 && lk.bar == 62` |
| `src/wpc/pinheck/display.c` | `c[k] * look->bar / (124 * m)` → `c[k] * look->bar / (128 * m)` | `DISPLAY FAIL display_test.c:216: is(20, 10, 255, 255, 255) && is(21, 10, 63, 63, 63) && …` |
| `src/wpc/pinheck/display.c` | `return v == h && h != v2 && v != h2 ? v : p;` → `return v == h && (h != v2 \|\| v != h2) ? v : p;` | `DISPLAY FAIL display_test.c:231: is(2, 0, 255, 255, 255) && is(3, 1, 255, 255, 255) && …` |
| `src/wpc/pinheck/display.c` | `uint8_t up = y > 0 ? f[(y - 1) * DISPLAY_W + x] : p,` → the same with `: 0,` | `LOOK FAIL: display.c and look.py differ in HIGHREZ brightness 255 position 340 bar 62 at pixel (15,0)` (the hand tests do not reach the top edge's clamp; the cross-check does) |
| `src/wpc/pinheck/display.c` | `dy = dy >= 0 ? dy / 4 : -((3 - dy) / 4);` → `dy = dy / 4;` | `DISPLAY FAIL display_test.c:259: is(0, 0, 255, 255, 255) && is(0, 1, 0, 0, 0) && is(0, 63, 0, 0, 0)` |
| `tests/pinheck/display/look.py` | `look['bar'] // (124 * m)` → `look['bar'] // (128 * m)` | `LOOK FAIL: display.c and look.py differ in ROUND brightness 255 position 340 bar 62 at pixel (1,0)` |
| `src/wpc/pinheck.c` | `#if !defined(LIBPINMAME) && PINHECK_VIDEO_SCALE == 2` → `#if PINHECK_VIDEO_SCALE == 2` | `LOOK FAIL: the look must be in the standalone renderer and not in libpinmame's` |

(Swapping the two inequalities of Scale2x's condition is not a usable mutation: with `v == h` both forms are the same test.)

The look check's own logic, on the run Task 2 Step 6 left in `tests/pinheck/display/build/look` (seconds each; `python3 tests/pinheck/display/look.py verify tests/pinheck/display/build/look`):

| File | Replace → with | Must show |
|---|---|---|
| `tests/pinheck/display/look.py` | `words = (250, st['position'], st['shape'], …` → `words = (250, st['shape'], st['position'], …` | `LOOK FAIL: config packet 1 is 00 fa 01 54 00 01 00 ff 00 80 00 20 00 3e, expected 00 fa 00 01 01 54 00 ff 00 80 00 20 00 3e` |
| `tests/pinheck/display/look.py` | `… < 256 else 175` → `… < 256 else 170` | `LOOK FAIL: config packet 4 is 00 fa 01 54 00 00 00 af 00 80 00 20 00 3e, expected 00 fa 01 54 00 00 00 aa 00 80 00 20 00 3e` |
| `tests/pinheck/display/look.py` | `dy = (look['position'] - 340) // 4` → `… // 2` | `LOOK FAIL: domi0005.png shows none of the last 8 frames in the look ROUND brightness 175 position 500 bar 62` |
| `tests/pinheck/display/look.py` | `SAVED = '00 b1 01 55 00 00 00 af …'` → `'00 b1 01 55 00 02 00 af …'` | `LOOK FAIL: after the restart the config packets are ['00 b1 01 55 00 00 00 af 00 80 00 20 00 00'], expected 00 b1 01 55 00 02 00 af 00 80 00 20 00 00` |

Machine-level (rebuild with `timeout 2700 cmake --build build/sdl3pinmame -j"$(nproc)"`, run the check, restore, rebuild):

| File | Replace → with | Check | Must show |
|---|---|---|---|
| `src/wpc/pinheck.c` | in `pinheck_disp_frame`, the line `disp_dirty = 1;` after `memcpy(disp_shown, frame, DISPLAY_FRAME);` → nothing (the window keeps the first frame it rendered) | `tests/pinheck/display/pinmame_display.sh` | `render: FAIL, the frame region shows none of the last 8 decoded frames`, exit 1 |

(Task 2 Step 2 already ran both machine checks without the look in the renderer: every non-`SQUARE` snapshot failed.)

Then `git status --short src tests` prints nothing and a final rebuild restores the tested binary.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-09-29-pinheck-m8-m9-design.md docs/superpowers/specs/2026-09-29-pinheck-m6-m7-display-audio-design.md docs/superpowers/plans/2026-09-28-pinheck-roadmap.md
git commit -m "Spec: the display config packet's encoding and the rendered look"
```

## Self-Review

**Spec coverage** (M8/M9 addendum §5):

| Requirement | Where |
|---|---|
| capture the config packet for each value of the four settings via `-key_script`, derive each field | Findings (firmware and a 254-packet sweep); Task 2's look check repeats the sweep (every value, 257 packets) against the derived encoding |
| render dot shape, brightness, offset in the `CORE_VIDEO` renderer | Task 1 `pinheck_display_render`, Task 2 `pinheck_video`; Rulings 3–8 |
| `BAR BRIGHT` established or ruled | Findings (the firmware only passes it on), Ruling 5 |
| unit tests for the packet decoding | Task 1 `look_decode` |
| golden images per setting | Task 1 hand-computed pixels per shape, brightness and position; Task 2's 10 launch-2 snapshots and the restart snapshot, against `look.py` (cross-checked with `display.c`) |
| `render.py` / `pinmame_display.sh` stay green | Task 2 (Ruling 12: judged in the start-up look) |
| exported frame contract kept, libpinmame unchanged | Global Constraints; Task 2's preprocessor check; no layout change |
| caveat in the plan and the spec | Rulings 3–8, Task 3's spec edit |

**Placeholder scan:** no TBD/TODO; every code step carries its full code or an anchored edit script; every Run has its Expected output from the proving replay.

**Type consistency:** `display_look`'s four `int` fields are set in the same order by `draw()` in `display_test.c`, `lookdump.c`'s `sscanf` and `look.py`'s dicts (`shape`, `brightness`, `position`, `bar`); `pinheck_display_look`/`pinheck_display_render` are declared in `display.h`, defined in `display.c`, used by `pinheck.c`; `look.py`'s `parse`/`render`/`name` are used by `render.py` under those names.

**Review Focus:** five items, each pinned by the named tests in Tasks 1–2.

**Proof:** every file and edit this plan writes was extracted from this document into a fresh worktree of `3d9971c3` and is byte-identical to the proven prototype. Every Expected output above was reproduced there: Task 1's failing and passing suite; Task 2's failing and passing unit suite, `pinmame_display.sh` and look check, the display suite with the firmware, and every regression suite of Step 7 (check 5 gave Plan 8a's figures exactly); every mutation of Task 3 at the named line. The passing look check gave identical output in the prototype and the replay; Task 2 Step 2's failing look-check output was recorded in the replay (the prototype never built that intermediate state), and its failing `pinmame_display.sh` output equals the machine mutation's, which the prototype also ran. The derivation sweep (254 packets) and the two restart probes of "Findings" ran on the prototype's build. Under the machine's load during the proof (other agents' emulations) the look check took 18–40 minutes and check 5 about 30.
