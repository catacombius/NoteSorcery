#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The web editor's copies of the drum kits, from the firmware: the drum track's KIT names (drums.c DRUM_KIT_NAMES
with the synthesised kits of build/gen/felucca_drumkits.h) and the factory synthesised kits the editor's mock
device plays (DSYN_MOCK_NAMES, DSYN_MOCK_KITS: per kit its crush and 16 x 22 sound bytes, base64). Run after
the kits change (tools/gen_drumkits.py, a new kit in drums.c); web/test_web.mjs checks they agree.

  tools/sync_editor_kits.py
"""
import base64
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    hdr = (ROOT / "build/gen/felucca_drumkits.h").read_text()
    drums = (ROOT / "firmware/src/drums.c").read_text()
    ed_path = ROOT / "web/editor.html"
    ed = ed_path.read_text(encoding="utf-8")
    ds = re.search(r"#define DS_KIT_NAME_LIST (.*)", hdr).group(1)
    dk = re.search(r"DRUM_KIT_NAMES\[\] = \{([^}]*)\}", drums).group(1).replace("DS_KIT_NAME_LIST", ds)
    names = [x.strip().strip('"') for x in dk.split(",") if x.strip()]
    ed, n = re.subn(r'(const KIT = E\("KIT", )\[[^\]]*\]', lambda m: m.group(1) + json.dumps(names).replace('","', '", "'), ed)
    assert n == 1, "KIT list"
    kits = re.findall(r'\{"([^"]*)", "[^"]*", (0x[0-9A-Fa-f]+|\d+), \{([\s\S]*?)\}\},', hdr)
    raw = bytearray()
    for _, crush, body in kits:
        raw.append(int(crush, 0))
        for row in re.findall(r"\{([^{}]*)\},", body):
            raw.extend(int(x) & 255 for x in row.split(","))
    ed, n1 = re.subn(r"const DSYN_MOCK_NAMES = \[[^\]]*\];", "const DSYN_MOCK_NAMES = " + json.dumps([k[0] for k in kits]).replace('","', '", "') + ";", ed)
    ed, n2 = re.subn(r'const DSYN_MOCK_KITS = "[^"]*";', 'const DSYN_MOCK_KITS = "' + base64.b64encode(bytes(raw)).decode() + '";', ed)
    assert n1 == 1 and n2 == 1, "mock kits"
    ed_path.write_text(ed, encoding="utf-8")
    print(f"editor kits: {len(names)} on the drum track, {len(kits)} synthesised ({len(raw)} B) -> web/editor.html")


if __name__ == "__main__":
    main()
