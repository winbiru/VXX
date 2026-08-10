# Kế hoạch trung hạn (1–3 tháng)

> Cập nhật: 09/08/2026
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

   - [ ] Chia dispatch hiện tại thành handler nhỏ, bảo toàn semantics và call frame.
   - [ ] Đưa state cần thiết vào API nội bộ có thể dựng trong test; không dựa vào
     stdout/global state để kiểm thử từng handler.
   - [ ] Giữ test tích hợp trước/sau mỗi nhánh refactor.

2. Mở rộng test opcode và compiler

   - [ ] Chuyển VM opcode smoke test thành ma trận test cho arithmetic, stack,
     branch, call/return, native call, lỗi runtime và boundary value.
   - [ ] Thêm regression cho lexer/compiler khi phát hiện lỗi thay vì chỉ sửa output
     của fixture.
   - [ ] Xác định test discovery rõ ràng: C++ unit target riêng, regression V++ riêng,
     fixture network riêng.

3. Hoàn thiện CI chất lượng

   - [ ] Thêm coverage report có ngưỡng và cách loại trừ code generated/fixture.
   - [ ] Thêm C++ static linter (ví dụ clang-tidy) với config được version hoá.
   - [ ] Cân nhắc macOS regression CI nếu native runtime có nhánh platform-specific;
     workflow release hiện mới xác nhận build/package macOS.

4. Giảm state toàn cục trong compiler

   - [ ] Thiết kế CompilationContext/BytecodeProgram để thay StringPool và registry
     mutable toàn cục dần theo context per-compilation.
   - [ ] Tách interface compile block/function/statement theo dữ liệu vào-ra cụ thể
     thay vì chia module chỉ theo file.
   - [ ] Có test biên dịch liên tiếp/đồng thời trước khi tuyên bố compiler re-entrant.

5. Benchmark và profiling

   - [ ] Tạo benchmark lặp lại được cho dispatch opcode, lexer/compiler và native
     HTTP path.
   - [ ] Ghi baseline trước mọi tối ưu JIT/VM, đưa kết quả vào tài liệu hoặc CI
     không-chặn.

## Rủi ro và dependency

- Tách VM có thể làm thay đổi thứ tự side effect; cần expected output và test call
  frame trước khi tối ưu.
- Sanitizer là tín hiệu lỗi tốt nhưng không thay thế coverage hoặc review ownership.
- AST/semantic/type system là dependency kiến trúc cho refactor compiler lớn hơn; cần
  chốt chính sách kiểu dữ liệu trước khi đưa Typed IR vào roadmap thực thi.
