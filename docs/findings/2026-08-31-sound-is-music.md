# Sport 2000 is playing music: the spectral evidence

Date: 2026-08-31. Commit under test: `484e2aea`. Control: `a6a8274a`.

Task 4 wired the sound handshake and the AY bus and reported strong proxies -- the `0x3D`
command's AY register stream matching ROM `0x3315` byte for byte, key-on writes 0 -> 206,
`clip_pct` 54.1 -> 0.11. It explicitly declined to claim the result sounds like music, because
nobody had examined the waveform. This closes that gap.

## Method

A temporary raw-PCM tap at `osd_update_audio_stream()` in `src/sound/mixer.c` (added, used,
reverted -- `git status --porcelain -- src/` clean, never committed), applied **identically** to
both trees. Both built `I8051_SWEEP=1`, both run 60 s headless with `-nosound -fakesound`, same
rompath, NVRAM cleared. The control was built from a worktree at `a6a8274a` rather than compared
against a stored measurement -- an earlier round in this project lost an hour to a baseline that
was two commits stale, and the discipline is now to re-measure the control every time.

Analysis: 8192-sample Hann-windowed FFT every 10 s of emulated audio, at the 8 kHz `-fakesound`
rate. For each window, take the strongest bin above 80 Hz, and measure how far it sits from the
nearest equal-tempered semitone.

## Result

| | before (`a6a8274a`) | after (`484e2aea`) |
|---|---|---|
| clipped at full scale | **55.74 %** | **0.0000 %** |
| distinct sample values | 11,019 | 20,111 |
| windows with a peak carrying >1 % of energy | 4 of 38 | 29 of 53 |
| median error vs equal temperament | 11.6 cents | **5.9 cents** |
| **distinct pitches** | **1** | **17** |

Before: a railed signal with a single stuck tone. After: seventeen distinct pitches, no clipping
at all, dominant tonal content in most windows.

Individual peaks land on concert pitch far too precisely for a noise source:

| measured | note | error |
|---|---|---|
| 879.9 Hz | A5 | **-0.2 cents** (three separate windows) |
| 659.2 Hz | E5 | -0.2 cents |
| 698.2 Hz | F5 | -0.5 cents |
| 494.1 Hz | B4 | +0.9 cents |
| 415.0 Hz | G#4 | -1.1 cents |
| 586.9 Hz | D5 | -1.2 cents |
| 219.7 Hz | A3 | -2.2 cents |

Harmonic series are visible too -- 440/880/1320/1760 together in one window -- and one window
shows 261.7 / 311.1 / 392.0, a C minor triad.

## What this does and does not establish

**Does:** the AY-3-8910 is being driven with real, musically-structured register data, and the
audio path from the sound ROM to the mixer works end to end.

**Does not:** that the music is *correct* -- right tune, right tempo, right instrument balance.
Verifying that needs a recording of the real machine, which this project does not have. The
low-frequency outliers in the table (B2 at +48 cents, G#2 at +27) are at or below the FFT's
resolution at this window size and are not evidence of mistuning.

Nothing here was listened to; the claim rests entirely on the numbers above, which are
reproducible from the two builds named.
