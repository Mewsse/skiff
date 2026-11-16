// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// fm1backup.js against a simulated device: 7-bit packing, CRC, manifest checks, a full capture,
// validation before the first write, restore order (live music last), the tape store (40..59) and the 29-object archives of the earlier Skiff and abort on a failed chunk.
import { BACKUP_IDS, BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, readBackup, captureBackup, restoreBackup, BackupConnection } from "./fm1backup.js";

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(72)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const throws = (f) => { try { f(); return false; } catch { return true; } };
const athrows = async (f) => { try { await f(); return false; } catch { return true; } };

const rnd = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 131 + seed * 17 + (i >> 3)) & 255);
ok(bkR32(bkU32(0xdeadbeef)) === 0xdeadbeef && bkCrc(new TextEncoder().encode("123456789")) === 0xcbf43926, "backup: numbers and CRC-32");
const b = rnd(1000, 1);
ok(bkUnpack(bkPack(b), b.length).every((v, i) => v === b[i]), "backup: 7-bit pack / unpack round trip");
ok(throws(() => bkUnpack([...bkPack(b.subarray(0, 14)), 0], 14)), "backup: trailing bytes refused");

/* a device: objects by id, sample slots, the order of writes */
function device(objs, opt = {}) {
  const d = { objs: new Map(objs), log: [], staged: null };
  d.request = async ([cmd, a]) => {
    if (cmd === BACKUP_CMD.LIST) {
      const out = [1, 0, BACKUP_IDS.length];
      for (const id of BACKUP_IDS) { const v = d.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === BACKUP_CMD.GET) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = d.objs.get(id);
      if (opt.changeOnGet === id && off === 0) v[0] ^= 1;
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === BACKUP_CMD.PUT) {
      const [op, id] = a;
      if (op === 0 && id > (opt.maxId ?? 59)) { d.log.push(`refused ${id}`); return [op, id, 1]; }   // (an older firmware)
      if (op === 0) { d.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, 0]; }
      if (op === 1) {
        if (opt.failChunk === id) return [op, id, 2];
        const off = bkR32(a, 2), p = a.slice(7), n = Math.min(256, d.staged.size - off);
        d.staged.bytes.push(...bkUnpack(p, n)); return [op, id, 0];
      }
      if (op === 2) { const s = Uint8Array.from(d.staged.bytes); const rc = bkCrc(s) === d.staged.crc ? 0 : 2; if (!rc) { d.objs.set(id, s); d.log.push(id); } return [op, id, rc]; }
      if (op === 3) { d.log.push(`abort ${id}`); return [op, id, 0]; }
    }
    if (cmd === 11 || cmd === 12 || cmd === 13 || cmd === 14) { if (cmd === 13 || cmd === 14) d.log.push(32 + a[0]); return [a[0], 0]; }
    throw new Error(`unexpected ${cmd}`);
  };
  return d;
}
const objs = [[0, rnd(3388, 2)], [1, rnd(1200, 3)], [2, rnd(3388, 4)], [6, rnd(3080, 5)]];
const BANK_RANGE = Array.from({ length: 16 }, (_, i) => 16 + i);
const noTape = (id) => id < 40 || id > 59;
const entry = (id, bytes) => ({ id, size: bytes.length, crc: bytes.length ? bkCrc(bytes) : 0, data: Buffer.from(bytes).toString("base64") });
const dev = device(objs);
const file = await captureBackup(dev.request, "TEST");
ok(BACKUP_IDS.length === 33 && file.objects.length === BACKUP_IDS.length && file.objects[0].size === 3388 && file.objects[3].size === 0, "backup: capture lists every object, empty ones as 0");
ok(readBackup(JSON.stringify(file)).objects[2].bytes.every((v, i) => v === objs[2][1][i]), "backup: capture -> file -> bytes round trip");
ok(await athrows(() => captureBackup(device(objs.map(([i, v]) => [i, v.slice()]), { changeOnGet: 2 }).request, "TEST")), "backup: a device that changes during capture fails the capture");

const bad = JSON.parse(JSON.stringify(file)); bad.objects[2].crc ^= 1;
ok(throws(() => readBackup(bad)), "backup: a damaged file is refused");
const noRun = JSON.parse(JSON.stringify(file)); noRun.objects[0] = { ...noRun.objects[0], size: 0, crc: 0, data: "" };
ok(throws(() => readBackup(noRun)), "backup: a file without the current music is refused");
ok(throws(() => bkManifest([1, 0, 3])), "backup: a short manifest is refused");

const target = device([]);
const damaged = JSON.parse(JSON.stringify(file)); damaged.objects[6].crc ^= 1;
ok(await athrows(() => restoreBackup(target.request, damaged)) && target.log.length === 0, "backup: restore validates every byte before the first write");
await restoreBackup(target.request, file);
const order = target.log.filter((x) => typeof x === "number");
ok(order.at(-1) === 0 && order.at(-2) === 1 && order.indexOf(2) < order.indexOf(1), "backup: restore order: projects, banks, samples, settings, live music last");
ok([0, 1, 2, 6].every((id) => target.objs.get(id).every((v, i) => v === objs.find((o) => o[0] === id)[1][i])), "backup: restored objects equal the source");
{   /* the FM6 patch bank (id 8) and the archives of firmware before it (11 objects, no id 8) */
  ok(BACKUP_IDS.length === 33 && file.objects.some((o) => o.id === 8) && file.objects.some((o) => o.id === 9) &&
    [40, 59].every((id) => file.objects.some((o) => o.id === id)) && !file.objects.some((o) => o.id >= 16 && o.id <= 31),
    "backup: 33 objects: id 8 (the retired FM6 bank, empty), id 9 (the user presets' FM6 patches), 40..59 (the tape store)");
  const noBank = (f) => f.objects.filter((o) => o.id < 16 || (o.id >= 32 && o.id <= 34));
  const v3 = JSON.parse(JSON.stringify(file)); v3.objects = noBank(v3);
  ok(readBackup(v3).objects.length === 13, "backup: an archive of Felucca 1.0.3.. (13 objects, no pattern pool or tape) still reads");
  const v2 = JSON.parse(JSON.stringify(file)); v2.objects = noBank(v2).filter((o) => o.id !== 9);
  ok(readBackup(v2).objects.length === 12, "backup: an archive of 1.0..1.0.2 (12 objects, the bank as id 8) still reads");
  const old = JSON.parse(JSON.stringify(file)); old.objects = noBank(old).filter((o) => o.id !== 8 && o.id !== 9);
  ok(readBackup(old).objects.length === 11, "backup: an archive of the 11 objects before FM6 still reads");
  const odd = JSON.parse(JSON.stringify(file)); odd.objects = odd.objects.filter((o) => o.id !== 33);
  ok(throws(() => readBackup(odd)), "backup: an archive missing another object is refused");
}
{   /* 1.0.3: an old archive with a bank restores its id 8 (after 6, 7); a new archive onto 1.0.2 skips id 9 */
  const bankBytes = rnd(3472, 8), patches = rnd(3728, 9);
  const v2dev = device([...objs, [8, bankBytes]]);
  const v2file = await captureBackup(async ([cmd, a]) => {   // (a 1.0.2 device: 12 objects)
    const r = await v2dev.request([cmd, a]);
    if (cmd !== BACKUP_CMD.LIST) return r;
    const n = r[2], keep = [];
    for (let i = 0; i < n; i++) { const id = r[3 + i * 11]; if (id !== 9 && noTape(id)) keep.push(...r.slice(3 + i * 11, 14 + i * 11)); }
    return [1, 0, keep.length / 11, ...keep];
  }, "1.0.2");
  const to103 = device([]);
  await restoreBackup(to103.request, v2file);
  const lg = to103.log.filter((x) => typeof x === "number");
  ok(v2file.objects.length === 12 && lg.indexOf(8) > lg.indexOf(7) && lg.indexOf(7) > lg.indexOf(6) &&
     to103.objs.get(8).every((v, i) => v === bankBytes[i]), "backup: a 1.0.2 archive (id 8, the bank) restores, its bank after the user presets");
  const newFile = await captureBackup(device([...objs, [9, patches]]).request, "1.0.3");
  const to102 = device([], { maxId: 8 });
  await restoreBackup(to102.request, newFile);
  ok(to102.log.includes("refused 9") && !to102.objs.has(9) && to102.log.at(-1) === 0,
    "backup: a 1.0.3 archive onto older firmware: id 9 skipped, the rest restored");
  const to103b = device([]);
  await restoreBackup(to103b.request, newFile);
  ok(to103b.objs.get(9).every((v, i) => v === patches[i]), "backup: a 1.0.3 archive restores id 9");
}
{   /* (Skiff) the tape store: its chunks captured and restored; onto firmware without it skipped */
  const head = rnd(3856, 10), body = rnd(3840, 11), tapeDev = device([...objs, [40, head], [41, body], [57, body]]);
  const tapeFile = await captureBackup(tapeDev.request, "skiff");
  const to = device([]);
  await restoreBackup(to.request, tapeFile);
  ok(tapeFile.objects.find((o) => o.id === 40).size === 3856 && tapeFile.objects.find((o) => o.id === 41).size === 3840 &&
     to.objs.get(40).every((v, i) => v === head[i]) && to.objs.get(40).length === 3856 && to.objs.get(41).every((v, i) => v === body[i]) &&
     to.objs.get(57).length === 3840 && to.log.at(-1) === 0 && to.log.at(-2) === 1,
     "backup: tape chunks (3856 and 3840 bytes) captured and restored byte for byte, live music still last");
  const tooBig = JSON.parse(JSON.stringify(tapeFile));
  tooBig.objects[tooBig.objects.findIndex((o) => o.id === 41)] = entry(41, rnd(3856, 12));
  ok(throws(() => readBackup(tooBig)), "backup: a tape chunk other than chunk 0 over 3840 bytes is refused");
  const older = device([], { maxId: 34 });
  await restoreBackup(older.request, tapeFile);
  ok(older.log.includes("refused 40") && older.log.includes("refused 59") && !older.objs.has(40) && !older.objs.has(41) &&
     older.objs.has(2) && older.log.at(-1) === 0, "backup: a new archive onto older firmware: 40..59 skipped, the rest restored");
  /* the 29 objects of the earlier Skiff (16..31: the pattern pool) */
  const chunk = rnd(3552, 13), skiff2 = JSON.parse(JSON.stringify(file));
  skiff2.objects = [...file.objects.filter((o) => o.id < 16), ...BANK_RANGE.map((id) => id === 18 ? entry(18, chunk) : entry(id, new Uint8Array(0))),
    ...file.objects.filter((o) => o.id >= 32 && o.id <= 34)];
  ok(skiff2.objects.length === 29 && readBackup(skiff2).objects.find((o) => o.id === 18).size === 3552, "backup: a 29-object archive of the earlier Skiff still reads");
  const toNew = device([]);
  await restoreBackup(toNew.request, skiff2);
  ok(toNew.log.includes(18) && toNew.objs.get(18).every((v, i) => v === chunk[i]) && toNew.objs.get(2) && toNew.log.at(-1) === 0,
    "backup: a 29-object archive restores (its 16..31 accepted), live music last");
  const toFelucca = device([], { maxId: 9 });
  await restoreBackup(toFelucca.request, skiff2);
  ok(toFelucca.log.includes("refused 18") && !toFelucca.objs.has(18) && toFelucca.objs.has(2) && toFelucca.log.at(-1) === 0,
    "backup: a 29-object archive onto firmware without the pool: 16..31 skipped, the rest restored");
}
{ // (Skiff 2.0) only: the installer's write-back of converted objects; the rest as the unit has them, in the same order
  const unit = device([[0, rnd(3388, 7)], [1, rnd(1200, 8)]]);
  await restoreBackup(unit.request, file, () => {}, [6, 2, 0]);
  ok(unit.log.join() === "2,6,0" && unit.objs.get(1).every((v, i) => v === rnd(1200, 8)[i]) &&
     unit.objs.get(0).every((v, i) => v === objs[0][1][i]), "backup: only some ids: those written, in the restore's order");
}
const failing = device([], { failChunk: 2 });
ok(await athrows(() => restoreBackup(failing.request, file)) && failing.log.includes("abort 2") && !failing.log.includes(0), "backup: a refused chunk aborts that object and stops before the live music");
{ // (#23) nobody answers: code "timeout", which the installer reads as "not running Skiff" (every Skiff answers INFO)
  const link = new BackupConnection({ state: "connected" }, { state: "connected", send() {} });
  const e = await link.request([1, []], { timeout: 5 }).catch((x) => x);
  link.close();
  ok(e?.code === "timeout", "backup: an unanswered request times out with code timeout");
}

console.log(fails ? `BACKUP WEB TESTS FAILED (${fails})` : "backup web tests passed");
process.exit(fails ? 1 : 0);
