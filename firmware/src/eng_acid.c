/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID: a TB-303 on a synth track (NoteSorcery). The voice is X0X's bass303 (x0x/bass303.c: Open303 by Robin
 * Schmidt, MIT, through schwung-303; the Devilfish ranges and the Soft / RAT drive), one per track, in the track's
 * engine arena (engines.c eng_arena_of). It is monophonic (engine_t.poly 1) and its own envelopes are the
 * voice's amplitude (ownenv): the ADSR does nothing here, the track's LEVEL, PAN, sends, FILTER and SLICER do.
 *
 * The sequencer drives it as a 303 is driven: an ACCENT step (velocity 127; >= 120 from MIDI) is an accented
 * note, a SLIDE step (or a glide on a legato note: voice.c gstep) slides into the next one. The EDIT values:
 *   CUT  cutoff (314 .. 2394 Hz; LFO -> FLT and ENV -> FLT move it)   RES  resonance
 *   ENV  env mod                                                      DEC  decay (200 .. 2000 ms)
 *   ACC  accent                                                       WAVE saw / square
 *   DRV  drive: 0 off, 1..63 SOFT, 64..127 RAT                        SLD  slide time (2 .. 360 ms)
 * The global TUNE (cents) tunes it (bass303 TUNE). Float DSP: it runs only in the audio ISR (x0x/README.md). */
/* (one translation unit: bass303.c's file-scope names get a prefix, so they meet nothing of SLOOP's) */
#define blep b303_blep
#define bq_low_shelf b303_bq_low_shelf
#define bq_lowpass b303_bq_lowpass
#define bq_run b303_bq_run
#define calc_envmod b303_calc_envmod
#define clear_audio_state b303_clear_audio_state
#define decim_hb1 b303_decim_hb1
#define decim_hb2 b303_decim_hb2
#define drive_block b303_drive_block
#define env_tables b303_env_tables
#define hb b303_hb
#define idle_advance b303_idle_advance
#define powers b303_powers
#define rat_coeffs b303_rat_coeffs
#define rc_coeff b303_rc_coeff
#define run b303_run
#define set_accent_state b303_set_accent_state
#define wave_names b303_wave_names
#define drv_names b303_drv_names
#define params b303_params
#define recip b303_recip
#include "x0x/bass303.c"
#undef blep
#undef bq_low_shelf
#undef bq_lowpass
#undef bq_run
#undef calc_envmod
#undef clear_audio_state
#undef decim_hb1
#undef decim_hb2
#undef drive_block
#undef env_tables
#undef hb
#undef idle_advance
#undef powers
#undef rat_coeffs
#undef rc_coeff
#undef run
#undef set_accent_state
#undef wave_names
#undef drv_names
#undef params
#undef recip

static bass303_t *acid_of(const track_t *t) { return (bass303_t *)eng_arena_of(t, ENGI_ACID); }

/* the track's values into the 303's pots (audio ISR, once a block): only what changed is set (bass303_set
 * recomputes a coefficient or two); cut is the cutoff with its modulation */
static void acid_pots(bass303_t *b, const track_t *t, int32_t cut)
{
    int32_t drv = t->p[P_E6], tune = 64 + song.g[G_TUNE] * 2 / 5;   /* 1 cent = 0.254 Hz = 0.41 pot */
    int32_t want[BASS303_NPARAMS];
    uint32_t i;
    want[BASS303_CUTOFF] = clamp(cut, 0, 127);
    want[BASS303_RESO] = clamp(t->p[P_E1], 0, 127);
    want[BASS303_ENVMOD] = clamp(t->p[P_E2], 0, 127);
    want[BASS303_DECAY] = clamp(t->p[P_E3], 0, 127);
    want[BASS303_ACCENT] = clamp(t->p[P_E4], 0, 127);
    want[BASS303_WAVE] = t->p[P_E5] != 0;
    want[BASS303_TUNE] = clamp(tune, 0, 127);
    want[BASS303_VOLUME] = 96;                          /* (peaks ~0.5: the track's LEVEL does the rest) */
    want[BASS303_DRVTYPE] = drv <= 0 ? BASS303_DRV_OFF : drv < 64 ? BASS303_DRV_SOFT : BASS303_DRV_RAT;
    want[BASS303_DRIVE] = drv <= 0 ? 0 : drv < 64 ? drv * 2 : (drv - 64) * 2 + 1;
    want[BASS303_SLIDE] = clamp(t->p[P_E7], 0, 127);
    want[BASS303_ACCDEC] = 7;                           /* (the stock 200 ms) */
    for (i = 0; i < BASS303_NPARAMS; i++)
        if (b->pot[i] != (uint8_t)want[i] || !b->tuning)
            bass303_set(b, (int)i, (int)want[i]);
}

static bass303_t *acid_ready(track_t *t)
{
    bass303_t *b = acid_of(t);
    if (b && b->tuning == 0.0f) {                       /* a fresh arena (cleared when the engine took it) */
        bass303_init(b);
        acid_pots(b, t, t->p[P_E0]);
    }
    return b;
}

static void acid_note_on(track_t *t, voice_t *v)
{
    bass303_t *b = acid_ready(t);
    if (!b)
        return;
    bass303_note_on(b, v->note, v->vel >= 120u, v->gstep != 0 && b->gate);
}

static void acid_block(track_t *t)
{
    bass303_t *b;
    if ((uint32_t)(t - trk) >= NPART)
        return;
    b = acid_ready(t);
    if (b && !t->v[0].active && b->gate)
        bass303_note_off(b);                            /* (the voice was given up or killed: the 303 lets go too) */
}

/* the amp envelope has died with the gate off: the voice ends (voice.c, engine_t.done) */
static int acid_done(track_t *t, voice_t *v)
{
    bass303_t *b = acid_of(t);
    if (!b)
        return 1;
    if (!v->gate && b->gate)
        bass303_note_off(b);
    return !b->gate && (b->idle || b->amp_y < 1e-5f);
}

static void acid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static float buf[CTL];
    bass303_t *b = acid_of(t);
    uint32_t i;
    int32_t a, da, k = VOICE_FS * 2;                    /* a peak of ~0.5 -> VOICE_FS */
    if (!b || n > CTL)
        return;
    if (!v->gate && b->gate)
        bass303_note_off(b);
    acid_pots(b, t, t->p[P_E0] + (m->cutoff >> 8));
    bass303_render(b, buf, (int)n);
    a = mulq15(m->amp0, k);
    da = (mulq15(m->amp1, k) - a) >> CTL_LOG2;
    for (i = 0; i < n; i++) {
        int32_t x = (int32_t)(buf[i] * 32767.0f);
        x = x > 65535 ? 65535 : x < -65535 ? -65535 : x;
        out[i] += (int32_t)(((int64_t)x * a) >> 15);
        a += da;
    }
}

/* {CUT, RES, ENV, DEC, ACC, WAVE, DRV, SLD}; MONO (legato: a SLIDE step slides). Names as ui.c BANK[] lists them */
static const preset_t ACID_PRESETS[] = {
    {"ACID CLASS", {40, 90, 70, 50, 80, 0, 0, 21}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 18, 10)},
    {"ACID SQUARE", {45, 85, 60, 40, 70, 1, 0, 21}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 18, 10)},
    {"SQUELCH", {30, 120, 100, 60, 100, 0, 0, 30}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 22, 14)},
    {"ACID RAT", {50, 100, 80, 40, 90, 0, 100, 21}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 14, 8)},
    {"SOFT 303", {70, 50, 30, 80, 40, 0, 0, 40}, {0, 0, 127, 0}, 0, 1, FX(0, 10, 20, 16)},
    {"DEEP 303", {20, 60, 40, 90, 50, 1, 20, 21}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 10, 6)},
    {"SCREAMER", {60, 127, 110, 30, 127, 0, 120, 15}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 16, 10)},
    {"RUBBER 303", {35, 70, 90, 70, 60, 1, 0, 60}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 20, 12)},
};

static const char *const N_ACID_WAVE[] = {"SAW", "SQR", 0};

static const engine_t ENG_ACID = {
    "ACID", {"FILTER", "TONE"},
    {
        {"CUT", F_PCT, 0, 127, 40, 0, 0},
        {"RES", F_PCT, 0, 127, 90, 0, 0},
        {"ENV", F_PCT, 0, 127, 70, 0, 0},
        {"DEC", F_PCT, 0, 127, 50, 0, 0},
        {"ACC", F_PCT, 0, 127, 80, 0, 0},
        {"WAVE", F_ENUM, 0, 1, 0, N_ACID_WAVE, 0},
        {"DRV", F_INT, 0, 127, 0, 0, 0},
        {"SLD", F_PCT, 0, 127, 21, 0, 0},
    },
    ACID_PRESETS, NELEM(ACID_PRESETS), 0, acid_note_on, acid_render,
    0xFB20, {P_E0, P_E1, P_E2, P_E3}, 1, 0, 0, acid_block,
    .ownenv = 1, .done = acid_done,
};
