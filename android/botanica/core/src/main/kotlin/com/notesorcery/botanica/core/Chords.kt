// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

/**
 * BOTANICA's petal chords and progressions, as the firmware plays them (firmware/src/seq.c CHORD_SEMI and
 * CHORD_PROG): the phone's chord pads play the same notes the FM-1's keys do.
 */
object Chords {
    enum class Kind(val label: String, val semis: IntArray) {
        MAJ7("MAJ7", intArrayOf(0, 4, 7, 11)),
        MAJ9("MAJ9", intArrayOf(0, 4, 11, 14)),        // 1 3 7 9: no 5th, spread
        ADD9("ADD9", intArrayOf(0, 4, 7, 14)),
        SIX9("6/9", intArrayOf(0, 4, 9, 14)),
        LYD11("LYD#11", intArrayOf(0, 4, 11, 18)),     // maj7 #11
    }

    /** one chord of a progression: its root (semitones above the key) and its notes above that root */
    class Step(val degree: Int, val semis: IntArray, val name: String)

    enum class Progression(val label: String, val steps: List<Step>) {
        ROYAL("ROYAL", listOf(Step(5, intArrayOf(0, 4, 7, 11), "IVmaj7"), Step(7, intArrayOf(0, 4, 7, 10), "V7"),
            Step(4, intArrayOf(0, 3, 7, 10), "iii7"), Step(9, intArrayOf(0, 3, 7, 10), "vi7"))),
        PETAL("PETAL", listOf(Step(5, intArrayOf(0, 4, 7, 11), "IVmaj7"), Step(7, intArrayOf(0, 4, 7, 14), "Vadd9"),
            Step(9, intArrayOf(0, 3, 10, 14), "vi9"), Step(0, intArrayOf(0, 4, 7, 14), "Iadd9"))),
    }

    /** a petal chord on [note] */
    fun chord(kind: Kind, note: Int): List<Int> = kind.semis.map { note + it }.filter { it in 0..127 }

    /**
     * chord [pos] of [prog] in the key of [root] (0 = C), its root the one nearest [near] (up to a tritone up or
     * down), as seq.c chord_notes places it
     */
    fun progression(prog: Progression, root: Int, pos: Int, near: Int = 60): List<Int> {
        val step = prog.steps[Math.floorMod(pos, prog.steps.size)]
        val r0 = Math.floorMod(root + step.degree, 12)
        val d = Math.floorMod(r0 - near, 12)
        val r = near + if (d > 6) d - 12 else d
        return step.semis.map { r + it }.filter { it in 0..127 }
    }

    /** the note names of a chord, C4 = 60 */
    private val NAMES = listOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
    fun noteName(n: Int): String = NAMES[Math.floorMod(n, 12)] + (n / 12 - 1)

    /**
     * the times (ms after the first) a strummed chord's notes start: [ms] apart, from the lowest (up), the highest
     * (down) or in a random order, as the firmware's STRUM / ORDER do
     */
    fun strumOffsets(notes: List<Int>, ms: Int, order: Int, random: java.util.Random = java.util.Random()): List<Pair<Int, Int>> {
        val sorted = when (order) {
            1 -> notes.sortedDescending()
            2 -> notes.shuffled(random)
            else -> notes.sorted()
        }
        return sorted.mapIndexed { i, n -> n to i * ms }
    }
}
