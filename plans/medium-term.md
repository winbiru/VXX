# Kế hoạch trung hạn (1–3 tháng)

> Cập nhật: 22/09/2026
> Mục tiêu là giảm coupling của compiler/runtime và tăng độ tin cậy. Baseline hiện
> có regression CTest, sanitizer trên Ubuntu và vài C++ unit test, nhưng chưa phải
> coverage đầy đủ.

## Nền tảng đã có

- [x] CMake đã tách target theo core, frontend, bytecode, compiler, runtime, tooling
  và CLI; dependency graph có chiều rõ ràng thay vì target helpers tổng quát.
- [x] Hồi quy tích hợp đã được đăng ký với CTest trên Unix và Windows.
- [x] Job Ubuntu có AddressSanitizer và UndefinedBehaviorSanitizer.
- [x] Có baseline test cho compiler support và một nhóm opcode VM.

## Việc còn lại

1. Tách VM::run() theo opcode handler

   - [x] Chia dispatch hiện tại thành handler nhỏ, bảo toàn semantics và call frame.
     `VM::run()` hiện chỉ giữ lifecycle/GC và routing opcode; logic call, value,
     arithmetic, literal, index, variable/call-frame, switch/block, loop-control,
     exception và branch đã nằm trong các handler riêng.
   - [x] Đưa state cần thiết vào API nội bộ có thể dựng trong test. `VMRuntimeFixture`
     hiện cho phép dựng stack/PC/variables/call frame/control state, gọi handler trực
     tiếp và cấu hình output sink. C++ unit consumer đã bị xóa có chủ đích ở `473a8e4`;
     regression hiện hành đi qua corpus `.vi`/public CLI.
   - [x] Giữ test tích hợp trước/sau mỗi nhánh refactor. Baseline trước refactor và
     hiện tại đều giữ suite xanh; baseline hiện tại đạt 78/78 regression.

2. Mở rộng test opcode và compiler

   - [x] Chuyển VM opcode smoke test thành ma trận test cho arithmetic, stack,
     branch, call/return, native call, lỗi runtime và boundary value. Ma trận hiện
     khóa arithmetic/modulo, logic/comparison boundary, boolean/null/unary stack,
     assignment/increment/decrement, list/index, branch, direct/indirect/native call,
     default parameter, switch/default, throw/catch/uncaught và các đường lỗi chính.
   - [x] Thêm regression cho lexer/compiler khi phát hiện lỗi thay vì chỉ sửa output
     của fixture.
   - [x] Xác định test discovery rõ ràng: C++ unit target riêng, regression V++ riêng,
     fixture network riêng.

3. Hoàn thiện CI chất lượng

   - [x] Thêm coverage report có ngưỡng 45% line coverage và loại trừ system header,
     test/fixture, example/template và build-generated code qua LCOV.
   - [x] Thêm `clang-tidy` với `.clang-tidy` được version hoá và job quality trên Ubuntu.
   - [x] Thêm macOS regression CI thường trực cho build + toàn bộ CTest, song song
     với Ubuntu và Windows; workflow release vẫn chịu trách nhiệm đóng gói artifact.

4. Giảm state toàn cục trong compiler

   - [x] Dùng `CompilationContext` làm owner cho StringPool, function maps, import set
     và class/access state theo từng top-level compilation. `StringPool`/`hamMap` còn
     là compatibility facade tới context active để recursive import dùng chung session;
     mọi execution path của CLI đọc runtime snapshot trực tiếp từ context.
   - [x] Tách ranh giới emit block/function/statement theo dữ liệu vào-ra cụ thể.
     Direct emitter hiện có `emitBlock(...)`, `emitStatement(...)` và
     `emitFunctionBody(...)`; function body trả về bytecode riêng trước khi được đăng ký,
     còn statement/block nhận output buffer tường minh thay vì dồn toàn bộ logic vào một
     dispatch duy nhất.
   - [x] Có test biên dịch liên tiếp và song song bằng các `CompilationContext` độc lập.
     `CompilationContext.importResolutionBase` tách lookup import khỏi process cwd và
     regression song song khóa hai module cùng tên ở hai thư mục độc lập. CLI chỉ đổi
     cwd trong pha `VM::run()` để giữ semantics file/database tương đối ở runtime.

5. Benchmark và profiling

   - [x] Tạo benchmark lặp lại được cho dispatch opcode, lexer/compiler và native
     HTTP parsing path bằng target `vpp-benchmark-baseline`.
   - [x] Ghi baseline trước mọi tối ưu JIT/VM qua output timing của benchmark và chạy
     trong CI theo dạng quan sát không đặt performance threshold.

   Harness P0 v2 hiện đã tách `vm_verify`, `vm_dispatch_preverified`, `vm_end_to_end`,
   compiler stages, package/module resolution, JSON extraction, native HTTP và GC collection.
   `scripts/quality/benchmark-regression.py` đã có alternating baseline/candidate, 3 warm-up,
   median/p95/MAD, noise check và regression threshold. Comparator yêu cầu tối thiểu 10 sample
   cho median và 40 sample cho GC p95 gate sau khi self-comparison 10 sample phơi ra tail
   outlier có thể tạo false regression; self-comparison 40 sample sau hardening đã PASS.
   Công việc P0 trước RC vẫn được theo dõi riêng tại
   [mục 0 của roadmap 1.0](roadmap-1.0.md#0-p0--runtime-performance-và-import-contract-trước-rc):
   còn phải đo baseline/candidate thực trên cùng máy, bổ sung GC allocation/RSS telemetry và
   nối comparator vào CI. Checklist baseline trung hạn hoàn tất không đồng nghĩa các release
   blocker evidence này đã hoàn tất.

## Rủi ro và dependency

- Tách VM có thể làm thay đổi thứ tự side effect; cần expected output và test call
  frame trước khi tối ưu.
- Sanitizer là tín hiệu lỗi tốt nhưng không thay thế coverage hoặc review ownership.
- AST/semantic/type system là dependency kiến trúc cho refactor compiler lớn hơn; cần
  chốt chính sách kiểu dữ liệu trước khi đưa Typed IR vào roadmap thực thi.
