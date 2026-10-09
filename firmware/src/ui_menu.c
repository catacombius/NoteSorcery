/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLOOP menu (HOME held), in sections as the pages are (2.4): SCREEN (COLOR, ZOOM; NoteSorcery: BRIGHT, NIGHT),
 * LIGHTS (LIGHTS, KEYS, NOTES),
 * AUDIO (LOWCUT, USB AUDIO, USB SERIAL), SYSTEM (HARDWARE CALIBRATION, ABOUT). SELECT goes to the section
 * before / after (stopping at the ends), KNOB 1..3 set the section's settings in order (the knob's colour marks
 * its row), PRESETS moves the cursor; OCT+ steps the cursor's setting round or opens it (CALIBRATION, ABOUT),
 * OCT- closes (from ABOUT: back to the section). */
/* ------------------------------------------------------------ menu --- */
enum { MI_COLOR, MI_ZOOM, MI_BRIGHT, MI_NIGHT, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_LOWCUT, MI_USB, MI_SERIAL, MI_USBSMP,
       MI_PANEL, MI_ABOUT, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "ZOOM", "BRIGHT", "NIGHT", "LIGHTS", "KEYS", "NOTES",
                                              "SPEAKER LOWCUT", "USB AUDIO", "USB SERIAL", "USB SAMPLE",
                                              "HARDWARE CALIBRATION", "ABOUT"};
/* NoteSorcery: the USB SAMPLE screen (ui.menu 3; usb_sample.c records, usb.c receives) */
static int usmp_start(uint32_t k);
static void usmp_stop(void);
static uint32_t usmp_ui(uint32_t *n, int32_t *peak);
static uint32_t usmp_state(void);
static int usb_smp_streaming(void);
static uint8_t usmp_slot_ui;                       /* the slot the next take goes to, 0..3 */
static const char *const USMP_STATE[] = {"READY", "ARMED", "REC", "SAVING", "SAVED", "FAILED"};
enum { MS_SCREEN, MS_LIGHTS, MS_AUDIO, MS_SYSTEM, MS_COUNT };
static const char *const MS_NAME[MS_COUNT] = {"SCREEN", "LIGHTS", "AUDIO", "SYSTEM"};   /* (AUDIO: and USB) */
static const uint8_t MS_FIRST[MS_COUNT + 1] = {MI_COLOR, MI_LIGHTS, MI_LOWCUT, MI_PANEL, MI_COUNT};   /* rows of each */
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH"};   /* every button lit, the labels readable */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS", "ALL KEYS"};      /* keys lit too, at the LIGHTS level */
#define MI_Y0 26                                   /* the first row, under the section tabs */
#define MI_DY 50                                   /* a row: the label, the value in large type */
#define MI_DY4 44                                  /* (a section of four rows: SCREEN) */

static uint32_t mi_sec(uint32_t i)                 /* the section of item i */
{
    uint32_t k = 0;
    while (k + 1u < MS_COUNT && i >= MS_FIRST[k + 1u])
        k++;
    return k;
}
static uint32_t mi_row(uint32_t i) { return i - MS_FIRST[mi_sec(i)]; }   /* its row: KNOB 1 + row sets it */

/* item i's value as shown, and its colour */
static const char *mi_value(uint32_t i, uint16_t *c)
{
    *c = C_HI;
    switch (i) {
    case MI_COLOR: return PALETTES[settings.palette].name;
    case MI_ZOOM: return settings.zoom ? "ON" : "OFF";
    case MI_BRIGHT: return BRIGHT_NAME[scr_bright % NBRIGHT];
    case MI_NIGHT: return night_on ? "ON" : "OFF";
    case MI_LIGHTS: return LIGHTS_NAME[lights_lvl % LIGHTS_N];
    case MI_KEYS:
        *c = lights_lvl ? C_HI : C_DIM;               /* (needs LIGHTS) */
        return KEYS_NAME[lights_keys % KEYS_N];
    case MI_NOTES: return lights_notes ? "ON" : "OFF";
    case MI_LOWCUT: return settings.lowcut ? "ON" : "OFF";
    case MI_USB: return usb_full ? "FULL" : "MASTER";
    case MI_SERIAL: return usb_serial ? "ON" : "OFF";
    case MI_USBSMP: return usb_sample ? "ON" : "OFF";
    default:
        *c = C_DIM;
        return "";                                    /* (an action: OCT+ opens it) */
    }
}

/* NoteSorcery: the USB SAMPLE screen. Play into the FM-1 from a phone or computer (its USB audio output); OCT+ erases
 * the slot and arms, the take starts with the first sound and ends with OCT+, the slot full or 2 s of silence */
static void draw_usb_sample(void)
{
    uint32_t n, pass, sig;
    static int32_t pk_hold;
    int32_t pk;
    uint32_t st = usmp_ui(&n, &pk);
    pk_hold = pk > pk_hold ? pk : pk_hold - pk_hold / 8;          /* the meter falls back slowly */
    sig = st * 7u + n / 2205u * 131u + (uint32_t)pk_hold / 1024u * 7919u + usmp_slot_ui * 104729u +
          (uint32_t)usb_smp_now() * 1299709u + (uint32_t)usb_smp_streaming() * 15485863u;
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)
        lcd_fill(0, H_HEAD + 1 + 124 + 95, 240, 240 - (H_HEAD + 1 + 124 + 95), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, "USB SAMPLE", C_HI);
    {
        char t[6] = {'U', 'S', 'R', (char)('1' + usmp_slot_ui), 0, 0};
        cv_text(236 - text_w(&FONT_S, t), 1, &FONT_S, t, TE_COL[0]);
    }
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {
        cv_begin(240, pass ? 95u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        if (!usb_smp_now()) {
            cv_text(4, 6, &FONT_L, "OFF", C_DIM);
            cv_text(4, 44, &FONT_S, "MENU > AUDIO > USB SAMPLE: ON,", C_HI);
            cv_text(4, 60, &FONT_S, "USB SERIAL: OFF, THEN RESTART.", C_HI);
            cv_text(4, 84, &FONT_S, "THE FM-1 IS THEN AN AUDIO", C_GRAY);
            cv_text(4, 100, &FONT_S, "OUTPUT OF YOUR PHONE OR", C_GRAY);
            cv_text(4, 116, &FONT_S, "COMPUTER: PLAY, AND RECORD.", C_GRAY);
        } else {
            uint32_t sec10 = n * 10u / 22050u, max10 = USMP_MAX_N * 10u / 22050u, wbar = n * 228u / USMP_MAX_N;
            char t[24];
            cv_text(4, 4, &FONT_L, USMP_STATE[st % 6u], st == US_REC ? TE_RED : st == US_ARMED ? TE_COL[2] : C_WHITE);
            cv_text(4, 40, &FONT_S, usb_smp_streaming() ? "HOST PLAYING" : "HOST SILENT", usb_smp_streaming() ? C_HI : C_DIM);
            cv_rect(4, 60, 228, 8, C_LINE);                                   /* the input */
            cv_rect(4, 60, (int32_t)((uint32_t)pk_hold * 228u / 32768u), 8, pk_hold > 30000 ? TE_RED : TE_COL[1]);
            cv_rect(4, 76, 228, 8, C_LINE);                                   /* the slot */
            cv_rect(4, 76, (int32_t)wbar, 8, TE_COL[0]);
            fmt_int(t, (int32_t)(sec10 / 10u));
            str_cpy(t + str_len(t), ".", 2);
            fmt_int(t + str_len(t), (int32_t)(sec10 % 10u));
            str_cpy(t + str_len(t), " / ", 4);
            fmt_int(t + str_len(t), (int32_t)(max10 / 10u));
            str_cpy(t + str_len(t), ".", 2);
            fmt_int(t + str_len(t), (int32_t)(max10 % 10u));
            str_cpy(t + str_len(t), " S", 3);
            cv_text(4, 92, &FONT_S, t, C_GRAY);
            cv_text(4, 130, &FONT_S, "K1 SLOT", TE_COL[0]);
            cv_text(4, 150, &FONT_S, st == US_ARMED || st == US_REC ? "OCT+ STOP" : "OCT+ ERASE AND ARM", C_HI);
            cv_text(4, 166, &FONT_S, "(STARTS WITH THE FIRST SOUND)", C_DIM);
        }
        cv_text(4, 196, &FONT_S, "OCT- CLOSE", C_DIM);
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

static void draw_menu(void)
{
    uint32_t i, pass, sec = mi_sec(ui.menu_sel % MI_COUNT);
    uint32_t sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                   settings.zoom * 104729u + lights_lvl * 1299709u + lights_keys * 15485863u +
                   lights_notes * 32452843u + usb_full * 49979687u + usb_serial * 86028121u + scr_bright * 179424673u +
                   night_on * 373587883u + usb_sample * 2971215073u;
    if (ui.menu == 3) {
        draw_usb_sample();
        return;
    }
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 95, 240, 240 - (H_HEAD + 1 + 124 + 95), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, ui.menu == 2 ? "ABOUT" : "MENU", C_HI);
    if (ui.menu != 2) {                             /* the section and its number, as a page's "ENV DEST 2/2" */
        char t[16];
        str_cpy(t, MS_NAME[sec], sizeof t);
        str_cpy(t + str_len(t), " ", 2);
        fmt_int(t + str_len(t), (int32_t)sec + 1);
        str_cpy(t + str_len(t), "/", 2);
        fmt_int(t + str_len(t), MS_COUNT);
        cv_text(236 - text_w(&FONT_S, t), 1, &FONT_S, t, C_GRAY);
    }
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: draw in two bands */
        cv_begin(240, pass ? 95u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        if (ui.menu == 2) {
            cv_text(4, 4, &FONT_L, "NOTESORCERY", C_WHITE);
            cv_rect(206, 10, 8, 4, TE_COL[0]), cv_rect(206, 16, 12, 4, TE_COL[1]);   /* the sail */
            cv_rect(206, 22, 16, 4, TE_COL[2]), cv_rect(206, 28, 20, 4, TE_COL[3]);
            cv_text(4, 36, &FONT_S, "BASED ON SLOOP + FELUCCA", C_AMB);
            cv_text(4, 54, &FONT_S, FELUCCA_VERSION, C_HI);
            cv_text(236 - text_w(&FONT_S, __DATE__), 54, &FONT_S, __DATE__, C_GRAY);   /* build date */
            cv_text(cv_text(4, 72, &FONT_S, "LEO KUROSHITA", C_HI) + 8, 72, &FONT_S, "@KUROGEDELIC", C_AMB);
            cv_text(4, 88, &FONT_S, "H\xDCGELTON INSTRUMENTS", C_HI);   /* Latin-1 U-umlaut */
            cv_text(4, 104, &FONT_S, "HUGELTON.COM", C_AMB);
            cv_text(4, 119, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
            cv_text(4, 132, &FONT_S, "CATACOMBIUS/NOTESORCERY", C_AMB);   /* (the source of this firmware) */
            cv_text(4, 146, &FONT_S, "SLOOP: ISOD89  X0X: C. VESTAL", C_DIM);
            cv_text(4, 159, &FONT_S, "OPEN303: R. SCHMIDT (MIT)", C_DIM);
            cv_text(4, 172, &FONT_S, "FONT: TERMINUS (OFL)", C_DIM);
            cv_text(4, 185, &FONT_S, "SAMPLES: VERSILIAN (CC0)", C_DIM);
            cv_text(4, 198, &FONT_S, "+ SONIC PI (CC0)", C_DIM);
        } else {
            int32_t x = 4;
            for (i = 0; i < MS_COUNT; i++) {        /* the sections as tabs: this one lit */
                int32_t w = text_w(&FONT_S, MS_NAME[i]);
                if (i == sec)
                    cv_rect(x - 2, 3, w + 4, 18, C_LINE);
                cv_text(x, 4, &FONT_S, MS_NAME[i], i == sec ? C_WHITE : C_DIM);
                x += w + 10;
            }
            uint32_t nrow = MS_FIRST[sec + 1u] - MS_FIRST[sec];
            int32_t dy = nrow > 3u ? MI_DY4 : MI_DY;
            for (i = MS_FIRST[sec]; i < MS_FIRST[sec + 1u]; i++) {
                uint32_t r = i - MS_FIRST[sec];
                int32_t y = MI_Y0 + (int32_t)r * dy, xv;
                int cur = i == ui.menu_sel;
                uint16_t vc;
                const char *v = mi_value(i, &vc);
                char k[4] = {'K', (char)('1' + r), 0, 0};
                cv_rect(4, y + 2, 4, dy - 8, cur ? C_WHITE : TE_COL[r & 3u]);   /* the knob's colour */
                xv = cv_text(14, y, &FONT_S, k, TE_COL[r & 3u]);
                cv_text(xv + 6, y, &FONT_S, MI_NAME[i], cur ? C_WHITE : C_GRAY);
                if (v[0] && text_w(&FONT_L, v) <= 222) {
                    xv = cv_text(14, y + 16, &FONT_L, v, cur && vc == C_HI ? C_WHITE : vc);
                } else if (v[0]) {                      /* (too wide for the large type: WHITE KEYS) */
                    xv = cv_text(14, y + 24, &FONT_S, v, cur && vc == C_HI ? C_WHITE : vc);
                } else {
                    xv = cv_text(14, y + 24, &FONT_S, "OCT+ OPENS", cur ? C_WHITE : C_DIM);
                }
                if (i == MI_COLOR) {                    /* the palette's colours */
                    uint32_t q;
                    for (q = 0; q < 5u; q++)
                        cv_rect(xv + 10 + (int32_t)q * 14, y + 28, 10, 10, pal[q]);
                }
                if (i == MI_SERIAL && usb_serial != usb_cdc_now())
                    cv_text(xv + 8, y + 30, &FONT_S, "RESTART", C_AMB);   /* (usb.c: at the next start) */
                if (i == MI_USBSMP)                     /* (usb.c: at the next start, without the console) */
                    cv_text(xv + 8, y + 30, &FONT_S, usb_sample != usb_smp_now() ? "RESTART" : "OCT+ OPENS",
                            usb_sample != usb_smp_now() ? C_AMB : C_DIM);
            }
            {
                int32_t yf = MI_Y0 + (int32_t)(nrow > 3u ? nrow : 3u) * dy;   /* (under the rows: .. 218 / 220) */
                cv_text(4, yf + 4, &FONT_S, "SELECT SECTION  KNOB SETS", C_DIM);
                cv_text(4, yf + 18, &FONT_S, "OCT+ OK   OCT- CLOSE", C_DIM);
            }
        }
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
    if (song.playing || transport_req)
        settings_later = 1;                            /* (a flash write stops the audio: once stopped) */
    else
        settings_save();                               /* palette / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* item i: a knob turned s (> 0 right, < 0 left), or 0: OCT+ (steps round, toggles, or opens) */
static void mi_set(uint32_t i, int32_t s)
{
    switch (i) {
    case MI_COLOR:                                     /* both ways round; OCT+ the next */
        settings.palette = (settings.palette + (s < 0 ? NPALETTES - 1u : 1u)) % NPALETTES;
        palette_set(settings.palette);
        ui.force = 1;                                  /* (the background too: the whole screen) */
        break;
    case MI_BRIGHT:                                    /* right brighter, left dimmer (stops at the ends) */
        if (s > 0 && scr_bright > 0u)
            scr_bright--;
        else if (s < 0 && scr_bright + 1u < NBRIGHT)
            scr_bright++;
        else if (!s)
            scr_bright = (uint8_t)((scr_bright + 1u) % NBRIGHT);
        palette_set(settings.palette);
        ui.force = 1;                                  /* (every colour changed) */
        break;
    case MI_NIGHT:
        night_set(s > 0 ? 1u : s < 0 ? 0u : !night_on);
        ui.force = 1;
        break;
    case MI_ZOOM:
    case MI_LOWCUT: {                                  /* right ON, left OFF; OCT+ toggles */
        uint32_t *v = i == MI_LOWCUT ? &settings.lowcut : &settings.zoom;
        *v = s > 0 ? 1u : s < 0 ? 0u : !*v;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
        break;
    }
    case MI_USB:                                       /* right FULL, left MASTER */
        usb_full = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_full);
        break;
    case MI_SERIAL:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
    case MI_USBSMP:                                    /* knob: ON / OFF (next start); OCT+ opens the screen */
        if (s)
            usb_sample = (uint8_t)(s > 0);
        else
            ui.menu = 3;
        ui.force = 1;
        break;
    case MI_NOTES:
        lights_notes = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !lights_notes);
        break;
    case MI_LIGHTS:
    case MI_KEYS: {                                    /* brighter / dimmer (stops at the ends); OCT+ steps round */
        uint8_t *v = i == MI_LIGHTS ? &lights_lvl : &lights_keys;
        uint32_t n = i == MI_LIGHTS ? LIGHTS_N : KEYS_N;
        if (s > 0 && *v + 1u < n)
            (*v)++;
        else if (s < 0 && *v > 0u)
            (*v)--;
        else if (!s)
            *v = (uint8_t)((*v + 1u) % n);
        if (i == MI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                   /* keys lit need a level: the lowest */
        break;
    }
    case MI_PANEL:                                     /* actions: OCT+ only */
        if (!s) {
            panel_setup();
            ui.force = 1;
        }
        break;
    case MI_ABOUT:
        if (!s) {
            ui.menu = 2;
            ui.force = 1;
        }
        break;
    default:
        break;
    }
}

/* menu: SELECT the section, KNOB 1..3 its settings, PRESETS the cursor, OCT+ ok, OCT- close (ABOUT -> the section) */
static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t k, ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    uint32_t sel = ui.menu_sel % MI_COUNT, sec = mi_sec(sel), n = MS_FIRST[sec + 1u] - MS_FIRST[sec];
    if (back) {
        if (ui.menu == 3)
            usmp_stop();                               /* (a take running: it ends, and is kept) */
        if (ui.menu == 2 || ui.menu == 3)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if (ui.menu == 3) {                                /* USB SAMPLE: K1 the slot, OCT+ arm / stop */
        uint32_t st = usmp_state();
        if ((s = panel_enc(EN_K1)) != 0 && st != US_ARMED && st != US_REC && st != US_SAVE)
            usmp_slot_ui = (uint8_t)((usmp_slot_ui + (s > 0 ? 1u : SMP_USER_SLOTS - 1u)) % SMP_USER_SLOTS);
        if (ok && usb_smp_now()) {
            if (st == US_ARMED || st == US_REC)
                usmp_stop();
            else if (st != US_SAVE)
                usmp_start(usmp_slot_ui);              /* (erases the slot: ~0.7 s) */
        }
        ui.force |= ok;
        enc_drop();
        return;
    }
    if (ui.menu != 1) {
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {             /* the section before / after (stops at the ends) */
        int32_t to = clamp((int32_t)sec + (s > 0 ? 1 : -1), 0, MS_COUNT - 1);
        if ((uint32_t)to != sec) {
            ui.menu_sel = MS_FIRST[to];
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0)               /* the cursor, round the section's rows */
        ui.menu_sel = (uint8_t)(MS_FIRST[sec] + (sel - MS_FIRST[sec] + (s > 0 ? 1u : n - 1u)) % n);
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0 && k < n) {
            ui.menu_sel = (uint8_t)(MS_FIRST[sec] + k);   /* (the cursor follows the knob turned) */
            mi_set(MS_FIRST[sec] + k, s);
        }
    if (ok)
        mi_set(ui.menu_sel % MI_COUNT, 0);
    enc_drop();                                        /* swallow the rest while the menu is up */
}
