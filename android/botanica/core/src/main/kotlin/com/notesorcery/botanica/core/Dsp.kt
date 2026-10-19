// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * The field recorder's processing, on the phone where the cycles are (the FM-1 only plays the result from a sample
 * slot): trim the silence, normalise, reverse, a looping crossfade, and a PaulStretch-like stretch that turns a few
 * seconds of crickets or a hummed note into a long, smeared drone (random phases over windowed FFT frames: the
 * botanica "spectral" smear). Mono float in -1..1 throughout.
 */
object Dsp {
    /** from the first to the last sample above [thresholdDb] (dBFS), with [padMs] kept on each side */
    fun trimSilence(x: FloatArray, rate: Int, thresholdDb: Double = -45.0, padMs: Int = 20): FloatArray {
        val th = 10.0.pow(thresholdDb / 20).toFloat()
        val first = x.indexOfFirst { abs(it) > th }
        if (first < 0) return FloatArray(0)
        val last = x.indexOfLast { abs(it) > th }
        val pad = rate * padMs / 1000
        return x.copyOfRange(max(0, first - pad), min(x.size, last + pad + 1))
    }

    fun peak(x: FloatArray): Float = x.maxOfOrNull { abs(it) } ?: 0f

    /** the peak to [peakDb] dBFS (silence stays silence) */
    fun normalize(x: FloatArray, peakDb: Double = -1.0): FloatArray {
        val p = peak(x)
        if (p < 1e-6f) return x.copyOf()
        val g = (10.0.pow(peakDb / 20) / p).toFloat()
        return FloatArray(x.size) { x[it] * g }
    }

    fun reverse(x: FloatArray): FloatArray = FloatArray(x.size) { x[x.size - 1 - it] }

    /** linear fades in and out, [ms] each */
    fun fade(x: FloatArray, rate: Int, ms: Int = 10): FloatArray {
        val n = min(x.size / 2, rate * ms / 1000)
        val y = x.copyOf()
        for (i in 0 until n) {
            val g = i.toFloat() / n
            y[i] *= g
            y[y.size - 1 - i] *= g
        }
        return y
    }

    /**
     * a seamless loop: the last [ms] crossfaded (equal power) into the start, the result that much shorter, so the
     * sample slot can loop it end to start with no click
     */
    fun loopCrossfade(x: FloatArray, rate: Int, ms: Int = 120): FloatArray {
        val n = min(x.size / 3, rate * ms / 1000)
        if (n < 2) return x.copyOf()
        val len = x.size - n
        val y = x.copyOfRange(0, len)
        for (i in 0 until n) {
            val t = i.toDouble() / n
            val a = cos(t * PI / 2).toFloat()
            val b = sin(t * PI / 2).toFloat()
            y[i] = x[len + i] * a + x[i] * b
        }
        return y
    }

    /** RMS level, dBFS */
    fun rmsDb(x: FloatArray): Double {
        if (x.isEmpty()) return -120.0
        val s = x.sumOf { it.toDouble() * it }
        return 10 * log10(s / x.size + 1e-24)
    }

    /**
     * PaulStretch-like: [factor] times longer. Hann frames of [window] samples (a power of two) taken [factor] times
     * slower than they are written, each frame's spectrum kept in magnitude with random phases, overlap-added at
     * half a window. A drone keeps its colour, loses its transients.
     */
    fun paulStretch(x: FloatArray, factor: Double, window: Int = 4096, seed: Long = 1): FloatArray {
        require(window >= 64 && window and (window - 1) == 0) { "the window is a power of two" }
        require(factor >= 1.0)
        if (x.isEmpty()) return FloatArray(0)
        val half = window / 2
        val outLen = (x.size * factor).toInt() + window
        val out = FloatArray(outLen)
        val hann = FloatArray(window) { (0.5 - 0.5 * cos(2 * PI * it / window)).toFloat() }
        val rnd = java.util.Random(seed)
        val re = DoubleArray(window)
        val im = DoubleArray(window)
        var pos = 0.0
        var w = 0
        val step = half / factor
        while (w + window <= outLen) {
            val p = pos.toInt()
            for (i in 0 until window) {
                val v = if (p + i < x.size) x[p + i] else 0f
                re[i] = (v * hann[i]).toDouble()
                im[i] = 0.0
            }
            fft(re, im, false)
            for (k in 0..half) {                         // the magnitude, a random phase (mirrored: a real signal)
                val m = sqrt(re[k] * re[k] + im[k] * im[k])
                val ph = rnd.nextDouble() * 2 * PI
                re[k] = m * cos(ph)
                im[k] = m * sin(ph)
                if (k in 1 until half) {
                    re[window - k] = re[k]
                    im[window - k] = -im[k]
                }
            }
            im[0] = 0.0
            im[half] = 0.0
            fft(re, im, true)
            for (i in 0 until window)
                out[w + i] += (re[i] * hann[i]).toFloat()
            w += half
            pos += step
            if (pos >= x.size) pos = 0.0                 // (a short source: around again)
        }
        return normalize(out.copyOfRange(0, (x.size * factor).toInt()), 20 * log10(max(peak(x), 1e-6f).toDouble()))
    }

    /** in-place radix-2 FFT (inverse: scaled by 1/n) */
    fun fft(re: DoubleArray, im: DoubleArray, inverse: Boolean) {
        val n = re.size
        var j = 0
        for (i in 1 until n) {                           // bit reversal
            var bit = n shr 1
            while (j and bit != 0) {
                j = j xor bit
                bit = bit shr 1
            }
            j = j or bit
            if (i < j) {
                var t = re[i]; re[i] = re[j]; re[j] = t
                t = im[i]; im[i] = im[j]; im[j] = t
            }
        }
        var len = 2
        while (len <= n) {
            val ang = 2 * PI / len * if (inverse) 1 else -1
            val wr = cos(ang)
            val wi = sin(ang)
            var i = 0
            while (i < n) {
                var cr = 1.0
                var ci = 0.0
                for (k in 0 until len / 2) {
                    val ar = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci
                    val ai = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr
                    re[i + k + len / 2] = re[i + k] - ar
                    im[i + k + len / 2] = im[i + k] - ai
                    re[i + k] += ar
                    im[i + k] += ai
                    val ncr = cr * wr - ci * wi
                    ci = cr * wi + ci * wr
                    cr = ncr
                }
                i += len
            }
            len = len shl 1
        }
        if (inverse) for (i in 0 until n) {
            re[i] /= n
            im[i] /= n
        }
    }

    /** 16-bit PCM (little-endian bytes, as AudioRecord gives them) -> float */
    fun fromPcm16(b: ByteArray, n: Int = b.size): FloatArray =
        FloatArray(n / 2) { (((b[2 * it + 1].toInt() shl 8) or (b[2 * it].toInt() and 0xFF)).toShort()) / 32768f }
}
