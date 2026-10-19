// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.notesorcery.botanica.core.Btp1
import com.notesorcery.botanica.core.Moods
import java.util.concurrent.ConcurrentHashMap

/**
 * The sounds by mood, as the device's browser groups them: pick a track, a mood, tap a sound to hear it on that track
 * (PRESET). The star keeps a favourite on the phone; FAVOURITES lists them.
 */
@Composable
fun MoodsScreen(link: Link) {
    val p = LocalPalette.current
    if (link.state != Link.State.CONNECTED) { NeedsDevice(link, "Browse BOTANICA's sounds by mood once the FM-1 is connected."); return }
    val prefs = LocalContext.current.getSharedPreferences("garden", Context.MODE_PRIVATE)
    var favs by remember { mutableStateOf(prefs.getStringSet("favs", emptySet())!!.toSet()) }
    var mood by remember { mutableStateOf<Moods.Mood?>(Moods.Mood.PETALS) }
    val names = remember { ConcurrentHashMap<Int, List<String>>() }
    val sel = link.tracks?.selected ?: 0
    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Heading("Moods", "Tap a sound to hear it on the selected track.")
        TrackRow(link, synthsOnly = true)
        LazyRow(Modifier.padding(vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(Moods.Mood.entries) { m ->
                FilterChip(selected = mood == m, onClick = { mood = m }, label = { Text(m.label) },
                    colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.knob[m.ordinal % 4], selectedLabelColor = p.ground))
            }
            item {
                FilterChip(selected = mood == null, onClick = { mood = null }, label = { Text("★ Favourites") },
                    colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.knob[3], selectedLabelColor = p.ground))
            }
        }
        mood?.let { Text(it.blurb, color = p.dim, modifier = Modifier.padding(bottom = 6.dp)) }
        val list = if (mood == null) Moods.PRESETS.filter { it.name in favs } else Moods.of(mood!!)
        val cur = link.tracks?.tracks?.getOrNull(sel)
        LazyVerticalGrid(GridCells.Adaptive(150.dp), Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(list, key = { it.name }) { pr ->
                val color = p.knob[pr.engine % 4]
                val on = cur != null && cur.engine == pr.engine && names[pr.engine]?.getOrNull(cur.preset)?.trim().equals(pr.name, true)
                Row(
                    Modifier
                        .heightIn(min = 64.dp)
                        .clip(RoundedCornerShape(14.dp))
                        .background(if (on) color else p.surface)
                        .border(2.dp, color.copy(alpha = 0.6f), RoundedCornerShape(14.dp))
                        .clickable {
                            link.run("Sound") { g ->
                                val n = names.getOrPut(pr.engine) { g.names(pr.engine) }
                                val i = Moods.indexIn(pr, n) ?: throw IllegalStateException("this firmware has no ${pr.name}")
                                g.preset(pr.engine, i)
                                link.selectTrack(sel)
                            }
                        }
                        .padding(start = 12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(pr.name, color = if (on) p.ground else p.text, fontWeight = FontWeight.Bold)
                        Text(Btp1.Layout.ENGINES.getOrElse(pr.engine) { "" }, color = if (on) p.ground else p.dim, fontSize = 12.sp)
                    }
                    val fav = pr.name in favs
                    Box(
                        Modifier.size(48.dp).semantics { contentDescription = if (fav) "Remove ${pr.name} from favourites" else "Favourite ${pr.name}" }
                            .clickable {
                                favs = if (fav) favs - pr.name else favs + pr.name
                                prefs.edit().putStringSet("favs", favs).apply()
                            },
                        contentAlignment = Alignment.Center,
                    ) { Text(if (fav) "★" else "☆", color = if (on) p.ground else p.knob[3], fontSize = 22.sp) }
                }
            }
        }
    }
}

/** T1..T8 (or the six synth tracks): the selected one lit; a tap selects (TRACK) */
@Composable
fun TrackRow(link: Link, synthsOnly: Boolean = false) {
    val p = LocalPalette.current
    val t = link.tracks
    val n = if (synthsOnly) 6 else (t?.tracks?.size ?: 8)
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        for (i in 0 until n) {
            val on = t?.selected == i
            val eng = t?.tracks?.getOrNull(i)?.engine
            Box(
                Modifier.weight(1f).heightIn(min = 48.dp).clip(RoundedCornerShape(10.dp))
                    .background(if (on) p.knob[0] else p.surface)
                    .semantics { contentDescription = "Track ${i + 1}" + if (on) ", selected" else "" }
                    .clickable { link.selectTrack(i) },
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("T${i + 1}", color = if (on) p.ground else p.text, fontWeight = FontWeight.Bold)
                    Text(if (i >= 6) "DRUM" else Btp1.Layout.ENGINES.getOrElse(eng ?: -1) { "" }.take(5),
                        color = if (on) p.ground else p.dim, fontSize = 10.sp, maxLines = 1)
                }
            }
        }
    }
}
