/* SPDX-License-Identifier: GPL-3.0-only */
/* DROPLET (BOTANICA): modal plucks (kalimba, marimba, music box, glass, harp) or a chirped 2-operator FM drop, and
 * the pitch-jumper: each played note drops a few echoes of itself, some an octave, a twelfth or two octaves up,
 * like water from leaves. All fixed point.
 *
 *   MODEL   KALIM MARIM BOX GLASS HARP: three sine partials at the model's ratios, each with its own decay (the
 *           high ones die first); CHRP3 CHRP4 CHRP7: a carrier modulated at 3x, 4x or 7x, its index falling, and a
 *           chirp that starts the note an octave up and falls in ~20 ms
 *   DECAY   the fundamental's decay (shorter up the keyboard); BRIT the upper partials / the FM index and a mallet click
 *   DROPS   echoes a note drops: 0..3 (the song's DROPLETS, G_DROPS, adds to it); TIME their spacing (synced to the
 *           tempo); FALL how much quieter each one is
 *   JUMP    the chance an echo jumps +12, +19 or +24 semitones (the song's DROPLETS raises it too)
 *   SPRD    a random detune of each echo (cents), so the drops never land quite on the same spot
 * The echoes are events, not audio: they wait in a small queue (drop_block, from the sequencer's block) and start
 * as notes of the same track through the voice budget (an echo never steals a voice: with the budget full it is
 * dropped), 50 ms long (the partials ring on). The engine's envelopes are the voice's amplitude (ownenv); done() ends it when every partial is quiet.
 * A released note is damped, as a hand on the bar: its partials ring at most DROP_Q_RING (tau 0.54 s), so a voice
 * is free a few seconds after its note-off; held, the key scaling stops at DROP_Q_MAX (tau 4.4 s).
 *
 * Voice state: ph[0..2] the partials' phases (CHRP: carrier, modulator), s[0..2] their envelopes (Q24), s[3] the
 * chirp (Q15), s[4] the noise state, s[5] the click's envelope, s[6] the detune of an echo (1/4096 semitone). */
static void trk_note_on(track_t *t, uint32_t note, uint32_t vel);   /* voice.c */
static void trk_note_off(track_t *t, uint32_t note);
static uint32_t voices_busy(void);
static const char *const N_DROP_MODEL[] = {"KALIM", "MARIM", "BOX", "GLASS", "HARP", "CHRP3", "CHRP4", "CHRP7"};
static const char *const N_DROP_TIME[] = {"1/16", "1/32", "16T", "32T", "1/64"};
#define DROP_CHIRP 5u                                    /* MODEL >= this: the FM drop */
#define DROP_ONE (1 << 24)
#define DROP_Q_MAX 112                                   /* the longest ring held: tau 1500 << 7 samples (4.4 s) */
#define DROP_Q_RING 64                                   /* released: tau 1500 << 4 samples (0.54 s), -60 dB in 3.8 s */
static const struct {
    uint16_t r1, r2;                                     /* partials 2 and 3: ratio to the fundamental, Q10 */
    uint16_t a1, a2;                                     /* their levels, Q15 */
    int8_t d1, d2;                                       /* their decays: 1/16 octaves shorter than the fundamental's */
} DROP_MODES[DROP_CHIRP] = {
    {6144, 13824, 8000, 2600, -40, -64},                 /* KALIM: 1, 6, 13.5 */
    {4086, 10138, 14000, 5000, -36, -60},                /* MARIM: 1, 3.99, 9.9 (the tuned bar) */
    {2826, 5530, 10000, 5000, -20, -40},                 /* BOX:   1, 2.76, 5.4 (a free bar: the comb's tines) */
    {2376, 4352, 18000, 11500, -8, -16},                 /* GLASS: 1, 2.32, 4.25 */
    {2048, 3072, 16000, 8000, -16, -32},                 /* HARP:  1, 2, 3 */
};
static const uint8_t DROP_FM_RATIO[3] = {3, 4, 7};

/* the echo queue: notes due on a track, in samples (all tracks share it) */
#define DROPQ 12u
static struct { uint8_t on, trk, note, vel; int16_t fine; uint32_t left; } dropq[DROPQ];   /* (vel 0: its note-off) */
static uint8_t drop_echoing;                             /* an echo is starting: it drops no echoes of its own */
static int16_t drop_fine_next;                           /* the detune the echo starting now takes */
static int32_t drop_rng = 0x5EED1234;
static uint32_t drop_echoes, drop_dropped;               /* echoes started, echoes left out (no voice): host tests */

static uint32_t drop_spacing(uint32_t time)              /* the echoes' spacing in samples, at the song tempo */
{
    static const uint8_t DEN[5] = {4, 8, 6, 12, 16};
    uint32_t beat = (uint32_t)FS * 60u / (uint32_t)clamp(song.g[G_BPM], 20, 300);
    return beat / DEN[time % 5u];
}

/* the echoes of a played note (DROPS, JUMP, the song's DROPLETS) into the queue */
static void drop_schedule(track_t *t, uint32_t note, uint32_t vel)
{
    const int16_t *p = t->p;
    int32_t drops = p[P_E1], jump = p[P_E2], i, k, n;
#if EDITION_BOTANICA
    drops += song.g[G_DROPS];
    jump += song.g[G_DROPS] >> 1;
#endif
    n = clamp(drops, 0, 127) * 4 / 128;                  /* 0..3 */
    if (!n || (uint32_t)(t - trk) >= NPART)
        return;
    for (i = 1; i <= n; i++) {
        uint32_t r = noise32(&drop_rng), nt = note, v = vel;
        int32_t fall = 127 - p[P_E7] / 2;                /* each echo: FALL 0 = -0.07 dB .. 127 = -6 dB */
        for (k = 0; k < i; k++)
            v = v * (uint32_t)fall / 128u;
        if ((int32_t)(r & 127u) < clamp(jump, 0, 127)) {
            static const uint8_t JMP[4] = {12, 19, 24, 12};
            nt += JMP[(r >> 8) & 3u];
        }
        if (nt > 127u || v < 8u)
            continue;
        for (k = 0; k < (int32_t)DROPQ && dropq[k].on; k++)
            ;
        if (k == (int32_t)DROPQ)
            return;                                      /* (full) */
        dropq[k].on = 1;
        dropq[k].trk = (uint8_t)(t - trk);
        dropq[k].note = (uint8_t)nt;
        dropq[k].vel = (uint8_t)v;
        dropq[k].fine = (int16_t)(p[P_E6] ? ((int32_t)((r >> 12) & 255u) - 128) * p[P_E6] * 41 / 128 : 0);   /* +-SPRD ct */
        dropq[k].left = drop_spacing((uint32_t)p[P_E5]) * (uint32_t)i;
    }
}

/* each audio block (seq.c, with the strums): the echoes due start, through the voice budget, and end 50 ms later
 * (the partials ring on: ownenv; a note-off in the same block would end a voice that has not sounded yet) */
static void drop_block(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < DROPQ; i++) {
        track_t *t;
        if (!dropq[i].on)
            continue;
        if (dropq[i].left > n) {
            dropq[i].left -= n;
            continue;
        }
        t = &trk[dropq[i].trk % NPART];
        if (!dropq[i].vel) {                             /* its note-off */
            dropq[i].on = 0;
            trk_note_off(t, dropq[i].note);
            continue;
        }
        if (t->engine != ENGI_DROPLET || t->eng_req != t->engine) {
            dropq[i].on = 0;                             /* the track plays something else now */
            continue;
        }
        if (voices_busy() >= NVOICE) {
            dropq[i].on = 0;
            drop_dropped++;                              /* never steal for an echo */
            continue;
        }
        drop_echoing = 1;
        drop_fine_next = dropq[i].fine;
        trk_note_on(t, dropq[i].note, dropq[i].vel);
        drop_echoing = 0;
        drop_fine_next = 0;
        drop_echoes++;
        dropq[i].vel = 0;                                /* the slot waits for its note-off */
        dropq[i].left = FS / 20u;
    }
}
static void drop_cancel(const track_t *t)                /* a track's all-off (STOP): its echoes too */
{
    uint32_t i;
    for (i = 0; i < DROPQ; i++)
        if (dropq[i].trk == (uint8_t)(t - trk))
            dropq[i].on = 0;
}

static void droplet_note_on(track_t *t, voice_t *v)
{
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[0] = DROP_ONE;
    v->s[1] = (int32_t)(t->p[P_E4] * 132104);            /* the upper partials / FM index: BRIT (Q24 at 127) */
    v->s[2] = v->s[1];
    v->s[3] = (uint32_t)t->p[P_E0] >= DROP_CHIRP ? 32767 : 0;
    v->s[5] = DROP_ONE;
    if (!v->s[4])
        v->s[4] = 0x13579BDF + (int32_t)v->age;
    v->s[6] = drop_echoing ? drop_fine_next : 0;
    if (!drop_echoing)
        drop_schedule(t, v->note, v->vel);
}

static uint32_t drop_fac(int32_t q)                      /* a block's decay factor (Q16), tau 2^(q/16) x 1500 samples */
{
    uint32_t tau = 1500u << (clamp(q, 0, 200) >> 4), d;
    tau += (tau >> 4) * (uint32_t)(clamp(q, 0, 200) & 15);
    d = (65536u * CTL) / tau;
    return d >= 65535u ? 0u : 65535u - d;
}
static int32_t drop_step(int32_t env, uint32_t fac)
{
    uint32_t e = (uint32_t)env >> 8;
    return (int32_t)((e > 65535u ? 65535u : e) * fac >> 8);
}
static uint32_t drop_mul(uint32_t inc, uint32_t r)       /* inc x r (Q10); 0 past Nyquist (and on overflow) */
{
    uint32_t w = r >> 10, hi = w ? inc * w : 0u, lo = (inc >> 10) * (r & 1023u);
    if (w && hi / w != inc)
        return 0;
    hi += lo;
    return hi < lo || hi >= 0x80000000u ? 0u : hi;
}

static void droplet_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t model = (uint32_t)p[P_E0] & 7u, i;
    int32_t semis = (m->pitch16 >> 4) - 60, q = p[P_E3] + 8 - semis;   /* DECAY 0..127: tau 47 ms .. 4.5 s at C4 */
    int32_t vel = v->vel * 258, brit = p[P_E4] * 258, nst = v->s[4];
    uint32_t inc = m->inc;
    int32_t qmax = v->gate ? DROP_Q_MAX : DROP_Q_RING;  /* (the key scaling: low notes ring longer) */
    int32_t e0a = v->s[0], e1a = v->s[1], e2a = v->s[2], cka = v->s[5], cha = v->s[3];
    int32_t e0b, e1b, e2b, ckb = drop_step(cka, drop_fac(4)), chb;
    uint32_t p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], i1, i2;
    if (v->s[6])                                         /* an echo's detune (SPRD) */
        inc += (uint32_t)((int32_t)(inc >> 12) * v->s[6] / 16);
    if (q > qmax)
        q = qmax;
    e0b = drop_step(e0a, drop_fac(q));
    if (model < DROP_CHIRP) {
        e1b = drop_step(e1a, drop_fac(q + DROP_MODES[model].d1));
        e2b = drop_step(e2a, drop_fac(q + DROP_MODES[model].d2));
        i1 = drop_mul(inc, DROP_MODES[model].r1);
        i2 = drop_mul(inc, DROP_MODES[model].r2);
        chb = 0;
    } else {
        e1b = drop_step(e1a, drop_fac(q - 24));          /* the FM index falls faster than the note */
        e2b = 0;
        i1 = drop_mul(inc, (uint32_t)DROP_FM_RATIO[model - DROP_CHIRP] << 10);
        i2 = 0;
        chb = cha - ((cha * 2400) >> 15);                /* the chirp: ~20 ms */
    }
    for (i = 0; i < n; i++) {
        int32_t e0 = e0a + (((e0b - e0a) >> CTL_LOG2) * (int32_t)i);
        int32_t e1 = e1a + (((e1b - e1a) >> CTL_LOG2) * (int32_t)i);
        int32_t y;
        if (model < DROP_CHIRP) {
            int32_t e2 = e2a + (((e2b - e2a) >> CTL_LOG2) * (int32_t)i);
            y = mulq15(sine_i(p0), e0 >> 9);
            if (i1)
                y += mulq15(mulq15(sine_i(p1), DROP_MODES[model].a1), e1 >> 9);
            if (i2)
                y += mulq15(mulq15(sine_i(p2), DROP_MODES[model].a2), e2 >> 9);
            p0 += inc;
            p1 += i1;
            p2 += i2;
        } else {
            int32_t ch = cha + (((chb - cha) * (int32_t)i) >> CTL_LOG2);
            uint32_t ic = inc + (uint32_t)(((int32_t)(inc >> 15)) * ch);   /* up to an octave up, falling */
            int32_t mo = sine_i(p1);
            y = mulq15(sine_i(p0 + ((uint32_t)(mo * ((e1 >> 11) + 1024)) << 1)), e0 >> 9);
            p0 += ic;
            p1 += i1 ? i1 + (uint32_t)(((int32_t)(i1 >> 15)) * ch) : 0u;
        }
        if (cka > 4096) {                                /* the mallet's click */
            int32_t ck = cka + (((ckb - cka) >> CTL_LOG2) * (int32_t)i);
            y += mulq15(mulq15((int32_t)(noise32(&nst) >> 17) - 16384, brit >> 3), ck >> 9);
        }
        y = mulq15(y, vel);
        out[i] += mulq15(mulq15(y, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = p0;
    v->ph[1] = p1;
    v->ph[2] = p2;
    v->s[0] = e0b;
    v->s[1] = e1b;
    v->s[2] = e2b;
    v->s[3] = chb;
    v->s[4] = nst;
    v->s[5] = ckb;
}

static int droplet_done(track_t *t, voice_t *v)
{
    (void)t;
    return v->s[0] < 16384 && v->s[1] < 16384 && v->s[2] < 16384;   /* all below -60 dB */
}

/* {MODEL, DROPS, JUMP, DECAY, BRIT, TIME, SPRD, FALL}, {A D S R: unused, the partials are the envelope}, fenv, mono,
 * sends */
static const preset_t DROPLET_PRESETS[] = {
    {"KALIMBA", {0, 40, 20, 58, 60, 0, 6, 60}, {0, 0, 127, 40}, 0, 0, FX(0, 10, 30, 44)},
    {"MARIMBA", {1, 0, 0, 52, 56, 0, 0, 60}, {0, 0, 127, 40}, 0, 0, FX(0, 6, 14, 30)},
    {"MUSIC BOX", {2, 34, 34, 70, 70, 1, 8, 54}, {0, 0, 127, 40}, 0, 0, FX(0, 14, 26, 52)},
    {"GLASS DROPS", {3, 72, 64, 80, 64, 1, 12, 48}, {0, 0, 127, 40}, 0, 0, FX(0, 24, 30, 62)},
    {"HARP", {4, 0, 0, 74, 50, 0, 4, 60}, {0, 0, 127, 40}, 0, 0, FX(0, 16, 18, 50)},
    {"RAIN ARP", {0, 100, 80, 46, 44, 4, 20, 40}, {0, 0, 127, 40}, 0, 0, FX(0, 20, 34, 56)},
    {"DEW CHIRP", {5, 60, 50, 40, 90, 3, 10, 50}, {0, 0, 127, 40}, 0, 0, FX(0, 10, 30, 48)},
    {"BUBBLE FM", {6, 80, 40, 34, 110, 1, 14, 44}, {0, 0, 127, 40}, 0, 0, FX(0, 12, 34, 46)},
    {"SPARKLE", {7, 90, 96, 30, 100, 4, 24, 36}, {0, 0, 127, 40}, 0, 0, FX(0, 20, 38, 60)},
};

static const engine_t ENG_DROPLET = {
    .name = "DROPLET",
    .page_title = {"MODAL", "DROPS"},
    .edit = {
        {"MODEL", F_ENUM, 0, 7, 0, N_DROP_MODEL, 0},
        {"DROPS", F_PCT, 0, 127, 40, 0, 0},
        {"JUMP", F_PCT, 0, 127, 20, 0, 0},
        {"DECAY", F_PCT, 0, 127, 60, 0, 0},
        {"BRIT", F_PCT, 0, 127, 60, 0, 0},
        {"TIME", F_ENUM, 0, 4, 0, N_DROP_TIME, 0},
        {"SPRD", F_INT, 0, 50, 6, 0, "ct"},
        {"FALL", F_PCT, 0, 127, 60, 0, 0},
    },
    .presets = DROPLET_PRESETS,
    .npresets = NELEM(DROPLET_PRESETS),
    .fil_page = -1,
    .note_on = droplet_note_on,
    .render = droplet_render,
    .color = 0x5E7F,                                     /* sky blue */
    .macro = {P_E1, P_E2, P_E6, P_E3},
    .poly = 8,
    .ownenv = 1,
    .done = droplet_done,
};
