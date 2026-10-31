# V++ benchmark gate — tái lập baseline (31/10/2026)

Benchmark so sánh **hai bản Git đã commit** bằng hai binary Release được build
riêng, trên **cùng host**, cùng benchmark harness. Runner thu mẫu bằng V++ và
dùng comparator V++ trong `gói/kiểm thử/so sánh benchmark.vi`.

## Chạy thử

Tại root repo, cần CMake, trình biên dịch C++17, dependency build V++ và CLI
V++ được build từ working tree hiện tại. Chỉ định baseline/candidate bằng SHA
commit; kết quả lưu vào **thư mục mới**, không tự ghi đè kết quả cũ.

```bash
cmake -S . -B build-benchmark-gate-cli -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF
cmake --build build-benchmark-gate-cli --parallel 2 --target vpp-cli

export VPP_EXEC="$PWD/build-benchmark-gate-cli/bin/vpp-cli"
export VPP_GATE_CASES=vm_verify,vm_dispatch_preverified,compiler_pipeline,gc_burst_collect_reuse
export VPP_GATE_SAMPLES=40 VPP_GATE_WARMUPS=3 VPP_GATE_TARGET_MS=50
BASELINE_SHA=f89c2c9 CANDIDATE_SHA=634b3d6  # thay bằng hai commit muốn đo
bash scripts/quality/run_benchmark_gate.sh "$BASELINE_SHA" "$CANDIDATE_SHA" \
  "$PWD/build-benchmark-evidence-001"
```

Để thử nhanh **collector**, dùng `VPP_GATE_CASES=vm_verify`,
`VPP_GATE_SAMPLES=10`, `VPP_GATE_TARGET_MS=10` (không dùng 10 samples để
kết luận GC p95). Runner mặc định đo toàn bộ case, có giới hạn 10–200 process
độc lập/case và ít nhất ba warmup mỗi phía. Phép đo xen kẽ thứ tự hai binary
để giảm ảnh hưởng drift. Số iteration mỗi tiến trình được chọn sau hai pilot.

## Ngưỡng cố định và mã thoát

| Điều kiện | Quyết định |
| --- | --- |
| Median `ns_per_iteration` tăng **>10%** | `FAIL` |
| Median peak RSS hoặc allocation metric tăng **>15%** | `FAIL` |
| GC pause p95 tăng **>15%** | `FAIL` nếu có >=40 process độc lập |
| MAD / median **>5%**, hoặc GC case <40 process | `INCONCLUSIVE` |
| Dữ liệu thiếu/sai, flags instrumentation, checksum khác, harness Git blob khác | `INVALID` |

Mã thoát: **0** PASS, **1** FAIL, **2** INCONCLUSIVE, **3** INVALID.
`INCONCLUSIVE`/`INVALID` phải dừng merge như FAIL. Nhiễu máy CI công cộng
có thể gây INCONCLUSIVE dù code không thay đổi; chạy lại trên host chuyên
dụng trước khi đánh giá kết quả. Nếu harness thay đổi, chọn baseline mới với
cùng các tệp workload đã kiểm tra; không so trực tiếp hai định nghĩa case khác.

## Bằng chứng và phạm vi

Runner xuất `identities.txt`, `baseline-build.log`, `candidate-build.log`,
`pilots.jsonl`, `samples.jsonl`, `report.json`, `comparison.json`,
`decision.json` và `collection.log`. Report chứa SHA commit, SHA-256 archive
Git **thực sự được giải nén để build**, SHA-256 binary, fingerprint harness,
SHA-256 mã collector, comparator và CLI, cùng OS/CPU được dùng để đo.
Khi lưu kết quả cần giữ cả thư mục và đối chiếu phiên bản toolchain trong
metadata build. Các binary trong scratch bị xóa
sau chạy; có hash để đối chiếu nhưng không được đóng gói trong evidence.

Kiểm thử cục bộ ngày 31/10/2026 tại `build-benchmark-gate-e2e-verified/`:
`f89c2c9` so `634b3d6`, case `vm_verify` với 10 process,
PASS; binary SHA-256 hai bản **giống nhau**, median khoảng 52.644 ns và
53.422 ns (chênh +1,5%). Kết quả này chứng minh luồng runner, **không** chứng minh tăng
hiệu năng. Trước khi chốt 1.0 phải chạy trên candidate SHA thực, corpus đầy
đủ, nhiều host, kiểm thử CI tạo regression đỏ và kiểm chứng các case còn
thiếu theo `plans/release-1.0-execution-checklist.md`.
