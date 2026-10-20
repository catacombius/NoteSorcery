/* SPDX-License-Identifier: GPL-3.0-only */
/* BOTANICA's screens as the FM-1 shows them out of the box (the MOSS theme), to PPM for review: DIR/moss-*.ppm.
 * The live UI of tests/ui_pages_test.c (its harness), a groove playing. Only BOTANICA (-DEDITION_BOTANICA=1). */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"

static void groove(void)
{
    static const uint8_t BASS[2] = {38, 45};
    uint32_t j, k;
    for (j = 0; j < NTRK; j++) { steps_clear(&trk[j]); trk[j].p[P_SLEN] = 16; }
    for (j = 0; j < 16u; j++) {
        step_t *sp;
        if (!(j & 1u)) { sp = &trk[0].step[j]; sp->n = 1; sp->note[0] = (uint8_t)(BASS[j / 8u] + ((j & 2u) ? 12 : 0)); sp->time = ST_NOTE; sp->vel = 100; }
        if ((j % 4u) == 2u) { sp = &trk[1].step[j]; sp->n = 3; sp->note[0] = 65; sp->note[1] = 69; sp->note[2] = 72; sp->time = ST_NOTE; sp->vel = 100; }
        if (!(j % 8u) || j == 11u) { sp = &trk[2].step[j]; sp->n = 2; sp->note[0] = 57; sp->note[1] = 64; sp->time = ST_NOTE; sp->vel = 100; }
        if (j % 3u == 0u) { sp = &trk[4].step[j]; sp->n = 1; sp->note[0] = 72; sp->time = ST_NOTE; sp->vel = 90; }
        memset(&TDRUM->dstep[j], 0, sizeof(dstep_t));
        memset(&trk[TRK_DRUM + 1].dstep[j], 0, sizeof(dstep_t));
        if (!(j % 4u)) dstep_set(&TDRUM->dstep[j], 0, LV_NORM, 0);
        if ((j % 8u) == 4u) dstep_set(&TDRUM->dstep[j], 3, LV_NORM, 0);
        dstep_set(&TDRUM->dstep[j], 4, (j & 1u) ? LV_SOFT : LV_NORM, 0);
        if (j % 4u == 3u) dstep_set(&trk[TRK_DRUM + 1].dstep[j], 2, LV_NORM, 0);
    }
    trk[3].p[P_SLEN] = 12;
    for (k = 0; k < NTRK; k++) trk[k].seq_active = 1;
}

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/botanica/host";
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 0;                               /* MOSS: BOTANICA's default */
    palette_set(0);
    host_tracks_init();
    bank_resolve();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    vis_on = 0;
    groove();
    sloop_splash(); ppm("moss-splash");
    go_home(); frames(4); ui.force = 1; frame(); ppm("moss-tracks");
    transport_req = 1; frames(37); ui.force = 1; frame(); ppm("moss-tracks-playing");
    open_family(FAM_ENV); ui.force = 1; ui.hot_col = 1; ui.hot_t = 30; frame(); ppm("moss-env");
    open_family(FAM_EDIT); ui.force = 1; frame(); ppm("moss-edit");
    open_family(FAM_FX); ui.force = 1; frame(); ppm("moss-fx");
    open_family(FAM_SEQ); ui.force = 1; frame(); ppm("moss-step");
    open_family(FAM_SEQ); ui.force = 1; frame(); ppm("moss-pattern");
    open_family(FAM_SAVE); ui.force = 1; frame(); ppm("moss-browser");
    open_family(FAM_GLO); ui.force = 1; frame(); ppm("moss-global");
    song.sel = TRK_DRUM; studio_open(SC_DRUM); drum_page = 0; drum_lane = 4; drum_cursor = 6;
    ui.force = 1; drums.hits = 1u | 1u << 4; frame(); ppm("moss-grid");
    drum_page = 1; ui.force = 1; drums.hits = 1u | 1u << 4; frame(); ppm("moss-kit");
    drum_page = 0; song.sel = 1; go_home(); ui_message("SAVED"); ui.force = 1; frame(); ppm("moss-toast");
    ui.msg_t = 0; ui.menu = 2; ui.force = 1; frame(); ppm("moss-about"); ui.menu = 0;
    go_home(); frames(2); tap(B_HOME); frames(60); ui.force = 1; frames(2); ppm("moss-garden");
    printf("bot_shots: %s/moss-*.ppm\n", outdir);
    return 0;
}
