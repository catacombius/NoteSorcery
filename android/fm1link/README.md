# fm1link

A small Kotlin library for talking to a NoteSorcery FM-1. It is plain JVM code with no dependencies. The
Android apps FieldTape and NoteMove carry a copy of `src/main`, synced with `tools/sync_fm1link.sh`.

| File | What it does |
|---|---|
| `Codec.kt` | The SysEx frame, v14 / uN numbers and pack7. `SysExAssembler` splits Android's MIDI byte chunks into SysEx frames and everything else. |
| `Fm1Client.kt` | Requests and replies over any MIDI transport: `info`, `caps` (NSX), `play` / `stop` / `tempo`, `pullSong` (the working project and sections A–D), `uploadSample`. `Fm1Midi` covers the clock, Song Position and the track channels. |
| `Nsp1.kt` | Reads the FM-1's project format and turns the steps into the notes the FM-1 plays (the same as `web/als/nsals.js`). The result is an `Fm1Song`: tempo, key, sections as scenes, eight tracks of clips. |
| `SampleSlot.kt` | A user sample slot (IMA-ADPCM zones), byte for byte as `tools/sampleio.py` builds it. For WAVs and SoundFont presets. |

The protocol is described in [`docs/NSX_PROTOCOL.md`](../../docs/NSX_PROTOCOL.md).

## Tests

    tools/gen_fm1link_fixtures.py     # (after ./build.sh) the fixtures from the firmware, nsals.js and sampleio.py
    cd android/fm1link && ./gradlew test

The tests check the library against the firmware's own project files and layout, `nsals.js`'s notes for them,
`sampleio.py`'s sample slot, and a fake FM-1 that replies as `editor.c` does (in random chunks, with clock
bytes in between).
