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
  if (name === 'binary_search_128') data[k%16] = Array.from({length:128}, (_,j)=>j*2);
  if (name === 'map_64') data[k%16] = Array.from({length:64}, (_,j)=>'key-'+(k+j));
  if (name === 'concat_64') data[k%16] = Array.from({length:64}, (_,j)=>'V++'+(k+j)).join('');
  if (name === 'split_32') data[k%16] = Array.from({length:32}, (_,j)=>k+j).join(',');
  if (name === 'unicode_normalize') data[k%16] = 'Vie\u0323\u0302t Nam '+k;
  if (name === 'json_encode') data[k%16] = '['+k+',"Việt",['+Array.from({length:16},(_,j)=>k+j).join(',')+']]';
  if (name === 'json_nested') data[k%16] = JSON.stringify({rows:Array.from({length:8},(_,j)=>({id:j,v:[k+j,j*2]}))});
}
function recursiveFib(n) { return n<2?n:recursiveFib(n-1)+recursiveFib(n-2); }
const functions = {
  arithmetic(k) { return (k + 17) * 31 + (k * 7 + 3) - (k + 5) * 11; },
  branch_loop(k) { let x = k * 97 + 11; for (let i = 0; i < 256; i++) { x += 251; if (x >= 65521) x -= 65521; } return x; },
  fibonacci(k) { let a = 0, b = 1; for (let i = 0; i < k % 16 + 20; i++) { const n = a + b; a = b; b = n; } return a; },
  list_128(k) { const a = []; for (let i = 0; i < 128; i++) a.push(k + i); let sum = 0;
    for (let i = 0; i < 128; i++) if (a[i] % 2 === 0) sum += a[i]; return sum; },
  unicode_replace(k) { return [...data[k % 16].replaceAll('Việt', 'V++')].length; },
  json_parse(k) { const v = JSON.parse(data[k % 16]); let sum = v.n + [...v['tên']].length;
    for (let i = 0; i < 16; i++) sum += v.values[i]; return sum; },
  gcd_64(k) { let sum=0; for(let i=1;i<=64;i++){let a=k*97+i*31,b=k*13+i*17;while(b){let r=a%b;a=b;b=r;}sum+=a;}return sum; },
  float_256(k) { let x=k*0.5,sum=0;for(let i=0;i<256;i++){x=x*0.5+(i%8)*0.25;sum+=x;}return Math.floor(sum*1024); },
  recursion(k) { return recursiveFib(10+k%5); },
  primes_256(k) { let count=0;for(let n=2;n<=256+k;n++){let prime=true;for(let d=2;d*d<=n;d++){if(n%d===0){prime=false;break;}}if(prime)count++;}return count; },
  matrix_4(k) { const a=[],b=[];for(let i=0;i<16;i++){a.push((k+i*3)%17);b.push((k+i*7)%19);}let sum=0;for(let i=0;i<4;i++)for(let j=0;j<4;j++){let cell=0;for(let t=0;t<4;t++)cell+=a[i*4+t]*b[t*4+j];sum+=(i*4+j+1)*cell;}return sum; },
  sort_64(k) { const a=[];for(let i=0;i<64;i++)a.push((i*37+k)%64+k);a.sort((a,b)=>a-b);let sum=0;for(let i=0;i<64;i++)sum+=(i+1)*a[i];return sum; },
  binary_search_128(k) { const a=data[k%16],target=k*2+1;let low=0,high=128;while(low<high){let mid=(low+high)>>1;if(a[mid]<target)low=mid+1;else high=mid;}return low*257+a[low]; },
  map_64(k) { const keys=data[k%16],m=new Map();for(let i=0;i<64;i++)m.set(keys[i],k+i*3);let sum=0;for(let i=63;i>=0;i--)sum+=(i+1)*m.get(keys[i]);return sum; },
  objects_32(k) { const a=[];for(let i=0;i<32;i++)a.push({id:i,payload:[k+i,i*2]});let sum=0;for(const o of a)sum+=(o.id+1)*(o.payload[0]+o.payload[1]);return sum; },
  concat_64(k) { let text='';for(let i=0;i<64;i++)text+='V++'+(k+i);if(text!==data[k%16])throw Error('concat content');return text.length; },
  split_32(k) { const parts=data[k%16].split(',');let sum=0;for(let i=0;i<parts.length;i++)sum+=(i+1)*Number(parts[i]);return sum+parts.length; },
  unicode_normalize(k) { const text=data[k%16].normalize('NFC');if(text!=='Việt Nam '+k)throw Error('unicode NFC');return [...text].length; },
  json_encode(k) { const a=[];for(let i=0;i<16;i++)a.push(k+i);const text=JSON.stringify([k,'Việt',a]);if(text!==data[k%16])throw Error('json encode content');return [...text].length; },
  json_nested(k) { const rows=JSON.parse(data[k%16]).rows;let sum=0;for(let i=0;i<8;i++){const r=rows[i];sum+=(i+1)*(r.id+r.v[0]+r.v[1]);}return sum; },
  exceptions_16(k) { let sum=0;for(let i=0;i<16;i++){try{if((i+k)%4===0)throw Error('benchmark exception');sum+=i;}catch(e){if(e.message!=='benchmark exception')throw e;sum+=i*3;}}return sum; }
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
