# NSX: talking to a NoteSorcery FM-1

NSX is how the browser editor and the Android apps (FieldTape, NoteMove) work with an FM-1 running
NoteSorcery. It has two parts:

- **Standard MIDI** for playing, syncing and recording notes.
- **SysEx** for projects, samples, transport and capabilities. This is the editor protocol (`web/EDITOR_PROTOCOL.md`)
  at version 11, plus three NoteSorcery commands, 80–82.

Everything runs over the FM-1's USB-C port as a class-compliant USB-MIDI device; nothing needs a driver. The
firmware side is `firmware/src/editor.c` and `firmware/src/editor_nsx.c`. The test `tests/nsx_test.c` checks
NSX itself; `tests/clock_test.c` checks the clock.

## 1. Finding the device

1. Open the MIDI device whose USB product name contains `FM-1`.
2. Send `INFO` (`F0 7D 46 4C 01 F7`). Its reply ends with the protocol version, which is 11 or more on
   NoteSorcery.
3. Send `NSX_CAPS` (`F0 7D 46 4C 50 F7`). A NoteSorcery FM-1 answers it. SLOOP and Felucca do not reply.

## 2. Standard MIDI

| What | How |
|---|---|
| Notes to the FM-1 | Channels 1–6 play synth tracks 1–6. Drum track 1 is on GLO → DRUMS → CH (default 10) and drum track 2 on the next channel. Any other channel plays the selected track. |
| Notes from the FM-1 | GLO → SYSTEM → MIDI = **SEQ** or **SEQ+CLK**: the sequencer's notes go out on those same channels. Drum lanes use GM notes (kick 36, snare 38, closed hat 42 …). |
| Clock from the FM-1 | GLO → SYSTEM → MIDI = **KEYS+CLK** or **SEQ+CLK**, with SYNC = INT: `F2 00 00` (Song Position 0), `FA` (Start), then 24 `F8` a beat while playing, `FC` (Stop) at stop. With SYNC = TRS, the TRS clock is passed on to USB. With SYNC = USB, the FM-1 sends no clock (that would loop back to the master). |
| Clock to the FM-1 | GLO → SYSTEM → SYNC = **USB** (or TRS): `F8`, `FA`, `FB`, `FC`. The tempo follows within a beat. |
| Song Position to the FM-1 | `F2 lsb msb` (16ths) then `FB` (Continue): the patterns start at that position. Ableton Live sends this when playing from a marker. `FA` after it starts from the top. |

### Syncing with Ableton Live

- **FM-1 as master:** set MIDI to SEQ+CLK and SYNC to INT. In Live's MIDI preferences, turn on *Sync* for the
  FM-1's input. Live follows the FM-1's PLAY, STOP and tempo.
- **Live as master:** set SYNC to USB. In Live, turn on *Sync* for the FM-1's output. Live's Start, Stop and
  position drive the FM-1.

### Syncing two FM-1s

An FM-1 is a USB device and its TRS jack only receives, so two FM-1s cannot be connected directly. They meet
through a host:

- an Android phone running FieldTape on a USB hub, which forwards the master's clock to every FM-1;
- a computer;
- a USB-MIDI host box.

Set the master to SEQ+CLK and SYNC = INT, and the others to SYNC = USB.

## 3. SysEx

Every request is `F0 7D 46 4C cmd args… F7`, and every data byte is 7-bit. Every request gets exactly one
reply with the same command number; a command the device does not know gets no reply. The encodings are
those of `web/EDITOR_PROTOCOL.md`:

| Encoding | Meaning |
|---|---|
| v14 | 2 bytes, LSB first, value + 8192 |
| u21 | 3 × 7 bits, LSB first |
| u35 | 5 × 7 bits, LSB first |
| string | ASCII, ended by 0 |
| pack7 | the bytes as one bit stream, LSB first, cut into 7-bit groups (`web/fm1ota.js` `pack7`, `unpack7`) |

### NoteSorcery commands (v11)

| cmd | Request | Reply |
|---|---|---|
| 80 NSX_CAPS | — | "NSX", NSX version (1), firmware version string, NTRK (8), NPART (6), NDRUMTRK (2), NSP1 size u21 (7604), "N", feature bits, drum MIDI channel (1–16), user sample slots (4), KiB a slot (64), user wave slots (15), drum kits, the 808 CM kit number, the 909 CM kit number |
| 81 NSX_TRANSPORT | op: 0 query, 1 play, 2 stop, 3 toggle | playing (0/1, after the op), BPM v14, beat u21 (beats since PLAY), MIDI OUT bits (1 SEQ, 2 CLK), SYNC (0 INT, 1 USB, 2 TRS) |
| 82 NSX_TEMPO | BPM v14 (or nothing: a query) | BPM v14. Clamped to the BPM range; ignored while an external clock drives the FM-1 |

The feature bits are:

| Bit | Feature |
|---|---|
| 1 | clock out |
| 2 | Song Position in |
| 4 | USB audio to the host |
| 8 | USB audio from the host (live sampling; milestone 5) |
| 16 | the 808 CM / 909 CM circuit kits |

### Projects: backup objects (v6)

The working project and the song sections are NSP1 projects (`firmware/src/project.c` `project_t`, 7604
bytes). Read them with `BK_LIST` (34) and `BK_GET` (35); write them with `BK_PUT` (36), only while stopped.

| Object | Contents |
|---|---|
| 0 | the working project |
| 2..5 | sections A..D (length 0 = empty) |
| 1 | the settings |
| 6..7 | user preset banks |
| 8 | FM6 bank |
| 9 | SYN drum kits |
| 32..35 | sample slots |

NSP1 layout, all little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `0x3150534E` ("NSP1") |
| 4 | 4 | size (7604) |
| 8 | 2 × G_COUNT (34) | globals (BPM at 0, swing at 1 …) |
| 76 | 4 | selected track, pending section, track count (8), reserved |
| 80 | 940 × 8 | tracks: `p[61]` (int16), engine, preset, 64 steps × 10 bytes, 64 nudges, 24 locks × 4, 16 fill bytes |
| 7600 | 4 | FNV-1a sum of bytes 0..7599 |

Tracks 1–6 hold synth steps (`note[4], n, time, flags, vel, lvl, rat`); tracks 7–8 hold drum steps (16 lanes:
on bits, levels, ratchets). `web/als/nsals.js` `decodeNsp1` and `trackClip` are a full reader: they turn the
steps into the notes the FM-1 plays. `tests/nsp1_export.c` writes `layout.json` with the firmware's own
offsets and indices.

### Samples (v1)

`SMP_BEGIN` (11), `SMP_WRITE` (12), `SMP_END` (13), `SMP_ERASE` (14) and `SMP_INFO` (15) fill a user slot with a
multisample: up to 16 zones, IMA-ADPCM, 64 KiB. The format is in `web/EDITOR_PROTOCOL.md`, and the reference
encoder is `tools/fm1_sample_upload.py` / `web/editor.html`. A SoundFont (.sf2) preset is uploaded the same
way, after being converted to zones on the phone or in the browser: one sample per key range, resampled to
fit.
