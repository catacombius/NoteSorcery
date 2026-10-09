/* SPDX-License-Identifier: GPL-3.0-only */
/* The circuit-model kits (drums.c 808 CM / 909 CM: X0X's 8W8 and 9W9 in one shared slot) on the host, through
 * hostsim.c: every lane of each is heard, bounded and ends; the circuit kick is about as loud as the synthesised
 * kick of the same machine; the 808's tom / conga, rim / clave and clap / maracas switches follow the lane; the
 * 909 CM plays its hats and cymbals on the synthesised 909; one drum track owns the slot and the other gets the
 * synthesised machine; a track off its circuit kit frees the slot once silent; a muted track is silent; the
 * cost of a busy circuit kit. Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(int ok, const char *what)
{
    printf("drumcm: %-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static const uint8_t DS_NOTE[DS_LANES] = {36, 38, 39, 42, 46, 43, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};

static int32_t render(track_t *t, uint32_t blocks)    /* blocks of one drum track: its largest |sample| */
{
    int32_t l[CTL], r[CTL], rv[CTL], dl[CTL], pk = 0;
    uint32_t b, k;
    while (blocks--) {
        memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv), memset(dl, 0, sizeof dl);
        drums_render(t, l, r, rv, dl, CTL);
        for (k = 0; k < CTL; k++) {
            int32_t a = l[k] < 0 ? -l[k] : l[k];
            if (a > pk) pk = a;
        }
    }
    (void)b;
    return pk;
}
static int synth_busy(const track_t *t)
{
    const drums_t *D = drums_of(t);
    uint32_t k;
    for (k = 0; k < NDRUM; k++)
        if (D->v[k].active && D->synth[k])
            return 1;
    return 0;
}
static void reset(void)
{
    host_tracks_init();
    memset(drumst, 0, sizeof drumst);
    cm_owner = 0xFF, cm_kind = CM_NONE;
    song.g[G_DRLVL] = 100;
}

int main(void)
{
    track_t *d1 = &trk[TRK_DRUM], *d2 = &trk[TRK_DRUM + 1];
    uint32_t kit, lane;
    check(!strcmp(DRUM_KIT_NAMES[DRUM_CM808], "808 CM") && !strcmp(DRUM_KIT_NAMES[DRUM_CM909], "909 CM") &&
          DRUM_CM909 == DRUM_KITS - 1u, "the kits: 808 CM and 909 CM, last in the list");

    for (kit = DRUM_CM808; kit <= DRUM_CM909; kit++) {   /* every lane: heard, bounded, ends */
        int ok = 1;
        char what[100];
        int32_t lo = 1 << 30, hi = 0;
        for (lane = 0; lane < DS_LANES; lane++) {
            int32_t pk;
            uint32_t b;
            reset();
            d1->p[P_E0] = (int16_t)kit;
            drum_on(d1, DS_NOTE[lane], 100);
            pk = render(d1, 4);
            for (b = 0; b < 6u * FS / CTL && (cm_busy || synth_busy(d1)); b++) {
                int32_t p = render(d1, 1);
                if (p > pk) pk = p;
            }
            ok &= pk > 800 && pk < 131072 && b < 6u * FS / CTL;
            if (pk < lo) lo = pk;
            if (pk > hi) hi = pk;
            if (!(pk > 800 && pk < 131072 && b < 6u * FS / CTL))
                printf("drumcm: %s lane %u: peak %d, %u blocks\n", DRUM_KIT_NAMES[kit], lane, pk, b);
        }
        snprintf(what, sizeof what, "%s: all 16 lanes heard, bounded, end (peaks %d .. %d)", DRUM_KIT_NAMES[kit], lo, hi);
        check(ok, what);
    }

    for (kit = 0; kit < 2u; kit++) {                     /* the circuit kick against the synthesised one */
        int32_t cm, sy;
        char what[100];
        reset();
        d1->p[P_E0] = (int16_t)(kit ? DRUM_CM909 : DRUM_CM808);
        drum_on(d1, 36, 100);
        cm = render(d1, FS / 2u / CTL);
        reset();
        d1->p[P_E0] = (int16_t)(DRUM_SAMPLED + kit);     /* (DS_KITS 0, 1: "808", "909") */
        drum_on(d1, 36, 100);
        sy = render(d1, FS / 2u / CTL);
        snprintf(what, sizeof what, "%s kick peak %d, the synthesised %s %d: within 6 dB", kit ? "909 CM" : "808 CM", cm,
                 DRUM_KIT_NAMES[DRUM_SAMPLED + kit], sy);
        check(cm * 2 > sy && cm < sy * 2, what);
    }

    {   /* the 808's switches follow the lane */
        reset();
        d1->p[P_E0] = DRUM_CM808;
        drum_on(d1, 63, 100);                             /* CONGA: MT as a conga */
        drum_on(d1, 43, 100);                             /* TOM LO: LT as a tom */
        drum_on(d1, 75, 100);                             /* CLAVE: RS as claves */
        drum_on(d1, 70, 100);                             /* SHAKER: CP as maracas */
        check(cm_slot.d8.sw[D8_MT] == 1 && cm_slot.d8.sw[D8_LT] == 0 && cm_slot.d8.sw[D8_RS] == 1 && cm_slot.d8.sw[D8_CP] == 1,
              "808 CM: conga, tom, clave, maracas lanes set the 808's switches");
        drum_on(d1, 37, 100);                             /* RIM */
        drum_on(d1, 39, 100);                             /* CLAP */
        check(cm_slot.d8.sw[D8_RS] == 0 && cm_slot.d8.sw[D8_CP] == 0, "... and back: rim, clap");
        render(d1, FS / CTL);
    }

    {   /* 909 CM: the circuit for BD SD toms RS CP, the synthesised 909 for the hats */
        uint32_t h0;
        reset();
        d1->p[P_E0] = DRUM_CM909;
        h0 = cm_hits;
        drum_on(d1, 36, 100);
        check(cm_hits == h0 + 1u && cm_kind == CM_909 && !synth_busy(d1), "909 CM: the kick on the circuit");
        drum_on(d1, 42, 100);
        check(cm_hits == h0 + 1u && synth_busy(d1), "909 CM: the closed hat on the synthesised 909");
        render(d1, FS / CTL);
    }

    {   /* one slot: drum track 1 owns it, drum track 2 gets the synthesised machine; freed when 1 leaves */
        uint32_t b;
        reset();
        d1->p[P_E0] = DRUM_CM808;
        d2->p[P_E0] = DRUM_CM909;
        drum_on(d1, 36, 100);
        drum_on(d2, 36, 100);
        check(cm_owner == 0u && cm_kind == CM_808 && synth_busy(d2) && !synth_busy(d1),
              "both on a circuit kit: track 7 owns the slot, track 8 plays the synthesised 909");
        d1->p[P_E0] = DRUM_SAMPLED;                       /* track 7 to the synthesised 808 */
        for (b = 0; b < 4u * FS / CTL && cm_owner == 0u; b++)
            render(d1, 1), render(d2, 1);
        check(cm_owner == 0xFFu, "track 7 off its circuit kit: the slot is free once it is silent");
        drum_on(d2, 38, 100);
        check(cm_owner == 1u && cm_kind == CM_909, "... and track 8's next hit takes it (909)");
        render(d2, FS / CTL);
        drum_on(d2, 36, 100);
        d2->p[P_E0] = DRUM_CM808;
        drum_on(d2, 36, 100);
        check(cm_owner == 1u && cm_kind == CM_808, "the owner changes machine: the slot follows (808)");
        render(d2, FS / CTL);
    }

    {   /* a muted drum track is silent (fx.c: a0 = a1 = full attenuation) */
        int32_t pk;
        reset();
        d1->p[P_E0] = DRUM_CM808;
        drumst[0].a0 = drumst[0].a1 = 32767;
        drum_on(d1, 36, 127);
        pk = render(d1, FS / 4u / CTL);
        check(pk == 0, "a muted track: silent");
    }

    {   /* cost: a busy 808 CM (16ths, every lane by turn) and 909 CM, against the synthesised kit (host, rough) */
        uint32_t k, b;
        double ns[3];
        for (k = 0; k < 3u; k++) {
            uint64_t t0, t1;
            reset();
            d1->p[P_E0] = (int16_t)(k == 0u ? DRUM_SAMPLED : k == 1u ? DRUM_CM808 : DRUM_CM909);
            t0 = now_ns();
            for (b = 0; b < 4u * FS / CTL; b++) {
                if (b % 10u == 0u) {
                    drum_on(d1, DS_NOTE[(b / 10u) % DS_LANES], 100);
                    drum_on(d1, (b / 10u) & 1u ? 42 : 36, 110);
                }
                render(d1, 1);
            }
            t1 = now_ns();
            ns[k] = (double)(t1 - t0) / (4.0 * FS);
        }
        printf("drumcm: cost (host -O2) a sample: synthesised 808 %.1f ns, 808 CM %.1f ns, 909 CM %.1f ns\n", ns[0], ns[1], ns[2]);
    }
    printf("drumcm: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
