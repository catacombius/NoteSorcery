/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixtures for the browser's Ableton export (web/als/nsals.js, web/als/test_als.mjs), from the firmware itself:
 *   layout.json   where NSP1 keeps what (project.c project_t), the P_ / G_ indices the export reads, the engines
 *   live.nsp1     a working project with every step feature the export turns into notes (accent, slide, ties,
 *                 ratchets, nudge, dynamics, a fill-only step, swing, a chord, drum lanes with levels and a roll)
 *   secA.nsp1, secB.nsp1   two song sections (B: another bass line, 32 steps)
 *   played.json   the MIDI the FM-1 sends (GLO > MIDI OUT: SEQ) over the first two loops of live.nsp1:
 *                 [sample, status, note, velocity] -- what the export must match
 * argv[1]: the directory. Run by tests/run_tests.sh before test_als.mjs. */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { (void)i; return 0u; }   /* (ui.c TRK_DEF: unused here) */
#include "../firmware/src/project.c"

static void put(track_t *t, uint32_t i, uint32_t n, const uint8_t *notes, uint32_t time, uint32_t flags)
{
    put_step(t, i, n, notes, time, flags);
}
static void one(track_t *t, uint32_t i, uint32_t note, uint32_t flags) { uint8_t n[1] = {(uint8_t)note}; put(t, i, 1, n, ST_NOTE, flags); }
static void tie(track_t *t, uint32_t i) { put(t, i, 0, 0, ST_TIE, 0); t->step[i].time = ST_TIE; }

static void write_file(const char *dir, const char *name, const void *b, size_t n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f || fwrite(b, 1, n, f) != n) {
        perror(path);
        exit(1);
    }
    fclose(f);
}

static void bass(track_t *t, int alt)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        put(t, i, 0, 0, ST_REST, 0);
    t->p[P_SLEN] = alt ? 32 : 16;
    t->p[P_SDIV] = 2;                                /* 1/16 */
    t->p[P_SGATE] = 64;
    if (alt) {
        for (i = 0; i < 32u; i += 4u)
            one(t, i, 36u + (i / 4u) % 5u * 2u, i % 8u ? 0u : SF_ACCENT);
        return;
    }
    one(t, 0, 36, SF_ACCENT);
    one(t, 2, 39, SF_SLIDE);                         /* slides into step 3 */
    one(t, 3, 41, 0);
    one(t, 4, 43, 0);                                /* held over steps 5 and 6 */
    tie(t, 5);
    tie(t, 6);
    one(t, 8, 48, 0);
    t->step[8].rat = 2;                              /* x3 */
    one(t, 10, 50, 0);
    t->micro[10] = 16;                               /* a quarter step late */
    one(t, 12, 51, 0);
    t->step[12].lvl = LV_GHOST;
    one(t, 14, 60, 0);
    t->fill[14 / 4] |= (uint8_t)(FC_FILL << (2u * (14u % 4u)));   /* only during a fill */
}

int main(int argc, char **argv)
{
    static project_t p;
    const char *dir = argc > 1 ? argv[1] : "build/host";
    track_t *b = &trk[0], *pad = &trk[1], *dr = &trk[TRK_DRUM];
    uint32_t i;
    FILE *f;
    char path[512];

    host_tracks_init();
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_SLEN] = 16, trk[i].p[P_SDIV] = 2;
    host_preset(b, ENGI_ACID, 0);
    host_preset(pad, ENGI_FM6, 0);
    pad->p[P_VOICE] = V_POLY;
    bass(b, 0);
    pad->p[P_SLEN] = 8;
    pad->p[P_SDIV] = 1;                              /* 1/8 */
    pad->p[P_SSWING] = 50;                           /* the odd eighths late */
    pad->p[P_SGATE] = 100;
    {
        static const uint8_t ch[3] = {60, 64, 67};
        put(pad, 0, 3, ch, ST_NOTE, 0);
        one(pad, 1, 62, 0);
        one(pad, 3, 65, 0);
        one(pad, 6, 67, 0);
    }
    dr->p[P_SLEN] = 16;
    dr->p[P_SDIV] = 2;
    dr->p[P_SGATE] = 64;
    for (i = 0; i < 16u; i++) {
        memset(&dr->dstep[i], 0, sizeof dr->dstep[i]);
        if (i % 4u == 0u)
            dstep_set(&dr->dstep[i], lane_of_note(36), LV_NORM, 0);
        if (i % 2u == 0u)
            dstep_set(&dr->dstep[i], lane_of_note(42), i % 4u ? LV_SOFT : LV_NORM, 0);
        if (i == 4u || i == 12u)
            dstep_set(&dr->dstep[i], lane_of_note(38), LV_HARD, 0);
    }
    dstep_set(&dr->dstep[15], lane_of_note(42), LV_NORM, 1);   /* a roll: x2 */
    proj_capture(&p);
    write_file(dir, "live.nsp1", &p, sizeof p);
    write_file(dir, "secA.nsp1", &p, sizeof p);
    bass(b, 1);
    proj_capture(&p);
    write_file(dir, "secB.nsp1", &p, sizeof p);
    bass(b, 0);                                      /* back to the live project, and play it */

    snprintf(path, sizeof path, "%s/played.json", dir);
    f = fopen(path, "w");
    fprintf(f, "{\"fs\": %u, \"bpm\": %d, \"ch\": [", FS, song.g[G_BPM]);
    for (i = 0; i < NTRK; i++)
        fprintf(f, "%s%u", i ? ", " : "", trk_midi_ch(i));
    fprintf(f, "], \"events\": [");
    {
        uint32_t fr, first = 1, frames = 2u * 4u * 60u * FS / 120u + FS / 4u;   /* two 4-beat loops, a tail */
        usb.config = 1;
        song.g[G_MIDI] = 1;                          /* MIDI OUT: SEQ */
        transport_req = 1;
        for (fr = 0; fr < frames; fr += CTL) {
            int32_t o[2 * CTL];
            mix_block(o, CTL);
            while (mo_r != mo_w) {
                uint32_t e = midi_out_q[mo_r++ % MQ];
                fprintf(f, "%s\n  [%u, %u, %u, %u]", first ? "" : ",", fr, (e >> 8) & 255u, (e >> 16) & 255u, e >> 24);
                first = 0;
            }
        }
    }
    fprintf(f, "\n]}\n");
    fclose(f);

    snprintf(path, sizeof path, "%s/layout.json", dir);
    f = fopen(path, "w");
    fprintf(f, "{\"magic\": %u, \"size\": %u, \"P_COUNT\": %u, \"G_COUNT\": %u, \"NTRK\": %u, \"NPART\": %u, \"NSTEP\": %u, "
               "\"NLOCK\": %u,\n", PROJ_MAGIC, (unsigned)sizeof(project_t), P_COUNT, G_COUNT, NTRK, NPART, NSTEP, NLOCK);
    fprintf(f, " \"P\": {\"LEVEL\": %u, \"ROOT\": %u, \"SCALE\": %u, \"TRANS\": %u, \"SLEN\": %u, \"SDIV\": %u, \"SSWING\": %u, "
               "\"SGATE\": %u, \"PAN\": %u, \"MUTE\": %u, \"E0\": %u},\n", P_LEVEL, P_ROOT, P_SCALE, P_TRANS, P_SLEN, P_SDIV,
            P_SSWING, P_SGATE, P_PAN, P_MUTE, P_E0);
    fprintf(f, " \"G\": {\"BPM\": %u, \"SWING\": %u},\n \"ENGINES\": [", G_BPM, G_SWING);
    for (i = 0; i < NENGINES; i++)
        fprintf(f, "%s\"%s\"", i ? ", " : "", N_ENGNAME[i]);
    fprintf(f, "],\n \"offsets\": {\"g\": %u, \"t\": %u, \"trk\": %u, \"step\": %u, \"micro\": %u, \"lock\": %u, \"fill\": %u, "
               "\"sum\": %u},\n", (unsigned)offsetof(project_t, g), (unsigned)offsetof(project_t, t), (unsigned)sizeof(proj_trk_t),
            (unsigned)offsetof(proj_trk_t, step), (unsigned)offsetof(proj_trk_t, micro), (unsigned)offsetof(proj_trk_t, lock),
            (unsigned)offsetof(proj_trk_t, fill), (unsigned)offsetof(project_t, sum));
    fprintf(f, " \"LANE_NOTE\": [");
    for (i = 0; i < DRUM_LANES; i++)
        fprintf(f, "%s%u", i ? ", " : "", LANE_NOTE[i]);
    fprintf(f, "],\n \"NSCALES\": %u}\n", (unsigned)NSCALES);
    fclose(f);
    printf("nsp1: fixtures (layout, live, sections A and B, the MIDI the FM-1 plays) -> %s\n", dir);
    return 0;
}
