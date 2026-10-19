// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica

import android.Manifest
import android.annotation.SuppressLint
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.notesorcery.botanica.core.Dsp
import com.notesorcery.botanica.core.FieldSample
import com.notesorcery.fm1link.SampleSlot
import kotlin.concurrent.thread
import kotlin.math.abs

/** the phone's microphone, mono 16 bit, up to [maxSeconds] */
class Recorder(private val rate: Int = 44100, private val maxSeconds: Int = 30) {
    @Volatile var recording = false
        private set
    @Volatile var level = 0f
        private set

    /** records on its own thread until [stop]; [done] gets the sound (on that thread) */
    @SuppressLint("MissingPermission")                     // (asked for before)
    fun start(done: (FloatArray) -> Unit) {
        val min = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        fun open(src: Int) = AudioRecord(src, rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, maxOf(min, rate / 5))
        val rec = open(MediaRecorder.AudioSource.UNPROCESSED).takeIf { it.state == AudioRecord.STATE_INITIALIZED }
            ?: open(MediaRecorder.AudioSource.MIC)            // (no raw microphone on this phone)
        recording = true
        thread(name = "field-rec") {
            val out = java.io.ByteArrayOutputStream()
            val buf = ByteArray(rate / 10 * 2)
            rec.startRecording()
            while (recording && out.size() < rate * 2 * maxSeconds) {
                val n = rec.read(buf, 0, buf.size)
                if (n <= 0) break
                out.write(buf, 0, n)
                level = Dsp.peak(Dsp.fromPcm16(buf, n))
            }
            rec.stop(); rec.release()
            recording = false
            level = 0f
            done(Dsp.fromPcm16(out.toByteArray()))
        }
    }

    fun stop() { recording = false }
}

/** plays a float sound once (the preview) */
private object Preview {
    private var track: AudioTrack? = null
    fun play(x: FloatArray, rate: Int) {
        stop()
        if (x.isEmpty()) return
        val pcm = ShortArray(x.size) { (x[it].coerceIn(-1f, 1f) * 32000).toInt().toShort() }
        val t = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
            .setTransferMode(AudioTrack.MODE_STATIC).setBufferSizeInBytes(pcm.size * 2).build()
        t.write(pcm, 0, pcm.size)
        t.play()
        track = t
    }
    fun stop() { track?.let { try { it.stop() } catch (_: Exception) { }; it.release() }; track = null }
}

/**
 * The field recorder: record crickets, rain, a voice; trim, reverse, stretch it into a drone, loop it; hear it; send
 * it to a sample slot (USR1..USR4) for SAMPLE's user presets. The phone does the spectral work, not the FM-1.
 */
@Composable
fun FieldScreen(link: Link) {
    val p = LocalPalette.current
    val ctx = LocalContext.current
    val rate = 44100
    val rec = remember { Recorder(rate) }
    var raw by remember { mutableStateOf<FloatArray?>(null) }
    var recording by remember { mutableStateOf(false) }
    var opts by remember { mutableStateOf(FieldSample(loop = true)) }
    var processed by remember { mutableStateOf<FloatArray?>(null) }
    var slot by remember { mutableIntStateOf(0) }
    var name by remember { mutableStateOf("FIELD") }
    var granted by remember {
        mutableStateOf(ContextCompat.checkSelfPermission(ctx, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED)
    }
    val ask = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted = it }
    DisposableEffect(Unit) { onDispose { rec.stop(); Preview.stop() } }

    fun reprocess(o: FieldSample) {
        opts = o
        val r = raw ?: return
        processed = null
        thread(name = "field-dsp") { val y = o.process(r, rate); link.say("Ready: %.1f s".format(y.size / SampleSlot.RATE.toFloat())); processed = y }
    }

    Column(Modifier.fillMaxSize().padding(16.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Heading("Field recorder", "Crickets, rain, a hummed note: record it, turn it into a texture, send it to the FM-1.")
        Button(
            onClick = {
                if (!granted) { ask.launch(Manifest.permission.RECORD_AUDIO); return@Button }
                if (recording) rec.stop() else {
                    recording = true
                    Preview.stop()
                    rec.start { x -> raw = x; recording = false; reprocess(opts) }
                }
            },
            colors = ButtonDefaults.buttonColors(containerColor = if (recording) p.knob[0] else p.surface, contentColor = if (recording) p.ground else p.knob[0]),
            modifier = Modifier.fillMaxWidth().heightIn(min = 72.dp),
        ) { Text(if (!granted) "Allow the microphone" else if (recording) "■  Stop" else "●  Record (30 s at most)", fontWeight = FontWeight.Bold) }

        val shown = processed ?: raw
        Canvas(Modifier.fillMaxWidth().height(100.dp).semantics { contentDescription = "The sound's waveform" }) {
            drawRect(p.surface)
            val x = shown ?: return@Canvas
            if (x.isEmpty()) return@Canvas
            val step = maxOf(1, x.size / size.width.toInt())
            var i = 0
            var px = 0f
            while (i < x.size) {
                var pk = 0f
                for (j in i until minOf(x.size, i + step)) pk = maxOf(pk, abs(x[j]))
                drawLine(p.knob[1], Offset(px, size.height / 2 * (1 - pk)), Offset(px, size.height / 2 * (1 + pk)), 1f)
                px += 1f; i += step
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(opts.trim, { reprocess(opts.copy(trim = !opts.trim)) }, { Text("TRIM") }, colors = chip(p))
            FilterChip(opts.reverse, { reprocess(opts.copy(reverse = !opts.reverse)) }, { Text("REVERSE") }, colors = chip(p))
            FilterChip(opts.loop, { reprocess(opts.copy(loop = !opts.loop)) }, { Text("LOOP") }, colors = chip(p))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("STRETCH", color = p.dim)
            listOf(1, 2, 4, 8).forEach { f ->
                FilterChip(opts.stretch == f, { reprocess(opts.copy(stretch = f)) }, { Text(if (f == 1) "OFF" else "×$f") }, colors = chip(p))
            }
        }
        Button(onClick = { processed?.let { Preview.play(it, SampleSlot.RATE) } }, enabled = processed != null,
            modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("▶  Hear it") }
        OutlinedTextField(name, { name = it.uppercase().take(8) }, label = { Text("Name (8 letters)") }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (s in 0 until 4) FilterChip(slot == s, { slot = s }, { Text("USR${s + 1}") }, colors = chip(p))
        }
        Button(
            onClick = {
                val r = raw ?: return@Button
                val o = opts
                link.run("Upload") { g ->
                    val sl = o.slot(name.ifBlank { "FIELD" }, r, rate)
                    g.client.uploadSample(slot, sl) { d, t -> link.progress(d, t) }
                    link.say("$name is in USR${slot + 1}: on a SAMPLE track, turn SET to USR${slot + 1}.")
                }
            },
            enabled = raw != null && link.state == Link.State.CONNECTED,
            modifier = Modifier.fillMaxWidth().heightIn(min = 56.dp),
        ) { Text("Send to the FM-1 (USR${slot + 1})", fontWeight = FontWeight.Bold) }
        link.progress?.let { androidx.compose.material3.LinearProgressIndicator({ it }, Modifier.fillMaxWidth()) }
        Text(link.message, color = p.dim)
    }
}

@Composable
private fun chip(p: Palette) = FilterChipDefaults.filterChipColors(selectedContainerColor = p.knob[1], selectedLabelColor = p.ground)
