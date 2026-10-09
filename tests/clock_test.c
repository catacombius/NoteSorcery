/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock out and SONG POSITION in (NoteSorcery, seq.c clock_out / mclk_event) on the host, through hostsim.c:
 *   - GLO > SYSTEM > MIDI = SEQ+CLK, SYNC = INT: SONG POSITION 0, START and the downbeat pulse when PLAY starts,
 *     then 24 pulses a beat at the tempo (even, within a block), a tempo change followed at once, STOP at STOP;
 *   - none with MIDI = SEQ (the clock bit off), none while following USB (no loop back to the master), STOP when
 *     the setting goes off while running;
 *   - following TRS: the TRS clock passed on to USB, pulse for pulse;
 *   - SONG POSITION in (USB and TRS) then CONTINUE: the patterns start there (Ableton Live playing from a
 *     marker); START after it: from the top.
 * Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(int ok, const char *what)
{
    printf("clock: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t now_s;                               /* samples since the test began */
typedef struct { uint32_t t; uint8_t st, d1, d2; } ev_t;
static ev_t evs[4096];
static uint32_t nev;
static void block(void)
{
    int32_t o[2 * CTL];
    mix_block(o, CTL);
    now_s += CTL;
    fm1_ms = (uint32_t)((uint64_t)now_s * 1000u / FS);
    while (mo_r != mo_w) {
        uint32_t e = midi_out_q[mo_r++ % MQ];
        if (nev < 4096u)
            evs[nev++] = (ev_t){now_s, (uint8_t)(e >> 8), (uint8_t)(e >> 16), (uint8_t)(e >> 24)};
    }
}
static void run_s(double s) { uint32_t k, n = (uint32_t)(s * FS / CTL); for (k = 0; k < n; k++) block(); }
static uint32_t count(uint8_t st) { uint32_t i, n = 0; for (i = 0; i < nev; i++) n += evs[i].st == st; return n; }
static void reset(uint32_t mout, uint32_t sync)
{
    if (song.playing) {
        transport_req = 2;
        block();
    }
    host_tracks_init();
    song.g[G_MIDI] = (int16_t)mout;
    song.g[G_SYNC] = (int16_t)sync;
    usb.config = 1;
    mo_r = mo_w;
    nev = 0;
    clko.run = 0;
}
static void usb_in(uint32_t pkt) { midi_in_event(pkt); }
static void block2(void) { block(); block(); }   /* (MIDI in is read after the transport: it starts a block later) */

int main(void)
{
    uint32_t i;
    reset(MOUT_SEQ | MOUT_CLK, 0);
    song.g[G_BPM] = 120;
    transport_req = 1;
    block();
    check(nev >= 3u && evs[0].st == 0xF2 && evs[0].d1 == 0 && evs[1].st == 0xFA && evs[2].st == 0xF8,
          "PLAY: SONG POSITION 0, START, the downbeat pulse");
    run_s(2.0);                                       /* 4 beats at 120 */
    {
        uint32_t n = count(0xF8), first = 0, last = 0, k = 0, worst = 0;
        double iv = 22050.0 / 24.0;
        for (i = 0; i < nev; i++)
            if (evs[i].st == 0xF8) {
                if (k && fabs((evs[i].t - first) - (k * iv)) > worst)
                    worst = (uint32_t)fabs((evs[i].t - first) - (k * iv));
                if (!k)
                    first = evs[i].t;
                last = evs[i].t;
                k++;
            }
        (void)last;
        check(n >= 96u && n <= 98u && worst <= CTL, "120 BPM: 24 pulses a beat, each within a block of its time");
        printf("clock: %u pulses in 2 s, worst %u samples off the grid\n", n, worst);
    }
    {
        uint32_t n0, n;
        song.g[G_BPM] = 90;
        nev = 0;
        run_s(2.0);                                   /* 3 beats at 90 */
        n0 = count(0xF8);
        n = n0;
        check(n >= 71u && n <= 73u, "a tempo change (90 BPM): the pulses follow at once");
    }
    transport_req = 2;
    nev = 0;
    block();
    run_s(0.5);
    check(count(0xFC) == 1u && count(0xF8) == 0u, "STOP: STOP once, no pulses after it");

    reset(MOUT_SEQ, 0);
    transport_req = 1;
    run_s(1.0);
    check(count(0xF8) == 0u && count(0xFA) == 0u, "MIDI = SEQ (no CLK): no clock");

    reset(MOUT_SEQ | MOUT_CLK, 1);
    transport_req = 1;
    run_s(1.0);
    check(count(0xF8) == 0u && count(0xFA) == 0u, "following USB (SYNC = USB): no clock back to the master");

    reset(MOUT_CLK, 0);
    transport_req = 1;
    run_s(0.5);
    song.g[G_MIDI] = 0;
    nev = 0;
    run_s(0.2);
    check(count(0xFC) == 1u && count(0xF8) == 0u, "the clock turned off while running: STOP, then nothing");

    {   /* following TRS: the clock passed on */
        uint32_t k, in = 0, t0;
        reset(MOUT_CLK, 2);
        t0 = now_s;
        song.g[G_BPM] = 120;
        um_byte(0xFA);
        for (k = 0; k < 200u; k++) {               /* 120 BPM from the TRS master: a pulse every 918.75 samples */
            if ((uint64_t)(now_s - t0) * 24u * 2u >= (uint64_t)in * FS) {
                um_byte(0xF8);
                in++;
            }
            block();
        }
        printf("clock: TRS in %u pulses, USB out %u\n", in, count(0xF8));
        check(count(0xFA) == 1u && count(0xF8) + 2u >= in && count(0xF8) <= in + 2u, "following TRS: its clock passed on to USB, pulse for pulse");
    }

    {   /* SONG POSITION in, USB: 6 sixteenths = beat 1 and a half; then CONTINUE and the clock */
        uint32_t k;
        reset(0, 1);
        song.g[G_BPM] = 120;
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_SLEN] = 16;
        usb_in(0x03u | 0xF2u << 8 | 6u << 16 | 0u << 24);
        usb_in(0x0Fu | 0xFBu << 8);
        usb_in(0x0Fu | 0xF8u << 8);
        block2();
        check(song.playing && clk_beat == 1u && clk_pos >= BEAT_U / 2u && clk_pos < BEAT_U / 2u + 4u * CTL * 120u,
              "SONG POSITION 6 (USB), CONTINUE: the patterns start at beat 1 + 1/2");
        for (k = 0; k < 10u; k++)
            block();
        check(trk[0].seq_idx == 6u, "... the tracks on step 6 (1/16)");
        usb_in(0x0Fu | 0xFCu << 8);
        block();
        usb_in(0x03u | 0xF2u << 8 | 6u << 16);
        usb_in(0x0Fu | 0xFAu << 8);
        usb_in(0x0Fu | 0xF8u << 8);
        block2();
        check(song.playing && clk_beat == 0u && clk_pos < 4u * CTL * 120u, "SONG POSITION then START: from the top");
        usb_in(0x0Fu | 0xFCu << 8);
        block();
    }
    {   /* SONG POSITION in, TRS: 2 bars (32 sixteenths: LSB 32, MSB 0) */
        reset(0, 2);
        song.g[G_BPM] = 120;
        um_byte(0xF2), um_byte(32), um_byte(0);
        um_byte(0xFB);
        um_byte(0xF8);
        block2();
        check(song.playing && clk_beat == 8u && clk_pos < 4u * CTL * 120u, "SONG POSITION 32 (TRS), CONTINUE: bar 3");
        um_byte(0xFC);
        block();
        um_byte(0xF2), um_byte(0), um_byte(1);     /* 128 sixteenths: the MSB */
        um_byte(0xFB);
        um_byte(0xF8);
        block2();
        check(song.playing && clk_beat == 32u, "SONG POSITION 128 (two data bytes): beat 32");
    }
    printf("clock: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
