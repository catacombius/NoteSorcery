/* SPDX-License-Identifier: GPL-3.0-only */
/* NSX: NoteSorcery's commands on the editor protocol (editor.c; docs/NSX_PROTOCOL.md), for the browser editor and
 * the Android apps (FieldTape, NoteMove: android/fm1link). The same frames (F0 7D 46 4C cmd ... F7), commands 80..:
 *   80 NSX_CAPS      -> "NSX", version, what this FM-1 has (below)
 *   81 NSX_TRANSPORT op (0 query, 1 play, 2 stop, 3 toggle) -> playing, BPM v14, beat (u21), MIDI OUT bits, SYNC
 *   82 NSX_TEMPO     BPM v14 -> BPM v14 (clamped as the BPM knob; ignored while following a clock)
 * Projects (NSP1) come and go as backup objects (BK_LIST / BK_GET / BK_PUT, v6), samples with SMP_* (v1). */
enum { ED_NSX_CAPS = 80, ED_NSX_TRANSPORT, ED_NSX_TEMPO };
#define NSX_VERSION 1u
/* NSX_CAPS feature bits */
#define NSX_F_CLOCK_OUT 1u       /* MIDI clock out (GLO > SYSTEM > MIDI ..+CLK) */
#define NSX_F_SPP_IN 2u          /* SONG POSITION + CONTINUE */
#define NSX_F_USB_AUDIO_IN 4u    /* the FM-1's audio to the host (UAC IN) */
#define NSX_F_USB_AUDIO_OUT 8u   /* the host's audio to the FM-1 (live sampling) */
#define NSX_F_CIRCUIT_DRUMS 16u  /* 808 CM / 909 CM */

static void ed_u21(uint32_t v)
{
    ed_b(v);
    ed_b(v >> 7);
    ed_b(v >> 14);
}
static int ed_nsx_handle(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    switch (cmd) {
    case ED_NSX_CAPS:
        ed_str("NSX", 4);
        ed_b(NSX_VERSION);
        ed_str(FELUCCA_VERSION, 24);
        ed_b(NTRK);
        ed_b(NPART);
        ed_b(NDRUMTRK);
        ed_u21(sizeof(project_t));                   /* an NSP1 project (backup objects 0, 2..5) */
        ed_b(PROJ_MAGIC & 0x7Fu);                    /* (the magic's first byte, "N") */
        ed_b(NSX_F_CLOCK_OUT | NSX_F_SPP_IN | NSX_F_CIRCUIT_DRUMS |   /* (USB SAMPLE: with UAC; the editor means flash) */
             (FELUCCA_UAC ? NSX_F_USB_AUDIO_IN | NSX_F_USB_AUDIO_OUT : 0u));
        ed_b(song.g[G_DRCH]);                        /* drum track 1's MIDI channel (1..16; drum track 2: the next) */
        ed_b(SMP_USER_SLOTS);
        ed_b(SMP_USER_SIZE >> 10);                   /* KiB a sample slot */
        ed_b(WT_USER);                               /* user single-cycle wave slots (WAVE engine) */
        ed_b(DRUM_KITS);
        ed_b(DRUM_CM808);
        ed_b(DRUM_CM909);
        break;
    case ED_NSX_TRANSPORT: {
        uint32_t op = na ? a[0] : 0u;
        if (op == 1u || (op == 3u && !song.playing))
            transport_req = 1;                       /* (the audio ISR starts it at its next block) */
        else if (op == 2u || (op == 3u && song.playing))
            transport_req = 2;
        ed_b(op == 1u || (op == 3u && !song.playing) ? 1u : op == 2u || op == 3u ? 0u : song.playing);
        ed_v(song.g[G_BPM]);
        ed_u21(clk_beat);
        ed_b(song.g[G_MIDI] & 3);
        ed_b(song.g[G_SYNC]);
        break;
    }
    case ED_NSX_TEMPO:
        if (na >= 2u && !mclk_on())
            song.g[G_BPM] = (int16_t)clamp(ed_rv(a), GP[G_BPM].min, GP[G_BPM].max);
        ed_v(song.g[G_BPM]);
        break;
    default:
        return 0;
    }
    return 1;
}
