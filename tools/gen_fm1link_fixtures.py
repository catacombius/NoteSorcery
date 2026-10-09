#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The fixtures android/fm1link's tests check the Kotlin code against, from the other implementations:

  live.nsp1, secA.nsp1, secB.nsp1, layout.json   the firmware's (tests/nsp1_export.c)
  notes.json                                     web/als/nsals.js's notes for them (node)
  slot.pcm, slot.hdr, slot.bin                   tools/sampleio.py's sample slot for two test tones
  pack7.json                                     tools/fm1_sample_upload.py's pack7 of a byte ramp

  tools/gen_fm1link_fixtures.py     (after ./build.sh; builds and runs nsp1_export)
"""
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import sampleio as sio  # noqa: E402
from fm1_sample_upload import pack7  # noqa: E402

OUT = ROOT / "android/fm1link/src/test/resources"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = ROOT / "build/host/nsp1_export"
    subprocess.run(["cc", "-O2", "-w", "-ffp-contract=off", "-Ibuild/gen", "-Ifirmware/src", "-Ifirmware/hal", "-o", str(exe),
                    "tests/nsp1_export.c", "-lm"], cwd=ROOT, check=True)
    subprocess.run([str(exe), str(OUT)], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    (OUT / "played.json").unlink()
    js = """
import { readFileSync } from "node:fs";
import { decodeNsp1, trackClip, songFromDevice } from "./web/als/nsals.js";
const d = process.argv[1], rd = (n) => new Uint8Array(readFileSync(d + "/" + n));
const prj = decodeNsp1(rd("live.nsp1"));
const clips = [0, 1, 2, 3, 4, 5, 6, 7].map((t) => trackClip(prj, t, { drumMap: "gm" }));
const song = songFromDevice({ working: rd("live.nsp1"), sections: [rd("secA.nsp1"), rd("secB.nsp1"), null, null] });
console.log(JSON.stringify({ clips, song }));
"""
    r = subprocess.run(["node", "--input-type=module", "-e", js, str(OUT)], cwd=ROOT, check=True, capture_output=True, text=True)
    (OUT / "notes.json").write_text(r.stdout)
    tones = []
    for f, n in ((220.0, 6000), (440.0, 4000)):
        x = [math.sin(2 * math.pi * f * i / sio.SLOT_RATE) * math.exp(-i / 3000) for i in range(n)]
        tones.append(sio.to_int16(x))
    (OUT / "slot.pcm").write_bytes(b"".join(struct.pack(f"<{len(t)}h", *t) for t in tones))
    hdr, data = sio.user_slot("Test Tone", [(tones[0], 57, None, None), (tones[1], 69, None, None)])
    (OUT / "slot.hdr").write_bytes(hdr)
    (OUT / "slot.bin").write_bytes(data)
    ramp = [(i * 37) & 255 for i in range(23)]
    (OUT / "pack7.json").write_text(json.dumps({"bytes": ramp, "pack7": pack7(ramp)}))
    print(f"fm1link fixtures -> {OUT}")


if __name__ == "__main__":
    main()
