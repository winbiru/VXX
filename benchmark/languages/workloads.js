'use strict';
const name = process.env.BENCH_CASE || 'arithmetic';
const iterations = Number(process.env.BENCH_ITERATIONS || 16);
const seed = Number(process.env.BENCH_SEED || 17);
if (iterations < 16 || iterations % 16 || iterations > 100000000) throw Error('iterations');
const data = Array(16).fill('');
for (let i = 0; i < 16; i++) {
  const k = seed + i;
  if (name === 'unicode_replace') data[k % 16] = 'Xin chào Việt Nam 🌿 — Việt! '.repeat(k % 4 + 4) + k;
  if (name === 'json_parse') data[k % 16] = '{"n":' + k + ',"tên":"Việt Nam 🌿","values":[' +
    Array.from({length: 16}, (_, j) => k + j).join(',') + ']}';
}
const functions = {
  arithmetic(k) { return (k + 17) * 31 + (k * 7 + 3) - (k + 5) * 11; },
  branch_loop(k) { let x = k * 97 + 11; for (let i = 0; i < 256; i++) { x += 251; if (x >= 65521) x -= 65521; } return x; },
  fibonacci(k) { let a = 0, b = 1; for (let i = 0; i < k % 16 + 20; i++) { const n = a + b; a = b; b = n; } return a; },
  list_128(k) { const a = []; for (let i = 0; i < 128; i++) a.push(k + i); let sum = 0;
    for (let i = 0; i < 128; i++) if (a[i] % 2 === 0) sum += a[i]; return sum; },
  unicode_replace(k) { return [...data[k % 16].replaceAll('Việt', 'V++')].length; },
  json_parse(k) { const v = JSON.parse(data[k % 16]); let sum = v.n + [...v['tên']].length;
    for (let i = 0; i < 16; i++) sum += v.values[i]; return sum; }
};
const fn = functions[name];
if (!fn) throw Error('unsupported case');
const checks = Array.from({length: 16}, (_, i) => fn(seed + i));
const expected = checks.reduce((s, v) => s + v % 997, 0) * (iterations / 16);
function batch() {
  let checksum = 0;
  const start = process.hrtime.bigint();
  for (let i = 0; i < iterations; i++) checksum += fn(seed + i % 16) % 997;
  const elapsed_ms = Number(process.hrtime.bigint() - start) / 1e6;
  if (checksum !== expected || elapsed_ms <= 0) throw Error('checksum/clock');
  return {checksum, elapsed_ms};
}
const warmup_ms = Array.from({length: 3}, () => batch().elapsed_ms);
const measured = batch();
console.log(JSON.stringify({case: name, iterations, seed, checks,
  checksum: String(measured.checksum), elapsed_ms: measured.elapsed_ms,
  ns_per_op: measured.elapsed_ms * 1e6 / iterations, warmup_ms}));
