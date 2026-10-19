#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copies android/fm1link/src/main (the shared Kotlin library) into the Android apps that carry it, so they build
# without this repo. Run after changing fm1link; each app commits its copy.
#   tools/sync_fm1link.sh NOTEMOVE_DIR FIELDTAPE_DIR [BOTANICA_DIR]   (an empty argument skips that app;
#   BOTANICA Garden is in this repo: android/botanica)
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)/android/fm1link/src/main/kotlin/com/notesorcery/fm1link"
if [ -n "$1" ]; then
    mkdir -p "$1/core/src/main/kotlin/com/notesorcery/fm1link"
    cp "$SRC"/*.kt "$1/core/src/main/kotlin/com/notesorcery/fm1link/"
    echo "fm1link -> $1 (core)"
fi
if [ -n "$2" ]; then
    mkdir -p "$2/app/src/main/java/com/notesorcery/fm1link"
    cp "$SRC"/*.kt "$2/app/src/main/java/com/notesorcery/fm1link/"
    echo "fm1link -> $2 (app)"
fi
if [ -n "$3" ]; then
    mkdir -p "$3/core/src/main/kotlin/com/notesorcery/fm1link"
    cp "$SRC"/*.kt "$3/core/src/main/kotlin/com/notesorcery/fm1link/"
    echo "fm1link -> $3 (core)"
fi
