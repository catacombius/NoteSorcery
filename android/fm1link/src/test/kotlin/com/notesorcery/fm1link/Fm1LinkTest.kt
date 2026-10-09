// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.fm1link

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.random.Random

/**
 * fm1link against the other implementations (fixtures: tools/gen_fm1link_fixtures.py): the firmware's NSP1 projects
 * and layout, web/als/nsals.js's notes for them, tools/sampleio.py's sample slot and pack7; and a fake FM-1 that
 * answers as editor.c does, in random chunks with clock bytes in between.
 */
class Fm1LinkTest {
    private fun res(name: String): ByteArray =
        javaClass.classLoader.getResourceAsStream(name)?.readBytes() ?: error("fixture $name missing")

    private fun json(name: String): Any? = MiniJson(String(res(name))).value()

    @Test
    fun pack7MatchesTheEditorProtocol() {
        @Suppress("UNCHECKED_CAST") val j = json("pack7.json") as Map<String, List<Double>>
        val bytes = ByteArray(j["bytes"]!!.size) { j["bytes"]!![it].toInt().toByte() }
        assertArrayEquals(j["pack7"]!!.map { it.toInt() }.toIntArray(), Codec.pack7(bytes))
        assertArrayEquals(bytes, Codec.unpack7(Codec.pack7(bytes)))
        val r = Random(3)
        repeat(50) {
            val b = ByteArray(r.nextInt(0, 300)) { r.nextInt().toByte() }
            assertArrayEquals(b, Codec.unpack7(Codec.pack7(b)))
        }
        assertEquals(-8192, Codec.readV14(Codec.v14(-9000), 0))
        assertEquals(123, Codec.readV14(Codec.v14(123), 0))
        assertEquals(7604L, Codec.readUN(Codec.uN(7604, 3), 0, 3))
    }

    @Test
    fun sysexFramesFromChunksWithRealtimeInside() {
        val frames = ArrayList<ByteArray>()
        val other = ArrayList<Int>()
        val a = SysExAssembler({ frames.add(it) }, { other.add(it) })
        val f1 = Codec.frame(1)
        val f2 = Codec.frame(35, intArrayOf(0, 0, 1, 2, 3, 4, 5, 6, 7))
        val stream = byteArrayOf(0x90.toByte(), 60, 100) + f1.copyOfRange(0, 3) + byteArrayOf(0xF8.toByte()) +
            f1.copyOfRange(3, f1.size) + byteArrayOf(0xF0.toByte(), 0x7D, 0x46, 0x90.toByte(), 61, 0) + f2
        var i = 0
        val r = Random(1)
        while (i < stream.size) {
            val n = minOf(stream.size - i, r.nextInt(1, 5))
            a.feed(stream, i, n)
            i += n
        }
        assertEquals(2, frames.size)
        assertArrayEquals(f1, frames[0])
        assertArrayEquals(f2, frames[1])
        assertEquals(listOf(0x90, 60, 100, 0xF8, 0x90, 61, 0), other)   // (the cut-off SysEx dropped, its note kept)
    }

    @Test
    fun layoutIsTheFirmwares() {
        @Suppress("UNCHECKED_CAST") val l = json("layout.json") as Map<String, Any?>
        val L = Nsp1.Layout
        assertEquals(L.MAGIC.toLong(), (l["magic"] as Double).toLong())
        assertEquals(L.SIZE, (l["size"] as Double).toInt())
        for ((k, v) in mapOf("P_COUNT" to L.P_COUNT, "G_COUNT" to L.G_COUNT, "NTRK" to L.NTRK, "NPART" to L.NPART,
            "NSTEP" to L.NSTEP, "NLOCK" to L.NLOCK)) assertEquals(k, v, (l[k] as Double).toInt())
        @Suppress("UNCHECKED_CAST") val p = l["P"] as Map<String, Double>
        assertEquals(L.P_LEVEL, p["LEVEL"]!!.toInt()); assertEquals(L.P_ROOT, p["ROOT"]!!.toInt())
        assertEquals(L.P_SCALE, p["SCALE"]!!.toInt()); assertEquals(L.P_SLEN, p["SLEN"]!!.toInt())
        assertEquals(L.P_SDIV, p["SDIV"]!!.toInt()); assertEquals(L.P_SSWING, p["SSWING"]!!.toInt())
        assertEquals(L.P_SGATE, p["SGATE"]!!.toInt()); assertEquals(L.P_PAN, p["PAN"]!!.toInt())
        assertEquals(L.P_MUTE, p["MUTE"]!!.toInt())
        assertEquals(L.ENGINES, l["ENGINES"])
        @Suppress("UNCHECKED_CAST") val lanes = (l["LANE_NOTE"] as List<Double>).map { it.toInt() }
        assertEquals(Nsp1.LANE_NOTE.toList(), lanes)
    }

    @Test
    fun notesAreNsalsJsNotes() {
        @Suppress("UNCHECKED_CAST") val j = json("notes.json") as Map<String, Any?>
        @Suppress("UNCHECKED_CAST") val clips = j["clips"] as List<Map<String, Any?>>
        val prj = Nsp1.decode(res("live.nsp1"))
        var total = 0
        for (t in 0 until 8) {
            val c = Nsp1.trackClip(prj, t, gmDrums = true)
            @Suppress("UNCHECKED_CAST") val want = clips[t]["notes"] as List<Map<String, Double>>
            assertEquals("track $t length", clips[t]["lengthBeats"] as Double, c.lengthBeats, 1e-9)
            assertEquals("track $t notes", want.size, c.notes.size)
            for ((k, n) in c.notes.withIndex()) {
                val w = want[k]
                assertEquals(w["pitch"]!!.toInt(), n.pitch)
                assertEquals(w["start"]!!, n.start, 2e-6)
                assertEquals(w["duration"]!!, n.duration, 2e-6)
                assertEquals(w["velocity"]!!.toInt(), n.velocity)
            }
            total += c.notes.size
        }
        assertTrue(total > 20)
        @Suppress("UNCHECKED_CAST") val s = j["song"] as Map<String, Any?>
        val song = Nsp1.song(res("live.nsp1"), listOf(res("secA.nsp1"), res("secB.nsp1"), null, null))
        assertEquals(s["sceneNames"], song.sceneNames)
        assertEquals(s["tempo"] as Double, song.tempo, 0.0)
        assertEquals(s["scaleName"], song.scaleName)
        @Suppress("UNCHECKED_CAST") val tr = s["tracks"] as List<Map<String, Any?>>
        for (t in 0 until 8) {
            assertEquals(tr[t]["name"], song.tracks[t].name)
            assertEquals(tr[t]["volume"] as Double, song.tracks[t].volume, 1e-9)
            @Suppress("UNCHECKED_CAST") val cl = tr[t]["clips"] as Map<String, Map<String, Any?>>
            assertEquals(cl.keys.map { it.toInt() }.toSet(), song.tracks[t].clips.keys)
            for ((k, c) in cl) assertEquals((c["notes"] as List<*>).size, song.tracks[t].clips[k.toInt()]!!.notes.size)
        }
    }

    @Test
    fun damagedProjectsAreRefused() {
        val d = res("live.nsp1").copyOf()
        d[300] = (d[300].toInt() xor 4).toByte()
        try { Nsp1.decode(d); fail("accepted") } catch (e: Nsp1.FormatException) { assertTrue(e.message!!.contains("checksum")) }
        try { Nsp1.decode(d.copyOf(100)); fail("accepted") } catch (e: Nsp1.FormatException) { }
    }

    @Test
    fun sampleSlotIsSampleioS() {
        val pcm = ByteBuffer.wrap(res("slot.pcm")).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
        val all = ShortArray(pcm.remaining()).also { pcm.get(it) }
        val slot = SampleSlot.build("Test Tone", listOf(SlotZone(all.copyOfRange(0, 6000), 57), SlotZone(all.copyOfRange(6000, 10000), 69)))
        assertArrayEquals(res("slot.bin"), slot.data)
        assertArrayEquals(res("slot.hdr"), slot.header)
        assertEquals(2, slot.zoneCount)
    }

    /** answers as firmware/src/editor.c does; replies in random chunks with clock pulses between them */
    private class FakeFm1(val objects: Map<Int, ByteArray>, val nsx: Boolean = true) {
        lateinit var client: Fm1Client
        val r = Random(7)
        val sampleData = java.io.ByteArrayOutputStream()
        var sampleHeader: ByteArray? = null
        var playing = false
        var bpm = 120

        fun receive(frame: ByteArray) {
            val (cmd, a) = Codec.parse(frame) ?: return
            val out = ArrayList<Int>()
            fun s(x: String) { x.forEach { out.add(it.code) }; out.add(0) }
            when (cmd) {
                Cmd.INFO -> { s("FELUCCA NOTESORCERY 0.1"); out += listOf(6, 61, 34, 64, 53); Nsp1.Layout.ENGINES.forEach { s(it) }; out += listOf(8, 11) }
                Cmd.NSX_CAPS -> {
                    if (!nsx) return
                    s("NSX"); out += 1; s("NOTESORCERY 0.1"); out += listOf(8, 6, 2); out += Codec.uN(7604, 3).toList()
                    out += listOf(0x4E, 19, 10, 4, 64, 15, 51, 49, 50)
                }
                Cmd.NSX_TRANSPORT -> { if (a[0] == 1) playing = true; if (a[0] == 2) playing = false
                    out += if (playing) 1 else 0; out += Codec.v14(bpm).toList(); out += Codec.uN(0, 3).toList(); out += listOf(3, 0) }
                Cmd.NSX_TEMPO -> { if (a.size >= 2) bpm = Codec.readV14(a, 0); out += Codec.v14(bpm).toList() }
                Cmd.BK_LIST -> {
                    out += listOf(0, objects.size)
                    for ((id, d) in objects) { out += id; out += Codec.uN(d.size.toLong(), 5).toList(); out += Codec.uN(Codec.crc32(d), 5).toList() }
                }
                Cmd.BK_GET -> {
                    val id = a[0]; val off = Codec.readUN(a, 1, 5).toInt(); val n = a[6] or (a[7] shl 7)
                    val d = objects[id]!!
                    out += listOf(id, 0); out += Codec.uN(off.toLong(), 5).toList(); out += listOf(n and 127, n shr 7)
                    out += Codec.pack7(d, off, off + n).toList()
                }
                Cmd.SMP_BEGIN -> { sampleData.reset(); out += listOf(a[0], 0) }
                Cmd.SMP_WRITE -> {
                    val off = Codec.readUN(a, 1, 3).toInt()
                    assertEquals(SampleSlot.DATA_OFF + sampleData.size(), off)
                    sampleData.write(Codec.unpack7(a, 4))
                    out += listOf(a[0]); out += Codec.uN(off.toLong(), 3).toList(); out += 0
                }
                Cmd.SMP_END -> { sampleHeader = Codec.unpack7(a, 1); out += listOf(a[0], 0) }
                else -> return
            }
            val reply = Codec.frame(cmd, out.toIntArray())
            var i = 0
            while (i < reply.size) {
                val n = minOf(reply.size - i, r.nextInt(1, 20))
                client.feed(reply, i, n)
                if (r.nextInt(4) == 0) client.feed(byteArrayOf(0xF8.toByte()))
                i += n
            }
        }
    }

    @Test
    fun clientPullsTheSongAndUploadsASample() {
        val objs = linkedMapOf(0 to res("live.nsp1"), 1 to ByteArray(88) { it.toByte() }, 2 to res("secA.nsp1"),
            3 to res("secB.nsp1"), 4 to ByteArray(0), 5 to ByteArray(0))
        val fake = FakeFm1(objs)
        var clocks = 0
        val client = Fm1Client({ fake.receive(it) }, { if (it == 0xF8) clocks++ })
        fake.client = client
        val info = client.info()
        assertEquals(8, info.tracks); assertEquals(11, info.proto); assertEquals(Nsp1.Layout.ENGINES, info.engines)
        val caps = client.caps()!!
        assertEquals(8, caps.tracks); assertEquals(7604, caps.projectSize); assertTrue(caps.clockOut && caps.songPositionIn)
        assertEquals(10, caps.drumChannel); assertEquals(49, caps.kit808cm)
        assertTrue(client.play().playing)
        assertEquals(133, client.tempo(133))
        assertEquals(false, client.stop().playing)
        var last = 0
        val song = client.pullSong(progress = { d, _ -> last = d })
        val want = Nsp1.song(res("live.nsp1"), listOf(res("secA.nsp1"), res("secB.nsp1"), null, null))
        assertEquals(want, song)
        assertEquals(3 * 7604, last)
        assertTrue(clocks > 0)
        val pcm = ShortArray(9000) { (Math.sin(it * 0.05) * 20000).toInt().toShort() }
        val slot = SampleSlot.build("SINE", listOf(SlotZone(pcm, 60)))
        client.uploadSample(1, slot)
        assertArrayEquals(slot.data, fake.sampleData.toByteArray())
        assertArrayEquals(slot.header, fake.sampleHeader)
        assertNull(Fm1Client({ FakeFm1(objs, nsx = false).also { f -> f.client = Fm1Client({}) }.receive(it) }).caps())
    }

    @Test
    fun clockHelpers() {
        assertArrayEquals(byteArrayOf(0xF2.toByte(), 32, 0), Fm1Midi.songPosition(32))
        assertArrayEquals(byteArrayOf(0xF2.toByte(), 0, 1), Fm1Midi.songPosition(128))
        assertEquals(9, Fm1Midi.channelOf(6)); assertEquals(10, Fm1Midi.channelOf(7)); assertEquals(2, Fm1Midi.channelOf(2))
        assertEquals(20_833_333L, Fm1Midi.pulseNanos(120.0))
    }
}

/** just enough JSON for the fixtures: objects, arrays, numbers (as Double), strings, true / false / null */
private class MiniJson(val s: String) {
    var i = 0
    fun ws() { while (i < s.length && s[i].isWhitespace()) i++ }
    fun value(): Any? {
        ws()
        return when (s[i]) {
            '{' -> { i++; val m = LinkedHashMap<String, Any?>(); ws(); if (s[i] == '}') { i++; return m }
                while (true) { ws(); val k = value() as String; ws(); i++; m[k] = value(); ws(); if (s[i++] == '}') return m } ; m }
            '[' -> { i++; val l = ArrayList<Any?>(); ws(); if (s[i] == ']') { i++; return l }
                while (true) { l.add(value()); ws(); if (s[i++] == ']') return l }; l }
            '"' -> { i++; val b = StringBuilder(); while (s[i] != '"') { if (s[i] == '\\') i++; b.append(s[i++]) }; i++; b.toString() }
            't' -> { i += 4; true }
            'f' -> { i += 5; false }
            'n' -> { i += 4; null }
            else -> { val st = i; while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++; s.substring(st, i).toDouble() }
        }
    }
}
