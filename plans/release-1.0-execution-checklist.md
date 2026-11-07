# V++ 1.0 — Checklist thực thi và phát hành

> Lập ngày: **22/10/2026**. Mốc mã nguồn khi lập kế hoạch: `e8ee877`, nhánh
> `Feature/replace-c-libraries-with-vpp`. Đây là **checklist theo dõi công việc**;
> mọi kết quả build/test ở commit trước phải được kiểm chứng lại trên SHA ứng viên.
>
> **Quyết định hiện tại: NO-GO cho 1.0 Stable; chưa đóng đủ gate để phát hành RC.**
> Tiêu chuẩn quyết định chính thức: [roadmap 1.0](roadmap-1.0.md), phần
> **“Release gate 1.0”**. Đánh dấu `[x]` chỉ khi có *SHA + lệnh chạy + kết quả
> + đường dẫn CI/log/artifact*; không dùng số phiên bản CLI làm bằng chứng đạt gate.

## 0. Nguyên tắc và phạm vi

- **Tier 1:** Linux x86-64, macOS và Windows x64, trên bản cài đặt thực tế.
  Linux ARM64 và cross-target (PowerPC64/RISC-V64/LoongArch64) tiếp tục được
  kiểm tra theo CI hiện có, nhưng không tự thay thế Tier 1.
- **Thư viện cam kết 1.0:** 18 mục `stable` trong
  [ma trận thư viện](../docs/library-readiness-1.0.md). 24 mục `experimental`
  vẫn được gắn nhãn thử nghiệm, không hứa tương thích 1.0.
- **Không đưa vào gate 1.0:** JIT production-grade, LLVM, generic, GUI,
  reflection đầy đủ, concurrency/async phức tạp và việc loại bỏ hoàn toàn C/C++.
  Native AOT/self-hosting chỉ được công bố đúng phạm vi đã được kiểm chứng.
- **Nguồn kiểm chứng:** kết quả chạy trên đúng commit và đúng binary/artifact;
  tài liệu tự báo PASS và lượt test tại SHA cũ chỉ là thông tin tham khảo.
- **Mọi lần kiểm chứng:** ghi SHA, trạng thái working tree, OS/CPU, compiler,
  cấu hình CMake, câu lệnh, exit code, danh sách skipped, log và artifact.
  Dùng `benchmark/reports/` cho raw performance data; tập hợp URL CI và
  biên bản release tại `docs/release-evidence-1.0.md` khi triển khai.

## 1. P0 — Chốt tính đúng của runtime và phạm vi 1.0

- [ ] **REL-01 / P0 — Chốt SHA ứng viên.** Rà soát các commit sau `e8ee877`,
  xác nhận `git status --short` sạch, tạo issue/PR cho lỗi còn mở và lưu SHA
  ứng viên. Nếu tiếp tục sửa code, phải chạy lại gate trên SHA mới.
  **Đạt khi:** có SHA cụ thể được dùng xuyên suốt build/test/performance/release.
- [ ] **REL-02 / P0 — Khóa correctness của P0.1, P0.2 và P0.5.** Chạy
  `vpp-rc-internal-hardening`, `vpp-runtime-p0-hardening`,
  `vpp-import-precedence-hardening` và `vpp-integration`; kiểm chứng
  bytecode verification cache/invalidation, HTTP/JSON qua V++, import collision
  và `VPP_GC_INTERVAL=1`. Kiểm tra rõ test socket/TLS nào bị skip do
  `EPERM` localhost; chạy lại các ca đó trong CI có hỗ trợ mạng.
  **Đạt khi:** tất cả case bắt buộc PASS, không có skip bị ngầm tính là PASS.
- [ ] **REL-03 / P0 — Hoàn tất GC P0.3.** Thêm hoặc khóa test workload
  `burst → collect → reuse` đo **allocation count, số lần resize/rehash,
  GC pause p95, peak RSS**, root/cycle, exception unwind; kiểm chứng
  periodic collection không trim lặp và explicit trim giữ đúng ngưỡng.
  **Đạt khi:** invariant PASS, có raw telemetry trước/sau cùng workload;
  không đổi latency lấy memory growth vượt ngân sách.

## 2. P0 — Benchmark và regression gate (P0.4)

Tiến độ triển khai ngày 24/10/2026: đã có harness-v3, runner/comparator,
GC burst/collect/reuse + explicit trim, allocation requests/bytes, peak RSS và
GC pause p95. Raw report tại `benchmark/reports/local-2026-10-24/` so sánh
`cc675ca` với `b39933e` + benchmark patch: **16 PASS, 3 INCONCLUSIVE, 0 FAIL**.
Ngày 30/10/2026, runner/comparator Python đã được gỡ theo yêu cầu.
Comparator thống kê đã được thay bằng V++, có self-test interpreter/native
và CLI exit 0/1/2/3. Từ 31/10/2026, runner hai revision
`scripts/quality/run_benchmark_gate.sh`, collector V++
`scripts/quality/collect_benchmark_gate.vi` và workflow PR
`.github/workflows/benchmark-regression.yml` đã được thêm vào mã nguồn.
Smoke trên macOS với `0f77c43`/`f89c2c9` và `vm_verify` (10 mẫu) PASS,
nhưng hash hai binary trùng nhau nên chỉ kiểm chứng pipeline. GC 40 mẫu
với binary giống nhau cũng PASS. Chưa có kết quả CI từ xa hoặc regression
cố ý trên workflow mới; xem [hướng dẫn tái lập](../benchmark/BASELINE.md).
P0.4 chưa nghiệm thu.
Giữ các mục nghiệm thu bên dưới mở vì còn ba case nhiễu, HTTP/JSON workload,
variable-map rehash telemetry và bằng chứng CI trên SHA ứng viên.

- [ ] **REL-04 / P0 — Chuẩn hóa harness.** Rà soát
  `benchmark/benchmark_baseline.cpp` và `benchmark/CMakeLists.txt`:
  `vm_verify`, `vm_dispatch_preverified`, `vm_end_to_end`, các compiler
  stage, `compiler_pipeline`, package/module graph, GC. Bổ sung workload
  HTTP/JSON V++ và GC telemetry còn thiếu. Dựng/reset fixture ngoài timer
  đúng biên đo, validate checksum/output và thêm corpus nhiều module.
  **Đạt khi:** cùng input cho kết quả bất biến, metadata và case ID ổn định.
- [ ] **REL-05 / P0 — Viết runner/comparator trong `scripts/quality/`.** Chạy
  baseline và candidate **xen kẽ trên cùng host**, build Release riêng cho
  hai revision bằng cùng toolchain, flags, corpus và GC/JIT policy. Dùng ít
  nhất **3 warm-up + 10 lần đo độc lập/case**; mặc định **40 lần** cho p95 GC.
  Chốt iterations sau pilot, trước lúc đo so sánh.
  **Đạt khi:** comparator báo PASS/FAIL/INCONCLUSIVE chính xác, từ chối
  metadata lệch, case thiếu và dữ liệu raw không hợp lệ; có unit/fixture
  cho ba trạng thái này.
- [ ] **REL-06 / P0 — Chốt và đo performance budget.** Thiết lập ngưỡng
  **trước khi chạy candidate**: mặc định median time tăng `>10%` là FAIL;
  GC pause p95 hoặc peak RSS tăng `>15%` là FAIL; `MAD/median >5%`
  hoặc <40 samples cho quyết định p95 là INCONCLUSIVE cần đo lại.
  Không đổi ngưỡng để làm xanh một kết quả đã thất bại.
  **Đạt khi:** `benchmark/reports/` có raw samples, SHA baseline/candidate,
  công cụ/máy/flags, median/p95/MAD, GC allocation/RSS và quyết định từng
  case; [BASELINE.md](../benchmark/BASELINE.md) ghi lệnh tái lập.
- [ ] **REL-07 / P0 — Tích hợp regression gate vào CI.** Dùng cùng runner
  có hiệu chỉnh hoặc máy benchmark cố định; upload raw report làm CI
  artifact; FAIL khi vượt ngưỡng hoặc report không hợp lệ; INCONCLUSIVE
  không được xem là PASS. Tách timing khỏi sanitizer/coverage.
  **Đạt khi:** thử cố tình tạo regression làm CI đỏ, mẫu đạt chuẩn làm CI xanh.

## 3. P0 — Regression, native, an toàn bộ nhớ

- [ ] **REL-08 / P0 — Full regression tại candidate SHA.** Build **Release**,
  chạy toàn bộ CTest trên mỗi nền tảng Tier 1 và `run_tests.sh`/test integration
  tương ứng; so tổng số PASS/FAIL/SKIP với danh sách đăng ký hiện hành.
  Chạy fuzz corpus, compiler stress, malformed input, import/package và
  VM-state invariant. Không suy số PASS từ tài liệu lịch sử (ví dụ 339).
  **Đạt khi:** `0 FAIL`, mọi skip được duyệt và ghi nguyên nhân.
- [ ] **REL-09 / P0 — Linux sanitizer.** Chạy AddressSanitizer,
  UndefinedBehaviorSanitizer và leak detection (`ASAN_OPTIONS=detect_leaks=1`)
  trên các stress/runtime/compiler cases áp dụng được; xử lý mọi lỗi thật.
  Đối chiếu job trong `.github/workflows/c-cpp.yml` với tập ca bị exclude,
  nêu rõ lý do từng nhóm không chạy dưới sanitizer.
  **Đạt khi:** CI Linux trên candidate SHA xanh, lưu log + test summary.
- [ ] **REL-10 / P0 — Native AOT/ABI/self-hosting.** Xác nhận native
  compiler, object/linking, Stage 0 → 1 → 2 → 3 và SHA Stage 2/3 bằng nhau
  trên nền tảng được hỗ trợ; chạy từ **prefix đã cài**. Trên Windows,
  khóa phạm vi COFF/ABI/object tests theo tài liệu hiện tại, không mô tả
  Windows đã self-host ba thế hệ nếu chưa có chứng cứ.
  **Đạt khi:** log ghi target, ABI, kết quả probe và SHA bootstrap.
- [ ] **REL-11 / P0 — TLS/network thật.** Kiểm thử DNS, TCP, HTTP(S), TLS
  handshake, chuỗi CA, hostname đúng/sai, cert hết hạn hoặc không tin cậy,
  timeout và lỗi mạng bằng fixture tái lập, có test qua public V++ API.
  Chốt rõ các API TLS/HTTP nào ổn định và giới hạn nào phải ghi trong docs.
  **Đạt khi:** đường TLS được công bố stable có end-to-end PASS trên Tier 1;
  các trường hợp phải reject được reject an toàn.

## 4. P0 — Thư viện chuẩn, công cụ và môi trường cài đặt

- [ ] **REL-12 / P0 — Audit 18 stable libraries.** Kiểm tra từng root/entry
  trong `vpp-libraries.json`, dependency closure không đi qua `experimental`,
  backend/facade hoạt động, test hồi quy tương ứng trong
  [library-readiness-1.0.md](../docs/library-readiness-1.0.md); bổ sung test
  nếu thiếu. Công bố rõ 24 thư viện experimental trong release notes.
  **Đạt khi:** ma trận 18/18 có case PASS và có bằng chứng dependency scan.
- [ ] **REL-13 / P0 — Kiểm thử trải nghiệm bản cài.** Chạy `vdk chẩn đoán`
  (qua CLI/alias thực tế), kiểm tra LSP nhìn thấy đủ 42 library roots,
  formatter/linter, VS Code extension, `vpp.json`/`vpp.lock` deterministic,
  offline restore, Unicode NFC trên macOS/Windows và các đường dẫn có dấu.
  **Đạt khi:** dùng hoàn toàn binary/resource từ install prefix sạch; không
  trỏ về source/build tree hay cache `.vpp` của workspace.

## 5. P0 — CI và release artifact trên ba hệ điều hành

- [ ] **REL-14 / P0 — CI matrix trên cùng SHA.** Xác minh các job của
  `.github/workflows/c-cpp.yml` (Linux x64/ARM64, macOS, Windows, sanitizers,
  quality/native cross smoke theo phạm vi). CI hiện kích hoạt khi push/PR
  vào `developer`; bảo đảm commit ứng viên thật sự được chạy (PR/merge hoặc
  trigger hợp lệ), rồi lưu link run + SHA + job summary.
  **Đạt khi:** tất cả job bắt buộc xanh trên đúng SHA, không dựa vào status
  commit/branch khác.
- [ ] **REL-15 / P0 — Preflight packaging.** Chạy
  `.github/workflows/release-binaries.yml` qua `workflow_dispatch` tại SHA
  ứng viên để dựng `vpp-linux-x64.tar.gz`, `vpp-macos.tar.gz`,
  `vpp-windows-x64.zip` cùng VDK; lưu SHA256 artifact và logs.
  Kiểm tra Windows UTF-8, phụ thuộc DLL/so/dylib, quyền thực thi và không
  mang `.vpp` cache.
  **Đạt khi:** cả 3 job hoàn tất và artifact có thể tải/cài thử.
- [ ] **REL-16 / P0 — Smoke trên *đúng artifact* đã đóng gói.** Cài mới,
  upgrade, stale-file cleanup, chạy Native AOT, package/stdlib, sample
  quản lý kho, HTTP localhost, CLI, uninstall và rollback cơ bản trên
  Windows/macOS/Linux. Build và chạy sample từ PATH/prefix mới trong môi
  trường sạch; kiểm tra không còn dependency vào build tree.
  **Đạt khi:** lưu log/câu lệnh/sample output của từng OS, `0 FAIL` và
  HTTP tests thực thi thay vì bị skip vì sandbox.

## 6. RC → Stable 1.0

- [ ] **REL-17 / RC gate — Đánh giá GO/NO-GO bằng chứng.** Có đủ
  REL-01…REL-16; không còn P0, VM corruption, crash trên input hợp lệ,
  package không reproducible hoặc regression chưa xử lý. Tổng hợp evidence
  theo SHA vào `docs/release-evidence-1.0.md`. Bất kỳ FAIL/INCONCLUSIVE
  nào cũng giữ trạng thái NO-GO.
- [ ] **REL-18 / RC — Phát hành bản thử nghiệm có nhãn đúng.** Chốt scope,
  changelog, migration guide, known limitations, version CLI/CMake/package
  thống nhất với nhãn RC (chẳng hạn `1.0.0-rc.1`); tạo RC trên SHA đã duyệt,
  cài thử công khai và sửa lỗi phát hiện. Mọi thay đổi sau RC phải kiểm chứng
  lại gate liên quan và chạy lại matrix release trên SHA mới.
- [ ] **REL-19 / Stable — Ký duyệt phát hành.** Sau giai đoạn RC đạt yêu
  cầu: rà release notes, API/CLI/lockfile compatibility, các artifact và
  checksum, thống nhất version `1.0.0` trong mọi nơi, tạo tag `v1.0.0`
  trên SHA đã nghiệm thu. Kiểm tra workflow `release: published` tạo đúng
  artifact cho tag đó trước khi quảng bá hoặc coi phát hành hoàn tất.
- [ ] **REL-20 / Sau phát hành — Xác nhận độc lập.** Tải và cài từ trang
  release chính thức trên cả 3 OS, chạy smoke ngắn, công bố checksum,
  tài liệu cài đặt, known issues và nhánh hỗ trợ `1.0.x`.

## Thứ tự làm việc ngay trong các phiên tiếp theo

1. **Phiên 1:** REL-01, REL-02 và kiểm tra các lỗi mới nhất trên Windows;
   chốt SHA/corpus và loại các lỗi correctness trước khi đo hiệu năng.
2. **Phiên 2:** REL-03 + REL-04, hoàn thiện GC telemetry và boundary benchmark.
3. **Phiên 3:** REL-05 + REL-06 + REL-07, đóng P0.4 bằng report so sánh
   baseline/candidate và regression CI gate.
4. **Phiên 4:** REL-08…REL-13, hardening/sanitizer, audit stdlib và native/TLS.
5. **Phiên 5:** REL-14…REL-16, CI đầy đủ và smoke artifact trên Tier 1.
6. **Phiên 6:** REL-17…REL-20, GO/NO-GO, RC rồi Stable khi đủ bằng chứng.

### Bộ lệnh kiểm chứng khởi đầu

Chạy từ thư mục gốc repository, thay `<SHA_ỨNG_VIÊN>` bằng commit thực tế.
Các lệnh dưới đây **chưa được thực thi chỉ vì xuất hiện trong checklist**.

```bash
git rev-parse HEAD
git status --short
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel 4
ctest --test-dir build-release --parallel 2 --output-on-failure --no-tests=error
```

Kiểm chứng trọng điểm khi cần phản hồi nhanh (không thay thế full suite):

```bash
ctest --test-dir build-release --output-on-failure \
  -R '^vpp-(rc-internal-hardening|runtime-p0-hardening|import-precedence-hardening)$'
```

Benchmark để kiểm tra harness (chưa đủ để quyết định performance gate):

```bash
cmake -S . -B build-benchmark -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=ON
cmake --build build-benchmark --parallel 4 --target vpp-benchmark-baseline
./build-benchmark/bin/vpp-benchmark-baseline
```

### Mẫu bằng chứng cho từng mục

```text
ID: REL-XX
Trạng thái: TODO | IN_PROGRESS | BLOCKED | VERIFIED
Candidate SHA / clean tree:
Hệ điều hành / kiến trúc / compiler / CMake flags:
Lệnh chạy và exit code:
Số test PASS / FAIL / SKIP (và nguyên nhân skip):
CI run URL, artifact SHA256 hoặc đường dẫn raw report/log:
Nhận xét / issue cần xử lý:
```

Khi một mục có bằng chứng hợp lệ, đổi `[ ]` thành `[x]` và ghi đường dẫn
evidence ngay sau mục đó. Không đánh dấu hoàn tất dựa vào việc source hoặc
workflow đã tồn tại; mỗi lần đổi candidate SHA cần tái xác nhận release gate.
