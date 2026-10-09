/* SPDX-License-Identifier: GPL-3.0-only */
/* USB SAMPLE (NoteSorcery: firmware/src/usb.c USMP_*, usb_sample.c) on the host:
 *   descriptors  with the menu's USB SAMPLE on (and USB SERIAL off, or not built), the configuration as a host parses
 *                it: every length, 4 interfaces, the audio control header naming 1, 2 and 3 with its class-specific
 *                total, four terminals linked (1 -> 2 out to the host, 3 -> 4 in from it), interface 3's alt 0 and
 *                alt 1 (AS general on terminal 3, 16-bit stereo 44.1 kHz, EP3 OUT isochronous adaptive, 192 B), a
 *                new bcdDevice; with it off, the plain descriptors byte for byte
 *   recorder     packets as a host sends them (44 / 45 frames a ms): silence keeps it armed, the first sound starts
 *                the take, the audio comes back from the ADPCM (decoded as the SAMPLE engine does) at 22.05 kHz,
 *                mono, the sum of both channels, at better than 30 dB; OCT+ ends it; a full slot ends it by itself
 * The SIE is never touched. Run by tests/run_tests.sh (-DT_CDC=0 / 2, as uac_test). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifndef HALF_FRAMES
#define HALF_FRAMES 128u
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC (T_CDC != 0)
#define FELUCCA_UAC 1
static volatile uint32_t fm1_ms;
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/src/usb.c"
#include "../firmware/src/usb_sample.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("usb sample: %-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
    return ok;
}
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

static void test_descriptors(void)
{
    const uint8_t *c, *d0, *c0, *dv;
    uint16_t cl, l0, dl;
    uint32_t off, nif = 0, ac_total = 0, ac_sum = 0, in_ac = 0, ac_hdr = 0;
    int lens = 1, coll = 0, terms = 0, links = 0, if3 = 0, fmt = 0, ep = 0, asg = 0, cur_if = -1, cur_alt = -1;
    uint8_t term[8] = {0}, src_of[8] = {0}, seen_if[8] = {0};
#if T_CDC
    usb_cdc_on = 0;                                     /* (USB SERIAL off: USB SAMPLE needs it) */
#endif
    usb_smp_on = 0;
    get_desc(0x0200, &c0, &l0);
    get_desc(0x0100, &d0, &dl);
    {
        static uint8_t plain[512], pdev[18];
        memcpy(plain, c0, l0);
        memcpy(pdev, d0, 18);
        usb_smp_on = 1;
        get_desc(0x0200, &c, &cl);
        get_desc(0x0100, &dv, &dl);
        check("USB SAMPLE on: bcdDevice + 0.40 (hosts read the new descriptors)", dv[12] == (uint8_t)(pdev[12] + 0x40u) && dv[13] == pdev[13]);
        usb_smp_on = 0;
        {
            const uint8_t *c2;
            uint16_t l2;
            get_desc(0x0200, &c2, &l2);
            check("USB SAMPLE off: the configuration as before, byte for byte", l2 == l0 && !memcmp(c2, plain, l0));
        }
        usb_smp_on = 1;
    }
    check("wTotalLength is what is sent", le16(c + 2) == cl);
    for (off = 0; off < cl; off += c[off]) {
        const uint8_t *d = c + off;
        if (d[0] < 2 || off + d[0] > cl) {
            lens = 0;
            break;
        }
        if (d[1] == 4) {                                /* interface */
            cur_if = d[2], cur_alt = d[3];
            if (!seen_if[d[2] & 7u]++)
                nif++;
            in_ac = d[5] == 1 && d[6] == 1;
            if (d[2] == USMP_IF)
                if3 |= d[3] == 0 && d[4] == 0 ? 1 : d[3] == 1 && d[4] == 1 && d[5] == 1 && d[6] == 2 ? 2 : 0;
            continue;
        }
        if (d[1] == 0x24 && in_ac) {                    /* audio control, class-specific */
            ac_sum += d[0];
            if (d[2] == 1) {
                ac_hdr = 1;
                ac_total = le16(d + 5);
                coll = d[7] == 3 && d[8] == 1 && d[9] == 2 && d[10] == 3;
            } else if (d[2] == 2 || d[2] == 3) {
                if (d[3] < 8)
                    term[d[3]] = d[2], src_of[d[3]] = d[2] == 3 ? d[7] : 0;
                terms++;
            }
            continue;
        }
        if (d[1] == 0x24 && cur_if == (int)USMP_IF && cur_alt == 1) {
            if (d[2] == 1)
                asg = d[3] == 3 && le16(d + 5) == 1;    /* AS general: terminal 3, PCM */
            if (d[2] == 2)
                fmt = d[0] == 11 && d[3] == 1 && d[4] == 2 && d[5] == 2 && d[6] == 16 && d[7] == 1 &&
                      (d[8] | d[9] << 8 | d[10] << 16) == 44100;
        }
        if (d[1] == 5 && cur_if == (int)USMP_IF && cur_alt == 1)
            ep = d[2] == 0x03 && (d[3] & 0x0Fu) == 0x09 && le16(d + 4) == USMP_MAXP && d[6] == 1;
    }
    links = term[1] == 2 && term[2] == 3 && src_of[2] == 1 && term[3] == 2 && term[4] == 3 && src_of[4] == 3;
    check("every descriptor's length, none past the end", lens && off == cl);
    check("4 interfaces (control, MIDI, audio to the host, audio from it), bNumInterfaces 4", nif == 4u && c[4] == 4u);
    check("audio control header: interfaces 1, 2 and 3", ac_hdr && coll);
    check("... its class-specific total is its descriptors'", ac_total == ac_sum);
    check("four terminals: 1 -> 2 (to the host), 3 (USB streaming in) -> 4 (speaker)", terms == 4 && links);
    check("interface 3: alt 0 without bandwidth, alt 1 with the endpoint", if3 == 3);
    check("alt 1: AS general on terminal 3, PCM; 2 ch 16 bit 44100 Hz", asg && fmt);
    check("EP3 OUT, isochronous adaptive, 192 bytes, every 1 ms", ep);
}

/* IMA decode, as the SAMPLE engine reads a slot */
static void ima_decode(const uint8_t *b, uint32_t n, int16_t *out)
{
    int32_t pred = 0, idx = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t code = (b[i >> 1] >> ((i & 1u) * 4u)) & 15u;
        int32_t step = IMA_STEP[idx], vd = step >> 3;
        if (code & 4u) vd += step;
        if (code & 2u) vd += step >> 1;
        if (code & 1u) vd += step >> 2;
        pred += code & 8u ? -vd : vd;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += IMA_IDX[code & 7u];
        idx = idx > 88 ? 88 : idx < 0 ? 0 : idx;
        out[i] = (int16_t)pred;
    }
}

static uint8_t flashed[USMP_MAX_N / 2u + 16u];
static void drain(void)                                 /* the main loop's flush, into "flash" */
{
    uint32_t to = usmp.w / 2u;
    while (usmp.r < to) {
        flashed[usmp.r] = usmp.ring[usmp.r % USMP_RING];
        usmp.r++;
    }
}
static uint32_t t_in, t_ms;                             /* frames sent so far (44.1 a ms: 44 and 45), ms */
static int16_t sig_at(uint32_t i, int which)            /* L: a 441 Hz sine, R: 882 Hz, both at -12 dB */
{
    double t = (double)i / 44100.0;
    return (int16_t)(which ? 8000.0 * sin(2 * M_PI * 882 * t) : 8000.0 * sin(2 * M_PI * 441 * t));
}
static int32_t mono_at(uint32_t i) { return ((int32_t)sig_at(i, 0) + sig_at(i, 1)) >> 1; }
static void send_ms(int silent)
{
    static int16_t pkt[2 * 46];
    uint32_t n = (t_ms + 1u) * 441u / 10u - t_ms * 441u / 10u, i;
    for (i = 0; i < n; i++) {
        pkt[2 * i] = silent ? 0 : sig_at(t_in + i, 0);
        pkt[2 * i + 1] = silent ? 0 : sig_at(t_in + i, 1);
    }
    usmp_take(pkt, n);
    t_in += n;
    t_ms++;
    fm1_ms++;
}

static void test_recorder(void)
{
    static int16_t dec[USMP_MAX_N];
    uint32_t i, start_in, n, k;
    double se = 0, ss = 0;
    memset(&usmp, 0, sizeof usmp);
    usmp.state = US_ARMED;
    for (i = 0; i < 100u; i++) send_ms(1);
    check("armed: 100 ms of silence take nothing", usmp.state == US_ARMED && usmp.w == 0u);
    start_in = t_in;
    while (abs(mono_at(start_in)) <= USMP_THRESH)       /* the take starts at the first sample above -40 dBFS */
        start_in++;
    for (i = 0; i < 1000u; i++) {
        send_ms(0);
        if (i % 20u == 0u) drain();
    }
    drain();
    check("the first sound starts the take (REC)", usmp.state == US_REC);
    n = usmp.w;
    k = (t_in - start_in) / 2u;                         /* what 1 s holds from the first sound on */
    printf("usb sample: 1 s in: %u samples (%u expected), drops %u\n", n, k, usmp.drops);
    check("1 s at 44.1 kHz stereo: 22050 mono samples less the silence before the sound, none dropped",
          n == k && n + 8u >= 22050u && !usmp.drops);
    ima_decode(flashed, n, dec);
    for (k = 64; k < n; k++) {                         /* against (L + R) / 2, pairs averaged */
        uint32_t a = start_in + 2u * k;
        double want = (double)((mono_at(a) + mono_at(a + 1)) >> 1);
        se += (dec[k] - want) * (dec[k] - want);
        ss += want * want;
    }
    printf("usb sample: ADPCM round trip SNR %.1f dB\n", 10 * log10(ss / se));
    check("the take decodes back to the mono mix at 22.05 kHz, SNR > 30 dB", 10 * log10(ss / se) > 30.0);
    usmp_stop();
    check("OCT+ while recording: the take ends (SAVE)", usmp.state == US_SAVE);
    usmp.state = US_ARMED;
    usmp_stop();
    check("OCT+ while armed (nothing came): back to idle, the slot left empty", usmp.state == US_IDLE);
    memset(&usmp, 0, sizeof usmp);                     /* a full slot ends it by itself */
    usmp.state = US_ARMED;
    for (i = 0; i < 7000u && usmp.state != US_SAVE; i++) {
        send_ms(0);
        drain();
    }
    check("a full slot (5.9 s) ends the take by itself, at its size", usmp.state == US_SAVE && usmp.w == USMP_MAX_N);
    {
        uint32_t nn;
        int32_t pk;
        usmp.peak = 0;
        for (i = 0; i < 5u; i++)                        /* (a 441 Hz period, 2.3 ms, twice) */
            send_ms(0);
        usmp_ui(&nn, &pk);
        check("the input meter: the packets' peak (the mono mix)", pk > 4000 && pk <= 8000);
        usmp_ui(&nn, &pk);
        check("... and it starts again once the screen has looked", pk == 0);
    }
}

int main(void)
{
    test_descriptors();
    test_recorder();
    printf("usb sample: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
