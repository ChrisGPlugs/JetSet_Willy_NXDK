# Jet Set Willy for the original Xbox (nxdk)

Native ports of Matthew Smith's **Jet Set Willy** (ZX Spectrum 48K, Software
Projects, 1984) and of John Elliott's **Jet Set Willy 128** (v0.04 Hacklevel 9)
to the original Xbox, built with [nxdk](https://github.com/XboxDev/nxdk). Three
XBEs, all with title ID **4A535758** ("JSWX"), so they share one save folder:

| Build | XBE | Data file(s) next to it |
|---|---|---|
| `make -C JetSetWillyX` | `JetSetWillyX/bin/default.xbe` "JetSetWillyX": both games | `JetSetWilly.tzx` and/or `JetSetWilly128.tzx` |
| `make` | `bin/default.xbe` "Jet Set Willy" | `JetSetWilly.tzx` |
| `make -C jsw128` | `jsw128/bin/default.xbe` "Jet Set Willy 128" | `JetSetWilly128.tzx` (the HL9 tape) |

## JetSetWillyX

With both tapes present, JetSetWillyX starts on a chooser: the 48K game's
own title screen (picture, theme tune, scrolling message), with a menu
underneath. D-pad (or stick) chooses, A or Start plays. **Back pressed twice
within 1.5 s on either game's title screen** returns to the chooser. With one
tape it runs that game directly (no chooser); with neither it says what's
missing. Holding X or Y at start-up still selects the original 1984 release
for the 48K game.

The game logic is the original Z80 code, translated to C instruction by
instruction from Richard Dymond's annotated
[SkoolKit disassembly](https://github.com/skoolkid/jetsetwilly), so it plays,
sounds and runs at the speed of a real 48K Spectrum. No game data is included:
you supply your own copy of the original tape.

## Playing

1. Build (below), or take `default.xbe` from a release.
2. Put your copy of the original tape next to `default.xbe`, named
   **`JetSetWilly.tzx`** (`JetSetWilly.tap` or a 32K dump of 32768–65535 as
   `JetSetWilly.bin` also work). Optionally add a 16K Spectrum ROM as `48.rom`
   for the original character set; otherwise a public-domain font is used.
3. Start it from your dashboard.

| Pad | Action |
|---|---|
| D-pad / left stick | Walk left / right |
| A or B | Jump |
| Start | Start the game (title screen); pause (in play) |
| X | Pause (any other button resumes) |
| Y | In-game tune on / off |
| Back | Quit to the title screen |
| White / Black | Type the WRITETYPER cheat (works where the original's does: First Landing, at the foot of the stairs). With the cheat on: teleport to the previous / next room |
| Left-stick click | Diagnostics (speed, audio, ULA contention) |
| LT + RT + Back + Start (or Black) | Return to the dashboard |

### Bug fixes and the code card

By default the port applies the four **official Software Projects bug-fix
POKEs** (including the Attic bug, which made the game impossible to finish)
and skips the colour-code card.

**Hold X or Y while the game starts** for the untouched 1984 release: original
bugs and the code card. On the code screen, X, B, Y and A are the colour keys
1–4 (blue, red, magenta, green) and Start is ENTER. You'll need the code card;
two wrong codes reset the game, as on the Spectrum.

## Jet Set Willy 128 (HL9)

JSW128 runs on a 128K Spectrum: 130 rooms kept in four 16K banks, AY music
played by Ian Collier's TUNE.BIN from IM2 interrupts, a new font and title
screen, extra guardian types, the in-game teleporter and an extended
cheat mode. Put the HL9 tape next to the XBE as `JetSetWilly128.tzx` (the
original file name, `Jet Set Willy 128k v. 0.04 HL 9.tzx`, also works).

Same pad layout as the 48K game, except: there is no code card and no
original/fixed choice, pause is X or Start (JSW128 pauses on A only), and
with the cheat on White/Black teleport through rooms 0–129. JSW128's own
cheat-mode keys are on the keyboard (desktop build) as on the Spectrum.

How it's translated: there is no full disassembly of HL9, so the code map
for `src/jsw128_code.c` comes from

- a trace of every instruction the original executes on the reference Z80
  core in long scripted runs (`tools/z80c/maps/jsw128_hl9_trace.txt`),
- every 48K code address whose bytes are unchanged in HL9,
- the 16-entry guardian-type jump table at 34002,
- and static descent from all of those (untaken branches, call targets).

The 128K machine (paging by copying 16K banks in and out of 0xC000,
contention of the odd banks, the AY ports, the /INT window of 36 T-states)
is in `src/zx.c`; interrupts are taken at instruction boundaries in the
generated code, with the EI delay, HALT, and IM 1/IM 2 timing. Comments for
the unchanged code come from the 48K disassembly; code new in JSW128 is
marked `(JSW128)`. If the game ever reaches code outside the map, the XBE
stops with the address on screen (please report it).

## Building

```sh
export NXDK_DIR=/path/to/nxdk
make -C JetSetWillyX         # JetSetWillyX/bin/default.xbe (+ JetSetWillyX.iso)
make                         # bin/default.xbe (+ JetSetWilly.iso)
make -C jsw128               # jsw128/bin/default.xbe (+ JetSetWilly128.iso)
make -f Makefile.host        # desktop builds (SDL2): ./jetsetwilly, ./jetsetwilly128, ./jetsetwillyx
make -f Makefile.host test   # compare both with the original Z80 code
```

The title ID (`4A535758`, "JSWX") is patched in after linking by
`tools/xbe_titleid.sh`, because nxdk's `cxbe` always writes FFFF0002. Each
XBE has its own folder with small wrapper `.c` files, because nxdk writes
object files next to their sources.

If `JetSetWilly.tzx` is in this folder or in `Data files/` when you run
`make`, it is copied next to the XBE and into the ISO. **Don't share ISOs or
zips that contain the game data.**

## How it works

- `tools/z80c/z80c.py` reads the disassembly's code map, labels and comments,
  decodes the real bytes of the game at every code address, and writes
  `src/jsw_code.c`: one C function in which each Z80 instruction is a few
  lines of C, with the original address, mnemonic and comment beside it. The
  decoder's mnemonics are checked against the disassembly's for all 2,041
  instructions.
  - Registers are C variables and flags are computed exactly. The Z80 stack
    lives in RAM, so the game's stack tricks work unchanged: BEGIN RETs into
    an address it poked, and KILLWILLY and the room-exit routines POP return
    addresses.
  - Each instruction charges its T-states one machine cycle at a time, with
    48K **ULA contention** at every memory and I/O access. JSW's screen and
    attribute buffers (and its stack) are in contended RAM, which makes the
    real game about 20% slower than an uncontended Z80.
  - Immediate operands are read from RAM, so the game's self-modifying code
    (36154/36166) and operand POKEs (the code-card bypass) work.
- `make -f Makefile.gen SKOOL=.../jsw.skool IMAGE=jsw48.bin` regenerates
  `src/jsw_code.c` (the image is the tape's 32K code block).
- `src/zx.c` is the Spectrum machine (memory, clock, contention, keyboard,
  Kempston, beeper, screen) shared with the
  [Manic Miner port](https://github.com/ChrisGPlugs/ManicMiner-nxdk). So are
  the Xbox front-end pieces in `src/main.c`, proven on hardware: the
  framebuffer drawn directly at 2x, audio fed straight to the AC97 and used as
  the clock, the in-game reset and the diagnostics overlay.

## Verification

`tools/difftest.c` runs the original code on a reference Z80 core
(`tools/z80ref`, superzazu/z80) and the translation side by side, with the
same scripted input, and compares a hash of all RAM, plus the T-states taken,
at every pass through the main loop. Contention is off for this comparison,
because the reference core lacks it. `tools/run_difftest.sh` runs:

- `idle`: start a game and stand still;
- `play:1`–`play:5`: 20,000 passes each of random play, teleporting to every
  room, with hundreds of deaths, game overs and restarts;
- `play:9` with the original bugs (no POKEs);
- `finale`: all items collected, Maria leaves, Willy goes to bed and runs to
  the toilet;
- `codes`: the code card, two wrong codes, machine reset.

All are identical to the original, RAM and timing.

For JSW128, `tools/difftest128.c` / `tools/run_difftest128.sh` do the same on
a 128K machine, hashing every bank and also the sequence of AY register
writes: the title screen (40 s of attract animation and tune), standing
still, the ending, random play with teleports through all 256 room numbers
(77–81 rooms visited per run), random play without the cheat (deaths, game
overs), and random presses of every key with cheat mode on, 20,000
main-loop passes each. All identical. The test build also checks, before
every instruction, that the opcode in RAM is the one that was translated.

Stopwatch for JSW128 (contention on): one in-game minute is 22.8 s, so
7:00am to 7:05am standing still takes about 1 min 54 s.

Stopwatch check on hardware: with contention, one in-game minute is 256
main-loop passes, or **23.0 s**. Start a game and stand still: the clock
should go from 7:00am to 7:05am in about 1 min 55 s.

## Licence

- Port code: MIT (see LICENSE).
- `src/jsw_code.c` and `src/jsw128_code.c` are generated from Richard Dymond's
  [Jet Set Willy disassembly](https://github.com/skoolkid/jetsetwilly) and
  carry its labels and comments, so those files are under the disassembly's
  licence for annotations, **CC BY-SA 3.0**, with attribution in their headers.
- Jet Set Willy is © 1984 Matthew Smith / Software Projects; Jet Set Willy
  128 is John Elliott's. The game data is not included: never commit it, or
  share ISOs built with it.
