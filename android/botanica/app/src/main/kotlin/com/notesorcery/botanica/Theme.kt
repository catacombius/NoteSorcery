// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

/**
 * The firmware's palettes (firmware/src/gfx.c MOSS, CONTRAST) and its knob colours (ui_studio.c): as on the OP-1,
 * a colour always means the same knob. Petal pink is knob 1, leaf green knob 2, sky blue knob 3, pollen yellow knob 4.
 */
data class Palette(
    val ground: Color, val surface: Color, val line: Color, val text: Color, val dim: Color,
    val knob: List<Color>, val sky: Color, val night: Color,
)

val MOSS = Palette(
    ground = Color(0xFF10201A), surface = Color(0xFF1A3327), line = Color(0xFF2E4D3C), text = Color(0xFFF1EEDF),
    dim = Color(0xFF9DB3A4),
    knob = listOf(Color(0xFFF48FB1), Color(0xFF9CCC65), Color(0xFF81D4FA), Color(0xFFFFE082)),
    sky = Color(0xFF3B6B8F), night = Color(0xFF14243A),
)

val CONTRAST = Palette(
    ground = Color.Black, surface = Color(0xFF111111), line = Color.White, text = Color.White, dim = Color(0xFFDDDDDD),
    knob = listOf(Color(0xFFFF66CC), Color(0xFF66FF66), Color(0xFF66CCFF), Color(0xFFFFFF00)),
    sky = Color(0xFF0033AA), night = Color.Black,
)

val LocalPalette = staticCompositionLocalOf { MOSS }

@Composable
fun BotanicaTheme(contrast: Boolean, content: @Composable () -> Unit) {
    val p = if (contrast) CONTRAST else MOSS
    val scheme = darkColorScheme(
        primary = p.knob[0], onPrimary = p.ground, secondary = p.knob[1], onSecondary = p.ground,
        tertiary = p.knob[2], background = p.ground, onBackground = p.text, surface = p.surface, onSurface = p.text,
        surfaceVariant = p.surface, onSurfaceVariant = p.dim, outline = p.line, secondaryContainer = p.line,
        onSecondaryContainer = p.text,
    )
    androidx.compose.runtime.CompositionLocalProvider(LocalPalette provides p) {
        MaterialTheme(colorScheme = scheme, content = content)
    }
}
