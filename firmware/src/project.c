/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects. NoteSorcery (format "NSP1"): eight tracks, every global, each track's sound, pattern, nudges,
 * parameter locks and fill conditions. SLOOP's formats (FUN1..FUN5, four tracks) are not read: the first
 * boot of NoteSorcery starts empty (back up with SLOOP's editor first).
 *
 * Four slots (the song sections A..D) live in flash, two sectors a copy (storage.c), and are read in place
 * through the plain XIP window (sec_get): the audio ISR applies a section straight from there (no flash
 * write ever runs while the transport plays: an erase stops everything for ~50 ms). A section stored while
 * playing waits in RAM (sec_pend, .noinit: it survives a warm reset as SLOOP's slots did) until the
 * transport stops and nothing sounds. The working project is also kept in flash by itself (autosave, when
 * the transport is stopped and nothing sounds) and comes back at power-on: you start where you left it.
 *
 * Built on the host too (tests/project_test.c, -DPROJ_HOST, with the sections in RAM): the part above the
 * #ifndef PROJ_HOST needs core.h, params.c (TP), drums.c (the lanes), the engines and trk_def_engine (ui.c). */
#define PROJ_MAGIC 0x3150534Eu                 /* "NSP1" */
typedef struct {                               /* one track; a drum track ignores engine / preset */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];                  /* (a drum track: 16 lanes, the same size) */
    };
    int8_t micro[NSTEP];                       /* each step's nudge (core.h) */
    plock_t lock[NLOCK];                       /* its parameter locks (step LOCK_FREE = none) */
    uint8_t fill[NSTEP / 4];                   /* its fill conditions, 2 bits a step (FC_*) */
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, slot, ntrk, rsv;              /* the selected track; slot: sec_pend's section; NTRK */
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_t;
#ifdef ST_BIG_PAYLOAD_MAX
_Static_assert(sizeof(project_t) <= ST_BIG_PAYLOAD_MAX, "a project fits its two flash sectors (storage.c)");
#endif
_Static_assert(2u * sizeof(project_t) <= 15400u, "sec_pend and proj_stage fit the .noinit RAM (app.ld NOINIT)");

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q)
{
    return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->ntrk == NTRK && q->sum == proj_sum(q);
}

/* n bytes of a stored project -> q; 0 = not a project of this format */
static int proj_import(project_t *q, const void *b, int n)
{
    if (n != (int)sizeof *q || !proj_ok((const project_t *)b))
        return 0;
    if (q != b)
        memcpy(q, b, sizeof *q);
    return 1;
}

/* ---- the working project <-> a project_t */
static void proj_capture(project_t *p)        /* what is playing now, as a project */
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    p->ntrk = NTRK;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    for (i = 0; i < NTRK; i++) {
        memcpy(p->t[i].p, trk[i].p, sizeof trk[i].p);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        memcpy(p->t[i].micro, trk[i].micro, sizeof trk[i].micro);
        memcpy(p->t[i].lock, trk[i].lock, sizeof trk[i].lock);
        memcpy(p->t[i].fill, trk[i].fill, sizeof trk[i].fill);
    }
    p->sum = proj_sum(p);
}

/* a project's tracks (and its globals, all: a load; or only the drum level / reverb / delay: a song
 * section) into the working one, every value back inside its range. The audio ISR must not run
 * meanwhile (the song sections: called from it, p read through XIP; a load: IRQ off) */
static void proj_apply(const project_t *p, int all)
{
    uint32_t i, k;
    for (i = 0; i < G_COUNT; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_SYNC && i != G_MIDI && i != G_ROUTE
                : i == G_DRLVL || i == G_DRREV || i == G_DRDLY)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = k < NPART ? s->engine % NENGINES : 0u;
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        t->lk_n = 0;                                    /* (the locks in force: the values come from the project) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = k >= TRK_DRUM && i == P_E0 ? &DRUM_KIT_DESC :   /* the drum kit */
                                    i >= P_E0 && i <= P_E7 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset == 0xFFu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        for (i = 0; i < NSTEP; i++)                     /* the nudges and locks, each inside its range */
            t->micro[i] = (int8_t)clamp(s->micro[i], MICRO_MIN, MICRO_MAX);
        for (i = 0; i < NSTEP / 4u; i++) {              /* the conditions: 3 means normal (0) */
            uint32_t b = s->fill[i], j;
            for (j = 0; j < 4u; j++)
                if (((b >> (2u * j)) & 3u) == 3u)
                    b &= ~(3u << (2u * j));
            t->fill[i] = (uint8_t)b;
        }
        for (i = 0; i < NLOCK; i++) {
            const plock_t *l = &s->lock[i];
            uint32_t id = l->param;
            if (l->step < NSTEP && id < P_COUNT && p_lockable(id)) {
                const param_desc_t *d = k >= TRK_DRUM && id == P_E0 ? &DRUM_KIT_DESC :
                                        id >= P_E0 && id <= P_E7 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
                t->lock[i].step = l->step;
                t->lock[i].param = (uint8_t)id;
                t->lock[i].val = (int16_t)clamp(l->val, d->min, d->max);
            } else {
                t->lock[i].step = LOCK_FREE;            /* no such step or parameter: the slot is free */
                t->lock[i].param = 0;
                t->lock[i].val = 0;
            }
        }
        if (k < TRK_DRUM)
            for (i = 0; i < NSTEP; i++) {
                step_t *st = &t->step[i];
                uint32_t j;
                if (st->n > 4u)
                    st->n = 4;
                if (st->time > ST_REST)
                    st->time = ST_REST;
                for (j = 0; j < 4u; j++)
                    st->note[j] &= 127u;
            }
        fm6_track_loaded(t);                            /* FM6: the project keeps PTCH, not the patch: its slot's */
    }
}

/* ---- the sections A..D (the project slots): sec_get(s) is section s as stored, 0 = empty */
#ifdef PROJ_HOST
project_t proj_slot[4];                         /* host: the sections in RAM */
static const project_t *sec_get(uint32_t s) { return proj_ok(&proj_slot[s & 3u]) ? &proj_slot[s & 3u] : 0; }
#else
static project_t sec_pend __attribute__((section(".noinit")));   /* a section stored, not yet in flash */
static uint8_t sec_pend_slot = 0xFFu;           /* its section, 0xFF = none */
static uint32_t sec_off[4];                     /* each section's payload in flash (its current copy), 0 = empty */
static const project_t *sec_get(uint32_t s)
{
    s &= 3u;
    if (sec_pend_slot == s)
        return &sec_pend;
#if FELUCCA_FLASH
    if (sec_off[s])
        return (const project_t *)fm1_xip_ptr(sec_off[s]);
#endif
    return 0;
}
#endif

#ifndef PROJ_HOST
#if FELUCCA_ARRANGER
#include "arranger_scene.c"
#endif
static uint8_t song_dirty;                      /* the song: in RAM, not yet in flash */
static project_t proj_stage __attribute__((section(".noinit")));   /* the main loop's staging: a load, a backup,
                                                                   * the FM6 bank (fm6_bank.c) */
#if FELUCCA_FLASH
/* where section s is in flash now (after a boot, a save, a restore): its payload when it holds a whole project */
static void sec_scan(uint32_t s)
{
    st_hdr_t h;
    int c = st_current(OBJ_PROJECT0 + (s & 3u), &h);
    uint32_t off = c < 0 || h.len != sizeof(project_t) ? 0u : st_sector(OBJ_PROJECT0 + (s & 3u), (uint32_t)c) + ST_PAYLOAD_OFF;
    sec_off[s & 3u] = off && proj_ok((const project_t *)fm1_xip_ptr(off)) ? off : 0u;
}
#include "fm6_bank.c"                           /* the FM6 patch bank (eng_fm6.c PTCH B1..B27): staged in proj_stage */
#endif

static void project_save(uint32_t slot)
{
    slot &= 3u;
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
#endif
#if FELUCCA_FLASH
    if (flash_ok) {
        proj_capture(&proj_stage);
        if (st_save(OBJ_PROJECT0 + slot, &proj_stage, sizeof proj_stage)) {
            ui_message("SAVE ERROR");
            return;
        }
        if (sec_pend_slot == slot) {
            sec_pend_slot = 0xFFu;                      /* (the stored one waiting is older) */
            sec_pend.magic = 0;
        }
        sec_scan(slot);
        ui_message("SAVED");
        return;
    }
#endif
    fm1_irq_off();
    proj_capture(&sec_pend);                            /* no flash: the one section RAM holds */
    sec_pend.slot = (uint8_t)slot;
    sec_pend.sum = proj_sum(&sec_pend);
    sec_pend_slot = (uint8_t)slot;
    fm1_irq_on();
    ui_message("SAVED (RAM)");
}

/* a project into the working one: the transport stops, everything sounding is released */
static void project_apply(const project_t *p)
{
    uint32_t k;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    proj_apply(p, 1);
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    (void)k;
    sync_reload = 1;
    ui.force = 1;
}

static void project_load(uint32_t slot)
{
    const project_t *p = sec_get(slot);
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE LOAD"); return; }
#endif
    if (!p) {
        ui_message("EMPTY SLOT");
        return;
    }
    project_apply(p);
    ui_message("LOADED");
}

/* ---- the working project, kept in flash by itself: saved when it changed, the transport is stopped,
 * nothing sounds and the panel was not touched for AUTOSAVE_IDLE (a flash erase stops the audio for
 * ~50 ms: never while something plays); loaded at power-on (autosave_resume) */
#define AUTOSAVE_IDLE 2500u                    /* ms without input */
#define AUTOSAVE_GAP 20000u                    /* ms between two saves at least */
static project_t autosave_buf __attribute__((section(".pool")));
static uint32_t autosave_hash, autosave_ms, autosave_checked;

static int audio_quiet(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 0;
    for (p = 0; p < NDRUMTRK; p++)
        for (i = 0; i < NDRUM; i++)
            if (drumst[p].v[i].active)
                return 0;
    return 1;
}

static void autosave_tick(void)                /* main loop */
{
#if FELUCCA_FLASH
    uint32_t h, now = fm1_ms;
    if (!flash_ok || song.playing || transport_req || rec_wait || ft_on || ui.menu ||
        now - ui_input_ms < AUTOSAVE_IDLE || now - autosave_ms < AUTOSAVE_GAP || now - autosave_checked < 1000u)
        return;
    autosave_checked = now;
    proj_capture(&autosave_buf);
    h = autosave_buf.sum;
    if (h == autosave_hash || !audio_quiet())
        return;
    if (st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
        autosave_hash = h;
    autosave_ms = fm1_ms;
#endif
}

static void autosave_resume(void)              /* power-on: the project as it was left (felucca_init) */
{
#if FELUCCA_FLASH
    project_t *q = &autosave_buf;
    int n;
    if (!flash_ok)
        return;
    n = st_load(OBJ_AUTOSAVE, q, sizeof *q);
    if (!proj_import(q, q, n))
        return;
    autosave_hash = q->sum;
    proj_apply(q, 1);
    song.sel = (uint8_t)(q->sel < NTRK ? q->sel : 0u);
    for (n = 0; n < NPART; n++)
        trk[n].engine = trk[n].eng_req;        /* (nothing sounds yet: no fade) */
#endif
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_ARRANGER
    arr_config_t arrangement;
#endif
    uint32_t lights;                               /* SLOOP 2.3: the backlight (panel.c lights_word); appended,
                                                    * so 2.2 still reads its part (st_load cuts at its size) */
} persist_t;
#define PERSIST_SIZE_V22 __builtin_offsetof(persist_t, lights)   /* the settings as 2.2 wrote them (no lights) */
_Static_assert(sizeof(persist_t) == PERSIST_SIZE_V22 + 4u, "lights: the last word, no padding before it");
#if FELUCCA_ARRANGER
_Static_assert(__builtin_offsetof(persist_t, arrangement) == 48u && sizeof(arr_config_t) == 36u,
               "the web editor's Song page edits bytes 48..83 of the settings (web/editor.html ARR)");
#endif
#if FELUCCA_ARRANGER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": includes the song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif
#if FELUCCA_FLASH
static persist_t persist_saved;
static void dsu_boot(void);
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_ARRANGER
    arr_defaults(&arrangement);
#endif
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
        for (k = 0; k < WT_USER; k++)
            wt_user_scan(k);                       /* NoteSorcery: your single-cycle waves (eng_wave.c) */
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (n == (int)PERSIST_SIZE_V22 && p.magic == PERSIST_MAGIC)
            p.lights = 0;                          /* from 2.2: backlight off */
        if (((n == (int)sizeof p || n == (int)PERSIST_SIZE_V22) && p.magic == PERSIST_MAGIC)
#if FELUCCA_ARRANGER
            || (n == (int)(16u + sizeof(panel_t)) && p.magic == 0x50455232u)
#endif
            ) {
            settings.magic = SETTINGS_MAGIC;
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;
            settings.zoom = p.zoom;
            if (p.panel.magic == PANEL_MAGIC)
                panel = p.panel;
            if (p.magic == PERSIST_MAGIC)
                lights_from_word(p.lights);
            else
                p.lights = 0;
#if FELUCCA_ARRANGER
            if (p.magic == PERSIST_MAGIC && arr_valid(&p.arrangement, 15u))
                arrangement = p.arrangement;
            else
                p.arrangement = arrangement;
#endif
            persist_saved = p;
        } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {   /* "PER1": palette, panel */
            const uint32_t *w = (const uint32_t *)&p;
            panel_t old;
            memcpy(&old, w + 2, sizeof old);
            settings.magic = SETTINGS_MAGIC;
            settings.palette = w[1];
            settings.lowcut = 0;
            settings.zoom = 0;
            if (old.magic == PANEL_MAGIC)
                panel = old;
        }
    }
    {   /* the sections: where each one is in flash. A section stored while playing and not written yet
         * survives a warm reset (an update, UPDATE MODE, a crash) in .noinit: still to be written when quiet */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            sec_scan(i);
        if (proj_ok(&sec_pend) && sec_pend.slot < 4u)
            sec_pend_slot = sec_pend.slot;
        else
            sec_pend.magic = 0;
    }
    dsu_boot();                                    /* SLOOP 2.5: the SYN kits (after the settings) */
    up_boot();                                     /* user presets */
    fm6_bank_boot();                               /* the FM6 patch bank (fm6_bank.c) */
#endif
}

static int project_used(uint32_t slot) { return sec_get(slot) != 0; }

#if FELUCCA_FLASH
/* SLOOP 2.5: the settings object holds the SYN kits after persist_t (drum_synth.c dsu): firmware before 2.5 reads
 * its persist_t and leaves the rest (st_load cuts at its size); its own next save drops them */
static int settings_write(const persist_t *p)
{
    if (!dsu_valid(&dsu))
        dsu_defaults();
    if (st_save2(OBJ_SETTINGS, p, sizeof *p, &dsu, sizeof dsu))
        return -1;
    dsu_dirty = 0;
    return 0;
}
static void dsu_boot(void)                          /* the SYN kits from the settings object, else the defaults */
{
    st_hdr_t h;
    dsu_defaults();
    if (st_current(OBJ_SETTINGS, &h) >= 0 && h.len == sizeof(persist_t) + sizeof(dsu_bank_t) &&
        dsu_valid((const dsu_bank_t *)(st_buf + sizeof(persist_t)))) {
        uint32_t k;
        memcpy(&dsu, st_buf + sizeof(persist_t), sizeof dsu);
        for (k = 0; k < DSU_N; k++)
            dsu_fix_kit(&dsu.k[k]);
    }
    dsu_dirty = 0;
}
#endif

static void persist_fill(persist_t *p)              /* the settings as they are now */
{
    memset(p, 0, sizeof *p);
    p->magic = PERSIST_MAGIC;
    p->palette = settings.palette;
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom;
    p->panel = panel;
    p->lights = lights_word();
#if FELUCCA_ARRANGER
    p->arrangement = arrangement;
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    persist_fill(&p);
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;                                    /* unchanged: no erase cycle */
    if (settings_write(&p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH

/* ---- backup restore (editor.c BK_PUT): each object checked as a load checks it, then written through the
 * same A/B commit as a save. rc: 0 ok, 2 not a valid object, 3 stop the song first, 4 flash */
static int panel_valid(const panel_t *q)           /* a permutation of the buttons and of the knobs */
{
    uint32_t i, b = 0, e = 0;
    if (q->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (q->btn[i] >= 14u || (b >> q->btn[i]) & 1u)
            return 0;
        b |= 1u << q->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (q->enc[i] >= 7u || (e >> q->enc[i]) & 1u || (q->dir[i] != 1 && q->dir[i] != -1))
            return 0;
        e |= 1u << q->enc[i];
    }
    return 1;
}

static uint32_t settings_restore(const void *raw, uint32_t n)
{
    persist_t p;
    if (n != sizeof p && n != PERSIST_SIZE_V22)
        return 2;
    memset(&p, 0, sizeof p);
    memcpy(&p, raw, n);
    if (p.magic != PERSIST_MAGIC || p.palette >= NPALETTES || p.lowcut > 1u || p.zoom > 1u || !panel_valid(&p.panel))
        return 2;
#if FELUCCA_ARRANGER
    if (!arr_valid(&p.arrangement, 15u))
        return 2;
    if ((song.playing || transport_req) && memcmp(&p.arrangement, &arrangement, sizeof arrangement))
        return 3;                                    /* SLOOP 2.5: a new song order only when stopped (as on the SONG screen) */
#endif
    if (!flash_ok || settings_write(&p))
        return 4;
    persist_saved = p;
    settings.palette = p.palette;
    settings.lowcut = p.lowcut;
    settings.zoom = p.zoom;
    panel = p.panel;
#if FELUCCA_ARRANGER
    arrangement = p.arrangement;
#endif
    lights_from_word(p.lights);
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
    ui.force = 1;
    return 0;
}

/* slot 0..3 (n 0: empty), or 4: the working project (loaded now) */
static uint32_t project_restore(uint32_t slot, const void *raw, uint32_t n)
{
    if (song.playing || transport_req)
        return 3;
    if (slot < 4u && sec_pend_slot == slot) {
        sec_pend_slot = 0xFFu;                          /* (what comes back replaces the stored one waiting) */
        sec_pend.magic = 0;
    }
    if (slot < 4u && !n) {
        if (!flash_ok || st_save(OBJ_PROJECT0 + slot, raw, 0))
            return 4;
        sec_scan(slot);
        return 0;
    }
    if (!proj_import(&proj_stage, raw, (int)n))
        return 2;
    if (slot == 4u) {
        project_apply(&proj_stage);
        return 0;
    }
    if (!flash_ok || st_save(OBJ_PROJECT0 + slot, &proj_stage, sizeof proj_stage))
        return 4;
    sec_scan(slot);
    return 0;
}
#endif
#if FELUCCA_ARRANGER
static void arrangement_save(void)
{
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
    settings_save();
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(memcmp(&persist_saved.arrangement, &arrangement, sizeof arrangement) ? "SAVE ERROR" : "SONG SAVED");
        return;
    }
#endif
    ui_message("SONG IN RAM ONLY");
}

/* ---- live sections (SAVE + key, ui_layers.c). A section is a project slot (A..D = 1..4): stored into RAM
 * at once (playing too), written to flash once the transport is stopped and nothing sounds (an erase
 * stops the audio for ~50 ms); a song recorded with SONG REC is saved the same way. */
static void sections_write(void);
static void section_store(uint32_t s)
{
    s &= 3u;
    if (sec_pend_slot != 0xFFu && sec_pend_slot != s) {   /* another section still waits for the flash */
        if (song.playing || transport_req) {
            ui_message("STOP TO STORE MORE");           /* (writing it now would stop the audio for ~50 ms) */
            return;
        }
        sections_write();
        if (sec_pend_slot != 0xFFu && sec_pend_slot != s) {
            ui_message("SAVE ERROR");
            return;
        }
    }
    fm1_irq_off();                                      /* (the audio ISR may be applying a section) */
    proj_capture(&sec_pend);
    sec_pend.slot = (uint8_t)s;
    sec_pend.sum = proj_sum(&sec_pend);
    sec_pend_slot = (uint8_t)s;
    live_sec = (int8_t)s;
    fm1_irq_on();
}
static void section_load(uint32_t s)                    /* stopped: the section is the loop now */
{
    const project_t *p = sec_get(s);
    if (!p)
        return;
    project_apply(p);
    live_sec = (int8_t)(s & 3u);
}
static void sections_write(void)                        /* the section waiting and the song into flash */
{
#if FELUCCA_FLASH
    if (flash_ok && sec_pend_slot < 4u) {
        uint32_t s = sec_pend_slot;
        if (st_save(OBJ_PROJECT0 + s, &sec_pend, sizeof sec_pend) == 0) {
            sec_scan(s);
            sec_pend_slot = 0xFFu;                      /* (a failed write stays waiting: tried again later) */
            sec_pend.magic = 0;
        }
    }
#endif
    if (song_dirty) {
        song_dirty = 0;
        settings_save();
    }
}
/* before an intentional reset (an update, UPDATE MODE, UBOOT from the host): the audio is stopped, so
 * whatever is only in RAM goes to flash now: the live sections, the song, the working project */
static void persist_flush_now(void)
{
    sections_write();
#if FELUCCA_FLASH
    if (flash_ok && !arrangement_clock.running) {     /* (a song playing: the tracks hold a section) */
        proj_capture(&autosave_buf);
        if (autosave_buf.sum != autosave_hash && st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
            autosave_hash = autosave_buf.sum;
    }
#endif
}
static void sections_flush(void)                        /* main loop */
{
    static uint32_t tried;
    if (srec_done) {
        song_dirty = srec_done != 0xFFu;
        if (song_dirty) {
            char b[8];
            fmt_int(b, srec_done);
            ui_say("SONG PARTS ", b);
        } else {
            ui_message("NO SONG");
        }
        srec_done = 0;
    }
    if ((uint32_t)song.g[G_SYNC] != lights_sync) {      /* GLO > SYSTEM > SYNC: kept with the settings */
        lights_sync = (uint8_t)song.g[G_SYNC];
        settings_later = 1;
    }
    if ((uint32_t)(song.g[G_MIDI] != 0) != lights_mout) {   /* GLO > SYSTEM > MIDI: the same */
        lights_mout = (uint8_t)(song.g[G_MIDI] != 0);
        settings_later = 1;
    }
    if ((uint32_t)(song.g[G_ROUTE] != 0) != lights_min) {   /* GLO > SYSTEM > IN: the same */
        lights_min = (uint8_t)(song.g[G_ROUTE] != 0);
        settings_later = 1;
    }
    if (settings_later) {                               /* the menu closed while playing */
        settings_later = 0;
        song_dirty = 1;                                 /* (settings_save when quiet, with the song) */
    }
    if ((sec_pend_slot == 0xFFu && !song_dirty) || song.playing || transport_req || !audio_quiet() || fm1_ms - ui_input_ms < 1500u ||
        fm1_ms - tried < 5000u)
        return;
    tried = fm1_ms;                                     /* (a failed write: again in 5 s, not every frame) */
    sections_write();
    if (sec_pend_slot != 0xFFu && flash_ok)
        ui_message("SAVE ERROR: RETRYING");
}
#endif
#endif /* PROJ_HOST */
