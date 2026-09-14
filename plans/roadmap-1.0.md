# Roadmap V++ 1.0

> Cập nhật: 14/09/2026
>
> Mục tiêu 1.0 không còn là mở rộng cú pháp theo chiều rộng. Trọng tâm là đóng băng
> semantics, làm cứng compiler/runtime, hoàn thiện package + stdlib + toolchain và chứng
> minh V++ chạy ổn định trên dự án thực tế.
>
> Checklist trong file này cố ý liệt kê chi tiết phần **còn phải chứng minh/đóng contract**,
> vì vậy tỷ lệ checkbox không được dùng như phần trăm khối lượng hoàn thành. Ước lượng tiến
> độ tới 1.0 nằm ở cuối tài liệu và được đánh giá theo khối lượng kỹ thuật.

## Định nghĩa V++ 1.0

V++ 1.0 được coi là đạt khi ngôn ngữ có semantics ổn định, compiler/runtime đáng tin cậy,
tracing GC hoạt động tốt dưới tải, module/package đủ dùng, stdlib đủ xây ứng dụng thực tế và
toolchain có thể cài đặt/chạy trên Windows, macOS và Linux.

Các hạng mục **không bắt buộc** để chặn 1.0: JIT production-grade, LLVM backend, generic,
reflection đầy đủ, GUI, FFI đầy đủ và concurrency/async phức tạp. Các phần này chỉ được kéo
vào 1.0 nếu chúng cần thiết để đóng một contract nền tảng đã có.

## 1. Đóng băng semantics ngôn ngữ

- [x] Viết ADR chốt type policy: V++ 1.0 dùng dynamic typing; value/call boundary,
  default parameter và arity compile-time/runtime đã có contract + regression.
- [x] Đóng contract cho scope, shadowing, `rỗng`, truthiness, equality, so sánh và chuyển
  đổi kiểu ngầm; thêm regression cho các trường hợp biên.
- [x] Đóng contract lỗi runtime: loại lỗi, thông điệp, unwind và trạng thái VM sau lỗi.
- [x] Đóng semantics object hiện tại: `mình`, `gốc`, constructor có tham số, đơn kế thừa
  class, nhiều interface compile-time và visibility method private/protected/public.
- [x] Chốt override method đầy đủ: arity, visibility narrowing, constructor interaction và
  diagnostic cho override không hợp lệ.
- [x] Chốt module semantics còn lại: explicit export/re-export, truy cập symbol không export,
  import cycle và diagnostic có đường dẫn module rõ ràng.
- [x] Chốt lambda/closure capture: capture theo value/reference, lifetime và mutation.
- [x] Quyết định destructor/finalizer: không hỗ trợ trong 1.0 hoặc định nghĩa lifecycle rõ ràng;
  không để behavior ngầm phụ thuộc GC.

## 2. Runtime và VM ổn định dưới tải

- [x] Runtime object model, superclass dispatch và tracing GC có cycle sweep đã chạy end-to-end.
- [x] Xây GC stress suite: marker/`trackValue()` dùng worklist lặp; unit test khóa cycle sâu
  4.096 node + burst 1.024 self-cycle, còn regression `.vi` khóa graph sâu/allocation liên tục,
  caller root qua child VM và collection với `VPP_GC_INTERVAL=1`.
- [x] Stress call stack: regression `.vi` chạy recursion sâu + exception xuyên nhiều child VM;
  VM giới hạn độ sâu lời gọi ở 256 và trả `CallBoundary` có diagnostic ổn định thay vì tràn
  native C++ stack. Handler unit khóa việc unwind call frame sau lỗi.
- [x] Bổ sung invariant/regression để runtime error không làm corrupt stack, call frame,
  module lifecycle hoặc heap roots cho lần chạy tiếp theo. Handler unit khóa cùng VM tiếp tục
  gọi function hợp lệ sau `VmFault`, caller stack/root vẫn nguyên và module failed-state ổn định.
- [ ] Chạy ASan/UBSan/LSan hoặc công cụ tương đương cho stress suite; local AppleClang
  ASan+UBSan hiện xanh 16/16 sau khi sửa UB opcode và GC destructor-chain stack overflow.
  Ubuntu CI đã ép `detect_leaks=1`; cần một lượt CI Linux xanh để đóng leak gate.
- [x] Xây stack trace có source file, line/column, function/method và module identity;
  debug metadata đi song song bytecode nên không thay đổi layout `Instruction`/ABI snapshot,
  `RuntimeError` giữ frame có cấu trúc và CLI nén frame đệ quy lặp liên tiếp. Diagnostic runtime
  dùng mã ổn định `VPP-Rxxxx` + catalog tập trung để sinh nhóm lỗi, giải thích và gợi ý mà
  không parse chuỗi `what()` hoặc lộ opcode/program counter nội bộ.
- [ ] Bổ sung benchmark dài hạn và profiler/event hook cho CPU/timing/allocation/GC latency;
  chỉ tối ưu JIT/dispatch dựa trên số đo.

## 3. Module và package system dùng được cho dự án thật

- [ ] Chốt project manifest (`vpp.json` hoặc `vpp.toml`) và layout project chuẩn.
- [ ] Tách hoàn toàn package/bare-module resolution khỏi compatibility path trong compiler;
  resolver phải sở hữu identity, source và dependency policy.
- [ ] Hỗ trợ semantic versioning + version range và diagnostic conflict rõ ràng.
- [ ] Tạo deterministic lockfile và reproducible install/build.
- [ ] Hỗ trợ package cache/offline, local path package và Git package.
- [ ] Thiết kế registry package và metadata tối thiểu cho publish/install.
- [ ] Hoàn thiện CLI dependency flow tương đương `vpp cài`, `vpp cập nhật`, `vpp xóa`,
  `vpp khóa` sau khi contract manifest/lockfile ổn định.

## 4. Standard library 1.0

- [ ] Audit UTF-8 end-to-end cho string/index/slice/length/case/normalization vì tiếng Việt là
  first-class syntax; bổ sung corpus Unicode và regression malformed input.
- [ ] Chuẩn hóa filesystem/path API đa nền tảng và edge case separator/encoding.
- [ ] Hoàn thiện time/date API và timezone contract ở mức đủ dùng cho ứng dụng thực tế.
- [ ] Bổ sung process API tối thiểu nếu permission model cho phép.
- [ ] Bổ sung crypto cơ bản bằng implementation đã được kiểm chứng: secure random, hash/HMAC;
  không tự viết primitive mật mã trong VM.
- [ ] Nâng package testing: assertion, expected error, setup/teardown và test discovery rõ ràng.
- [ ] Audit các package đã có: collection, file, JSON, HTTP client/server, math, random,
  environment, logging và config; đóng behavior + lỗi + cross-platform test cho 1.0.

## 5. Compiler hardening

- [x] `CompilationContext` đã sở hữu registry chính và có regression compile song song bằng
  context độc lập.
- [x] Loại bỏ mutable global state/facade còn lại khỏi production compiler: direct codegen,
  callable lookup và recursive import nhận registry tường minh; CLI/tooling/`compileSource`
  dùng `CompilationContext`. Facade thread-local chỉ còn cho compatibility test/caller cũ,
  và regression "poison legacy registry" khóa việc context-driven compile không phụ thuộc nó.
- [ ] Thêm fuzzing cho lexer/parser và malformed-source corpus có seed cố định.
- [ ] Thêm malformed AST/invalid IR tests và bytecode verifier trước khi VM thực thi input
  không tin cậy.
- [ ] Khóa deterministic compilation/reproducible bytecode cho cùng source + dependency lock.
- [ ] Stress source/project lớn và theo dõi peak memory/compiler time để phát hiện regression.

## 6. Toolchain và trải nghiệm phát triển

- [ ] Đóng public CLI contract cho `new`, `run`, `test`, `build` và package commands.
- [ ] Hoàn thiện formatter và linter đủ ổn định để dùng trong CI/editor.
- [ ] Nâng LSP: diagnostic, completion, go-to-definition, hover và rename dựa trên semantic model.
- [ ] Đồng bộ VS Code extension với LSP/toolchain 1.0 và syntax mới.
- [ ] Bổ sung project templates và ít nhất một sample project thực tế ngoài regression corpus.
- [ ] Hoàn thiện installer/update path cho Windows, macOS và Linux; đánh giá Homebrew/winget
  sau khi install contract ổn định.
- [ ] Hoàn thiện documentation 1.0: language reference, package guide, CLI guide, debugging,
  migration/changelog và examples.

## Mốc phát hành

### V++ 0.7 — Runtime / compiler cleanup

- [x] `CompilationContext` sở hữu state chính và concurrent compilation có regression.
- [x] Module lifecycle Phase 1 + runtime initialization dependency-first.
- [x] `VM::run()` đã tách thành opcode handlers có fixture test nội bộ.
- [x] Loại facade/global mutable state khỏi production path và khóa compiler re-entrant contract.

### V++ 0.8 — Object/runtime stability

- [x] Object model + `mình`/`gốc` + constructor.
- [x] Kế thừa class + interface/`triển khai`.
- [x] Tracing GC + cycle sweep.
- [x] GC/runtime stress + VM-state invariant + stack overflow/nested exception hardening.
- [x] Stack trace có source span/function/module.

### V++ 0.9 — Developer ecosystem

- [ ] Package resolver + semver + lockfile + cache.
- [ ] Standard library audit/completeness cho 1.0.
- [ ] Test framework nâng cấp.
- [ ] Formatter/linter/LSP/VS Code integration hoàn thiện.
- [ ] Project templates + sample project thực tế.

### V++ 1.0 RC

- [ ] Freeze syntax và semantics.
- [ ] Freeze bytecode compatibility/versioning policy.
- [ ] Freeze package manifest/lockfile format.
- [ ] Freeze public CLI contract.
- [ ] Fuzz + stress + sanitizer/leak suite xanh.
- [ ] Release artifact và install smoke test xanh trên Windows/macOS/Linux.
- [ ] Chạy thành công sample project thực tế bằng artifact release, không dùng build tree.
- [ ] Documentation + migration notes + changelog hoàn chỉnh.

## Release gate 1.0

Không phát hành 1.0 nếu còn lỗi làm VM corrupt state, crash trên input hợp lệ, package install
không reproducible, semantics chưa có contract hoặc artifact release không qua smoke test trên
ba hệ điều hành mục tiêu.

Ước lượng hiện tại: còn khoảng **25–30% khối lượng tới 1.0**, nhưng phần còn lại tập trung vào
hardening, kiểm thử và đóng contract nên có độ khó cao hơn việc thêm cú pháp/tính năng mới.
