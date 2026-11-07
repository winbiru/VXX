require 'json'
name = ENV.fetch('BENCH_CASE', 'arithmetic')
iterations = Integer(ENV.fetch('BENCH_ITERATIONS', '16'))
seed = Integer(ENV.fetch('BENCH_SEED', '17'))
raise 'iterations' unless iterations >= 16 && iterations <= 100000000 && iterations % 16 == 0
data = Array.new(16, '')
16.times do |i|
  k = seed + i
  data[k % 16] = 'Xin chào Việt Nam 🌿 — Việt! ' * (k % 4 + 4) + k.to_s if name == 'unicode_replace'
  if name == 'json_parse'
    data[k % 16] = '{"n":' + k.to_s + ',"tên":"Việt Nam 🌿","values":[' + (0...16).map { |j| (k+j).to_s }.join(',') + ']}'
  end
end
functions = {
  'arithmetic' => ->(k) { (k + 17) * 31 + (k * 7 + 3) - (k + 5) * 11 },
  'branch_loop' => ->(k) { x = k * 97 + 11; 256.times { x += 251; x -= 65521 if x >= 65521 }; x },
  'fibonacci' => ->(k) { a = 0; b = 1; (k % 16 + 20).times { a, b = b, a+b }; a },
  'list_128' => ->(k) { a = []; 128.times { |i| a << k+i }; sum = 0; 128.times { |i| sum += a[i] if a[i] % 2 == 0 }; sum },
  'unicode_replace' => ->(k) { data[k % 16].gsub('Việt', 'V++').length },
  'json_parse' => ->(k) { v = JSON.parse(data[k % 16]); sum = v['n'] + v['tên'].length; 16.times { |i| sum += v['values'][i] }; sum }
}
fn = functions.fetch(name)
checks = (0...16).map { |i| fn.call(seed+i) }
expected = checks.sum { |v| v % 997 } * (iterations / 16)
batch = lambda do
  checksum = 0.0
  start = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  iterations.times { |i| checksum += fn.call(seed + i % 16) % 997 }
  elapsed_ms = (Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond) - start) / 1_000_000.0
  raise 'checksum/clock' unless checksum == expected && elapsed_ms > 0
  [checksum, elapsed_ms]
end
warmup_ms = Array.new(3) { batch.call[1] }
checksum, elapsed_ms = batch.call
puts JSON.generate(case: name, iterations: iterations, seed: seed, checks: checks,
  checksum: checksum.to_i.to_s, elapsed_ms: elapsed_ms, ns_per_op: elapsed_ms*1_000_000/iterations,
  warmup_ms: warmup_ms)
