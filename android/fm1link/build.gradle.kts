// SPDX-License-Identifier: GPL-3.0-only
// fm1link: talking to a NoteSorcery FM-1 from Kotlin (docs/NSX_PROTOCOL.md). Plain JVM, no dependencies: the
// Android apps (FieldTape, NoteMove) carry a copy of src/main (tools/sync_fm1link.sh).
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    kotlin("jvm") version "2.0.21"
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
