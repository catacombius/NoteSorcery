# NoteSorcery licensing

NoteSorcery is built on SLOOP (isod89, <https://github.com/isod89/sloop-fm1>), which is built on Felucca (Leo
Kuroshita / Hügelton Instruments). It carries both projects' licence unchanged: the code is GPL-3.0-only and the
Felucca Assets are combined under the section 7 permission below. NoteSorcery's own changes are GPL-3.0-only too.
NoteSorcery also contains:

- `firmware/src/x0x/`: DSP from X0X (Charles Vestal, <https://github.com/charlesvestal/fm1-x0x>, GPL-3.0-only): the
  TB-303 voice (Open303 by Robin Schmidt, MIT, `LICENSES/MIT-Open303.txt`), 8W8's TR-808 (its rim shot from
  Yoshinosuke Horiuchi's sc808, MIT) and 9W9's TR-909 (grown out of Matthew Cieplak's ER-99, GPL-3.0). See
  `firmware/src/x0x/README.md`.
- `web/als/`: the Ableton Live Set and MIDI writers and the Live set templates, ported from NoteMove
  (catacombius), contributed by its author under GPL-3.0-only.

What follows is Felucca's licensing text, kept as it is, with the third-party table brought up to date for
what NoteSorcery builds.

# Felucca licensing

Felucca is free software. Its **code** is licensed under the GNU General Public License,
version 3 only (`GPL-3.0-only`, full text in `LICENSE`). Its **assets** are not part of
that licence: the icon atlas `assets/icons.png`, the panel image `docs/panel.jpg` and the drum sounds made by
`tools/gen_waves.py` (the Hügelton Sample Pack) are Copyright (C) 2026 Hügelton Instruments,
all rights reserved. Their licence terms will be published later. SLOOP's firmware does not contain the
Hügelton Sample Pack: its sampled drum kit is made of CC0 recordings (`assets/samples-cc0/KIT`), and
`gen_waves.py` only feeds the SLICE engine's demo loop, which SLOOP does not build.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## What is code (GPL-3.0-only)

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header (one source file
carries another: `firmware/src/fm6_core.c`, msfa, Apache-2.0, below; the firmware built with it is
GPL-3.0-only as a whole):

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`

You may use, study, change and share it under the GPL. If you distribute Felucca, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Felucca
inside.

## Additional permission (GPL-3.0 section 7)

As an additional permission under GPL-3.0 section 7, you may combine Felucca, or a work
based on it, with the Felucca Assets (above), and convey the combination.
This is allowed even though the Felucca Assets are not licensed under the GPL, provided
that:

- you follow the GPL for every part that is not a Felucca Asset; and
- you follow the terms published for the assets.

The Felucca Assets are data (wavetables, icons, sample data). They are not program
code. A firmware image built from the GPL sources with replacement assets, or with no
assets, is entirely governed by the GPL.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Instrument and drum samples (Versilian Studios VSCO-2 CE, VCSL; NoteSorcery builds in the grand piano and the GM kit only) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| Terminus font 8x16 (ter-u16n): the FM-1's screen, and the web editor's font (`tools/gen_webfont.py` makes it a TrueType font inlined in `web/editor.html`) | SIL OFL 1.1 | `assets/fonts/ter-u16n.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| Fukiai icon font (Hügelton Instruments), web editor only | MIT | `web/fukiai.ttf`, `web/FUKIAI-LICENSE.txt` |
| msfa by Google Inc. and Pascal Gauthier, from Dexed (<https://github.com/asb2m10/dexed>): the FM6 engine's synthesis (SLOOP 2.4), ported to integer C for Felucca 1.0 by Leo Kuroshita (Dexed itself is GPL-3.0; only msfa is used; the FM6 factory patches are Felucca's own) | Apache-2.0 | `firmware/src/fm6_core.c`, `LICENSES/Apache-2.0-msfa.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Contributions

Contributions are welcome under GPL-3.0-only. By submitting one, you agree that it may be
combined with the Felucca Assets under the section 7 permission above.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. Felucca is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

## Radio

Felucca never enables the Bluetooth / Wi-Fi radio of the hardware.
