// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

import com.notesorcery.fm1link.Fm1Song
import java.io.ByteArrayOutputStream

/**
 * A Standard MIDI File (type 1) of the FM-1's song, for Ableton Live or any DAW: a tempo track, then a track per
 * FM-1 track that has notes (its name, on its channel: synth tracks 1..6 on channels 1..6, the drum tracks on 10
 * and 11), the scenes one after the other, each [loops] times.
 */
object MidiFile {
    const val PPQ = 480

    fun write(song: Fm1Song, loops: Int = 1): ByteArray {
        val tracks = ArrayList<ByteArray>()
        tracks.add(track {                               // the tempo and the name
            meta(0, 0x03, song.name.toByteArray(Charsets.US_ASCII))
            val us = (60_000_000.0 / song.tempo).toInt()
            meta(0, 0x51, byteArrayOf((us shr 16).toByte(), (us shr 8).toByte(), us.toByte()))
            meta(0, 0x58, byteArrayOf(4, 2, 24, 8))
        })
        // where each scene starts (beats): its longest clip, looped
        val sceneLen = (0 until song.sceneCount).map { s -> song.tracks.maxOfOrNull { it.clips[s]?.lengthBeats ?: 0.0 }?.takeIf { it > 0 } ?: 4.0 }
        for (t in song.tracks) {
            if (t.clips.isEmpty()) continue
            val ch = if (t.isDrum) 9 + (t.index - 6) else t.index
            val ev = ArrayList<Triple<Long, Int, Pair<Int, Int>>>()   // tick, status, (data1, data2)
            var at = 0.0
            for (s in 0 until song.sceneCount) {
                val c = t.clips[s]
                for (l in 0 until loops) {
                    if (c != null) for (n in c.notes) {
                        val on = ((at + l * sceneLen[s] + n.start) * PPQ).toLong()
                        val off = ((at + l * sceneLen[s] + n.start + n.duration) * PPQ).toLong()
                        ev.add(Triple(on, 0x90 or ch, n.pitch to n.velocity.coerceIn(1, 127)))
                        ev.add(Triple(maxOf(off, on + 1), 0x80 or ch, n.pitch to 0))
                    }
                }
                at += loops * sceneLen[s]
            }
            ev.sortWith(compareBy({ it.first }, { if (it.second and 0xF0 == 0x80) 0 else 1 }))   // offs first on a tick
            tracks.add(track {
                meta(0, 0x03, t.name.toByteArray(Charsets.US_ASCII))
                var last = 0L
                for ((tick, st, d) in ev) {
                    event(tick - last, byteArrayOf(st.toByte(), d.first.toByte(), d.second.toByte()))
                    last = tick
                }
            })
        }
        val out = ByteArrayOutputStream()
        out.write("MThd".toByteArray())
        out.write(be32(6))
        out.write(byteArrayOf(0, 1, 0, tracks.size.toByte(), (PPQ shr 8).toByte(), PPQ.toByte()))
        for (tr in tracks) {
            out.write("MTrk".toByteArray())
            out.write(be32(tr.size))
            out.write(tr)
        }
        return out.toByteArray()
    }

    private class TrackWriter {
        val b = ByteArrayOutputStream()
        fun vlq(v: Long) {
            var x = v
            val tmp = ArrayList<Int>()
            tmp.add((x and 0x7F).toInt())
            x = x shr 7
            while (x > 0) {
                tmp.add(((x and 0x7F) or 0x80).toInt())
                x = x shr 7
            }
            for (i in tmp.indices.reversed()) b.write(tmp[i])
        }
        fun event(delta: Long, data: ByteArray) {
            vlq(delta)
            b.write(data)
        }
        fun meta(delta: Long, type: Int, data: ByteArray) {
            vlq(delta)
            b.write(0xFF)
            b.write(type)
            vlq(data.size.toLong())
            b.write(data)
        }
    }

    private fun track(body: TrackWriter.() -> Unit): ByteArray {
        val w = TrackWriter()
        w.body()
        w.meta(0, 0x2F, ByteArray(0))                   // end of track
        return w.b.toByteArray()
    }

    private fun be32(v: Int) = byteArrayOf((v shr 24).toByte(), (v shr 16).toByte(), (v shr 8).toByte(), v.toByte())

    /** the notes of a type-1 file, (track, tick, channel, note, velocity) of each note-on: for the tests */
    fun readNoteOns(file: ByteArray): List<IntArray> {
        val out = ArrayList<IntArray>()
        var o = 14
        var tr = 0
        while (o + 8 <= file.size) {
            val len = ((file[o + 4].toInt() and 0xFF) shl 24) or ((file[o + 5].toInt() and 0xFF) shl 16) or
                ((file[o + 6].toInt() and 0xFF) shl 8) or (file[o + 7].toInt() and 0xFF)
            var i = o + 8
            val end = i + len
            var tick = 0
            var status = 0
            while (i < end) {
                var d = 0
                do {
                    val c = file[i++].toInt() and 0xFF
                    d = (d shl 7) or (c and 0x7F)
                } while (c and 0x80 != 0)
                tick += d
                var st = file[i].toInt() and 0xFF
                if (st and 0x80 != 0) i++ else st = status
                status = st
                when {
                    st == 0xFF -> {
                        i++
                        var l = 0
                        do {
                            val c = file[i++].toInt() and 0xFF
                            l = (l shl 7) or (c and 0x7F)
                        } while (c and 0x80 != 0)
                        i += l
                    }
                    st and 0xF0 == 0x90 -> {
                        val n = file[i].toInt(); val v = file[i + 1].toInt()
                        if (v > 0) out.add(intArrayOf(tr, tick, st and 15, n, v))
                        i += 2
                    }
                    st and 0xF0 == 0xC0 || st and 0xF0 == 0xD0 -> i += 1
                    else -> i += 2
                }
            }
            o = end
            tr++
        }
        return out
    }
}
