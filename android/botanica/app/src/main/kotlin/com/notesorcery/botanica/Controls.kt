// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.roundToInt

/**
 * A big vertical fader, Move-style: drag anywhere on it (relative, so a touch never jumps the value), its colour the
 * knob's. TalkBack reads it as an adjustable control (swipe up / down).
 */
@Composable
fun Fader(label: String, value: Int, max: Int, color: Color, modifier: Modifier = Modifier, min: Int = 0,
          shown: (Int) -> String = { "${(it - min) * 100 / maxOf(1, max - min)}%" }, onChange: (Int) -> Unit) {
    val p = LocalPalette.current
    val cur by rememberUpdatedState(value)
    var acc by remember { mutableFloatStateOf(0f) }
    var h by remember { mutableFloatStateOf(1f) }
    val frac = ((value - min).toFloat() / maxOf(1, max - min)).coerceIn(0f, 1f)
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, color = color, fontWeight = FontWeight.Bold, fontSize = 15.sp, maxLines = 1)
        Box(
            Modifier
                .padding(vertical = 6.dp)
                .fillMaxWidth()
                .weight(1f)
                .clip(RoundedCornerShape(18.dp))
                .background(p.surface)
                .border(2.dp, color.copy(alpha = 0.5f), RoundedCornerShape(18.dp))
                .semantics {
                    contentDescription = label
                    stateDescription = shown(value)
                    progressBarRangeInfo = ProgressBarRangeInfo(value.toFloat(), min.toFloat()..max.toFloat())
                    setProgress { v -> onChange(v.roundToInt().coerceIn(min, max)); true }
                }
                .pointerInput(max, min) {
                    h = size.height.toFloat()
                    detectVerticalDragGestures(onDragStart = { acc = cur.toFloat() }) { change, dy ->
                        change.consume()
                        acc = (acc - dy / h * (max - min) * 1.2f).coerceIn(min.toFloat(), max.toFloat())
                        val v = acc.roundToInt()
                        if (v != cur) onChange(v)
                    }
                },
            contentAlignment = Alignment.BottomCenter,
        ) {
            Box(Modifier.fillMaxWidth().fillMaxHeight(frac).background(color.copy(alpha = 0.85f)))
            Text(shown(value), Modifier.align(Alignment.Center), color = p.text, fontSize = 18.sp, fontWeight = FontWeight.Bold)
        }
    }
}

/** a button that is on while it is held (FREEZE, GLITCH, BREAK), with a tick of haptics on each edge */
@Composable
fun HoldButton(label: String, color: Color, modifier: Modifier = Modifier, onHold: (Boolean) -> Unit) {
    val p = LocalPalette.current
    val haptic = LocalHapticFeedback.current
    var down by remember { mutableStateOf(false) }
    Box(
        modifier
            .heightIn(min = 56.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(if (down) color else p.surface)
            .border(2.dp, color, RoundedCornerShape(16.dp))
            .semantics { contentDescription = "$label, hold" }
            .pointerInput(Unit) {
                awaitEachGesture {
                    awaitFirstDown()
                    down = true; haptic.performHapticFeedback(HapticFeedbackType.LongPress); onHold(true)
                    waitForUpOrCancellation()
                    down = false; haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove); onHold(false)
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (down) p.ground else color, fontWeight = FontWeight.Bold, fontSize = 16.sp)
    }
}

@Composable
fun Heading(text: String, sub: String? = null) {
    val p = LocalPalette.current
    Column(Modifier.fillMaxWidth().padding(bottom = 8.dp)) {
        Text(text, style = MaterialTheme.typography.headlineSmall, color = p.text, fontWeight = FontWeight.Bold)
        if (sub != null) Text(sub, style = MaterialTheme.typography.bodyMedium, color = p.dim)
    }
}

/** the screen's note when there is no FM-1 to talk to */
@Composable
fun NeedsDevice(link: Link, what: String) {
    val p = LocalPalette.current
    Box(Modifier.fillMaxSize().padding(24.dp), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(what, color = p.text, textAlign = TextAlign.Center, style = MaterialTheme.typography.titleMedium)
            Text(link.message, color = p.dim, textAlign = TextAlign.Center)
        }
    }
}
