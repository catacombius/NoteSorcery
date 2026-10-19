#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The fixtures android/botanica's core tests check the Kotlin code against (BTP1 projects, as the BOTANICA firmware
writes them, and web/als/nsals.js's notes for them):

  live.btp1, secA.btp1, secB.btp1, layout.json   tests/nsp1_export.c built with EDITION_BOTANICA
  notes.json                                     nsals.js (LAYOUT_BOTANICA) for them

  tools/gen_botanica_app_fixtures.py     (after ./build.sh --edition botanica, for build/botanica/gen)
"""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "android/botanica/core/src/test/resources"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "nsp1_export"
        subprocess.run(["cc", "-O2", "-w", "-ffp-contract=off", "-DEDITION_BOTANICA=1", "-Ibuild/botanica/gen", "-Ifirmware/src",
                        "-Ifirmware/hal", "-o", str(exe), "tests/nsp1_export.c", "-lm"], cwd=ROOT, check=True)
        subprocess.run([str(exe), tmp], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        for n in ("live", "secA", "secB"):
            (OUT / f"{n}.btp1").write_bytes((Path(tmp) / f"{n}.nsp1").read_bytes())
        (OUT / "layout.json").write_bytes((Path(tmp) / "layout.json").read_bytes())
    js = """
import { readFileSync } from "node:fs";
import { decodeNsp1, trackClip, songFromDevice } from "./web/als/nsals.js";
const d = process.argv[1], rd = (n) => new Uint8Array(readFileSync(d + "/" + n));
const prj = decodeNsp1(rd("live.btp1"));
const clips = [0, 1, 2, 3, 4, 5, 6, 7].map((t) => trackClip(prj, t, { drumMap: "gm" }));
const song = songFromDevice({ working: rd("live.btp1"), sections: [rd("secA.btp1"), rd("secB.btp1"), null, null] });
console.log(JSON.stringify({ clips, song }));
"""
    r = subprocess.run(["node", "--input-type=module", "-e", js, str(OUT)], cwd=ROOT, check=True, capture_output=True, text=True)
    (OUT / "notes.json").write_text(r.stdout)
    print(f"fixtures -> {OUT}")


if __name__ == "__main__":
    main()
