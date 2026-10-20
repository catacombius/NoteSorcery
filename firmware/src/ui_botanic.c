/* SPDX-License-Identifier: GPL-3.0-only */
/* BOTANICA's look (ui_studio.c includes it): pixel-art plants where NoteSorcery has squares, bars and rings.
 * - marks: a leaf, a bud, a five-petal flower, a vine rule, a sprig;
 * - sprites: 12 x 12 pixel art drawn as text below ('.' clear, a character a colour of BOT_P), one per engine and
 *   per mood of the browser, a bee (the playhead), a leaf (a track's badge); [tint] colours x / y / z (dark, main,
 *   light) are the caller's, a track's or a knob's;
 * - widgets: the flower dial (petals light up with the value, a stem below), the vine gauge, a step as a seed /
 *   sprout / bloom.
 * The layout, the hit areas and the redraw caches stay NoteSorcery's: only what is drawn in them differs. */

/* the sprites' fixed colours ('1'..'9', 'a'..'h'): outline, leaf dark / mid / light, petal dark / mid / light,
 * pollen, water dark / mid / light, wood dark / mid, cream, violet, cloud, soil; scaled with BRIGHT */
static const uint16_t BOT_P0[17] = {
    RGB(16, 30, 22), RGB(46, 110, 60), RGB(96, 184, 92), RGB(170, 224, 120), RGB(190, 70, 120), RGB(255, 128, 170),
    RGB(255, 200, 220), RGB(255, 210, 70), RGB(40, 100, 190), RGB(90, 170, 255), RGB(200, 235, 255), RGB(96, 58, 34),
    RGB(150, 98, 56), RGB(244, 236, 214), RGB(170, 130, 240), RGB(150, 160, 170), RGB(70, 46, 30)};
static uint16_t BOT_P[17];
static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c);   /* (ui_studio.c) */
#define BOT_POLLEN BOT_P[7]
#define BOT_SOIL BOT_P[16]
#define BOT_LEAF BOT_P[2]

static uint16_t bot_mix(uint32_t a, uint32_t b, uint32_t q8)    /* a .. b, q8 0 .. 256 */
{
    uint32_t r = (((a >> 11) & 31u) * (256u - q8) + ((b >> 11) & 31u) * q8) >> 8;
    uint32_t g = (((a >> 5) & 63u) * (256u - q8) + ((b >> 5) & 63u) * q8) >> 8;
    uint32_t l = ((a & 31u) * (256u - q8) + (b & 31u) * q8) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | l);
}

/* a sprite w x h from its text; tint: the colours of 'x', 'y', 'z' (or 0) */
static void bot_spr(int32_t x, int32_t y, int32_t w, int32_t h, const char *art, const uint16_t *tint)
{
    int32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            char ch = art[j * w + i];
            uint16_t c;
            if (ch == '.')
                continue;
            if (ch >= '1' && ch <= '9')
                c = BOT_P[ch - '1'];
            else if (ch >= 'a' && ch <= 'h')
                c = BOT_P[9 + ch - 'a'];
            else if (tint && ch >= 'x' && ch <= 'z')
                c = tint[ch - 'x'];
            else
                continue;
            cv_pset(x + i, y + j, c);
        }
}

/* a leaf 4 x 3, its base at (x, y), pointing right (dir > 0) or left */
static void bot_leaf(int32_t x, int32_t y, int dir, uint16_t c)
{
    int32_t s = dir > 0 ? 1 : -1;
    cv_rect(dir > 0 ? x : x - 3, y, 4, 1, c);
    cv_pset(x + s, y - 1, c);
    cv_pset(x + 2 * s, y - 1, c);
    cv_pset(x + s, y + 1, c);
}

/* a bud: 3 x 3 without its lower corners, on a 1 px stalk of [stalk] pixels below it */
static void bot_bud(int32_t cx, int32_t cy, int32_t stalk, uint16_t c)
{
    cv_rect(cx - 1, cy - 1, 3, 2, c);
    cv_pset(cx, cy + 1, c);
    if (stalk > 0)
        cv_rect(cx, cy + 2, 1, stalk, c);
}

/* five petals round a heart, 5 px across */
static void bot_flower5(int32_t cx, int32_t cy, uint16_t c, uint16_t heart)
{
    cv_pset(cx, cy - 2, c);
    cv_pset(cx - 2, cy - 1, c);
    cv_pset(cx + 2, cy - 1, c);
    cv_pset(cx - 1, cy + 2, c);
    cv_pset(cx + 1, cy + 2, c);
    cv_rect(cx - 1, cy - 1, 3, 3, c);
    cv_pset(cx, cy, heart);
}

/* a rule of [w] px at row y with a leaf now and then above it, alternately leaning right and left */
static void bot_vine(int32_t y, int32_t w, uint16_t rule, uint16_t leaf)
{
    int32_t x, k = 0;
    cv_rect(0, y, w, 1, rule);
    for (x = 30; x < w - 6; x += 60, k++)
        bot_leaf(x, y - 2, k & 1 ? -1 : 1, leaf);
}

/* a sprig: a short stem with two leaves, for the ends of a message bar */
static void bot_sprig(int32_t x, int32_t y, int dir, uint16_t c)
{
    cv_rect(x, y - 4, 1, 9, c);
    bot_leaf(x + dir, y - 2, dir, c);
    bot_leaf(x - dir, y + 2, -dir, c);
}

/* ------------------------------------------------------------------ sprites --- */
static const char SPR_FELT[] =
    "......344..."
    ".....3443..."
    ".....23....."
    ".....2......"
    "ee111e111ee."
    "ee111e111ee."
    "ee111e111ee."
    "ee111e111ee."
    "eee.eee.eee."
    "eee.eee.eee."
    "eee.eee.eee."
    "ggg.ggg.ggg.";
static const char SPR_DROPLET[] =
    ".....11....."
    "....1ba1...."
    "...1baa91..."
    "...1baaa1..."
    "..1baaaa91.."
    "..1aaaaa91.."
    "..1aaaa991.."
    "...1a9991..."
    "....1111...."
    "..56666665.."
    ".5667777665."
    "..55555555..";
static const char SPR_ANALOG[] =
    "..44........"
    ".4432......."
    "..3333......"
    ".33..33....."
    "33....33...."
    "3......33..3"
    "........33.3"
    ".........333"
    ".......23..."
    "......2443.."
    ".......44..."
    "............";
static const char SPR_SAMPLE[] =
    ".dddddddddd."
    "dccccccccccd"
    "deeeeeeeeeed"
    "de...34...ed"
    "de..3443..ed"
    "de...23...ed"
    "de....2...ed"
    "de..2.2...ed"
    "de...22...ed"
    "deeeeeeeeeed"
    "de.h.h.h.hed"
    ".dddddddddd.";
static const char SPR_NOISE[] =
    "............"
    "gggggg......"
    "......gg...."
    "..ggggg.g..."
    "........g..."
    ".gggg..g...."
    "............"
    "3..3...2..3."
    "3.33..32.33."
    "233.3.2.233."
    "2223322.222."
    "hhhhhhhhhhhh";
static const char SPR_PHYS[] =
    "...1331....."
    "...1341..44."
    "...2222.443."
    "...13312332."
    "...13412...."
    "...1341....."
    "...2222....."
    "...1331....."
    ".441341....."
    "3442341....."
    ".3322221...."
    "...1331.....";
static const char SPR_GRAIN[] =
    "..e..e..e..."
    "....eee....."
    ".e.eeeee.e.."
    "..eeeeeee..."
    "e.eeeeeee.e."
    "..eeeeeee..."
    ".e.eeeee.e.."
    "....e2e....."
    "..e..2..e..."
    ".....2......"
    "....32......"
    "....2.......";
static const char SPR_VOICE[] =
    "............"
    "....1111...."
    "...166661..."
    "..16616661.."
    "..1666666118"
    "...16666188."
    "..1556661..."
    ".15566661..."
    "1555566661.."
    "..1111111..."
    "....1..1...."
    "...11.11....";
static const char SPR_WAVE[] =
    "....3333...."
    "...344443..."
    "..34....43.."
    "..3..33..3.."
    "..3.3..3.3.."
    "..3.3.4..3.."
    "..3..3..34.."
    "...3...34..."
    "....3332...."
    "......2....."
    ".....32....."
    ".....2......";
static const char SPR_DRUM[] =
    ".....c......"
    "....cdc....."
    "..cddddcc..."
    ".cdhdhdhdc.."
    ".chdhdhdhc.."
    "..cccccccc.."
    "..dddddddd.."
    "..d8dd8ddd.."
    "..dddddddd.."
    "...dd8ddd..."
    "....dddd...."
    ".....dd.....";
static const char SPR_PETL[] =
    "....7......."
    "...767.66..."
    "..6766766..."
    "...66886...."
    "..66888667.."
    ".7667886766."
    "..66..66...."
    "......2....."
    "..33..2....."
    ".3443.2....."
    "..3322......"
    "......2.....";
static const char SPR_CANP[] =
    "....3333...."
    "..33444433.."
    ".3344443343."
    ".3444433333."
    "333433334333"
    "333333333333"
    ".2333332332."
    "..22233222.."
    ".....dc....."
    ".....dc....."
    ".....dc....."
    "...hhddhh...";
static const char SPR_ROOT[] =
    "....3..3...."
    ".....33....."
    "....3443...."
    "hhhhhhhhhhhh"
    "...d88d....."
    "...d888d...."
    "....d88d...."
    "..c.d88d.c.."
    "...c.d8dc..."
    "..c..d8d...."
    "......dc...."
    "......c.....";
static const char SPR_WTHR[] =
    "............"
    "....ggg....."
    "..ggeeegg..."
    ".geeeeeeeg.."
    "geeeeeeeeeg."
    ".gggggggggg."
    "............"
    "..a...a...a."
    ".a...a...a.."
    "............"
    "...a...a...."
    "..a...a.....";
static const char SPR_HYPR[] =
    ".....8......"
    ".....8......"
    "....888....."
    "8888888888.."
    "..8888888..."
    "...88888.f.."
    "...88.88.f.."
    "..8.....fff."
    "..........f."
    ".f......f..."
    "fff........."
    ".f..........";
static const char SPR_BEE[] =
    ".bb.bb"
    "..bbb."
    ".8c8c8"
    "c8c8c8"
    ".8c8c.";
static const char SPR_LEAF16[] =
    "..........xxxxx."
    "........xxyyyyx."
    "......xxyyyyyyx."
    ".....xyyyyyyyyx."
    "....xzyyyyyyyyx."
    "...xzyyyyyyyyyx."
    "..xzzyyyyyyyyx.."
    "..xzyyyyyyyyyx.."
    ".xzyyyyyyyyyx..."
    ".xzyyyyyyyyyx..."
    ".xzyyyyyyyyx...."
    ".xzyyyyyyyx....."
    ".xyyyyyyxx......"
    "..xxyyxx........"
    "..x.xx.........."
    ".x..............";

/* the sprite of an engine (ENGINES[]->name, or "DRUM") and of a mood of the browser (ui.c BANK_KIND) */
static const char *bot_engine_spr(const char *name)
{
    static const struct { const char *name, *art; } M[] = {
        {"FELT", SPR_FELT}, {"DROPLET", SPR_DROPLET}, {"ANALOG", SPR_ANALOG}, {"SAMPLE", SPR_SAMPLE},
        {"NOISE", SPR_NOISE}, {"PHYS", SPR_PHYS}, {"GRAIN", SPR_GRAIN}, {"VOICE", SPR_VOICE}, {"WAVE", SPR_WAVE},
        {"DRUM", SPR_DRUM},
        {"PETL", SPR_PETL}, {"CANP", SPR_CANP}, {"DEW", SPR_DROPLET}, {"VOIC", SPR_VOICE}, {"ROOT", SPR_ROOT},
        {"WTHR", SPR_WTHR}, {"HYPR", SPR_HYPR},
    };
    uint32_t i;
    for (i = 0; i < sizeof M / sizeof M[0]; i++)
        if (str_eq(name, M[i].name))
            return M[i].art;
    return 0;
}
static int bot_icon(int32_t x, int32_t y, const char *name)     /* 12 x 12; 0: none for that name */
{
    const char *a = bot_engine_spr(name);
    if (a)
        bot_spr(x, y, 12, 12, a, 0);
    return a != 0;
}

/* a track's badge: a leaf 16 x 16 with its number; tint dark, main, light */
static void bot_badge(int32_t x, int32_t y, const uint16_t *tint, char n, uint16_t ink)
{
    char s[2] = {n, 0};
    bot_spr(x, y, 16, 16, SPR_LEAF16, tint);
    cv_text(x + 7 - text_w(&FONT_S, s) / 2, y, &FONT_S, s, ink);       /* (twice: bold) */
    cv_text(x + 8 - text_w(&FONT_S, s) / 2, y, &FONT_S, s, ink);
}

/* the flower dial: seven petals round the heart over the knob's 270 degrees (lower left, clockwise, to lower
 * right), lit up to the value, a petal half lit on the way; a white dot where the value is; a stem in the gap
 * below. ratio 0..1000, -1: all petals closed (a plain dial). c / mid / dim: the knob's colours */
static void bot_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t mid, uint16_t dim)
{
    int32_t x, y, i, d = r * 5 / 8, ra = r * 3 / 8 + 1, rb = r / 4 + 1;
    int32_t lit = ratio < 0 ? 0 : ratio * 14 / 1000;               /* half petals: 0 .. 14 */
    uint16_t hi = bot_mix(c, RGB(255, 255, 255), 110), shut = bot_mix(dim, C_BLACK, 96);
    int32_t pc[7], ps[7];
    for (i = 0; i < 7; i++) {                                       /* petal i at 135 + 45 i degrees */
        uint32_t a = (uint32_t)(384 + i * 128) & 1023u;
        pc[i] = SINE[(a + 256u) & 1023u];
        ps[i] = SINE[a];
    }
    cv_rect(cx, cy + 2, 1, r - 1, BOT_P[1]);                        /* the stem, two leaves */
    cv_rect(cx - 3, cy + r - 3, 3, 1, BOT_LEAF);
    cv_pset(cx - 2, cy + r - 4, BOT_LEAF);
    cv_rect(cx + 1, cy + r - 2, 3, 1, BOT_LEAF);
    cv_pset(cx + 2, cy + r - 3, BOT_LEAF);
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            for (i = 0; i < 7; i++) {
                int32_t px = x * 256 - ((pc[i] * d) >> 7), py = y * 256 - ((ps[i] * d) >> 7);
                int32_t u = (px * pc[i] + py * ps[i]) >> 15, v = (py * pc[i] - px * ps[i]) >> 15;   /* along, across */
                int32_t e = (u * u) / (ra * ra) + (v * v) / (rb * rb);   /* 0 at its middle, 65536 at its edge */
                uint16_t col;
                if (e > 65536)
                    continue;
                if (2 * i + 2 <= lit)
                    col = e > 36000 ? mid : u < -ra * 64 && e < 30000 ? hi : c;
                else if (2 * i + 1 <= lit)
                    col = e > 36000 ? mid : dim;
                else
                    col = e > 36000 ? dim : shut;
                cv_pset(cx + x, cy + y, col);
                break;
            }
    te_disc(cx, cy, r / 4, ratio >= 1000 ? C_WHITE : BOT_POLLEN);   /* the heart */
    cv_pset(cx - 1, cy - 1, bot_mix(BOT_POLLEN, RGB(255, 255, 255), 160));
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + ratio * 768 / 1000) & 1023u;
        int32_t vx = cx + ((SINE[(a + 256u) & 1023u] * r) >> 15), vy = cy + ((SINE[a] * r) >> 15);
        cv_rect(vx - 1, vy - 1, 2, 2, C_WHITE);
    }
}

/* a gauge as a vine: seeds along the row, the stem grown to fx with a leaf every 5 px, above and below in
 * turn, a bud at its end (a flower when full); rows y-1 .. y+3 */
static void bot_gauge(int32_t x0, int32_t y, int32_t w, int32_t fx, uint16_t stem, uint16_t leaf, uint16_t bud, int full)
{
    int32_t x;
    for (x = 1; x < w; x += 4)
        cv_pset(x0 + x, y + 2, C_LINE);
    cv_rect(x0, y + 1, fx, 1, stem);
    for (x = 3; x + 2 < fx; x += 5) {
        int up = (x / 5) & 1;
        cv_rect(x0 + x, up ? y : y + 2, 2, 1, leaf);
        cv_pset(x0 + x + (up ? 1 : 0), up ? y - 1 : y + 3, leaf);
    }
    if (full)
        bot_flower5(x0 + fx, y + 1, bud, BOT_POLLEN);
    else
        bot_bud(x0 + fx, y, 1, bud);
}

/* a step in a bed (x, y: the top left of a 5 x 12 cell): 0 none, 1 a seed, 2 a sprout, 3 a bloom (playing) */
static void bot_bed(int32_t x, int32_t y, uint32_t kind, const uint16_t *tint)
{
    static const char SPROUT[] = "y...y" "zy.yz" ".yxy." "..x.." "..x.." "..x..";
    static const char BLOOM[] = ".y.y." "yyzyy" ".z8z." "yyzyy" ".y.y." "..x..";
    if (kind == 1u)
        cv_rect(x + 2, y + 9, 1, 1, BOT_P[12]);
    else if (kind >= 2u)
        bot_spr(x, y + 2, 5, 6, kind == 3u ? BLOOM : SPROUT, tint);
}

/* the playhead: a bee 6 x 5 */
static void bot_bee(int32_t x, int32_t y) { bot_spr(x, y, 6, 5, SPR_BEE, 0); }

/* a box with its corners rounded off (a pad, a badge) */
static void bot_pill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint16_t shine)
{
    cv_rect(x + 1, y, w - 2, h, c);
    cv_rect(x, y + 1, 1, h - 2, c);
    cv_rect(x + w - 1, y + 1, 1, h - 2, c);
    if (shine != c && h > 3)
        cv_rect(x + 2, y + 1, w - 4 > 4 ? 3 : 1, 1, shine);
}

static void bot_colors(uint32_t q8)                     /* (studio_colors) */
{
    uint32_t k;
    for (k = 0; k < sizeof BOT_P / sizeof BOT_P[0]; k++)
        BOT_P[k] = rgb_scale(BOT_P0[k], q8);
}
