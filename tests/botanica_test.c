/* SPDX-License-Identifier: GPL-3.0-only */
/* BOTANICA on the host (-DEDITION_BOTANICA=1, through hostsim.c; run by tests/run_tests_botanica.sh):
 *   the engines   FELT (every preset bounded, audible, no DC, its voices end; BLOOM brings the strings up), DROPLET
 *                 (the echoes: jumps of 12 / 19 / 24 only, never a stolen voice, DROPS 0 none, the song's DROPLETS
 *                 adds them), NOISE's FOLEY (every kind a cluster of bursts, quiet after)
 *   PERFORM / SKY BLOOM (SWELL ducks the reverb on an onset and swells it back; FREEZE holds the tail, bounded),
 *                 GLITCH (punch-in effects on the sixteenths, never over a held one, gone at 0), FOLEY (hits at the
 *                 knob's rate on the FOLEY tracks only, none at 0), WEATHER (the wow within 22 cents, DUST from it),
 *                 BREEZE (within +-6 cents, moving)
 *   the sequencer DRIFT (a ramp over RAMP bars from the next bar, RAMP 0 a jump), BREAK / DROP, the petal chords
 *                 (MAJ7 MAJ9 ADD9 6/9 LYD#11) and progressions (ROYAL, PETAL), STRUM's order (UP, DOWN, RAND) and
 *                 HUMAN (+-12 %, <= 10 ms), the ratchets' shapes (RAMP, BOUNCE, RAND)
 *   the stores    projects are BTP1 (a NoteSorcery NSP1 project is refused)
 * and writes demos into DIR: every FELT / DROPLET / FOLEY preset, and a botanica sketch (an ambient intro at 72 BPM,
 * the break, the drop into 160). */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { (void)i; return ENGI_FELT; }
#include "../firmware/src/project.c"

static int bad;
static void check(int ok, const char *what)
{
    printf("botanica: %-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- rendering */
static int32_t out2[2 * CTL];
static FILE *wav;
static uint32_t wav_frames;
static void blk(void)
{
    uint32_t i;
    mix_block(out2, CTL);
    if (wav) {
        for (i = 0; i < CTL; i++)
            wav_put(wav, out2[2u * i], out2[2u * i + 1u]);
        wav_frames += CTL;
    }
}
static void blocks(uint32_t n) { while (n--) blk(); }
static uint32_t secs(double s) { return (uint32_t)(s * FS / CTL); }
static void wav_open(const char *dir, const char *name)
{
    char path[512];
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    wav = fopen(path, "wb");
    wav_frames = 0;
    if (wav)
        wav_hdr(wav, 0);
}
static void wav_close(void)
{
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    wav_hdr(wav, wav_frames);
    fclose(wav);
    wav = 0;
}
static int voices_on(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return (int)n;
}
/* the notes that started in the last block (any part): note, velocity, the block */
#define NSTART 256
static struct { uint8_t trk, note, vel; uint32_t at; } st[NSTART];
static uint32_t nst, seen_age, blk_no;
static void starts_reset(void) { nst = 0; seen_age = vage; blk_no = 0; }
static void blk_watch(void)
{
    uint32_t p, i, top = seen_age;
    blk();
    blk_no++;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && v->age > seen_age && nst < NSTART) {
                st[nst].trk = (uint8_t)p, st[nst].note = v->note, st[nst].vel = v->vel, st[nst].at = blk_no;
                nst++;
                if (v->age > top)
                    top = v->age;
            }
        }
    seen_age = top;
}
/* level of the output over n blocks: peak, mean (DC), rms */
static int32_t pk;
static double dc, rms;
static void measure(uint32_t n)
{
    double s = 0, q = 0;
    uint32_t b, i, cnt = 0;
    pk = 0;
    for (b = 0; b < n; b++) {
        blk();
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out2[i] < 0 ? -out2[i] : out2[i];
            if (a > pk)
                pk = a;
            s += out2[i];
            q += (double)out2[i] * out2[i];
            cnt++;
        }
    }
    dc = s / cnt;
    rms = sqrt(q / cnt);
}
static void quiet_mix(void)
{
    uint32_t k;
    host_tracks_init();
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_MUTE] = 0;
    song.g[G_DRLVL] = 0;
}
static void start_play(void)
{
    transport_req = 1;
    blk();
}
static void stop_play(void)
{
    transport_req = 2;
    blk();
}

/* ---- the engines */
static void t_presets(const char *dir, uint32_t e, const char *what)
{
    uint32_t pi, ok = 1, freed = 1, dcok = 1, loud = 1;
    char nm[64];
    for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
        track_t *t = &trk[0];
        uint32_t k;
        static const uint8_t PH[4] = {48, 55, 60, 64};
        quiet_mix();
        host_preset(t, e, pi);
        snprintf(nm, sizeof nm, "%s_%02u_%s", N_ENGNAME[e], pi, ENGINES[e]->presets[pi].name);
        for (k = 0; nm[k]; k++)
            if (nm[k] == ' ' || nm[k] == '/' || nm[k] == '+' || nm[k] == '#')
                nm[k] = '_';
        wav_open(dir, nm);
        for (k = 0; k < 4u; k++)
            trk_note_on(t, PH[k], 100);
        measure(secs(1.5));
        for (k = 0; k < 4u; k++)
            trk_note_off(t, PH[k]);
        {
            int32_t p1 = pk;
            double d1 = dc;
            measure(secs(0.5));
            if (pk > p1)
                p1 = pk;
            ok &= p1 < 30000;
            loud &= p1 > 250;
            dcok &= fabs(d1) < 400.0;
            if (p1 >= 30000 || p1 <= 250 || fabs(d1) >= 400.0)
                printf("botanica:   %s %s: peak %d, dc %.0f\n", N_ENGNAME[e], ENGINES[e]->presets[pi].name, p1, d1);
        }
        for (k = 0; k < secs(12) && voices_on(t); k++)
            blk();
        freed &= !voices_on(t);
        if (voices_on(t))
            printf("botanica:   %s %s: voices still on 12 s after the note-offs\n", N_ENGNAME[e], ENGINES[e]->presets[pi].name);
        wav_close();
    }
    snprintf(nm, sizeof nm, "%s: every preset bounded, audible, no DC, its voices end", what);
    check(ok && loud && dcok && freed, nm);
}

static void t_felt_bloom(void)
{
    double r0, r1;
    track_t *t = &trk[0];
    quiet_mix();
    host_preset(t, ENGI_FELT, 0);                        /* FELT UPRIGHT: no strings */
    t->p[P_REV] = t->p[P_CHOR] = t->p[P_DLY] = 0;
    trk_note_on(t, 60, 100);
    blocks(secs(2.5));
    measure(secs(0.5));
    r0 = rms;
    trk_note_off(t, 60);
    blocks(secs(4));
    song.g[G_BLOOM] = 127;
    trk_note_on(t, 60, 100);
    blocks(secs(2.5));
    measure(secs(0.5));
    r1 = rms;
    trk_note_off(t, 60);
    song.g[G_BLOOM] = 0;
    blocks(secs(4));
    printf("botanica:   FELT UPRIGHT held 2.5 s: rms %.0f, with BLOOM 127 %.0f\n", r0, r1);
    check(r1 > r0 * 1.5, "FELT: the song's BLOOM brings the string bloom up under a held note");
}

static void t_droplet(void)
{
    track_t *t = &trk[0], *u = &trk[1];
    uint32_t i, e0, ok = 1, n, kills0, d0;
    quiet_mix();
    host_preset(t, ENGI_DROPLET, 0);
    t->p[P_E1] = 127, t->p[P_E2] = 127;                  /* DROPS, JUMP all the way */
    starts_reset();
    e0 = drop_echoes;
    trk_note_on(t, 60, 100);
    blk_watch();
    trk_note_off(t, 60);
    for (i = 0; i < secs(1.5); i++)
        blk_watch();
    n = drop_echoes - e0;
    for (i = 0; i < nst; i++)
        ok &= st[i].note == 60 || st[i].note == 72 || st[i].note == 79 || st[i].note == 84;
    printf("botanica:   DROPLET, DROPS 127 JUMP 127: %u echoes, notes", n);
    for (i = 0; i < nst; i++)
        printf(" %u", st[i].note);
    printf("\n");
    check(n == 3u && nst >= 4u && ok, "DROPLET: three echoes of a note, each the note or +12 / +19 / +24");
    blocks(secs(6));
    /* DROPS 0: none; the song's DROPLETS: some */
    host_preset(t, ENGI_DROPLET, 1);                     /* MARIMBA: DROPS 0 */
    e0 = drop_echoes;
    trk_note_on(t, 60, 100);
    blk();
    trk_note_off(t, 60);
    blocks(secs(1));
    check(drop_echoes == e0, "DROPLET: DROPS 0, no echo");
    song.g[G_DROPS] = 127;
    trk_note_on(t, 62, 100);
    blk();
    trk_note_off(t, 62);
    blocks(secs(1));
    check(drop_echoes > e0, "DROPLET: the song's DROPLETS (PERFORM knob 2) drops echoes from it too");
    song.g[G_DROPS] = 0;
    blocks(secs(6));
    /* the budget full (another part holds 8 notes): echoes are left out, nothing is stolen for them */
    host_preset(u, ENGI_ANALOG, 3);                      /* CANOPY PAD (POLY) */
    u->p[P_SUS] = 127;
    for (i = 0; i < 8u; i++)
        trk_note_on(u, 48u + 3u * i, 90);
    blocks(4);
    host_preset(t, ENGI_DROPLET, 0);
    t->p[P_E1] = 127;
    kills0 = voice_kills, d0 = drop_dropped;
    trk_note_on(t, 60, 100);
    blk();
    trk_note_off(t, 60);
    blocks(secs(1));
    printf("botanica:   budget full: %u echoes left out, %u voices taken (the played note: 1)\n", drop_dropped - d0,
           voice_kills - kills0);
    check(drop_dropped > d0 && voice_kills - kills0 <= 1u, "DROPLET: with the voice budget full its echoes are left out, never steal");
    for (i = 0; i < 8u; i++)
        trk_note_off(u, 48u + 3u * i);
    blocks(secs(8));
    check(!voices_on(t) && !voices_on(u), "DROPLET: every voice free after");
}

static void t_foley_kinds(void)
{
    static const char *const K[FOLEY_KINDS] = {"TWIG", "LEAF", "DROP", "BRUSH", "BREATH", "CLICK", "CRACKLE"};
    uint32_t k, ok = 1, onsets[FOLEY_KINDS];
    track_t *t = &trk[0];
    for (k = 0; k < FOLEY_KINDS; k++) {
        uint32_t b, i, on = 0;
        int32_t env = 0, prev = 0, top = 0;
        quiet_mix();
        host_preset(t, ENGI_NOISE, 0);
        t->p[P_REV] = t->p[P_DLY] = t->p[P_CHOR] = 0;
        t->p[P_E5] = (int16_t)(k * 128u / FOLEY_KINDS + 9u);
        t->p[P_E7] = 64;
        trk_note_on(t, 60, 110);
        for (b = 0; b < secs(1.2); b++) {               /* onsets: the envelope jumping up */
            blk();
            for (i = 0; i < CTL; i++) {
                int32_t a = out2[2u * i] < 0 ? -out2[2u * i] : out2[2u * i];
                env = a > env ? a : env - (env >> 7);
            }
            if (env > 600 && env > prev * 2)
                on++;
            prev = env;
            top = env > top ? env : top;
        }
        trk_note_off(t, 60);
        measure(secs(1.5));
        onsets[k] = on;
        ok &= (on >= 1u || (FOLEY[k].att && top > 600)) && pk < 200;   /* (BRUSH, BREATH rise: no sharp onset) */
        printf("botanica:   FOLEY %-8s %2u onsets, peak %d, after: peak %d\n", K[k], on, top, pk);
    }
    check(ok, "FOLEY: every kind a burst cluster, quiet once released");
    check(onsets[1] > onsets[5], "FOLEY: a LEAF crunch is many cracks, a CLICK one");
}

/* ---- PERFORM / SKY */
static double rev_tail(int freeze)
{
    track_t *t = &trk[0];
    double a, b;
    quiet_mix();
    host_preset(t, ENGI_ANALOG, 3);
    t->p[P_REV] = 127;
    host_preset(t, ENGI_ANALOG, 9);                      /* PETAL PLUCK: struck, at once */
    t->p[P_REV] = 127;
    song.g[G_BMODE] = 1;
    song.g[G_BLOOM] = 0;
    trk_note_on(t, 60, 110), trk_note_on(t, 67, 110);
    blocks(secs(0.6));
    song.g[G_BLOOM] = freeze ? 127 : 0;                  /* the reverb full: freeze it */
    blocks(secs(0.4));
    trk_note_off(t, 60), trk_note_off(t, 67);
    blocks(secs(3));
    measure(secs(0.5));
    a = rms;
    blocks(secs(3));
    measure(secs(0.5));
    b = rms;
    printf("botanica:   reverb tail, FREEZE %s: rms %.0f at 3 s, %.0f at 6.5 s, peak %d\n", freeze ? "on " : "off", a, b, pk);
    song.g[G_BLOOM] = 0;
    song.g[G_BMODE] = 0;
    blocks(secs(4));
    return freeze ? (a > 200 && b > a * 0.5 && b < a * 1.5 && pk < 30000) : (b < 30);
}

static void t_bloom(void)
{
    track_t *t = &trk[0];
    int32_t lo = 32767, hi = 0;
    uint32_t i;
    check(rev_tail(0) && rev_tail(1), "BLOOM FREEZE: the reverb holds its tail (bounded, no growth); off, it dies away");
    quiet_mix();
    host_preset(t, ENGI_FELT, 0);
    t->p[P_REV] = 100;
    song.g[G_BLOOM] = 127, song.g[G_BMODE] = 0;
    blocks(secs(1));
    trk_note_on(t, 60, 110);
    for (i = 0; i < secs(0.1); i++) {
        blk();
        lo = bot.swell < lo ? bot.swell : lo;
    }
    blocks(secs(2));
    hi = bot.swell;
    trk_note_off(t, 60);
    song.g[G_BLOOM] = 0;
    blocks(secs(3));
    printf("botanica:   SWELL: the reverb's return down to %d on the onset, %d 2 s later\n", lo, hi);
    check(lo < 8000 && hi > 30000, "BLOOM SWELL: an onset ducks the reverb's return, it swells back within 2 s");
}

static void t_glitch(void)
{
    track_t *t = &trk[0];
    uint32_t i, starts = 0, during_hold = 0;
    int8_t last = -1;
    static const uint8_t C[1] = {60};
    quiet_mix();
    host_preset(t, ENGI_ANALOG, 3);
    for (i = 0; i < 16u; i++)
        put_step(t, i, 1, C, ST_NOTE, 0);
    song.g[G_GLITCH] = 127;
    start_play();
    for (i = 0; i < secs(8); i++) {
        blk();
        if (punch.req >= 0 && punch.req != last)
            starts++;
        last = punch.req;
    }
    punch.req = 3, punch.keybit = 1u << 4;               /* a finger holds LOOP 32 */
    for (i = 0; i < secs(4); i++) {
        blk();
        during_hold += punch.req != 3;
    }
    punch.req = -1, punch.keybit = 0;
    song.g[G_GLITCH] = 0;
    blocks(secs(1));
    printf("botanica:   GLITCH 127, 8 s at 120 BPM: %u punch-in effects started\n", starts);
    check(starts >= 6u, "GLITCH: punch-in effects now and then on the sixteenths");
    check(!during_hold, "GLITCH: never over the effect a finger holds");
    check(punch.req < 0 && bseq.gl_fx < 0, "GLITCH: at 0, nothing left on");
    stop_play();
}

static void t_foley_gen(void)
{
    track_t *t = &trk[2], *a = &trk[0];
    uint32_t i, n0, n1, other = 0;
    quiet_mix();
    host_preset(t, ENGI_NOISE, 1);                       /* LEAF CRUNCH (FOLEY) */
    host_preset(a, ENGI_FELT, 0);
    song.g[G_FOLEY] = 0;
    starts_reset();
    start_play();
    for (i = 0; i < secs(2); i++)                        /* 2 s at 120 BPM: 16 sixteenths */
        blk_watch();
    n0 = nst;
    song.g[G_FOLEY] = 127;
    starts_reset();
    for (i = 0; i < secs(4); i++)
        blk_watch();
    for (i = 0, n1 = 0; i < nst; i++) {
        n1 += st[i].trk == 2u;
        other += st[i].trk != 2u;
    }
    song.g[G_FOLEY] = 0;
    stop_play();
    printf("botanica:   FOLEY 0: %u hits; FOLEY 127: %u hits in 32 sixteenths, %u on other tracks\n", n0, n1, other);
    check(n0 == 0u, "FOLEY 0: no hit");
    check(n1 >= 8u && n1 <= 26u && !other, "FOLEY 127: about half the sixteenths, on the FOLEY track only");
}

static void t_weather_breeze(void)
{
    uint32_t i;
    int32_t wmax = 0, bmax = 0, bmin = 0;
    quiet_mix();
    host_preset(&trk[0], ENGI_FELT, 0);
    check(bot_weather_dust() == 0 && bot.wow == 0, "WEATHER 0: no wow, no dust");
    song.g[G_WEATHER] = 127;
    for (i = 0; i < secs(5); i++) {
        blk();
        wmax = abs(bot.wow) > wmax ? abs(bot.wow) : wmax;
    }
    check(bot_weather_dust() == 127 && wmax > 600 && wmax <= 902, "WEATHER 127: the tape's wow and flutter (<= 22 cents), DUST all the way");
    song.g[G_WEATHER] = 0;
    song.g[G_BREEZE] = 127;
    blocks(1);
    for (i = 0; i < secs(20); i++) {
        int32_t b;
        blk();
        b = bot_pitch(&trk[0]);
        bmax = b > bmax ? b : bmax;
        bmin = b < bmin ? b : bmin;
    }
    song.g[G_BREEZE] = 0;
    printf("botanica:   WEATHER 127: wow up to %.1f cents; BREEZE 127: %.1f .. %.1f cents over 20 s\n", wmax / 40.96,
           bmin / 40.96, bmax / 40.96);
    check(bmax <= 246 && bmin >= -246 && bmax - bmin > 120, "BREEZE: a slow walk of the pitch, within +-6 cents");
}

/* ---- the sequencer */
static void t_drift(void)
{
    uint32_t i, mid = 0, k;
    static const uint8_t C[1] = {60};
    quiet_mix();
    for (i = 0; i < 4u; i++)
        put_step(&trk[0], i * 4u, 1, C, ST_NOTE, 0);
    song.g[G_BPM] = 60;
    song.g[G_DRIFT] = 0;
    start_play();
    blocks(secs(0.5));
    song.g[G_DRIFT] = 160, song.g[G_RAMP] = 2;
    for (k = 0; k < secs(20) && song.g[G_BPM] != 160; k++) {
        blk();
        if (clk_beat >= 6u && clk_beat < 7u && !mid)
            mid = (uint32_t)song.g[G_BPM];
    }
    printf("botanica:   DRIFT 60 -> 160 over 2 bars: %u BPM in the middle, 160 after %.1f s\n", mid, k * (double)CTL / FS);
    check(song.g[G_BPM] == 160 && mid > 70u && mid < 150u && clk_beat >= 11u, "DRIFT: from the next bar, straight over RAMP bars to the target");
    song.g[G_DRIFT] = 80, song.g[G_RAMP] = 0;
    for (k = 0; k < secs(4) && song.g[G_BPM] != 80; k++)
        blk();
    check(song.g[G_BPM] == 80 && !(clk_beat & 3u), "DRIFT: RAMP 0 jumps on the next bar");
    /* BREAK / DROP */
    song.g[G_DRIFT] = 140;
    blocks(secs(1));
    song.sel = 0;
    bot_brk = 1;
    blocks(secs(0.5));
    check(trk_silent(&trk[1]) && !trk_silent(&trk[0]) && bot.frozen, "BREAK: the others silent, the selected track heard, the reverb frozen");
    bot_brk = 2;
    for (k = 0; k < secs(6) && bot_brk; k++)
        blk();
    check(!bot_brk && !(clk_beat & 3u) && song.g[G_BPM] == 140 && !trk_silent(&trk[1]),
          "DROP: everything back on the next bar, at the DRIFT tempo");
    song.g[G_DRIFT] = 0;
    stop_play();
    song.g[G_BPM] = 120;
}

static int notes_are(const uint8_t *c, uint32_t n, const uint8_t *w, uint32_t wn)
{
    uint32_t i;
    if (n != wn)
        return 0;
    for (i = 0; i < n; i++)
        if (c[i] != w[i])
            return 0;
    return 1;
}
static void t_chords(void)
{
    static const uint8_t W[5][4] = {{60, 64, 67, 71}, {60, 64, 71, 74}, {60, 64, 67, 74}, {60, 64, 69, 74}, {60, 64, 71, 78}};
    static const uint8_t R[4][4] = {{65, 69, 72, 76}, {55, 59, 62, 65}, {64, 67, 71, 74}, {57, 60, 64, 67}};
    static const uint8_t P[4][4] = {{65, 69, 72, 76}, {55, 59, 62, 69}, {57, 60, 67, 71}, {60, 64, 67, 74}};
    uint8_t c[4];
    uint32_t k, ok = 1, n;
    track_t *t = &trk[0];
    quiet_mix();
    t->p[P_ROOT] = 0, t->p[P_SCALE] = 1;
    for (k = 0; k < 5u; k++) {
        t->p[P_CHORD] = (int16_t)(CH_SEMI0 + k);
        n = chord_notes(t, 60, c);
        ok &= notes_are(c, n, W[k], 4);
    }
    check(ok, "petal chords on C4: MAJ7 60 64 67 71, MAJ9 60 64 71 74, ADD9, 6/9 60 64 69 74, LYD#11 60 64 71 78");
    ok = 1;
    t->p[P_CHORD] = CH_ROYAL;
    prog_pos[0] = 0;
    for (k = 0; k < 4u; k++) {
        n = chord_notes(t, 60, c);
        ok &= notes_are(c, n, R[k], 4);
        prog_advance(t);
    }
    check(ok && prog_pos[0] == 0, "ROYAL in C, at C4: Fmaj7 G7 Em7 Am7, each press the next, around again");
    ok = 1;
    t->p[P_CHORD] = CH_PETAL;
    for (k = 0; k < 4u; k++) {
        n = chord_notes(t, 60, c);
        ok &= notes_are(c, n, P[k], 4);
        prog_advance(t);
    }
    check(ok, "PETAL in C, at C4: Fmaj7 Gadd9 Am9 Cadd9");
    t->p[P_CHORD] = 0;
}

static void t_strum_human(void)
{
    static const uint8_t CH[4] = {60, 64, 67, 71};
    track_t *t = &trk[0];
    uint32_t i, k, up = 1, down = 1, rnd_orders = 0, vmin = 127, vmax = 0, late_max = 0;
    uint32_t first[4] = {0};
    quiet_mix();
    host_preset(t, ENGI_FELT, 0);
    t->p[P_STRUM] = 20;
    for (k = 0; k < 3u; k++) {                           /* UP, DOWN, RAND */
        uint32_t rep;
        t->p[P_STRDIR] = (int16_t)k;
        for (rep = 0; rep < 6u; rep++) {
            starts_reset();
            for (i = 0; i < 4u; i++)
                trk_note_chord(t, CH, 4, i, 100);
            for (i = 0; i < secs(0.15); i++)
                blk_watch();
            for (i = 0; i < 4u; i++)
                trk_note_off(t, CH[i]);
            blocks(secs(0.3));
            if (nst != 4u) {
                up = down = 0;
                continue;
            }
            if (k == 0u)
                up &= st[0].note == 60 && st[3].note == 71 && st[3].at - st[0].at >= 75u && st[3].at - st[0].at <= 85u;
            if (k == 1u)
                down &= st[0].note == 71 && st[3].note == 60;
            if (k == 2u)
                first[rep % 4u] = st[0].note, rnd_orders += rep && st[0].note != first[0];
        }
    }
    check(up, "STRUM 20 ms, UP: from the lowest, 60 ms from the first to the fourth");
    check(down, "STRUM, DOWN: from the highest");
    check(rnd_orders >= 1u, "STRUM, RAND: the order changes from chord to chord");
    t->p[P_STRUM] = 0;
    t->p[P_HUMAN] = 127;
    for (k = 0; k < 40u; k++) {
        starts_reset();
        trk_note_chord(t, CH, 1, 0, 100);
        for (i = 0; i < 20u; i++)
            blk_watch();
        trk_note_off(t, 60);
        if (nst) {
            vmin = st[0].vel < vmin ? st[0].vel : vmin;
            vmax = st[0].vel > vmax ? st[0].vel : vmax;
            late_max = st[0].at > late_max ? st[0].at : late_max;
        }
        blocks(4);
    }
    t->p[P_HUMAN] = 0;
    printf("botanica:   HUMAN 127: velocity %u .. %u (100 asked), up to %u blocks late\n", vmin, vmax, late_max - 1u);
    check(vmin >= 88u && vmax <= 112u && vmax - vmin >= 8u && late_max >= 2u && late_max <= 15u,
          "HUMAN: each note's velocity within +-12 %, up to 10 ms late");
}

static void t_ratchets(void)
{
    static const uint8_t C[1] = {60};
    track_t *t = &trk[0];
    uint32_t m, i;
    for (m = 1; m <= 3u; m++) {
        quiet_mix();
        host_preset(t, ENGI_FELT, 0);
        song.g[G_BPM] = 60;
        t->p[P_SDIV] = 0;                                /* a step = a beat: 1 s */
        t->p[P_SLEN] = 1;
        t->p[P_RATMODE] = (int16_t)m;
        put_step(t, 0, 1, C, ST_NOTE, 0);
        t->step[0].rat = 3;                              /* x4 */
        starts_reset();
        start_play();
        for (i = 0; i < secs(0.99); i++)
            blk_watch();
        stop_play();
        if (m == 1u)
            check(nst == 4u && st[0].note == 60 && st[1].note == 64 && st[2].note == 68 && st[3].note == 72,
                  "RATCH RAMP: x4 plays 60 64 68 72");
        if (m == 2u)
            check(nst == 4u && st[1].at > secs(0.48) && st[1].at < secs(0.52) && st[2].at > secs(0.73) &&
                  st[2].at < secs(0.77) && st[3].at > secs(0.86) && st[3].at < secs(0.9) && st[3].vel < st[1].vel,
                  "RATCH BOUNCE: x4 at 1/2, 3/4, 7/8 of the step, each softer");
        if (m == 3u) {
            uint32_t ok = nst == 4u;
            for (i = 0; i < nst; i++)
                ok &= st[i].note == 60 || st[i].note == 72;
            check(ok, "RATCH RAND: each hit the note or its octave");
        }
        t->p[P_RATMODE] = 0;
    }
    song.g[G_BPM] = 120;
}

/* ---- the stores */
static void t_stores(void)
{
    static project_t q;
    quiet_mix();
    proj_capture(&q);
    check(PROJ_MAGIC == 0x31505442u && proj_ok(&q) && !memcmp(&q.magic, "BTP1", 4), "projects: BTP1, a capture reads back");
    q.magic = 0x3150534Eu;                               /* NoteSorcery's NSP1 */
    q.sum = proj_sum(&q);
    check(!proj_ok(&q), "projects: a NoteSorcery NSP1 project is not read");
}

/* ---- a sketch: ambient at 72 BPM (PETAL chords on FELT, droplets, foley, BLOOM swelling), the break, the drop
 * into 160 (the HYPER kit, a supersaw) */
static void demo_sketch(const char *dir)
{
    static const uint8_t K[8] = {60, 0, 0, 60, 0, 0, 60, 0};
    uint32_t i, s;
    if (!dir)
        return;
    quiet_mix();
    song.g[G_DRLVL] = 100;
    host_preset(&trk[0], ENGI_FELT, 1);                 /* NURTURE KEYS */
    trk[0].p[P_CHORD] = CH_PETAL, trk[0].p[P_STRUM] = 18, trk[0].p[P_HUMAN] = 60;
    host_preset(&trk[1], ENGI_DROPLET, 0);              /* KALIMBA */
    host_preset(&trk[2], ENGI_ANALOG, 0);               /* SOFT SUB */
    host_preset(&trk[3], ENGI_NOISE, 1);                /* LEAF CRUNCH */
    host_preset(&trk[4], ENGI_ANALOG, 10);              /* HYPER LEAD */
    trk[4].p[P_LEVEL] = 0;
    for (s = 0; s < 16u; s++) {
        uint8_t c[4], n;
        if (s % 4u == 0u) {
            prog_pos[0] = (uint8_t)(s / 4u);
            n = (uint8_t)chord_notes(&trk[0], 60, c);
            put_step(&trk[0], s, n, c, ST_NOTE, 0);
        }
        if (K[s % 8u])
            put_step(&trk[1], s, 1, (const uint8_t[]){(uint8_t)(72 + (s * 5u) % 12u)}, ST_NOTE, 0);
        if (s % 8u == 0u)
            put_step(&trk[2], s, 1, (const uint8_t[]){(uint8_t)(s ? 41 : 36)}, ST_NOTE, 0);
    }
    for (i = TRK_DRUM; i < NTRK; i++)
        trk[i].p[P_E0] = (int16_t)(DRUM_SAMPLED + DS_KIT_HYPER);
    trk[TRK_DRUM].p[P_LEVEL] = 0;
    for (s = 0; s < 16u; s += 4u)
        trk[TRK_DRUM].dstep[s].on[0] |= 1u;              /* kick */
    for (s = 4; s < 16u; s += 8u)
        trk[TRK_DRUM].dstep[s].on[0] |= 2u;              /* snare */
    for (s = 0; s < 16u; s += 2u)
        trk[TRK_DRUM].dstep[s].on[0] |= 8u;              /* closed hat */
    song.g[G_BPM] = 72, song.g[G_BLOOM] = 90, song.g[G_FOLEY] = 50, song.g[G_WEATHER] = 40, song.g[G_BREEZE] = 80;
    song.g[G_DRIFT] = 160, song.g[G_RAMP] = 0;
    wav_open(dir, "sketch_ambient_break_drop");
    song.g[G_DRIFT] = 0;
    start_play();
    blocks(secs(14));                                    /* ambient */
    song.sel = 0;
    bot_brk = 1;                                         /* the break */
    blocks(secs(3));
    song.g[G_DRIFT] = 160;
    bot_brk = 2;                                         /* the drop */
    for (i = 0; i < secs(4) && bot_brk; i++)
        blk();
    trk[TRK_DRUM].p[P_LEVEL] = 104, trk[4].p[P_LEVEL] = 96, song.g[G_GLITCH] = 40, song.g[G_BLOOM] = 30;
    blocks(secs(8));
    stop_play();
    blocks(secs(2));
    wav_close();
    song.g[G_GLITCH] = song.g[G_BLOOM] = song.g[G_FOLEY] = song.g[G_WEATHER] = song.g[G_BREEZE] = 0;
    song.g[G_DRIFT] = 0, song.g[G_BPM] = 120;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    check(NENGINES == 9u && !strcmp(ENGINES[ENGI_FELT]->name, "FELT") && !strcmp(ENGINES[ENGI_DROPLET]->name, "DROPLET") &&
          !strcmp(N_ENGNAME[ENGI_FORMANT], "VOICE"), "BOTANICA's engines: FELT DROPLET ANALOG SAMPLE NOISE PHYS GRAIN VOICE WAVE");
    t_presets(dir, ENGI_FELT, "FELT");
    t_presets(dir, ENGI_DROPLET, "DROPLET");
    t_presets(dir, ENGI_NOISE, "NOISE (FOLEY)");
    t_felt_bloom();
    t_droplet();
    t_foley_kinds();
    t_bloom();
    t_glitch();
    t_foley_gen();
    t_weather_breeze();
    t_drift();
    t_chords();
    t_strum_human();
    t_ratchets();
    t_stores();
    demo_sketch(dir);
    printf(bad ? "botanica: FAILED (%d)\n" : "botanica: all checks ok\n", bad);
    return bad ? 1 : 0;
}
