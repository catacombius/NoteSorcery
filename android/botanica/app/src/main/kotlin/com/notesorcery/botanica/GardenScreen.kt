// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch
import kotlin.math.exp
import kotlin.math.sin

/**
 * The live screen: the device's GARDEN mirrored (a plant a track, its flower opening on the track's notes, rain with
 * WEATHER, the sky with the tempo), the PERFORM and SKY pages of four big faders, and FREEZE / GLITCH / BREAK held.
 */
@Composable
fun GardenScreen(link: Link) {
    val p = LocalPalette.current
    if (link.state != Link.State.CONNECTED) { NeedsDevice(link, "The garden grows when a BOTANICA FM-1 is connected."); return }
    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Plants(link, Modifier.fillMaxWidth().weight(0.8f))
        Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(
                onClick = { link.transport(3) },
                colors = ButtonDefaults.buttonColors(containerColor = if (link.playing) p.knob[1] else p.surface,
                    contentColor = if (link.playing) p.ground else p.text),
                modifier = Modifier.heightIn(min = 48.dp),
            ) { Text(if (link.playing) "■  STOP" else "▶  PLAY", fontWeight = FontWeight.Bold) }
            Spacer(Modifier.width(16.dp))
            Text("${link.bpm} BPM", color = p.text, fontSize = 20.sp, fontWeight = FontWeight.Bold)
            val drift = link.values["DRIFT"] ?: 0
            if (drift > 0 && drift != link.bpm) Text("  → $drift", color = p.knob[2], fontSize = 20.sp)
        }
        val pager = rememberPagerState { 2 }
        val scope = rememberCoroutineScope()
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf("PERFORM", "SKY").forEachIndexed { i, name ->
                Button(
                    onClick = { scope.launch { pager.animateScrollToPage(i) } },
                    colors = ButtonDefaults.buttonColors(containerColor = if (pager.currentPage == i) p.line else p.surface, contentColor = p.text),
                ) { Text(name) }
            }
        }
        HorizontalPager(pager, Modifier.fillMaxWidth().weight(1f)) { page ->
            val labels = if (page == 0) Garden4.PERFORM else Garden4.SKY
            Row(Modifier.fillMaxSize().padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                labels.forEachIndexed { k, (label, title) ->
                    val d = link.garden?.global(label)
                    val min = d?.min ?: 0
                    val max = d?.max ?: 127
                    Fader(title, link.values[label] ?: d?.def ?: 0, max, p.knob[k], Modifier.weight(1f).fillMaxSize(), min,
                        shown = if (label == "DRIFT") { v -> if (v == 0) "OFF" else "$v" } else { v -> "${(v - min) * 100 / maxOf(1, max - min)}%" },
                    ) { v -> link.setGlobal(label, v) }
                }
            }
        }
        Row(Modifier.fillMaxWidth().padding(top = 10.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            HoldButton("FREEZE", p.knob[2], Modifier.weight(1f)) { on -> link.run("Freeze") { g -> g.freeze(on) } }
            HoldButton("GLITCH", p.knob[3], Modifier.weight(1f)) { on -> link.run("Glitch") { g -> g.glitch(on) } }
            HoldButton("BREAK", p.knob[0], Modifier.weight(1f)) { on -> link.run("Break") { g -> if (on) g.breakDown() else g.drop() } }
        }
    }
}

/** the pages' labels (DESC) and the words the faders show */
object Garden4 {
    val PERFORM = listOf("BLOOM" to "BLOOM", "DROPS" to "DROPLETS", "FOLEY" to "FOLEY", "GLTCH" to "GLITCH")
    val SKY = listOf("WTHR" to "WEATHER", "BREEZ" to "BREEZE", "DRIFT" to "DRIFT", "FILT" to "FILTER")
}

@Composable
private fun Plants(link: Link, modifier: Modifier) {
    val p = LocalPalette.current
    var now by remember { mutableLongStateOf(System.nanoTime()) }
    LaunchedEffect(Unit) { while (true) withFrameNanos { now = System.nanoTime() } }
    val tracks = link.tracks
    val weather = (link.values["WTHR"] ?: 0) / 127f
    val bloom = (link.values["BLOOM"] ?: 0) / 127f
    val glitch = (link.values["GLTCH"] ?: 0) / 127f
    val skyMix = ((link.bpm - 60) / 100f).coerceIn(0f, 1f)
    Canvas(modifier.semantics { contentDescription = "The garden: a plant for each track, flowering on its notes" }) {
        val t = now / 1e9
        drawRect(lerp(p.night, p.sky, skyMix))
        drawRect(p.surface, Offset(0f, size.height * 0.86f), androidx.compose.ui.geometry.Size(size.width, size.height * 0.14f))
        // rain, more with WEATHER
        val drops = (weather * 60).toInt()
        for (i in 0 until drops) {
            val x = (((i * 97.3 + 13) % 100) / 100 * size.width).toFloat()
            val y = (((t * (0.6 + (i % 5) * 0.1) + i * 0.137) % 1.0) * size.height * 0.86).toFloat()
            drawLine(p.knob[2].copy(alpha = 0.5f), Offset(x, y), Offset(x - 2f, y + 10f), 2f)
        }
        val n = 8
        for (i in 0 until n) {
            val ts = tracks?.tracks?.getOrNull(i)
            val level = ((ts?.level ?: 104) / 127f).coerceIn(0.1f, 1f)
            val muted = ts?.mute == true
            val x = size.width * (i + 0.5f) / n
            val ground = size.height * 0.88f
            val top = ground - size.height * 0.7f * level
            val sway = (sin(t * 0.8 + i) * 6 * (0.3 + weather)).toFloat()
            val stem = if (muted) p.dim.copy(alpha = 0.4f) else p.knob[1]
            drawLine(stem, Offset(x, ground), Offset(x + sway, top), 5f, StrokeCap.Round)
            val age = (now - link.lastNote[i]) / 1e9
            val open = if (link.lastNote[i] == 0L) 0f else exp(-age * 2.5).toFloat()
            val r = size.minDimension * 0.035f * (1f + 1.2f * maxOf(open, bloom * 0.6f))
            val petal = if (i >= 6) p.knob[3] else p.knob[0]
            val c = Offset(x + sway, top)
            if (bloom > 0.05f) drawCircle(petal.copy(alpha = 0.18f * bloom), r * 2.4f, c)
            val jitter = if (glitch > 0.3f && open > 0.3f) ((t * 40).toInt() % 3 - 1) * 4f * glitch else 0f
            for (k in 0 until 5) rotate(k * 72f + (t * 10).toFloat() % 360 * open, c) {
                drawCircle(if (muted) p.dim.copy(alpha = 0.4f) else petal, r * 0.55f, Offset(c.x + jitter, c.y - r * 0.7f))
            }
            drawCircle(p.knob[3], r * 0.38f, c)
            if (tracks?.selected == i) drawCircle(Color.White.copy(alpha = 0.6f), r * 1.5f, c, style = androidx.compose.ui.graphics.drawscope.Stroke(2f))
        }
    }
    Spacer(Modifier.height(0.dp))
}
