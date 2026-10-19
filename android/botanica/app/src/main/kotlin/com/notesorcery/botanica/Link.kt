// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import android.content.Context
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiInputPort
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.notesorcery.botanica.core.Garden
import com.notesorcery.fm1link.Fm1Midi
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

/**
 * The FM-1 over android.media.midi (USB, or a BLE MIDI device the phone has opened), and what the screens show of it.
 * Every request runs on one worker thread, in order ([run]); the state the screens read is Compose state, written on
 * the main thread. While connected: WATCH kept alive with a PING every second, the transport read 4 times a second.
 */
class Link(context: Context) {
    enum class State { IDLE, SEARCHING, CONNECTED, NOT_BOTANICA, ERROR }

    var state by mutableStateOf(State.IDLE)
        private set
    var message by mutableStateOf("Plug the FM-1 in over USB (an OTG adapter), then tap Connect.")
        private set
    var deviceName by mutableStateOf("")
        private set
    var device by mutableStateOf<Garden.Device?>(null)
        private set
    /** the globals the screens show, by DESC label */
    val values = mutableStateMapOf<String, Int>()
    var tracks by mutableStateOf<Garden.Tracks?>(null)
        private set
    var playing by mutableStateOf(false)
        private set
    var bpm by mutableStateOf(120)
        private set
    var beat by mutableStateOf(0L)
        private set
    /** when each track last played a note (System.nanoTime; the garden's flowers) */
    val lastNote = mutableStateListOf<Long>().apply { repeat(8) { add(0L) } }
    /** a busy job's progress 0..1, or null */
    var progress by mutableStateOf<Float?>(null)
        private set

    private val mm = context.getSystemService(MidiManager::class.java)
    private val io = Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "fm1-link").apply { isDaemon = true } }
    private val main = Handler(Looper.getMainLooper())
    private var midi: MidiDevice? = null
    private var inPort: MidiInputPort? = null
    private var outPort: MidiOutputPort? = null
    private val sendLock = Any()
    private val timers: MutableList<ScheduledFuture<*>> = java.util.Collections.synchronizedList(ArrayList())
    @Volatile var garden: Garden? = null
        private set
    /** called on each transport read with the beat (the drift planner follows the song with it) */
    @Volatile var onBeat: (Long) -> Unit = {}

    private fun ui(f: () -> Unit) { main.post(f) }

    init {
        @Suppress("DEPRECATION")                           // (the Executor overload is API 33)
        mm?.registerDeviceCallback(object : MidiManager.DeviceCallback() {
            override fun onDeviceAdded(info: MidiDeviceInfo) { if (state == State.IDLE || state == State.ERROR) connect() }
            override fun onDeviceRemoved(info: MidiDeviceInfo) {
                if (midi?.info?.id == info.id) { close(); state = State.IDLE; message = "The FM-1 went away." }
            }
        }, main)
    }

    @Suppress("DEPRECATION")
    private fun allDevices(): List<MidiDeviceInfo> = when {
        mm == null -> emptyList()
        Build.VERSION.SDK_INT >= 33 -> mm.getDevicesForTransport(MidiManager.TRANSPORT_MIDI_BYTE_STREAM).toList()
        else -> mm.devices.toList()
    }

    private fun nameOf(i: MidiDeviceInfo): String {
        val p = i.properties
        return p.getString(MidiDeviceInfo.PROPERTY_NAME) ?: p.getString(MidiDeviceInfo.PROPERTY_PRODUCT) ?: "MIDI device ${i.id}"
    }

    /** the MIDI devices that can talk both ways; the FM-1 (by its name) first */
    fun candidates(): List<Pair<MidiDeviceInfo, String>> = allDevices()
        .filter { it.inputPortCount > 0 && it.outputPortCount > 0 }
        .map { it to nameOf(it) }
        .sortedByDescending { (_, n) -> n.contains("FM-1", true) || n.contains("FM1", true) || n.contains("M-VAVE", true) }

    /** opens [info] (null: the best candidate) and asks it what it is */
    fun connect(info: MidiDeviceInfo? = null) {
        if (mm == null) { state = State.ERROR; message = "This phone has no MIDI support."; return }
        val pick = info ?: candidates().firstOrNull()?.first
        if (pick == null) { state = State.IDLE; message = "No MIDI device found. Plug the FM-1 in (USB-C OTG) and try again."; return }
        close()
        state = State.SEARCHING
        deviceName = nameOf(pick)
        message = "Opening $deviceName…"
        mm.openDevice(pick, { dev ->
            if (dev == null) { state = State.ERROR; message = "Could not open $deviceName."; return@openDevice }
            midi = dev
            val input = dev.openInputPort(0)
            val output = dev.openOutputPort(0)
            if (input == null || output == null) { state = State.ERROR; message = "$deviceName is busy (another app?)."; return@openDevice }
            inPort = input
            outPort = output
            val g = Garden({ b -> synchronized(sendLock) { input.send(b, 0, b.size) } })
            output.connect(object : MidiReceiver() {
                override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) { g.feed(msg, offset, count) }
            })
            hook(g)
            garden = g
            io.execute { hello(g) }
        }, main)
    }

    private fun hook(g: Garden) {
        g.onChanged = { scope, id, v -> if (scope == Garden.SCOPE_GLOBAL) g.labelOf(id)?.let { l -> ui { values[l] = v } } }
        g.onReload = { _, _, _ -> io.execute { refreshTracks() } }
        g.onTrackChanged = { _, _, _ -> io.execute { refreshTracks() } }
        g.onNote = { ch, _, vel ->
            if (vel > 0) {
                val drum = (device?.caps?.drumChannel ?: 10) - 1
                val t = when { ch < 6 -> ch; ch == drum -> 6; ch == ((drum + 1) and 15) -> 7; else -> -1 }
                if (t >= 0) { val now = System.nanoTime(); ui { lastNote[t] = now } }
            }
        }
    }

    /** on the worker: INFO, CAPS, the globals' DESC, WATCH, then the timers */
    private fun hello(g: Garden) {
        try {
            val d = g.connect()
            ui { device = d }
            if (!d.botanica) {
                ui {
                    state = State.NOT_BOTANICA
                    message = "${d.firmware} is not BOTANICA. Install BOTANICA from the web installer (catacombius.github.io/NoteSorcery/botanica), then reconnect."
                }
                return
            }
            g.watch(true)
            val v = (Garden.PERFORM + Garden.SKY + listOf("BMODE", "RAMP")).filter { g.global(it) != null }.associateWith { g.get(it) }
            refreshTracks()
            ui {
                values.clear(); values.putAll(v)
                state = State.CONNECTED
                message = "${d.firmware} on $deviceName"
            }
            if (garden !== g) return                       // (closed meanwhile)
            timers.add(io.scheduleWithFixedDelay({ quiet { g.ping() } }, 1, 1, TimeUnit.SECONDS))
            timers.add(io.scheduleWithFixedDelay({ quiet { readTransport(g) } }, 0, 250, TimeUnit.MILLISECONDS))
        } catch (e: Exception) {
            ui { state = State.ERROR; message = "No answer from $deviceName: ${e.message}" }
        }
    }

    private inline fun quiet(f: () -> Unit) { try { f() } catch (_: Exception) { } }

    private fun readTransport(g: Garden) {
        val t = g.client.transport()
        onBeat(t.beat)
        ui { playing = t.playing; bpm = t.bpm; beat = t.beat }
    }

    private fun refreshTracks() {
        val g = garden ?: return
        quiet { val t = g.tracks(); ui { tracks = t } }
    }

    /** a job on the worker; its error ends up in [message] */
    fun run(what: String, job: (Garden) -> Unit) {
        val g = garden ?: run { message = "Connect the FM-1 first."; return }
        io.execute {
            try { job(g) } catch (e: Exception) { ui { message = "$what: ${e.message}" } }
            ui { progress = null }
        }
    }

    fun progress(done: Int, total: Int) { ui { progress = if (total > 0) done.toFloat() / total else null } }
    fun say(text: String) { ui { message = text } }

    private val pending = ConcurrentHashMap<String, Int>()
    @Volatile private var flushing = false

    /** a global by its label: shown at once, sent coalesced (a fader's drag sends its latest value) */
    fun setGlobal(label: String, v: Int) {
        values[label] = v
        pending[label] = v
        if (flushing) return
        flushing = true
        io.execute {
            flushing = false
            val g = garden ?: return@execute
            for (k in pending.keys.toList()) pending.remove(k)?.let { x -> quiet { g.set(k, x) } }
        }
    }

    fun selectTrack(t: Int) = run("Select") { g -> val r = g.tracks(t); ui { tracks = r } }
    fun transport(op: Int) = run("Transport") { g -> val t = g.client.transport(op); ui { playing = t.playing } }

    /** raw MIDI to the FM-1 (the chord pads), at once, not behind the requests */
    fun sendMidi(b: ByteArray) { synchronized(sendLock) { inPort?.send(b, 0, b.size) } }

    fun note(track: Int, note: Int, vel: Int) {
        val ch = Fm1Midi.channelOf(track, device?.caps?.drumChannel ?: 10)
        sendMidi(byteArrayOf(((if (vel > 0) 0x90 else 0x80) or ch).toByte(), note.toByte(), vel.coerceIn(0, 127).toByte()))
    }

    fun close() {
        synchronized(timers) { timers.forEach { it.cancel(false) }; timers.clear() }
        val g = garden
        garden = null
        val o = outPort; val i = inPort; val m = midi
        outPort = null; inPort = null; midi = null
        io.execute {
            if (g != null) quiet { g.watch(false) }
            quiet { o?.close() }; quiet { i?.close() }; quiet { m?.close() }
        }
        device = null
        tracks = null
    }
}
