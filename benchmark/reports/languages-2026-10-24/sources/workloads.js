'use strict';
const texts = Array.from({length: 16}, (_, i) => 'Xin chào Việt Nam! '.repeat(i + 1));
const documents = Array.from({length: 16}, (_, i) => '{"name":"V++ Việt","quantity":' + i
  + ',"active":true,"values":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}');
function arithmetic(i) { return (1000 + i * 7 + 20 + i) - (i % 7) * (i % 11); }
function branch(i) {
  let x = i * 4093 + 7;
  for (let j = 0; j < 128 + i; j++) {
    x += 251;
    if (x >= 65521) x -= 65521;
  }
  return x;
}
function fibonacci(i) {
  let a = 0, b = 1;
  for (let j = 0; j < 20 + i; j++) {
    const next = a + b; a = b; b = next;
  }
  return a;
}
function list(i) {
  const values = [];
  for (let j = 0; j < 128; j++) values.push((j * 17 + i) % 1000);
  let result = 0;
  for (let j = 0; j < 128; j++) {
    const value = values[j];
    if (value >= 500) result += value;
  }
  return result;
}
function text(i) { return texts[i].replaceAll('Việt', 'V++').length; }
function json(i) {
  const parsed = JSON.parse(documents[i]);
  let result = parsed.quantity + parsed.name.length;
  for (let j = 0; j < 16; j++) result += parsed.values[j];
  return result;
}
const name = process.env.VPP_BENCH_CASE;
const work = {arithmetic, branch, fibonacci, list, text, json}[name];
const iterations = Number(process.env.VPP_BENCH_ITERS);
for (let i = 0; i < 16; i++) console.log(work(i));
let previous = null, elapsed, checksum;
for (let batch = 0; batch < 4; batch++) {
  checksum = 0;
  const start = process.hrtime.bigint();
  for (let i = 0; i < iterations; i++) checksum += work(i % 16);
  elapsed = Number(process.hrtime.bigint() - start) / 1e6;
  if (previous !== null && previous !== checksum) throw Error('unstable checksum');
  previous = checksum;
}
console.log(elapsed);
console.log(checksum);
console.log(iterations);
