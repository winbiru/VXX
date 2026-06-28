# Kiến trúc tổng quan — V++ (nhánh `developer`)

Mục đích: tài liệu hóa cấu trúc dự án hiện tại (theo nhánh `developer`), quy ước đặt file/namespace, luồng build, và các bước đề xuất để dự án có cấu trúc giống hơn với mô hình module/assembly như Java/C#.

Ghi chú: mình soạn theo tình trạng hiện tại của nhánh `developer` (src/, include/, CMakeLists.txt) và các từ khóa/tên module tìm được trong repo. Tài liệu này nhằm làm đường dẫn cho refactor tiếp theo (di chuyển headers, tách CMake per-module, mapping namespace ↔ thư mục, v.v.).

---

## 1. Tổng quan modules chính
- frontend: phân tích cú pháp (lexer, parser), chuyển sang AST/bytecode.
    - source: src/frontend
    - public headers: include/vietvm/frontend/...
- compiler: các bước biên dịch (AST → bytecode / tối ưu).
    - source: src/compiler
    - public headers: include/vietvm/compiler/...
- vm: runtime, bytecode interpreter, instruction set.
    - source: src/vm
    - public headers: include/vietvm/vm/...
    - ví dụ: instruction.h, vm.h → nên đặt vào include/vietvm/vm/
- stdlib (tùy chọn): thư viện chuẩn, utils.
    - source: src/stdlib
    - public headers: include/vietvm/stdlib/...
- cli: ứng dụng dòng lệnh/entrypoint.
    - source: src/cli
    - ít khi cần public headers; nếu có, include/vietvm/cli/...
- helpers / common: helper functions, utilities.
    - source: src/helpers
    - public headers: include/vietvm/common/...

---

## 2. Cấu trúc thư mục đề xuất (chuẩn hoá)
Root:
- CMakeLists.txt
- README.md
- architechture.md (this file)
- docs/
- include/
    - vietvm/
        - frontend/
        - compiler/
        - vm/
        - stdlib/
        - common/
- src/
    - frontend/
    - compiler/
    - vm/
    - stdlib/
    - cli/
    - helpers/
    - tests/
- tests/  (tách ngoài nếu muốn)
- src/frontend/grammar.bnf (hoặc V++.g4 nếu dùng ANTLR)

Lý do: tương tự Java/C# là có "root namespace" (vietvm) và thư mục con tương ứng module; public headers nằm dưới `include/vietvm/...` để dễ include như `#include <vietvm/vm/vm.h>`.

---

## 3. Quy ước tên target & namespace
- CMake targets: dùng tên target theo namespace kiểu `vietvm::frontend`, `vietvm::compiler`, `vietvm::vm`.
    - Ví dụ: add_library(vietvm::vm STATIC ...)
    - Lợi ích: rõ ràng khi target_link_libraries(app PRIVATE vietvm::vm)
- Namespace trong code: `namespace vietvm::vm { ... }`, `namespace vietvm::frontend { ... }`
- Header guards / pragma once: theo đường dẫn, ví dụ `V++_VM_VM_H` hoặc `V++_VM_VM_HPP`.
- Include style: `#include <vietvm/vm/vm.h>`

---

## 4. Build system hiện tại & đề xuất
Hiện tại: CMakeLists.txt gốc thu thập sources bằng GLOB_RECURSE và tạo các target vietvm-frontend, vietvm-compiler, vietvm-vm, vietvm-stdlib, vietvm-cli. Public include dir là `${CMAKE_SOURCE_DIR}/include`.

Đề xuất:
- Giữ CMake gốc làm top-level, nhưng tách `src/<module>/CMakeLists.txt` cho từng module; trong top-level gọi add_subdirectory(src/<module>).
- Trong mỗi CMakeLists module, dùng:
    - add_library(vietvm::vm STATIC ${SOURCES})
    - target_include_directories(vietvm::vm PUBLIC ${V++_INCLUDE_DIR})
    - target_compile_features(... PUBLIC cxx_std_17)
- Tránh GLOB cho production — liệt kê nguồn tường minh (giúp CI detect changes).
- Thiết lập export và cài đặt (install) nếu cần.

---

## 5. Public API vs internal headers
- Public headers: tất cả header dùng bởi các module khác hoặc người dùng library phải nằm dưới `include/vietvm/<module>/`.
- Internal/private headers: đặt trong `src/<module>/internal/` hoặc cùng `src/<module>/` và không đưa vào include path để tránh leak API.
- Ví dụ chuyển:
    - `include/vm.h` -> `include/vietvm/vm/vm.h`
    - `include/instruction.h` -> `include/vietvm/vm/instruction.h`
    - `include/keywords.h` -> `include/vietvm/frontend/keywords.h` (hoặc compiler nếu phù hợp)

---

## 6. Lexer / Parser / Grammar
- Đã có bản thô grammar.bnf (mình soạn dựa trên include/keywords.h). Đặt file grammar ở `src/frontend/grammar.bnf`.
- Gợi ý: chọn parser-generator:
    - Nếu ANTLR: tạo `src/frontend/V++.g4` (parser + lexer).
    - Nếu flex/bison: tạo `src/frontend/lexer.l` + `src/frontend/parser.y`.
- Normalize keywords: quyết định dùng dạng có dấu hay không; lexer nên map cả hai biến thể (`"nếu"` và `"neu"`) về một token IF.
- Multi-word keywords (ví dụ "trường hợp", "mặc định", "nếu không") cần lexer xử lý là single token (ghi nhận cụm) hoặc grammar phải chấp nhận chuỗi token.

---

## 7. Tests & CI
- Thư mục tests nên mirror cấu trúc module: `tests/frontend/*`, `tests/vm/*`, ...
- Thêm workflow GitHub Actions:
    - Build matrix (linux / windows / macos) nếu cần
    - Steps: checkout, configure cmake, build, run tests (ctest)
    - Lint: clang-tidy, clang-format
- Unit tests: dùng GoogleTest (gtest) hoặc framework tương tự; link test target với module targets.

---

## 8. Checklist refactor & chuyển đổi (hành động cụ thể)
1. Tạo thư mục `include/vietvm/` và các subfolders: vm, frontend, compiler, stdlib, common.
2. Di chuyển:
    - include/vm.h -> include/vietvm/vm/vm.h
    - include/instruction.h -> include/vietvm/vm/instruction.h
    - include/keywords.h -> include/vietvm/frontend/keywords.h (hoặc compiler)
3. Cập nhật tất cả #include trong src/ để dùng `<vietvm/...>` style.
4. Thêm namespace trong files nếu chưa có: `namespace vietvm::<module> { ... }`
5. Tách CMakeLists.txt: mỗi module có CMakeLists con và export target `vietvm::<module>`.
6. Di chuyển grammar.bnf -> src/frontend/grammar.bnf; nếu dùng ANTLR, tạo `V++.g4`.
7. Viết CI workflow để build & test trên nhánh developer.

---

## 9. Ví dụ include & code snippet
- Trước:
    - #include "vm.h"
- Sau:
    - #include <vietvm/vm/vm.h>

- Namespace:
```cpp
namespace vietvm::vm {
    class VM { ... };
}
```

---

## 10. Ghi chú về từ khóa tiếng Việt vs ASCII
- Quyết định chuẩn hoá: gợi ý là chấp nhận cả hai trong lexer nhưng map về cùng token. Ví dụ:
    - "nếu" và "neu" => IF
    - "hàm" và "ham" => FUNC
- Tài liệu lexer nên nêu rõ mapping này để contributors biết viết test.

---
