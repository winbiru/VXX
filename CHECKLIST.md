# ✅ V++ — Checklist Phát Triển Ngôn Ngữ

> Cập nhật lần cuối: 28/06/2026  
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

### 1.3 Lớp & Quyền truy cập (Public/Private/Protected tương đương)
| Công việc | Trạng thái |
|-----------|-----------|
| Thêm cú pháp khai báo lớp: `lớp [công khai|riêng tư|bảo vệ] TenClass { ... }` | ✅ |
| Thêm modifier thành viên lớp theo thứ tự ưu tiên: `hàm công khai`, `hàm riêng tư`, `hàm bảo vệ` | ✅ |
| Không hỗ trợ cú pháp cũ: `công khai hàm`, `riêng tư hàm`, `bảo vệ hàm` (báo lỗi hướng dẫn cú pháp mới) | ✅ |
| Biên dịch method lớp thành tên đầy đủ dạng `TenClass.tenHam` | ✅ |
| Hỗ trợ gọi nội bộ trong cùng lớp bằng tên ngắn (ví dụ `nhanNoiBo(...)`) | ✅ |
| Chặn truy cập `riêng tư` từ ngoài lớp | ✅ |
| Chặn truy cập `bảo vệ` từ ngoài lớp (MVP chưa có kế thừa) | ✅ |
| Cập nhật syntax highlighting cho `lớp`, `công khai`, `riêng tư`, `bảo vệ` | ✅ |
| Bổ sung test hồi quy cho lớp + quyền truy cập | ✅ |
| Mở rộng `bảo vệ` theo mô hình kế thừa thật sự (khi có inheritance) | ⬜ |
| Hỗ trợ thuộc tính lớp (field) với quyền truy cập tương ứng | ⬜ |
| Hỗ trợ tạo đối tượng/instance (`new`) và gọi method theo instance | ⬜ |

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
| Số thực (float/double) | `3.14` | ✅ |
| Kiểu null / rỗng | `rỗng` | ✅ |
| Từ điển / Map | `{"a": 1}` | ✅ |

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
| Hàm ẩn danh / lambda | `hàm(x) { trả về x * 2; }` | ✅ |
| Hàm bậc cao (higher-order) | Truyền hàm làm tham số | ✅ |
| Giá trị tham số mặc định | `hàm f(x = 0) { ... }` | ✅ |
| Biến cục bộ tách biệt toàn cục | Scope isolation | ✅ |

---

## 6. 📁 Module & Import

| Tính năng | Trạng thái |
|-----------|-----------|
| `nhập file.vi` — import file khác | ✅ |
| Phát hiện import vòng (circular import) | ✅ |
| Phân giải đường dẫn tương đối | ✅ |
| Namespace / tên module | ✅ |
| Thư viện chuẩn tiếng Việt (stdlib) | ✅ |

### 6.1 API stdlib native
| API | Trạng thái |
|-----|-----------|
| `io_doc_file(path)` | ✅ |
| `io_ghi_file(path, content)` | ✅ |
| `doc_config(path)` | ✅ |
| `lay_thoi_gian_hien_tai()` | ✅ |
| `mang_http_get(url)` | ✅ *(phụ thuộc `curl` và mạng)* |
| `mang_http_post(url, payload)` | ✅ *(phụ thuộc `curl` và mạng)* |
| `mang_http_put(url, payload)` | ✅ *(phụ thuộc `curl` và mạng)* |

### 6.2 Kiến trúc stdlib theo module (Spring-style)
| Hạng mục | Trạng thái |
|----------|-----------|
| Entry-point `lib/stdlib.vi` đóng vai trò aggregator | ✅ |
| Entry-point `gói/stdlib/main.vi` đóng vai trò aggregator | ✅ |
| Module `core` (math/string/logic) | ✅ |
| Module `web` (HTTP get/post/put) | ✅ |
| Module `io` (đọc/ghi file) | ✅ |
| Module `time` (clock/time API) | ✅ |
| Module `config` (đọc cấu hình) | ✅ |
| Module `support` (logging) | ✅ |
| Facade `stdlib-web-starter` (import chọn lọc web stack) | ✅ |
| Facade `stdlib-data-starter` (import chọn lọc data stack) | ✅ |
| Facade `stdlib-app-starter` (import full app stack) | ✅ |

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
| Báo lỗi vị trí (dòng:cột) | ✅ |
| Khối `thử ... bắt lỗi` (try/catch) | ✅ |
| `ném lỗi` (throw exception) | ✅ |
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
| Tối ưu hoá bytecode (peephole) | ✅ |
| Garbage Collection (MVP: runtime compaction theo chu kỳ) | ✅ |
| JIT Compilation (MVP: linear bytecode lambda JIT, bật qua env) | ✅ |

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
| `kiem_tra_ngoai_le.vi` | `thử`/`bắt lỗi`/`ném` | ✅ |
| `kiem_tra_so_thuc.vi` | Số thực (float/double) | ✅ |
| `kiem_tra_rong_va_map.vi` | Kiểu `rỗng` và literal map | ✅ |
| `kiem_tra_lambda_hof_mac_dinh.vi` | Lambda + higher-order + tham số mặc định | ✅ |
| `kiem_tra_namespace_module.vi` | Namespace alias khi import module | ✅ |
| `kiem_tra_stdlib.vi` | Import và dùng stdlib tiếng Việt | ✅ |
| `kiem_tra_stdlib_starter.vi` | Import starter facade và dùng API cốt lõi | ✅ |
| `kiem_tra_stdlib_http.vi` | Native API: HTTP call thành công | ✅ |
| `kiem_tra_stdlib_http_post_put.vi` | Native API: HTTP POST/PUT thành công | ✅ |
| `kiem_tra_stdlib_tinh_toan.vi` | Bộ hàm tính toán stdlib đầy đủ | ✅ |
| `kiem_tra_stdlib_io_config_time.vi` | Native API: file/config/time | ✅ |
| `kiem_tra_tong_hop_khong_xung_dot.vi` | Test tích hợp nhiều tính năng trong cùng chương trình | ✅ |
| Unit test cho StringPool | Thêm/lấy/xóa | ⬜ |
| Unit test cho symbolTable | Scope isolation | ⬜ |

**Tỷ lệ regression hiện tại: 35/35 PASS ✅ (theo `run_tests.sh`, ngày 04/07/2026)**

---

## 11. 🛠️ Công Cụ & Hạ Tầng (Tooling)

| Tính năng | Trạng thái |
|-----------|-----------|
| CLI (`vietvm-cli`) | ✅ |
| Build với CMake | ✅ |
| Script chạy test (`run_tests.sh`) | ✅ |
| Không có duplicate library warnings | ✅ |
| `.gitignore` cho build artefacts | ✅ |
| CI/CD (GitHub Actions) | ✅ |
| Disassembler (xem bytecode) | ✅ |
| REPL (interactive shell) | ✅ |
| Language Server Protocol (LSP) | ✅ |
| Syntax highlighting (VSCode/Vim) | ✅ |
| Formatter / linter | ✅ |
| Package manager (`vpp-cli install`, `vpp-cli cai`) | ✅ |

### 11.1 Thư Viện & Phụ Thuộc Mã Nguồn
#### Đang sử dụng (đầy đủ theo quét include/CMake)
| Thành phần | Loại | Vai trò trong source code | Trạng thái |
|-----------|------|----------------------------|-----------|
| C++ Standard Library: `algorithm`, `cctype`, `cmath`, `cstddef`, `cstdint`, `filesystem`, `fstream`, `functional`, `iomanip`, `iostream`, `map`, `optional`, `ostream`, `regex`, `sstream`, `stack`, `stdexcept`, `string`, `unordered_map`, `unordered_set`, `utility`, `variant`, `vector` | Thư viện chuẩn C++17 | Nền tảng chính cho lexer, compiler, VM, CLI, package manager, LSP parser mini | ✅ |
| C/C++ runtime headers: `cstdio`, `cstdlib`, `ctime` | Thư viện chuẩn runtime | Hỗ trợ thao tác tiến trình/phụ trợ runtime (`popen`, thời gian hệ thống,...) | ✅ |
| `curl` (binary hệ thống, gọi qua shell) | Phụ thuộc runtime tuỳ chọn | Dùng trong `mang_http_get/post/put(...)` của stdlib native | ✅ *(tuỳ chọn; cần cài trên máy chạy)* |
| CMake >= 3.15 | Build system | Cấu hình module, compile/link (`vpp-frontend`, `vpp-compiler`, `vpp-vm`, `vpp-cli`) | ✅ |

#### Không sử dụng (không phát hiện trong include/CMake hiện tại)
| Thành phần | Trạng thái |
|-----------|-----------|
| `find_package(...)` cho thư viện bên thứ ba trong `CMakeLists.txt` | ✅ Không sử dụng |
| `Boost` | ✅ Không sử dụng |
| `OpenSSL` | ✅ Không sử dụng |
| `libcurl` (link trực tiếp qua CMake) | ✅ Không sử dụng *(chỉ gọi binary `curl` runtime)* |
| `fmt`, `spdlog` | ✅ Không sử dụng |
| `nlohmann/json`, `yaml-cpp` | ✅ Không sử dụng |
| `gRPC`, `protobuf` | ✅ Không sử dụng |
| `SQLite` | ✅ Không sử dụng |
| `Qt`/`wxWidgets` | ✅ Không sử dụng |
| `gtest`/`catch2` qua CMake | ✅ Không sử dụng |

### 11.2 Thư Viện Hỗ Trợ Ngôn Ngữ V++ (Theo Module Chức Năng)
| Thư viện/Module | Trạng thái | Ghi chú |
|-----------------|-----------|--------|
| I/O tệp & luồng | ✅ | Có `io_doc_file`, `io_ghi_file`, đọc/ghi file cơ bản |
| Hệ thống tệp (filesystem) | ✅ | Dùng `std::filesystem` cho import, package manager, CLI |
| Mạng TCP/UDP | ⬜ | Chưa có API socket native |
| HTTP client | ✅ | Có `mang_http_get/post/put(...)` qua `curl` shell; chưa link `libcurl` trực tiếp |
| Đa luồng/đồng thời | ⬜ | Chưa có thread API trong V++ |
| Collections (array/map) | ✅ | Array, map literal đã hỗ trợ |
| Xử lý chuỗi | ✅ | Nối chuỗi, thao tác chuỗi cơ bản |
| Toán học | ✅ | Có nhóm hàm stdlib tính toán |
| Ngày giờ | ✅ | Có `lay_thoi_gian_hien_tai()` |
| Serialization JSON/XML/YAML | ⬜ | Chưa có module serialize chuẩn |
| Logging chuẩn | ⬜ | Chưa có module log chuyên dụng |
| Cấu hình (config) | ✅ | Có `doc_config(path)` |
| Xử lý lỗi/ngoại lệ | ✅ | Có `thử` / `bắt lỗi` / `ném lỗi` |
| Testing framework nội bộ ngôn ngữ | 🚧 | Có `run_tests.sh`, chưa có test framework API trong V++ |
| Reflection/Metadata | ⬜ | Chưa có introspection runtime |
| FFI (gọi thư viện ngoài) | ⬜ | Chưa có cơ chế FFI chính thức |
| Quản lý gói & phiên bản | ✅ | Có `vpp cài đặt`, `vpp danh sách`, `vpp phiên bản` |
| Bảo mật/Crypto | ⬜ | Chưa có module mã hóa/hash chuẩn |
| Sandboxing/Permission | ⬜ | Chưa có hệ quyền/sandbox runtime |
| i18n/l10n | ⬜ | Chưa có module locale/translation |
| Diagnostics/Profiling | 🚧 | Có `vpp bác sĩ`, chưa có profiler chuyên sâu |
| GUI/Đồ họa | ⬜ | Chưa có thư viện GUI chuẩn |
| OS bindings nâng cao | 🚧 | Có mức cơ bản qua file/process, chưa có syscall API đầy đủ |

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
- [x] Báo lỗi có số dòng và cột
- [x] CI cơ bản với GitHub Actions

### Trung hạn (1–3 tháng)
- [x] Hỗ trợ số thực (float)
- [x] Xử lý ngoại lệ (`thử`/`bắt lỗi`/`ném`)
- [ ] Tách `VM::run()` thành các handler nhỏ
- [ ] Unit test cho từng opcode handler
- [ ] REPL (gõ lệnh trực tiếp)
- [ ] Disassembler hiển thị bytecode

### Dài hạn (3–12 tháng)
- [x] Kiểu từ điển / Map
- [x] Hàm bậc cao (higher-order functions)
- [x] Garbage Collection *(MVP)*
- [x] Thư viện chuẩn tiếng Việt (stdlib)
- [x] Namespace / module có tên
- [ ] Language Server Protocol (LSP)
- [x] Syntax highlighting cho VSCode
- [x] JIT Compilation (tuỳ chọn, MVP)
- [ ] Embeddable C API

---

## 📊 Tổng Kết

| Hạng mục | Hoàn thành | Tổng |
|---------|-----------|------|
| Từ khoá & cú pháp | 14 | 14 |
| Toán tử | 16 | 16 |
| Kiểu dữ liệu | 8 | 8 |
| Điều khiển luồng | 9 | 9 |
| Hàm | 12 | 12 |
| Module & import | 5 | 5 |
| Xử lý lỗi | 9 | 10 |
| VM & Bytecode | 14 | 14 |
| Compiler | 17 | 19 |
| Tests | 29 | 31 |
| Công cụ | 8 | 13 |
| Tài liệu | 7 | 10 |

> **Tổng cộng: ~148/161 (~92%) tính năng cốt lõi đã hoàn thành.**
