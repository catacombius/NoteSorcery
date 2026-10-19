// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * BOTANICA Garden: the phone beside a BOTANICA FM-1. Seven screens, OP-1 / Move style, big and colour-coded; the
 * connection lives as long as the activity (it is reopened when the FM-1 is plugged in).
 */
class MainActivity : ComponentActivity() {
    private lateinit var link: Link
    private lateinit var drift: DriftRunner

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        link = Link(applicationContext)                               // (rotation does not recreate: configChanges)
        drift = DriftRunner(link)
        val prefs = getSharedPreferences("garden", Context.MODE_PRIVATE)
        setContent {
            var contrast by rememberSaveable { mutableStateOf(prefs.getBoolean("contrast", false)) }
            BotanicaTheme(contrast) {
                App(link, drift, contrast) { contrast = it; prefs.edit().putBoolean("contrast", it).apply() }
            }
        }
        if (link.state == Link.State.IDLE) link.connect()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (link.state != Link.State.CONNECTED && link.state != Link.State.SEARCHING) link.connect()   // (the FM-1 was plugged in)
    }

    override fun onDestroy() {
        if (isFinishing) link.close()
        super.onDestroy()
    }
}

private enum class Screen(val label: String, val glyph: String) {
    GARDEN("Garden", "✿"), MOODS("Moods", "❀"), PADS("Pads", "♪"), DRIFT("Drift", "∿"), FIELD("Field", "●"),
    PROJECTS("Projects", "▤"), CONNECT("FM-1", "⏻"),
}

@Composable
private fun App(link: Link, drift: DriftRunner, contrast: Boolean, setContrast: (Boolean) -> Unit) {
    val p = LocalPalette.current
    var screen by rememberSaveable { mutableIntStateOf(if (link.state == Link.State.CONNECTED) 0 else Screen.CONNECT.ordinal) }
    Scaffold(
        containerColor = p.ground,
        bottomBar = {
            NavigationBar(containerColor = p.surface) {
                Screen.entries.forEach { s ->
                    NavigationBarItem(
                        selected = screen == s.ordinal,
                        onClick = { screen = s.ordinal },
                        icon = { Text(s.glyph, fontSize = 20.sp) },
                        label = { Text(s.label, maxLines = 1, fontSize = 11.sp) },
                        colors = NavigationBarItemDefaults.colors(indicatorColor = p.line, selectedTextColor = p.knob[0],
                            selectedIconColor = p.knob[0], unselectedTextColor = p.dim, unselectedIconColor = p.dim),
                        modifier = Modifier.semantics { contentDescription = s.label },
                    )
                }
            }
        },
    ) { pad ->
        Column(Modifier.fillMaxSize().padding(pad)) {
            when (Screen.entries[screen]) {
                Screen.GARDEN -> GardenScreen(link)
                Screen.MOODS -> MoodsScreen(link)
                Screen.PADS -> PadsScreen(link)
                Screen.DRIFT -> DriftScreen(link, drift)
                Screen.FIELD -> FieldScreen(link)
                Screen.PROJECTS -> ProjectsScreen(link)
                Screen.CONNECT -> ConnectScreen(link, contrast, setContrast) { screen = Screen.GARDEN.ordinal }
            }
        }
    }
}

/** what the phone is connected to, the other MIDI devices, the installer, the high-contrast theme */
@Composable
private fun ConnectScreen(link: Link, contrast: Boolean, setContrast: (Boolean) -> Unit, toGarden: () -> Unit) {
    val p = LocalPalette.current
    val ctx = androidx.compose.ui.platform.LocalContext.current
    Column(Modifier.fillMaxSize().padding(16.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Heading("BOTANICA Garden", "The companion of the BOTANICA firmware for the M-VAVE FM-1.")
        val (word, color) = when (link.state) {
            Link.State.CONNECTED -> "Connected" to p.knob[1]
            Link.State.SEARCHING -> "Connecting…" to p.knob[3]
            Link.State.NOT_BOTANICA -> "Not BOTANICA" to p.knob[0]
            Link.State.ERROR -> "No connection" to p.knob[0]
            Link.State.IDLE -> "Not connected" to p.dim
        }
        Text(word, color = color, fontSize = 26.sp, fontWeight = FontWeight.Bold)
        Text(link.message, color = p.text)
        link.device?.let { d ->
            val c = d.caps
            Text(buildString {
                append("Firmware: ${d.firmware}\n")
                if (c != null) append("Tracks: ${c.synthTracks} synth + ${c.drumTracks} drum · project ${c.projectSize} bytes · drums on channel ${c.drumChannel}\n")
                append("Engines: ${d.engines.joinToString(" ")}")
            }, color = p.dim)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button({ link.connect() }, Modifier.heightIn(min = 48.dp)) { Text(if (link.state == Link.State.CONNECTED) "Reconnect" else "Connect") }
            if (link.state == Link.State.CONNECTED) OutlinedButton(toGarden, Modifier.heightIn(min = 48.dp)) { Text("To the garden") }
        }
        val others = link.candidates()
        if (others.size > 1) {
            Text("Other MIDI devices", color = p.text, fontWeight = FontWeight.Bold)
            others.forEach { (info, name) ->
                OutlinedButton({ link.connect(info) }, Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text(name) }
            }
        }
        Text("Installing or updating BOTANICA is done from the web installer in Chrome on a computer (Web MIDI); the app " +
            "never writes firmware.", color = p.dim)
        OutlinedButton({
            ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(INSTALLER)))
        }, Modifier.heightIn(min = 48.dp)) { Text("Open the web installer") }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("High contrast", color = p.text, fontWeight = FontWeight.Bold)
                Text("Black, white and bright knob colours (the firmware's CONTRAST palette).", color = p.dim)
            }
            Switch(contrast, setContrast, modifier = Modifier.semantics { contentDescription = "High contrast" })
        }
    }
}

/** the installer page CI publishes (web/make_site.py --edition botanica) */
private const val INSTALLER = "https://catacombius.github.io/NoteSorcery/botanica/"
