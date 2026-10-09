/* SPDX-License-Identifier: GPL-3.0-only */
/* The X0X circuit-model drum machines (x0x/drum808.c: 8W8's TR-808; x0x/drum909.c: 9W9's TR-909; both by Charles
 * Vestal and contributors, GPL-3.0) for NoteSorcery's 808 CM and 909 CM kits (drums.c). One translation unit: their
 * file-scope macros are undefined after them, so they meet nothing of SLOOP's. Float DSP: called only from the
 * audio ISR (x0x/README.md). */
#include "x0x/drum808.c"
#include "x0x/drum909.c"
#undef BANK_DUTY
#undef BD_Q
#undef CHUNK
#undef CL_F
#undef CL_Q
#undef CP_F
#undef CP_Q
#undef CTRL_RATE
#undef DIODE_VON
#undef DRV
#undef DST
#undef LVL
#undef MA_ATK_DEF
#undef NON
#undef PI_SR
#undef PP
#undef PUL_DUTY
#undef P_DIST
#undef P_DLY
#undef P_DRIVE
#undef P_REV
#undef QUIET
#undef SC808_FULL_VELOCITY_GAIN
#undef SD_DECAY_DEF
#undef SD_F1
#undef SD_F2
#undef SD_G1
#undef SD_G2
#undef SD_Q1
#undef SD_Q2
#undef SH_A
#undef SH_DC
#undef SND
#undef SR
#undef TOM_LN100_Q_PI
#undef TOM_Q
#undef TRK
#undef TU12
#undef TU2
#undef TUM
#undef TWO_PI_SR
#undef DIST
#undef DRIVE
#undef EXP
#undef FITS
#undef LEVEL
#undef LIN
#undef NP
#undef PAN
#undef SENDS
