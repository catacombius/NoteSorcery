// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.notesorcery.botanica.core.Chords
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Petal pads: the Royal Road and IV-V-vi-I on four big pads, and the petal chords (MAJ7 .. LYD#11) on the key, played
 * on the selected track's channel, strummed as the firmware strums (0..80 ms, up / down / random). Where a pad is
 * touched sets the velocity: the top is loud, the bottom soft.
 */
@Composable
fun PadsScreen(link: Link) {
    val p = LocalPalette.current
    if (link.state != Link.State.CONNECTED) { NeedsDevice(link, "The pads play the FM-1: connect it first."); return }
    var prog by remember { mutableStateOf(Chords.Progression.PETAL) }
    var root by remember { mutableIntStateOf(0) }
    var octave by remember { mutableIntStateOf(0) }
    var strum by remember { mutableIntStateOf(30) }
    var order by remember { mutableIntStateOf(0) }
    val scope = rememberCoroutineScope()
    val track = (link.tracks?.selected ?: 0).let { if (it >= 6) 0 else it }
    val play = remember { Player(link, scope) }
    play.track = track; play.strum = strum; play.order = order

    Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Heading("Petal pads", "Track ${track + 1} · key of ${KEYS[root]}")
        TrackRow(link, synthsOnly = true)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Chords.Progression.entries.forEach { pr ->
                FilterChip(prog == pr, { prog = pr }, { Text(pr.label) },
                    colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.knob[1], selectedLabelColor = p.ground))
            }
            listOf("UP", "DOWN", "RAND").forEachIndexed { i, s ->
                FilterChip(order == i, { order = i }, { Text(s) },
                    colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.knob[2], selectedLabelColor = p.ground))
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("KEY", color = p.dim, modifier = Modifier.padding(end = 8.dp))
            Slider(root.toFloat(), { root = it.toInt() }, valueRange = 0f..11f, steps = 10, modifier = Modifier.weight(1f)
                .semantics { contentDescription = "Key" })
            Text(KEYS[root], color = p.text,
                modifier = Modifier.padding(horizontal = 8.dp), fontWeight = FontWeight.Bold)
            Text("OCT", color = p.dim)
            Slider(octave.toFloat(), { octave = it.toInt() }, valueRange = -2f..2f, steps = 3, modifier = Modifier.weight(0.6f)
                .semantics { contentDescription = "Octave" })
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("STRUM", color = p.dim, modifier = Modifier.padding(end = 8.dp))
            Slider(strum.toFloat(), { strum = it.toInt() }, valueRange = 0f..80f, modifier = Modifier.weight(1f)
                .semantics { contentDescription = "Strum, milliseconds a note" })
            Text("$strum ms", color = p.text, modifier = Modifier.padding(start = 8.dp))
        }
        Row(Modifier.fillMaxWidth().weight(1f), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            prog.steps.forEachIndexed { i, st ->
                Pad(st.name, p.knob[i], Modifier.weight(1f), play) { Chords.progression(prog, root, i, 60 + 12 * octave) }
            }
        }
        Row(Modifier.fillMaxWidth().weight(0.7f), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Chords.Kind.entries.forEachIndexed { i, k ->
                Pad(k.label, p.knob[(i + 1) % 4].copy(alpha = 0.8f), Modifier.weight(1f), play) { Chords.chord(k, 48 + root + 12 * octave) }
            }
        }
    }
}

/** plays a pad's chord strummed, and lets it go */
private class Player(val link: Link, val scope: CoroutineScope) {
    var track = 0
    var strum = 0
    var order = 0
    private val jobs = HashMap<List<Int>, Job>()
    fun handle(notes: List<Int>, vel: Int) {
        jobs.remove(notes)?.cancel()                        // (let go before the strum ended: no stuck notes)
        if (vel > 0) jobs[notes] = scope.launch {
            var t = 0
            for ((n, at) in Chords.strumOffsets(notes, strum, order)) {
                if (at > t) { delay((at - t).toLong()); t = at }
                link.note(track, n, vel)
            }
        } else for (n in notes) link.note(track, n, 0)
    }
}

private val KEYS = listOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

/** a pad: a touch plays [chord] (its velocity from where it was touched), the release lets those notes go */
@Composable
private fun Pad(label: String, color: Color, modifier: Modifier, player: Player, chord: () -> List<Int>) {
    val p = LocalPalette.current
    val haptic = LocalHapticFeedback.current
    var down by remember { mutableStateOf(false) }
    val current by rememberUpdatedState(chord)
    Box(
        modifier.fillMaxSize().clip(RoundedCornerShape(18.dp)).background(if (down) color else p.surface)
            .border(2.dp, color, RoundedCornerShape(18.dp))
            .semantics { contentDescription = "Chord $label" }
            .pointerInput(Unit) {
                awaitEachGesture {
                    val d = awaitFirstDown()
                    val vel = (127 - d.position.y / size.height * 87).toInt().coerceIn(40, 127)
                    val notes = current()
                    down = true
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    player.handle(notes, vel)
                    waitForUpOrCancellation()
                    down = false
                    player.handle(notes, 0)
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (down) p.ground else color, fontWeight = FontWeight.Bold, fontSize = 18.sp, textAlign = TextAlign.Center)
    }
}
