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
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
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

/** the firmware's track colours (firmware/src/ui_studio.c TRK_COL0, BOTANICA): each plant keeps its track's */
private val TRACK_COL = listOf(Color(0xFFFF78A8), Color(0xFF60C86E), Color(0xFF5AAAFF), Color(0xFFFFCE40),
    Color(0xFFB08CFF), Color(0xFFFF8C64), Color(0xFF96BE3C), Color(0xFF50D2BE))

/**
 * The device's night GARDEN (firmware/src/ui_garden.c): stars twinkling under a crescent moon, and a plant a track,
 * each its own kind (moonflower, fern, tulip, lavender, dandelion, bluebell; the drum tracks mushrooms and cattails),
 * as tall as the track is loud, answering its notes.
 */
@Composable
private fun Plants(link: Link, modifier: Modifier) {
    var now by remember { mutableLongStateOf(System.nanoTime()) }
    LaunchedEffect(Unit) { while (true) withFrameNanos { now = System.nanoTime() } }
    val tracks = link.tracks
    val weather = (link.values["WTHR"] ?: 0) / 127f
    val bloom = (link.values["BLOOM"] ?: 0) / 127f
    val warm = ((link.bpm - 50) / 110f).coerceIn(0f, 1f) * 0.25f
    Canvas(modifier.semantics { contentDescription = "The night garden: a plant for each track, answering its notes" }) {
        val t = now / 1e9
        val w = size.width
        val hgt = size.height
        val ground = hgt * 0.86f
        drawRect(Brush.verticalGradient(listOf(Color(0xFF060A20), lerp(Color(0xFF1E1A4C), Color(0xFF603478), warm)), 0f, ground))
        drawRect(Color(0xFF12281C), Offset(0f, ground), Size(w, hgt - ground))
        if (weather < 0.72f) for (k in 0 until 48) {             // the stars
            val hsh = (k * 2654435761L) and 0xFFFFFFFFL
            val sx = (hsh % 240) / 240f * w
            val sy = ((hsh shr 9) % 160) / 160f * ground * 0.9f
            val tw = (0.5 + 0.5 * sin(t * 2.4 + k * 0.84)).toFloat()
            drawCircle(Color(0xFFFFFCE8).copy(alpha = 0.25f + 0.75f * tw), if (k % 7 == 0) 2.2f else 1.3f, Offset(sx, sy))
        }
        val moon = Offset(w * 0.86f, hgt * 0.12f)                // a crescent moon
        drawCircle(Color(0xFFF0EACE), hgt * 0.05f, moon)
        drawCircle(Color(0xFF080C26), hgt * 0.046f, moon + Offset(hgt * 0.022f, -hgt * 0.02f))
        val drops = (weather * 60).toInt()                       // the rain
        for (i in 0 until drops) {
            val x = (((i * 97.3 + 13) % 100) / 100 * w).toFloat()
            val y = (((t * (0.6 + (i % 5) * 0.1) + i * 0.137) % 1.0) * ground).toFloat()
            drawLine(Color(0xFFAAC8FF).copy(alpha = 0.5f), Offset(x, y), Offset(x - 2f, y + 10f), 2f)
        }
        for (i in 0 until 8) {
            val ts = tracks?.tracks?.getOrNull(i)
            val quiet = ts?.mute == true
            val level = ((ts?.level ?: 104) / 127f).coerceIn(0.1f, 1f)
            val x = w * (i + 0.5f) / 8
            val h = if (quiet) hgt * 0.12f else hgt * 0.65f * level
            val sway = (sin(t * 0.8 + i) * 6 * (0.3 + weather)).toFloat()
            val tip = Offset(x + sway, ground - h)
            val age = (now - link.lastNote[i]) / 1e9
            val f = if (link.lastNote[i] == 0L || quiet) 0f else exp(-age * 2.5).toFloat()   // the note's flash 1..0
            val col = if (quiet) Color.Gray else TRACK_COL[i]
            val stem = if (quiet) Color(0xFF5A6050) else Color(0xFF389C4C)
            val u = hgt * 0.012f                                 // a "pixel" of the device's picture
            if (bloom > 0.05f && !quiet) drawCircle(col.copy(alpha = 0.15f * bloom), u * 9, tip)
            fun stalk() = drawLine(stem, Offset(x, ground), tip, u * 0.9f, StrokeCap.Round)
            when (i) {
                0 -> { stalk()                                   // moonflower
                    val r = u * (5 + 3 * f)
                    for (k in 0 until 5) rotate(k * 72f + (t * 8).toFloat(), tip) {
                        drawCircle(lerp(Color(0xFFECECFF), col, 0.35f), r * 0.6f, Offset(tip.x, tip.y - r * 0.7f))
                    }
                    drawCircle(Color(0xFFFFE278), r * 0.4f, tip) }
                1 -> { stalk()                                   // fern
                    val n = 10
                    for (k in 1 until n) {
                        val py = ground - h * k / n
                        val px = x + sway * k / n
                        val len = u * (2 + 5f * (n - k) / n)
                        val c = if (f > 0.1f && k >= n * (1 - f)) col else stem
                        drawLine(c, Offset(px, py), Offset(px - len, py - len / 2), u * 0.6f)
                        drawLine(c, Offset(px, py), Offset(px + len, py - len / 2), u * 0.6f)
                    }
                    drawCircle(stem, u * (3 - 2 * f), tip + Offset(u * (3 - 2 * f), 0f), style = Stroke(u * 0.6f)) }
                2 -> { stalk()                                   // tulip
                    val o = u * 3 * f
                    drawOval(col, tip - Offset(u * 4, u * 5), Size(u * 8, u * 10))
                    drawOval(col, tip - Offset(u * 6 + o, u * 7 + o / 2), Size(u * 3, u * 7))
                    drawOval(col, tip + Offset(u * 3 + o, -u * 7 - o / 2), Size(u * 3, u * 7)) }
                3 -> { stalk()                                   // lavender
                    for (k in 0 until 7) {
                        val lit = 7 - k <= (f * 7).toInt()
                        drawCircle(if (lit) lerp(Color(0xFFBE96FF), col, 0.3f) else Color(0xFF765CBA), u * 1.8f,
                            tip + Offset(if (k % 2 == 0) -u * 2 else u * 2, u * (3 + k * 3)))
                    } }
                4 -> { drawLine(stem, Offset(x, ground), tip, u * 0.6f)   // dandelion
                    for (k in 0 until 12) rotate(k * 30f, tip) {
                        drawCircle(lerp(Color(0xFFE6E6F0), col, 0.3f), u * 0.6f, tip + Offset(0f, -u * 6))
                    }
                    drawCircle(col, u * 1.6f, tip)
                    if (f > 0f) for (k in 0 until 3) drawCircle(Color(0xFFE6E6F0), u * 0.7f,
                        tip + Offset(u * (k * 5 - 4), -u * 6 - (1 - f) * u * (12 + k * 6))) }
                5 -> { stalk()                                   // bluebell
                    val sw = (sin(t * 12) * f * u * 2).toFloat()
                    drawLine(stem, tip, tip + Offset(u * 10, u * 4), u * 0.8f)
                    for (k in 0 until 3) drawOval(col, tip + Offset(u * (1 + k * 4) + sw, u * (2 + k * 2)), Size(u * 4, u * 5)) }
                6 -> for (k in 0 until 3) {                      // mushrooms
                    val mh = h * 0.25f * (3 - k) / 3 + u * 6
                    val mx = x + (k - 1) * u * 9
                    val r = u * (6 - k)
                    if (f > 0f) drawCircle(col.copy(alpha = 0.3f * f), r * 1.8f, Offset(mx, ground - mh))
                    drawLine(Color(0xFFDCD2BE), Offset(mx, ground), Offset(mx, ground - mh), u * 1.6f)
                    drawOval(lerp(col, Color.White, f * 0.4f), Offset(mx - r, ground - mh - r / 2), Size(r * 2, r))
                }
                else -> for (k in 0 until 3) {                   // cattails
                    val rh = h * (1f - k * 0.2f)
                    val bob = if (k == 1) u * 3 * f else -u * 1.5f * f
                    val top2 = Offset(x + (k - 1) * u * 5 + sway * (1f - k * 0.2f) + bob, ground - rh)
                    drawLine(stem, Offset(x + (k - 1) * u * 5, ground), top2, u * 0.7f)
                    drawOval(lerp(Color(0xFF784828), col, 0.3f), top2 + Offset(-u * 1.8f, u * 1.5f), Size(u * 3.6f, u * 9))
                }
            }
            if (tracks?.selected == i) drawCircle(Color.White.copy(alpha = 0.5f), u * 9, tip, style = Stroke(1.5f))
        }
    }
}
