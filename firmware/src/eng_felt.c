/* SPDX-License-Identifier: GPL-3.0-only */
/* FELT (BOTANICA): a felt piano with a string bloom under it, in one voice. All fixed point.
 *
 *   body     two phase-modulated sine pairs (4 operators): carrier 1 <- modulator 1 at RATIO, carrier 2 <- modulator 2,
 *            carrier 2 detuned by DTUN cents (the slight beating of a piano's unison strings). The modulation index
 *            (TONE, the velocity) falls with the body's own envelope: bright on the strike, round as it rings
 *   hammer   the felt knock: a burst of noise a few ms long, louder with the velocity (HAMR)
 *   damping  a one-pole low-pass on the whole voice that closes with the key (high notes darker, as felt dampers)
 *            and opens with TONE and with the song's BLOOM (G_BLOOM)
 *   strings  two detuned triangles (soft, no filter needed) that swell in while the key is held (ATK, 0.2..3 s)
 *            and fade with REL after it: BLM sets how much of them, the song's BLOOM adds to it
 *   warmth   WARM: a gentle saturation (softclip) on the sum, compressing the strike a little
 * The engine's envelopes are the voice's amplitude (ownenv): DCAY sets the body's decay (shorter for higher notes),
 * the key's release damps it; done() ends the voice when everything is quiet.
 *
 * Voice state: ph[0] carrier 1, ph[1] modulator 1, ph[2] carrier 2; s[0] modulator 2, s[1] s[2] the triangles
 * (phases), s[3] the body envelope, s[4] the strings' envelope, s[5] the hammer envelope (Q24 each), s[6] the noise
 * state, s[7] the damping filter. */
static const char *const N_FELT_RATIO[] = {"PNO", "EPNO", "BOX", "HOLW"};   /* modulator ratio: 1, 14, 3, 2 */
static const uint8_t FELT_RATIO[4] = {1, 14, 3, 2};
#define FELT_ONE (1 << 24)

static void felt_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[0] = 0;
    v->s[1] = 0x15555555;                               /* the triangles a third of a turn apart */
    v->s[2] = 0x6AAAAAAA;
    v->s[3] = FELT_ONE;                                 /* the body: struck */
    v->s[5] = FELT_ONE;                                 /* the knock */
    if (!v->s[6])
        v->s[6] = 0x2545F491 + (int32_t)v->age;
    /* s[4] (strings) and s[7] (filter) go on from where they were: a repeated note blooms on */
}

/* a time constant in samples, q in 1/16 octaves: 0 = 34 ms, 16 a doubling (q 200: 9 min) */
static uint32_t felt_tau(int32_t q)
{
    uint32_t tau;
    q = clamp(q, 0, 200);
    tau = 1500u << (q >> 4);
    return tau + ((tau >> 4) * (uint32_t)(q & 15));
}
static uint32_t felt_fac(uint32_t tau)
{
    uint32_t d = (65536u * CTL) / (tau ? tau : 1u);
    return d >= 65535u ? 0u : 65535u - d;
}
static int32_t felt_step(int32_t env, uint32_t fac)    /* env (Q24) after a block of decay */
{
    uint32_t e = (uint32_t)env >> 8;
    return (int32_t)((e > 65535u ? 65535u : e) * fac >> 8);
}

static void felt_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t bloom = 0, semis = (m->pitch16 >> 4) - 60, i;
#if EDITION_BOTANICA
    bloom = song.g[G_BLOOM];                            /* PERFORM > BLOOM: warmer, more strings */
#endif
    int32_t tone = clamp(p[P_E0] + (m->cutoff >> 8) + (bloom >> 2), 0, 127);
    int32_t hamr = p[P_E1], blm = clamp(p[P_E2] + (bloom >> 1), 0, 127), warm = p[P_E6];
    uint32_t ratio = FELT_RATIO[p[P_E7] & 3], inc = m->inc;
    uint32_t inc_m = inc * ratio, inc2, incs1, incs2;
    int32_t vel = v->vel * 258;                          /* Q15 */
    /* body envelope: decays (DCAY, key-scaled); released: the damper, 8x faster */
    uint32_t fb = felt_fac(felt_tau(p[P_E3] + 40 - semis) / (v->gate ? 1u : 8u));   /* 0.14 .. 35 s at C4 */
    uint32_t fh = felt_fac(180u + (uint32_t)(127 - hamr) * 2u);   /* the knock: 4..10 ms */
    int32_t eb0 = v->s[3], eb1 = felt_step(eb0, fb), eh0 = v->s[5], eh1 = felt_step(eh0, fh);
    int32_t es0 = v->s[4], es1;
    /* the strings: swell while held (ATK of the string bloom, E5), fade after (the track's REL) */
    if (v->gate && blm) {
        int32_t up = FELT_ONE / (int32_t)((8820u + (uint32_t)p[P_E5] * 1000u) / CTL);   /* 0.2 .. 3.1 s */
        es1 = es0 + up > FELT_ONE ? FELT_ONE : es0 + up;
    } else {
        es1 = felt_step(es0, felt_fac(felt_tau(p[P_REL] / 2)));
    }
    /* the modulation index: TONE x velocity, falling with the body (bright strike, round tail); Q16 of pi */
    int32_t idx0 = (tone * (v->vel + 30)) * ((eb0 >> 9) + 8192) >> 15;   /* up to ~25000: 2.4 rad */
    int32_t idx1 = (tone * (v->vel + 30)) * ((eb1 >> 9) + 8192) >> 15;
    /* damping: a one-pole; brighter with TONE, darker up the keyboard */
    int32_t a = clamp(3000 + tone * 180 - semis * 90, 900, 30000);
    int32_t det = p[P_E4];                               /* cents, 0..20 */
    inc2 = inc + (uint32_t)((int32_t)(inc >> 12) * (det * 2367 / 1000));
    incs1 = inc - (uint32_t)((inc >> 12) * 17u);         /* the strings: -7 / +7 cents */
    incs2 = inc + (uint32_t)((inc >> 12) * 17u);
    if (ratio > 1u && inc_m / ratio != inc)              /* (the modulator past the top: none) */
        inc_m = 0;
    {
        uint32_t c1 = v->ph[0], m1 = v->ph[1], c2 = v->ph[2], m2 = (uint32_t)v->s[0];
        uint32_t s1 = (uint32_t)v->s[1], s2 = (uint32_t)v->s[2];
        int32_t nst = v->s[6], lp = v->s[7];
        int32_t sg = blm * 200;                          /* the strings' level, Q15 (up to ~0.8) */
        int32_t hg = hamr * 150;                         /* the knock's level */
        int32_t wd = warm * 200;                         /* WARM: dry -> saturated */
        for (i = 0; i < (int32_t)n; i++) {
            int32_t eb = eb0 + (((eb1 - eb0) >> CTL_LOG2) * i), eh = eh0 + (((eh1 - eh0) >> CTL_LOG2) * i);
            int32_t es = es0 + (((es1 - es0) >> CTL_LOG2) * i), idx = idx0 + (((idx1 - idx0) * i) >> CTL_LOG2);
            int32_t mo1 = sine_i(m1), mo2 = sine_i(m2), body, y, s;
            body = sine_i(c1 + ((uint32_t)(mo1 * idx) << 1)) + sine_i(c2 + ((uint32_t)(mo2 * (idx >> 1)) << 1));
            y = mulq15(body >> 1, eb >> 9);
            s = mulq15(osc_tri(s1) + osc_tri(s2), sg) >> 1;
            y += mulq15(s, es >> 9);
            if (eh > 4096)
                y += mulq15(mulq15((int32_t)(noise32(&nst) >> 17) - 16384, hg), eh >> 9);
            y = mulq15(y, vel);
            if (wd)
                y += mulq15(softclip(y + (y >> 1)) - y, wd);
            lp += mulq15(y - lp, a);
            out[i] += mulq15(mulq15(lp, amp_at(m, (uint32_t)i)), VOICE_FS) << 1;
            c1 += inc;
            m1 += inc_m;
            c2 += inc2;
            m2 += inc_m;
            s1 += incs1;
            s2 += incs2;
        }
        v->ph[0] = c1;
        v->ph[1] = m1;
        v->ph[2] = c2;
        v->s[0] = (int32_t)m2;
        v->s[1] = (int32_t)s1;
        v->s[2] = (int32_t)s2;
        v->s[6] = nst;
        v->s[7] = lp;
    }
    v->s[3] = eb1;
    v->s[4] = es1;
    v->s[5] = eh1;
}

/* done: the body and the strings are quiet (the strings count only once released: held, they swell) */
static int felt_done(track_t *t, voice_t *v)
{
    return v->s[3] < 2048 && (!v->gate || !t->p[P_E2]) && v->s[4] < 2048 && v->s[5] < 4096;
}

/* {TONE, HAMR, BLM, DCAY, DTUN, ATK (strings), WARM, RATIO}, {A D S R: only REL counts: the strings' fade}, fenv,
 * mono, sends */
static const preset_t FELT_PRESETS[] = {
    {"FELT UPRIGHT", {44, 60, 0, 70, 6, 60, 30, 0}, {0, 0, 127, 50}, 0, 0, FX(0, 8, 12, 40)},
    {"NURTURE KEYS", {56, 40, 34, 78, 9, 70, 24, 0}, {0, 0, 127, 64}, 0, 0, FX(0, 20, 22, 56)},
    {"PNO+STRINGS", {40, 48, 90, 80, 7, 64, 20, 0}, {0, 0, 127, 80}, 0, 0, FX(0, 24, 16, 64)},
    {"BOX PIANO", {70, 20, 0, 56, 3, 60, 10, 2}, {0, 0, 127, 40}, 0, 0, FX(0, 10, 30, 52)},
    {"TINE DREAM", {62, 30, 20, 74, 8, 70, 30, 1}, {0, 0, 127, 50}, 0, 0, FX(0, 36, 26, 48)},
    {"SOFT PEDAL", {28, 24, 18, 90, 10, 80, 40, 0}, {0, 0, 127, 70}, 0, 0, FX(0, 16, 10, 60)},
    {"HOLLOW FELT", {50, 52, 10, 64, 5, 60, 36, 3}, {0, 0, 127, 46}, 0, 0, FX(0, 12, 20, 44)},
    {"STRING BLOOM", {20, 0, 127, 40, 12, 90, 16, 0}, {0, 0, 127, 96}, 0, 0, FX(0, 40, 14, 72)},
};

static const engine_t ENG_FELT = {
    .name = "FELT",
    .page_title = {"PIANO", "BLOOM"},
    .edit = {
        {"TONE", F_PCT, 0, 127, 44, 0, 0},
        {"HAMR", F_PCT, 0, 127, 60, 0, 0},
        {"BLM", F_PCT, 0, 127, 0, 0, 0},
        {"DCAY", F_PCT, 0, 127, 70, 0, 0},
        {"DTUN", F_INT, 0, 20, 6, 0, "ct"},
        {"ATK", F_PCT, 0, 127, 60, 0, 0},
        {"WARM", F_PCT, 0, 127, 30, 0, 0},
        {"TYPE", F_ENUM, 0, 3, 0, N_FELT_RATIO, 0},
    },
    .presets = FELT_PRESETS,
    .npresets = NELEM(FELT_PRESETS),
    .fil_page = -1,
    .note_on = felt_note_on,
    .render = felt_render,
    .color = 0xFD9B,                                     /* petal pink */
    .macro = {P_E0, P_E1, P_E2, P_E3},
    .poly = 6,
    .ownenv = 1,
    .done = felt_done,
};
