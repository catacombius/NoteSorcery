/* SPDX-License-Identifier: GPL-3.0-only */
/* GARDEN (BOTANICA's home picture, the first visualiser style; ui_vis.c includes it): what is playing, at a glance.
 *   - the eight tracks are eight plants: a stem as tall as the track is loud, a flower in its colour that opens on
 *     each note (a muted or silent track wilts, grey)
 *   - the sky follows the tempo, a night blue at 50 BPM to a warm pink at 160 and up; DRIFT draws its arrow
 *   - WEATHER rains, BREEZE sways the grass and the stems, BLOOM glows round the flowers, a freeze (BMODE FREEZE,
 *     GLO + D#4, BREAK) frosts the sky
 *   - DROPLET notes and FOLEY hits drop ripples into the pond at the foot of their plant; GLITCH scatters petals
 * Drawn in the main loop, every other frame, in ui_vis.c's two bands of 120 rows. */
#define GD_RIPPLES 8u
static struct {
    int16_t lvl[NTRK];                   /* each track's level, 0..1000, smoothed */
    uint8_t flash[NTRK];                 /* frames since its note (8 = now) */
    uint8_t hit;                         /* the tracks a note started on since the last frame */
    struct { int16_t x, r; uint8_t life, trk; } rip[GD_RIPPLES];
    uint32_t frame;
    int32_t rng;
} gd = {.rng = 0x51ED270B};

/* the tracks' peaks and notes (vis_update, which takes them for the other pictures too) */
static void garden_take(uint32_t i, int32_t peak, int hit)
{
    int32_t v = vis_lvl(peak);
    gd.lvl[i] = (int16_t)(v > gd.lvl[i] ? v : gd.lvl[i] - (gd.lvl[i] - v) / 5);
    if (hit) {
        gd.flash[i] = 8;
        gd.hit |= (uint8_t)(1u << i);
    } else if (gd.flash[i]) {
        gd.flash[i]--;
    }
}

static int32_t garden_x(uint32_t i) { return 16 + (int32_t)i * 30; }

/* a track whose notes ripple the pond: DROPLET, NOISE in FOLEY mode, the drum tracks */
static int garden_rippler(uint32_t i)
{
    const track_t *t = &trk[i];
    return is_drum(t) || t->engine == ENGI_DROPLET || (t->engine == ENGI_NOISE && t->p[P_E0] == NZ_FOLEY);
}

static void garden_update(void)
{
    uint32_t i, k;
    gd.frame++;
    for (i = 0; i < NTRK; i++)
        if (((gd.hit >> i) & 1u) && garden_rippler(i))
            for (k = 0; k < GD_RIPPLES; k++)
                if (!gd.rip[k].life) {
                    gd.rip[k].x = (int16_t)(garden_x(i) + (int32_t)(noise32(&gd.rng) & 15u) - 8);
                    gd.rip[k].r = 2;
                    gd.rip[k].life = 24;
                    gd.rip[k].trk = (uint8_t)i;
                    break;
                }
    gd.hit = 0;
    for (k = 0; k < GD_RIPPLES; k++)
        if (gd.rip[k].life) {
            gd.rip[k].life--;
            gd.rip[k].r = (int16_t)(gd.rip[k].r + 1);
        }
}

static void garden_flower(int32_t cx, int32_t cy, int32_t r, uint16_t c, int scatter)
{
    uint32_t a;
    for (a = 0; a < 1024u; a += 205u) {                  /* five petals */
        int32_t dx = (icos(a + gd.frame * 3u) * r) >> 15, dy = (isin(a + gd.frame * 3u) * r) >> 15;
        if (scatter) {
            dx += (int32_t)(noise32(&gd.rng) % 9u) - 4;
            dy += (int32_t)(noise32(&gd.rng) % 9u) - 4;
        }
        vis_ellipse(cx + dx, cy + dy, r * 2 / 3 + 1, r * 2 / 3 + 1, c);
    }
    vis_ellipse(cx, cy, r / 2 + 1, r / 2 + 1, RGB(255, 226, 120));   /* the heart: pollen */
}

static void vis_garden(void)
{
    int32_t bpm = song.g[G_BPM], y, i, sway, wind = song.g[G_BREEZE];
    uint32_t bq = vis_beat_q8(), k;
    int32_t warm = clamp((bpm - 50) * 256 / 110, 0, 256);
    uint16_t top = mix565(RGB(10, 16, 48), RGB(255, 132, 150), warm), low = mix565(RGB(30, 44, 90), RGB(255, 206, 160), warm);
    int frozen = bot.frozen, scatter = bseq.gl_fx >= 0;
    if (frozen) {                                        /* frost: the sky pales */
        top = mix565(top, RGB(220, 236, 255), 150);
        low = mix565(low, RGB(240, 248, 255), 150);
    }
    for (y = 0; y < 176; y += 8)                         /* the sky, in bands */
        cv_rect(0, y, 240, 8, mix565(top, low, y * 256 / 176));
    if (frozen)
        for (k = 0; k < 40u; k++)                        /* frost glints, still */
            cv_pset((int32_t)((k * 97u) % 240u), (int32_t)((k * 53u) % 170u), RGB(255, 255, 255));
    /* the ground and the pond */
    cv_rect(0, 176, 240, 64, RGB(26, 58, 34));
    vis_ellipse(120, 212, 112, 14, RGB(30, 70, 96));
    for (k = 0; k < GD_RIPPLES; k++)                     /* the ripples */
        if (gd.rip[k].life) {
            uint16_t c = mix565(RGB(30, 70, 96), TRK_COL[gd.rip[k].trk % NTRK], gd.rip[k].life * 10);
            int32_t r = gd.rip[k].r, x;
            for (x = -r; x <= r; x++) {
                int32_t h = (r * r - x * x) > 0 ? 1 + (r * r - x * x) / (r * 6 + 1) : 0;
                cv_pset(gd.rip[k].x + x, 212 - h / 3, c);
                cv_pset(gd.rip[k].x + x, 212 + h / 3, c);
            }
        }
    /* the grass, swaying with the beat and the breeze */
    sway = (isin(bq * 2u) * (2 + wind / 24)) >> 15;
    for (i = 0; i < 240; i += 4) {
        int32_t h = 6 + (int32_t)((i * 7) % 5);
        vis_thick(i, 196, i + sway + ((i / 4) % 3) - 1, 196 - h, RGB(70, 140, 70));
    }
    /* the plants */
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        int32_t x = garden_x(k), h = 26 + gd.lvl[k] * 100 / 1000, lean = (sway * h) / 60, fy = 190 - h;
        int quiet = trk_silent(t), r = 5 + (gd.flash[k] ? gd.flash[k] / 2 + 2 : 0);
        uint16_t stem = quiet ? RGB(90, 96, 80) : RGB(60, 170, 80), col = quiet ? RGB(120, 120, 120) : TRK_COL[k];
        if (quiet) {
            h = 18;
            fy = 190 - h;
            lean = 6;
        }
        vis_thick(x, 190, x + lean / 2, 190 - h / 2, stem);
        vis_thick(x + lean / 2, 190 - h / 2, x + lean, fy, stem);
        vis_ellipse(x + lean / 2 + 4, 190 - h / 3, 3, 2, stem);            /* a leaf */
        if (song.g[G_BLOOM] && !quiet)                                   /* BLOOM: a glow */
            vis_ellipse(x + lean, fy, r + 4 + song.g[G_BLOOM] / 24, r + 4 + song.g[G_BLOOM] / 24,
                        mix565(top, col, 40 + song.g[G_BLOOM]));
        garden_flower(x + lean, fy, quiet ? 3 : r, col, scatter && !quiet);
    }
    /* the rain: WEATHER */
    if (song.g[G_WEATHER]) {
        uint32_t n = (uint32_t)song.g[G_WEATHER] / 4u + 2u;
        for (k = 0; k < n; k++) {
            int32_t x = (int32_t)((k * 131u + gd.frame * 3u) % 240u), y0 = (int32_t)((k * 71u + gd.frame * 11u) % 200u);
            vis_thick(x, y0, x - 2, y0 + 7, RGB(170, 200, 255));
        }
    }
    /* the tempo, and where DRIFT takes it */
    {
        char b[12];
        fmt_int(b, bpm);
        cv_text(4, 222, &FONT_S, b, RGB(220, 236, 210));
        if (song.g[G_DRIFT] && song.g[G_DRIFT] != bpm) {
            char d[12];
            fmt_int(d, song.g[G_DRIFT]);
            cv_text(36, 222, &FONT_S, song.g[G_DRIFT] > bpm ? ">" : "<", TE_COL[2]);
            cv_text(48, 222, &FONT_S, d, TE_COL[2]);
        }
        if (bot_brk)
            cv_text(236 - text_w(&FONT_S, bot_brk == 1u ? "BREAK" : "DROP"), 222, &FONT_S, bot_brk == 1u ? "BREAK" : "DROP", TE_COL[0]);
    }
}
