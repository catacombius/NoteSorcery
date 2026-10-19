#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                  redirect to the installer
  firmware/notesorcery-VER.fwsc  the package
  webapp/installer/index.html index_pkg.html with fm1pkg.js, fm1ota.js and the metadata inlined
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt, als/: the Ableton export, sf2.js: SoundFonts)
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR [--beta] [--edition botanica] [--alt URL]
  (--beta: web/beta_banner.html at the top of the installer: the beta channel, docs/beta)
  (--edition botanica: BOTANICA's site, its package botanica-VER.fwsc (identity FM-1_96XY), its name, colours and
   flower on the pages; --alt URL: a link on the NoteSorcery installer to the alternate release there)

The package identity (FM-1_9xx) is read from the package; the device reports it
after the install.
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def botanica_brand(html):
    """the installer as BOTANICA's: its name (the source stays NoteSorcery's repository), the moss colours, the flower"""
    repo = "github.com/catacombius/NoteSorcery"
    html = html.replace(repo, "\0REPO\0")
    html = html.replace("NoteSorcery is based on", "BOTANICA is the botanica-music edition of NoteSorcery, which is based on")
    html = html.replace("NoteSorcery", "BOTANICA").replace("\0REPO\0", repo)
    html = html.replace("--bg: #000; --fg: #f2f2f2; --dim: #8a8a92; --tile: #16161a; --line: #2a2a30;",
                        "--bg: #0a1a12; --fg: #f4eede; --dim: #9fb5a2; --tile: #12291d; --line: #24442f; --petal: #ff78a8;")
    html = html.replace("<!--BANNER-->", '<p><img src="botanica.svg" alt="" width="96" height="96" '
                                         'style="border-radius:16px"></p>\n  <!--BANNER-->')
    return html


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    edition = sys.argv[sys.argv.index("--edition") + 1] if "--edition" in sys.argv else "notesorcery"
    raw = pkg.read_bytes()
    product = product_of(raw)
    want = r"FM-1_96\d\d" if edition == "botanica" else r"FM-1_9\d\d|FM-1_95\d\d"   # (NoteSorcery: FM-1_95XY)
    if not re.fullmatch(want, product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a {edition} package ({want})")
    if b"FELUCCA-LOADER-1" not in raw:              # marker of firmware/loader
        raise SystemExit(f"{pkg}: no Felucca update loader in it")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    name = f"{edition}-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest()})   # the page checks the download against it
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    guide = HERE.parent / "output/pdf/Studio-0.3-guide-rapide-FR.pdf"
    guide_link = ""
    if guide.exists() and "--studio-guide" in sys.argv:   # LIVE: that guide describes the removed RYTHME tab
        shutil.copy(guide, out / guide.name)
        guide_link = '<p lang="fr"><a href="../../' + guide.name + '">Guide rapide illustré (PDF, 4 pages)</a></p>'
    html = html.replace("<!--STUDIO_GUIDE-->", guide_link)
    banner = ""
    if "--beta" in sys.argv:                       # the beta channel (docs/beta): what it is, what it implies
        notes = HERE / "beta_banner.html"
        banner = notes.read_text(encoding="utf-8") if notes.exists() else "<p><b>BETA</b></p>"
    if "--alt" in sys.argv:                        # NoteSorcery: the alternate release
        url = sys.argv[sys.argv.index("--alt") + 1]
        banner += (f'<p class="small">Alternate release: <a href="{url}">BOTANICA</a>, the botanica-music edition '
                   '(felt piano, droplets, foley, the GARDEN).</p>')
    if edition == "botanica":
        html = botanica_brand(html)
    html = html.replace("<!--BANNER-->", banner)
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob(f"{edition}-*.fwsc")):   # one package: the current one
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / name)
    shutil.copy(HERE / "editor.html", ed / "index.html")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "sf2.js"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    (ed / "als").mkdir(exist_ok=True)                 # NoteSorcery: the Ableton export the editor imports
    for f in ("nsals.js", "als_templates.js"):
        shutil.copy(HERE / "als" / f, ed / "als" / f)
    title = "BOTANICA" if edition == "botanica" else "NoteSorcery"
    if edition == "botanica":
        shutil.copy(HERE.parent / "assets" / "logo" / "botanica.svg", inst / "botanica.svg")
    (out / "index.html").write_text(
        f'<!doctype html><meta charset="utf-8"><title>{title}</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        f'<a href="webapp/installer/">{title} installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, firmware/{name} ({len(raw)} B, {product})")


if __name__ == "__main__":
    args, skip = [], False
    for a in sys.argv[1:]:
        if skip:
            skip = False
        elif a in ("--edition", "--alt"):
            skip = True                            # (its value)
        elif a not in ("--studio-guide", "--beta"):
            args.append(a)
    if len(args) != 3:
        sys.exit(__doc__)
    main(*args)
