# VietVM — Kế hoạch cập nhật & Tóm tắt nhanh

Tài liệu này tóm tắt cấu trúc chính của project VietVM và đưa ra các hướng cập nhật/đổi mới cho tương lai. Nội dung bằng tiếng Việt, mục tiêu để dán trực tiếp vào repository.

## Mục đích
- Giúp contributor mới hiểu nhanh các modules chính và luồng compile → bytecode → VM.
- Liệt kê các vấn đề hiện tại, cơ hội cải tiến.
- Trình bày bộ kế hoạch theo 3 timeframe: ngắn hạn, trung hạn, dài hạn.

Xem chi tiết kế hoạch trong thư mục `plans/`:
- `plans/short-term.md` — Ngắn hạn (1–2 tuần)
- `plans/medium-term.md` — Trung hạn (1–3 tháng)
- `plans/long-term.md` — Dài hạn (3–12+ tháng)

---

## Tổng quan cấu trúc project (đường dẫn tham khảo)
- CLI / entrypoint:
  - `src/cli/main.cpp`
- Frontend / Lexer:
  - `include/frontend/lexer.h`, `src/frontend/lexer.cpp`
  - `include/frontend/keywords.h`, `src/frontend/keywords.cpp`
- Compiler / codegen:
  - `include/compiler/*.h`, `src/compiler/*.cpp` (ví dụ `compileStatement.cpp`, `compileFunction.cpp`, `compileRegistry.cpp`)
- Helpers / utilities:
  - `include/common/*`, `src/helpers/*` (ví dụ `storeString.cpp`, `symbolTable.cpp`)
- VM / runtime:
  - `include/vm/instruction.h`, `include/vm/vm.h`, `src/vm/vm.cpp`
- Docs & tests:
  - `docs/` (architecture, bytecode, grammar)
  - `src/tests/` (tập chương trình `.vi` dùng cho testing/manual)
- Build:
  - `CMakeLists.txt`, `Makefile`, `cmake-build-debug/` (hiện có artefact trong repo)

## Kiến trúc & luồng xử lý (tóm tắt)
1. Frontend (lexer) đọc source và sinh tokens (`src/frontend/lexer.cpp`).
2. Compiler (`src/compiler/compiler.cpp` và các `compile*.cpp`) chuyển tokens thành `std::vector<Instruction>` (bytecode) và ghi vào các cấu trúc như `StringPool` / `hamBytecodeMap`.
3. `StringPool` / `storeString.cpp` quản lý chuỗi/hàm và ánh xạ tên ↔ id.
4. `VM` (`src/vm/vm.cpp`) nhận bytecode + string pool và thực thi bằng một vòng lặp opcode (implemented as `VM::run()`).
5. CLI (`src/cli/main.cpp`) kết hợp tất cả: compile nguồn, khởi tạo VM, chạy, và in kết quả.

## Vấn đề chính & cơ hội cải tiến (tóm tắt)
- Thiết kế: `VM::run()` khá monolithic — khó bảo trì và test.
- Trạng thái toàn cục: `StringPool`/hamMap có state global; cần làm rõ lifecycle để tránh leak khi compile nhiều lần.
- Testing: thiếu unit tests cho compiler/VM; hiện chỉ có chương trình ví dụ (`src/tests/*.vi`).
- CI: repository có cấu hình tối thiểu; khuyến nghị thiết lập GitHub Actions đầy đủ (build matrix, unit tests, code coverage).
- Docs: `docs/*.md` tốt nhưng có thể không khớp hoàn toàn với hiện trạng code (cần cập nhật bytecode spec & instruction semantics).
- Build artefacts: `cmake-build-debug/` hiện ở repo — nên loại bỏ khỏi VCS hoặc di chuyển vào `.gitignore`.

## Quickstart (local)
1. Build (CMake):

```bash
mkdir -p cmake-build-debug && cd cmake-build-debug
cmake ..
make -j
```

2. Chạy CLI với file thử nghiệm:

```bash
./cmake-build-debug/bin/vietvm-cli src/tests/kiem_tra_ham.vi
```

Lưu ý: các lệnh trên giả định bạn đang làm việc trên macOS / Linux với CMake & make đã cài.

## Đề xuất cấp cao
- Ngắn hạn: ổn định build, thêm unit tests cơ bản, cập nhật README và xoá artefacts build khỏi repo.
- Trung hạn: tách `VM::run()` thành các handler, viết unit tests cho opcode handlers, thêm CI (matrix), coverage, linter.
- Dài hạn: chuẩn hoá bytecode (spec), viết assembler/disassembler + round-trip tests, profiling & tối ưu hoá VM, public API để nhúng VietVM.

Xem chi tiết trong `plans/`.

---

Những file kế hoạch đã được tạo trong `plans/`. Nếu bạn muốn, tôi có thể tiếp tục:
- tạo skeleton unit tests (`tests/unit/`),
- thêm pipeline GitHub Actions mẫu (`.github/workflows/ci.yml`),
- hoặc tạo PR với các sửa đổi ban đầu (vd. xóa `cmake-build-debug/` khỏi VCS).

Ghi chú: nội dung kế hoạch chi tiết nằm trong các file `plans/*.md` được tạo cùng lúc.

