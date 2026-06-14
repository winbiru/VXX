# ✅ VietVM — Checklist Phát Triển Ngôn Ngữ

> Cập nhật lần cuối: 14/06/2026  
> Trạng thái: `✅ Hoàn thành` · `🚧 Đang làm` · `⬜ Chưa làm` · `❌ Lỗi / Cần sửa`

---

## 1. 🔤 Từ Khoá & Cú Pháp (Syntax)

### 1.1 Từ khoá cơ bản
| Từ khoá | Ý nghĩa | Trạng thái |
|---------|---------|-----------|
| `in` | In ra màn hình (print) | ✅ |
| `nếu` | Điều kiện if | ✅ |
| `hoặc` | Else | ✅ |
| `nếu không` | Else if | ✅ |
| `lặp` | Vòng lặp (for / while) | ✅ |
| `hàm` | Định nghĩa hàm | ✅ |
| `trả về` | Trả về giá trị (return) | ✅ |
| `nhập` | Import module | ✅ |
| `chọn` | Switch | ✅ |
| `ca` | Case | ✅ |
| `mặc định` | Default (trong switch) | ✅ |
| `thoát` | Break | ✅ |
| `bỏ qua` | Continue | ✅ |
| `khởi tạo` | Khai báo biến | ✅ |

### 1.2 Bắt buộc dấu tiếng Việt
| Yêu cầu | Trạng thái |
|---------|-----------|
| Từ khoá PHẢI có dấu (ví dụ: `trả về`, không phải `tra ve`) | ✅ |
| Lexer phát hiện và báo lỗi rõ ràng khi thiếu dấu | ✅ |

---

## 2. 🧮 Toán Tử (Operators)

| Toán tử | Mô tả | Trạng thái |
|---------|-------|-----------|
| `+` | Cộng số / nối chuỗi | ✅ |
| `-` | Trừ | ✅ |
| `*` | Nhân | ✅ |
| `/` | Chia | ✅ |
| `%` | Chia lấy dư | ✅ |
| `==` | So sánh bằng | ✅ |
| `!=` | Khác bằng | ✅ |
| `>` | Lớn hơn | ✅ |
| `<` | Nhỏ hơn | ✅ |
| `>=` | Lớn hơn hoặc bằng | ✅ |
| `<=` | Nhỏ hơn hoặc bằng | ✅ |
| `&&` / `và` | Logic AND | ✅ |
| `\|\|` / `hoặc` | Logic OR | ✅ |
| `!` | Phủ định logic | ✅ |
| `=` | Gán giá trị | ✅ |
| `++` | Tăng một đơn vị | ✅ |
| `--` | Giảm một đơn vị | ✅ |
| `+=`, `-=`, `*=`, `/=`, `%=` | Gán kết hợp | ✅ |

---

## 3. 📦 Kiểu Dữ Liệu (Data Types)

| Kiểu | Ví dụ | Trạng thái |
|------|-------|-----------|
| Số nguyên (int) | `42`, `-7` | ✅ |
| Chuỗi (string) | `"xin chào"` | ✅ |
| Mảng (array) | `[1, 2, 3]` | ✅ |
| Mảng đa chiều | `[[1,2],[3,4]]` | ✅ |
| Boolean (`đúng`/`sai`) | `đúng`, `sai` | ✅ |
| Số thực (float/double) | `3.14` | ⬜ |
| Kiểu null / rỗng | `rỗng` | ⬜ |
| Từ điển / Map | `{"a": 1}` | ⬜ |

---

## 4. 🔧 Cấu Trúc Điều Khiển (Control Flow)

| Tính năng | Ví dụ | Trạng thái |
|-----------|-------|-----------|
| `nếu ... hoặc ...` | if / else | ✅ |
| `nếu không ... hoặc ...` | else if / else | ✅ |
| Điều kiện lồng nhiều cấp | if trong if | ✅ |
| Vòng lặp `for` kiểu C | `lặp(i=0; i<10; i++)` | ✅ |
| Vòng lặp `while` | `lặp(điều kiện)` | ✅ |
| `thoát` (break) | Thoát vòng lặp/switch | ✅ |
| `bỏ qua` (continue) | Bỏ qua lần lặp hiện tại | ✅ |
| `chọn ... ca ...` | switch / case | ✅ |
| `mặc định` | default trong switch | ✅ |

---

## 5. 🏗️ Hàm (Functions)

| Tính năng | Ví dụ | Trạng thái |
|-----------|-------|-----------|
| Định nghĩa hàm không tham số | `hàm xinChao() { ... }` | ✅ |
| Định nghĩa hàm có tham số | `hàm cong(a, b) { ... }` | ✅ |
| Hàm nhiều tham số (≥4) | `hàm f(a,b,c,d) { ... }` | ✅ |
| `trả về` giá trị | `trả về a + b;` | ✅ |
| `trả về` không có giá trị | `trả về;` (mặc định = 0) | ✅ |
| Gọi hàm với đối số | `cong(2, 3)` | ✅ |
| Đệ quy (recursion) | Fibonacci, giai thừa | ✅ |
| Hàm lồng nhau (nested calls) | `f(g(x))` | ✅ |
| Hàm ẩn danh / lambda | `hàm(x) { trả về x * 2; }` | ⬜ |
| Hàm bậc cao (higher-order) | Truyền hàm làm tham số | ⬜ |
| Giá trị tham số mặc định | `hàm f(x = 0) { ... }` | ⬜ |
| Biến cục bộ tách biệt toàn cục | Scope isolation | ✅ |

---

## 6. 📁 Module & Import

| Tính năng | Trạng thái |
|-----------|-----------|
| `nhập "file.vi"` — import file khác | ✅ |
| Phát hiện import vòng (circular import) | ✅ |
| Phân giải đường dẫn tương đối | ✅ |
| Namespace / tên module | ⬜ |
| Thư viện chuẩn tiếng Việt (stdlib) | ⬜ |

---

## 7. ⚠️ Xử Lý Lỗi (Error Handling)

| Tính năng | Trạng thái |
|-----------|-----------|
| Báo lỗi khi chia cho 0 | ✅ |
| Báo lỗi khi thiếu dấu tiếng Việt | ✅ |
| Báo lỗi khi import file không tồn tại | ✅ |
| Báo lỗi khi hàm không tồn tại | ✅ |
| Báo lỗi khi stack underflow | ✅ |
| Báo lỗi khi so sánh 2 kiểu khác nhau | ✅ |
| Báo lỗi vị trí (dòng:cột) | ⬜ |
| Khối `thử ... bắt lỗi` (try/catch) | ⬜ |
| `ném lỗi` (throw exception) | ⬜ |
| Stack trace khi crash | ⬜ |

---

## 8. 🖥️ VM & Bytecode

| Tính năng | Trạng thái |
|-----------|-----------|
| Thực thi bytecode stack-based | ✅ |
| String pool (tránh trùng lặp chuỗi) | ✅ |
| Call frame cho hàm | ✅ |
| Biến cục bộ trong frame (localsVec) | ✅ |
| Biến toàn cục | ✅ |
| Chia sẻ biến toàn cục giữa hàm con và mẹ | ✅ |
| Truyền tham số qua `OP_PARAM` | ✅ |
| `OP_TRA_VE` trả về giá trị từ hàm | ✅ |
| `OP_BO_QUA` (continue) | ✅ |
| `OP_TRU_MOT` (--) | ✅ |
| Kế thừa `functionTableByNameIndex` cho đệ quy | ✅ |
| Tối ưu hoá bytecode (peephole) | ⬜ |
| Garbage Collection | ⬜ |
| JIT Compilation | ⬜ |

---

## 9. 🧩 Compiler

| Tính năng | Trạng thái |
|-----------|-----------|
| Tokenizer / Lexer | ✅ |
| Parser → Bytecode (single-pass) | ✅ |
| Biên dịch biểu thức số học | ✅ |
| Biên dịch chuỗi và nối chuỗi | ✅ |
| Biên dịch điều kiện `nếu/hoặc` | ✅ |
| Biên dịch vòng lặp | ✅ |
| Biên dịch switch/case | ✅ |
| Biên dịch hàm và gọi hàm | ✅ |
| Biên dịch `trả về` | ✅ |
| Biên dịch `nhập` (import) | ✅ |
| Biên dịch mảng | ✅ |
| Biên dịch `bỏ qua` (continue) | ✅ |
| Biên dịch `--`, `+=`, `-=`, `*=`, `/=`, `%=` | ✅ |
| Biên dịch `đúng`/`sai` (boolean literals) | ✅ |
| Symbol table | ✅ |
| Fix `isNumber("-")` bug | ✅ |
| Fix `convertToPostfix` nested call argc | ✅ |
| Kiểm tra kiểu tĩnh (type checking) | ⬜ |
| AST (cây cú pháp trừu tượng) | ⬜ |
| Phân tích ngữ nghĩa (semantic analysis) | ⬜ |

---

## 10. 🧪 Tests

| Bài test | Mô tả | Trạng thái |
|---------|-------|-----------|
| `kiem_tra_so_nguyen.vi` | Số nguyên và phép tính | ✅ |
| `kiem_tra_so_chan_1-20.vi` | Lọc số chẵn 1–20 | ✅ |
| `kiem_tra_so_le_chia_het_cho_5.vi` | Số lẻ chia hết cho 5 | ✅ |
| `kiem_tra_so_chia_het_cho_3_va_4.vi` | Chia hết 3 và 4 | ✅ |
| `kiem_tra_dieu_kien_phu_dinh.vi` | Điều kiện phủ định | ✅ |
| `kiem_tra_dieu_kien_long_nhieu_cap.vi` | Điều kiện lồng nhau | ✅ |
| `kiem_tra_noi_chuoi.vi` | Nối chuỗi | ✅ |
| `kiem_tra_mang_3_chieu.vi` | Mảng 3 chiều | ✅ |
| `kiem_tra_chon_ca.vi` | Switch/case | ✅ |
| `kiem_tra_ham_tham_so.vi` | Hàm có tham số | ✅ |
| `kiem_tra_ham_4_tham_so.vi` | Hàm 4 tham số | ✅ |
| `kiem_tra_tra_ve.vi` | `trả về` có/không có giá trị | ✅ |
| `import_main.vi` | Import module | ✅ |
| `program.vi` | Chương trình tổng hợp | ✅ |
| `kiem_tra_bo_qua.vi` | `bỏ qua` (continue) | ✅ |
| `kiem_tra_toan_tu_moi.vi` | `--`, `+=`, `-=`, `*=`, `/=`, `%=` | ✅ |
| `kiem_tra_boolean.vi` | `đúng`/`sai` boolean literals | ✅ |
| `kiem_tra_de_quy.vi` | Đệ quy: Fibonacci, giai thừa | ✅ |
| Test exception handling | try/catch | ⬜ |
| Test float | Số thực | ⬜ |
| Unit test cho StringPool | Thêm/lấy/xóa | ⬜ |
| Unit test cho symbolTable | Scope isolation | ⬜ |

**Tỷ lệ test hiện tại: 18/18 PASS ✅**

---

## 11. 🛠️ Công Cụ & Hạ Tầng (Tooling)

| Tính năng | Trạng thái |
|-----------|-----------|
| CLI (`vietvm-cli`) | ✅ |
| Build với CMake | ✅ |
| Script chạy test (`run_tests.sh`) | ✅ |
| Không có duplicate library warnings | ✅ |
| `.gitignore` cho build artefacts | ✅ |
| CI/CD (GitHub Actions) | ⬜ |
| Disassembler (xem bytecode) | ⬜ |
| REPL (interactive shell) | ⬜ |
| Language Server Protocol (LSP) | ⬜ |
| Syntax highlighting (VSCode/Vim) | ⬜ |
| Formatter / linter | ⬜ |
| Package manager | ⬜ |

---

## 12. 📚 Tài Liệu (Documentation)

| Tài liệu | Trạng thái |
|---------|-----------|
| `README.md` — Giới thiệu & build guide | ✅ |
| `README-updates.md` — Changelog | ✅ |
| `docs/architecture.md` — Kiến trúc | ✅ |
| `docs/bytecode.md` — Mô tả bytecode | ✅ |
| `docs/grammar.bnf` — Ngữ pháp BNF | ✅ |
| `docs/language-comparison.md` — So sánh với ngôn ngữ khác | ✅ |
| `CHECKLIST.md` — File này | ✅ |
| `CONTRIBUTING.md` — Hướng dẫn đóng góp | ⬜ |
| Tutorial / ví dụ từng bước | ⬜ |
| API reference cho embedding | ⬜ |

---

## 13. 🚀 Lộ Trình Phát Triển (Roadmap)

### Ngắn hạn (1–2 tuần) — **Đã hoàn thành phần lớn**
- [x] Thêm `bỏ qua` (continue) trong vòng lặp
- [x] Thêm toán tử `--` và `+=`, `-=`, `*=`, `/=`, `%=`
- [x] Thêm kiểu boolean (`đúng`/`sai`)
- [x] Hỗ trợ đệ quy (recursion)
- [x] Fix bug `isNumber("-")` và nested function calls
- [x] Tạo `.gitignore` cho build artefacts
- [ ] Báo lỗi có số dòng và cột
- [ ] CI cơ bản với GitHub Actions

### Trung hạn (1–3 tháng)
- [ ] Hỗ trợ số thực (float)
- [ ] Xử lý ngoại lệ (`thử`/`bắt lỗi`/`ném`)
- [ ] Tách `VM::run()` thành các handler nhỏ
- [ ] Unit test cho từng opcode handler
- [ ] REPL (gõ lệnh trực tiếp)
- [ ] Disassembler hiển thị bytecode

### Dài hạn (3–12 tháng)
- [ ] Kiểu từ điển / Map
- [ ] Hàm bậc cao (higher-order functions)
- [ ] Garbage Collection
- [ ] Thư viện chuẩn tiếng Việt (stdlib)
- [ ] Namespace / module có tên
- [ ] Language Server Protocol (LSP)
- [ ] Syntax highlighting cho VSCode
- [ ] JIT Compilation (tuỳ chọn)
- [ ] Embeddable C API

---

## 📊 Tổng Kết

| Hạng mục | Hoàn thành | Tổng |
|---------|-----------|------|
| Từ khoá & cú pháp | 14 | 14 |
| Toán tử | 16 | 16 |
| Kiểu dữ liệu | 5 | 8 |
| Điều khiển luồng | 9 | 9 |
| Hàm | 10 | 12 |
| Module & import | 3 | 5 |
| Xử lý lỗi | 6 | 10 |
| VM & Bytecode | 13 | 14 |
| Compiler | 17 | 19 |
| Tests | 18 | 22 |
| Công cụ | 6 | 13 |
| Tài liệu | 7 | 10 |

> **Tổng cộng: ~124/152 (~82%) tính năng cốt lõi đã hoàn thành.**
