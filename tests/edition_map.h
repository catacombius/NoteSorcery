/* SPDX-License-Identifier: GPL-3.0-only */
/* The host tests on BOTANICA (-DEDITION_BOTANICA=1, tests/run_tests_botanica.sh): the engines the shared tests name
 * by their NoteSorcery roles, mapped onto BOTANICA's (FM6, an engine with its own envelopes: FELT; TRIO, a
 * polyphonic synth with a filter: WAVE), the FM6 hooks the harness calls (BOTANICA has no FM6), and the drum kit
 * list the kit tests expect. */
#if EDITION_BOTANICA
#define ENGI_FM6 ENGI_FELT
#define ENGI_TRIO ENGI_WAVE
#define ENGI_ACID ENGI_ANALOG         /* (a mono bass: ANALOG) */
#define FM6_POLY 6               /* (FELT: 6 voices, as FM6) */
static void fm6_track_loaded(track_t *t) { (void)t; }
static void fm6_init(void) {}
#define T_KIT_AFTER_SYN DRUM_KITS               /* (no circuit kits after SYN1..SYN4) */
#define T_KIT0 "TWIG"                           /* the first synthesised kits: the SYN kits' defaults */
#define T_KIT1 "FIELD"
#define T_KIT3 "MOSS"
#else
#define T_KIT_AFTER_SYN DRUM_CM808
#define T_KIT0 "808"
#define T_KIT1 "909"
#define T_KIT3 "TECHNO"
#endif
