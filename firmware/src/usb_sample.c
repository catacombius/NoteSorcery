/* SPDX-License-Identifier: GPL-3.0-only */
/* NoteSorcery: USB SAMPLE. What a phone or computer plays into the FM-1 (its USB audio output: usb.c USMP_*,
 * menu AUDIO > USB SAMPLE) recorded into a user sample slot, USR1..USR4, as one zone over the whole keyboard (root
 * C4): the SAMPLE engine plays it, the drum kits USR1..4 too (a lane per key), the editor can chop it.
 *
 * The TIMER5 ISR (usb.c usmp_rx -> usmp_take) mixes each packet to mono, halves the rate (44.1 -> 22.05 kHz, the
 * slots' rate; a pair averaged) and encodes IMA ADPCM into a 2 KiB ring (186 ms). The main loop (usmp_service)
 * programs the slot in 64-byte pieces (the interrupts are off for each: ~0.2 ms, which the audio's 2.9 ms halves
 * and the USB FIFO ride out) and, when the take ends (OCT+, the slot full, or the host silent for 2 s), the header
 * with its CRC. The slot is erased before (0.7 s, the audio silenced as for any erase); then ARMED: the take
 * starts with the first sound above -40 dBFS, as an OP-1's sampler does. */
#include "ima.h"
#define USMP_RING 2048u                                 /* ADPCM bytes: 4096 samples, 186 ms */
#define USMP_PIECE 64u                                  /* bytes a flash write */
#define USMP_THRESH 328                                 /* -40 dBFS: the take starts */
_Static_assert(USMP_RING % USMP_PIECE == 0u, "a piece never wraps the ring");
static struct {
    volatile uint8_t state;                             /* US_* (the ISR moves ARMED -> REC, REC -> SAVE) */
    uint8_t slot;                                       /* 0..3 */
    uint8_t ring[USMP_RING];
    volatile uint32_t w;                                /* samples encoded (nibbles) */
    uint32_t r;                                         /* bytes programmed */
    int32_t pred, idx;                                  /* the encoder */
    int32_t half;                                       /* decimation: the first of a pair */
    uint8_t have_half;
    volatile int32_t peak;                              /* the input's peak since the screen looked */
    volatile uint32_t last_ms;                          /* the last packet */
    uint32_t drops, errs;                               /* samples the full ring dropped; flash errors */
} usmp;

static uint32_t ima_encode1(int32_t *pred, int32_t *idx, int32_t x)   /* one sample -> its nibble */
{
    int32_t step = IMA_STEP[*idx], diff = x - *pred, vd = step >> 3;
    uint32_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
        vd += step;
    }
    if (diff >= step >> 1) {
        code |= 2;
        diff -= step >> 1;
        vd += step >> 1;
    }
    if (diff >= step >> 2) {
        code |= 1;
        vd += step >> 2;
    }
    *pred = code & 8u ? *pred - vd : *pred + vd;
    *pred = *pred > 32767 ? 32767 : *pred < -32768 ? -32768 : *pred;
    *idx += IMA_IDX[code & 7u];
    *idx = *idx > 88 ? 88 : *idx < 0 ? 0 : *idx;
    return code;
}

/* from the TIMER5 ISR: a packet of 16-bit stereo frames */
static void usmp_take(const int16_t *pcm, uint32_t frames)
{
    uint32_t i;
    int32_t pk = usmp.peak;
    usmp.last_ms = fm1_ms;
    for (i = 0; i < frames; i++) {
        int32_t m = ((int32_t)pcm[2u * i] + pcm[2u * i + 1u]) >> 1, a = m < 0 ? -m : m, x;
        uint32_t w, code;
        if (a > pk)
            pk = a;
        if (usmp.state == US_ARMED && a > USMP_THRESH) {
            usmp.state = US_REC;                        /* the first sound: the take starts here */
            usmp.have_half = 0;
        }
        if (usmp.state != US_REC)
            continue;
        if (!usmp.have_half) {
            usmp.half = m;
            usmp.have_half = 1;
            continue;
        }
        usmp.have_half = 0;
        x = (usmp.half + m) >> 1;                       /* 44.1 -> 22.05 kHz: a pair averaged */
        w = usmp.w;
        if (w >= USMP_MAX_N) {
            usmp.state = US_SAVE;                       /* the slot is full */
            break;
        }
        if (w / 2u - usmp.r >= USMP_RING - 1u) {
            usmp.drops++;                               /* (the main loop fell behind: never seen at 186 ms) */
            continue;
        }
        code = ima_encode1(&usmp.pred, &usmp.idx, x);
        if (w & 1u)
            usmp.ring[(w >> 1) % USMP_RING] |= (uint8_t)(code << 4);
        else
            usmp.ring[(w >> 1) % USMP_RING] = (uint8_t)code;
        usmp.w = w + 1u;
    }
    usmp.peak = pk;
}

static void usmp_stop(void)                             /* OCT+ again, or the screen closed */
{
    if (usmp.state == US_REC)
        usmp.state = US_SAVE;
    else if (usmp.state == US_ARMED)
        usmp.state = US_IDLE;                           /* (nothing came: the slot stays empty) */
}

#if FELUCCA_FLASH && FELUCCA_OTA
/* bytes [usmp.r, to) of the ring into the slot, in pieces */
static void usmp_flush(uint32_t to)
{
    while (usmp.r < to) {
        uint32_t n = to - usmp.r, at = usmp.r % USMP_RING;
        if (n > USMP_PIECE)
            n = USMP_PIECE;
        if (n > USMP_RING - at)
            n = USMP_RING - at;
        if (fl_write(SMP_USER_OFF(usmp.slot) + SMP_USER_DATA + usmp.r, usmp.ring + at, n))
            usmp.errs++;
        usmp.r += n;
    }
}

/* the take's header: one zone, root C4, the whole keyboard, 22050 Hz */
static int usmp_header(uint32_t n)
{
    static smp_user_hdr_t h;
    uint32_t bytes = (n + 1u) / 2u;
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = 1;
    memcpy(h.name, "USB TAKE", 8);
    h.data_len = bytes;
    ed_smp_inval(usmp.slot);
    h.crc = st_crc32(smp_user_xip(usmp.slot) + SMP_USER_DATA, bytes);
    h.zone[0].off = 0;
    h.zone[0].n = n;
    h.zone[0].ls = 0;
    h.zone[0].le = n - 1u;
    h.zone[0].rate = 32768u;                            /* 22050 / 44100, Q16 */
    h.zone[0].root16 = 60 * 16;
    h.zone[0].lo = 0;
    h.zone[0].hi = 127;
    if (fl_write(SMP_USER_OFF(usmp.slot), (const uint8_t *)&h, sizeof h))
        return 0;
    ed_smp_inval(usmp.slot);
    smp_user_scan(usmp.slot);
    return usr_nz[usmp.slot] != 0;
}

/* the screen: a take into slot k (erases it: ~0.7 s), armed */
static int usmp_start(uint32_t k)
{
    if (!flash_ok || k >= SMP_USER_SLOTS)
        return 0;
    usmp.state = US_IDLE;
    usmp.slot = (uint8_t)k;
    if (ed_smp_erase(k, 1)) {
        usmp.state = US_FAIL;
        return 0;
    }
    usmp.w = usmp.r = 0;
    usmp.pred = usmp.idx = 0;
    usmp.have_half = 0;
    usmp.drops = usmp.errs = 0;
    usmp.state = US_ARMED;                              /* (last: the ISR sees a ready recorder) */
    return 1;
}

/* main loop: the take into flash, and its end */
static void usmp_service(void)
{
    uint32_t w = usmp.w;
    if (usmp.state == US_REC) {
        usmp_flush(w / 2u / USMP_PIECE * USMP_PIECE);   /* whole pieces while it runs */
        if (fm1_ms - usmp.last_ms > 2000u && w)
            usmp.state = US_SAVE;                       /* the host stopped playing */
    }
    if (usmp.state == US_SAVE) {
        usmp_flush((w + 1u) / 2u);                      /* the rest, the last half byte too */
        usmp.state = w && usmp_header(w) && !usmp.errs ? US_DONE : w ? US_FAIL : US_IDLE;
    }
}
#else
static int usmp_start(uint32_t k) { (void)k; return 0; }
static void usmp_service(void) {}
#endif

/* the screen (ui_menu.c): the state, the samples taken, the input's peak since it last looked */
static uint32_t usmp_ui(uint32_t *n, int32_t *peak)
{
    *n = usmp.w;
    *peak = usmp.peak;
    usmp.peak = 0;
    return usmp.state;
}
static uint32_t usmp_state(void) { return usmp.state; }
#if FELUCCA_UAC
static int usb_smp_streaming(void) { return usb_smp_now() && usmp_usb.alt && fm1_ms - usmp.last_ms < 200u; }
#else
static int usb_smp_streaming(void) { return 0; }
#endif
