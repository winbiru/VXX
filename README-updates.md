# V++ — Trạng thái repo và hướng cập nhật

> Cập nhật: 14/09/2026
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
| CI hồi quy | Workflow có Ubuntu full CTest + ASan/UBSan, Windows Release full CTest và macOS full CTest. | .github/workflows/c-cpp.yml |
| CI chất lượng | Ubuntu chạy clang-tidy, coverage gate 45% và benchmark baseline quan sát. | .github/workflows/c-cpp.yml, scripts/quality/run-coverage.sh |
| Đóng gói release | Đã có workflow tạo binary cho Ubuntu, macOS và Windows khi publish Release hoặc chạy thủ công. | .github/workflows/release-binaries.yml |
| Hướng dẫn đóng góp | Đã có hướng dẫn cơ bản cho contributor. | CONTRIBUTING.md |
| Hồi quy tích hợp | CTest gọi run_tests.sh trên Unix và scripts/windows/run-tests.ps1 trên Windows. Các chương trình V++ và output mong đợi nằm cạnh nhau. | run_tests.sh, scripts/windows/, src/tests/ |
| Test C++/tooling | CMake đăng ký compiler support, opcode/native constants, runtime value, VM opcode matrix, VM handler fixture, pipeline/module graph/recursive IR/direct codegen/parity, tooling và CLI dump checks. | test/CMakeLists.txt |
| VM handler test API | `VMRuntimeFixture` cho phép dựng stack/PC/variables/call frame/control state và gọi từng handler trực tiếp; output có sink riêng cho test. | src/include/vpp/runtime/vm_fixture.h, test/vm_handler_tests.cpp |
| Native stdlib | Runtime đã có native helpers cho chuyển kiểu/type, random, path/filesystem, environment/platform/sleep ngoài HTTP/JSON/file/config/database/collections/text. | src/runtime/native/, gói/chuẩn/ |
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
- Fixture test handler nội bộ: src/include/vpp/runtime/vm_fixture.h; không phải public
  embedding API.
- Tooling CLI: src/tooling/; các header theo namespace vpp đang được gom ở
  src/include/vpp/.
- Test: chương trình hồi quy V++ ở src/tests/*.vi, output ở src/tests/expected/;
  C++ unit test ở test/*.cpp. src/tests/.tmp/ chỉ là workspace tạm được tạo khi
  chạy test.
- Package/gói chuẩn, template và ví dụ: gói/, templates/ và examples/.

Các header trong src/include/vpp/ là hướng tổ chức API theo module; chúng chưa được
cam kết là C/C++ embedding API ổn định.

## Build và test cục bộ

Từ root của repository:

    cmake -S . -B build
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure --verbose --no-tests=error

Binary được CMake đặt trong build/bin/. Có thể chạy riêng bộ hồi quy
Unix bằng cách đặt VPP_EXEC trỏ đến binary rồi gọi run_tests.sh. Trên Windows,
CTest tự gọi PowerShell 7 và scripts/windows/run-tests.ps1 khi pwsh có mặt.

Baseline local ngày 14/09/2026: **16/16 CTest pass**, integration regression
**70/70**, direct-IR parity **79/79**.

## Việc còn lại theo thứ tự ưu tiên

1. Module Semantics Phase 1 và runtime lifecycle đã có: identity/export index, alias
   namespace, semantic `ImportedFunction`, runtime `ModuleTable` và initialization
   dependency-first đúng một lần. Object model theo contract hiện tại đã hoàn tất:
   `Class(args...)` qua `hàm khởi tạo(...)`, field động, bound method, `mình`/`gốc`,
   inheritance và private/protected method visibility ở cả semantic lẫn runtime. Interface
   compile-time dùng `giao diện`, hỗ trợ interface kế thừa nhiều interface và class
   `triển khai` nhiều interface trong khi vẫn chỉ có một superclass; semantic buộc class
   cung cấp đúng hợp đồng method công khai, kể cả khi implementation được kế thừa từ lớp cha.
2. Tracing GC object graph đã hoàn tất và chạy mặc định: heap per-VM, root qua
   stack/variables/call frame/class table, cycle sweep và bảo toàn caller root qua child VM.
   Lát cắt runtime tiếp theo là stack trace/debugger hook trên call frame + source span ổn định.
3. Tách package/bare-module lookup thành package resolver riêng trước khi thêm
   version/dependency/lockfile.
4. Chốt chính sách kiểu dữ liệu (dynamic, static hoặc gradual) trước khi thêm type
   checking/Typed IR.
5. Chốt ABI/versioning `.vbc`, rồi mới viết serializer/deserializer,
   assembler/disassembler và verifier tương ứng.
6. Thiết kế C API/C++ embedding API dựa trên lifecycle compiler/runtime đã tách state.

Xem chi tiết và trạng thái từng nhóm ở:

- plans/short-term.md — việc có thể hoàn tất trong vòng 1–2 tuần.
- plans/medium-term.md — refactor và độ tin cậy trong 1–3 tháng.
- plans/long-term.md — nền tảng ngôn ngữ/runtime và phát hành dài hạn.
