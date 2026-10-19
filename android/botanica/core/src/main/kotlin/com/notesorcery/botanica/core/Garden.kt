// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

import com.notesorcery.fm1link.Codec
import com.notesorcery.fm1link.Fm1Caps
import com.notesorcery.fm1link.Fm1Client
import com.notesorcery.fm1link.Fm1Exception
import com.notesorcery.fm1link.SysExAssembler

/**
 * A BOTANICA FM-1, live: the editor protocol (web/EDITOR_PROTOCOL.md) on top of [Fm1Client]. The PERFORM / SKY globals
 * are found by their DESC label, never by number, so a firmware that moves them still works. [send] writes to the
 * FM-1's MIDI input; give every byte from its output to [feed]. Requests block (call them off the UI thread); the
 * pushes of WATCH ([onChanged], [onReload], [onTrackChanged]) and the notes the FM-1 plays ([onNote]) arrive on the
 * thread that calls [feed].
 */
class Garden(send: (ByteArray) -> Unit, timeoutMs: Long = 1500) {
    /** a parameter's description (DESC) */
    data class Desc(val scope: Int, val id: Int, val fmt: Int, val min: Int, val max: Int, val def: Int,
                    val label: String, val unit: String, val names: List<String>)

    data class TrackState(val engine: Int, val preset: Int, val level: Int, val mute: Boolean, val armed: Boolean)
    data class Tracks(val selected: Int, val tracks: List<TrackState>, val soloMask: Int)

    /** what [connect] found */
    data class Device(val firmware: String, val caps: Fm1Caps?, val botanica: Boolean, val engines: List<String>,
                      val pCount: Int, val gCount: Int)

    @Volatile var onChanged: (scope: Int, id: Int, value: Int) -> Unit = { _, _, _ -> }
    @Volatile var onReload: (engine: Int, preset: Int, track: Int) -> Unit = { _, _, _ -> }
    @Volatile var onTrackChanged: (track: Int, id: Int, value: Int) -> Unit = { _, _, _ -> }
    /** a note the FM-1 sent (its MIDI out): channel 0..15, note, velocity (0 = off) */
    @Volatile var onNote: (channel: Int, note: Int, velocity: Int) -> Unit = { _, _, _ -> }

    private var status = 0
    private val data = IntArray(2)
    private var nData = 0
    val client = Fm1Client(send, { b -> midiByte(b) }, timeoutMs)
    private val pushes = SysExAssembler({ f -> Codec.parse(f)?.let { push(it.first, it.second) } })

    fun feed(bytes: ByteArray, offset: Int = 0, count: Int = bytes.size - offset) {
        client.feed(bytes, offset, count)
        synchronized(pushes) { pushes.feed(bytes, offset, count) }
    }

    private fun push(cmd: Int, a: IntArray) {
        when (cmd) {
            Cmd.CHANGED -> if (a.size >= 4) onChanged(a[0], a[1], Codec.readV14(a, 2))
            Cmd.RELOAD -> if (a.size >= 2) onReload(a[0], a[1], a.getOrElse(2) { 0 })
            Cmd.TRACK_CHANGED -> if (a.size >= 4) onTrackChanged(a[0], a[1], Codec.readV14(a, 2))
        }
    }

    /** the channel messages among the bytes that are not SysEx: note on / off only */
    private fun midiByte(b: Int) {
        if (b >= 0xF8) return
        if (b and 0x80 != 0) { status = b; nData = 0; return }
        if (status and 0xE0 != 0x80) return                 // (8n / 9n)
        data[nData++] = b
        if (nData == 2) {
            nData = 0
            onNote(status and 15, data[0], if (status and 0xF0 == 0x90) data[1] else 0)
        }
    }

    private val labels = HashMap<Pair<Int, String>, Desc>()
    var device: Device? = null
        private set

    /** INFO, NSX_CAPS and the DESC of every global: is this a BOTANICA FM-1? */
    fun connect(): Device {
        val a = client.request(Cmd.INFO)
        var (fw, i) = Codec.readString(a, 0)
        val nEng = a[i]; val pCount = a[i + 1]; val gCount = a[i + 2]
        i += 5
        val engines = ArrayList<String>()
        repeat(nEng) { val (s, j) = Codec.readString(a, i); engines.add(s); i = j }
        val caps = client.caps()
        labels.clear()
        for (id in 0 until gCount) {
            val d = try { desc(SCOPE_GLOBAL, id) } catch (e: Fm1Exception) { continue }
            labels[SCOPE_GLOBAL to d.label] = d
        }
        val botanica = (caps != null && caps.features and FEATURE_BOTANICA != 0) || fw.startsWith("BOTANICA")
        return Device(fw, caps, botanica, engines, pCount, gCount).also { device = it }
    }

    fun desc(scope: Int, id: Int): Desc {
        val a = client.request(Cmd.DESC, intArrayOf(scope, id))
        var i = 3
        val min = Codec.readV14(a, i); val max = Codec.readV14(a, i + 2); val def = Codec.readV14(a, i + 4)
        i += 6
        val (label, i1) = Codec.readString(a, i)
        val (unit, i2) = Codec.readString(a, i1)
        i = i2
        val names = ArrayList<String>()
        if (a[2] == FMT_ENUM) while (i < a.size) { val (s, j) = Codec.readString(a, i); names.add(s); i = j }
        return Desc(a[0], a[1], a[2], min, max, def, label, unit, names)
    }

    /** a global by its DESC label (BLOOM, DROPS, FOLEY, GLTCH, WTHR, BREEZ, DRIFT, RAMP, BMODE, FILT, BPM ..) */
    fun global(label: String): Desc? = labels[SCOPE_GLOBAL to label]

    /** a global's DESC label by its id (for the CHANGED pushes) */
    fun labelOf(id: Int): String? = labels.values.firstOrNull { it.scope == SCOPE_GLOBAL && it.id == id }?.label

    private fun gid(label: String) = global(label)?.id ?: throw Fm1Exception("this firmware has no $label")

    fun get(scope: Int, id: Int): Int = Codec.readV14(client.request(Cmd.GET, intArrayOf(scope, id)), 2)
    fun set(scope: Int, id: Int, v: Int): Int = Codec.readV14(client.request(Cmd.SET, intArrayOf(scope, id) + Codec.v14(v)), 2)
    fun get(label: String): Int = get(SCOPE_GLOBAL, gid(label))
    fun set(label: String, v: Int): Int = set(SCOPE_GLOBAL, gid(label), v)

    /** NAMES: [engine]'s preset names */
    fun names(engine: Int): List<String> {
        val a = client.request(Cmd.NAMES, intArrayOf(engine))
        val n = a[1]
        var i = 2
        return List(n) { val (s, j) = Codec.readString(a, i); i = j; s }
    }

    /** PRESET on the selected track */
    fun preset(engine: Int, preset: Int) { client.request(Cmd.PRESET, intArrayOf(engine, preset)) }

    /** TRACK: the tracks, after selecting [select] (null: only ask) */
    fun tracks(select: Int? = null): Tracks {
        val a = client.request(Cmd.TRACK, select?.let { intArrayOf(it) } ?: IntArray(0))
        val n = a[1]
        val t = List(n) { k -> val o = 2 + 6 * k; TrackState(a[o], a[o + 1], Codec.readV14(a, o + 2), a[o + 3] != 0, a[o + 4] != 0) }
        return Tracks(a[0], t, a.getOrElse(2 + 6 * n) { 0 } or (a.getOrElse(3 + 6 * n) { 0 } shl 7))   // (solo: 2 x 7 bits)
    }

    /** TRACK_PARAM: track [track]'s P_* [id], set to [v] (null: get) */
    fun trackParam(track: Int, id: Int, v: Int? = null): Int =
        Codec.readV14(client.request(Cmd.TRACK_PARAM, intArrayOf(track, id) + (v?.let { Codec.v14(it) } ?: IntArray(0))), 2)

    /** WATCH (with the other tracks' changes); keep it alive with [ping] about every second */
    fun watch(on: Boolean): Int = client.request(Cmd.WATCH, intArrayOf(if (on) 3 else 0))[0]
    fun ping() { client.request(Cmd.PING, timeout = 800, retries = 0) }

    // ---- the momentary actions of the device's GLO layer, made of SETs (the app's FREEZE / GLITCH / BREAK) ----

    private var saved: Map<String, Int>? = null
    private var savedGlitch: Int? = null
    private var savedMutes: Map<Int, Int>? = null

    /** FREEZE held: BLOOM's FREEZE (BMODE FREEZE, BLOOM up) until [freeze] false puts them back */
    fun freeze(on: Boolean) {
        if (on && saved == null) {
            saved = mapOf("BMODE" to get("BMODE"), "BLOOM" to get("BLOOM"))
            set("BMODE", 1)
            set("BLOOM", 127)
        } else if (!on) saved?.let { s -> s.forEach { (k, v) -> set(k, v) }; saved = null }
    }

    /** GLITCH held: all the way, as GLO + F#4 on the device */
    fun glitch(on: Boolean) {
        if (on && savedGlitch == null) {
            savedGlitch = get("GLTCH")
            set("GLTCH", 127)
        } else if (!on) savedGlitch?.let { set("GLTCH", it); savedGlitch = null }
    }

    /**
     * BREAK: every track but the selected one muted and the reverb frozen; [drop] brings them back (and the tempo to
     * DRIFT, when it is set), as the device's BREAK / DROP do
     */
    fun breakDown() {
        if (savedMutes != null) return
        val t = tracks()
        val mute = pId("MUTE", P_MUTE_DEFAULT)
        savedMutes = t.tracks.indices.filter { it != t.selected }.associateWith { trackParam(it, mute) }
        for (k in savedMutes!!.keys) trackParam(k, mute, 1)
        freeze(true)
    }

    fun drop() {
        val m = savedMutes ?: return
        val mute = pId("MUTE", P_MUTE_DEFAULT)
        val drift = global("DRIFT")?.let { get(SCOPE_GLOBAL, it.id) } ?: 0
        if (drift > 0) client.tempo(drift)
        m.forEach { (k, v) -> trackParam(k, mute, v) }
        savedMutes = null
        freeze(false)
    }

    private val pIds = HashMap<String, Int>()
    /** a track parameter's id by its label, from DESC of the selected track (the fixed ones are the same on all) */
    private fun pId(label: String, fallback: Int): Int = pIds.getOrPut(label) {
        val n = device?.pCount ?: 64
        (0 until n).firstOrNull { id -> try { desc(SCOPE_TRACK, id).label == label } catch (e: Fm1Exception) { false } } ?: fallback
    }

    // ---- backup / restore ----

    /** BK_PUT: write object [id] (a project, a section, ..) back; the song must be stopped for projects */
    fun putObject(id: Int, bytes: ByteArray, progress: (Int, Int) -> Unit = { _, _ -> }) {
        fun rc(a: IntArray, what: String) {
            val r = a.getOrElse(2) { 1 }
            if (r != 0) throw Fm1Exception("restore of object $id: $what rc $r" + if (r == 3) " (stop the song first)" else "")
        }
        rc(client.request(Cmd.BK_PUT, intArrayOf(0, id) + Codec.uN(bytes.size.toLong(), 5) + Codec.uN(Codec.crc32(bytes), 5),
            timeout = 5000, retries = 0), "begin")
        var off = 0
        while (off < bytes.size) {
            val n = minOf(256, bytes.size - off)
            rc(client.request(Cmd.BK_PUT, intArrayOf(1, id) + Codec.uN(off.toLong(), 5) + Codec.pack7(bytes, off, off + n), retries = 1), "data")
            off += n
            progress(off, bytes.size)
        }
        rc(client.request(Cmd.BK_PUT, intArrayOf(2, id), timeout = 8000, retries = 0), "commit")
    }

    /** the working project (object 0) and the sections A..D (2..5), raw (null = empty) */
    fun pullProjects(progress: (Int, Int) -> Unit = { _, _ -> }): Map<Int, ByteArray?> {
        val list = client.listObjects()
        val ids = listOf(0, 2, 3, 4, 5)
        val total = ids.sumOf { list[it]?.first ?: 0 }
        var done = 0
        return ids.associateWith { id ->
            val (len, crc) = list[id] ?: (0 to 0L)
            if (len <= 0) null else client.readObject(id, len, crc) { d, _ -> progress(done + d, total) }.also { done += len }
        }
    }

    object Cmd {
        const val INFO = 1
        const val GET = 2
        const val SET = 3
        const val DESC = 5
        const val PRESET = 8
        const val NAMES = 10
        const val WATCH = 22
        const val CHANGED = 23
        const val RELOAD = 24
        const val PING = 25
        const val TRACK = 27
        const val TRACK_PARAM = 31
        const val TRACK_CHANGED = 32
        const val BK_PUT = 36
    }

    companion object {
        const val SCOPE_TRACK = 0
        const val SCOPE_GLOBAL = 1
        const val FMT_ENUM = 8
        const val FEATURE_BOTANICA = 32
        const val P_MUTE_DEFAULT = 40
        /** the two pages of four, as on the device (params.c PERFORM, SKY): DESC labels */
        val PERFORM = listOf("BLOOM", "DROPS", "FOLEY", "GLTCH")
        val SKY = listOf("WTHR", "BREEZ", "DRIFT", "FILT")
    }
}
