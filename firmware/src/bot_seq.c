/* SPDX-License-Identifier: GPL-3.0-only */
/* BOTANICA's sequencer side (seq.c includes it; bot_block runs every audio block from events_block):
 *
 *   DRIFT (SKY knob 3, G_DRIFT, G_RAMP): a tempo to drift to. Turned, the tempo moves there from the next bar,
 *     straight over RAMP bars (0: it jumps on the next bar): an ambient 60 that slides up to 160, or the half-time
 *     drop 80 <-> 160. While playing on the FM-1's own clock (a MIDI clock leads otherwise)
 *   BREAK / DROP (GLO + C#4): held, every track but the selected one falls silent and the reverb freezes (bot_fx.c):
 *     the ambient hold, the grid still counting; let go, the drop: everything back on the next bar, at the DRIFT
 *     tempo if one is set
 *   GLITCH (PERFORM knob 4, G_GLITCH; GLO + F#4 held: all the way): on the sixteenths, now and then, a punch-in
 *     effect for a step or two (STUTTER, LOOP 32 / 16, REVERSE, STOP, GATE: punch.c), never over the one your
 *     finger holds; and the synth steps take a ratchet now and then (bot_step)
 *   FOLEY (PERFORM knob 3, G_FOLEY): hits on every track that plays NOISE's FOLEY mode, scattered on the
 *     sixteenths (each a burst cluster of its kind at a random pitch and level), as many as the knob asks for
 *   RATCH (FEEL page, P_RATMODE): how a step's ratchets move: NORM, RAMP (up to an octave), BOUNCE (1/2, 3/4, 7/8
 *     of the step, each softer: a drop bouncing), RAND (an octave up or another note of the step, at random)
 * The droplets' echoes (eng_droplet.c drop_block) and BOTANICA's master LFOs (bot_fx.c bot_fx_block) run here too. */
static int mclk_on(void);                                /* seq.c */
static struct {
    uint32_t s16;                        /* the sixteenth the last block was in */
    uint32_t free_u;                     /* (stopped: a free-running clock for the sixteenths, units) */
    int8_t gl_fx;                        /* the punch-in effect GLITCH started, -1 = none */
    uint8_t gl_left;                     /* sixteenths it has left */
    uint8_t foley_note[NPART];           /* the foley hit sounding on a track (0 = none) */
    int16_t dr_from, dr_to;              /* DRIFT: the ramp's ends (BPM) */
    int16_t dr_want;                     /* the target it is heading to (0 = none) */
    uint32_t dr_b0, dr_len;              /* where it started and how long (beats x 256), 0 = no ramp */
    uint8_t dr_wait;                     /* a new target: the ramp starts on the next bar */
    uint32_t bar;                        /* the last bar seen */
    int32_t rng;
    volatile uint8_t glitch_hold;        /* GLO + F#4 held (the UI) */
} bseq = {0xFFFFFFFFu, 0, -1, 0, {0}, 0, 0, 0, 0, 0, 0, 0xFFFFFFFFu, 0x3C6EF372, 0};
static step_t bot_sx[NPART];             /* the step playing as GLITCH ratcheted it */
static uint8_t bot_sx_on[NPART];

static int32_t bot_glitch(void) { return bseq.glitch_hold ? 127 : song.g[G_GLITCH]; }

/* a synth step about to play: with GLITCH up, now and then a ratchet (x2..x4) on a step that has none */
static const step_t *bot_step(track_t *t, const step_t *s)
{
    uint32_t p = trk_index(t) % NPART, r, i, rat = 0;
    int32_t g = bot_glitch();
    bot_sx_on[p] = 0;
    if (!g || s->time != ST_NOTE || !s->n || s->rat)
        return s;
    r = noise32(&bseq.rng);
    if ((int32_t)(r & 255u) >= g)                        /* up to half the steps at 127 */
        return s;
    for (i = 0; i < s->n; i++)
        rat |= (1u + ((r >> 8) % 3u)) << (2u * i);
    bot_sx[p] = *s;
    bot_sx[p].rat = (uint8_t)rat;
    bot_sx_on[p] = 1;
    return &bot_sx[p];
}

/* ratchet hit h of hits of note i (seq.c seq_ratchets): what it lets go, what it plays, how loud, its length */
static uint8_t rat_last[NPART][4];                       /* the note each of a step's notes' last hit played */
static void bot_ratchet(track_t *t, const step_t *s, uint32_t i, uint32_t hits, uint32_t h, uint32_t *off,
                        uint32_t *on, uint32_t *vel, uint32_t *g, uint32_t slen)
{
    uint32_t n = s->note[i], p = trk_index(t) % NPART;
    uint32_t last = t->rat_done[i] && rat_last[p][i & 3u] ? rat_last[p][i & 3u] : n;
    switch (t->p[P_RATMODE]) {
    case 1:                                              /* RAMP: up to an octave over the step */
        *off = last;
        *on = n + (12u * h + (hits - 1u) / 2u) / (hits - 1u);
        break;
    case 2:                                              /* BOUNCE: each hit half as far, softer */
        *g = slen >> (h + 1u);
        *vel = *vel * (hits - h) / hits;
        break;
    case 3: {                                            /* RAND: an octave up, or another note of the step */
        uint32_t r = noise32(&bseq.rng);
        *off = last;
        *on = (r & 1u) && n + 12u < 128u ? n + 12u : s->note[(r >> 1) % s->n];
        break;
    }
    default:
        break;
    }
    if (*on > 127u)
        *on = 127u;
    if (*vel < 1u)
        *vel = 1u;
    rat_last[p][i & 3u] = (uint8_t)*on;
}

/* the position on the transport, beats x 256 */
static uint32_t bot_beat_q8(void) { return clk_beat * 256u + clk_pos / (BEAT_U >> 8); }

static void bot_drift(uint32_t newbar)
{
    int32_t want = song.g[G_DRIFT];
    if (!song.playing || mclk_on()) {
        bseq.dr_len = 0;
        bseq.dr_wait = 0;
        bseq.dr_want = 0;
        return;
    }
    if (want && want != bseq.dr_want) {                  /* a new target: from the next bar */
        bseq.dr_want = (int16_t)want;
        bseq.dr_wait = 1;
    }
    if (!want)
        bseq.dr_want = 0;
    if (bseq.dr_wait && newbar) {
        bseq.dr_wait = 0;
        bseq.dr_from = song.g[G_BPM];
        bseq.dr_to = (int16_t)clamp(bseq.dr_want, GP[G_BPM].min, GP[G_BPM].max);
        bseq.dr_b0 = bot_beat_q8();
        bseq.dr_len = (uint32_t)song.g[G_RAMP] * 4u * 256u;
        if (!bseq.dr_len)
            song.g[G_BPM] = bseq.dr_to;                  /* RAMP 0: at once */
    }
    if (bseq.dr_len) {
        uint32_t e = bot_beat_q8() - bseq.dr_b0;
        if (e >= bseq.dr_len) {
            song.g[G_BPM] = bseq.dr_to;
            bseq.dr_len = 0;
        } else {
            song.g[G_BPM] = (int16_t)(bseq.dr_from + (bseq.dr_to - bseq.dr_from) * (int32_t)e / (int32_t)bseq.dr_len);
        }
    }
}

/* GLITCH on a new sixteenth: maybe a punch-in effect for a step or two (never over the one your finger holds) */
static void bot_glitch_tick(void)
{
    static const int8_t FX[6] = {PX_STUT, PX_LOOP32, PX_LOOP16, PX_REV, PX_STOP, PX_GATE};
    int32_t g = bot_glitch();
    uint32_t r;
    if (bseq.gl_fx >= 0 && (!bseq.gl_left || !--bseq.gl_left || !g)) {
        if (punch.req == bseq.gl_fx && !punch.keybit)
            punch.req = -1;
        bseq.gl_fx = -1;
        bseq.gl_left = 0;
    }
    if (!g || bseq.gl_fx >= 0 || punch.req >= 0 || punch.keybit || punch.hold)
        return;
    r = noise32(&bseq.rng);
    if ((int32_t)(r & 511u) >= g)                        /* up to a quarter of the sixteenths */
        return;
    bseq.gl_fx = FX[(r >> 9) % 6u];
    bseq.gl_left = (uint8_t)(1u + ((r >> 16) & 1u));
    punch.req = bseq.gl_fx;
}

/* FOLEY on a new sixteenth: the hits on the tracks that play NOISE's FOLEY mode */
static void bot_foley_tick(void)
{
    int32_t f = song.g[G_FOLEY];
    uint32_t p;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t r;
        if (bseq.foley_note[p]) {                        /* last sixteenth's hit: let go */
            trk_note_off(t, bseq.foley_note[p]);
            bseq.foley_note[p] = 0;
        }
        if (!f || t->engine != ENGI_NOISE || t->eng_req != t->engine || t->p[P_E0] != NZ_FOLEY || trk_silent(t))
            continue;
        r = noise32(&bseq.rng);
        if ((int32_t)(r & 255u) >= f)                    /* up to half the sixteenths at 127 */
            continue;
        bseq.foley_note[p] = (uint8_t)(48u + ((r >> 8) % 36u));
        trk_note_on(t, bseq.foley_note[p], 50u + ((r >> 16) % 70u));
    }
}

static void bot_block(uint32_t n)
{
    uint32_t s16, bar, newbar;
    drop_block(n);                                       /* the droplets' echoes due (eng_droplet.c) */
    bot_fx_block();                                      /* WEATHER's wow, BREEZE's walks (bot_fx.c) */
    if (song.playing) {
        s16 = clk_beat * 4u + clk_pos / (BEAT_U / 4u);
        bar = clk_beat >> 2;
    } else {                                             /* stopped: the sixteenths of a free clock */
        bseq.free_u += n * (uint32_t)song.g[G_BPM];
        s16 = bseq.free_u / (BEAT_U / 4u);
        bar = s16 >> 4;
        if (bot_brk == 2u)
            bot_brk = 0;                                 /* (stopped: the drop at once) */
    }
    newbar = bar != bseq.bar;
    bseq.bar = bar;
    if (bot_brk == 2u && newbar) {                       /* the drop: everything back, at the DRIFT tempo */
        bot_brk = 0;
        if (song.g[G_DRIFT] && song.playing && !mclk_on()) {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_DRIFT], GP[G_BPM].min, GP[G_BPM].max);
            bseq.dr_want = song.g[G_DRIFT];
            bseq.dr_wait = 0;
            bseq.dr_len = 0;
        }
    }
    bot_drift(newbar);
    if (s16 != bseq.s16) {
        bseq.s16 = s16;
        bot_glitch_tick();
        bot_foley_tick();
    }
}
/* STOP (seq.c seq_stop): no glitch left on, no foley hit held, no break */
static void bot_stop(void)
{
    uint32_t p;
    if (bseq.gl_fx >= 0 && punch.req == bseq.gl_fx && !punch.keybit)
        punch.req = -1;
    bseq.gl_fx = -1;
    bseq.gl_left = 0;
    for (p = 0; p < NPART; p++)
        if (bseq.foley_note[p]) {
            trk_note_off(&trk[p], bseq.foley_note[p]);
            bseq.foley_note[p] = 0;
        }
    bot_brk = 0;
    bseq.dr_len = 0;
}
