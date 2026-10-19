// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

import com.notesorcery.fm1link.SampleSlot
import com.notesorcery.fm1link.SlotZone

/**
 * A field recording made into a sample slot (USR1..USR4) for the FM-1's SAMPLE engine: the field recorder's chain in
 * order (trim the silence, reverse, stretch into a drone, loop crossfade, fades), at the slot's 22 050 Hz, cut to what a
 * slot holds (about 5.9 s), played from [root].
 */
data class FieldSample(
    val trim: Boolean = true,
    val reverse: Boolean = false,
    /** PaulStretch factor: 1 = as recorded */
    val stretch: Int = 1,
    val loop: Boolean = false,
    val root: Int = 60,
) {
    /** the processed sound at the slot's rate, cut to fit */
    fun process(x: FloatArray, rate: Int): FloatArray {
        var y = x
        if (trim) y = Dsp.trimSilence(y, rate)
        if (y.isEmpty()) return y
        y = SampleSlot.resample(y, rate, SampleSlot.RATE)
        if (reverse) y = Dsp.reverse(y)
        if (stretch > 1) {
            val keep = MAX_SAMPLES / stretch + 4096                   // (no use stretching more than fits)
            if (y.size > keep) y = y.copyOf(keep)
            y = Dsp.paulStretch(y, stretch.toDouble(), 4096)
        }
        val cap = if (loop) MAX_SAMPLES + SampleSlot.RATE * 120 / 1000 else MAX_SAMPLES
        if (y.size > cap) y = y.copyOf(cap)
        y = if (loop) Dsp.loopCrossfade(y, SampleSlot.RATE, 120) else Dsp.fade(y, SampleSlot.RATE, 8)
        if (y.size > MAX_SAMPLES) y = y.copyOf(MAX_SAMPLES)
        return Dsp.normalize(y, -1.0)
    }

    fun slot(name: String, x: FloatArray, rate: Int): SampleSlot {
        val y = process(x, rate)
        require(y.size >= 64) { "nothing above the silence to keep" }
        return SampleSlot.build(name, listOf(SlotZone(SampleSlot.toInt16(y), root)))
    }

    companion object {
        /** what a slot holds: IMA ADPCM is 4 bits a sample */
        val MAX_SAMPLES = SampleSlot.MAX_DATA * 2 - 512
    }
}
