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
   - shows the colour display (libpinmame's 128×32 frame, as sent);
   - shows the 64 lamps, GI and RGB outputs as labelled indicators;
   - maps the switches to keys;
   - shows coil and servo activity;
   - plays sound.

   The proof is scripted where VPX standalone can run headless, otherwise a written manual checklist.
4. **Windows VPX with VPinMAME:**
   - The Windows cmake builds of VPinMAME must contain every pinHeck source.
   - VPinMAME's display window presents the 256×64 `CORE_VIDEO` layout (2×2 dots, in the display look) crisply, as the standalone window does (established on Windows: `DmdWidth`/`DmdHeight` 256×64; `RawDmdWidth`/`RawDmdHeight` are −1 for every `CORE_VIDEO` game, Baby Pac-Man included). No change is needed.
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

## 8. Findings of Milestone 10

- **Precondition met.** `pinheck` at `29692743` runs `dominos` in real time on the reference machine: 1.28× in attract mode and 1.08× with video (Plans 9b and 9c).
- **VPX version.** libpinmame names its plugin messages with a version (`GetStateSrc:1`, `GetDisplays:1`, upstream since 2026-09-05). The PinMAME plugin of the VPX 10.8.1-5436 release (2026-08-21) asks for the unversioned names, so with this libpinmame it gets no lamps, solenoids, switches or display. VPX builds from after 2026-09-05 (tested: 10.8.1-5947 `4dc8d8afb`) work. On Windows, VPX with VPinMAME (COM) does not use these messages.
- **Flipper buttons.** PinMAME copies its flipper column to cabinet switches 4 (left) and 3 (right) every frame, so a table presses switch 114 (lower left) and 112 (lower right); `pinheck_names.h` names them `swLLFlip` and `swLRFlip`.
- **Numbers that name nothing.** A switch number outside the matrix goes to an unused switch; a lamp number outside the matrix reads 0.
- **First launch.** On an empty NVRAM the firmware updates itself and asks for two restarts: the PIC32 is reflashed and the display shows `PLEASE RESTART`; after the restart it shows `System has been updated / Please restart your machine`; after the second restart the game runs. With the test hook `PINHECK_INSERVICE=6` the first launch skips the reflash and shows `System has been updated`, so one restart is enough.
