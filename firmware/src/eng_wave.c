/* SPDX-License-Identifier: GPL-3.0-only */
/* WAVE: single-cycle waveforms (NoteSorcery). Two oscillators read a wave each (WAVE A, WAVE B) and MORPH
 * crossfades from A to B (LFO -> SHP and ENV -> SHP move it); SPRD detunes B (a wide pair when they are the
 * same wave), SUB adds a sine an octave down, DRV saturates, then the low-pass (CUT, RES; the track's LFO and ENV
 * move it, as on ANALOG).
 *
 * The waves: 24 built in (tools/gen_wavetables.py -> ns_waves.h: sine, tri, saw, square, pulses, organs, vowels,
 * bell, brass, reed, FM, sync, digital, glass, clavi, choir, metal, soft saw) and WT_USER of your own, uploaded
 * from the web editor (single-cycle WAV files, AKWF and the like: the editor builds the same pyramid) into flash
 * at WT_USER_BASE, read through the plain XIP window. Each wave is a band-limited mip pyramid (WT_LEVELS
 * tables, level l holding the harmonics up to 128 >> l): a note reads the level whose top harmonic stays under
 * 20 kHz, so nothing aliases up the keyboard. Integer DSP. */
#include "ns_waves.h"

#define WT_USER 15u                                 /* your waves: three a sector over five sectors */
#define WT_USER_BASE 0x9B000u                       /* (storage.c: 0x9B000..0x9FFFF, NoteSorcery) */
#define WT_USER_SLOT 1280u                          /* 64 B header + WT_SIZE int16 */
#define WT_USER_MAGIC 0x5657534Eu                   /* "NSWV" */
#define WT_ALL (WT_BUILTIN + WT_USER)
typedef struct {                                    /* a user wave's header (the editor writes it) */
    uint32_t magic;
    uint16_t version, size;                         /* 1, WT_SIZE */
    char name[8];
    uint32_t crc;                                   /* CRC-32 of the samples */
    uint8_t rsv[44];
} wt_user_hdr_t;
_Static_assert(sizeof(wt_user_hdr_t) == 64u && 64u + WT_SIZE * 2u <= WT_USER_SLOT, "a user wave's slot");
_Static_assert(3u * WT_USER_SLOT <= 4096u && (WT_USER + 2u) / 3u * 4096u <= 0xA0000u - WT_USER_BASE, "the user waves' flash");
static uint32_t wt_user_off(uint32_t k) { return WT_USER_BASE + k / 3u * 4096u + k % 3u * WT_USER_SLOT; }
#ifndef WT_USER_XIP                                 /* host tests: a RAM image */
#define WT_USER_XIP(k) fm1_xip_ptr(wt_user_off(k))
#endif
static uint16_t wt_user_ok;                         /* bit per slot: a valid wave there (wt_user_scan) */
static char wt_user_name[WT_USER][9] = {"U1", "U2", "U3", "U4", "U5", "U6", "U7", "U8", "U9", "U10", "U11", "U12",
                                       "U13", "U14", "U15"};
/* WAVE A / B's names (an F_ENUM): the built-in ones, then each user slot's (its own name, or U1 .. U15) */
static const char *wt_names_all[WT_ALL + 1u] = {WT_NAMES_INIT,
    wt_user_name[0], wt_user_name[1], wt_user_name[2], wt_user_name[3], wt_user_name[4], wt_user_name[5],
    wt_user_name[6], wt_user_name[7], wt_user_name[8], wt_user_name[9], wt_user_name[10], wt_user_name[11],
    wt_user_name[12], wt_user_name[13], wt_user_name[14], 0};
_Static_assert(WT_USER == 15u, "a name per user slot above");
static void wt_user_scan(uint32_t k)                /* (boot, after an upload) */
{
    const wt_user_hdr_t *h;
    if (k >= WT_USER)
        return;
    h = (const wt_user_hdr_t *)WT_USER_XIP(k);
    wt_user_ok &= (uint16_t)~(1u << k);
    if (h->magic == WT_USER_MAGIC && h->version == 1u && h->size == WT_SIZE) {
        uint32_t i;
        wt_user_ok |= (uint16_t)(1u << k);
        for (i = 0; i < 8u; i++)
            wt_user_name[k][i] = h->name[i] >= ' ' && h->name[i] <= '~' ? h->name[i] : ' ';
        wt_user_name[k][8] = 0;
        for (i = 8u; i > 0u && wt_user_name[k][i - 1u] == ' '; i--)
            wt_user_name[k][i - 1u] = 0;
    } else {                                        /* empty: U1 .. U15 */
        wt_user_name[k][0] = 'U';
        wt_user_name[k][1] = (char)(k < 9u ? '1' + k : '1');
        wt_user_name[k][2] = (char)(k < 9u ? 0 : '0' + k - 9u);
        wt_user_name[k][3] = 0;
    }
}
/* the samples of wave w at level l (an empty user slot: the sine) */
static const int16_t *wt_table(uint32_t w, uint32_t l)
{
    if (w >= WT_BUILTIN && w < WT_ALL && ((wt_user_ok >> (w - WT_BUILTIN)) & 1u))
        return (const int16_t *)(WT_USER_XIP(w - WT_BUILTIN) + sizeof(wt_user_hdr_t)) + WT_OFF[l];
    return &WT_DATA[w < WT_BUILTIN ? w : 0u][WT_OFF[l]];
}
/* the mip level of a note (its phase increment): the first whose top harmonic, at this pitch, is under 20 kHz */
static uint32_t wt_level(uint32_t inc)
{
    static const uint8_t TOP[WT_LEVELS] = {127, 64, 32, 15, 8, 4, 2, 1};   /* the harmonics each level holds */
    uint32_t l;
    for (l = 0; l + 1u < WT_LEVELS; l++)            /* inc * TOP * FS / 2^32 < 20000 Hz */
        if ((uint64_t)inc * TOP[l] * FS < (uint64_t)20000u << 32)
            break;
    return l;
}
static inline int32_t wt_read(const int16_t *tb, uint32_t ph, uint32_t lg)   /* linear between two samples */
{
    uint32_t i = ph >> (32u - lg), m = (1u << lg) - 1u;
    int32_t f = (int32_t)((ph << lg) >> 17), a = tb[i], b = tb[(i + 1u) & m];   /* f: Q15 */
    return a + (((b - a) * f) >> 15);
}

static void wave_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out)
        v->ph[0] = v->ph[1] = v->ph[2] = 0;         /* a fresh note: from phase 0 */
    v->s[0] = v->s[1] = 0;                          /* filter */
}

static void wave_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wa = (uint32_t)clamp(p[P_E0], 0, WT_ALL - 1), wb = (uint32_t)clamp(p[P_E1], 0, WT_ALL - 1), i;
    int32_t mo = clamp(p[P_E2] * 258 + ((m->shape - (64 << 8)) << 1), 0, 32767), mo1 = 32767 - mo;   /* MORPH, Q15 */
    int32_t sub = p[P_E3] * 258, cut = (p[P_E4] << 8) + m->cutoff, drv = p[P_E6];
    int32_t drive = 32768 + drv * 768, dw = drv * 258;
    int32_t spr = p[P_E7] * 16 / 100, rem = p[P_E7] * 16 - spr * 100;   /* SPRD in cents: 1/16 st + fine */
    uint32_t inc = m->inc, inc2 = spr || rem ? pitch_inc(clamp(m->pitch16 + spr, 0, 2047)) : inc, lv, lg;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    const int16_t *ta, *tb;
    tsvf_t flt;
    if (spr || rem)
        inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000 + m->fine));
    lv = wt_level(inc > inc2 ? inc : inc2);
    lg = WT_LEN_LOG2[lv];
    ta = wt_table(wa, lv);
    tb = wt_table(wb, lv);
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t s = mo1 ? mulq15(wt_read(ta, ph0, lg), mo1) : 0;
        if (mo)
            s += mulq15(wt_read(tb, ph1, lg), mo);
        if (sub)
            s += mulq15(osc_sine(ph2), sub) >> 1;
        ph0 += inc;
        ph1 += inc2;
        ph2 += inc >> 1;
        if (drv)
            s += mulq15(softclip(((s >> 2) * (drive >> 2)) >> 11) - s, dw);
        {   /* the low-pass, as ANALOG's: linear up to half scale, a soft knee above */
            int32_t y = tsvf_lp(&flt, s >> 1, &ic1, &ic2), a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y << 1;
        }
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
}


/* {WAVA, WAVB, MRPH, SUB, CUT, RES, DRV, SPRD}; waves: 0 SINE 1 TRI 2 SAW 3 SQUARE 4 PULSE25 5 PULSE12 6 ORGAN
 * 7 ORGAN2 8 VOX A 9 VOX O 10 BELL 11 BRASS 12 REED 13 HOLLOW 14 FM 1 15 FM 2 16 SYNC 17 SYNC 2 18 DIGI 19 GLASS
 * 20 CLAVI 21 CHOIR 22 METAL 23 SOFTSAW */
static const preset_t WAVE_PRESETS[] = {
    {"VOX PAD", {8, 9, 40, 0, 96, 10, 0, 12}, {70, 90, 118, 80}, 0, 0, FX(0, 50, 20, 60), XP(P_LRATE + 1, 20, P_LD_SHP + 1, 30)},
    {"CHOIR WAVE", {21, 8, 30, 0, 100, 0, 0, 18}, {80, 90, 120, 90}, 0, 0, FX(0, 60, 20, 70)},
    {"GLASS KEYS", {19, 10, 50, 0, 110, 10, 0, 6}, {0, 80, 40, 60}, 0, 0, FX(0, 20, 30, 45)},
    {"DIGI LEAD", {18, 16, 30, 20, 100, 20, 30, 0}, {2, 70, 110, 30}, 0, 1, FX(0, 0, 26, 20), XP(P_GLIDE + 1, 30)},
    {"SYNC LEAD W", {16, 17, 0, 0, 96, 30, 20, 0}, {2, 70, 110, 30}, 20, 1, FX(0, 0, 24, 20), XP(P_LRATE + 1, 40, P_LD_SHP + 1, 40)},
    {"FM BELL", {15, 10, 50, 0, 120, 0, 0, 4}, {0, 100, 0, 80}, 0, 0, FX(0, 10, 30, 50)},
    {"HOLLOW PAD", {13, 6, 60, 0, 80, 10, 0, 20}, {60, 90, 118, 80}, 0, 0, FX(0, 50, 20, 60), XP(P_LRATE + 1, 14, P_LD_SHP + 1, 24)},
    {"REED LEAD", {12, 11, 30, 0, 90, 20, 20, 4}, {6, 70, 110, 40}, 0, 1, FX(0, 10, 20, 30), XP(P_LD_PIT + 1, 1, P_LRATE + 1, 80, P_LFADE + 1, 60)},
    {"CLAVI PLUCK", {20, 4, 20, 0, 90, 20, 10, 0}, {0, 60, 0, 30}, 30, 0, FX(0, 10, 20, 20)},
    {"METAL PLUCK", {22, 10, 40, 0, 110, 10, 0, 8}, {0, 70, 0, 50}, 0, 0, FX(0, 10, 30, 40)},
    {"WAVE BASS", {23, 2, 30, 60, 60, 30, 30, 0}, {0, 60, 90, 20}, 30, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"ORGAN WAVE", {6, 7, 50, 0, 120, 0, 10, 2}, {0, 127, 127, 20}, 0, 0, FX(0, 40, 0, 30)},
};

static const engine_t ENG_WAVE = {
    "WAVE", {"WAVES", "TONE"},
    {
        {"WAVA", F_ENUM, 0, WT_ALL - 1, 2, (const char *const *)wt_names_all, 0},
        {"WAVB", F_ENUM, 0, WT_ALL - 1, 3, (const char *const *)wt_names_all, 0},
        {"MRPH", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
        {"RES", F_PCT, 0, 127, 10, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"SPRD", F_INT, 0, 50, 0, 0, "ct"},
    },
    WAVE_PRESETS, NELEM(WAVE_PRESETS), 1, wave_note_on, wave_render,
    0x5DFB, {P_E0, P_E2, P_E4, P_E5}, 0,
};
