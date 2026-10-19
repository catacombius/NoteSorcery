// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

import com.notesorcery.fm1link.Fm1Clip
import com.notesorcery.fm1link.Fm1Song
import com.notesorcery.fm1link.Fm1Track
import com.notesorcery.fm1link.Nsp1
import com.notesorcery.fm1link.Nsp1Project
import com.notesorcery.fm1link.Nsp1Track
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * BOTANICA's project format (BTP1: firmware/src/project.c built with EDITION_BOTANICA). It is NSP1 with three more
 * track parameters before P_E0 (STRDIR, HUMAN, RATMODE), nine more globals (the PERFORM and SKY macros) and 2 bytes of
 * padding before the sum. The steps and every parameter the notes need sit where NSP1 has them, so a decoded BTP1
 * project is an [Nsp1Project] and [Nsp1.trackClip] turns its steps into the notes BOTANICA plays (web/als/nsals.js
 * LAYOUT_BOTANICA does the same in the browser).
 */
object Btp1 {
    object Layout {
        const val MAGIC = 0x31505442          // "BTP1"
        const val SIZE = 7672
        const val P_COUNT = 64
        const val G_COUNT = 43
        const val NTRK = 8
        const val NPART = 6
        const val NSTEP = 64
        const val NLOCK = 24
        const val P_E0 = 56
        /** the PERFORM / SKY globals (core.h G_BLOOM ..): their indices in a project's globals */
        const val G_BLOOM = 34
        const val G_DROPS = 35
        const val G_FOLEY = 36
        const val G_GLITCH = 37
        const val G_WEATHER = 38
        const val G_BREEZE = 39
        const val G_DRIFT = 40
        const val G_RAMP = 41
        const val G_BMODE = 42
        val ENGINES = listOf("FELT", "DROPLET", "ANALOG", "SAMPLE", "NOISE", "PHYS", "GRAIN", "VOICE", "WAVE")
    }

    fun isBtp1(bytes: ByteArray?): Boolean =
        bytes != null && bytes.size >= 4 && ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).getInt(0) == Layout.MAGIC

    fun decode(bytes: ByteArray): Nsp1Project {
        val L = Layout
        if (bytes.size != L.SIZE) throw Nsp1.FormatException("not a BTP1 project (${bytes.size} bytes, expected ${L.SIZE})")
        val bb = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (bb.getInt(0) != L.MAGIC || bb.getInt(4) != L.SIZE) throw Nsp1.FormatException("not a BTP1 project (magic / size)")
        if (bb.getInt(L.SIZE - 4) != Nsp1.fnv1a(bytes, L.SIZE - 4)) throw Nsp1.FormatException("BTP1 project damaged (checksum)")
        var o = 8
        val g = ShortArray(L.G_COUNT) { bb.getShort(o + 2 * it) }
        o += 2 * L.G_COUNT
        val sel = bytes[o].toInt() and 0xFF
        val ntrk = bytes[o + 2].toInt() and 0xFF
        o += 4
        if (ntrk != L.NTRK) throw Nsp1.FormatException("BTP1 with $ntrk tracks (expected ${L.NTRK})")
        val tracks = ArrayList<Nsp1Track>(L.NTRK)
        for (t in 0 until L.NTRK) {
            val p = ShortArray(L.P_COUNT) { bb.getShort(o + 2 * it) }
            o += 2 * L.P_COUNT
            val engine = bytes[o].toInt() and 0xFF
            val preset = bytes[o + 1].toInt() and 0xFF
            o += 2
            val steps = Array(L.NSTEP) { bytes.copyOfRange(o + 10 * it, o + 10 * it + 10) }
            o += 10 * L.NSTEP
            val micro = bytes.copyOfRange(o, o + L.NSTEP)
            o += L.NSTEP + 4 * L.NLOCK
            val fill = bytes.copyOfRange(o, o + L.NSTEP / 4)
            o += L.NSTEP / 4
            tracks.add(Nsp1Track(t, p, engine, preset, steps, micro, fill, t >= L.NPART))
        }
        if ((o + 3) and 3.inv() != L.SIZE - 4) throw Nsp1.FormatException("BTP1 layout mismatch")
        return Nsp1Project(g, sel, tracks)
    }

    /** a project's bytes, either edition's: BTP1 here, NSP1 through fm1link */
    fun decodeAny(bytes: ByteArray): Nsp1Project = if (isBtp1(bytes)) decode(bytes) else Nsp1.decode(bytes)

    private fun levelToGain(lvl: Int): Double =                   // (as fm1link's Nsp1: 1/2 dB steps, -70 dB .. +6 dB)
        if (lvl <= 0) 0.0003162278 else Math.pow(10.0, (lvl - 104) / 40.0).coerceIn(0.0003162278, 1.99526)

    /**
     * BOTANICA's song: the sections A..D (null = empty) as scenes; without any, the working project as scene A. The
     * tracks are named after BOTANICA's engines.
     */
    fun song(working: ByteArray?, sections: List<ByteArray?>, name: String = "BOTANICA sketch", gmDrums: Boolean = false): Fm1Song {
        val scenes = ArrayList<Pair<String, Nsp1Project>>()
        sections.forEachIndexed { k, b -> if (b != null && b.isNotEmpty()) scenes.add(("ABCD".getOrNull(k)?.toString() ?: "S${k + 1}") to decode(b)) }
        if (scenes.isEmpty()) scenes.add("A" to decode(working ?: throw Nsp1.FormatException("no project to read")))
        val head = scenes[0].second
        val key = head.tracks.firstOrNull { it.index < Layout.NPART && it.p[Nsp1.Layout.P_SCALE].toInt() != 0 } ?: head.tracks[0]
        val tracks = (0 until Layout.NTRK).map { ti ->
            val t0 = head.tracks[ti]
            val engine = if (t0.isDrum) "DRUMS" else Layout.ENGINES.getOrElse(t0.engine) { "SYNTH" }
            val tname = if (t0.isDrum) "DRUMS ${ti - Layout.NPART + 1}" else "T${ti + 1} $engine"
            val clips = LinkedHashMap<Int, Fm1Clip>()
            scenes.forEachIndexed { s, (sn, prj) ->
                val c = Nsp1.trackClip(prj, ti, gmDrums, "$sn $tname")
                if (c.notes.isNotEmpty()) clips[s] = c
            }
            Fm1Track(ti, tname, t0.isDrum, engine, levelToGain(t0.p[Nsp1.Layout.P_LEVEL].toInt()),
                (t0.p[Nsp1.Layout.P_PAN] / 64.0).coerceIn(-1.0, 1.0), t0.p[Nsp1.Layout.P_MUTE].toInt() != 0, clips)
        }
        val bpm = head.globals[Nsp1.Layout.G_BPM].toDouble()
        return Fm1Song(name, if (bpm <= 0) 120.0 else bpm.coerceIn(20.0, 999.0), head.globals[Nsp1.Layout.G_SWING].toInt(),
            Math.floorMod(key.p[Nsp1.Layout.P_ROOT].toInt(), 12), Nsp1.LIVE_SCALE.getOrElse(key.p[Nsp1.Layout.P_SCALE].toInt()) { "Minor" },
            scenes.map { it.first }, tracks)
    }
}
