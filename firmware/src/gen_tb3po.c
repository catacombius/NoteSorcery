/* SPDX-License-Identifier: GPL-3.0-only */
/* GEN (TOOLS page, NoteSorcery): a generative 303 line on the selected synth track, after X0X's TB-3PO
 * (seq/tb3po.c, Charles Vestal), itself a port of schwung-tb3po and of the Phazerville Hemisphere Suite TB_3PO
 * applet (djphazer and contributors), GPL-3.0. The same draws in the same order (xorshift32; density, the root on
 * a beat, the degree, the octave, accent, slide; a slide into a rest dropped), in integer maths: it runs in the
 * main loop, where no float may run (x0x/README.md). Instead of TB-3PO's six scales it walks the track's own
 * key (SEL page: ROOT, SCL), two octaves up from C2 + ROOT (an ACID or a bass) or C3 + ROOT (anything else). The
 * line fills the track's LEN (up to 64 steps); ACCENT steps play at 127, SLIDE steps slide (seq.c). Included by
 * seq.c (scale_mask, steps_clear). */
#define TB3_DENSITY 70u                         /* % of the steps that play (schwung-tb3po: 0.7 / 0.4 / 0.25) */
#define TB3_ACCENT 40u
#define TB3_SLIDE 25u
#define TB3_OCTAVES 2u

static uint32_t tb3_rng(uint32_t *r)            /* xorshift32, as schwung-tb3po */
{
    uint32_t x = *r ? *r : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *r = x;
    return x;
}
/* a draw in [0, 1) as 24 bits (TB-3PO's rng_f), compared with pct / 100 */
static uint32_t tb3_draw(uint32_t *r) { return tb3_rng(r) & 0xFFFFFFu; }
static uint32_t tb3_below(uint32_t d, uint32_t pct) { return (uint64_t)d * 100u < (uint64_t)pct * 0x1000000u; }
static uint32_t tb3_atmost(uint32_t d, uint32_t pct) { return (uint64_t)d * 100u <= (uint64_t)pct * 0x1000000u; }
static uint32_t tb3_pick(uint32_t *r, uint32_t n)   /* (int)(rng_f * n), at most n - 1 */
{
    uint32_t d = (uint32_t)(((uint64_t)tb3_draw(r) * n) >> 24);
    return d >= n ? n - 1u : d;
}

/* the line into track t (a synth track): its steps 0..LEN-1, the rest of the pattern cleared; seed: any
 * entropy. Call with the audio IRQ off (the sequencer reads the steps) */
static void tb3po_fill(track_t *t, uint32_t seed)
{
    uint8_t deg[12], nd = 0, note[NSTEP], fl[NSTEP];
    uint32_t r = seed * 2654435761u + 0x9E3779B9u, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), i, any = 0;
    uint32_t mask = scale_mask(t), root = (uint32_t)t->p[P_ROOT] % 12u;
    uint32_t base = (ENGINES[t->eng_req % NENGINES] == &ENG_ACID || t->p[P_VOICE] != V_POLY ? 36u : 48u) + root;
    tb3_rng(&r);
    tb3_rng(&r);
    for (i = 0; i < 12u; i++)                       /* the key's degrees, from the root */
        if ((mask >> i) & 1u)
            deg[nd++] = (uint8_t)i;
    if (!nd)
        deg[nd++] = 0;
    for (i = 0; i < NSTEP; i++) {
        note[i] = (uint8_t)base;
        fl[i] = 0xFF;                               /* a rest */
    }
    for (i = 0; i < len; i++) {
        uint32_t d, o, f;
        if (!tb3_atmost(tb3_draw(&r), TB3_DENSITY))   /* rng_f > density: a rest */
            continue;
        if (i % 4u == 0u && tb3_below(tb3_draw(&r), 35u))
            d = 0;                                  /* on the beat: the root, often */
        else
            d = tb3_pick(&r, nd);
        o = tb3_pick(&r, TB3_OCTAVES);
        note[i] = (uint8_t)clamp((int32_t)(base + deg[d] + 12u * o), 0, 127);
        f = 0;
        if (tb3_below(tb3_draw(&r), TB3_ACCENT))
            f = SF_ACCENT;
        if (tb3_below(tb3_draw(&r), TB3_SLIDE))
            f = SF_SLIDE;                           /* (in TB-3PO a SLIDE step is not also an ACCENT) */
        fl[i] = (uint8_t)f;
    }
    for (i = 0; i < len; i++) {                     /* a slide into a rest does nothing */
        if (fl[i] == SF_SLIDE && fl[(i + 1u) % len] == 0xFFu)
            fl[i] = 0;
        any |= fl[i] != 0xFFu;
    }
    if (!any) {
        note[0] = (uint8_t)base;
        fl[0] = 0;
    }
    steps_clear(t);                                 /* (no lock, nudge or condition of the old pattern) */
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        if (fl[i] == 0xFFu)
            continue;                               /* (steps_clear left a REST) */
        s->note[0] = note[i];
        s->n = 1;
        s->time = ST_NOTE;
        s->flags = fl[i];
        s->vel = fl[i] & SF_ACCENT ? 127u : 96u;
        s->lvl = 0;
        s->rat = 0;
    }
}
