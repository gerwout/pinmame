# p8x32a core tests

`./tools.sh` builds the three external oracles at pinned commits into `build/tools` (needs `git`, `make`, `g++`, `verilator`, `patch`). `./check.sh` then runs everything; `SEEDS=N` changes the random corpus size (default 100 per oracle).

Oracles are test tools only and are never linked into PinMAME:

- **p1rtl**: Parallax's P8X32A RTL (GPL 3.0, github.com/parallaxinc/Propeller_1_Design) under Verilator, driven by `rtl/tb.cpp`. It is the arbiter: every `chip/`, `isa/` and random program must produce a byte-identical trace (`P` pin out/dir, `K` CLKSET, `S` cog start, `E` end, each at the cycle it first becomes visible) and a byte-identical 32 KB hub RAM dump.
- **spinsim** (MIT, patched by `spinsim-dump.patch` to dump a hub window at exit): instruction semantics only, compared on the `$6000-$63FF` result window. It is not cycle-accurate and does not model the hub-op flag quirks, so its random corpus omits flags on hub ops.
- **openspin** (MIT) assembles the test programs.

Test programs are Spin files whose PASM sits between the marker longs `$C0DE5EED` and `$C0DEE0D0`. `mkrom.py` scrambles that block into a ROM image at `$F800`, where cog 0 loads from at reset, so RTL tests run without the Parallax mask ROM. A block after `$C0DE0B0B` is a worker whose hub address replaces the placeholder `$C0DEADD1`. `mkrom.py --launch` instead builds a launcher that starts the block in cog 1, which is how spinsim's `cognew` runs it. A first line `' ARGS: ...` adds runner arguments (for example `-extat cycle hex` external pin changes); `' EXPECT-LOG: ...` requires a core log line.

The boot test needs the mask ROM (GPL 3.0, never committed) and the game's Propeller image: set `P8X32A_ROM` to the 32 KB silicon image (crc32 `f99b3070`) and `DOMINOS_PRP` to `PRP_V008.BIN`. It boots through the real booter and EEPROM model with P31 low and with P31 high (host-detect timeout path) until cog 0 is restarted with the Spin interpreter, and compares against a cached RTL run (about two minutes each the first time).
