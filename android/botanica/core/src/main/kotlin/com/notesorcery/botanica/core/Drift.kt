// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

/**
 * The tempo-drift planner: a tempo for each song section A..D, each [barsPerSection] bars long, and the ramp
 * between them ([rampBars], as the firmware's RAMP: 0 = a jump on the bar). The phone draws the curve, and while the
 * FM-1 plays it sends each coming section's tempo as DRIFT and the ramp as RAMP, one bar ahead of the section
 * change. The firmware then makes the ramp itself, on its own clock (firmware/src/bot_seq.c bot_drift).
 */
data class DriftPlan(val bpm: List<Int>, val barsPerSection: Int = 8, val rampBars: Int = 4) {
    init {
        require(bpm.isNotEmpty() && bpm.size <= 4) { "one to four sections" }
        require(barsPerSection in 1..64 && rampBars in 0..16)
    }

    val bars: Int get() = bpm.size * barsPerSection

    /** the section [bar] (from 0, looping) is in */
    fun sectionAt(bar: Int): Int = Math.floorMod(bar, bars) / barsPerSection

    /**
     * the tempo at [bar] (a fraction of a bar allowed), as the FM-1 plays the plan: each section starts at its
     * tempo; the ramp to the next one begins [rampBars] before the section change (or at the change, for 0)
     */
    fun bpmAt(bar: Double): Double {
        val b = ((bar % bars) + bars) % bars
        val s = (b / barsPerSection).toInt().coerceIn(0, bpm.size - 1)
        val next = bpm[(s + 1) % bpm.size]
        val into = b - s * barsPerSection
        val ramp = rampBars.coerceAtMost(barsPerSection).toDouble()
        val start = barsPerSection - ramp
        if (ramp == 0.0 || into < start) return bpm[s].toDouble()
        return bpm[s] + (next - bpm[s]) * (into - start) / ramp
    }

    /** points to draw the curve, [perBar] a bar */
    fun curve(perBar: Int = 4): List<Pair<Double, Double>> =
        (0..bars * perBar).map { val x = it.toDouble() / perBar; x to bpmAt(x) }

    /**
     * what to send at the start of [bar] (from 0): (DRIFT, RAMP) when the ramp to the next section starts there,
     * else null. The firmware ramps over RAMP bars from the next bar, so it is sent a bar before the ramp starts.
     */
    fun cueAt(bar: Int): Pair<Int, Int>? {
        val b = Math.floorMod(bar, bars)
        val ramp = rampBars.coerceAtMost(barsPerSection)
        for (k in bpm.indices) {                        // section k ramps to k + 1 over its last RAMP bars
            val cue = Math.floorMod(k * barsPerSection + barsPerSection - ramp - 1, bars)
            if (b == cue) return bpm[(k + 1) % bpm.size] to ramp
        }
        return null
    }

    companion object {
        /** the botanica arc: an ambient intro, a lift, the drop into hyperpop, and back down */
        val ARC = DriftPlan(listOf(72, 96, 160, 80), barsPerSection = 8, rampBars = 4)
    }
}
