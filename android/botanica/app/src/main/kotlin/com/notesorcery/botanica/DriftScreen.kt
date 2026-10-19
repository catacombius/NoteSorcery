// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.notesorcery.botanica.core.DriftPlan

/**
 * Follows the song with a [DriftPlan]: at each bar the FM-1 plays (its transport's beat), the DRIFT and RAMP the plan
 * calls for, a bar ahead, so the firmware makes the ramp on its own clock (firmware/src/bot_seq.c).
 */
class DriftRunner(private val link: Link) {
    var plan by mutableStateOf(DriftPlan.ARC)
    var running by mutableStateOf(false)
        private set
    private var lastBar = -1L

    fun start() {
        running = true
        lastBar = -1
        val first = plan.bpm[0]
        link.run("Drift") { g -> g.client.tempo(first); g.set("DRIFT", 0) }
        link.onBeat = { beat -> tick(beat) }
    }

    fun stop() {
        running = false
        link.onBeat = {}
        link.setGlobal("DRIFT", 0)
    }

    /** on the link's worker, with each transport read */
    private fun tick(beat: Long) {
        val bar = beat / 4
        if (bar == lastBar) return
        lastBar = bar
        val cue = plan.cueAt(bar.toInt()) ?: return
        val g = link.garden ?: return
        try { g.set("RAMP", cue.second); g.set("DRIFT", cue.first) } catch (_: Exception) { }
    }
}

@Composable
fun DriftScreen(link: Link, runner: DriftRunner) {
    val p = LocalPalette.current
    val plan = runner.plan
    fun edit(f: (DriftPlan) -> DriftPlan) { runner.plan = try { f(plan) } catch (_: IllegalArgumentException) { plan } }
    Column(Modifier.fillMaxSize().padding(16.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Heading("Tempo drift", "Ambient sections that slam into hyperpop: a tempo per section, the ramp between them.")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            AssistChip({ runner.plan = DriftPlan.ARC }, { Text("ARC 72·96·160·80") })
            AssistChip({ runner.plan = DriftPlan(listOf(60, 160), 8, 0) }, { Text("SLAM 60→160") })
            AssistChip({ runner.plan = DriftPlan(listOf(80, 160), 8, 2) }, { Text("HALF/DOUBLE") })
        }
        val bar = if (runner.running && link.playing) (link.beat / 4.0) % plan.bars else -1.0
        Canvas(Modifier.fillMaxWidth().height(180.dp).semantics {
            contentDescription = "Tempo curve: " + plan.bpm.joinToString(", ") { "$it BPM" }
        }) {
            val pts = plan.curve(8)
            fun x(b: Double) = (b / plan.bars * size.width).toFloat()
            fun y(v: Double) = (size.height * (1 - (v - 30) / 200)).toFloat()
            for (s in plan.bpm.indices) {
                drawRect(if (s % 2 == 0) p.surface else p.line.copy(alpha = 0.4f), Offset(x(s * plan.barsPerSection.toDouble()), 0f),
                    androidx.compose.ui.geometry.Size(x(plan.barsPerSection.toDouble()), size.height))
            }
            val path = Path()
            pts.forEachIndexed { i, (b, v) -> if (i == 0) path.moveTo(x(b), y(v)) else path.lineTo(x(b), y(v)) }
            drawPath(path, p.knob[2], style = Stroke(5f))
            if (bar >= 0) drawLine(p.knob[3], Offset(x(bar), 0f), Offset(x(bar), size.height), 4f)
        }
        Row(Modifier.fillMaxWidth()) {
            plan.bpm.forEachIndexed { i, v -> Text("${"ABCD"[i]}: $v", Modifier.weight(1f), color = p.knob[i], fontWeight = FontWeight.Bold) }
        }
        plan.bpm.forEachIndexed { i, v ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("${"ABCD"[i]}", color = p.knob[i], fontWeight = FontWeight.Bold, modifier = Modifier.padding(end = 12.dp))
                Slider(v.toFloat(), { nv -> edit { pl -> pl.copy(bpm = pl.bpm.toMutableList().also { it[i] = nv.toInt() }) } },
                    valueRange = 40f..200f, modifier = Modifier.weight(1f).semantics { contentDescription = "Section ${"ABCD"[i]} tempo" })
                Text("$v BPM", color = p.text, modifier = Modifier.padding(start = 8.dp))
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            AssistChip({ edit { it.copy(bpm = it.bpm.dropLast(1)) } }, { Text("− section") }, enabled = plan.bpm.size > 1)
            AssistChip({ edit { it.copy(bpm = it.bpm + it.bpm.last()) } }, { Text("+ section") }, enabled = plan.bpm.size < 4)
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("BARS", color = p.dim, modifier = Modifier.padding(end = 12.dp))
            Slider(plan.barsPerSection.toFloat(), { v -> edit { it.copy(barsPerSection = v.toInt()) } }, valueRange = 1f..32f,
                modifier = Modifier.weight(1f).semantics { contentDescription = "Bars a section" })
            Text("${plan.barsPerSection} a section", color = p.text, modifier = Modifier.padding(start = 8.dp))
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("RAMP", color = p.dim, modifier = Modifier.padding(end = 12.dp))
            Slider(plan.rampBars.toFloat(), { v -> edit { it.copy(rampBars = v.toInt()) } }, valueRange = 0f..16f,
                modifier = Modifier.weight(1f).semantics { contentDescription = "Ramp, bars" })
            Text(if (plan.rampBars == 0) "jump" else "${plan.rampBars} bars", color = p.text, modifier = Modifier.padding(start = 8.dp))
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Follow the song", color = p.text, fontWeight = FontWeight.Bold)
                Text(if (link.state == Link.State.CONNECTED) "Sends DRIFT and RAMP to the FM-1 as it plays." else "Connect the FM-1 to follow it.",
                    color = p.dim)
            }
            Switch(runner.running, { on -> if (on) runner.start() else runner.stop() }, enabled = link.state == Link.State.CONNECTED,
                modifier = Modifier.semantics { contentDescription = "Follow the song" })
        }
    }
}
