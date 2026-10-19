// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

import com.notesorcery.fm1link.Codec
import com.notesorcery.fm1link.Fm1Song
import com.notesorcery.fm1link.Nsp1
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.File
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
import kotlin.random.Random

/**
 * The app's logic against the firmware and the web editor (fixtures: tools/gen_botanica_app_fixtures.py): BTP1
 * projects as BOTANICA writes them and web/als/nsals.js's notes for them; the chord and mood tables against the
 * firmware's source; the drift, the DSP, the MIDI file; and a fake BOTANICA FM-1 for the live link.
 */
class BotanicaCoreTest {
    private fun res(name: String): ByteArray =
        javaClass.classLoader.getResourceAsStream(name)?.readBytes() ?: error("fixture $name missing")

    private fun json(name: String): Any? = MiniJson(String(res(name))).value()

    /** the firmware's source (the tests run in android/botanica/core) */
    private fun firmware(name: String): String = File("../../../firmware/src/$name").readText()

    @Test
    fun layoutIsTheFirmwares() {
        @Suppress("UNCHECKED_CAST") val j = json("layout.json") as Map<String, Any?>
        val L = Btp1.Layout
        assertEquals(L.MAGIC, (j["magic"] as Double).toInt())
        assertEquals(L.SIZE, (j["size"] as Double).toInt())
        assertEquals(L.P_COUNT, (j["P_COUNT"] as Double).toInt())
        assertEquals(L.G_COUNT, (j["G_COUNT"] as Double).toInt())
        @Suppress("UNCHECKED_CAST") val p = j["P"] as Map<String, Double>
        assertEquals(L.P_E0, p["E0"]!!.toInt())
        // the parameters the notes need are where NSP1 has them
        assertEquals(Nsp1.Layout.P_ROOT, p["ROOT"]!!.toInt())
        assertEquals(Nsp1.Layout.P_SCALE, p["SCALE"]!!.toInt())
        assertEquals(Nsp1.Layout.P_SDIV, p["SDIV"]!!.toInt())
        assertEquals(Nsp1.Layout.P_MUTE, p["MUTE"]!!.toInt())
        assertEquals(L.ENGINES, j["ENGINES"])
        for (n in listOf("live.btp1", "secA.btp1", "secB.btp1")) assertTrue(Btp1.isBtp1(res(n)))
    }

    @Test
    fun notesAreNsalsJsNotes() {
        @Suppress("UNCHECKED_CAST") val j = json("notes.json") as Map<String, Any?>
        @Suppress("UNCHECKED_CAST") val clips = j["clips"] as List<Map<String, Any?>>
        val prj = Btp1.decode(res("live.btp1"))
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
        val song = Btp1.song(res("live.btp1"), listOf(res("secA.btp1"), res("secB.btp1"), null, null), name = "FM-1 sketch")
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
    fun damagedOrForeignProjectsAreRefused() {
        val b = res("live.btp1")
        val bad = b.copyOf().also { it[200] = (it[200] + 1).toByte() }
        try { Btp1.decode(bad); fail("damaged") } catch (e: Nsp1.FormatException) { assertTrue(e.message!!.contains("checksum")) }
        try { Btp1.decode(b.copyOf(7604)); fail("NSP1-sized") } catch (e: Nsp1.FormatException) { }
        try { Nsp1.decode(b); fail("NoteSorcery's reader takes no BTP1") } catch (e: Nsp1.FormatException) { }
        assertEquals(Btp1.decode(b).tracks.size, Btp1.decodeAny(b).tracks.size)
    }

    @Test
    fun chordsAreSeqCs() {
        val src = firmware("seq.c")
        val semi = Regex("""CHORD_SEMI\[5]\[4] = \{(.*?)\};""", RegexOption.DOT_MATCHES_ALL).find(src)!!.groupValues[1]
        val rows = Regex("""\{(\d+), (\d+), (\d+), (\d+)\}""").findAll(semi).map { m -> m.groupValues.drop(1).map { it.toInt() } }.toList()
        assertEquals(Chords.Kind.entries.map { it.semis.toList() }, rows)
        val prog = Regex("""CHORD_PROG\[2]\[4] = \{(.*?)\n\};""", RegexOption.DOT_MATCHES_ALL).find(src)!!.groupValues[1]
        val steps = Regex("""\{(\d+), \{(\d+), (\d+), (\d+), (\d+)\}\}""").findAll(prog).map { m -> m.groupValues.drop(1).map { it.toInt() } }.toList()
        val mine = Chords.Progression.entries.flatMap { p -> p.steps.map { listOf(it.degree) + it.semis.toList() } }
        assertEquals(mine, steps)
        // PETAL in C, from middle C: IVmaj7 (F A C E), then Vadd9 (G B D A) from the G below
        assertEquals(listOf(65, 69, 72, 76), Chords.progression(Chords.Progression.PETAL, 0, 0, 60))
        assertEquals(listOf(55, 59, 62, 69), Chords.progression(Chords.Progression.PETAL, 0, 1, 60))   // (G: nearer below)
        assertEquals(listOf(57, 61, 68, 75), Chords.chord(Chords.Kind.LYD11, 57))
        assertEquals("C4", Chords.noteName(60))
        val st = Chords.strumOffsets(listOf(64, 60, 67), 30, 1)
        assertEquals(listOf(67 to 0, 64 to 30, 60 to 60), st)
    }

    @Test
    fun moodsAreTheBrowsers() {
        val src = firmware("ui.c")
        val a = src.indexOf("BANK_KIND[] = {\"PETL\"")
        val body = src.substring(a, src.indexOf("#else", a))
        val eng = mapOf("FELT" to 0, "DROPLET" to 1, "ANALOG" to 2, "SAMPLE" to 3, "NOISE" to 4, "PHYS" to 5, "GRAIN" to 6,
            "FORMANT" to 7, "WAVE" to 8)
        val want = Regex("""\{BK_(\w+), ENGI_(\w+), "([^"]+)"\}""").findAll(body)
            .map { m -> Moods.P(Moods.Mood.valueOf(m.groupValues[1]), eng[m.groupValues[2]]!!, m.groupValues[3]) }.toList()
        assertEquals(want, Moods.PRESETS)
        assertEquals(Moods.Mood.WEATHER, Moods.moodOf("crickets"))
        assertNull(Moods.moodOf("ACID CLASS"))
        assertEquals(2, Moods.indexIn(Moods.PRESETS[2], listOf("FELT UPRIGHT", "NURTURE KEYS", "PNO+STRINGS ")))
    }

    @Test
    fun driftRampsLikeTheFirmware() {
        val p = DriftPlan(listOf(60, 160), barsPerSection = 8, rampBars = 4)
        assertEquals(60.0, p.bpmAt(0.0), 0.0)
        assertEquals(60.0, p.bpmAt(4.0), 0.0)                   // the ramp starts 4 bars before B
        assertEquals(110.0, p.bpmAt(6.0), 1e-9)
        assertEquals(160.0, p.bpmAt(8.0), 0.0)
        assertEquals(60.0, p.bpmAt(16.0), 0.0)                  // (round again)
        assertEquals(160 to 4, p.cueAt(3))                       // a bar before the ramp
        assertEquals(60 to 4, p.cueAt(11))
        assertNull(p.cueAt(4))
        val jump = DriftPlan(listOf(80, 160), 4, 0)
        assertEquals(80.0, jump.bpmAt(3.99), 0.0)
        assertEquals(160 to 0, jump.cueAt(3))
        assertEquals(DriftPlan.ARC.bars * 4 + 1, DriftPlan.ARC.curve().size)
    }

    @Test
    fun dspStaysInBounds() {
        val rate = 22050
        val tone = FloatArray(rate) { if (it < 2000 || it > 18000) 0f else (0.3 * sin(2 * PI * 220 * it / rate)).toFloat() }
        val t = Dsp.trimSilence(tone, rate)
        assertTrue(t.size in 16000..16900)
        val n = Dsp.normalize(t, -1.0)
        assertEquals(0.891, Dsp.peak(n).toDouble(), 0.005)
        assertArrayEquals(n, Dsp.reverse(Dsp.reverse(n)), 0f)
        val f = Dsp.fade(n, rate, 10)
        assertEquals(0f, f[0], 0f)
        val l = Dsp.loopCrossfade(n, rate, 100)
        assertEquals(n.size - rate / 10, l.size)
        val s = Dsp.paulStretch(t, 4.0, 2048)
        assertEquals(t.size * 4, s.size)
        assertTrue(Dsp.peak(s) <= Dsp.peak(t) + 1e-4f)
        assertTrue(abs(s.average()) < 0.01)                      // no DC
        assertTrue(Dsp.rmsDb(s) > -40)                           // a drone, not silence
        assertEquals(0, Dsp.trimSilence(FloatArray(100), rate).size)
        val pcm = byteArrayOf(0, 0x40, 0, 0xC0.toByte())
        assertArrayEquals(floatArrayOf(0.5f, -0.5f), Dsp.fromPcm16(pcm), 0f)
        // the FFT round trip
        val re = DoubleArray(64) { sin(it * 0.3) }; val im = DoubleArray(64); val orig = re.copyOf()
        Dsp.fft(re, im, false); Dsp.fft(re, im, true)
        for (i in 0 until 64) assertEquals(orig[i], re[i], 1e-9)
    }

    @Test
    fun midiFileHasTheSongsNotes() {
        val song: Fm1Song = Btp1.song(res("live.btp1"), listOf(res("secA.btp1"), res("secB.btp1"), null, null))
        val mid = MidiFile.write(song, loops = 2)
        assertEquals("MThd", String(mid, 0, 4))
        val ons = MidiFile.readNoteOns(mid)
        val want = song.tracks.sumOf { t -> t.clips.values.sumOf { it.notes.size } } * 2
        assertEquals(want, ons.size)
        for (o in ons) {
            val tr = song.tracks.filter { it.clips.isNotEmpty() }[o[0] - 1]
            assertEquals(if (tr.isDrum) 9 + tr.index - 6 else tr.index, o[2])
        }
        // the first scene's first note of the first track with notes, at its time
        val t0 = song.tracks.first { it.clips.isNotEmpty() }
        val first = t0.clips.entries.minBy { it.key }
        if (first.key == 0) {
            val n = first.value.notes.minBy { it.start }
            assertTrue(ons.any { it[0] == 1 && it[3] == n.pitch && it[1] == (n.start * MidiFile.PPQ).toInt() })
        }
    }

    // ---- the live link, against a fake BOTANICA FM-1 ----

    private class FakeBotanica {
        lateinit var garden: Garden
        val r = Random(3)
        val g = IntArray(43).also { it[0] = 120; it[41] = 4 }
        val labels = mapOf(0 to "BPM", 33 to "FILT", 34 to "BLOOM", 35 to "DROPS", 36 to "FOLEY", 37 to "GLTCH", 38 to "WTHR",
            39 to "BREEZ", 40 to "DRIFT", 41 to "RAMP", 42 to "BMODE")
        val mutes = IntArray(8)
        var sel = 2
        var bpm = 120
        val put = HashMap<Int, java.io.ByteArrayOutputStream>()
        val committed = HashMap<Int, ByteArray>()
        var putLen = 0; var putCrc = 0L
        val log = ArrayList<String>()

        fun receive(frame: ByteArray) {
            val (cmd, a) = Codec.parse(frame) ?: return
            val out = ArrayList<Int>()
            fun s(x: String) { x.forEach { out.add(it.code) }; out.add(0) }
            fun v(x: Int) { out += Codec.v14(x).toList() }
            when (cmd) {
                1 -> { s("BOTANICA 0.1"); out += listOf(9, 64, 43, 64, 56); Btp1.Layout.ENGINES.forEach { s(it) }; out += listOf(8, 11) }
                80 -> { s("NSX"); out += 1; s("BOTANICA 0.1"); out += listOf(8, 6, 2); out += Codec.uN(7672, 3).toList()
                    out += listOf(0x4E, 1 or 2 or 32, 10, 4, 64, 15, 7, 127, 127, 'B'.code, 43) }
                5 -> {
                    val sc = a[0]; val id = a[1]
                    out += listOf(sc, id)
                    if (sc == 1) {
                        val l = labels[id] ?: "G$id"
                        if (l == "BMODE") { out += 8; v(0); v(2); v(0); s(l); s(""); listOf("SWELL", "FREEZE", "SMEAR").forEach { s(it) } }
                        else { out += 1; v(0); v(if (l == "DRIFT") 240 else 127); v(0); s(l); s("") }
                    } else { out += 0; v(0); v(127); v(0); s(if (id == 40) "MUTE" else "P$id"); s("") }
                }
                2 -> { out += listOf(a[0], a[1]); v(g[a[1]]) }
                3 -> { g[a[1]] = Codec.readV14(a, 2); log += "${labels[a[1]]}=${g[a[1]]}"; out += listOf(a[0], a[1]); v(g[a[1]]) }
                10 -> { out += listOf(a[0], 3); s("KALIMBA"); s("MARIMBA"); s("MUSIC BOX"); s("SHAPE"); s("ECHO") }
                8 -> { log += "preset ${a[0]} ${a[1]}"; out += listOf(a[0], a[1]) }
                27 -> { if (a.isNotEmpty()) sel = a[0]
                    out += listOf(sel, 8); for (t in 0 until 8) { out += listOf(if (t < 6) t else 9, 1); v(100); out += listOf(mutes[t], 0) }; out += listOf(0, 1) }
                31 -> { if (a.size >= 4) mutes[a[0]] = Codec.readV14(a, 2); out += listOf(a[0], a[1]); v(mutes[a[0]]) }
                82 -> { if (a.size >= 2) bpm = Codec.readV14(a, 0); v(bpm) }
                22 -> out += a[0]
                36 -> {
                    val id = a[1]
                    when (a[0]) {
                        0 -> { put[id] = java.io.ByteArrayOutputStream(); putLen = Codec.readUN(a, 2, 5).toInt(); putCrc = Codec.readUN(a, 7, 5) }
                        1 -> { assertEquals(put[id]!!.size().toLong(), Codec.readUN(a, 2, 5)); put[id]!!.write(Codec.unpack7(a, 7)) }
                        2 -> { val d = put[id]!!.toByteArray(); assertEquals(putLen, d.size); assertEquals(putCrc, Codec.crc32(d)); committed[id] = d }
                    }
                    out += listOf(a[0], id, 0)
                }
                else -> return
            }
            send(Codec.frame(cmd, out.toIntArray()))
        }

        /** in random chunks, with a note and a push in between, as the device does */
        fun send(reply: ByteArray) {
            var i = 0
            while (i < reply.size) {
                val n = minOf(reply.size - i, r.nextInt(1, 24))
                garden.feed(reply, i, n)
                i += n
            }
        }
    }

    @Test
    fun gardenTalksToBotanica() {
        val fake = FakeBotanica()
        val garden = Garden({ fake.receive(it) })
        fake.garden = garden
        val d = garden.connect()
        assertTrue(d.botanica)
        assertEquals("BOTANICA 0.1", d.firmware)
        assertEquals(9, d.engines.size)
        for (l in Garden.PERFORM + Garden.SKY) assertNotNull("global $l", garden.global(l))
        assertEquals(listOf("SWELL", "FREEZE", "SMEAR"), garden.global("BMODE")!!.names)
        assertEquals(240, garden.global("DRIFT")!!.max)

        assertEquals(90, garden.set("BLOOM", 90))
        assertEquals(90, fake.g[34])
        garden.freeze(true)
        assertEquals(1, fake.g[42]); assertEquals(127, fake.g[34])
        garden.freeze(false)
        assertEquals(0, fake.g[42]); assertEquals(90, fake.g[34])
        garden.set("GLTCH", 10)
        garden.glitch(true); assertEquals(127, fake.g[37])
        garden.glitch(false); assertEquals(10, fake.g[37])

        fake.mutes[5] = 1
        garden.breakDown()
        assertEquals(listOf(1, 1, 0, 1, 1, 1, 1, 1), fake.mutes.toList())   // all but the selected (2)
        assertEquals(1, fake.g[42])
        garden.set("DRIFT", 160)
        garden.drop()
        assertEquals(listOf(0, 0, 0, 0, 0, 1, 0, 0), fake.mutes.toList())   // as they were
        assertEquals(160, fake.bpm)
        assertEquals(0, fake.g[42])

        assertEquals(listOf("KALIMBA", "MARIMBA", "MUSIC BOX"), garden.names(1))
        garden.preset(1, 2)
        assertTrue(fake.log.contains("preset 1 2"))
        val t = garden.tracks(4)
        assertEquals(4, t.selected); assertEquals(8, t.tracks.size); assertEquals(128, t.soloMask)
        assertEquals(9, t.tracks[6].engine)

        val bytes = res("secA.btp1")
        var prog = 0
        garden.putObject(3, bytes) { p, _ -> prog = p }
        assertArrayEquals(bytes, fake.committed[3])
        assertEquals(bytes.size, prog)

        // pushes and notes in between
        val got = ArrayList<String>()
        garden.onChanged = { sc, id, v -> got += "changed $sc $id $v" }
        garden.onNote = { ch, n, v -> got += "note $ch $n $v" }
        garden.onReload = { e, p, tr -> got += "reload $e $p $tr" }
        fake.send(Codec.frame(23, intArrayOf(1, 38) + Codec.v14(64)))
        fake.send(byteArrayOf(0x92.toByte(), 60, 100, 0xF8.toByte(), 64, 90, 0x82.toByte(), 60, 0))
        fake.send(Codec.frame(24, intArrayOf(4, 7, 3)))
        assertEquals(listOf("changed 1 38 64", "note 2 60 100", "note 2 64 90", "note 2 60 0", "reload 4 7 3"), got)
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

class FieldAndBackupTest {
    @Test
    fun aRecordingFitsASlot() {
        val rate = 44100
        val rec = FloatArray(rate * 9) { i -> if (i < rate / 2) 0f else (0.2 * sin(2 * PI * 330 * i / rate) * (1 + 0.3 * sin(i / 9000.0))).toFloat() }
        for (fs in listOf(FieldSample(), FieldSample(reverse = true, loop = true), FieldSample(stretch = 4, loop = true))) {
            val y = fs.process(rec, rate)
            assertTrue("$fs: ${y.size}", y.size in 20_000..FieldSample.MAX_SAMPLES)
            assertTrue(Dsp.peak(y) in 0.85f..0.9f)
            val slot = fs.slot("crickets", rec, rate)
            assertTrue(slot.data.size <= com.notesorcery.fm1link.SampleSlot.MAX_DATA)
            assertEquals(1, slot.zoneCount)
        }
        val short = FieldSample().process(FloatArray(rate) { if (it in 1000..12000) 0.5f * sin(it * 0.05f) else 0f }, rate)
        assertTrue(short.size in 5000..6500)                     // trimmed to the sound, at 22 kHz
        try { FieldSample().slot("x", FloatArray(1000), rate); fail("silence") } catch (e: IllegalArgumentException) { }
    }

    @Test
    fun backupsAreTheWebEditors() {
        val live = javaClass.classLoader.getResourceAsStream("live.btp1")!!.readBytes()
        val b = Backup("BOTANICA 0.1", "2026-10-19T12:00:00Z", mapOf(0 to live, 1 to byteArrayOf(1, 2, 3)))
        val json = b.toJson()
        assertTrue(json.startsWith("{\"format\": \"sloop-backup\", \"version\": 1"))
        val r = Backup.fromJson(json)
        assertEquals("BOTANICA 0.1", r.firmware)
        assertArrayEquals(live, r.objects[0])
        assertEquals(setOf(0), r.projects().keys)
        val bad = json.replace("\"crc\": ${Codec.crc32(live)}", "\"crc\": 1")
        try { Backup.fromJson(bad); fail("crc") } catch (e: IllegalArgumentException) { }
        try { Backup.fromJson("{\"format\": \"other\"}"); fail("format") } catch (e: IllegalArgumentException) { }
    }
}

class DeviceRankTest {
    @Test
    fun theFm1IsPickedFirst() {
        val names = listOf("Pixel Synth" to false, "Bluetooth MIDI" to false, "USB MIDI keyboard" to true,
            "Hügelton Instruments Felucca" to true)
        val best = names.maxBy { (n, usb) -> Garden.rankDevice(n, usb) }
        assertEquals("Hügelton Instruments Felucca", best.first)
        assertTrue(Garden.rankDevice("H?gelton Instruments Felucca", true) > Garden.rankDevice("USB MIDI keyboard", true))
        assertTrue(Garden.rankDevice("M-VAVE FM-1", false) > Garden.rankDevice("Some USB synth", true))
        assertTrue(Garden.rankDevice("SINCO USB-MIDI", true) > Garden.rankDevice("Pixel Synth", false))
    }
}
