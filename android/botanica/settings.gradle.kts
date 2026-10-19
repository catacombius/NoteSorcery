// SPDX-License-Identifier: GPL-3.0-only
// BOTANICA Garden: the Android companion of the BOTANICA firmware (BOTANICA.md). core: plain JVM (the FM-1 link, the
// BTP1 reader, chords, the tempo drift, the field recorder's DSP, the MIDI file), unit-tested; app: the Compose UI,
// the MIDI transport and the microphone.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "botanica-garden"
include(":core", ":app")
