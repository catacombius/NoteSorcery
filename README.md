# NoteSorcery

**An OP-1-style, eight-track groovebox firmware for the M-VAVE FM-1.**
Free and open source (GPL-3.0-only). Built on [SLOOP](https://github.com/isod89/sloop-fm1) by isod89,
which is built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments.

> **Status: in development (0.1).** All seven milestones are done: eight tracks, the new project format, the
> ACID / WAVE engines, the circuit and Machinedrum-style drum kits, the visual sequencer, themes and night mode,
> MIDI clock out and Song Position, live sampling over USB, Ableton export from the browser, and the FieldTape /
> NoteMove integration. They have been tested on the host, not yet on a device (see the roadmap below). Install at
> your own risk and back up first: NoteSorcery does
> not read SLOOP's projects, and its sample slots are smaller (see [Differences from SLOOP](#differences-from-sloop)).

## Install

Open **<https://catacombius.github.io/NoteSorcery/>** in **Chrome or Edge** on a computer, plug the FM-1 in by USB (a data
cable, no hub), press **INSTALL**, allow MIDI access and wait for *Done*. The editor (Ableton export, samples,
SoundFonts, backups) is linked from the same page. Coming from SLOOP, save a backup in SLOOP's editor first.
Other ways (a local copy of the page, the command line) and rescue: [GUIDE.md](GUIDE.md#1-install-and-update).

## What it is

- **Eight tracks:** six synth tracks (1–6) and two drum machines (7, 8). The synth tracks share 8 voices
  (FM6 at most 6 on one track). Each drum track has its own kit, voices, pattern, level, pan, filter and MIDI
  channel.
- **A visual sequencer:** press SEQ twice for PATTERN. It shows every track's 16 steps of the page at once,
  each in its track colour, with each track's playhead (tracks of other lengths run on their own), muted tracks
  dimmed and the selected one marked.
- **Themes, brightness and night mode** (hold HOME → SCREEN):
  - **COLOR:** the SLOOP colours plus OP-1, PAPER (dark on light) and CONTRAST.
  - **BRIGHT:** 100 / 70 / 45 / 25 %. The screen's backlight is on/off only, so every colour is scaled.
  - **NIGHT:** every button and key lights up low, the sounding notes light their keys and the screen dims.
    Turn it off and the lights come back as they were.
- **A step sequencer on the keys:** hold SEQ and the 16 white keys are the 16 steps of the page (black keys
  1–4 pick pages 1–4, 64 steps a track). There are also parameter locks, nudges, ratchets, chance by fill,
  chords, song mode and quick chains (from SLOOP).
- **Engines:** ANALOG (virtual analog), TRIO (three-oscillator VA), FM6 (six-operator DX7-style FM, .syx
  import), SAMPLE (multisamples: grand piano and GM kit built in, plus four slots of your own), **ACID** (a
  TB-303: X0X's Open303 port, accent from velocity, slide from the sequencer; TOOLS → GEN writes a TB-3PO line)
  and **WAVE** (single-cycle waves: 24 built in, band-limited, A/B morph, 15 slots of your own).
- **Drum kits:** **808 CM** and **909 CM** (X0X's circuit-modelled TR-808 and TR-909; one drum track at a time
  plays the circuit, the other gets the synthesised machine), three **Machinedrum-style** kits (MD EFM: 2-op FM
  drums, MD TRX, MD SRR; in the manner of Elektron's machines, not affiliated), SLOOP's synthesised kits (808,
  909, 606, house, techno…), the GM sample kit and your own kits (user sample slots, SYN1–SYN4). Power-on: track
  7 on 808 CM, track 8 on MD EFM.
- **Ableton Live, no app needed:** the browser editor's PROJECTS page exports the working project and song
  sections A–D as a Live Set (`.als`, Live 11 / 12: eight MIDI tracks, the sections as scenes and in the
  arrangement) or a MIDI file, from the FM-1 or from a saved backup file. The notes are what the FM-1 plays:
  swing, nudges, ties, slides, ratchets and dynamics included.
- **Live sampling over USB:** with AUDIO → USB SAMPLE on, the FM-1 is also a USB audio *output*. Whatever a phone
  or computer plays into it is recorded into a sample slot (USR1–4), starting at the first sound, as on an OP-1.
  See [GUIDE.md: Live sampling over USB](GUIDE.md#live-sampling-over-usb). Recordings and SoundFont presets
  (`.sf2`) can also be sent from the browser editor or from FieldTape.
- **USB-C:** class-compliant MIDI in/out and USB audio from the FM-1 to your computer or phone (44.1 / 48 kHz).
  - **Clock in:** the FM-1 follows clock, Start/Stop and Song Position (USB or TRS).
  - **Clock out:** GLO → SYSTEM → MIDI = SEQ+CLK (or KEYS+CLK) makes the FM-1 the master of Ableton Live or of
    other FM-1s on a host.
  - **SysEx:** the NSX protocol for apps, in [docs/NSX_PROTOCOL.md](docs/NSX_PROTOCOL.md).
- **Android apps:** FieldTape and NoteMove read the FM-1's song (its sections as scenes), send it their clock and
  (FieldTape) push samples and SoundFont presets to its slots. They share
  [`android/fm1link`](android/fm1link), a Kotlin library.

### Roadmap

| Milestone | What | State |
|---|---|---|
| 1 | Eight tracks (6 synth + 2 drums), new project format (NSP1), legacy engines removed | **done** |
| 2 | ACID (Open303 + TB-3PO), WAVE (single-cycle), circuit-modelled 808 / 909, Machinedrum-style synth drums | **done** |
| 3 | 8 × 16 visual sequencer screen, OP-1-style screens, themes (colours, dark / light, brightness), night mode | **done** |
| 4 | MIDI clock **out** and Song Position Pointer for Ableton Live and other FM-1s; per-track MIDI out; NSX SysEx protocol | **done** |
| 5 | Live sampling: the FM-1 as a USB audio output (record what a phone or computer plays), sample and SoundFont upload | **done** |
| 6 | Ableton Live `.als` and `.mid` export from the browser editor, with no app needed | **done** |
| 7 | Integration with the FieldTape and NoteMove Android apps (`android/fm1link`) | **done** |

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
