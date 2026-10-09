// SPDX-License-Identifier: GPL-3.0-only
// The SAMPLES page's SoundFont reader (sf2.js) on SoundFonts written here, then through the editor's buildSlot into
// a slot as the firmware checks it (eng_sample.c smp_zone_ok) and plays it (the ADPCM state stored at the loop start):
//   - presets listed by bank and program; a preset's instrument zones, the global zones merged, key and velocity
//     ranges intersected with the preset's;
//   - one layer a key range (the loudest), a stereo pair mixed to mono, the keys shared without gaps, root and fine
//     tuning (coarse, fine, the sample's correction), loops kept and scaled to 22.05 kHz;
//   - too long: shortened together to fit the slot, a loop past the cut dropped; more than 16 ranges: 16 kept.
//   node web/test_sf2.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { parseSf2, presetRegions, presetZones } from "./sf2.js";

let bad = 0;
const check = (ok, what) => { console.log(`sf2: ${what.padEnd(84)} ${ok ? "ok" : "FAIL"}`); if (!ok) bad++; };
const HERE = fileURLToPath(new URL(".", import.meta.url));
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const E = vm.runInNewContext(html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/")) +
  ";({ buildSlot, resample, SMP })", { console, TextEncoder, TextDecoder });

/* ---- a SoundFont writer: samples [{ name, x (Int16Array), rate, pitch, corr, ls, le, type, link }], instruments
   [{ name, zones: [[ [op, amt | [lo, hi]] ... ]] }] (the first zone may be global), presets [{ name, bank, program,
   zones }] (as instruments, op 41 for the instrument) */
function writeSf2({ samples, insts, presets }) {
  const parts = [], u8 = (n) => new Uint8Array(n);
  const chunk = (id, body) => { const b = u8(8 + body.length + (body.length & 1)), v = new DataView(b.buffer); for (let i = 0; i < 4; i++) b[i] = id.charCodeAt(i); v.setUint32(4, body.length, true); b.set(body, 8); return b; };
  const list = (type, kids) => { const n = kids.reduce((a, k) => a + k.length, 4), b = u8(n); for (let i = 0; i < 4; i++) b[i] = type.charCodeAt(i); let o = 4; for (const k of kids) { b.set(k, o); o += k.length; } return chunk("LIST", b); };
  const name20 = (b, o, s) => { for (let i = 0; i < Math.min(19, s.length); i++) b[o + i] = s.charCodeAt(i); };
  /* smpl: each sample then 46 zeros */
  let total = 0;
  const at = samples.map((s) => { const a = total; total += s.x.length + 46; return a; });
  const smpl = u8(total * 2), sv = new DataView(smpl.buffer);
  samples.forEach((s, k) => s.x.forEach((y, i) => sv.setInt16((at[k] + i) * 2, y, true)));
  const shdr = u8(46 * (samples.length + 1)), hv = new DataView(shdr.buffer);
  samples.forEach((s, k) => {
    const o = 46 * k;
    name20(shdr, o, s.name);
    [at[k], at[k] + s.x.length, at[k] + (s.ls ?? 0), at[k] + (s.le ?? 0), s.rate].forEach((x, j) => hv.setUint32(o + 20 + 4 * j, x, true));
    shdr[o + 40] = s.pitch ?? 60; hv.setInt8(o + 41, s.corr ?? 0); hv.setUint16(o + 42, s.link ?? 0, true); hv.setUint16(o + 44, s.type ?? 1, true);
  });
  name20(shdr, 46 * samples.length, "EOS");
  const bagsGens = (things) => {
    const bag = [], gen = [];
    for (const t of things) for (const z of t.zones) { bag.push([gen.length, 0]); for (const g of z) gen.push(g); }
    bag.push([gen.length, 0]);
    const B = u8(4 * bag.length), BV = new DataView(B.buffer), G = u8(4 * (gen.length + 1)), GV = new DataView(G.buffer);
    bag.forEach(([g], i) => BV.setUint16(4 * i, g, true));
    gen.forEach(([op, a], i) => { GV.setUint16(4 * i, op, true); if (Array.isArray(a)) { G[4 * i + 2] = a[0]; G[4 * i + 3] = a[1]; } else GV.setInt16(4 * i + 2, a, true); });
    return { B, G, firsts: things.reduce((acc, t) => { acc.push(acc.length ? acc[acc.length - 1] + things[acc.length - 1].zones.length : 0); return acc; }, []), n: bag.length - 1 };
  };
  const pi = bagsGens(presets), ii = bagsGens(insts);
  const phdr = u8(38 * (presets.length + 1)), pv = new DataView(phdr.buffer);
  presets.forEach((p, k) => { name20(phdr, 38 * k, p.name); pv.setUint16(38 * k + 20, p.program, true); pv.setUint16(38 * k + 22, p.bank, true); pv.setUint16(38 * k + 24, pi.firsts[k], true); });
  name20(phdr, 38 * presets.length, "EOP"); pv.setUint16(38 * presets.length + 24, pi.n, true);
  const inst = u8(22 * (insts.length + 1)), iv = new DataView(inst.buffer);
  insts.forEach((t, k) => { name20(inst, 22 * k, t.name); iv.setUint16(22 * k + 20, ii.firsts[k], true); });
  name20(inst, 22 * insts.length, "EOI"); iv.setUint16(22 * insts.length + 20, ii.n, true);
  const info = list("INFO", [chunk("ifil", u8(4)), chunk("INAM", new TextEncoder().encode("Test Font\0"))]);
  const body = [new TextEncoder().encode("sfbk"), info, list("sdta", [chunk("smpl", smpl)]),
    list("pdta", [chunk("phdr", phdr), chunk("pbag", pi.B), chunk("pmod", u8(10)), chunk("pgen", pi.G), chunk("inst", inst),
      chunk("ibag", ii.B), chunk("imod", u8(10)), chunk("igen", ii.G), chunk("shdr", shdr)])];
  const n = body.reduce((a, k) => a + k.length, 0), b = u8(8 + n), v = new DataView(b.buffer);
  b.set(new TextEncoder().encode("RIFF")); v.setUint32(4, n, true);
  let o = 8;
  for (const k of body) { b.set(k, o); o += k.length; }
  return b;
}

const sine = (f, sr, n, a = 12000) => Int16Array.from({ length: n }, (_, i) => Math.round(a * Math.sin(2 * Math.PI * f * i / sr)));
const K = 43, V = 44, INST = 41, SID = 53, MODES = 54, ROOT = 58, COARSE = 51, FINE = 52;
/* samples: 0 a looped low tone (44.1 kHz), 1/2 a soft and a loud layer (32 kHz), 3/4 a stereo pair (L 1 kHz, R silent) */
const samples = [
  { name: "low", x: sine(110, 44100, 22050), rate: 44100, pitch: 40, ls: 4410, le: 4410 + 401 * 10 },
  { name: "soft", x: sine(262, 32000, 16000, 3000), rate: 32000, pitch: 60, corr: -20 },
  { name: "loud", x: sine(262, 32000, 16000), rate: 32000, pitch: 60, corr: -20 },
  { name: "padL", x: sine(1000, 44100, 8820), rate: 44100, pitch: 84, type: 4, link: 4 },
  { name: "padR", x: new Int16Array(8820), rate: 44100, pitch: 84, type: 2, link: 3 },
];
const insts = [
  { name: "keys", zones: [
    [[MODES, 1]],                                                     /* global: loop */
    [[K, [0, 47]], [SID, 0]],
    [[K, [48, 71]], [V, [0, 63]], [MODES, 0], [FINE, 50], [SID, 1]],
    [[K, [48, 71]], [V, [64, 127]], [MODES, 0], [FINE, 50], [SID, 2]],
    [[K, [76, 127]], [MODES, 0], [ROOT, 86], [SID, 3]],
    [[K, [76, 127]], [MODES, 0], [ROOT, 86], [SID, 4]],
  ] },
];
const presets = [
  { name: "Second", bank: 0, program: 5, zones: [[[COARSE, -12], [INST, 0]]] },
  { name: "Keys", bank: 0, program: 1, zones: [[[K, [0, 100]], [INST, 0]]] },
];
const font = writeSf2({ samples, insts, presets });
if (process.env.SF2_OUT) writeFileSync(process.env.SF2_OUT, font);   /* (a file for a browser check of the editor) */
const sf = parseSf2(font);
check(sf.name === "Test Font" && sf.presets.map((p) => p.name).join() === "Keys,Second", "the file's name; its presets by bank and program");
const regs = presetRegions(sf, 0);
check(regs.length === 5 && regs[3].hi === 100 && regs[0].looped && !regs[1].looped, "regions: the global zone merged, the keys cut by the preset's (76..100)");
const r = presetZones(sf, 0, { resample: E.resample, room: E.SMP.MAX_DATA * 2 });
const z = r.zones;
check(r.name === "Keys" && z.length === 3, "3 zones: one a key range (the velocity layers and the stereo pair merged)");
check(z[0].lo === 0 && z.every((q, j) => j === 0 || q.lo === z[j - 1].hi + 1) && z[2].hi === 127, "the keys: 0..127, no gap, no overlap");
check(z[0].hi === 47 && z[1].lo === 48 && z[1].hi === 73, "... their own ranges, the gap 72..75 shared at the middle of the roots (60, 86)");
const pk = (s) => s.reduce((a, y) => Math.max(a, Math.abs(y)), 0);
check(pk(z[1].s) > 20000, "the loud layer of 48..71 (the soft one left out)");
check(z[1].root === 60 && z[1].tune16 === -5, "tuning: fine +50 and correction -20 cents (+0.3) -> root C4 less 5/16");
check(z[2].root === 86 && pk(z[2].s) > 10000 && pk(z[2].s) < 20000, "the overriding root key; the stereo pair mixed (L + silent R: half)");
check(z[0].looped && Math.abs(z[0].ls - 2205) <= 1 && Math.abs(z[0].le + 1 - (2205 + 2005)) <= 1 && z[0].s.length === z[0].le + 1,
  "the loop: scaled to 22.05 kHz (ls 2205, 10 periods), the sample cut after it");
check(!z[1].looped && z[1].le === z[1].s.length - 1 && Math.abs(z[1].s.length - 11025) <= 2, "no loop: 32 kHz -> 22.05 kHz, its whole length");
const second = presetZones(sf, 1, { resample: E.resample });
check(second.zones[0].root === 52 && second.zones[1].root === 72, "a preset's coarse tune (-12) moves the roots up an octave");

/* through buildSlot: the slot the firmware reads */
const slot = E.buildSlot(r.name, z), hv = new DataView(slot.hdr.buffer);
const zone = (j) => { const b = 32 + 28 * j; return { off: hv.getUint32(b, true), n: hv.getUint32(b + 4, true), ls: hv.getUint32(b + 8, true), le: hv.getUint32(b + 12, true),
  rate: hv.getUint32(b + 16, true), root16: hv.getInt16(b + 20, true), pred: hv.getInt16(b + 22, true), idx: slot.hdr[b + 24], lo: slot.hdr[b + 25], hi: slot.hdr[b + 26], looped: slot.hdr[b + 27] }; };
const zok = (q) => q.off <= slot.data.length && q.off + ((q.n + 1) >> 1) <= slot.data.length && q.idx <= 88 && q.rate && q.ls <= q.le && q.le < q.n && q.lo <= q.hi;
check(slot.hdr[6] === 3 && [0, 1, 2].every((j) => zok(zone(j))), "buildSlot: 3 zones the firmware takes (smp_zone_ok)");
check(zone(0).looped === 1 && zone(1).looped === 0 && zone(1).root16 === 60 * 16 - 5 && zone(0).ls === z[0].ls && zone(0).le === z[0].le,
  "... the loop flag and points, root16 with the fine tuning");
{ /* the ADPCM state at the loop start: decoding up to it gives the stored predictor and index */
  const q = zone(0), IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767];
  const IDX = [-1, -1, -1, -1, 2, 4, 6, 8];
  let p = 0, ix = 0;
  for (let i = 0; i < q.ls; i++) {
    const c = (slot.data[q.off + (i >> 1)] >> ((i & 1) * 4)) & 15, st = IMA_STEP[ix];
    let vd = st >> 3; if (c & 4) vd += st; if (c & 2) vd += st >> 1; if (c & 1) vd += st >> 2;
    p = Math.max(-32768, Math.min(32767, c & 8 ? p - vd : p + vd)); ix = Math.max(0, Math.min(88, ix + IDX[c & 7]));
  }
  check(p === q.pred && ix === q.idx, "... the ADPCM state stored at the loop start is the decoder's there");
}

/* too long: 4 zones of 3 s each (4 x 66150 > 130048) */
{
  const long = [0, 1, 2, 3].map((k) => ({ name: `L${k}`, x: sine(220 * (k + 1), 22050, 66150), rate: 22050, pitch: 48 + 12 * k, ls: 1000, le: 60000 }));
  const big = parseSf2(writeSf2({ samples: long, insts: [{ name: "long", zones: [[[MODES, 1]], ...long.map((_, k) => [[K, [k * 32, k * 32 + 31]], [SID, k]])] }],
    presets: [{ name: "Long", bank: 0, program: 0, zones: [[[INST, 0]]] }] }));
  const b = presetZones(big, 0, { room: E.SMP.MAX_DATA * 2 });
  const bytes = b.zones.reduce((a, q) => a + ((q.s.length + 1) >> 1), 0);
  check(b.cut === 4 && bytes <= E.SMP.MAX_DATA && bytes > E.SMP.MAX_DATA - 8, "too long: all 4 shortened alike, the slot filled");
  check(b.zones.every((q) => !q.looped && q.le === q.s.length - 1) && b.zones.every((q) => Math.abs(q.s[q.s.length - 1]) < 50),
    "... their loops (past the cut) dropped, the cut faded");
  let built = null;
  try { built = E.buildSlot(b.name, b.zones); } catch (e) { /* too long */ }
  check(!!built && built.data.length <= E.SMP.MAX_DATA, "... and buildSlot takes them");
}
/* more than 16 ranges: 16 kept, spread over the keyboard */
{
  const many = Array.from({ length: 40 }, (_, k) => ({ name: `M${k}`, x: sine(100 + k, 22050, 2000), rate: 22050, pitch: 20 + 2 * k }));
  const m = parseSf2(writeSf2({ samples: many, insts: [{ name: "many", zones: many.map((_, k) => [[K, [20 + 2 * k, 21 + 2 * k]], [SID, k]]) }],
    presets: [{ name: "Many", bank: 0, program: 0, zones: [[[INST, 0]]] }] }));
  const q = presetZones(m, 0).zones;
  check(q.length === 16 && q[0].lo === 0 && q[15].hi === 127 && q.every((x, j) => j === 0 || (x.lo === q[j - 1].hi + 1 && x.root > q[j - 1].root)),
    "40 key ranges: 16 kept, spread, the keyboard covered");
}
{
  let err = "";
  try { parseSf2(new TextEncoder().encode("RIFF\x04\0\0\0WAVE")); } catch (e) { err = e.message; }
  check(/SoundFont/.test(err), "not a SoundFont: an error that says so");
}
console.log(bad ? `sf2: ${bad} FAILED` : "sf2: all checks ok");
process.exit(bad ? 1 : 0);
