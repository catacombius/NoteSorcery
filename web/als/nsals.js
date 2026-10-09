/* SPDX-License-Identifier: GPL-3.0-only */
/* NoteSorcery: sketch on the FM-1, carry on in Ableton Live -- without an Android device.
 *
 * Reads the FM-1's projects as the device stores them (NSP1, firmware/src/project.c project_t: the working
 * project is backup object 0, the song sections A..D objects 2..5, web/EDITOR_PROTOCOL.md BK_GET) and writes
 *   - an Ableton Live Set (.als: gzipped XML, Live 11 schema, opens in Live 11 and 12): eight MIDI tracks, the
 *     sections as scenes (Session View) and one after the other in the Arrangement View;
 *   - a Standard MIDI File (type 1) of the same song.
 * The steps become notes as the FM-1 plays them (seq.c): the track's DIV, LEN, GATE, swing, nudge, ties,
 * slides (legato), ratchets and dynamics; a step that plays only during a fill is left out.
 * The Live Set writer and the MIDI writer are ports of NoteMove's LiveSetExporter.kt and MidiFileWriter.kt; the
 * set skeleton is NoteMove's templates (als_templates.js, tools/gen_als_templates.py).
 *
 * Plain ES module, no dependencies: the browser editor imports it, and node runs its tests (test_als.mjs). */
import { TEMPLATES } from "./als_templates.js";

/* ---- the device's layout (core.h, project.c). tests/nsp1_layout.c prints the firmware's own and test_als.mjs
 * compares it with this, so a firmware change that moves a field fails the tests, not a user's export. */
export const LAYOUT = {
  magic: 0x3150534e,           /* "NSP1" */
  size: 7604,
  P_COUNT: 61, G_COUNT: 34, NTRK: 8, NPART: 6, NSTEP: 64, NLOCK: 24,
  P: { LEVEL: 0, ROOT: 25, SCALE: 26, TRANS: 28, SLEN: 29, SDIV: 30, SSWING: 31, SGATE: 32, PAN: 39, MUTE: 40, E0: 53 },
  G: { BPM: 0, SWING: 1 },
  ENGINES: ["ANALOG", "TRIO", "FM6", "SAMPLE", "ACID", "WAVE"],
};

/* the FM-1's scales (params.c N_SCALE, seq.c SCALE_MASK) as Live names them */
const LIVE_SCALE = ["Chromatic", "Major", "Minor", "Dorian", "Mixolydian", "Major Pentatonic", "Minor Pentatonic",
  "Harmonic Minor", "Phrygian", "Lydian", "Locrian", "Melodic Minor", "Minor Blues", "Whole Tone", "Half-whole Dim.",
  "Whole-half Dim."];
/* the drum lanes (drums.c LANE_NOTE: the GM notes the FM-1 plays and sends) */
export const LANE_NOTE = [36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56];
export const LANE_NAME = ["KICK", "KICK 2", "SNARE", "CLAP", "CL HAT", "OP HAT", "PEDAL HAT", "RIM", "SNARE 2", "TOM LO",
  "TOM HI", "CRASH", "RIDE", "SHAKER", "CONGA", "COWBELL"];
/* the track colours as Live's palette indices: OP-1 blue, green, white(ish), orange, then the rest */
const LIVE_COLOR = [9, 5, 13, 1, 11, 7, 14, 3];

const DIV_DEN = [1, 2, 4, 8, 3, 6];       /* core.h: steps 0..5 are 1/DEN beats, 6..8 two, four, eight beats */
const DIV_BEATS = [2, 4, 8];
const ST_NOTE = 0, ST_TIE = 1, ST_REST = 2;
const SF_ACCENT = 1, SF_SLIDE = 2;
const FC_FILL = 1;
const LV_VEL = [0, 42, 72, 127];          /* drums.c lvl_vel: as played, ghost, soft, hard */

function fnv1a(b, n) {
  let s = 0x811c9dc5;
  for (let i = 0; i < n; i++) s = Math.imul(s ^ b[i], 16777619) >>> 0;
  return s >>> 0;
}

/* ---- NSP1 bytes -> a plain object. Throws on anything that is not a project of this format. */
export function decodeNsp1(bytes, L = LAYOUT) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (b.length !== L.size) throw new Error(`not an NSP1 project (${b.length} bytes, expected ${L.size})`);
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const u32 = (o) => dv.getUint32(o, true), i16 = (o) => dv.getInt16(o, true);
  if (u32(0) !== L.magic || u32(4) !== L.size) throw new Error("not an NSP1 project (magic / size)");
  if (u32(L.size - 4) !== fnv1a(b, L.size - 4)) throw new Error("NSP1 project damaged (checksum)");
  let o = 8;
  const g = [];
  for (let i = 0; i < L.G_COUNT; i++, o += 2) g.push(i16(o));
  const sel = b[o], ntrk = b[o + 2];
  o += 4;
  if (ntrk !== L.NTRK) throw new Error(`NSP1 with ${ntrk} tracks (expected ${L.NTRK})`);
  const tracks = [];
  for (let t = 0; t < L.NTRK; t++) {
    const p = [];
    for (let i = 0; i < L.P_COUNT; i++, o += 2) p.push(i16(o));
    const engine = b[o], preset = b[o + 1];
    o += 2;
    const raw = [];
    for (let s = 0; s < L.NSTEP; s++, o += 10) raw.push(b.slice(o, o + 10));
    const micro = [];
    for (let s = 0; s < L.NSTEP; s++, o++) micro.push((b[o] << 24) >> 24);
    const locks = [];
    for (let k = 0; k < L.NLOCK; k++, o += 4) if (b[o] !== 0xff) locks.push({ step: b[o], param: b[o + 1], val: i16(o + 2) });
    const fill = b.slice(o, o + L.NSTEP / 4);
    o += L.NSTEP / 4;
    tracks.push({ p, engine, preset, raw, micro, locks, fill, drum: t >= L.NPART });
  }
  if (o !== L.size - 4) throw new Error("NSP1 layout mismatch");
  return { g, sel, tracks };
}

/* a track's division -> beats per step */
function stepBeats(div) {
  div = ((div % 9) + 9) % 9;
  return div < 6 ? 1 / DIV_DEN[div] : DIV_BEATS[div - 6];
}

/* ---- one track of a decoded project -> a clip: {lengthBeats, notes: [{pitch, start, duration, velocity}]} */
export function trackClip(prj, ti, opts = {}, L = LAYOUT) {
  const t = prj.tracks[ti], P = L.P;
  const len = Math.max(1, Math.min(L.NSTEP, t.p[P.SLEN] || 1));
  const div = t.p[P.SDIV], sb = stepBeats(div);
  const swingPct = Math.max(0, Math.min(100, t.p[P.SSWING] + prj.g[L.G.SWING]));
  const sw = ((div % 9) + 9) % 9 < 4 ? (swingPct * sb) / 200 : 0;          /* seq.c swings / swing_units */
  const at = (i) => i * sb + (i & 1 ? sw : 0) + (t.micro[i] / 64) * sb; /* a step's start (swing, nudge) */
  const slen = (i) => sb + (i & 1 ? -sw : sw);      /* swung: an even step lasts until the late odd one */
  const gateOf = (i) => (slen(i) * Math.max(1, t.p[P.SGATE])) / 128;
  const fillOnly = (i) => ((t.fill[i >> 2] >> (2 * (i & 3))) & 3) === FC_FILL;
  const notes = [];
  const lengthBeats = len * sb;
  const push = (pitch, start, dur, vel) => {
    if (start < 0) start = 0;
    if (start >= lengthBeats) return;
    notes.push({ pitch, start: +start.toFixed(6), duration: +Math.max(0.001, dur).toFixed(6), velocity: Math.max(1, Math.min(127, vel | 0)) });
  };
  for (let i = 0; i < len; i++) {
    const r = t.raw[i];
    if (fillOnly(i)) continue;
    if (t.drum) {
      const on = r[0] | (r[1] << 8);
      for (let l = 0; l < 16; l++) {
        if (!((on >> l) & 1)) continue;
        const lvl = (r[2 + (l >> 2)] >> ((l & 3) * 2)) & 3, hits = 1 + ((r[6 + (l >> 2)] >> ((l & 3) * 2)) & 3);
        const pitch = opts.drumMap === "gm" ? LANE_NOTE[l] : 36 + l;
        const vel = lvl ? LV_VEL[lvl] : 100;
        for (let h = 0; h < hits; h++) push(pitch, at(i) + (h * slen(i)) / hits, slen(i) / hits, vel);   /* (until the next hit) */
      }
      continue;
    }
    const n = r[4], time = r[5], flags = r[6], svel = r[7], lvl = r[8], rat = r[9];
    if (time !== ST_NOTE || !n) continue;
    let ties = 0;                                   /* the TIE steps after it hold the notes */
    while (i + ties + 1 < len && t.raw[i + ties + 1][5] === ST_TIE) ties++;
    const lastFlags = t.raw[i + ties][6];
    const slide = (lastFlags & SF_SLIDE) !== 0 && i + ties + 1 < len;
    for (let k = 0; k < Math.min(n, 4); k++) {
      const base = flags & SF_ACCENT ? 127 : svel || 96;
      const lv = (lvl >> (2 * k)) & 3, vel = lv ? LV_VEL[lv] : base;
      const hits = ties ? 1 : 1 + ((rat >> (2 * k)) & 3);
      const held = at(i + ties) - at(i);           /* (the TIE steps) */
      let dur;
      if (slide) dur = held + slen(i + ties) + sb / 16;   /* legato into the next step: a glide on a mono synth */
      else if (ties) dur = held + Math.min(gateOf(i + ties) + slen(i + ties) / 2, slen(i + ties));
      else dur = gateOf(i) / hits;
      for (let h = 0; h < hits; h++) push(r[k], at(i) + (h * slen(i)) / hits, dur, vel);
    }
    i += ties;
  }
  notes.sort((a, c) => a.start - c.start || a.pitch - c.pitch);
  return { lengthBeats, notes };
}

function levelToGain(lvl) {                       /* LEVEL: 104 = 0 dB, half dB steps, 0 = off (fx.c LEVEL_Q12) */
  if (lvl <= 0) return 0.0003162278;
  return Math.min(1.99526, Math.max(0.0003162278, Math.pow(10, (lvl - 104) / 40)));
}

/* ---- the FM-1's song -> a set: { name, tempo, rootNote, scaleName, sceneCount, sceneNames, tracks[] } (the shape
 * of NoteMove's Project). src: { working: NSP1 bytes, sections: [A, B, C, D] (NSP1 bytes or null) }. With sections,
 * each is a scene (A..D); without, the working project is scene 1. */
export function songFromDevice(src, opts = {}, L = LAYOUT) {
  const scenes = [];
  (src.sections || []).forEach((s, k) => { if (s) scenes.push({ name: "ABCD"[k] || `S${k + 1}`, prj: decodeNsp1(s, L) }); });
  if (!scenes.length || opts.withWorking) {
    if (!src.working) throw new Error("no project to export");
    scenes.unshift({ name: scenes.length ? "LIVE" : "A", prj: decodeNsp1(src.working, L) });
  }
  const head = scenes[0].prj, P = L.P;
  const key = head.tracks.find((t, i) => i < L.NPART && t.p[P.SCALE]) || head.tracks[0];
  const tracks = [];
  for (let ti = 0; ti < L.NTRK; ti++) {
    const drum = ti >= L.NPART, t0 = head.tracks[ti];
    const name = drum ? `DRUMS ${ti - L.NPART + 1}` : `T${ti + 1} ${L.ENGINES[t0.engine] || "SYNTH"}`;
    const clips = {};
    scenes.forEach((sc, s) => {
      const c = trackClip(sc.prj, ti, opts, L);
      if (c.notes.length) clips[s] = { name: `${sc.name} ${name}`, ...c };
    });
    tracks.push({
      name, kind: drum ? "drums" : "synth", color: LIVE_COLOR[ti % LIVE_COLOR.length],
      volume: levelToGain(t0.p[P.LEVEL]), pan: Math.max(-1, Math.min(1, t0.p[P.PAN] / 64)), mute: !!t0.p[P.MUTE], clips,
    });
  }
  return {
    name: opts.name || "FM-1 sketch", tempo: Math.max(20, Math.min(999, head.g[L.G.BPM] || 120)),
    rootNote: ((key.p[P.ROOT] % 12) + 12) % 12, scaleName: LIVE_SCALE[key.p[P.SCALE]] || "Minor",
    sceneCount: scenes.length, sceneNames: scenes.map((s) => s.name), tracks,
  };
}

/* ---- the Live Set (NoteMove LiveSetExporter.buildXml) ---- */
export function num(v) {
  if (Number.isInteger(v) && Math.abs(v) < 1e15) return String(v);
  return v.toFixed(6).replace(/0+$/, "").replace(/\.$/, "");
}
export function xmlEscape(s) {
  let o = "";
  for (const c of String(s)) {
    if (c === "&") o += "&amp;";
    else if (c === "<") o += "&lt;";
    else if (c === ">") o += "&gt;";
    else if (c === '"') o += "&quot;";
    else if (c === "'") o += "&apos;";
    else if (c.charCodeAt(0) >= 0x20 || c === "\t") o += c;
  }
  return o;
}
const rep = (s, k, v) => s.split(k).join(v);
const indent = (s, p) => s.split("\n").map((l) => (l.trim() ? p + l : l)).join("\n");

function usedScenes(project) {
  const out = [];
  for (let s = 0; s < project.sceneCount; s++) if (project.tracks.some((t) => t.clips[s])) out.push(s);
  return out;
}
function sceneLength(project, s) {
  let m = 0;
  for (const t of project.tracks) if (t.clips[s]) m = Math.max(m, t.clips[s].lengthBeats);
  return m || 4;
}

function clipXml(project, clip, color, id, time, length, session, T) {
  let keyTracks = "", noteId = 1;
  const byPitch = new Map();
  for (const n of clip.notes) (byPitch.get(n.pitch) || byPitch.set(n.pitch, []).get(n.pitch)).push(n);
  [...byPitch.keys()].sort((a, b) => a - b).forEach((pitch, i) => {
    keyTracks += `\t\t\t\t<KeyTrack Id="${i}">\n\t\t\t\t\t<Notes>\n`;
    for (const n of byPitch.get(pitch).sort((a, b) => a.start - b.start))
      keyTracks += `\t\t\t\t\t\t<MidiNoteEvent Time="${num(n.start)}" Duration="${num(Math.max(0.001, n.duration))}" ` +
        `Velocity="${Math.max(1, Math.min(127, n.velocity))}" VelocityDeviation="0" OffVelocity="64" Probability="1" ` +
        `IsEnabled="true" NoteId="${noteId++}"/>\n`;
    keyTracks += `\t\t\t\t\t</Notes>\n\t\t\t\t\t<MidiKey Value="${pitch}"/>\n\t\t\t\t</KeyTrack>\n`;
  });
  const start = session ? 0 : time, end = session ? clip.lengthBeats : time + length;
  let x = T.clip;
  x = rep(x, "{{CLIP_ID}}", String(id));
  x = rep(x, "{{TIME}}", num(start));
  x = rep(x, "{{CURRENT_START}}", num(start));
  x = rep(x, "{{CURRENT_END}}", num(end));
  x = rep(x, "{{LENGTH}}", num(clip.lengthBeats));
  x = rep(x, "{{LOOP_ON}}", "true");
  x = rep(x, "{{NAME}}", xmlEscape(clip.name || ""));
  x = rep(x, "{{COLOR}}", String(color));
  x = rep(x, "{{KEY_TRACKS}}", keyTracks.trimEnd());
  x = rep(x, "{{NEXT_NOTE_ID}}", String(noteId));
  x = rep(x, "{{ROOT_NOTE}}", String(project.rootNote));
  x = rep(x, "{{SCALE_NAME}}", xmlEscape(project.scaleName));
  x = rep(x, "{{IN_KEY}}", "false");
  return indent(x, "\t\t\t\t\t\t\t\t\t\t");
}

/* Live's automation targets need ids unique in the set: the template marks them Id="@@" */
function idFill(xml, ids) {
  return xml.split('Id="@@"').reduce((acc, part, i) => (i ? acc + `Id="${ids.next++}"` + part : part), "");
}

export function buildLiveSetXml(project, opts = {}, T = TEMPLATES) {
  const repeats = Math.max(1, opts.arrangementRepeats ?? 2), arrangement = opts.arrangement ?? true;
  const ids = { next: 40000 };
  const sceneCount = Math.max(project.sceneCount, 1);
  const sceneTimes = new Map();
  let t = 0;
  for (const s of usedScenes(project)) {
    const len = sceneLength(project, s) * repeats;
    sceneTimes.set(s, [t, len]);
    t += len;
  }
  let tracksXml = "";
  project.tracks.forEach((track, index) => {
    const color = track.color;
    let slots = "", freeze = "", arr = "";
    for (let s = 0; s < sceneCount; s++) {
      const clip = track.clips[s];
      slots += `\t\t\t\t\t\t\t<ClipSlot Id="${s}">\n\t\t\t\t\t\t\t\t<LomId Value="0"/>\n\t\t\t\t\t\t\t\t<ClipSlot>\n`;
      slots += clip ? `\t\t\t\t\t\t\t\t\t<Value>\n${clipXml(project, clip, color, 0, 0, clip.lengthBeats, true, T)}\n\t\t\t\t\t\t\t\t\t</Value>\n`
        : "\t\t\t\t\t\t\t\t\t<Value/>\n";
      slots += "\t\t\t\t\t\t\t\t</ClipSlot>\n\t\t\t\t\t\t\t\t<HasStop Value=\"true\"/>\n\t\t\t\t\t\t\t\t<NeedRefreeze Value=\"true\"/>\n\t\t\t\t\t\t\t</ClipSlot>\n";
      freeze += `\t\t\t\t\t\t\t<ClipSlot Id="${s}">\n\t\t\t\t\t\t\t\t<LomId Value="0"/>\n\t\t\t\t\t\t\t\t<ClipSlot>\n\t\t\t\t\t\t\t\t\t<Value/>\n` +
        "\t\t\t\t\t\t\t\t</ClipSlot>\n\t\t\t\t\t\t\t\t<HasStop Value=\"true\"/>\n\t\t\t\t\t\t\t\t<NeedRefreeze Value=\"true\"/>\n\t\t\t\t\t\t\t</ClipSlot>\n";
    }
    if (arrangement) {
      let clipId = 0;
      for (const [s, [start, len]] of sceneTimes) {
        const clip = track.clips[s];
        if (clip) arr += clipXml(project, clip, color, clipId++, start, len, false, T) + "\n";
      }
    }
    let x = T.track;
    x = rep(x, "{{TRACK_ID}}", String(10 + index));
    x = rep(x, "{{NAME}}", xmlEscape(track.name));
    x = rep(x, "{{COLOR}}", String(color));
    x = rep(x, "{{SPEAKER_ON}}", track.mute ? "false" : "true");
    x = rep(x, "{{PAN}}", num(track.pan));
    x = rep(x, "{{VOLUME}}", num(track.volume));
    x = rep(x, "{{CLIPSLOTS}}", slots.trimEnd());
    x = rep(x, "{{FREEZE_CLIPSLOTS}}", freeze.trimEnd());
    x = rep(x, "{{ARRANGEMENT_CLIPS}}", arr.trimEnd());
    tracksXml += idFill(x, ids) + "\n";
  });
  let scenes = "";
  for (let s = 0; s < sceneCount; s++) {
    let x = T.scene;
    x = rep(x, "{{SCENE_ID}}", String(s));
    x = rep(x, "{{NAME}}", xmlEscape(project.sceneNames[s] || ""));
    x = rep(x, "{{TEMPO}}", String(Math.round(project.tempo)));
    scenes += x + "\n";
  }
  let tail = T.tail;
  tail = rep(tail, "{{SCENES}}", scenes.trimEnd());
  tail = rep(tail, "{{TEMPO}}", num(project.tempo));
  tail = rep(tail, "{{ROOT_NOTE}}", String(project.rootNote));
  tail = rep(tail, "{{SCALE_NAME}}", xmlEscape(project.scaleName));
  const xml = T.head.trimEnd() + "\n" + tracksXml + tail;
  return rep(xml, "{{NEXT_POINTEE_ID}}", String(ids.next + 1));
}

/* gzip (the .als container): the browser's CompressionStream, else node's zlib */
export async function gzip(bytes) {
  if (typeof CompressionStream !== "undefined") {
    const s = new Blob([bytes]).stream().pipeThrough(new CompressionStream("gzip"));
    return new Uint8Array(await new Response(s).arrayBuffer());
  }
  const zlib = await import("node:zlib");
  return new Uint8Array(zlib.gzipSync(bytes));
}
export async function exportAls(project, opts = {}) {
  return gzip(new TextEncoder().encode(buildLiveSetXml(project, opts)));
}

/* ---- a Standard MIDI File, type 1 (NoteMove MidiFileWriter.song): the used scenes in order, each repeats times */
const PPQ = 480;
function vlq(v) {
  v = Math.max(0, v | 0);
  const b = [v & 0x7f];
  for (v >>= 7; v > 0; v >>= 7) b.unshift((v & 0x7f) | 0x80);
  return b;
}
const be32 = (v) => [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
const ascii = (s) => [...new TextEncoder().encode(s)];
function chunk(events) {
  const body = [];
  let last = 0;
  for (const e of events) {
    body.push(...vlq(e.tick - last), ...e.bytes);
    last = e.tick;
  }
  body.push(0, 0xff, 0x2f, 0x00);
  return [...ascii("MTrk"), ...be32(body.length), ...body];
}
export function midiSong(project, repeats = 1) {
  const tick = (beat) => Math.round(beat * PPQ);
  const mpqn = Math.round(60000000 / project.tempo), nm = ascii(project.name);
  const tracks = [chunk([
    { tick: 0, bytes: [0xff, 0x03, ...vlq(nm.length), ...nm] },
    { tick: 0, bytes: [0xff, 0x51, 0x03, (mpqn >> 16) & 255, (mpqn >> 8) & 255, mpqn & 255] },
    { tick: 0, bytes: [0xff, 0x58, 0x04, 4, 2, 24, 8] },
  ])];
  const scenes = usedScenes(project);
  project.tracks.forEach((track, ti) => {
    const ch = track.kind === "drums" ? 9 : ti % 15 >= 9 ? (ti % 15) + 1 : ti % 15;
    const ev = [];
    let offset = 0;
    for (const s of scenes) {
      const span = sceneLength(project, s) * repeats, clip = track.clips[s];
      if (clip)
        for (let loop = 0; loop < span - 1e-9; loop += clip.lengthBeats)
          for (const n of clip.notes) {
            const start = loop + n.start;
            if (start >= span) continue;
            const end = Math.min(start + n.duration, span, loop + clip.lengthBeats + n.duration);
            const on = tick(offset + start), off = Math.max(on + 1, tick(offset + end));
            ev.push({ tick: on, order: 1, bytes: [0x90 | ch, n.pitch, Math.max(1, Math.min(127, n.velocity))] });
            ev.push({ tick: off, order: 0, bytes: [0x80 | ch, n.pitch, 64] });
          }
      offset += span;
    }
    ev.sort((a, b) => a.tick - b.tick || a.order - b.order);
    const name = ascii(track.name);
    tracks.push(chunk([{ tick: 0, bytes: [0xff, 0x03, ...vlq(name.length), ...name] }, ...ev]));
  });
  const out = [...ascii("MThd"), ...be32(6), 0, 1, (tracks.length >> 8) & 255, tracks.length & 255, (PPQ >> 8) & 255, PPQ & 255];
  for (const t of tracks) out.push(...t);
  return new Uint8Array(out);
}
