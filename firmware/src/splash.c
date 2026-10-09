/* SPDX-License-Identifier: GPL-3.0-only */
/* Boot splash: the icon (tools/gen_logo.py -> sloop_logo.h: 16 colours, RLE runs of (run - 1) << 4 | colour; the
 * four-colour sail of SLOOP, the OP-1's knob colours), drawn into the canvas in bands of at most 120 rows, and
 * NoteSorcery's name in the large type. */
#include "sloop_logo.h"
static void sloop_logo_draw(uint32_t y0)
{
    uint32_t r0;
    for (r0 = 0; r0 < SLOOP_SPLASH_H; r0 += 120u) {
        uint32_t rows = SLOOP_SPLASH_H - r0 > 120u ? 120u : SLOOP_SPLASH_H - r0;
        uint32_t lo = r0 * SLOOP_SPLASH_W, hi = (r0 + rows) * SLOOP_SPLASH_W, pos = 0, i;
        cv_begin(SLOOP_SPLASH_W, rows, 0x0000u);
        for (i = 0; i < sizeof SLOOP_SPLASH_RLE && pos < hi; i++) {
            uint32_t run = (SLOOP_SPLASH_RLE[i] >> 4) + 1u, c = SLOOP_SPLASH_RLE[i] & 15u, p;
            if (c && pos + run > lo)
                for (p = pos < lo ? lo : pos; p < pos + run && p < hi; p++)
                    cv_px[p - lo] = swap16(SLOOP_SPLASH_PAL[c]);
            pos += run;
        }
        cv_blit((240u - SLOOP_SPLASH_W) / 2u, y0 + r0);
    }
}

/* the boot screen: the icon, the name, the version, where it comes from */
static void sloop_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sloop_logo_draw(4);
    const char *v = FELUCCA_VERSION;
    if (v[0] == 'N' && v[1] == 'O' && v[11] == ' ')
        v += 12;                                        /* "NOTESORCERY 0.1" -> "0.1" under the name */
    cv_begin(240, 82, C_BLACK);
    cv_text(120 - text_w(&FONT_L, "NOTESORCERY") / 2, 0, &FONT_L, "NOTESORCERY", RGB(242, 242, 242));
    cv_text(120 - text_w(&FONT_S, v) / 2, 36, &FONT_S, v, RGB(196, 196, 204));
    cv_text(120 - text_w(&FONT_S, "sloop + felucca") / 2, 56, &FONT_S, "sloop + felucca", RGB(96, 96, 104));
    cv_blit(0, 156);
}
