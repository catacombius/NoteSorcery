/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project format (firmware/src/project.c, -DPROJ_HOST part). NoteSorcery's format ("NSP1":
 * eight tracks, every global, each track's sound, pattern, nudges, parameter locks and fill conditions) is the
 * only one read: a round trip as stored, damaged images refused (checksum, size, track count, magic, SLOOP's
 * FUN5), the working project captured and applied (both drum tracks, every synth track), a damaged image's
 * values clamped. Run by tests/run_tests.sh (needs build/gen). */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)       /* ui.c TRK_DEF */
{
    static const uint8_t E[NPART] = {ENGI_ANALOG, ENGI_FM6, ENGI_TRIO, ENGI_ANALOG, ENGI_FM6, ENGI_SAMPLE};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    static project_t q, q2;
    static uint8_t buf[sizeof(project_t) + 16u] __attribute__((aligned(16)));
    uint32_t t, i;
    int bad = 0, ok;

    host_tracks_init();
    bad += check("NSP1: eight tracks, two of them drum tracks; a project fits two flash sectors and the .noinit RAM twice",
                 NTRK == 8u && NPART == 6u && TRK_DRUM == 6u && sizeof(project_t) <= 2u * 4096u - 256u &&
                 2u * sizeof(project_t) <= 15400u);

    /* a round trip: stored as is (levels, ratchets, lanes on both drum tracks, nudges, locks, fill conditions) */
    proj_capture(&q);
    q.t[1].engine = ENGI_FM6;
    q.t[5].engine = ENGI_SAMPLE;
    q.t[0].step[3].lvl = 0x9C;
    q.t[0].step[3].rat = 0x27;
    dstep_set(&q.t[TRK_DRUM].dstep[5], 13, LV_GHOST, 2);
    dstep_set(&q.t[TRK_DRUM + 1].dstep[6], 2, LV_HARD, 1);
    q.t[0].micro[3] = -32;
    q.t[0].micro[4] = 31;
    q.t[TRK_DRUM + 1].micro[9] = -7;
    q.t[0].lock[0].step = 3, q.t[0].lock[0].param = P_E0, q.t[0].lock[0].val = 2;
    q.t[0].lock[1].step = 3, q.t[0].lock[1].param = P_DIST, q.t[0].lock[1].val = 100;
    q.t[5].lock[23].step = 63, q.t[5].lock[23].param = P_LEVEL, q.t[5].lock[23].val = 50;
    q.t[2].p[P_TFLT] = -30;
    q.t[0].fill[0] = 0x09;                         /* steps 1 and 2: FILL ONLY, NO FILL */
    q.t[TRK_DRUM + 1].fill[15] = 0x40;             /* step 64: FILL ONLY */
    q.sum = proj_sum(&q);
    memcpy(buf, &q, sizeof q);
    bad += check("NSP1 -> NSP1: as stored (levels, ratchets, lanes of both drum tracks, nudges, locks, conditions)",
                 proj_import(&q2, buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == ENGI_FM6 &&
                 q2.t[0].micro[3] == -32 && q2.t[0].lock[1].val == 100 && q2.t[5].lock[23].step == 63 &&
                 q2.t[0].fill[0] == 0x09 && q2.t[TRK_DRUM + 1].fill[15] == 0x40 && q2.t[2].p[P_TFLT] == -30 &&
                 dstep_has(&q2.t[TRK_DRUM + 1].dstep[6], 2));

    /* damaged / wrong size / another format */
    memcpy(buf, &q, sizeof q);
    ((project_t *)buf)->t[3].p[3]++;
    bad += check("NSP1 with a bad checksum: refused", !proj_import(&q2, buf, (int)sizeof q));
    memcpy(buf, &q, sizeof q);
    bad += check("NSP1 with a wrong length: refused", !proj_import(&q2, buf, (int)sizeof q - 2));
    ((project_t *)buf)->ntrk = 4;
    ((project_t *)buf)->sum = proj_sum((const project_t *)buf);
    bad += check("NSP1 with another track count (checksum right): refused", !proj_import(&q2, buf, (int)sizeof q));
    memcpy(buf, &q, sizeof q);
    ((project_t *)buf)->magic = 0x46554E35u;       /* SLOOP's "FUN5" */
    ((project_t *)buf)->sum = proj_sum((const project_t *)buf);
    bad += check("SLOOP's FUN5 (and older): not read", !proj_import(&q2, buf, (int)sizeof q));

    /* capture / apply: the working project round trip (every track: both drum tracks, the six synth tracks) */
    host_tracks_init();
    for (t = 0; t < NTRK; t++)
        trk[t].p[P_SLEN] = (int16_t)(5 + t);
    trk[5].p[P_LEVEL] = 77;
    trk[TRK_DRUM + 1].p[P_LEVEL] = 66;
    trk[TRK_DRUM + 1].p[P_E0] = 3;
    trk[1].step[2].n = 2, trk[1].step[2].note[0] = 60, trk[1].step[2].note[1] = 64, trk[1].step[2].time = ST_NOTE;
    trk[1].step[2].lvl = 0x0D;
    trk[5].step[7].n = 1, trk[5].step[7].note[0] = 72, trk[5].step[7].time = ST_NOTE;
    dstep_set(&trk[TRK_DRUM].dstep[9], 4, LV_SOFT, 1);
    dstep_set(&trk[TRK_DRUM + 1].dstep[11], 7, LV_HARD, 0);
    song.g[G_DUST] = 33;
    song.g[G_DRDLY] = 77;
    trk[1].micro[2] = -20;
    trk[TRK_DRUM].micro[9] = 12;
    lock_set(&trk[1], 2, P_ED_FLT, -30);
    lock_set(&trk[1], 2, P_E1, 5);
    lock_set(&trk[TRK_DRUM], 9, P_DIST, 64);
    step_fill_set(&trk[1], 2, FC_FILL);
    step_fill_set(&trk[TRK_DRUM], 9, FC_NOFILL);
    step_fill_set(&trk[TRK_DRUM + 1], 63, FC_FILL);
    song.sel = 7;
    proj_capture(&q);
    host_tracks_init();
    song.g[G_DRDLY] = 0;
    proj_apply(&q, 1);
    ok = proj_ok(&q) && q.sel == 7 && q.ntrk == NTRK && song.g[G_DRDLY] == 77 && trk[2].p[P_SLEN] == 7 &&
         trk[7].p[P_SLEN] == 12 && trk[5].p[P_LEVEL] == 77 && trk[7].p[P_LEVEL] == 66 && trk[7].p[P_E0] == 3 &&
         trk[1].step[2].n == 2 && trk[1].step[2].lvl == 0x0D && trk[5].step[7].note[0] == 72 && song.g[G_DUST] == 33 &&
         dstep_has(&trk[TRK_DRUM].dstep[9], 4) && dstep_lvl(&trk[TRK_DRUM].dstep[9], 4) == LV_SOFT &&
         dstep_rat(&trk[TRK_DRUM].dstep[9], 4) == 1u && dstep_has(&trk[TRK_DRUM + 1].dstep[11], 7) &&
         !dstep_has(&trk[TRK_DRUM + 1].dstep[9], 4) &&
         trk[1].micro[2] == -20 && trk[TRK_DRUM].micro[9] == 12 && trk[1].micro[3] == 0 &&
         lock_find(&trk[1], 2, P_ED_FLT, 0) >= 0 && trk[1].lock[lock_find(&trk[1], 2, P_ED_FLT, 0)].val == -30 &&
         lock_find(&trk[1], 2, P_E1, 0) >= 0 && lock_find(&trk[TRK_DRUM], 9, P_DIST, 0) >= 0 &&
         lock_find(&trk[TRK_DRUM], 9, P_E0, 0) < 0 &&
         step_fill(&trk[1], 2) == FC_FILL && step_fill(&trk[1], 3) == FC_NORM && step_fill(&trk[TRK_DRUM], 9) == FC_NOFILL &&
         step_fill(&trk[TRK_DRUM + 1], 63) == FC_FILL && step_fill(&trk[TRK_DRUM + 1], 8) == FC_NORM;
    bad += check("the working project: capture -> apply round trip (8 tracks, both drum tracks, DUST, drum DLY, locks)", ok);
    q2 = q;
    q2.g[G_DRDLY] = 5;
    q2.g[G_DUST] = 99;
    proj_apply(&q2, 0);                            /* (as a song section: the drum bus only) */
    ok = song.g[G_DRDLY] == 5 && song.g[G_DUST] == 33;
    q2.g[G_DRDLY] = 200;
    proj_apply(&q2, 1);
    ok &= song.g[G_DRDLY] == 127;
    bad += check("a section sets the drum bus (DLY 5), not the rest (DUST kept); a load clamps (200 -> 127)", ok);

    /* a damaged image: a nudge out of range, a lock on a parameter that cannot lock, on a step past the end,
     * with a value past the range: clamped, freed, freed, clamped; an engine past the table: back in it */
    q.t[1].micro[7] = 100;
    q.t[1].micro[8] = -100;
    q.t[1].lock[5].step = 4, q.t[1].lock[5].param = P_SLEN, q.t[1].lock[5].val = 8;
    q.t[1].lock[6].step = 64, q.t[1].lock[6].param = P_E0, q.t[1].lock[6].val = 1;
    q.t[1].lock[7].step = 4, q.t[1].lock[7].param = P_LEVEL, q.t[1].lock[7].val = 999;
    q.t[1].lock[8].step = 4, q.t[1].lock[8].param = 200, q.t[1].lock[8].val = 1;
    q.t[1].fill[1] = 0xF9;                         /* steps 5, 6: fill only, no fill; 7, 8: 3 (-> normal) */
    q.t[4].engine = 200;
    q.t[TRK_DRUM + 1].p[P_E0] = 30000;             /* a kit past the list */
    proj_apply(&q, 1);
    ok = trk[1].micro[7] == MICRO_MAX && trk[1].micro[8] == MICRO_MIN && trk[1].lock[5].step == LOCK_FREE &&
         trk[1].lock[6].step == LOCK_FREE && trk[1].lock[8].step == LOCK_FREE && trk[1].lock[7].step == 4 &&
         trk[1].lock[7].val == 127 && lock_find(&trk[1], 2, P_ED_FLT, 0) >= 0 &&
         step_fill(&trk[1], 4) == FC_FILL && step_fill(&trk[1], 5) == FC_NOFILL && trk[1].fill[1] == 0x09 &&
         trk[4].eng_req < NENGINES && trk[TRK_DRUM + 1].p[P_E0] == (int16_t)(DRUM_KITS - 1u);
    bad += check("apply: nudges clamped, bad locks freed, LEVEL 999 -> 127, condition 3 -> normal, engine / kit in range", ok);

    /* the sections (host: in RAM; the firmware reads them through XIP, sec_get) */
    memset(proj_slot, 0, sizeof proj_slot);
    proj_capture(&proj_slot[2]);
    ok = !sec_get(0) && !sec_get(1) && sec_get(2) == &proj_slot[2] && !sec_get(3) && sec_get(6) == &proj_slot[2];
    for (i = 0; i < 4u; i++)
        ok &= (sec_get(i) != 0) == (i == 2u);
    bad += check("sections: sec_get finds the stored one only (the slot number wraps)", ok);

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
