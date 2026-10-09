/* SPDX-License-Identifier: GPL-3.0-only */
/* WAVE (firmware/src/eng_wave.c: single-cycle waves) on the host, through hostsim.c: every built-in table
 * band-limited (no harmonic above its level's top), the level a note reads keeps its top harmonic under 20 kHz
 * from the lowest note to the highest, MORPH 0 / 127 = wave A / B alone, a user wave read from its slot (an
 * empty one plays the sine), every preset bounded and audible. Run by tests/run_tests.sh. */
#include <stdint.h>
static uint8_t host_waves[15u * 1280u + 4096u];
#define WT_USER_XIP(k) (host_waves + (k) * 1280u)
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(int ok, const char *what)
{
    printf("wave: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* the largest harmonic of table t (length n) with an amplitude above -60 dB of the waves' full level (30000):
 * what is under it is the int16 rounding of a quiet level, not a harmonic */
static uint32_t top_harmonic(const int16_t *t, uint32_t n)
{
    double amp[129] = {0}, mx = 30000.0 * n / 2.0;
    uint32_t k, i, top = 0;
    for (k = 1; k <= n / 2u && k <= 128u; k++) {
        double c = 0, s = 0;
        for (i = 0; i < n; i++) {
            c += t[i] * cos(2 * M_PI * k * i / n);
            s += t[i] * sin(2 * M_PI * k * i / n);
        }
        amp[k] = sqrt(c * c + s * s);
    }
    for (k = 1; k <= n / 2u && k <= 128u; k++)
        if (amp[k] > mx * 1e-3)
            top = k;
    return top;
}

static int32_t render_peak(track_t *t, uint32_t note, uint32_t blocks)
{
    int32_t o[2 * CTL], pk = 0;
    uint32_t b, i;
    trk_note_on(t, note, 100);
    for (b = 0; b < blocks; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            if (a > pk) pk = a;
        }
    }
    trk_note_off(t, note);
    for (b = 0; b < FS / CTL; b++) mix_block(o, CTL);
    return pk;
}

int main(void)
{
    static const uint8_t TOP[WT_LEVELS] = {127, 64, 32, 15, 8, 4, 2, 1};
    uint32_t w, l, ok = 1, n;
    track_t *t = &trk[0];
    for (w = 0; w < WT_BUILTIN; w++)
        for (l = 0; l < WT_LEVELS; l++)
            ok &= top_harmonic(&WT_DATA[w][WT_OFF[l]], WT_LEN[l]) <= TOP[l];
    check(ok, "every built-in wave: each level holds no harmonic above its top (128 >> level)");
    ok = 1;
    for (n = 0; n < 128u * 16u; n += 4u) {          /* the whole pitch range, 1/4 semitone apart */
        uint32_t inc = pitch_inc(n), lv = wt_level(inc);
        double f0 = (double)inc * FS / 4294967296.0;
        ok &= TOP[lv] * f0 < 20000.0 || lv == WT_LEVELS - 1u;
        ok &= lv == 0u || TOP[lv - 1u] * f0 >= 20000.0;   /* (and the richest level that does) */
    }
    check(ok, "wt_level: the richest level whose top harmonic stays under 20 kHz, every pitch");

    {   /* MORPH 0 = A alone, 127 = B alone: compared with a render of B as A */
        int32_t a0, b0, ab;
        host_tracks_init();
        host_preset(t, ENGI_WAVE, 0);
        t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = t->p[P_LD_SHP] = t->p[P_E7] = t->p[P_E3] = 0;
        t->p[P_E0] = 2, t->p[P_E1] = 0, t->p[P_E2] = 0;             /* SAW, SINE: morph 0 */
        a0 = render_peak(t, 60, 40);
        t->p[P_E2] = 127;                                           /* morph 127: the sine */
        ab = render_peak(t, 60, 40);
        t->p[P_E0] = 0, t->p[P_E2] = 0;                             /* the sine as A */
        b0 = render_peak(t, 60, 40);
        check(a0 != ab && abs(ab - b0) * 50 < b0, "MORPH 0 plays wave A, 127 wave B (as B on its own)");
    }

    {   /* a user wave: its slot read through "XIP"; empty, the sine */
        wt_user_hdr_t *h = (wt_user_hdr_t *)WT_USER_XIP(2);
        int16_t *d = (int16_t *)(h + 1);
        uint32_t i;
        memset(host_waves, 0xFF, sizeof host_waves);
        wt_user_scan(2);
        check(!(wt_user_ok & 4u) && wt_table(WT_BUILTIN + 2u, 0) == &WT_DATA[0][0] && !strcmp(wt_names_all[WT_BUILTIN + 2u], "U3"),
              "an empty user slot: the sine plays, named U3");
        memset(h, 0, sizeof *h);
        h->magic = WT_USER_MAGIC, h->version = 1, h->size = WT_SIZE;
        memcpy(h->name, "MYWAVE  ", 8);
        for (i = 0; i < WT_SIZE; i++) d[i] = WT_DATA[16][i];        /* (SYNC's pyramid) */
        wt_user_scan(2);
        check((wt_user_ok & 4u) && wt_table(WT_BUILTIN + 2u, 3) == d + WT_OFF[3] && !strcmp(wt_names_all[WT_BUILTIN + 2u], "MYWAVE"),
              "a user wave in slot 3: read in place (its levels), its name on WAVE A / B");
        host_tracks_init();
        host_preset(t, ENGI_WAVE, 0);
        t->p[P_E0] = (int16_t)(WT_BUILTIN + 2u), t->p[P_E2] = 0;
        check(render_peak(t, 48, 40) > 2000, "... and it sounds");
    }

    for (w = 0; w < ENG_WAVE.npresets; w++) {
        int32_t pk;
        char what[96];
        host_tracks_init();
        host_preset(t, ENGI_WAVE, w);
        pk = render_peak(t, 57, FS / 4u / CTL);
        snprintf(what, sizeof what, "%-12s peak %d (audible, bounded)", ENG_WAVE.presets[w].name, pk);
        check(pk > 1500 && pk < (1 << 22), what);
    }
    printf("wave: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
