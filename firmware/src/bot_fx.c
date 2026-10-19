/* SPDX-License-Identifier: GPL-3.0-only */
/* BOTANICA's master effects (voice.c includes it: the voices read BREEZE and WEATHER; fx.c runs BLOOM): no buffer of their own, they bend the ones there are.
 *
 *   BLOOM (PERFORM knob 1, G_BLOOM; G_BMODE what it does to the reverb):
 *     SWELL  the reverb comes in after each new note instead of with it: an onset (the reverb's send jumping
 *            above its slow average) ducks the reverb's return, which swells back over 0.25..1.5 s: the
 *            reversed-reverb rise of botanica. BLOOM also washes the whole mix into the reverb and grows it
 *     FREEZE the reverb's four lines hold what is in them (loop gain 1, no damping, no input): a frozen pad
 *            under whatever plays next; past half the knob it holds, below it the tail grows longer
 *     SMEAR  a deep, slow chorus and the longest, darkest reverb: a blur
 *     (FELT also reads BLOOM: warmer, more strings, eng_felt.c.) GLO + D#4 held freezes at once, as BREAK does.
 *   WEATHER (SKY knob 1, G_WEATHER): first tape wow and flutter (every synth part's pitch, slow and fast), then
 *     the record's hiss and dust, then the sampler's grit (fx.c dust_process, DUST rising with it)
 *   BREEZE (SKY knob 2, G_BREEZE): a slow random walk per track (a new target every 0.7..3 s, followed with a lag)
 *     moves its pitch (up to +-6 cents), its filter / tone and the delay's time a little: no loop plays twice alike
 * All per block, in the audio ISR. */
static struct {
    int32_t fast, slow;                  /* the reverb send's level: followers (per block) */
    int32_t swell;                       /* SWELL: the reverb's return, Q15 (ducked on an onset, swelling back) */
    int32_t in_g;                        /* the reverb's input gain, Q15 (FREEZE closes it, ramped) */
    uint32_t wow_ph, flut_ph;            /* WEATHER: the tape's wow and flutter phases */
    int32_t wow;                         /* the pitch offset of every synth part now, 1/4096 semitone */
    int32_t bz[NTRK], bz_tg[NTRK];       /* BREEZE: each track's walk and its target (Q15) */
    uint16_t bz_cnt[NTRK];               /* blocks to its next target */
    int32_t bz_rng;
    volatile uint8_t freeze_hold;        /* GLO + D#4 held (the UI) */
    uint8_t frozen;                      /* the reverb holds now */
} bot = {0, 0, 32767, 32767, 0, 0, 0, {0}, {0}, {0}, 0x1D872B41, 0, 0};

static int bot_freezing(void)
{
    return bot.freeze_hold || bot_brk || (song.g[G_BMODE] == 1 && song.g[G_BLOOM] >= 64);
}

/* per block (bot_seq.c bot_block, before the parts render): WEATHER's wow, BREEZE's walks */
static void bot_fx_block(void)
{
    int32_t w = song.g[G_WEATHER], b = song.g[G_BREEZE], i;
    {   /* wow: up to 18 cents at 0.55 Hz, flutter up to 4 cents at 7 Hz; it comes in over the first half of WEATHER */
        int32_t wd = (w > 64 ? 64 : w) * 737 / 64, fd = (w > 64 ? 64 : w) * 164 / 64;   /* 1/4096 semitone */
        bot.wow_ph += 53550u * CTL;                    /* 0.55 Hz: 2^32 x 0.55 / 44100 a sample */
        bot.flut_ph += 681600u * CTL;                  /* 7 Hz */
        bot.wow = w ? mulq15(osc_sine(bot.wow_ph), wd) + mulq15(osc_sine(bot.flut_ph), fd) : 0;
    }
    for (i = 0; i < (int32_t)NTRK; i++) {              /* BREEZE: each track's own walk */
        if (!b) {
            bot.bz[i] -= bot.bz[i] >> 6;               /* (off: back to the middle, gently) */
            continue;
        }
        if (!bot.bz_cnt[i]--) {
            uint32_t r = noise32(&bot.bz_rng);
            bot.bz_tg[i] = ((int32_t)(r >> 17) - 16384) * 2;   /* +-32767 */
            bot.bz_cnt[i] = (uint16_t)(960u + (r & 3071u));   /* 0.7 .. 3 s */
        }
        bot.bz[i] += (bot.bz_tg[i] - bot.bz[i]) >> 10;   /* a lag of ~0.7 s */
    }
}
/* a synth part's pitch offset (voice.c track_render), 1/4096 semitone: the tape's wow and the track's breeze */
static int32_t bot_pitch(const track_t *t)
{
    int32_t i = (int32_t)(t - trk) % (int32_t)NTRK;
    return bot.wow + (song.g[G_BREEZE] ? mulq15(bot.bz[i], song.g[G_BREEZE] * 246 / 127) : 0);   /* +-6 cents at 127 */
}
/* a synth part's filter / tone offset (0..127 << 8 units): the breeze, up to +-4 steps */
static int32_t bot_cutoff(const track_t *t)
{
    int32_t i = (int32_t)(t - trk) % (int32_t)NTRK;
    return song.g[G_BREEZE] ? mulq15(bot.bz[i], song.g[G_BREEZE] * 8) : 0;
}
/* the delay's time, + this many samples: the breeze of track 1, up to +-24 (a tape echo's drift) */
static int32_t bot_delay_wobble(void)
{
    return song.g[G_BREEZE] ? mulq15(bot.bz[0], song.g[G_BREEZE] * 24 / 127) : 0;
}

/* BLOOM's wash: the mix into the reverb (SWELL, SMEAR), up to -10 dB of it; mix_block, before the buses */
static void bot_bloom_send(int32_t *send, const int32_t *l, const int32_t *r, uint32_t n)
{
    int32_t k = song.g[G_BLOOM] * 80, i;               /* Q15: up to ~0.31 */
    if (!k || song.g[G_BMODE] == 1)
        return;
    for (i = 0; i < (int32_t)n; i++)
        send[i] += mulq15((l[i] + r[i]) >> 1, k);
}

/* the reverb for this block (fx.c fx_buses): its loop gain, damping, the chorus' depth, and the input / return
 * gains at both ends of the block (Q15, ramped by the caller) */
static void bot_bloom(const int32_t *rev_in, uint32_t n, int32_t *g, int32_t *lpk, int32_t *cdepth,
                      int32_t *in0, int32_t *in1, int32_t *ret0, int32_t *ret1)
{
    int32_t bl = song.g[G_BLOOM], mode = song.g[G_BMODE], e = 0, i, target;
    for (i = 0; i < (int32_t)n; i += 4)                /* the send's level (every 4th sample) */
        e += (rev_in[i] < 0 ? -rev_in[i] : rev_in[i]) >> 6;
    bot.fast += (e - bot.fast) >> 1;
    bot.slow += (e - bot.slow) >> 5;
    *ret0 = bot.swell;
    *in0 = bot.in_g;
    bot.frozen = (uint8_t)bot_freezing();
    if (bot.frozen) {                                  /* FREEZE: the lines hold what they have */
        *g = 32767;
        *lpk = 32767;
        target = 0;
    } else {
        target = 32767;
        if (bl) {
            *g += (32767 - *g) * bl / 160;             /* a longer tail with the knob */
            if (mode == 2) {                           /* SMEAR: the longest, darkest, a deep chorus */
                *g = 32767 - (32767 - *g) / 3;
                *lpk -= *lpk * bl / 400;
                *cdepth += bl * 6;
            }
        }
    }
    bot.in_g += (target - bot.in_g) >> 2;              /* (the input closes / opens over a few blocks) */
    if (bl && mode == 0 && !bot.frozen) {              /* SWELL: an onset ducks the return, then it swells */
        if (bot.fast > 2 * bot.slow + 64)
            bot.swell = 32767 - bl * 240;              /* down to ~7 % at 127 */
        else
            bot.swell += (32767 - bot.swell) / (12 + (bl >> 1)) + 1;   /* back over 0.25 .. 1.5 s */
    } else {
        bot.swell += (32767 - bot.swell) >> 3;
    }
    bot.swell = clamp(bot.swell, 0, 32767);
    *ret1 = bot.swell;
    *in1 = bot.in_g;
}

/* WEATHER's grit: the DUST it turns up (fx.c dust_process), past the first quarter of the knob */
static int32_t bot_weather_dust(void)
{
    int32_t w = song.g[G_WEATHER], d = w > 32 ? (w - 32) * 127 / 95 : 0;
    return d > song.g[G_DUST] ? d : song.g[G_DUST];
}
