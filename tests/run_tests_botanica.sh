#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Host tests of BOTANICA (no hardware). Run from the repo root after ./build.sh --edition botanica:
#   tests/run_tests_botanica.sh
# The shared tests (the sequencer, the projects, the UI, the drums, the USB / MIDI paths...) built with
# -DEDITION_BOTANICA=1 against build/botanica/gen, BOTANICA's own (tests/botanica_test.c: FELT, DROPLET, FOLEY, the
# PERFORM and SKY macros, the petal chords, DRIFT, the stores), and the regression renders of its engines and
# presets (tests/golden_botanica.txt, tests/cpu_baseline_botanica.txt, tests/target_budget_botanica.txt).
# After an intended change of the sound: GOLDEN_UPDATE=1 sh tests/run_tests_botanica.sh (review the diff); of the
# cost: BUDGET_UPDATE=1. The NoteSorcery suite is tests/run_tests.sh.
set -e
cd "$(dirname "$0")/.."
B=build/botanica
OUT=$B/host
mkdir -p "$OUT"
CC="${CC:-cc} -O2 -w -DEDITION_BOTANICA=1 -I$B/gen -Ifirmware/src -Ifirmware/hal"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f $B/felucca.fwsc ] || { echo "run ./build.sh --edition botanica first"; exit 1; }

$CC -o "$OUT/botanica_test" tests/botanica_test.c -lm
mkdir -p $B/botanica_demo
run "BOTANICA: FELT, DROPLET, FOLEY, PERFORM / SKY, petal chords, strum, ratchets, DRIFT, the stores, demos" \
    "$OUT/botanica_test" $B/botanica_demo

$CC -o "$OUT/song_audio_test" tests/song_audio_test.c -lm
run "song: four simultaneous tracks, scene transition and stop" "$OUT/song_audio_test" "$OUT/song-demo.wav"
$CC -o "$OUT/song_ui_test" tests/song_ui_test.c -lm
run "song screen: commands, load (OCT+ twice), display bounds" "$OUT/song_ui_test" "$OUT/song-screen.ppm"
$CC -o "$OUT/studio_drums_test" tests/studio_drums_test.c -lm
run "drum lanes, kit audio, metronome, record arm, free take" "$OUT/studio_drums_test" "$OUT/drum-styles.wav"
$CC -o "$OUT/seq2_test" tests/seq2_test.c -lm
run "sequencer: no drift, ratchets, roll, erase / undo, ghost / hard, chords, mute / solo, nudge, locks" "$OUT/seq2_test"
$CC -o "$OUT/drumkit_test" tests/drumkit_test.c -lm
run "synthesised drum kits (TWIG, FIELD, DEW, MOSS, ...): bounded, audible, finite, levels, cost" \
    "$OUT/drumkit_test" "$OUT/drum-kits.wav" "$OUT/drum-kits.txt"
$CC -o "$OUT/clock_test" tests/clock_test.c -lm
run "MIDI clock out and SONG POSITION in" "$OUT/clock_test"
$CC -o "$OUT/nsx_test" tests/nsx_test.c -lm
run "NSX: CAPS (edition B), TRANSPORT, TEMPO" "$OUT/nsx_test"
$CC -o "$OUT/userkit_test" tests/userkit_test.c -lm
run "user drum kits (USR1..USR4)" "$OUT/userkit_test"
$CC -o "$OUT/dsyn_test" tests/dsyn_test.c -lm
run "SYN1..SYN4 drum kits and the editor's DRUM SYNTH commands" "$OUT/dsyn_test"
$CC -o "$OUT/punch_test" tests/punch_test.c -lm
run "punch-in FX: 16 effects, bounded, dry after release" "$OUT/punch_test" "$OUT/punch-fx.wav"
$CC -o "$OUT/ui_pages_test" tests/ui_pages_test.c -lm
run "live UI: pages (PERFORM, SKY, FEEL), layers, holds, drums, REC, the mood browser, fuzz" "$OUT/ui_pages_test" "$OUT"
UBSAN="${CC_UB:-cc} -O1 -w -DEDITION_BOTANICA=1 -fsanitize=integer-divide-by-zero -fno-sanitize-recover=integer-divide-by-zero -I$B/gen -Ifirmware/src -Ifirmware/hal"
$UBSAN -o "$OUT/ui_pages_ub" tests/ui_pages_test.c -lm && $UBSAN -o "$OUT/botanica_ub" tests/botanica_test.c -lm &&
    $UBSAN -o "$OUT/soak_ub" tests/soak_test.c -lm || fail=1
mkdir -p "$OUT/ub"
run "no divide by zero (UBSan): UI fuzz, BOTANICA's features, a minute of live use" \
    sh -c "'$OUT/ui_pages_ub' '$OUT/ub' >/dev/null && '$OUT/botanica_ub' '$OUT/ub' >/dev/null && '$OUT/soak_ub' 1 >/dev/null && echo 'no divide by zero'"
$CC -o "$OUT/soak_test" tests/soak_test.c -lm
run "soak: ${SOAK_MIN:-5} minutes of random live use" "$OUT/soak_test" "${SOAK_MIN:-5}"
$CC -o "$OUT/stress_test" tests/stress_test.c -lm
run "stress: hard random use, clean stop" "$OUT/stress_test" "${STRESS_FRAMES:-40000}"
ASAN="${CC_UB:-cc} -O1 -g -w -DEDITION_BOTANICA=1 -fsanitize=address,undefined -fno-sanitize=shift-base,signed-integer-overflow -fno-sanitize-recover=all -I$B/gen -Ifirmware/src -Ifirmware/hal"
if $ASAN -o "$OUT/stress_asan" tests/stress_test.c -lm 2>/dev/null && $ASAN -o "$OUT/botanica_asan" tests/botanica_test.c -lm 2>/dev/null; then
    run "stress and BOTANICA's features under ASan + UBSan (no access out of bounds)" \
        sh -c "'$OUT/stress_asan' ${STRESS_ASAN_FRAMES:-15000} 7 && '$OUT/botanica_asan' '$OUT/ub' >/dev/null"
else
    echo "(ASan: this compiler has no AddressSanitizer, skipped)"
fi
$CC -o "$OUT/scale_test" tests/scale_test.c -lm
run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
$CC -o "$OUT/project_test" tests/project_test.c -lm
run "project format (BTP1: 8 tracks), capture / apply, sections" "$OUT/project_test"
$CC -o "$OUT/hostsim" tests/hostsim.c -lm
mkdir -p $B/tracks_demo
run "TRACKS: 4-track pattern, live recording, voice budget, engine switch, cost" \
    env TRACKS=$B/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
$CC -o "$OUT/regress" tests/regress.c -lm
if [ -z "$SKIP_CPU_VALGRIND" ] && command -v valgrind >/dev/null 2>&1; then export CPU_VALGRIND=1; fi
run "regression: golden renders of BOTANICA's engines and presets, health, voices, CPU budget" \
    "$OUT/regress" tests/golden_botanica.txt tests/cpu_baseline_botanica.txt
run "regression: target cost of BOTANICA's render loops" env NS_EDITION=botanica python3 tests/target_budget.py \
    $B/felucca.dis tests/target_budget_botanica.txt

[ $fail -eq 0 ] && echo "ALL BOTANICA HOST TESTS PASSED" || { echo "BOTANICA HOST TESTS FAILED"; exit 1; }
