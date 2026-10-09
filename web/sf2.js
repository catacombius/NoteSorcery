/* SPDX-License-Identifier: GPL-3.0-only */
/* NoteSorcery: SoundFont 2 (.sf2) presets -> a user sample slot (the SAMPLES page of the editor).
 *
 * An .sf2 file does not fit the FM-1 (1 MiB of flash), but one preset does, reduced: one velocity layer (the loudest),
 * at most 16 key zones, 22.05 kHz mono (a stereo pair mixed), each zone's loop kept when it fits, and the lengths
 * shortened together (the longest first) until all of them fit the slot. FieldTape (Fm1Bridge.soundFontSlot) does
 * the same on Android.
 *
 *   parseSf2(bytes)                 -> { name, presets: [{ name, bank, program }] } (the file's order: bank, program)
 *   presetZones(sf, i, opt)         -> { name, zones: [{ fname, s (Int16Array), root, tune16, lo, hi, ls, le, looped }],
 *                                       total, cut } for the editor's draft (buildSlot), opt { rate, room, maxZones,
 *                                       resample(x, from, to) }
 * Pure: web/test_sf2.mjs runs it under node. */

const GEN = {
  startAddrsOffset: 0, endAddrsOffset: 1, startloopAddrsOffset: 2, endloopAddrsOffset: 3, startAddrsCoarseOffset: 4,
  endAddrsCoarseOffset: 12, startloopAddrsCoarseOffset: 45, endloopAddrsCoarseOffset: 50, instrument: 41, keyRange: 43,
  velRange: 44, coarseTune: 51, fineTune: 52, sampleID: 53, sampleModes: 54, overridingRootKey: 58,
};
const ADD = [GEN.coarseTune, GEN.fineTune];          /* (preset level: added to the instrument's; the rest is not read) */

function str(d, o, n) {
  let s = "";
  for (let i = 0; i < n && d[o + i]; i++) s += String.fromCharCode(d[o + i]);
  return s.replace(/[^\x20-\x7E]/g, "").trim();
}

/* the RIFF tree: the chunks of a list, { id -> [off, len] } (LIST chunks by their type) */
function chunks(d, v, off, end) {
  const out = {};
  while (off + 8 <= end) {
    const id = str(d, off, 4), n = v.getUint32(off + 4, true), body = off + 8;
    if (body + n > end) throw new Error("truncated file");
    out[id === "LIST" ? str(d, body, 4) : id] = id === "LIST" ? [body + 4, n - 4] : [body, n];
    off = body + n + (n & 1);
  }
  return out;
}

export function parseSf2(buf) {
  const d = buf instanceof Uint8Array ? buf : new Uint8Array(buf), v = new DataView(d.buffer, d.byteOffset, d.byteLength);
  if (d.length < 12 || str(d, 0, 4) !== "RIFF" || str(d, 8, 4) !== "sfbk") throw new Error("not a SoundFont (.sf2)");
  const top = chunks(d, v, 12, Math.min(d.length, 8 + v.getUint32(4, true)));
  if (!top.sdta || !top.pdta) throw new Error("SoundFont without sdta/pdta");
  const sd = chunks(d, v, top.sdta[0], top.sdta[0] + top.sdta[1]), pd = chunks(d, v, top.pdta[0], top.pdta[0] + top.pdta[1]);
  if (!sd.smpl) throw new Error("SoundFont without samples (smpl)");
  const info = top.INFO ? chunks(d, v, top.INFO[0], top.INFO[0] + top.INFO[1]) : {};
  const rec = (id, size) => {
    if (!pd[id]) throw new Error(`SoundFont without ${id}`);
    const [o, n] = pd[id], out = [];
    for (let i = 0; i + size <= n; i += size) out.push(o + i);
    return out;
  };
  const gens = (id) => rec(id, 4).map((o) => ({ op: v.getUint16(o, true), lo: d[o + 2], hi: d[o + 3], amt: v.getInt16(o + 2, true) }));
  const bags = (id) => rec(id, 4).map((o) => v.getUint16(o, true));
  const sf = {
    d, v,
    smpl: sd.smpl,
    name: info.INAM ? str(d, info.INAM[0], info.INAM[1]) : "",
    phdr: rec("phdr", 38).map((o) => ({ name: str(d, o, 20), program: v.getUint16(o + 20, true), bank: v.getUint16(o + 22, true), bag: v.getUint16(o + 24, true) })),
    pbag: bags("pbag"), pgen: gens("pgen"),
    inst: rec("inst", 22).map((o) => ({ name: str(d, o, 20), bag: v.getUint16(o + 20, true) })),
    ibag: bags("ibag"), igen: gens("igen"),
    shdr: rec("shdr", 46).map((o) => ({
      name: str(d, o, 20), start: v.getUint32(o + 20, true), end: v.getUint32(o + 24, true), ls: v.getUint32(o + 28, true),
      le: v.getUint32(o + 32, true), rate: v.getUint32(o + 36, true), pitch: d[o + 40], corr: v.getInt8(o + 41),
      link: v.getUint16(o + 42, true), type: v.getUint16(o + 44, true),
    })),
  };
  /* (the last record of each list is its terminal one) */
  sf.presets = sf.phdr.slice(0, -1).map((p, i) => ({ name: p.name || `PRESET ${i + 1}`, bank: p.bank, program: p.program, i }))
    .sort((a, b) => a.bank - b.bank || a.program - b.program || a.i - b.i);
  if (!sf.presets.length) throw new Error("SoundFont without presets");
  return sf;
}

/* the zones of a bag list: [{ g: Map(op -> gen) }], the global zone (the first, without its terminal generator)
   merged into the others */
function zonesOf(bag, gen, from, to, term) {
  const zs = [];
  for (let b = from; b < to; b++) {
    const g = new Map();
    for (let k = bag[b]; k < bag[b + 1] && k < gen.length; k++) g.set(gen[k].op, gen[k]);
    zs.push(g);
  }
  let glob = null;
  if (zs.length && !zs[0].has(term)) glob = zs.shift();
  return zs.filter((g) => g.has(term)).map((g) => ({ g, glob }));
}
const get = (z, op) => (z.g.has(op) ? z.g.get(op) : z.glob && z.glob.has(op) ? z.glob.get(op) : null);
const amt = (z, op, def = 0) => { const x = get(z, op); return x ? x.amt : def; };
const range = (z, op) => { const x = get(z, op); return x ? [x.lo, x.hi] : [0, 127]; };

/* preset i's regions: [{ lo, hi, vlo, vhi, sample, root, cents, looped, start, end, ls, le }] (sample frames) */
export function presetRegions(sf, i) {
  const p = sf.phdr[sf.presets[i].i], next = sf.phdr[sf.presets[i].i + 1], out = [];
  for (const pz of zonesOf(sf.pbag, sf.pgen, p.bag, next.bag, GEN.instrument)) {
    const ii = amt(pz, GEN.instrument), inst = sf.inst[ii], instNext = sf.inst[ii + 1];
    if (!inst || !instNext) continue;
    const [pkl, pkh] = range(pz, GEN.keyRange), [pvl, pvh] = range(pz, GEN.velRange);
    for (const iz of zonesOf(sf.ibag, sf.igen, inst.bag, instNext.bag, GEN.sampleID)) {
      const s = sf.shdr[amt(iz, GEN.sampleID)];
      if (!s || s.type & 0x8000 || s.end <= s.start) continue;   /* (ROM samples: not in the file) */
      const [kl, kh] = range(iz, GEN.keyRange), [vl, vh] = range(iz, GEN.velRange);
      const lo = Math.max(kl, pkl), hi = Math.min(kh, pkh), vlo = Math.max(vl, pvl), vhi = Math.min(vh, pvh);
      if (lo > hi || vlo > vhi) continue;
      const add = (op) => amt(iz, op) + (ADD.includes(op) ? amt(pz, op) : 0);
      const ofs = (f, c) => amt(iz, f) + 32768 * amt(iz, c);
      const rk = amt(iz, GEN.overridingRootKey, -1);
      out.push({
        lo, hi, vlo, vhi, sample: s,
        root: rk >= 0 && rk <= 127 ? rk : s.pitch <= 127 ? s.pitch : 60,
        cents: add(GEN.coarseTune) * 100 + add(GEN.fineTune) + s.corr,
        looped: (amt(iz, GEN.sampleModes) & 1) === 1,
        start: s.start + ofs(GEN.startAddrsOffset, GEN.startAddrsCoarseOffset),
        end: s.end + ofs(GEN.endAddrsOffset, GEN.endAddrsCoarseOffset),
        ls: s.ls + ofs(GEN.startloopAddrsOffset, GEN.startloopAddrsCoarseOffset),
        le: s.le + ofs(GEN.endloopAddrsOffset, GEN.endloopAddrsCoarseOffset),
      });
    }
  }
  return out;
}

/* frames [a, b) of sample s (its stereo partner mixed in) as floats */
function frames(sf, r, a, b) {
  const s = r.sample, base = sf.smpl[0], n = sf.smpl[1] >> 1, x = new Float64Array(Math.max(0, b - a));
  const pair = (s.type === 2 || s.type === 4) && sf.shdr[s.link] && !(sf.shdr[s.link].type & 0x8000) ? sf.shdr[s.link] : null;
  for (let k = 0; k < x.length; k++) {
    const i = a + k;
    let y = i >= 0 && i < n ? sf.v.getInt16(base + 2 * i, true) : 0;
    if (pair) {
      const j = pair.start + (i - s.start);
      y = (y + (j >= pair.start && j < pair.end && j < n ? sf.v.getInt16(base + 2 * j, true) : 0)) / 2;
    }
    x[k] = y / 32768;
  }
  return x;
}

function linear(x, from, to) {
  if (from === to) return Array.from(x);
  const step = from / to, out = [];
  for (let p = 0; p < x.length - 1; p += step) { const i = Math.floor(p), f = p - i; out.push(x[i] * (1 - f) + x[i + 1] * f); }
  return out;
}

/* the longest length L with sum(min(n_i, L)) <= room (Infinity: all fit) */
export function fitLength(lens, room) {
  const sum = (L) => lens.reduce((a, n) => a + Math.min(n, L), 0);
  if (sum(Infinity) <= room) return Infinity;
  let lo = 0, hi = Math.max(...lens);
  while (lo < hi) { const L = (lo + hi + 1) >> 1; if (sum(L) <= room) lo = L; else hi = L - 1; }
  return lo;
}

export function presetZones(sf, i, opt = {}) {
  const rate = opt.rate || 22050, maxZones = opt.maxZones || 16, room0 = opt.room || (65536 - 512) * 2, rs = opt.resample || linear;
  const regs = presetRegions(sf, i);
  if (!regs.length) throw new Error("this preset has no samples");
  /* one region a key range: the loudest layer (a left / mono sample before its right) */
  const by = new Map();
  for (const r of regs) {
    const k = `${r.lo}-${r.hi}`, o = by.get(k);
    if (!o || r.vhi > o.vhi || (r.vhi === o.vhi && o.sample.type === 2 && r.sample.type !== 2)) by.set(k, r);
  }
  let picked = [...by.values()].sort((a, b) => a.lo - b.lo || a.hi - b.hi);
  if (picked.length > maxZones) picked = Array.from({ length: maxZones }, (_, j) => picked[Math.floor(j * picked.length / maxZones)]);
  /* each zone at the slot's rate, its loop scaled */
  let zs = picked.map((r) => {
    const s = r.sample, sr = s.rate || 44100, f = rate / sr;
    const a = Math.max(0, r.start), end = Math.max(a + 1, r.end);
    const looped = r.looped && r.ls >= a && r.le > r.ls + 1 && r.le <= end;
    const x = rs(frames(sf, r, a, looped ? r.le : end), sr, rate);
    const n = Math.max(1, x.length);
    const ls = looped ? Math.min(n - 1, Math.round((r.ls - a) * f)) : 0;
    const le = looped ? Math.max(ls, Math.min(n - 1, Math.round((r.le - a) * f) - 1)) : n - 1;
    const c = -r.cents / 100, whole = Math.round(c);                 /* the tuning: the root moves, in 1/16 semitones */
    return { r, x: x.length ? x : [0], n, ls, le, looped: looped && le > ls, root: Math.max(0, Math.min(127, r.root + whole)),
      tune16: Math.max(-128, Math.min(127, Math.round((c - whole) * 16))) };
  });
  /* the keys: each zone its own range, the gaps and overlaps shared at the middle of the two roots */
  for (let j = 0; j < zs.length; j++) {
    const z = zs[j], nx = zs[j + 1];
    z.lo = j === 0 ? 0 : zs[j - 1].hi + 1;
    if (!nx) { z.hi = 127; break; }
    const tiled = z.r.hi + 1 === nx.r.lo;
    const mid = Math.floor((Math.max(z.r.lo, Math.min(z.r.hi, z.root)) + Math.max(nx.r.lo, Math.min(nx.r.hi, nx.root))) / 2);
    z.hi = Math.max(z.lo, Math.min(126, nx.r.hi - 1, tiled ? z.r.hi : mid));
  }
  zs = zs.filter((z) => z.lo <= 127);                 /* (crowded at the top: the last ones left out) */
  zs[zs.length - 1].hi = 127;
  /* the slot: the longest shortened first (a loop beyond the cut is dropped; the cut fades over 10 ms) */
  const L = fitLength(zs.map((z) => z.n), room0 - zs.length);
  let cut = 0, pk = 1e-9;
  for (const z of zs) {
    if (z.n > L) {
      cut++;
      z.x = z.x.slice(0, L);
      z.n = L;
      if (z.le >= L) z.looped = false;
      const fade = Math.min(L, Math.round(rate / 100));
      for (let k = 0; k < fade; k++) z.x[L - 1 - k] *= k / fade;
    }
    if (!z.looped) { z.ls = 0; z.le = z.n - 1; }
    for (const y of z.x) pk = Math.max(pk, Math.abs(y));
  }
  const g = 30000 / pk;                                /* one gain for all: the zones keep their levels */
  const name = sf.presets[i].name;
  return {
    name,
    cut,
    total: zs.reduce((a, z) => a + z.n, 0),
    zones: zs.map((z) => ({
      fname: `${name} ${z.r.sample.name}`.trim(),
      s: Int16Array.from(z.x, (y) => Math.max(-32768, Math.min(32767, Math.trunc(y * g)))),
      root: z.root, tune16: z.tune16, lo: z.lo, hi: z.hi, ls: z.ls, le: z.le, looped: z.looped,
    })),
  };
}
