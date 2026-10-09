# NoteSorcery

**An OP-1-style, eight-track groovebox firmware for the M-VAVE FM-1.**
Free and open source (GPL-3.0-only). Built on [SLOOP](https://github.com/isod89/sloop-fm1) by isod89,
which is built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments.

> **Status: in development (0.1).** Milestone 1 is done: eight tracks, the new project format and the engine
> set's base. The rest of the roadmap is below. Install at your own risk and back up first: NoteSorcery does
> not read SLOOP's projects, and its sample slots are smaller (see [Differences from SLOOP](#differences-from-sloop)).

## What it is

- **Eight tracks:** six synth tracks (1–6) and two drum machines (7, 8). The synth tracks share 8 voices
  (FM6 at most 6 on one track). Each drum track has its own kit, voices, pattern, level, pan, filter and MIDI
  channel.
- **A step sequencer on the keys:** hold SEQ and the 16 white keys are the 16 steps of the page (black keys
  1–4 pick pages 1–4, 64 steps a track). There are also parameter locks, nudges, ratchets, chance by fill,
  chords, song mode and quick chains (from SLOOP).
- **Engines:** ANALOG (virtual analog, with an ACID 303 preset), TRIO (three-oscillator VA), FM6 (six-operator
  DX7-style FM, .syx import) and SAMPLE (multisamples: grand piano and GM kit built in, plus four slots of your
  own).
- **Drum kits:** SLOOP's 32 synthesised kits (808, 909, 606, house, techno…), the GM sample kit and your own
  kits (user sample slots, SYN1–SYN4).
- **USB-C:** class-compliant MIDI in/out with clock in, and USB audio from the FM-1 to your computer or phone
  (44.1 / 48 kHz).

### Roadmap

| Milestone | What | State |
|---|---|---|
| 1 | Eight tracks (6 synth + 2 drums), new project format (NSP1), legacy engines removed | **done** |
| 2 | ACID (Open303 + TB-3PO), WAVE (single-cycle), circuit-modelled 808 / 909, Machinedrum-style synth drums | planned |
| 3 | 8 × 16 visual sequencer screen, OP-1-style screens, themes (colours, dark / light, brightness), night mode | planned |
| 4 | MIDI clock **out** and Song Position Pointer for Ableton Live and other FM-1s; per-track MIDI out; NSX SysEx protocol | planned |
| 5 | Live sampling: the FM-1 as a USB audio output (record what a phone or computer plays), sample and SoundFont upload | planned |
| 6 | Ableton Live `.als` and `.mid` export from the browser editor, with no app needed | planned |
| 7 | Integration with the FieldTape and NoteMove Android apps | planned |

## Tracks and channels

| Track | Kind | MIDI channel (in / out) |
|---|---|---|
| 1–6 | synth | 1–6 |
| 7 | drums | GLO → DRUMS → CH (default 10) |
| 8 | drums | the next channel (default 11) |
| — | the selected track | any other channel |

MIDI CCs act on the track their channel plays: 7 level, 10 pan, 74 filter, 71 resonance, 73 / 75 / 72 attack /
decay / release, 5 glide, 91 / 93 / 94 the reverb, chorus and delay sends. A drum track takes 7, 10 and 74 for
itself, and 91 / 94 for the drum bus.

## The GLO (mix) layer

Hold GLO:

- **White keys 1–8:** mute tracks 1–8.
- **White keys 9–16:** solo tracks 1–8.
- **Black keys 1, 2, 3:** fill while held, fill on the next bar, tap tempo.
- **KNOB 1–4:** the levels of tracks 1–4, or of tracks 5–8 when one of those is selected.

## Differences from SLOOP

- **Eight tracks instead of four.** The project format is new (NSP1, 7.6 KB) and SLOOP's projects are not read.
  Back up with SLOOP's editor before installing.
- **Sections A–D are stored in flash**, two sectors a copy with A/B commits, and played from there. A section
  stored while playing waits in RAM until the transport stops. While one is waiting, storing another one says
  STOP TO STORE MORE.
- **User sample slots are 64 KiB** (about 5.9 s at 22 kHz) instead of 80 KiB. This made room for the larger
  projects.
- **The slicer's STUT records 93 ms a track** (a 1/16 down to 161 BPM) instead of 186 ms. A longer step loops
  its first part.
- **Engines removed:** DIGITAL, PHASE, LOFI, VOICE, WHEEL, GRAIN, PHYS and NOISE. Their sounds are not in the
  preset list. Only the grand piano and the GM kit stay built in; the other sample sets went.
- **The drum level is per track.** The drum tracks' LEVEL is their own, on the drum bus (GLO → DRUMS: LVL, REV,
  DLY for both).

## Building and tests

Build and test as in [BUILDING.md](BUILDING.md):

```
tools/get_toolchain.sh                        # the JieLi clang for pi32v2
# the AC79 SDK's three files: see BUILDING.md
sh build.sh                                   # build/felucca.fwsc, identity FM-1_9500
sh tests/run_tests.sh                         # every host test (needs the build)
```

- The host tests cover:
  - the storage layer (two-sector projects, torn writes, bit rot);
  - the project format;
  - the sequencer, UI and MIDI routing with eight tracks;
  - the DSP renders (golden hashes), CPU budgets, the installer and the web pages.
- Release builds: `sh build.sh --release 0.1` gives `notesorcery-0.1.fwsc` with identity `FM-1_9501`.
- **Memory:** about 357 KB of the 568 KB code area, RAM 87 of 96 KB, pool 326 of 336 KB.

## Credits

- **SLOOP** by isod89: the live groovebox this firmware grows from (GPL-3.0). Its original README is in
  [docs/SLOOP-README.md](docs/SLOOP-README.md), and its manual is [SLOOP.md](SLOOP.md).
- **Felucca** by Leo Kuroshita (@kurogedelic), Hügelton Instruments: the firmware under SLOOP (GPL-3.0), with
  its engines, sequencer, editor, installer, USB audio and FM6 port.
- **msfa** (Google, Pascal Gauthier / Dexed): the FM6 engine (Apache-2.0).
- **Samples:** Versilian Studios VSCO-2 CE and VCSL (CC0). **Font:** Terminus (SIL OFL 1.1).
- Interface ideas after teenage engineering's OP-1 and pocket operators, Elektron's step entry and Akai's MPC.
  NoteSorcery is not affiliated with any of them, nor with M-VAVE.

See [LICENSING.md](LICENSING.md) for the full licensing, including the Felucca Assets permission.

## Licence

GPL-3.0-only, no warranty. See [LICENSE](LICENSE).
