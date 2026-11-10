'use strict';
const fs = require('node:fs');
const name = process.env.BENCH_CASE;
const path = process.env.BENCH_FILE;
const iterations = Number(process.env.BENCH_ITERATIONS);
const expected = Number(process.env.BENCH_EXPECT_UNIT);
const n = Number(process.env.BENCH_FEATURE_N || 32);
const fileBytes = Number(process.env.BENCH_FILE_BYTES);
const output = process.env.BENCH_OUTPUT;
const writePayload = name === 'write_bytes' ? fs.readFileSync(path) : null;
if (!Number.isInteger(iterations) || iterations < 1 || iterations > 1000000) throw Error('iterations');

function run() {
  if (name === 'filter_sum') {
    const values = []; for (let i = 0; i < n; i++) values.push(i);
    let total = 0; for (const v of values) if (v % 3 === 0) total += v;
    return total;
  }
  if (name === 'seek_sample') {
    const fd = fs.openSync(path, 'r');
    const byte = Buffer.allocUnsafe(1);
    let total = 0;
    try {
      for (let i = 0; i < 32; ++i) {
        const position = (i * 1009 + 17) % fileBytes;
        if (fs.readSync(fd, byte, 0, 1, position) !== 1) throw Error('seek read failed');
        total += byte[0];
      }
    } finally { fs.closeSync(fd); }
    return total;
  }
  if (name === 'write_bytes') {
    fs.writeFileSync(output, writePayload);
    return writePayload.length;
  }
  if (name === 'sort_numbers') {
    const values = []; for (let i = n - 1; i >= 0; --i) values.push(i);
    values.sort((a, b) => a - b);
    return values.reduce((sum, v, i) => sum + (i + 1) * v, 0);
  }
  if (name === 'linear_search') {
    const values = []; for (let i = 0; i < n; ++i) values.push(i);
    return values.indexOf(n - 1);
  }
  if (name === 'set_dedup') {
    const values = []; for (let i = 0; i < n; ++i) values.push(i % 16);
    const distinct = new Set(values);
    let sum = distinct.size; for (const value of distinct) sum += value;
    return sum;
  }
  if (name === 'list_append') {
    const a = []; for (let i = 0; i < n; i++) a.push(i);
    return a.reduce((s, v) => s + v, 0);
  }
  if (name === 'map_lookup') {
    const m = new Map(); for (let i = 0; i < n; i++) m.set('k' + i, 2 * i);
    let s = 0; for (let i = 0; i < n; i++) s += m.get('k' + i);
    return s;
  }
  if (name === 'function_calls') {
    const f = (x) => x * 3 + 7;
    let s = 0; for (let i = 0; i < n; i++) s += f(i);
    return s;
  }
  if (name === 'string_concat') {
    let s = ''; for (let i = 0; i < n; i++) s += 'V++';
    return [...s].length;
  }
  if (name === 'alloc_objects') {
    const a = []; for (let i = 0; i < n; i++) a.push({id: i, n: i * 2});
    return a.reduce((sum, obj) => sum + obj.n, 0);
  }
  if (name === 'exceptions') {
    let c = 0;
    for (let i = 0; i < n; i++) { try { if (i % 16 === 0) throw Error('bench'); } catch (e) { c++; } }
    return c;
  }
  if (name === 'recursion') {
    const fib = (x) => x < 2 ? x : fib(x - 1) + fib(x - 2);
    let s = 0; for (let i = 0; i < n; i++) s += fib(12);
    return s;
  }
  if (name === 'read_bytes') return fs.readFileSync(path).length;
  if (name === 'bytes_sum') {
    let sum = 0;
    for (const byte of fs.readFileSync(path)) sum += byte;
    return sum % 65521;
  }
  if (name === 'read_text') return Array.from(fs.readFileSync(path, 'utf8')).length;
  if (name === 'stream_64k') {
    const fd = fs.openSync(path, 'r');
    const buf = Buffer.allocUnsafe(65536);
    let total = 0;
    try {
      let n;
      while ((n = fs.readSync(fd, buf, 0, buf.length, null)) > 0) total += n;
    } finally { fs.closeSync(fd); }
    return total;
  }
  if (name === 'csv_split') {
    let total = 0;
    for (const line of fs.readFileSync(path, 'utf8').split('\n')) {
      if (line) {
        const columns = line.split(',');
        if (columns.length !== 3) throw Error('csv columns');
        total += Number(columns[0]) + Number(columns[2]);
      }
    }
    return total;
  }
  if (name === 'jsonl_parse') {
    let total = 0;
    for (const line of fs.readFileSync(path, 'utf8').split('\n')) {
      if (line) {
        const row = JSON.parse(line);
        total += row.id + row.n;
      }
    }
    return total;
  }
  throw Error('unsupported case');
}
if (run() !== expected) throw Error('preflight mismatch');
const start = process.hrtime.bigint();
let checksum = 0;
for (let i = 0; i < iterations; ++i) {
  const unit = run();
  if (unit !== expected) throw Error('unit mismatch');
  checksum += unit;
}
const elapsed_ms = Number(process.hrtime.bigint() - start) / 1e6;
console.log(JSON.stringify({case: name, iterations, unit: expected, checksum, elapsed_ms}));
