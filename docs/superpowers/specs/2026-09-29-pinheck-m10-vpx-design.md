# pinHeck / Domino's: VPX support (Milestone 10) — design addendum

Status: approved design, pre-implementation
Date: 2026-09-29
Parent spec: `2026-09-28-pinheck-dominos-design.md` (binding where this addendum is silent). Siblings: `2026-09-29-pinheck-m6-m7-display-audio-design.md`, `2026-09-29-pinheck-m8-m9-design.md`.

## 1. Goal

Run `dominos` in Visual Pinball X, both as VPX standalone on Linux (PinMAME through libpinmame and the PinMAME plugin) and as Windows VPX with VPinMAME (COM). The colour display, sound and every lamp, coil, switch, GI, RGB and servo line must reach the table.

The deliverable is a minimal test table that proves the integration. A table author can build on it later.

## 2. Precondition

Milestone 9: the emulation runs at real time on the reference machine. VPX drives PinMAME in real time.

## 3. What already exists

- **libpinmame display export:** `CORE_VIDEO` displays are exported as colour frames of depth 24 (r, g, b bytes) or 16. Palette-based bitmaps are resolved through the palette, which covers the RGB332 palette of Milestone 6 (`src/libpinmame/libpinmame.cpp`).
- **VPinMAME frame copy:** `core.c` keeps a `CORE_VIDEO` frame copy as RGB8, the path Baby Pac-Man's screen takes.
- **Audio:** libpinmame and VPinMAME take audio from PinMAME's stream, which Milestone 7 provides.
- **Outputs:** lamps, solenoids, GI and modulated outputs go through the standard interfaces. RGB and servos are custom solenoids 51–61 and above (parent spec §4.5, addendum M8 §3.1).

## 4. Work

1. **Export proof through libpinmame** (headless, on this machine, via `src/libpinmame/test.cpp` or a small dedicated test program):
   - display frames equal Milestone 6's decoded frames (RGB332 → RGB24);
   - the audio callback delivers Milestone 7's stream;
   - lamps, solenoids, GI and modulated outputs equal Milestone 8's board outputs during the scripted game.
2. **Names:** `src/wpc/pinheck_names.h`, following `src/wpc/p2k_names.h`. It covers switch, lamp, coil, GI, RGB and servo names and numbers from `docs/Dominos-Switch-Matrix.pdf`, `Dominos-Lamp-Matrix.pdf` and `Spooky_Pinball_Domino's_Solenoid_List.pdf`, for VPinMAME name lookups and table scripts.
3. **VPX standalone on Linux:** a minimal test table loads `dominos` through the PinMAME plugin and:
   - shows the 128×32 colour display;
   - shows the 64 lamps, GI and RGB outputs as labelled indicators;
   - maps the switches to keys;
   - shows coil and servo activity;
   - plays sound.

   The proof is scripted where VPX standalone can run headless, otherwise a written manual checklist.
4. **Windows VPX with VPinMAME:**
   - The Windows cmake builds of VPinMAME must contain every pinHeck source.
   - It is established how VPinMAME's display window presents a 128×32 `CORE_VIDEO` display, with Baby Pac-Man as the precedent. Any change needed goes into the driver or core.
   - The same test table and a manual checklist are for the user to run on Windows, because no Windows machine is available to this work.
5. **Where the table lives:** the test table (`.vpx`, a binary authoring file) and its script live outside the PinMAME repository, in `/code/spooky_domino/vpx/`. Whether any of it ships is part of the release decision.

## 5. Failure behaviour

- **Missing export:** if an export path does not carry a `dominos` output, that is a defect in the driver or core, fixed with a test. It is never worked around in the table script.
- **Windows-only checks:** anything that can only be checked on Windows is listed in the manual checklist and reported as unverified until the user has run it.

## 6. Out of scope

- A full playable Domino's playfield recreation (3D and art).
- Colourisation and Serum.
- FlexDMD and B2S backglass art.

## 7. Order

After Milestone 9, and before or together with the release phase.
