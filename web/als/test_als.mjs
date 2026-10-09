// SPDX-License-Identifier: GPL-3.0-only
// The browser's Ableton export (nsals.js) against the firmware: tests/nsp1_export.c writes a project with every
// step feature, two song sections, the firmware's NSP1 layout and the MIDI the FM-1 sends while it plays the
// project; here the export must
//   - read the layout the firmware has (indices, sizes), and refuse a damaged project;
//   - turn the steps into the notes the FM-1 plays: every note-on at its time (within a block), velocity and
//     length (to the note-off), the fill-only step left out;
//   - make the sections scenes; write a Live Set whose XML is well-formed, complete (8 MIDI tracks, every
//     placeholder filled, automation ids unique and under NextPointeeId) and gzipped; a type-1 MIDI file with
//     every note.
//   node web/als/test_als.mjs build/host/als
import { execFileSync } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { gunzipSync } from "node:zlib";
import { LAYOUT, buildLiveSetXml, decodeNsp1, exportAls, midiSong, songFromDevice, trackClip } from "./nsals.js";

const dir = process.argv[2] || "build/host/als";
let bad = 0;
const check = (ok, what) => {
  console.log(`als: ${what.padEnd(78)} ${ok ? "ok" : "FAIL"}`);
  if (!ok) bad++;
};
const rd = (n) => new Uint8Array(readFileSync(join(dir, n)));

const lay = JSON.parse(readFileSync(join(dir, "layout.json"), "utf8"));
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
check(lay.magic === LAYOUT.magic && lay.size === LAYOUT.size && ["P_COUNT", "G_COUNT", "NTRK", "NPART", "NSTEP", "NLOCK"].every((k) => lay[k] === LAYOUT[k]) &&
  same(lay.P, LAYOUT.P) && same(lay.G, LAYOUT.G) && same(lay.ENGINES, LAYOUT.ENGINES),
  "the firmware's NSP1 layout, P_ / G_ indices and engines are the exporter's");
check(lay.offsets.t + lay.NTRK * lay.offsets.trk === lay.offsets.sum && lay.offsets.step === 2 * lay.P_COUNT + 2 &&
  lay.offsets.micro === lay.offsets.step + 10 * lay.NSTEP, "... and its track record (sound, steps, nudges, locks, fills) as read");

const live = rd("live.nsp1");
{
  const d = live.slice();
  d[200] ^= 1;
  let msg = "";
  try { decodeNsp1(d); } catch (e) { msg = e.message; }
  check(/checksum/.test(msg), "a damaged project: refused (checksum)");
}

// ---- the notes against what the FM-1 played (two loops of 4 beats at 120 BPM: loop 1 compared)
const played = JSON.parse(readFileSync(join(dir, "played.json"), "utf8"));
const spb = (played.fs * 60) / played.bpm, loop = 4 * spb, TOL = 40, TOL_END = 72;   // a block (32) + rounding; ends: on and off each a block
const prj = decodeNsp1(live);
for (let ti = 0; ti < LAYOUT.NTRK; ti++) {
  const ch = played.ch[ti];
  const clip = trackClip(prj, ti, { drumMap: "gm" });
  const ons = played.events.filter((e) => e[1] === (0x90 | ch) && e[0] < loop - TOL);
  const offs = played.events.filter((e) => e[1] === (0x80 | ch));
  const ex = clip.notes.map((n) => ({ ...n, at: n.start * spb, end: (n.start + n.duration) * spb }));
  if (!ons.length && !ex.length) continue;
  let ok = ons.length === ex.length, why = `${ex.length} notes exported, ${ons.length} played`;
  const used = new Set();
  for (const e of ons) {
    const k = ex.findIndex((n, i) => !used.has(i) && n.pitch === e[2] && Math.abs(n.at - e[0]) <= TOL);
    if (k < 0) { ok = false; why = `note ${e[2]} at ${e[0]} not exported`; break; }
    used.add(k);
    const n = ex[k];
    if (n.velocity !== e[3]) { ok = false; why = `note ${e[2]} at ${e[0]}: velocity ${n.velocity}, played ${e[3]}`; break; }
    const off = offs.find((o) => o[2] === e[2] && o[0] > e[0]);
    if (!off) continue;
    const slide = ex.some((m) => m !== n && m.at > n.at + TOL && m.at < n.end);   // a legato note: past the next start
    if (slide ? n.end < off[0] - TOL_END : Math.abs(n.end - off[0]) > TOL_END) {
      ok = false; why = `note ${e[2]} at ${e[0]}: ends ${n.end.toFixed(0)}, played until ${off[0]}`; break;
    }
  }
  check(ok, `track ${ti + 1}: ${ex.length} notes, each when, as loud and as long as the FM-1 plays it` + (ok ? "" : ` (${why})`));
}
{
  const c = trackClip(prj, 0);
  check(!c.notes.some((n) => n.pitch === 60) && c.notes.filter((n) => n.pitch === 48).length === 3 &&
    c.notes.some((n) => n.pitch === 51 && n.velocity === 42) && c.lengthBeats === 4,
    "the bass: the fill-only step left out, the x3 ratchet, the ghost note, 16 sixteenths = 4 beats");
  const d = trackClip(prj, LAYOUT.NPART);
  check(d.notes.every((n) => n.pitch >= 36 && n.pitch < 52) && d.notes.some((n) => n.pitch === 36) && d.notes.some((n) => n.pitch === 36 + 4),
    "drums: Drum Rack pads by default (lane k = note 36 + k: kick C1, closed hat E1)");
}

// ---- sections -> scenes, the Live Set, the MIDI file
const song = songFromDevice({ working: live, sections: [rd("secA.nsp1"), rd("secB.nsp1"), null, null] }, { name: "test & <sketch>" });
check(song.sceneCount === 2 && same(song.sceneNames, ["A", "B"]) && song.tracks.length === 8 && song.tempo === 120 &&
  song.tracks[0].clips[0].lengthBeats === 4 && song.tracks[0].clips[1].lengthBeats === 8 && !song.tracks[7].clips[0],
  "sections A, B: two scenes; the bass 4 beats in A, 32 steps = 8 beats in B; the empty drum track no clip");
check(/^T1 ACID$/.test(song.tracks[0].name) && /^T2 FM6$/.test(song.tracks[1].name) && song.tracks[6].kind === "drums",
  "track names from the engines; tracks 7 and 8 the drum tracks");
const xml = buildLiveSetXml(song);
const count = (re) => (xml.match(re) || []).length;
check(count(/<MidiTrack Id=/g) === 8 && count(/<Scene Id=/g) === 2 && !xml.includes("{{") && !xml.includes('Id="@@"'),
  "the set: 8 MIDI tracks, 2 scenes, every placeholder filled");
{
  const next = +xml.match(/<NextPointeeId Value="(\d+)"/)[1];
  const ids = [...xml.matchAll(/ Id="(\d+)"/g)].map((m) => +m[1]).filter((v) => v >= 40000);
  check(ids.length > 100 && new Set(ids).size === ids.length && Math.max(...ids) < next, `automation ids unique (${ids.length}) and under NextPointeeId`);
}
const nNotes = song.tracks.reduce((a, t) => a + Object.values(t.clips).reduce((b, c) => b + c.notes.length, 0), 0);
check(count(/<MidiNoteEvent /g) === nNotes * 2, `every note in its session clip and in the arrangement (${nNotes} x 2)`);
check(xml.includes('<Name Value="test &amp; &lt;sketch&gt;"/>') || !xml.includes("test & <sketch>"), "names escaped");
const als = await exportAls(song);
writeFileSync(join(dir, "sketch.als"), als);
check(als[0] === 0x1f && als[1] === 0x8b && gunzipSync(als).toString("utf8") === xml, "the .als: gzip of the XML");
try {
  execFileSync("python3", ["-c", "import gzip,sys,xml.etree.ElementTree as E; r=E.fromstring(gzip.open(sys.argv[1]).read()); " +
    "assert r.tag=='Ableton'; ls=r.find('LiveSet'); assert len(ls.find('Tracks'))==8; assert len(ls.find('Scenes'))==2", join(dir, "sketch.als")]);
  check(true, "well-formed XML (Python's parser): Ableton > LiveSet, 8 tracks, 2 scenes");
} catch (e) {
  check(false, "well-formed XML (Python's parser): " + String(e.stderr || e.message).trim().split("\n").pop());
}
{
  const mid = midiSong(song, 1);
  writeFileSync(join(dir, "sketch.mid"), mid);
  const dv = new DataView(mid.buffer);
  let o = 14, ons = 0, ntrk = 0;
  while (o < mid.length) {
    const len = dv.getUint32(o + 4);
    let p = o + 8, run = 0;
    const end = p + len;
    ntrk++;
    while (p < end) {
      while (mid[p++] & 0x80);                              // delta
      let st = mid[p];
      if (st === 0xff) { p += 2; let l = 0, c; do { c = mid[p++]; l = (l << 7) | (c & 0x7f); } while (c & 0x80); p += l; continue; }
      if (st & 0x80) p++; else st = run;
      run = st;
      if ((st & 0xf0) === 0x90) ons++;
      p += 2;
    }
    o = end;
  }
  // (a scene plays as long as its longest clip: the 4-beat clips twice in the 8-beat scene B)
  const expect = song.tracks.reduce((a, t) => a + [0, 1].reduce((b, s) => b + (t.clips[s] ? t.clips[s].notes.length * (8 * s + 4 * (1 - s)) / t.clips[s].lengthBeats : 0), 0), 0);
  check(mid[0] === 0x4d && dv.getUint16(10) === 9 && ntrk === 9 && ons === expect, `the MIDI file: type 1, 9 tracks, ${ons} notes (looped to each scene)`);
}
console.log(`als: ${bad ? "FAILED" : "all checks ok"}`);
process.exit(bad ? 1 : 0);
