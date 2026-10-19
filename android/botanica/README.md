# BOTANICA Garden

The Android companion of the [BOTANICA](../../BOTANICA.md) firmware for the M-VAVE FM-1. Plug the FM-1 into the
phone (USB-C, an OTG adapter if needed); the app opens and connects. It talks the FM-1's editor protocol over
`android.media.midi`. That means USB, or a Bluetooth MIDI device the phone has already paired.

The app never writes firmware. Install and update BOTANICA from the web installer in Chrome on a computer.

## Screens

| Screen | What it does |
|---|---|
| **Garden** | The device's GARDEN mirrored: a plant per track that flowers on its notes, rain with WEATHER, the sky with the tempo. Below it are two pages of four big faders, coloured like the knobs. **PERFORM** is BLOOM, DROPLETS, FOLEY and GLITCH; **SKY** is WEATHER, BREEZE, DRIFT and FILTER. **FREEZE**, **GLITCH** and **BREAK** act while held (BREAK mutes every track but the selected one and freezes the reverb; letting go is the DROP, at the DRIFT tempo). There is also play / stop. |
| **Moods** | BOTANICA's sounds grouped as the device's browser groups them: Petals, Canopy, Dew, Voices, Roots, Weather, Hyper. Pick a track, then tap a sound to hear it there. Stars keep favourites on the phone. |
| **Pads** | Petal chord pads on the selected track's MIDI channel. The top row is ROYAL (IVmaj7 V7 iii7 vi7) or PETAL (IVmaj7 Vadd9 vi9 Iadd9) in any key; the bottom row is MAJ7, MAJ9, ADD9, 6/9 and LYD#11. Strum is 0..80 ms, up, down or random, as the firmware strums. Touch near the top of a pad to play loud, near the bottom to play soft. |
| **Drift** | A tempo per song section (A..D), the bars a section and the ramp, drawn as a curve. Presets: ARC 72·96·160·80, SLAM 60→160, HALF/DOUBLE 80↔160. With *Follow the song* on, the app sends DRIFT and RAMP a bar ahead of each change while the FM-1 plays, and the firmware makes the ramp on its own clock. |
| **Field** | Record crickets, rain or a hummed note with the phone's microphone. Trim it, reverse it, stretch it ×2..×8 into a drone (PaulStretch-style) and make it loop seamlessly. Hear it, then send it to a sample slot (USR1..USR4). On a SAMPLE track, turn SET to that slot. |
| **Projects** | Back up the working project and sections A..D to a file in the web editor's format (either one restores it), restore a backup, and export the song as a multi-track `.mid` for Ableton Live. Use the web editor for a full `.als`. |
| **FM-1** | The connection: firmware, tracks, engines, other MIDI devices, a link to the web installer, and the high-contrast theme. |

The app checks that the FM-1 runs BOTANICA: NSX_CAPS feature bit 32, see [docs/NSX_PROTOCOL.md](../../docs/NSX_PROTOCOL.md). It finds the PERFORM and SKY globals by their DESC labels, not by number. On NoteSorcery or stock firmware it says so and does nothing else.

## Accessibility

- Every control has a TalkBack label; the faders are adjustable controls (swipe up / down).
- Touch targets are at least 48 dp.
- Pads and hold buttons give haptic feedback.
- The **High contrast** theme (the firmware's CONTRAST palette) is on the FM-1 screen.

## Building

```
cd android/botanica
./gradlew :core:test :app:assembleDebug      # needs JDK 17+ and the Android SDK (ANDROID_HOME)
```

The APK is `app/build/outputs/apk/debug/app-debug.apk`. CI builds it on every push (the `android` job in
`.github/workflows/site.yml`) and uploads it as the `botanica-garden-0.1-debug` artifact. A signed release build
needs your own keystore and is not set up here.

## Layout

- `core/`: plain JVM, unit-tested, no Android.
  - `fm1link/`: a copy of [android/fm1link](../fm1link) (refresh it with `tools/sync_fm1link.sh "" "" android/botanica`).
  - `Garden.kt`: the live link: DESC, GET/SET by label, NAMES, PRESET, TRACK, TRACK_PARAM, WATCH pushes, BK_PUT, and the FREEZE / GLITCH / BREAK actions.
  - `Btp1.kt`: the BTP1 project reader.
  - `Chords.kt`, `Moods.kt`, `Drift.kt`.
  - `Dsp.kt` and `FieldSample.kt`: the recorder's processing.
  - `MidiFile.kt`, `Backup.kt`.
- `app/`: Compose (Material 3) screens, `Link.kt` (the MIDI transport and the state the screens show), and the recorder.

The tests check `core` against the firmware itself:

- BTP1 projects that the firmware's own code wrote, and the notes `web/als/nsals.js` reads from them (fixtures: `tools/gen_botanica_app_fixtures.py`).
- The chord and mood tables, parsed from `firmware/src/seq.c` and `ui.c`.
- A fake BOTANICA FM-1 for the live link.

There are no UI tests. The screens have only been checked by building them.

GPL-3.0-only, like the rest of the repository.
