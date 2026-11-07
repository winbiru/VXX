# Đánh giá và kế hoạch cải tiến V++ — 31/10/2026

## Phạm vi và bằng chứng

- Nhánh kiểm tra: `Feature/benchmark_refactor_module`; HEAD `f89c2c9` (31/10/2026), working tree sạch tại thời điểm đánh giá.
- `ctest --test-dir build-ci-regression-fix -N`: **89 bài đăng ký**; đây là số lượng bài, **không phải** 89 bài PASS.
- Kiểm chứng mới trên macOS ngày 31/10: **9/9 PASS** trong hai lượt CTest chọn lọc (có một fixture bootstrap được kéo vào theo phụ thuộc):
  - `vpp-native-cli-compiler-bootstrap`, `vpp-integration-backend`, `vpp-benchmark-vpp-tool`, `vpp-native-lsp-diagnostics`, `vpp-native-lsp-format`, `vpp-native-aot-numeric-add-fastpath` (6/6, 91,46 giây).
  - `vpp-runtime-p0-hardening`, `vpp-rc-internal-hardening`, `vpp-import-precedence-hardening` (3/3, 0,77 giây).
- **Chưa được xác nhận trong lượt này**: toàn bộ 89 bài, Windows/Linux CI trên cùng SHA, sanitizer, cài đặt từ release artifact, TLS/HTTPS end-to-end và tỷ lệ benchmark trên đúng SHA hiện tại.
- Tài liệu tham chiếu: `plans/release-1.0-execution-checklist.md`, `plans/roadmap-1.0.md`, `docs/compiler-self-hosting-status.md`, `docs/library-readiness-1.0.md` và `report/benchmark-languages-2026-10-30.md`.

## Đánh giá mức độ trưởng thành

| Trục | Năng lực đã có | Thiếu hoặc rủi ro | Đánh giá |
| --- | --- | --- | --- |
| Ngôn ngữ và công cụ | Frontend, VM, bytecode, OOP, module, package, LSP/VS Code, native codegen | Cần full regression và smoke bản cài trên ba OS cùng SHA | Có nền tảng rộng; chưa khóa phát hành |
| Tự biên dịch | Tài liệu ghi Stage 1→2→3 và byte Stage 2/3 trùng nhau; Stage 3 chạy chương trình | Kết quả chưa được chạy lại tại HEAD trong lượt này; còn biên C/C++ ở một số đường chạy | Đạt mốc bootstrap quan trọng, chưa phải toàn bộ runtime thuần V++ |
| Thư viện | 42 root: 18 `stable`, 24 `experimental` theo ma trận | Cần xác nhận dependency, test thực tế, contract cho từng root stable | Chưa nghiệm thu 18/18 |
| Mạng và mật mã | DNS, transport, crypto record và nhiều fixture đã có | TLS handshake, xác thực CA/hostname và HTTPS an toàn chưa hoàn tất end-to-end | Không công bố TLS/HTTPS stable trước khi khóa gate |
| Hiệu năng | Kernel native i32 có kết quả gần C++ trong các bài hẹp | AOT ứng dụng chậm VM C++ tương thích 14,65–28,65 lần trên 6 workload của báo cáo 30/10; cần profile nguyên nhân | Điểm nghẽn sản phẩm lớn nhất |
| Release/CI | CTest, CI matrix, workflow gói phát hành, checklist REL-01..REL-17 | Còn nhiều gate chưa có bằng chứng cùng SHA | **NO-GO 1.0 Stable** |

Không dùng phần trăm tiến độ từ `plans/progress.md` (22/09) để suy ra mức sẵn sàng hiện tại: công việc compiler, runtime, CI và phát hành có trọng số khác nhau.

## Kế hoạch theo thứ tự phụ thuộc

### Giai đoạn A — Khóa tính đúng và tái lập kết quả (ưu tiên P0)

**Đầu vào:** một SHA ứng viên, cây Git sạch và cấu hình build cố định.

1. Chạy đầy đủ 89 CTest trên macOS, lưu tên bài, PASS/FAIL/SKIP và thời gian; chạy thêm `run_tests.sh`, integration, VM/ASM/selfhost với timeout phù hợp thực tế. Sửa lỗi thật, cập nhật SHA ứng viên rồi chạy lại.
2. Chạy CI trên **cùng SHA** ở macOS, Linux x86-64 và Windows x64; phân loại skip do sandbox/network, không tính skip là PASS. Chạy Linux ASan/UBSan/leak detection theo REL-09.
3. Chạy smoke từ **prefix cài đặt sạch**: VDK/CLI, Unicode đường dẫn, package/lock, LSP và native executable; lưu bằng chứng artifact và dependency. Không lấy việc chạy binary từ build tree làm bằng chứng release.
4. Chốt phạm vi thư viện stable bằng ma trận 18/18 và tách rõ API network/TLS thử nghiệm.

**Hoàn tất khi:** 0 lỗi bắt buộc trên ba OS tại cùng SHA; skip được giải trình; log/CI link/command/artifact lưu vào `docs/release-evidence-1.0.md` theo checklist REL-01, 02, 08, 09, 12–16.

### Giai đoạn B — Xử lý nguyên nhân chậm của AOT ứng dụng (ưu tiên P0/P1)

**Bước đo trước khi sửa:** dùng lại 6 workload và oracle của benchmark 30/10; đo CPU time theo call path, số allocation/bytes, tần suất boxing/unboxing, dispatch, copy chuỗi/list, GC pause p95 và peak RSS. So sánh `vpp-vm`, `vpp-aot`, `vpp-vm-asm` trên cùng input; ghi profile và call graph. Thí nghiệm i32 kernel được theo dõi riêng, không thay kết quả AOT ứng dụng.

**Tập trung cải tiến:**

1. Vòng lặp số học/nhánh: chuyên biệt hóa đường `i32`/`f64` khi chứng minh được kiểu, giảm gọi helper/runtime trên mỗi iteration, giữ nguyên kiểm tra overflow và conversion.
2. Collection: giảm boxing và cấp phát tạm; reserve capacity, reuse buffer, giảm copy khi map/filter/list traversal; đo allocation/op.
3. Unicode và JSON: profile chi phí giải mã, chuyển kiểu, tạo chuỗi và object; tối ưu phần nóng có benchmark chứng minh, giữ nguyên xử lý NFC, emoji và lỗi parse.
4. Mọi thay đổi codegen/runtime phải có differential test đối chiếu interpreter với AOT cho giá trị, exception, GC/root và Unicode.

**Mục tiêu thử nghiệm:** giảm tối thiểu 2 lần median/op ở **hai workload chậm nhất được chọn sau profiling**, không làm regression correctness; thu hẹp khoảng cách AOT/VM trước khi đặt mục tiêu ngang C++ hoặc Python. Đây là mục tiêu cải tiến, không phải hiệu năng đã đạt.

### Giai đoạn C — Biến benchmark thành CI gate (ưu tiên P0)

1. Hoàn thành orchestrator thu mẫu **baseline/candidate xen kẽ** trên cùng máy, cùng compiler/flags/corpus, build Release tách biệt. Comparator V++ hiện có sẽ xác minh metadata, checksum và quyết định `PASS/FAIL/INCONCLUSIVE`.
2. Chạy >=3 warmup và >=10 tiến trình độc lập/case; riêng quyết định GC p95 cần >=40 mẫu; lưu raw report và SHA của cả hai revision.
3. Khóa ngưỡng trước phép đo: median chậm hơn >10% là FAIL; peak RSS hoặc GC p95 tăng >15% là FAIL; MAD/median >5% hoặc thiếu mẫu là INCONCLUSIVE, cần đo lại.
4. Tạo một regression cố ý để chứng minh gate CI chuyển đỏ và một mẫu hợp lệ để chứng minh gate xanh; phân biệt job `benchmark-smoke` với regression gate đầy đủ.

**Hoàn tất khi:** REL-04..REL-07 có bằng chứng CI và báo cáo máy đọc được; thay đổi hiệu năng không còn chỉ đánh giá thủ công.

### Giai đoạn D — Mở rộng độc lập nền tảng và chốt 1.0 RC (ưu tiên P1/P2)

1. Kiểm chứng self-host Stage 0→1→2→3, hash Stage 2/3, chạy compiler từ prefix đã cài, ghi rõ phạm vi OS/ABI/host bridge. Lập danh sách dependency C/C++ còn lại và thay dần theo độ rủi ro, **không** gộp việc loại bỏ 100% C/C++ vào gate 1.0.
2. Hoàn thiện TLS handshake và kiểm chứng CA/hostname/cert hết hạn/không tin cậy với fixture end-to-end trước khi tuyên bố HTTPS stable; nếu chưa đủ, giữ `experimental` và mô tả giới hạn.
3. Đạt REL-01..REL-16 trên đúng SHA; tổng hợp bằng chứng REL-17, đóng gói và cài thử trên ba OS trước khi ra quyết định RC → Stable.

**Hoàn tất khi:** release checklist có SHA, log, CI URL và artifact cụ thể; lỗi/skip blocking = 0, các API chưa đủ an toàn được ghi rõ phạm vi hỗ trợ.

## Cơ chế thực thi

- Đề xuất chu kỳ **1–2 tuần cho mỗi giai đoạn A/B/C** tùy số lỗi thực tế; D là gate theo bằng chứng, không cam kết ngày phát hành khi chưa hoàn thành A–C.
- Mỗi PR có một baseline, một giả thuyết kỹ thuật và một phép đo xác minh; ghi `before/after` kèm CI/log để tránh tối ưu dựa vào cảm giác.
- Thứ tự quan trọng: **correctness → profile/tối ưu → gate tự động → self-host/RC**. Không tăng số lượng tính năng experimental trong khi còn gate 1.0 bắt buộc chưa đóng.

## Cập nhật triển khai trong ngày 31/10/2026

- Đã thêm collector benchmark bằng V++ và runner lấy riêng hai Git archive,
  build Release cho từng revision, đo xen kẽ process và lưu raw JSONL. Bằng
  chứng nhận diện có SHA-256 của archive được build, binary, V++ CLI, mã
  collector và comparator; báo cáo có thông tin OS/CPU.
- Đã bổ sung workflow `.github/workflows/benchmark-regression.yml` và mở
  rộng job `benchmark-smoke` để xác minh bốn mã thoát PASS/FAIL/INCONCLUSIVE/
  INVALID. Đây là **thay đổi cấu hình CI chưa chạy trên GitHub**.
- Chạy cục bộ tại `f89c2c9` + working tree thay đổi: 5/5 CTest chọn lọc
  PASS (bao gồm fixture bootstrap), fixture comparator có mã thoát 0/1/2/3
  đúng kỳ vọng, runner với `f89c2c9`/`634b3d6` đạt PASS cho `vm_verify` với
  10 process/case; log tại `build-benchmark-gate-e2e-verified/` (thư mục build
  bị ignore). Vì binary hai revision có hash bằng nhau, đây chỉ là kiểm tra
  pipeline và provenance, không khẳng định cải thiện hiệu năng.
- **Còn mở:** chạy full 89 CTest, workflow GitHub trên SHA ứng viên, trường
  hợp regression thực sự làm CI đỏ, benchmark HTTP/JSON và telemetry GC còn
  thiếu, profiling/tối ưu AOT ứng dụng và các gate release còn lại.
