/* SPDX-License-Identifier: GPL-3.0-only */
/* GARDEN (BOTANICA's home picture, the first visualiser style; ui_vis.c includes it): what is playing, at a glance.
 *   - a night garden: the eight tracks are eight plants, each its own kind (moonflower, fern, tulip, lavender,
 *     dandelion, bluebell; the drum tracks mushrooms and cattails), as tall as the track is loud, in its colour,
 *     answering each note (opens, unfurls, lights up, sheds seeds, swings, glows, bobs); a silent track wilts, grey
 *   - the sky is night, stars twinkling with the beat under a crescent moon; the tempo warms the horizon a little;
 *     DRIFT draws its arrow
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

static void garden_flower(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint16_t heart, int scatter)
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
    vis_ellipse(cx, cy, r / 2 + 1, r / 2 + 1, heart);
}

/* the stem from the ground (x, GD_BASE) to the tip (x + lean, GD_BASE - h), in two pieces as a stalk bends */
#define GD_BASE 190
static void garden_stem(int32_t x, int32_t h, int32_t lean, uint16_t c)
{
    vis_thick(x, GD_BASE, x + lean / 2, GD_BASE - h / 2, c);
    vis_thick(x + lean / 2, GD_BASE - h / 2, x + lean, GD_BASE - h, c);
}

/* one plant: kind 0..7 (moonflower, fern, tulip, lavender, dandelion, bluebell, mushrooms, cattails); f the note's
 * flash 0..8; the colours: its own (col), its stem's, the sky's (for glows) */
static void garden_plant(uint32_t kind, int32_t x, int32_t h, int32_t lean, int32_t f, uint16_t col, uint16_t stem,
                         uint16_t sky, int quiet, int scatter)
{
    int32_t tx = x + lean, ty = GD_BASE - h, i;
    switch (kind) {
    case 0:                                              /* moonflower: a pale bloom that opens on the note */
        garden_stem(x, h, lean, stem);
        vis_ellipse(x + lean / 2 + 4, GD_BASE - h / 3, 3, 2, stem);
        garden_flower(tx, ty, quiet ? 3 : 6 + f / 2, quiet ? col : mix565(RGB(236, 236, 255), col, 90),
                      quiet ? col : RGB(255, 226, 120), scatter);
        break;
    case 1: {                                            /* fern: a frond of leaflets; the tip unfurls on the note */
        int32_t n = h / 7, curl = quiet ? 4 : 4 - f / 2;
        garden_stem(x, h, lean, stem);
        for (i = 1; i < n; i++) {
            int32_t py = GD_BASE - h * i / n, px = x + lean * i / n, len = 3 + (n - i) * 7 / n;
            uint16_t c = f > 0 && i * 8 >= n * (8 - f) ? col : stem;
            cv_line(px, py, px - len, py - len / 2, c);
            cv_line(px, py, px + len, py - len / 2, c);
        }
        if (curl > 0)
            vis_ellipse(tx + curl, ty, curl, curl, stem);
        break;
    }
    case 2: {                                            /* tulip: a cup of petals that opens on the note */
        int32_t o = quiet ? 0 : f / 2;
        garden_stem(x, h, lean, stem);
        vis_thick(x, GD_BASE, x - 6, GD_BASE - h / 3, stem);                 /* the long leaves */
        vis_thick(x, GD_BASE, x + 7, GD_BASE - h / 4, stem);
        vis_ellipse(tx, ty, quiet ? 3 : 5, quiet ? 4 : 6, col);
        vis_ellipse(tx - 4 - o, ty - 3 - o / 2, 2, 4, col);
        vis_ellipse(tx + 4 + o, ty - 3 - o / 2, 2, 4, col);
        break;
    }
    case 3: {                                            /* lavender: buds up a spike, lit from the bottom by the note */
        int32_t lit = quiet ? 0 : f * 7 / 8;
        garden_stem(x, h, lean, stem);
        for (i = 0; i < 7; i++) {
            int32_t py = ty + 4 + i * 3, px = tx - lean * i * 3 / (h + 1) + (i & 1 ? 2 : -2);
            vis_ellipse(px, py, 2, 2, quiet ? col : 7 - i <= lit ? mix565(RGB(190, 150, 255), col, 70) : RGB(118, 92, 186));
        }
        break;
    }
    case 4: {                                            /* dandelion: a seed puff; the note (or GLITCH) blows seeds */
        uint16_t seed = quiet ? col : mix565(RGB(230, 230, 240), col, 50);
        cv_line(x, GD_BASE, tx, ty, stem);
        vis_ellipse(x + 3, GD_BASE - 4, 4, 2, stem);
        for (i = 0; i < 12; i++) {
            uint32_t a = (uint32_t)i * 85u;
            cv_pset(tx + ((icos(a) * 7) >> 15), ty + ((isin(a) * 7) >> 15), seed);
            cv_pset(tx + ((icos(a) * 4) >> 15), ty + ((isin(a) * 4) >> 15), seed);
        }
        vis_ellipse(tx, ty, 2, 2, col);
        if (!quiet && (f > 0 || scatter))                /* three seeds drift up and away as the flash fades */
            for (i = 0; i < 3; i++) {
                int32_t up = (8 - f) * (5 + i * 2), sx = tx + i * 5 - 4 + ((isin(gd.frame * 20u + (uint32_t)i * 300u) * 3) >> 15);
                cv_pset(sx, ty - 6 - up, seed);
                cv_pset(sx + 1, ty - 7 - up, seed);
            }
        break;
    }
    case 5: {                                            /* bluebell: an arching stem, three bells that swing on the note */
        int32_t sw = quiet ? 0 : (isin(gd.frame * 40u) * f) >> 17;
        garden_stem(x, h, lean, stem);
        cv_line(tx, ty, tx + 10, ty + 4, stem);                              /* the arch */
        for (i = 0; i < 3; i++) {
            int32_t bx = tx + 2 + i * 4 + sw, by = ty + 4 + i * 2 + (i & 1);
            vis_ellipse(bx, by, 2, 3, col);
            cv_pset(bx - 2, by + 3, col);
            cv_pset(bx + 2, by + 3, col);
        }
        break;
    }
    case 6:                                              /* mushrooms: caps that glow on the hit */
        for (i = 0; i < 3; i++) {
            int32_t mh = (h / 4) * (3 - i) / 3 + 6, mx = x - 8 + i * 8 + (i == 1 ? lean / 4 : 0), r = 6 - i;
            if (f && !quiet)
                vis_ellipse(mx, GD_BASE - mh, r + 3 + f / 3, r / 2 + 3 + f / 3, mix565(sky, col, 50 + f * 12));
            cv_rect(mx - 1, GD_BASE - mh, 2, mh, quiet ? col : RGB(220, 210, 190));
            vis_ellipse(mx, GD_BASE - mh, r, r / 2 + 1, f ? mix565(col, RGB(255, 255, 255), f * 12) : col);
            cv_pset(mx - 2, GD_BASE - mh - 1, RGB(255, 250, 230));
        }
        break;
    default: {                                           /* cattails: reeds with brown heads that bob on the hit */
        int32_t bob = quiet ? 0 : f / 2;
        for (i = 0; i < 3; i++) {
            int32_t rh = h * (10 - i * 2) / 10, rx = x - 5 + i * 5, rt = rx + lean * (10 - i * 2) / 10 + (i == 1 ? bob : -bob / 2);
            cv_line(rx, GD_BASE, rt, GD_BASE - rh, stem);
            vis_ellipse(rt, GD_BASE - rh + 6, 2, 5, quiet ? col : mix565(RGB(120, 72, 40), col, 40));
            cv_line(rt, GD_BASE - rh, rt, GD_BASE - rh - 3, stem);
        }
        break;
    }
    }
}

/* the night: navy above, indigo at the horizon (the tempo warms it a little), the stars, a crescent moon */
static uint16_t garden_sky(int32_t y, uint16_t top, uint16_t low) { return mix565(top, low, y * 256 / 176); }

static void vis_garden(void)
{
    int32_t bpm = song.g[G_BPM], y, i, sway, wind = song.g[G_BREEZE];
    uint32_t bq = vis_beat_q8(), k;
    int32_t warm = clamp((bpm - 50) * 64 / 110, 0, 64);
    uint16_t top = RGB(6, 10, 32), low = mix565(RGB(30, 26, 76), RGB(96, 52, 120), warm);
    int frozen = bot.frozen, scatter = bseq.gl_fx >= 0;
    if (frozen) {                                        /* frost: the sky pales */
        top = mix565(top, RGB(220, 236, 255), 150);
        low = mix565(low, RGB(240, 248, 255), 150);
    }
    for (y = 0; y < 176; y += 8)                         /* the sky, in bands */
        cv_rect(0, y, 240, 8, garden_sky(y, top, low));
    if (song.g[G_WEATHER] <= 90)                         /* the stars (a downpour hides them), twinkling with the beat */
        for (k = 0; k < 48u; k++) {
            uint32_t h = k * 2654435761u;
            int32_t sx = (int32_t)(h % 240u), sy = (int32_t)((h >> 9) % 160u);
            int32_t tw = isin(bq + k * 137u) + 32768;      /* 0..65535 */
            uint16_t c = mix565(garden_sky(sy, top, low), RGB(255, 252, 232), 60 + (tw >> 9));
            cv_pset(sx, sy, c);
            if (k % 7u == 0u && tw > 40000) {             /* a few bright ones: a small cross */
                cv_pset(sx - 1, sy, c);
                cv_pset(sx + 1, sy, c);
                cv_pset(sx, sy - 1, c);
                cv_pset(sx, sy + 1, c);
            }
        }
    vis_ellipse(206, 30, 12, 12, frozen ? RGB(255, 255, 255) : RGB(240, 234, 206));   /* the moon: a crescent */
    vis_ellipse(212, 25, 11, 11, garden_sky(25, top, low));
    if (frozen)
        for (k = 0; k < 40u; k++)                        /* frost glints, still */
            cv_pset((int32_t)((k * 97u) % 240u), (int32_t)((k * 53u) % 170u), RGB(255, 255, 255));
    /* the ground and the pond (the moon in it) */
    cv_rect(0, 176, 240, 64, RGB(18, 40, 28));
    vis_ellipse(120, 212, 112, 14, RGB(20, 40, 70));
    vis_ellipse(206, 210, 6, 2, RGB(90, 96, 110));
    for (k = 0; k < GD_RIPPLES; k++)                     /* the ripples */
        if (gd.rip[k].life) {
            uint16_t c = mix565(RGB(20, 40, 70), TRK_COL[gd.rip[k].trk % NTRK], gd.rip[k].life * 10);
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
        vis_thick(i, 196, i + sway + ((i / 4) % 3) - 1, 196 - h, RGB(46, 104, 58));
    }
    /* the plants: one kind a track */
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        int32_t x = garden_x(k), h = 26 + gd.lvl[k] * 100 / 1000, lean = (sway * h) / 60;
        int quiet = trk_silent(t), f = quiet ? 0 : gd.flash[k];
        uint16_t stem = quiet ? RGB(90, 96, 80) : RGB(56, 156, 76), col = quiet ? RGB(120, 120, 120) : TRK_COL[k];
        uint32_t kind = is_drum(t) ? 6u + (k + 1u >= NTRK) : k % 6u;
        if (quiet) {
            h = 18;
            lean = 6;
        }
        if (song.g[G_BLOOM] && !quiet)                   /* BLOOM: a glow round the top */
            vis_ellipse(x + lean, GD_BASE - h, 8 + song.g[G_BLOOM] / 24, 8 + song.g[G_BLOOM] / 24,
                        mix565(garden_sky(GD_BASE - h, top, low), col, 16 + song.g[G_BLOOM] / 2));
        garden_plant(kind, x, h, lean, f, col, stem, garden_sky(GD_BASE - h, top, low), quiet, scatter && !quiet);
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
