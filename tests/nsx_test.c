/* SPDX-License-Identifier: GPL-3.0-only */
/* NSX (firmware/src/editor_nsx.c; docs/NSX_PROTOCOL.md) on the host, through hostsim.c: CAPS says what this FM-1
 * has (tracks, the NSP1 size, features, slots, the circuit kits) as the firmware has it; TRANSPORT plays, stops,
 * toggles and reports (BPM, beat, MIDI OUT, SYNC); TEMPO sets the BPM within its range, and not while a clock
 * drives it. editor.c needs the OTA plumbing (flash), so its five reply helpers stand in here, as editor.c has
 * them. Run by tests/run_tests.sh. */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { (void)i; return 0u; }
#include "../firmware/src/project.c"

static uint8_t ed_out[600];                         /* (editor.c's, without the frame: the reply's data) */
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
static void ed_str(const char *s, uint32_t max) { uint32_t i; for (i = 0; s && s[i] && i < max; i++) ed_b((uint8_t)s[i] & 0x7Fu); ed_b(0); }
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "NOTESORCERY TEST"          /* (ui.c has it; hostsim leaves ui.c out) */
#endif
#include "../firmware/src/editor_nsx.c"

static int bad;
static void check(int ok, const char *what)
{
    printf("nsx: %-75s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t rd;
static uint32_t gb(void) { return rd < ed_n ? ed_out[rd++] : 0xFFu; }
static int32_t gv(void) { uint32_t a = gb(); return (int32_t)(a | gb() << 7) - 8192; }
static uint32_t g21(void) { uint32_t a = gb(), b = gb(); return a | b << 7 | gb() << 14; }
static void gs(char *o) { uint32_t c, i = 0; while ((c = gb()) != 0u && c != 0xFFu) o[i++] = (char)c; o[i] = 0; }
static int req(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    ed_n = 0;
    rd = 0;
    return ed_nsx_handle(cmd, a, na);
}
static void block(void) { int32_t o[2 * CTL]; mix_block(o, CTL); }

int main(void)
{
    char s[32], v[32];
    host_tracks_init();
    song.g[G_DRCH] = 10;
    {
        uint32_t ver, ntrk, npart, ndr, psize, flags, drch, slots, kib, waves, kits, cm8, cm9;
        check(req(ED_NSX_CAPS, 0, 0), "CAPS (80) answered");
        gs(s), ver = gb(), gs(v), ntrk = gb(), npart = gb(), ndr = gb(), psize = g21();
        gb();
        flags = gb(), drch = gb(), slots = gb(), kib = gb(), waves = gb(), kits = gb(), cm8 = gb(), cm9 = gb();
        printf("nsx: CAPS: %s v%u \"%s\", %u tracks (%u synth, %u drums), NSP1 %u B, flags %u, drums ch %u, %u slots x %u KiB, "
               "%u waves, %u kits (CM %u %u)\n", s, ver, v, ntrk, npart, ndr, psize, flags, drch, slots, kib, waves, kits, cm8, cm9);
        check(!strcmp(s, "NSX") && ver == NSX_VERSION && !strcmp(v, FELUCCA_VERSION) && ntrk == NTRK && npart == NPART && ndr == NDRUMTRK,
              "CAPS: NSX, its version, the firmware's, 8 tracks: 6 synth, 2 drums");
        check(psize == sizeof(project_t) && (flags & NSX_F_CLOCK_OUT) && (flags & NSX_F_SPP_IN) && (flags & NSX_F_CIRCUIT_DRUMS) &&
              drch == 10u && slots == SMP_USER_SLOTS && kib == 64u && waves == WT_USER && kits == DRUM_KITS && cm8 == DRUM_CM808 &&
              cm9 == DRUM_CM909 && rd == ed_n, "CAPS: the NSP1 size, clock out, SPP, circuit kits, drum channel, slots, kits, nothing after");
    }
    {
        uint8_t op;
        uint32_t playing;
        song.g[G_BPM] = 128;
        song.g[G_MIDI] = 3;
        op = 0;
        req(ED_NSX_TRANSPORT, &op, 1);
        playing = gb();
        check(playing == 0u && gv() == 128 && g21() == 0u && gb() == 3u && gb() == 0u && !transport_req,
              "TRANSPORT query: stopped, 128 BPM, beat 0, MIDI OUT SEQ+CLK, SYNC INT");
        op = 1;
        req(ED_NSX_TRANSPORT, &op, 1);
        check(gb() == 1u && transport_req == 1u, "TRANSPORT play: says playing, PLAY queued for the audio ISR");
        block();
        block();
        check(song.playing, "... and it plays");
        while (clk_beat < 2u)
            block();
        op = 0;
        req(ED_NSX_TRANSPORT, &op, 1);
        gb(), gv();
        check(g21() == clk_beat, "TRANSPORT query while playing: the beat");
        op = 3;
        req(ED_NSX_TRANSPORT, &op, 1);
        check(gb() == 0u && transport_req == 2u, "TRANSPORT toggle while playing: STOP queued");
        block();
        check(!song.playing, "... and it stops");
        req(ED_NSX_TRANSPORT, &op, 1);
        check(gb() == 1u && transport_req == 1u, "TRANSPORT toggle while stopped: PLAY");
        transport_req = 0;
    }
    {
        uint8_t a[2];
        uint32_t u = (uint32_t)(140 + 8192);
        a[0] = (uint8_t)(u & 127u), a[1] = (uint8_t)(u >> 7);
        req(ED_NSX_TEMPO, a, 2);
        check(gv() == 140 && song.g[G_BPM] == 140, "TEMPO 140: set");
        u = (uint32_t)(5000 + 8192);
        a[0] = (uint8_t)(u & 127u), a[1] = (uint8_t)(u >> 7);
        req(ED_NSX_TEMPO, a, 2);
        check(song.g[G_BPM] == GP[G_BPM].max, "TEMPO 5000: the BPM's maximum");
        req(ED_NSX_TEMPO, 0, 0);
        check(gv() == GP[G_BPM].max, "TEMPO without a value: a query");
    }
    check(!req(79, 0, 0) && !req(83, 0, 0), "79, 83: not NSX (left to the other handlers)");
    printf("nsx: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
