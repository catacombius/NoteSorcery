/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID (firmware/src/eng_acid.c: X0X's Open303 on a synth track) on the host, through hostsim.c: every preset
 * bounded, finite and audible; an accented note louder than a plain one; a slide keeps the 303's gate (no new
 * attack) and glides; a released note ends its voice; two ACID tracks at once, each its own 303; the arena
 * taken over from another engine starts clean; the cost of one ACID track. Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static int bad;
static void check(int ok, const char *what)
{
    printf("acid: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t blk_peak(void)                   /* one block of the mix: its largest |sample| */
{
    int32_t o[2 * CTL], pk = 0;
    uint32_t i;
    mix_block(o, CTL);
    for (i = 0; i < 2u * CTL; i++) {
        int32_t a = o[i] < 0 ? -o[i] : o[i];
        if (a > pk) pk = a;
    }
    return pk;
}
static int32_t run_peak(uint32_t blocks)
{
    int32_t pk = 0;
    while (blocks--) {
        int32_t p = blk_peak();
        if (p > pk) pk = p;
    }
    return pk;
}

int main(void)
{
    uint32_t p, k;
    track_t *t = &trk[0];
    host_tracks_init();
    song.g[G_BPM] = 120;
    check(ENGINES[ENGI_ACID] == &ENG_ACID && ENG_ACID.poly == 1u && ENG_ACID.ownenv && sizeof(bass303_t) <= sizeof(eng_arena_t),
          "the engine: mono, its own envelopes, the 303 in the track's arena");

    for (p = 0; p < ENG_ACID.npresets; p++) {   /* every preset: a phrase, bounded and audible */
        int32_t pk = 0, fin = 1;
        char what[96];
        host_tracks_init();
        host_preset(t, ENGI_ACID, p);
        t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
        for (k = 0; k < 8u; k++) {
            int32_t q;
            trk_note_on(t, 36u + (k * 7u) % 24u, k % 3u ? 96u : 127u);
            q = run_peak(FS / 8u / CTL);
            trk_note_off(t, 36u + (k * 7u) % 24u);
            q = q > run_peak(FS / 16u / CTL) ? q : run_peak(FS / 16u / CTL);
            pk = q > pk ? q : pk;
            fin &= q < (1 << 23);
        }
        snprintf(what, sizeof what, "%-12s a phrase: peak %d (audible, bounded)", ENG_ACID.presets[p].name, pk);
        check(fin && pk > 2000 && pk < (1 << 22), what);
    }

    {   /* accent: the same note, accented, comes out louder (and brighter) */
        int32_t plain, acc;
        host_tracks_init();
        host_preset(t, ENGI_ACID, 0);
        t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
        trk_note_on(t, 45, 96);
        plain = run_peak(FS / 10u / CTL);
        trk_note_off(t, 45);
        run_peak(FS / CTL);
        trk_note_on(t, 45, 127);
        acc = run_peak(FS / 10u / CTL);
        trk_note_off(t, 45);
        run_peak(FS / CTL);
        printf("acid: accent peak %d, plain %d\n", acc, plain);
        check(acc > plain * 11 / 10, "an accented note (velocity 127) is louder than a plain one (96)");
    }

    {   /* slide: a legato note with a glide keeps the 303's gate and does not attack again */
        bass303_t *b;
        float amp_before;
        int still_gate;
        host_tracks_init();
        host_preset(t, ENGI_ACID, 0);
        trk_note_on(t, 40, 96);
        run_peak(FS / 8u / CTL);
        b = acid_of(t);
        amp_before = b->amp_y;
        t->slide_glide = 1;                         /* (seq.c: a SLIDE step: the next note glides) */
        trk_note_on(t, 47, 96);
        trk_note_off(t, 40);
        still_gate = b->gate;
        run_peak(2u);
        check(still_gate && !b->amp_trig && b->amp_y <= amp_before * 1.5f + 1e-6f,
              "a slide (legato, glide on): the gate stays, no new attack");
        trk_note_off(t, 47);
        for (k = 0; k < 3u * FS / CTL && t->v[0].active; k++)
            blk_peak();
        check(!t->v[0].active && !b->gate, "released: the voice ends once the 303's amp has died (done)");
    }

    {   /* two ACID tracks, each its own 303 */
        bass303_t *a, *b;
        host_tracks_init();
        host_preset(&trk[0], ENGI_ACID, 0);
        host_preset(&trk[3], ENGI_ACID, 1);
        trk_note_on(&trk[0], 36, 100);
        trk_note_on(&trk[3], 48, 100);
        run_peak(FS / 10u / CTL);
        a = acid_of(&trk[0]);
        b = acid_of(&trk[3]);
        check(a != b && a->gate && b->gate && a->wave == 0 && b->wave == 1, "two ACID tracks: two 303s (saw, square), both sounding");
        trk_note_off(&trk[0], 36);
        trk_note_off(&trk[3], 48);
        run_peak(FS / CTL);
    }

    {   /* the arena: FM6 played on track 1, then ACID: the 303 starts from init, not from FM6's bytes */
        bass303_t *b;
        host_tracks_init();
        host_preset(t, ENGI_FM6, 0);
        trk_note_on(t, 60, 100);
        run_peak(FS / 10u / CTL);
        trk_note_off(t, 60);
        run_peak(FS / CTL);
        host_preset(t, ENGI_ACID, 2);
        trk_note_on(t, 36, 100);
        b = acid_of(t);
        check(b->tuning > 400.0f && b->tuning < 480.0f && b->gate, "after FM6 on the same track: the 303 initialised (tuning ~440 Hz)");
        trk_note_off(t, 36);
        run_peak(FS / CTL);
    }

    {   /* TOOLS > GEN: a 303 line in the track's key, the same for the same seed, slides never into a rest */
        static step_t first[NSTEP];
        uint32_t i, notes = 0, inkey = 1, slides_ok = 1, acc_ok = 1, len, mask, root, same;
        host_tracks_init();
        host_preset(t, ENGI_ACID, 0);
        t->p[P_SLEN] = 16;
        t->p[P_ROOT] = 9;                           /* A */
        t->p[P_SCALE] = 2;                          /* (a minor scale) */
        t->step[20].n = 1, t->step[20].time = ST_NOTE, t->step[20].note[0] = 99;   /* past LEN: cleared */
        tb3po_fill(t, 12345);
        memcpy(first, t->step, sizeof first);
        len = 16;
        mask = scale_mask(t);
        root = 9;
        for (i = 0; i < len; i++) {
            const step_t *s = &t->step[i];
            if (s->time != ST_NOTE || !s->n)
                continue;
            notes++;
            inkey &= (mask >> ((s->note[0] + 12u - root) % 12u)) & 1u;
            inkey &= s->note[0] >= 36u + root && s->note[0] < 36u + root + 24u;
            acc_ok &= (s->flags & SF_ACCENT) ? s->vel == 127u : s->vel == 96u;
            if (s->flags & SF_SLIDE)
                slides_ok &= t->step[(i + 1u) % len].time == ST_NOTE;
        }
        tb3po_fill(t, 12345);
        same = !memcmp(first, t->step, sizeof first);
        tb3po_fill(t, 999);
        check(notes >= 6u && notes <= 16u && inkey && slides_ok && acc_ok && same && memcmp(first, t->step, sizeof first) &&
              t->step[20].time == ST_REST, "GEN: a line in the track's key (two octaves from C2 + ROOT), seeded, no slide into a rest");
        printf("acid: GEN, seed 12345: %u notes of 16\n", notes);
    }

    {   /* cost: one ACID track, a 16th line, against the idle mix (host, rough) */
        uint64_t t0, t1;
        uint32_t f;
        host_tracks_init();
        t0 = now_ns();
        for (f = 0; f < 4u * FS / CTL; f++) blk_peak();
        t1 = now_ns();
        double idle = (double)(t1 - t0) / (4.0 * FS);
        host_preset(t, ENGI_ACID, 3);              /* ACID RAT: the drive on */
        t0 = now_ns();
        for (f = 0; f < 4u * FS / CTL; f++) {
            if (f % 8u == 0u) trk_note_on(t, 36u + (f / 8u) % 12u, f % 32u ? 96u : 127u);
            if (f % 8u == 6u) trk_note_off(t, 36u + (f / 8u) % 12u);
            blk_peak();
        }
        t1 = now_ns();
        printf("acid: cost (host -O2): idle mix %.1f ns, + one ACID track (RAT drive) %.1f ns a sample\n", idle,
               (double)(t1 - t0) / (4.0 * FS) - idle);
    }
    printf("acid: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
