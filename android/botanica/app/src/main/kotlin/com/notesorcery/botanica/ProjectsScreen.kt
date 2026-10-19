// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.notesorcery.botanica.core.Backup
import com.notesorcery.botanica.core.Btp1
import com.notesorcery.botanica.core.MidiFile
import java.time.Instant
import java.time.LocalDate

/**
 * The FM-1's projects on the phone: a backup of the working project and the sections A..D (the web editor's file
 * format, so either restores it), a restore, and the song as a multi-track .mid for Ableton Live or any DAW.
 */
@Composable
fun ProjectsScreen(link: Link) {
    val p = LocalPalette.current
    val ctx = LocalContext.current
    var restore by remember { mutableStateOf<Backup?>(null) }
    val connected = link.state == Link.State.CONNECTED

    fun write(uri: Uri, bytes: ByteArray) {
        ctx.contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) } ?: throw IllegalStateException("cannot write the file")
    }

    val saveBackup = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
        if (uri != null) link.run("Backup") { g ->
            val objs = g.pullProjects { d, t -> link.progress(d, t) }.filterValues { it != null }.mapValues { it.value!! }
            write(uri, Backup(link.device?.firmware ?: "BOTANICA", Instant.now().toString(), objs).toJson().toByteArray())
            link.say("Backed up: the working project and ${objs.keys.count { it != 0 }} sections.")
        }
    }
    val saveMidi = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/midi")) { uri ->
        if (uri != null) link.run("MIDI export") { g ->
            val o = g.pullProjects { d, t -> link.progress(d, t) }
            val song = Btp1.song(o[0], listOf(o[2], o[3], o[4], o[5]), name = "BOTANICA sketch")
            write(uri, MidiFile.write(song, loops = 2))
            link.say("Exported ${song.noteCount()} notes in ${song.sceneCount} scene(s), ${song.tempo.toInt()} BPM.")
        }
    }
    val open = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) try {
            val text = ctx.contentResolver.openInputStream(uri)!!.use { String(it.readBytes()) }
            val b = Backup.fromJson(text)
            if (b.projects().isEmpty()) link.say("That backup holds no BOTANICA projects (a NoteSorcery backup?).") else restore = b
        } catch (e: Exception) { link.say("Not a backup: ${e.message}") }
    }

    Column(Modifier.fillMaxSize().padding(16.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Heading("Projects", "Keep your sketches on the phone, take them to Ableton.")
        Button({ saveBackup.launch("botanica-${LocalDate.now()}.json") }, Modifier.fillMaxWidth().heightIn(min = 56.dp), enabled = connected) {
            Text("Back up the FM-1", fontWeight = FontWeight.Bold)
        }
        OutlinedButton({ open.launch(arrayOf("application/json", "application/octet-stream", "*/*")) }, Modifier.fillMaxWidth().heightIn(min = 56.dp),
            enabled = connected) { Text("Restore a backup…") }
        Button({ saveMidi.launch("botanica-sketch.mid") }, Modifier.fillMaxWidth().heightIn(min = 56.dp), enabled = connected) {
            Text("Export to Ableton (.mid)", fontWeight = FontWeight.Bold)
        }
        Text("The .mid has a track per FM-1 track with its name and the sections A..D one after the other. For a full " +
            "Live Set (.als, scenes and colours), open the web editor's Ableton export on a computer.", color = p.dim)
        link.progress?.let { LinearProgressIndicator({ it }, Modifier.fillMaxWidth()) }
        Text(link.message, color = p.dim)
    }

    restore?.let { b ->
        val ids = b.projects().keys.sorted()
        AlertDialog(
            onDismissRequest = { restore = null },
            title = { Text("Restore ${ids.size} project(s)?") },
            text = {
                Text("From ${b.firmware}, ${b.date.take(10)}. This replaces " +
                    ids.joinToString { if (it == 0) "the working project" else "section ${"ABCD"[it - 2]}" } +
                    " on the FM-1. The song is stopped first.")
            },
            confirmButton = {
                TextButton({
                    restore = null
                    val projects = b.projects()
                    link.run("Restore") { g ->
                        g.client.stop()
                        val order = projects.keys.sortedBy { if (it == 0) 99 else it }   // (the sections, then the working project)
                        val total = projects.values.sumOf { it.size }
                        var done = 0
                        for (id in order) {
                            val d = projects[id]!!
                            g.putObject(id, d) { x, _ -> link.progress(done + x, total) }
                            done += d.size
                        }
                        link.say("Restored ${order.size} project(s).")
                    }
                }) { Text("Restore") }
            },
            dismissButton = { TextButton({ restore = null }) { Text("Cancel") } },
        )
    }
}
