/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The drum tracks (NoteSorcery: tracks 7 and 8, NDRUMTRK of them, each a drum machine of its own): a kit
 * each (the GM sample kit, the synthesised kits, your kits), played by its step pattern, the keys when it
 * is selected, and its own MIDI channel (GLO -> DRUMS CH, default 10: drum track 1 on it, drum track 2 on
 * the next one). Its own voices (outside the parts' voice budget). One-shots: note-offs are ignored; a
 * closed or pedal hi-hat chokes the open one. The drum bus: GLO > DRUMS (G_DRLVL, G_DRREV, G_DRDLY) for
 * both; each track's LEVEL (P_LEVEL, 104 = the bus level), PAN and MUTE its own. Rendered from the audio
 * ISR. */
#define NDRUM 6
#include "drum_synth.c"       /* synthesised kits (DS_KITS) */
#include "x0x_drums.c"          /* the 808 CM and 909 CM kits: X0X's circuit models */
/* P_E0 was unused on the drum track: it holds the kit. 0..4: the GM sample kit and its four
 * treatments (as before: old projects keep their kit), 5..: the synthesised kits, then (2.4) USR1..USR4:
 * a user sample slot as a kit (the web editor's DRUM KIT: a sound per lane, zone lo..hi = the lane's note),
 * and USR3+4: one kit over the slots USR3 and USR4 (a lane's sound in either) */
#define DRUM_SAMPLED 5u
#define DRUM_USR (DRUM_SAMPLED + DS_NKITS)
#define DRUM_PAIR (DRUM_USR + SMP_USER_SLOTS)   /* USR3+4: one kit in two slots (the editor splits it), ~15 s */
#define DRUM_SYN (DRUM_PAIR + 1u)                /* SLOOP 2.5: SYN1..SYN4, your synthesised kits (drum_synth.c dsu) */
#define DRUM_CM808 (DRUM_SYN + DSU_N)             /* NoteSorcery: X0X's circuit-modelled TR-808 (x0x_drums.c) */
#define DRUM_CM909 (DRUM_CM808 + 1u)              /* ... and TR-909 (its hats and cymbals: the synthesised 909's) */
#define DRUM_KITS (DRUM_CM909 + 1u)
static const char *const DRUM_KIT_NAMES[] = {"ACOUSTIC", "DEEP", "TIGHT", "BRIGHT", "DUST", DS_KIT_NAME_LIST,
                                             "USR1", "USR2", "USR3", "USR4", "USR3+4",
                                             "SYN1", "SYN2", "SYN3", "SYN4", "808 CM", "909 CM"};
static const char *const DRUM_KIT_STYLES[] = {"STUDIO", "SOFT", "PUNCHY", "BRIGHT", "DUSTY", DS_KIT_STYLE_LIST,
                                              "YOUR KIT", "YOUR KIT", "YOUR KIT", "YOUR KIT", "BIG KIT",
                                              "YOUR SYNTH", "YOUR SYNTH", "YOUR SYNTH", "YOUR SYNTH",
                                              "CIRCUIT", "CIRCUIT"};
_Static_assert(sizeof(DRUM_KIT_NAMES) / sizeof(DRUM_KIT_NAMES[0]) == DRUM_KITS, "a name per kit");
static uint32_t drum_kit_of(const track_t *t) { return (uint32_t)clamp(t->p[P_E0], 0, DRUM_KITS - 1); }
static uint32_t drum_kit(void) { return drum_kit_of(TDRUM); }   /* the drum track the UI shows */
#define DRUM_DEFAULT_KIT DRUM_SAMPLED   /* (the synthesised 808) */
/* NoteSorcery power-on: drum track 1 the circuit 808, drum track 2 the Machinedrum-style EFM kit */
#define DRUM_DEFAULT_KIT_OF(d) ((d) ? DRUM_SAMPLED + DS_KIT_MD_EFM : DRUM_CM808)

typedef struct {
    voice_t v[NDRUM];
    uint32_t age;
    int32_t tail;                /* declick: the last output of cut voices, decaying */
    int32_t peak;                /* largest |output| since the UI last looked (TRACKS meter) */
    uint8_t kit[NDRUM];
    int32_t filter[NDRUM], env[NDRUM];
    uint8_t synth[NDRUM];        /* the voice plays a synthesised kit (ds[]) */
    dsv_t ds[NDRUM];
    volatile uint16_t hits;      /* bit per lane hit since the UI last looked (pads, key LEDs) */
    int32_t a0, a1;              /* the drum track's mute / solo attenuation over this block (fx.c), Q15, 0 = heard */
} drums_t;
static drums_t drumst[NDRUMTRK];
static volatile uint8_t drum_kick;              /* a kick was hit on a drum track (fx.c DUCK) */
static int16_t drum_gm_set = -2;                 /* SMP_SETS index of "PERC" (GM map), -1 = none */
static drums_t *drums_of(const track_t *t) { return &drumst[(uint32_t)(t - trk - TRK_DRUM) % NDRUMTRK]; }
#define drums (*drums_of(TDRUM))                 /* (the UI: the drum track it shows) */

/* ---- NoteSorcery: the circuit-model kits. One slot (in the pool) holds an 808 or a 909; one drum track at a time
 * owns it, and keeps it while its kit is 808 CM or 909 CM. The other drum track asking for a circuit kit then
 * plays the synthesised kit of the same machine (DS_KITS "808" / "909"). Float: run only in the audio ISR. */
enum { CM_NONE, CM_808, CM_909 };
static union {
    drum808_t d8;
    drum909_t d9;
} cm_slot __attribute__((section(".pool")));
static uint8_t cm_kind;                          /* CM_*: what cm_slot holds */
static uint8_t cm_owner = 0xFF;                  /* the drum track (0 .. NDRUMTRK-1) that owns it, 0xFF = none */
static uint32_t cm_hits;                         /* hits the slot played (the host tests count them) */
static volatile uint8_t cm_busy;                 /* the slot sounds (set by the ISR: the main loop runs no float) */
/* float -> the synthesised voices' Q15-ish level, each kick about as loud as the synthesised kit's (drumcm_test) */
#define CM808_GAIN 130000.0f
#define CM909_GAIN 26000.0f
static int is_cm_kit(uint32_t kit) { return kit == DRUM_CM808 || kit == DRUM_CM909; }
static uint32_t drum_index(const track_t *t) { return (uint32_t)(t - trk - TRK_DRUM) % NDRUMTRK; }
static int cm_active(void) { return cm_kind == CM_808 ? drum808_active(&cm_slot.d8) : cm_kind == CM_909 ? drum909_active(&cm_slot.d9) : 0; }
/* the slot for drum track d and kit: 1 = it is d's (taken now if free, or held by a track off its circuit kit) */
static int cm_take(uint32_t d, uint32_t kit)
{
    uint32_t kind = kit == DRUM_CM808 ? CM_808 : CM_909;
    if (cm_owner < NDRUMTRK && cm_owner != d && is_cm_kit(drum_kit_of(&trk[TRK_DRUM + cm_owner])))
        return 0;                                /* the other drum track plays its circuit kit */
    if (cm_owner != d || cm_kind != kind) {
        if (kind == CM_808)
            drum808_init(&cm_slot.d8);
        else
            drum909_init(&cm_slot.d9);
        cm_kind = (uint8_t)kind;
        cm_owner = (uint8_t)d;
    }
    return 1;
}
/* a lane (drum_synth.c ds_lane) -> the 808's track and its switch (-1: none), the 909's voice (-1: the synthesised
 * 909 plays it: hats, cymbals, shaker, cowbell, clave) */
static const int8_t CM8_TRK[DS_LANES][2] = {
    {D8_BD, -1}, {D8_SD, -1}, {D8_CP, 0}, {D8_CH, -1}, {D8_OH, -1}, {D8_LT, 0}, {D8_HT, 0}, {D8_CY, -1},
    {D8_CY, -1}, {D8_CP, 1}, {D8_MT, 1}, {D8_RS, 0}, {D8_CB, -1}, {D8_RS, 1}, {D8_BD, -1}, {D8_SD, -1}};
static const int8_t CM9_VOICE[DS_LANES] = {DR_BD, DR_SD, DR_CP, -1, -1, DR_LT, DR_HT, -1,
                                           -1, -1, DR_MT, DR_RS, -1, -1, DR_BD, DR_SD};
/* a step's velocity -> the machines' trigger: 127 (an ACCENT step; >= 120 from MIDI) the accented hit, 100 the
 * plain one */
static float cm_vel(uint32_t vel)
{
    if (vel >= 120u)
        return 1.0f;
    return (float)vel * (D8_VEL_NORMAL / 100.0f);
}
/* a hit on the circuit kit; 0: not played here (the caller plays the synthesised kit) */
static int cm_on(track_t *t, uint32_t note, uint32_t kit, uint32_t vel)
{
    int32_t semi;
    uint32_t lane = ds_lane(note, &semi);
    if (!cm_take(drum_index(t), kit))
        return 0;
    if (cm_kind == CM_808) {
        int tr = CM8_TRK[lane][0], sw = CM8_TRK[lane][1];
        if (sw >= 0)
            drum808_set(&cm_slot.d8, tr, tr == D8_CP ? 4 : 3, sw);   /* (the "Sound" switch: tom / conga ...) */
        drum808_trigger(&cm_slot.d8, tr, cm_vel(vel));
    } else {
        if (CM9_VOICE[lane] < 0)
            return 0;
        drum909_trigger(&cm_slot.d9, CM9_VOICE[lane], cm_vel(vel));
    }
    cm_hits++;
    return 1;
}

static int32_t ds_buf[CTL];

static int32_t drum_set(void)
{
    uint32_t i;
    if (drum_gm_set == -2) {
        drum_gm_set = -1;
        for (i = 0; i < SMP_NSETS; i++)
            if (str_eq(SMP_SETS[i].name, "PERC"))
                drum_gm_set = (int16_t)i;
    }
    return drum_gm_set;
}

/* ------------------------------------------------------------ lanes --- */
/* The drum track's 16 sounds, one per white key, F3 (kick) .. G5 (cowbell): kicks, snare and clap,
 * the hi-hats, rim and a second snare, the toms, the cymbals, the percussion. Each plays a GM note
 * (the sampled kits: their samples; the synthesised kits: drum_synth.c DS_MAP). A black key plays
 * the lane of the white key left of it (two fingers on one sound). */
static const uint8_t LANE_NOTE[DRUM_LANES] = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
static const char *const LANE_NAME[DRUM_LANES] = {
    "KICK", "KICK 2", "SNARE", "CLAP", "HAT", "OPEN HAT", "PEDAL", "RIM",
    "SNARE 2", "LOW TOM", "HI TOM", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"};
static const char *const LANE_SHORT[DRUM_LANES] = {           /* 5 characters: tiles, dials */
    "kick", "kick2", "snare", "clap", "hat", "open", "pedal", "rim",
    "snr 2", "tom l", "tom h", "crash", "ride", "shake", "conga", "bell"};
/* a GM note (MIDI in, old projects) -> its lane: the nearest sound of the 16 (35..81; below: kick, above: shaker) */
static const uint8_t LANE_OF_GM[81 - 35 + 1] = {
    /* 35 */ 1, 0, 7, 2, 3, 8, 9, 4, 9, 6,
    /* 45 */ 9, 5, 10, 10, 11, 10, 12, 11, 12, 13,
    /* 55 */ 11, 15, 11, 13, 12, 14, 14, 14, 14, 14,
    /* 65 */ 14, 14, 15, 15, 13, 13, 15, 15, 13, 13,
    /* 75 */ 7, 7, 7, 14, 14, 15, 15};
static uint32_t lane_of_note(uint32_t note)
{
    return note < 35u ? 0u : note > 81u ? 13u : LANE_OF_GM[note - 35u];
}
/* the lane of key k (0 = F3 .. 26 = G5): white keys in order, a black key the white key left of it */
static uint32_t lane_of_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    if (i < 0)
        i = W[(k - 1u) % 12u];
    return (uint32_t)((int32_t)(k / 12u) * 7 + i) & 15u;
}
/* the white key of lane l (key index), for the key LEDs */
static uint32_t key_of_lane(uint32_t l)
{
    static const uint8_t K[7] = {0, 2, 4, 6, 7, 9, 11};      /* F G A B C D E */
    return (l / 7u) * 12u + K[l % 7u];
}

/* drum steps: a lane's bit, level, ratchet */
static int dstep_has(const dstep_t *s, uint32_t l) { return (s->on[(l >> 3) & 1u] >> (l & 7u)) & 1u; }
static uint32_t dstep_lvl(const dstep_t *s, uint32_t l) { return (s->lvl[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_rat(const dstep_t *s, uint32_t l) { return (s->rat[(l >> 2) & 3u] >> ((l & 3u) * 2u)) & 3u; }
static uint32_t dstep_mask(const dstep_t *s) { return (uint32_t)s->on[0] | (uint32_t)s->on[1] << 8; }
static void dstep_set(dstep_t *s, uint32_t l, uint32_t lvl, uint32_t rat)   /* lane on, with its level and ratchet */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] |= (uint8_t)(1u << (l & 7u));
    s->lvl[b] = (uint8_t)((s->lvl[b] & ~(3u << sh)) | (lvl & 3u) << sh);
    s->rat[b] = (uint8_t)((s->rat[b] & ~(3u << sh)) | (rat & 3u) << sh);
}
static void dstep_clr(dstep_t *s, uint32_t l)                                /* lane off */
{
    uint32_t sh = (l & 3u) * 2u, b = (l >> 2) & 3u;
    s->on[(l >> 3) & 1u] &= (uint8_t)~(1u << (l & 7u));
    s->lvl[b] &= (uint8_t)~(3u << sh);
    s->rat[b] &= (uint8_t)~(3u << sh);
}

/* the zone a sampled drum voice plays (drum_on: v->s[4]): SMP_ZONES[i], or a user kit's, DZ_USR + slot * 16 + zone */
#define DZ_USR (1 << 16)                 /* (above every SMP_ZONES index) */
static const smp_zone_t *drum_zone_of(const voice_t *v)
{
    int32_t z = v->s[4];
    return z >= DZ_USR ? &usr_zone[((uint32_t)(z - DZ_USR) >> 4) % SMP_USER_SLOTS][(uint32_t)z & 15u] : &SMP_ZONES[(uint32_t)z];
}
/* a user kit (KIT USR1..3): the zone of slot k for the lane note ln (the lane's GM note), -1 = none */
static int32_t drum_usr_zone(uint32_t k, uint32_t ln)
{
    uint32_t j;
    for (j = 0; j < usr_nz[k] && j < 16u; j++)
        if (ln >= usr_zone[k][j].lo && ln <= usr_zone[k][j].hi)
            return DZ_USR + (int32_t)(k * 16u + j);
    return -1;
}

/* the velocity of a level: as played (LV_NORM: vel), ghost, soft, hard */
static uint32_t lvl_vel(uint32_t lvl, uint32_t vel)
{
    static const uint8_t V[4] = {0, 42, 72, 127};
    return lvl & 3u ? V[lvl & 3u] : vel;
}
/* a MIDI velocity -> the level it records as (keys play 100: LV_NORM) */
static uint32_t vel_lvl(uint32_t vel)
{
    return vel < 56u ? LV_GHOST : vel < 88u ? LV_SOFT : vel < 116u ? LV_NORM : LV_HARD;
}

/* the metronome (GLO > GLOBAL > CLICK, and the REC count-in), 2.4.1: a voice of its own, mixed after the drum
 * track (fx.c mix_block), so it is heard with every kit (a user kit has no wood block: it was silent), whatever
 * the drum track's mute, a solo, its filter or slicer. A short sine tick: 1568 Hz on a bar's first beat, 1047 Hz
 * on the others, about 12 ms to fade by 1/e; its level follows GLO > DRUMS > LVL (100: as loud as the old
 * wood block). It costs a few hundred cycles a block while it sounds, nothing after */
static struct { uint32_t ph, inc; int32_t env, gain; } click;   /* env: Q30, 0 = silent */
static uint32_t click_n;                         /* clicks so far, and the last one's accent (the host tests count them) */
static uint8_t click_acc;
static void click_on(int accent)
{
    click.ph = 0;
    click.inc = accent ? (uint32_t)(((uint64_t)1568u << 32) / FS) : (uint32_t)(((uint64_t)1047u << 32) / FS);
    click.gain = accent ? 20000 : 12000;
    click.env = 1 << 30;
    click_acc = (uint8_t)(accent != 0);
    click_n++;
}
static void click_render(int32_t *ml, int32_t *mr, uint32_t n)
{
    uint32_t i;
    int32_t g;
    if (!click.env)
        return;
    g = (int32_t)((int64_t)click.gain * song.g[G_DRLVL] / 100);
    for (i = 0; i < n; i++) {
        int32_t v = (int32_t)(((int64_t)sine_i(click.ph) * (click.env >> 15) >> 15) * g >> 15);
        ml[i] += v;
        mr[i] += v;
        click.ph += click.inc;
        click.env -= click.env >> 9;                 /* about 12 ms to 1/e */
    }
    if (click.env < (1 << 18))
        click.env = 0;                                /* (-72 dB: done, about 100 ms) */
}

/* a synthesised voice: note on the 16 sounds snd (a factory kit, SYN1..4), the oldest voice stolen */
static void __attribute__((noinline)) drum_synth_on(drums_t *D, uint32_t note, uint32_t vel, uint32_t kit, const dsnd_t *snd,
                                                    uint32_t crush)
{
    voice_t *v = &D->v[0];
    uint32_t i;
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (D->v[i].active && D->v[i].note == 46u) {
                D->v[i].active = 0;
                D->tail += D->v[i].s[7];
            }
    for (i = 0; i < NDRUM; i++) {
        if (!D->v[i].active) {
            v = &D->v[i];
            break;
        }
        if (D->v[i].age < v->age)
            v = &D->v[i];
    }
    if (v->active)
        D->tail += v->s[7];
    i = (uint32_t)(v - D->v);
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->s[7] = 0;
    v->age = ++D->age;
    D->synth[i] = 1;
    D->kit[i] = (uint8_t)kit;
    ds_on(&D->ds[i], snd, crush, note, vel);
}
/* the editor's audition (editor_dsyn.c DSYN_PLAY): queued, played by the audio ISR (drum_audition_poll) */
static volatile uint8_t dsu_aud_k, dsu_aud_lane, dsu_aud_vel;
static void drum_audition_poll(void)
{
    uint32_t vel = dsu_aud_vel;
    if (!vel)
        return;
    dsu_aud_vel = 0;
    {
        const dsu_kit_t *u = dsu_kit(dsu_aud_k);
        drum_synth_on(&drums, DS_LANE_NOTE[dsu_aud_lane % DS_LANES], vel, DRUM_SYN + dsu_aud_k % DSU_N, u->s, u->crush);
    }
}

static void drum_on(track_t *t, uint32_t note, uint32_t vel)
{
    drums_t *D = drums_of(t);
    int32_t si = drum_set();
    const smp_set_t *set;
    voice_t *v = &D->v[0];
    uint32_t i, zi = 0xFFFFu, kit = drum_kit_of(t);
    int32_t zid = -1;
    if (note != 76u && note != 77u)                 /* the pads and key LEDs (not the click's wood block) */
        D->hits |= (uint16_t)(1u << lane_of_note(note));
    if (note == 35u || note == 36u)
        drum_kick = 1;                             /* (DUCK) */
    if (is_cm_kit(kit)) {                           /* 808 CM / 909 CM: the circuit, else the synthesised machine */
        if (!cm_on(t, note, kit, vel))
        {
            const dkit_t *k = &DS_KITS[kit == DRUM_CM808 ? DS_KIT_808 : DS_KIT_909];
            drum_synth_on(D, note, vel, kit, k->s, k->crush);
        }
        return;
    }
    if (kit >= DRUM_SYN) {                          /* SYN1..SYN4: your synthesised kit */
        const dsu_kit_t *u = dsu_kit(kit - DRUM_SYN);
        drum_synth_on(D, note, vel, kit, u->s, u->crush);
        return;
    }
    if (kit >= DRUM_USR) {                          /* a user kit: the lane's sound, as its note (the hat choke) */
        if (note == 76u || note == 77u)
            return;                                 /* (the click's wood block: not a lane) */
        note = LANE_NOTE[lane_of_note(note)];
        if (kit == DRUM_PAIR) {                     /* USR3+4: the lane's sound in USR3, else in USR4 */
            zid = drum_usr_zone(2, note);
            if (zid < 0)
                zid = drum_usr_zone(3, note);
        } else {
            zid = drum_usr_zone(kit - DRUM_USR, note);
        }
        if (zid < 0)
            return;                                 /* (an empty slot, or no sound on this lane) */
    } else if (kit >= DRUM_SAMPLED) {               /* synthesised kit */
        drum_synth_on(D, note, vel, kit, DS_KITS[kit - DRUM_SAMPLED].s, DS_KITS[kit - DRUM_SAMPLED].crush);
        return;
    }
    if (zid < 0) {                                  /* the GM sample kit */
        if (si < 0)
            return;
        set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (note >= SMP_ZONES[set->z0 + i].lo && note <= SMP_ZONES[set->z0 + i].hi)
                zi = set->z0 + i;
        if (zi == 0xFFFFu)
            return;
        zid = (int32_t)zi;
    }
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (D->v[i].active && D->v[i].note == 46u) {
                D->v[i].active = 0;
                D->tail += D->v[i].s[7];          /* fade what it was playing, not a step */
            }
    for (i = 0; i < NDRUM; i++) {                   /* free voice, else the oldest */
        if (!D->v[i].active) {
            v = &D->v[i];
            break;
        }
        if (D->v[i].age < v->age)
            v = &D->v[i];
    }
    if (v->active)
        D->tail += v->s[7];                      /* stolen voice: fade its last value */
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->s[7] = 0;
    v->age = ++D->age;
    v->s[4] = zid;
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = v->s[1] = v->s[2] = 0;
    {
        const smp_zone_t *z = drum_zone_of(v);
        v->s[3] = sample_next(z, v, 0);
        v->s[5] = (int32_t)((pow2_q16((int32_t)note * 16 - z->root16) >> 8) * (z->rate >> 8));
    }
    {
        uint32_t vi = (uint32_t)(v - D->v);
        int32_t shift = kit == 1u ? (note <= 36u ? -5 : -2) : kit == 3u ? 2 : kit == 4u ? -1 : 0;
        D->kit[vi] = (uint8_t)kit;
        D->synth[vi] = 0;
        D->filter[vi] = 0;
        D->env[vi] = 32767;
        if (shift) v->s[5] = (int32_t)((pow2_q16((int32_t)note * 16 + shift * 16 - SMP_ZONES[zi].root16) >> 8) * (SMP_ZONES[zi].rate >> 8));
    }
}

/* adds the drums into the dry mix and the reverb and delay sends; mono != 0: into mono instead, before
 * the pan and the sends (the SLICER, slicer.c slicer_drums, does those after it) */
static inline void drums_mix(track_t *t, int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, int32_t *mono, uint32_t n)
{
    drums_t *D = drums_of(t);
    uint32_t k, i;
    int32_t lvl = (int32_t)((int64_t)song.g[G_DRLVL] * 200 * LEVEL_Q12[t->p[P_LEVEL] & 127] / 2584), send = song.g[G_DRREV] * 258, dsend = song.g[G_DRDLY] * 258, pk = D->peak;
    int32_t pan = t->p[P_PAN], gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
    for (i = 0; i < n && D->tail; i++) {         /* declick tail, ~0.4 ms */
        if (mono) {
            mono[i] += D->tail;
        } else {
            ml[i] += D->tail;
            mr[i] += D->tail;
        }
        D->tail -= D->tail / 16 + (D->tail > 0 ? 1 : D->tail < 0 ? -1 : 0);
    }
    if (cm_owner == drum_index(t)) {            /* the circuit kit, this track's */
        static float cf[CTL], cr[CTL], cd[CTL];
        uint32_t m = n < CTL ? n : CTL;
        cm_busy = (uint8_t)cm_active();
        if (!is_cm_kit(drum_kit_of(t)) && !cm_busy) {
            cm_owner = 0xFF;                       /* off its circuit kit and silent: the slot is free */
        } else {
            for (i = 0; i < m; i++)
                cf[i] = cr[i] = cd[i] = 0.0f;
            if (cm_kind == CM_808)
                drum808_render(&cm_slot.d8, cf, cr, cd, (int)m);
            else
                drum909_render(&cm_slot.d9, cf, cr, cd, (int)m);
            for (i = 0; i < m; i++) {
                float f = cf[i] * (cm_kind == CM_808 ? CM808_GAIN : CM909_GAIN);
                int32_t s = f > 65535.0f ? 65535 : f < -65535.0f ? -65535 : (int32_t)f;   /* (+6 dB over full) */
                s = mulq15(s, mulq15(lvl, 32767 - D->a0 - (((D->a1 - D->a0) * (int32_t)i) >> CTL_LOG2)));
                if (s > pk || -s > pk)
                    pk = s < 0 ? -s : s;
                if (mono) {
                    mono[i] += s;
                    continue;
                }
                ml[i] += (s * gl) >> 12;
                mr[i] += (s * gr) >> 12;
                if (send)
                    rev[i] += mulq15(s, send);
                if (dsend)
                    dly[i] += mulq15(s, dsend);
            }
        }
    }
    for (k = 0; k < NDRUM; k++) {               /* synthesised voices: render, then as below */
        voice_t *v = &D->v[k];
        uint32_t m = n < CTL ? n : CTL;             /* (the mix runs in blocks of CTL) */
        if (!v->active || !D->synth[k])
            continue;
        if (!ds_render(&D->ds[k], ds_buf, m))
            v->active = 0;
        for (i = 0; i < m; i++) {
            int32_t s = mulq15(ds_buf[i], mulq15(lvl, 32767 - D->a0 - (((D->a1 - D->a0) * (int32_t)i) >> CTL_LOG2)));
            v->s[7] = s;
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                continue;
            }
            ml[i] += (s * gl) >> 12;
            mr[i] += (s * gr) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
            if (dsend)
                dly[i] += mulq15(s, dsend);
        }
        if (!v->active) {
            D->tail += v->s[7];                  /* ended: no step at the end */
            v->s[7] = 0;
        }
    }
    for (k = 0; k < NDRUM; k++) {
        voice_t *v = &D->v[k];
        const smp_zone_t *z;
        if (D->synth[k])
            continue;
        z = drum_zone_of(v);
        uint32_t frac = v->ph[1], stepq = (uint32_t)v->s[5];   /* Q16 source samples per output (drum_on) */
        int32_t g;
        if (!v->active)
            continue;
        g = mulq15(lvl, v->vel * 258);
        g += g * 3 >> 2;                           /* x1.75 (+5 dB): as loud as the synthesised kits */
        for (i = 0; i < n; i++) {
            int32_t s;
            frac += stepq;
            while (frac >= 65536u) {
                frac -= 65536u;
                v->s[2] = v->s[3];
                if (v->ph[0] >= z->n) {
                    v->active = 0;
                    break;
                }
                v->s[3] = sample_next(z, v, 0);
            }
            if (!v->active)
                break;
            s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
            s = mulq15(s, mulq15(g, 32767 - D->a0 - (((D->a1 - D->a0) * (int32_t)i) >> CTL_LOG2)));
            if (D->kit[k] == 1u || D->kit[k] == 4u) {
                D->filter[k] += (s - D->filter[k]) >> (D->kit[k] == 1u ? 2 : 1);
                s = D->filter[k];
                if (D->kit[k] == 4u) s = (s >> 8) * 256;
            } else if (D->kit[k] == 2u) {
                s = mulq15(s, D->env[k]);
                D->env[k] -= (D->env[k] >> 11) + 1;
                if (D->env[k] <= 0) v->active = 0;
            }
            v->s[7] = s;
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                continue;
            }
            ml[i] += (s * gl) >> 12;
            mr[i] += (s * gr) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
            if (dsend)
                dly[i] += mulq15(s, dsend);
        }
        v->ph[1] = frac;
    }
    D->peak = pk;
}
static void drums_render(track_t *t, int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, uint32_t n) { drums_mix(t, ml, mr, rev, dly, 0, n); }
static void drums_render_mono(track_t *t, int32_t *mono, uint32_t n) { drums_mix(t, 0, 0, 0, 0, mono, n); }
