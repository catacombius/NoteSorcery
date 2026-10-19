// SPDX-License-Identifier: GPL-3.0-only
package com.notesorcery.botanica.core

/**
 * BOTANICA's sounds by mood, as the FM-1's browser groups them (firmware/src/ui.c BANK): each factory preset's mood
 * and engine. The app auditions one with PRESET (engine, the preset's index in that engine's NAMES list).
 */
object Moods {
    enum class Mood(val label: String, val tag: String, val blurb: String) {
        PETALS("Petals", "PETL", "felt keys and plucks"),
        CANOPY("Canopy", "CANP", "pads, strings and drones"),
        DEW("Dew", "DEW", "droplets, grain and glitter"),
        VOICES("Voices", "VOIC", "breath and choirs"),
        ROOTS("Roots", "ROOT", "warm bass"),
        WEATHER("Weather", "WTHR", "foley and field textures"),
        HYPER("Hyper", "HYPR", "hyperpop leads"),
    }

    /** a factory preset: its mood, its engine (Btp1.Layout.ENGINES index) and name */
    data class P(val mood: Mood, val engine: Int, val name: String)

    val PRESETS = listOf(
        P(Mood.PETALS, 0, "FELT UPRIGHT"),
        P(Mood.PETALS, 0, "NURTURE KEYS"),
        P(Mood.PETALS, 0, "PNO+STRINGS"),
        P(Mood.PETALS, 0, "SOFT PEDAL"),
        P(Mood.PETALS, 0, "HOLLOW FELT"),
        P(Mood.PETALS, 0, "BOX PIANO"),
        P(Mood.PETALS, 0, "TINE DREAM"),
        P(Mood.PETALS, 3, "GRAND PNO"),
        P(Mood.PETALS, 3, "DUSTY PNO"),
        P(Mood.PETALS, 3, "LOFI KEYS"),
        P(Mood.PETALS, 1, "KALIMBA"),
        P(Mood.PETALS, 1, "MARIMBA"),
        P(Mood.PETALS, 1, "MUSIC BOX"),
        P(Mood.PETALS, 1, "HARP"),
        P(Mood.PETALS, 5, "FAIRY HARP"),
        P(Mood.PETALS, 5, "NYLON GTR"),
        P(Mood.PETALS, 5, "STEEL GTR"),
        P(Mood.PETALS, 5, "MUTED GTR"),
        P(Mood.PETALS, 5, "KOTO"),
        P(Mood.PETALS, 5, "THUMB PNO"),
        P(Mood.PETALS, 5, "VIBRA BAR"),
        P(Mood.PETALS, 8, "BELL TONE"),
        P(Mood.PETALS, 5, "WIND CHIMES"),
        P(Mood.PETALS, 5, "BELL TREE"),
        P(Mood.CANOPY, 0, "STRING BLOOM"),
        P(Mood.CANOPY, 2, "CANOPY PAD"),
        P(Mood.CANOPY, 2, "MOSS STRINGS"),
        P(Mood.CANOPY, 2, "DAWN PAD"),
        P(Mood.CANOPY, 2, "SINE PAD"),
        P(Mood.CANOPY, 8, "GLASS PAD"),
        P(Mood.CANOPY, 8, "HOLLOW AIR"),
        P(Mood.CANOPY, 8, "SOFT SAW"),
        P(Mood.CANOPY, 6, "PNO CLOUD"),
        P(Mood.CANOPY, 6, "PNO FREEZE"),
        P(Mood.CANOPY, 6, "DEEP DRONE"),
        P(Mood.CANOPY, 5, "GLASS BOWL"),
        P(Mood.CANOPY, 5, "CELLO BOW"),
        P(Mood.CANOPY, 5, "TANPURA"),
        P(Mood.DEW, 1, "GLASS DROPS"),
        P(Mood.DEW, 1, "RAIN ARP"),
        P(Mood.DEW, 1, "DEW CHIRP"),
        P(Mood.DEW, 1, "BUBBLE FM"),
        P(Mood.DEW, 1, "SPARKLE"),
        P(Mood.DEW, 6, "SHIMMER"),
        P(Mood.DEW, 6, "OCTAVE DEW"),
        P(Mood.DEW, 6, "GLITTER"),
        P(Mood.DEW, 6, "PNO RAIN"),
        P(Mood.VOICES, 7, "AIR CHOIR"),
        P(Mood.VOICES, 7, "WHISPER PAD"),
        P(Mood.VOICES, 7, "SOUL OOH"),
        P(Mood.VOICES, 7, "ANGEL AH"),
        P(Mood.VOICES, 7, "VOX CHOP"),
        P(Mood.VOICES, 7, "HUM"),
        P(Mood.VOICES, 7, "VOX PAD"),
        P(Mood.VOICES, 8, "CHOIR WAVE"),
        P(Mood.ROOTS, 2, "SOFT SUB"),
        P(Mood.ROOTS, 2, "FRETLESS"),
        P(Mood.ROOTS, 2, "WARM ROOT"),
        P(Mood.WEATHER, 4, "TWIG SNAP"),
        P(Mood.WEATHER, 4, "LEAF CRUNCH"),
        P(Mood.WEATHER, 4, "WATER DROP"),
        P(Mood.WEATHER, 4, "BRUSH"),
        P(Mood.WEATHER, 4, "BREATH"),
        P(Mood.WEATHER, 4, "KEY CLICK"),
        P(Mood.WEATHER, 4, "FIRE CRACKLE"),
        P(Mood.WEATHER, 4, "CRICKETS"),
        P(Mood.WEATHER, 4, "WIND"),
        P(Mood.WEATHER, 4, "RAIN"),
        P(Mood.WEATHER, 4, "OCEAN"),
        P(Mood.WEATHER, 4, "VINYL"),
        P(Mood.WEATHER, 5, "HAND DRUM"),
        P(Mood.WEATHER, 5, "WOOD BLOCK"),
        P(Mood.WEATHER, 3, "GM KIT"),
        P(Mood.HYPER, 2, "HYPER LEAD"),
        P(Mood.HYPER, 2, "SUPERSAW"),
        P(Mood.HYPER, 2, "PETAL PLUCK"),
        P(Mood.HYPER, 2, "GLIDE LEAD"),
        P(Mood.HYPER, 8, "FLUTE TONE"),
    )

    fun of(mood: Mood): List<P> = PRESETS.filter { it.mood == mood }

    /** the mood of a preset name (null: a user preset or one the table does not know) */
    fun moodOf(name: String): Mood? = PRESETS.firstOrNull { it.name.equals(name.trim(), ignoreCase = true) }?.mood

    /**
     * where a preset is in its engine's list, from the device's NAMES reply ([names] for [P.engine]); null when the
     * device has no preset of that name (an older or newer firmware)
     */
    fun indexIn(p: P, names: List<String>): Int? = names.indexOfFirst { it.trim().equals(p.name, ignoreCase = true) }.takeIf { it >= 0 }
}
