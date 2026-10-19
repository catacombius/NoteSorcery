// SPDX-License-Identifier: GPL-3.0-only
// The app's logic, plain JVM: a copy of android/fm1link (tools/sync_fm1link.sh) and BOTANICA's own (Btp1, Chords,
// Drift, Dsp, MidiFile, Moods, Garden), with JVM unit tests.
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    kotlin("jvm")
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    compilerOptions { jvmTarget.set(JvmTarget.JVM_17) }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}

tasks.test {
    testLogging { events("passed", "failed"); showStandardStreams = false }
}
