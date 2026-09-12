# V++ — Trạng thái repo và hướng cập nhật

> Cập nhật: 09/08/2026
> Tài liệu này mô tả những gì đang có trong source tree và các hạng mục còn lại. Sự
> tồn tại của workflow không đồng nghĩa mọi lần chạy CI đều đã thành công; trạng thái
> từng lần chạy cần xem trên GitHub Actions.

## Mục đích

- Giúp contributor định vị đúng module, test và script hiện hành.
- Phân biệt phần đã có trong repo với roadmap còn dang dở.
- Dẫn đến kế hoạch ngắn, trung và dài hạn trong thư mục plans/.

## Những nền tảng đã có

| Hạng mục | Trạng thái hiện tại | Vị trí |
| --- | --- | --- |
| CI hồi quy | Đã có workflow Ubuntu chạy CTest đầy đủ và ASan/UBSan; có job Windows chạy CTest bản Release. | .github/workflows/c-cpp.yml |
| Đóng gói release | Đã có workflow tạo binary cho Ubuntu, macOS và Windows khi publish Release hoặc chạy thủ công. | .github/workflows/release-binaries.yml |
| Hướng dẫn đóng góp | Đã có hướng dẫn cơ bản cho contributor. | CONTRIBUTING.md |
| Hồi quy tích hợp | CTest gọi run_tests.sh trên Unix và scripts/windows/run-tests.ps1 trên Windows. Các chương trình V++ và output mong đợi nằm cạnh nhau. | run_tests.sh, scripts/windows/, src/tests/ |
| Unit test C++ nền tảng | Đã có target CTest cho StringPool, symbolTable/hamMap, canonical opcode/native constants và smoke test opcode VM. Đây là baseline, chưa phải coverage từng handler. | test/CMakeLists.txt, test/compiler_support_tests.cpp, test/opcode_and_native_constants_tests.cpp, test/vm_opcode_smoke_tests.cpp |
| Vệ sinh build | Các thư mục build phổ biến, output test và binary đã được ignore; không dùng build artefact làm source. | .gitignore |

## Cấu trúc source hiện hành

- CLI: src/cli/main.cpp.
- Core và frontend: src/core/, src/frontend/; header tương ứng ở src/include/common/ và
  src/include/frontend/.
- Compiler: src/compiler/; các thành phần hỗ trợ ở src/compiler/support/ (không còn
  nằm tại src/helpers/).
- Bytecode: src/bytecode/; header theo hướng module mới ở src/include/vpp/bytecode/ và
  header tương thích cũ vẫn ở src/include/vm/.
- Runtime và native adapter: src/runtime/ và src/runtime/native/ (không còn
  src/vm/).
- Tooling CLI: src/tooling/; các header theo namespace vpp đang được gom ở
  src/include/vpp/.
- Test: chương trình hồi quy V++ ở src/tests/*.vi, output ở src/tests/expected/;
  C++ unit test ở test/*.cpp. src/tests/.tmp/ chỉ là workspace tạm được tạo khi
  chạy test.
- Package/thư viện chuẩn, template và ví dụ: gói/, templates/ và examples/.

Các header trong src/include/vpp/ là hướng tổ chức API theo module; chúng chưa được
cam kết là C/C++ embedding API ổn định.

## Build và test cục bộ

Từ root của repository:

    cmake -S . -B cmake-build-debug
    cmake --build cmake-build-debug
    ctest --test-dir cmake-build-debug --output-on-failure --verbose --no-tests=error

Binary được CMake đặt trong cmake-build-debug/bin/. Có thể chạy riêng bộ hồi quy
Unix bằng cách đặt VPP_EXEC trỏ đến binary rồi gọi run_tests.sh. Trên Windows,
CTest tự gọi PowerShell 7 và scripts/windows/run-tests.ps1 khi pwsh có mặt.

## Việc còn lại theo thứ tự ưu tiên

1. Chốt chính sách kiểu dữ liệu (dynamic, static hoặc gradual) trước khi thiết kế
   AST, semantic analysis và Typed IR.
2. Tách VM::run() theo handler, rồi mở rộng test từ smoke test sang coverage từng
   opcode, đường lỗi và call frame.
3. Đồng bộ docs/bytecode.md với bytecode thực thi; chỉ version hoá/serialize khi
   contract opcode đã ổn định.
4. Thiết kế C API/C++ embedding API không phụ thuộc state compiler toàn cục.
5. Nâng MVP GC/JIT thành thiết kế có benchmark, profiling và kiểm thử hồi quy trước
   khi xem là runtime production.
6. Bổ sung coverage report, C++ linter và benchmark; chúng chưa có trong CI hiện tại.

Xem chi tiết và trạng thái từng nhóm ở:

- plans/short-term.md — việc có thể hoàn tất trong vòng 1–2 tuần.
- plans/medium-term.md — refactor và độ tin cậy trong 1–3 tháng.
- plans/long-term.md — nền tảng ngôn ngữ/runtime và phát hành dài hạn.
