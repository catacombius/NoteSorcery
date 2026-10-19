# BOTANICA

**A botanica-music firmware for the M-VAVE FM-1.** Felt piano, droplets, foley, bloom and glitch, with
four colour-coded knobs.

Botanica (sometimes called "petalcore") puts nostalgic acoustic sounds next to tiny, organic digital textures:
felt piano, strings, plucks, whispers, crickets and rain, then granular droplets, reversed swells, micro-glitches,
tape wear and sudden tempo drops. The producer dowfi describes it as *"piano, strings and bass, then textures,
glitches and bits of foley"*. BOTANICA is built around that order, and every sound, knob and page serves it.

BOTANICA is an edition of [NoteSorcery](README.md). It runs on the same platform (eight tracks, the sequencer,
the browser installer and editor, the Ableton export) with its own engines, presets, kits, theme and stores. It is
free and open source (GPL-3.0-only), and is built on [SLOOP](https://github.com/isod89/sloop-fm1) and
[Felucca](https://github.com/hugelton/Felucca).

> **Status: 0.1, tested on the host only.** Every feature here passes the host test suite
> (`tests/run_tests_botanica.sh`), but none has run on an FM-1 yet. Install it at your own risk and back up first.
> BOTANICA does not read NoteSorcery's or SLOOP's projects, and they do not read BOTANICA's.

## Install

1. Open the BOTANICA installer, `/botanica/` under the NoteSorcery site, in **Chrome or Edge**. The NoteSorcery
   installer links to it as the *alternate release*.
2. Plug the FM-1 in by USB with a data cable (no hub).
3. Press **INSTALL**, allow MIDI access and wait for *Done*.

Going back to NoteSorcery or to the official V15 works the same way, from their pages. If an update is ever
interrupted, the FM-1's ROM bootloader always brings it back. See [GUIDE.md](GUIDE.md#1-install-and-update) for
the rescue steps, which are the same for every edition.

From source:

    bash build.sh --edition botanica                 # build/botanica/felucca.fwsc (identity FM-1_9600)
    bash build.sh --edition botanica --release 0.1   # build/botanica/botanica-0.1.fwsc (FM-1_9601)
    sh tests/run_tests_botanica.sh                   # its host tests, renders and demos

## The first minute

Power it on and you are in the **GARDEN**. Each of the eight tracks is a plant:

- Its stem grows as the track gets louder, and its flower opens on every note.
- A muted track wilts.
- The sky warms from night blue to dawn pink as the tempo rises.
- Rain falls with WEATHER, flowers glow with BLOOM, and frost covers the sky when the reverb is frozen.
- Droplets and foley hits ripple the pond.

Tap HOME to leave the GARDEN and reach the TRACKS screen. Tap it again to come back. SELECT steps through the
other pictures (the scope, spectrum, tape and so on).

The six synth tracks power on as dowfi's layers. Play the keys on any of them:

| Track | Sound | Engine | Layer |
|---|---|---|---|
| 1 | NURTURE KEYS | FELT | felt piano with a little string bloom |
| 2 | KALIMBA | DROPLET | plucks that drop echoes |
| 3 | SOFT SUB | ANALOG | the warm root |
| 4 | CANOPY PAD | ANALOG | the pad |
| 5 | LEAF CRUNCH | NOISE (FOLEY) | foley |
| 6 | AIR CHOIR | VOICE | breath |
| 7, 8 | TWIG, FIELD | drum kits | wood and felt; leaves, drops, rain and crickets |

## Four colours, four knobs

As on the OP-1, a colour always means the same knob: **pink is KNOB 1, green is KNOB 2, blue is KNOB 3 and
yellow is KNOB 4**. Tap GLO and the first two pages are BOTANICA's performance mode, four big macros each.

**PERFORM**

| Knob | Macro | What it does |
|---|---|---|
| pink | **BLOOM** | Warmth and swell. FELT gets rounder with more strings, the mix is washed into the reverb, and the reverb grows. GLO → MASTER → BMODE picks what it does to the reverb: **SWELL** (the reverb comes in *after* each note, the reversed-reverb rise of botanica), **FREEZE** (past half the knob the reverb holds what is in it, a frozen pad) or **SMEAR** (the longest, darkest reverb and a deep chorus). |
| green | **DROPS** | More echoes on every DROPLET track, more often jumping up an octave, a twelfth or two octaves. |
| blue | **FOLEY** | Hits on every track that plays NOISE's FOLEY mode, scattered on the sixteenths: up to half of them at the top. |
| yellow | **GLTCH** | On the sixteenths, now and then: a stutter, a short loop, a reversed beat, a tape stop or a gate for a step or two, plus a random ratchet on the synth steps. It never plays over a punch-in effect your finger holds. |

**SKY**

| Knob | Macro | What it does |
|---|---|---|
| pink | **WTHR** (weather) | First tape wow and flutter on the synth tracks (up to 22 cents), then the record's hiss and dust, then the old sampler's grit. |
| green | **BREEZE** | A slow random walk on every track's pitch (within ±6 cents), tone and delay time, so no loop plays twice exactly alike. |
| blue | **DRIFT** | A tempo to drift to (0 = off). Turn it and, from the next bar, the tempo moves there in a straight line over RAMP bars (GLO → MASTER → RAMP; 0 jumps on the next bar). Try 60 → 160 over 8 bars, or the half-time 80 ↔ 160. It works on the FM-1's own clock only, because an external MIDI clock leads otherwise. |
| yellow | **FILT** | The DJ filter: low-pass to the left, high-pass to the right. |

## Moments: hold GLO and press a black key

| Keys | What happens |
|---|---|
| GLO + **C#4** held | **BREAK.** Every track except the selected one goes quiet and the reverb freezes. Play over the frozen pad while the grid keeps counting. Let go and the **DROP** lands on the next bar, at the DRIFT tempo if one is set. |
| GLO + **D#4** held | **FREEZE** the reverb while held. |
| GLO + **F#4** held | **GLITCH** all the way while held. |

The other GLO keys work as in NoteSorcery: white keys 1–8 mute, 9–16 solo, F#3 fill, G#3 fill the next bar,
A#3 tap tempo.

## Finding sounds by mood

The PRESETS knob, or the PRESETS page, browses every factory sound by mood instead of by instrument:

| Mood | Tag | Sounds |
|---|---|---|
| PETALS | PETL | felt pianos, kalimba, marimba, music box, harps, guitars, koto, chimes |
| CANOPY | CANP | string bloom, pads, glass and hollow tones, piano clouds, drones, bowed bowls |
| DEW | DEW | glass drops, rain arps, chirps, bubbles, sparkle, shimmer, glitter |
| VOICES | VOIC | air choir, whisper pad, ooh, angel ah, vox chop, hum |
| ROOTS | ROOT | soft sub, fretless, warm root |
| WEATHER | WTHR | twig, leaves, water drop, brush, breath, key click, crackle, crickets, wind, rain, ocean, vinyl |
| HYPER | HYPR | hyper lead, supersaw, petal pluck, glide lead, flute |

Your own user presets follow the factory list.

## Petal chords and FEEL

On the SEL pages, CHORD has BOTANICA's chords. Press one white key for a spread, four-voice chord:

- **MAJ7**: 1 3 5 7
- **MAJ9**: 1 3 7 9, no 5th
- **ADD9**: 1 3 5 9
- **6/9**: 1 3 6 9
- **LYD#11**: 1 3 7 #11

The chord modes are also two progressions, where *each press plays the next chord* in the track's key, placed
near the key you press:

- **ROYAL**: IVmaj7, V7, iii7, vi7 (the Royal Road progression).
- **PETAL**: IVmaj7, Vadd9, vi9, Iadd9. It "almost sounds heartbreaking at the minor chord, but then resolves".

**SEL 2** strums them: STRUM is 0–80 ms per note, and ORDER is UP from the lowest note, DOWN from the highest,
or RAND, a new order each chord.

The **FEEL** page has two more controls:

- **HUMAN** moves each note's velocity (up to ±12 %) and timing (up to 10 ms late), like a hand on felt keys.
- **RATCH** shapes the ratchets you set on steps:
  - **RAMP** climbs up to an octave.
  - **BOUNCE** lands at 1/2, 3/4 and 7/8 of the step, each softer, like a bouncing drop.
  - **RAND** plays each hit an octave up or as another note of the step.

## The engines

| Engine | What it is | Knobs (HOME) |
|---|---|---|
| **FELT** | A felt piano: four fixed-point FM operators (bright on the strike, round in the tail), a felt hammer knock, key-scaled dampers, and a string layer that swells in while you hold the key. TYPE: piano, e-piano tine, music box, hollow. | TONE, HAMR, BLM, DCAY |
| **DROPLET** | Modal plucks (kalimba, marimba, music box comb, glass, harp: three partials each), or a chirped 2-operator FM drop. Each note drops up to three echoes; each echo can jump +12, +19 or +24 and is detuned a little. Echoes are notes, not audio, and never take a voice from another track. | DROPS, JUMP, SPRD, DECAY |
| **ANALOG** | Virtual analog: warm subs and fretless glides, pads, the hyperpop leads. | CUT, RES, ATK, REL |
| **SAMPLE** | The CC0 grand piano, and your own multisamples (four slots, from the editor or USB sampling). | |
| **NOISE** | Noise beds (wind, rain, ocean, vinyl), and **FOLEY**: bursts of filtered noise and resonant pings. KIND is twig, leaf, drop, brush, breath, click or crackle, and BRST sets how many bursts a note fires. There are no samples, so it costs no flash. | FREQ, RES, COLR, REL |
| **PHYS** | Physical models from SLOOP 2.5 (DaisySP, Rings): guitars, a harp with sympathetic strings, koto, chimes, bowls, a bowed cello. | |
| **GRAIN** | Grains over the piano or your samples: clouds, shimmer an octave up, droplets two octaves up, freezes, drones. | |
| **VOICE** | Formant voices: choirs, whispers, hums, vocal chops. | |
| **WAVE** | Soft single-cycle tones: glass, hollow, choir, a flute. | |

## Drum kits

| Kit | Sound |
|---|---|
| **TWIG** | Wood and felt: a felt thump, a heartbeat, wood snaps, a finger snap, twig ticks, shaken leaves, wood blocks, a small bell, seeds. |
| **FIELD** | Foley: a footstep, a twig snapping, leaves crunching, a water drop, rain, a log knock, a gust of wind, a cricket, a bird's chirp. |
| **DEW** | Micro-glitch: blips and chips, tiny and quick. |
| **MOSS** | Soft lo-fi. |
| **HYPER** | The hyperpop kit with a sub 808. |
| **AMBIENT**, **808** | Ambient and hip-hop kits. |

Your own kits (USR1–4, USR3+4, SYN1–SYN4) work as in NoteSorcery.

## Themes

Hold HOME → SCREEN → COLOR:

- **MOSS** (the default): deep green, cream type, petal-pink values.
- **PETAL**: light, cream paper with plum ink.
- **DUSK**: violet evening with peach values.
- **PAPER**, **CONTRAST** and **MONO**.

## Ableton Live and your phone

- The browser editor's PROJECTS page exports BOTANICA's projects (BTP1) as an Ableton Live Set or a MIDI file,
  the same way as NoteSorcery's. Tracks are named after BOTANICA's engines.
- The **BOTANICA Garden** Android app ([android/botanica](android/botanica/README.md)) is the companion: the
  PERFORM and SKY faders on a big touchscreen, the mood browser, petal chord pads, a tempo-drift planner, a field
  recorder that turns the phone's microphone into sample slots, and project backups with a MIDI export.

## How it differs from NoteSorcery

- **Engines:** FELT, DROPLET, NOISE (+FOLEY), PHYS, GRAIN and VOICE replace TRIO, FM6 and ACID. ANALOG, SAMPLE
  and WAVE carry BOTANICA's presets.
- **No 808 CM / 909 CM circuit kits and no TOOLS → GEN.** Their memory went to the engines' arenas.
- **The SLICER's stutter records 46 ms instead of 93 ms**, for the same reason.
- **Stores:** projects are BTP1 and user presets BTU1, so neither edition reads the other's. The settings
  (theme, lights) are BOTANICA's own.
- **CPU:** PHYS (bowed and sympathetic models) and GRAIN are heavier than NoteSorcery's engines. As on every
  edition, the FM-1 sheds voices gracefully if a dense arrangement asks for more than it can play.

## Credits

BOTANICA is built on NoteSorcery, SLOOP by isod89 and Felucca by Leo Kuroshita (@kurogedelic) / Hügelton
Instruments. PHYS ports DaisySP and Rings by Émilie Gillet (MIT). The piano is Versilian Studios' CC0 samples, and
the font is Terminus (OFL). Botanica as a genre owes its name and shape to its artists (Alexander Panos, phritz,
cli, Kihara Kenji, Suji, dowfi and many more) and to Porter Robinson's *Nurture*. BOTANICA is not affiliated with
any of them, nor with M-VAVE.
