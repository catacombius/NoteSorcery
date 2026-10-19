/* SPDX-License-Identifier: GPL-3.0-only */
/* BOTANICA's small botanical marks, kept quiet (ui_studio.c includes it): a leaf, a bud, a five-petal flower and a
 * vine rule, a few pixels each in the palette's dim colours, where NoteSorcery has a square, a bar or a plain line.
 * They never move or resize anything: the layout, the hit areas and the redraw caches are NoteSorcery's. */

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
