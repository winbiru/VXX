# ✅ V++ — Checklist Phát Triển Ngôn Ngữ

> Cập nhật lần cuối: 12/08/2026
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
| Mảng (array) | `[1, 2, 3]` | ⬜ — token cú pháp có mặt, nhưng literal/index chưa có runtime API ổn định |
| Mảng đa chiều | `[[1,2],[3,4]]` | ⬜ — chưa có model dữ liệu mảng thực thi |
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

### 6.2 Kiến trúc thư viện/framework theo module
| Hạng mục | Trạng thái |
|----------|-----------|
| `gói/thư viện/cốt lõi` (toán/chuỗi/luận lý/xác thực) | ✅ |
| `gói/thư viện/vào ra` (tệp/cấu hình/thời gian/nhật ký) | ✅ |
| `gói/thư viện/mạng` (HTTP native client/server wrapper) | ✅ |
| `gói/thư viện/mạng web` (REST helpers; test API tách riêng) | ✅ |
| `gói/thư viện/dữ liệu` (phân trang/database adapter) | ✅ *(phụ thuộc driver CLI của host)* |
| `gói/thư viện/ứng dụng` (lifecycle chung, không tự import full stack) | ✅ |
| `gói/thư viện/khởi động` (facade web/dữ liệu/ứng dụng; `ứng dụng` là full stack) | ✅ |
| `gói/thư viện/main.vi` (entrypoint đầy đủ) | ✅ |
| Shim tên starter cũ trong cùng cây `gói/thư viện/khởi động` | ✅ |

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
| Garbage Collection (MVP: runtime compaction theo chu kỳ) | 🚧 |
| JIT Compilation (MVP: linear bytecode lambda JIT, bật qua env) | 🚧 |

---

## 9. 🧩 Compiler

| Tính năng | Trạng thái |
|-----------|-----------|
| Tokenizer / Lexer (token mang span) | ✅ |
| Pipeline source → lexer → parser → AST → semantic → IR → optimizer → bytecode | ✅ — chọn direct IR cho program được hỗ trợ; phần còn lại dùng whole-program legacy bridge |
| AST cấu trúc không kiểu và mang span | ✅ — expression arena + statement tree lossless; lambda có parameter/default/body cấu trúc |
| Semantic model, scope tree và name/call resolution | ✅ — bind theo ExprId; lambda scope/capture đã có, module graph/import exports còn tiếp tục |
| IR không kiểu, đệ quy và optimizer IR | ✅ — direct emitter đọc structured IR; token payload chỉ còn cho compatibility fallback |
| Biên dịch biểu thức số học | ✅ |
| Biên dịch chuỗi và nối chuỗi | ✅ |
| Biên dịch điều kiện `nếu/hoặc` | ✅ |
| Biên dịch switch/case | ✅ |
| Biên dịch hàm và gọi hàm | ✅ |
| Biên dịch `trả về` | ✅ |
| Biên dịch `nhập` (import) | ✅ |
| Cú pháp token mảng (chưa codegen/runtime stable) | 🚧 |
| Biên dịch `bỏ qua` (continue) | ✅ |
| Biên dịch `--`, `+=`, `-=`, `*=`, `/=`, `%=` | ✅ |
| Biên dịch `đúng`/`sai` (boolean literals) | ✅ |
| Symbol table | ✅ |
| Fix `isNumber("-")` bug | ✅ |
| Fix `convertToPostfix` nested call argc | ✅ |
| Kiểm tra kiểu tĩnh (type checking) | ⬜ |
| Expression AST arena: literal/name/operator/assignment/call/lambda/map + span | ✅ — lambda body là recursive Block và vẫn giữ span/range lossless |
| Scope tree (global/class/function/block/lambda/catch) | ✅ |
| Lexical name/call resolution và class visibility | ✅ — module graph/import exports còn tiếp tục |
| Recursive IR lowering cho body/block/expression | ✅ — conditional/loop/switch/try/class đã có structured payload; import còn tiếp tục |
| IR → bytecode trực tiếp, không parse token lại | 🚧 — expression/function/lambda/call + structured control-flow/try/class-method cohort |
| Legacy token fallback = 0 trên toàn bộ corpus `.vi` | 🚧 — direct 29/57 chương trình; 28 program còn dùng whole-program bridge |
| Type policy và Typed IR | ⬜ |

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
| `kiem_tra_mang_3_chieu.vi` | Vòng lặp lồng ba chiều (không phải runtime array) | ✅ |
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
| `kiem_tra_package_modules.vi` | Import trực tiếp toàn bộ entrypoint package tiếng Việt | ✅ |
| `kiem_tra_package_tieng_viet.vi` | Import bare/quoted package tiếng Việt, kể cả qua `VPP_HOME` ngoài repo | ✅ |
| `kiem_tra_stdlib.vi` | Import và dùng stdlib tiếng Việt | ✅ |
| `kiem_tra_stdlib_starter.vi` | Import starter facade và dùng API cốt lõi | ✅ |
| `kiem_tra_stdlib_http.vi` | Native API: HTTP call thành công | ✅ |
| `kiem_tra_stdlib_http_post_put.vi` | Native API: HTTP POST/PUT thành công | ✅ |
| `http_fixture.vi` | HTTP fixture nội bộ viết bằng V++ cho test GET/POST/PUT/DELETE và body request | ✅ |
| `kiem_tra_stdlib_tinh_toan.vi` | Bộ hàm tính toán stdlib đầy đủ | ✅ |
| `kiem_tra_stdlib_io_config_time.vi` | Native API: file/config/time | ✅ |
| `kiem_tra_tong_hop_khong_xung_dot.vi` | Test tích hợp nhiều tính năng trong cùng chương trình | ✅ |
| `compiler_support_tests.cpp` — StringPool | Thêm/lấy/xóa, dedupe, invalid index | ✅ |
| `compiler_support_tests.cpp` — symbolTable / hamMap | ID ổn định, caller-map độc lập, reset allocator | ✅ |
| `vm_opcode_smoke_tests.cpp` | Số học, modulo, so sánh và chuỗi ở VM | ✅ — smoke, chưa bao phủ mọi opcode |
| `pipeline_tests.cpp` | Parser báo unmatched `]`/unclosed `[` bằng `VPP-SYN-035` và span nguồn | ✅ |
| `pipeline_legacy_parity_tests.cpp` | So fingerprint bytecode + StringPool + function maps của pipeline với baseline legacy đóng băng cho toàn bộ `src/tests/**/*.vi` | ✅ — 57 file, compile-only; production không có test API |
| `recursive_ir_tests.cpp` | Expression/default parameter/body/block lowering đệ quy và reachable fallback count | ✅ |
| `direct_codegen_tests.cpp` | Direct IR emitter cho literal/operator/assignment/print/map và bridge selection | ✅ |

**Regression suite: 49 checks trong `run_tests.sh`; full run cần cổng fixture `18080` khả dụng.**

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
| CTest integration target | ✅ |
| AddressSanitizer / UndefinedBehaviorSanitizer trên CI | ✅ |
| Disassembler (xem bytecode) | ✅ |
| REPL (interactive shell) | ✅ |
| Language Server Protocol (LSP) | 🚧 — MVP |
| Syntax highlighting (VSCode/Vim) | ✅ |
| Formatter / linter | ✅ |
| Package manager (`vpp-cli install`, `vpp-cli cai`) | 🚧 — MVP, chưa có dependency/version resolver |
| Mẫu backend/API server | ✅ — `examples/api_project` và `vpp khởi tạo backend <tên>` từ `templates/backend` |

### 11.1 Thư Viện & Phụ Thuộc Mã Nguồn
#### Đang sử dụng (đầy đủ theo quét include/CMake)
| Thành phần | Loại | Vai trò trong source code | Trạng thái |
|-----------|------|----------------------------|-----------|
| C++ Standard Library: `algorithm`, `cctype`, `cmath`, `cstddef`, `cstdint`, `filesystem`, `fstream`, `functional`, `iomanip`, `iostream`, `map`, `optional`, `ostream`, `regex`, `sstream`, `stack`, `stdexcept`, `string`, `unordered_map`, `unordered_set`, `utility`, `variant`, `vector` | Thư viện chuẩn C++17 | Nền tảng chính cho lexer, compiler, VM, CLI, package manager, LSP parser mini | ✅ |
| C/C++ runtime headers: `cstdio`, `cstdlib`, `ctime` | Thư viện chuẩn runtime | Hỗ trợ thao tác tiến trình/phụ trợ runtime (`popen`, thời gian hệ thống,...) | ✅ |
| `curl` (binary hệ thống, gọi qua process/shell tuỳ nền tảng) | Phụ thuộc runtime tuỳ chọn | Dùng trong `mang_http_get/post/put/delete(...)` của stdlib native | ✅ *(tuỳ chọn; cần cài trên máy chạy)* |
| CMake >= 3.15 | Build system | Cấu hình module, compile/link (`vpp-core`, `vpp-bytecode`, `vpp-frontend`, `vpp-compiler`, `vpp-runtime`, `vpp-tooling`, `vpp-cli`) | ✅ |

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
| `SQLite` C++ library | ✅ Không link trực tiếp; database adapter dùng `sqlite3` CLI runtime khi được cấu hình |
| `Qt`/`wxWidgets` | ✅ Không sử dụng |
| `gtest`/`catch2` qua CMake | ✅ Không sử dụng |

### 11.2 Thư Viện Hỗ Trợ Ngôn Ngữ V++ (Theo Module Chức Năng)
| Thư viện/Module | Trạng thái | Ghi chú |
|-----------------|-----------|--------|
| I/O tệp & luồng | ✅ | Có `io_doc_file`, `io_ghi_file`, đọc/ghi file cơ bản |
| Hệ thống tệp (filesystem) | ✅ | Dùng `std::filesystem` cho import, package manager, CLI |
| Mạng TCP/UDP | ⬜ | Có TCP nội bộ cho HTTP server; chưa có API TCP/UDP tổng quát cho V++ |
| HTTP client | ✅ | Có `mang_http_get/post/put/delete(...)` qua `curl` shell; test dùng fixture nội bộ viết bằng `.vi`; chưa link `libcurl` trực tiếp |
| Đa luồng/đồng thời | ⬜ | Chưa có thread API trong V++ |
| Collections (array/map) | 🚧 | Có map literal scalar; array literal/index và map get/set chưa là API runtime ổn định |
| Xử lý chuỗi | ✅ | Nối chuỗi, thao tác chuỗi cơ bản |
| Toán học | ✅ | Có nhóm hàm stdlib tính toán |
| Ngày giờ | ✅ | Có `lay_thoi_gian_hien_tai()` |
| Serialization JSON/XML/YAML | 🚧 | Có `mạng web/json.vi` cho escape/quote và dựng object chuỗi; chưa có parser hoặc array/nested-object runtime |
| Logging chuẩn | ✅ | Có module `gói/thư viện/vào ra/nhật ký.vi` cơ bản |
| Cấu hình (config) | ✅ | Có `doc_config(path)` |
| Xử lý lỗi/ngoại lệ | ✅ | Có `thử` / `bắt lỗi` / `ném lỗi` |
| Testing framework nội bộ ngôn ngữ | 🚧 | Có `run_tests.sh` và `kiểm thử` với assertion cơ bản; chưa có discovery/runner API trong V++ |
| Reflection/Metadata | ⬜ | Chưa có introspection runtime |
| FFI (gọi thư viện ngoài) | ⬜ | Chưa có cơ chế FFI chính thức |
| Quản lý gói & phiên bản | 🚧 | Có CLI MVP; chưa có dependency/version resolver |
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
| `README-updates.md` — Trạng thái repo & roadmap | ✅ |
| `docs/architecture.md` — Kiến trúc | ✅ |
| `docs/bytecode.md` — Mô tả bytecode | ✅ |
| `docs/grammar.bnf` — Ngữ pháp BNF | ✅ |
| `docs/language-comparison.md` — So sánh với ngôn ngữ khác | ✅ |
| `CHECKLIST.md` — File này | ✅ |
| `CONTRIBUTING.md` — Hướng dẫn đóng góp | ✅ |
| Tutorial / ví dụ HTTP tối giản | ✅ — `examples/api_project` |
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
- [x] CTest unit target cho StringPool, hamMap và symbolTable
- [x] CTest unit target cho canonical opcode và native HTTP constants
- [x] CTest smoke target cho opcode số học, so sánh và chuỗi của VM

### Trung hạn (1–3 tháng)
- [x] Hỗ trợ số thực (float)
- [x] Xử lý ngoại lệ (`thử`/`bắt lỗi`/`ném`)
- [ ] Tách `VM::run()` thành các handler nhỏ
- [ ] Unit test cho từng opcode handler *(MVP smoke test đang được mở rộng; chưa đủ coverage từng handler)*
- [x] REPL (gõ lệnh trực tiếp)
- [x] Disassembler hiển thị bytecode

### Dài hạn (3–12 tháng)
- [x] Kiểu từ điển / Map
- [x] Hàm bậc cao (higher-order functions)
- [x] Runtime compaction theo chu kỳ *(GC MVP; tracing GC còn mở)*
- [x] Thư viện chuẩn tiếng Việt (stdlib)
- [x] Namespace / module có tên
- [x] Language Server Protocol (LSP, MVP)
- [x] Syntax highlighting cho VSCode
- [x] Linear bytecode JIT path (tuỳ chọn, MVP; chưa sinh mã máy)
- [ ] Embeddable C API

---

## 14. 🧭 Đánh Giá Kiến Trúc & Hướng Phát Triển

Phần này phân biệt rõ **mức hoàn thành checklist nội bộ** với mức trưởng thành
của một platform như Java, C# hoặc Python. Nó được đối chiếu với source hiện
có; không tính các mục chỉ nằm trên roadmap.

### 14.1 So sánh capability hiện tại

| Thành phần | V++ hiện tại | Java | C# | Python |
|---|---|---|---|---|
| Lexer / parser | ✅ Token mang span, parser cấu trúc | ✅ Trưởng thành | ✅ Trưởng thành | ✅ Trưởng thành |
| AST chuẩn | 🚧 AST cấu trúc mang span; chưa có AST biểu thức đầy đủ | ✅ | ✅ | ✅ |
| Name resolution / semantic analysis | 🚧 Khai báo và lời gọi trực tiếp; chưa có scope/import/lớp đầy đủ | ✅ | ✅ | ✅ |
| Static typing | ⬜ Chưa quyết định mô hình kiểu | ✅ Mạnh | ✅ Mạnh | 🟡 Dynamic + type hints |
| Bytecode / VM | ✅ Stack bytecode và V++ VM | ✅ JVM | ✅ CLR | ✅ CPython VM |
| GC / JIT | 🚧 MVP, chưa production-grade | ✅ Mature | ✅ Mature | ✅/🟡 Tuỳ runtime |
| Exception / function / lambda-HOF | ✅ | ✅ | ✅ | ✅ |
| Array / map | 🚧 Map scalar; chưa có array/index API ổn định | ✅ | ✅ | ✅ |
| Module / import | ✅ | ✅ | ✅ | ✅ |
| Class / object / inheritance | 🚧 Class-method và modifier; chưa có instance/inheritance | ✅ | ✅ | ✅ |
| Generics / reflection | ⬜ (reflection hiện không là mục tiêu mặc định) | ✅ | ✅ | ✅ |
| Concurrency / async / FFI | ⬜ | ✅ | ✅ | ✅ |
| Standard library | 🚧 Nhỏ, theo module | ✅ Rất lớn | ✅ Rất lớn | ✅ Rất lớn |
| HTTP / package / REPL | ✅ HTTP cơ bản, package MVP, REPL | ✅ | ✅ | ✅ |
| LSP / debugger / profiler | 🚧 LSP MVP; chưa có debugger/profiler | ✅ | ✅ | ✅ |
| Cross-platform | 🚧 Unix + Windows CI; còn phụ thuộc binary host | ✅ | ✅ | ✅ |
| Production maturity | ⬜ Experimental | ✅✅✅ | ✅✅✅ | ✅✅✅ |

### 14.2 Thước đo đúng

Mốc **~92% checklist của V++** chỉ đo các hạng mục mà roadmap cũ đã liệt kê.
Nó **không** có nghĩa V++ bằng 92% Java. Nếu lấy Java platform là 100, đây là
ước lượng kiến trúc để định hướng đầu tư, không phải KPI sản phẩm:

| Lớp | V++ | Java |
|---|---:|---:|
| Syntax | 85% | 100% |
| Functions | 80% | 100% |
| Control flow | 90% | 100% |
| Data types | 60% | 100% |
| Compiler pipeline | 40% | 100% |
| VM | 35% | 100% |
| GC | 15% | 100% |
| JIT | 5–10% | 100% |
| OOP | 25% | 100% |
| Standard library | 15% | 100% |
| Concurrency | 0–5% | 100% |
| Tooling | 20% | 100% |
| Ecosystem | <1% | 100% |

Theo góc nhìn platform engineering, V++ hiện phù hợp khoảng **15–25%** của
một Java platform. Đây là điều bình thường với một runtime nghiên cứu nhỏ;
Java đã tích luỹ hơn ba thập kỷ cùng hệ sinh thái rất lớn.

### 14.3 Tài sản runtime và khoảng trống compiler

V++ không cần bỏ VM. Stack bytecode VM, call frame, globals/locals, recursion,
GC MVP, peephole optimizer và JIT MVP là nền tảng đúng hướng, gần Java/C# hơn
là một interpreter thuần source:

```text
.vi → V++ compiler → V++ bytecode → V++ VM → interpreter / JIT MVP → CPU
```

Khoảng trống quan trọng vẫn là loại bỏ phần token-backed còn lại giữa
frontend và bytecode. Pipeline incremental đã có các ranh giới cấu trúc sau;
backend selector chọn direct IR cho cohort đã migrate và whole-program legacy bridge
khi program còn vùng chưa được emitter hỗ trợ:

```text
Source
  ↓ Lexer (token mang span)
  ↓ Parser
AST cấu trúc, không kiểu
  ↓ Semantic model (khai báo/lời gọi trực tiếp)
IR không kiểu, lossless
  ↓ Optimizer
Bytecode
  ↓ VM / JIT
```

Expression AST, scope tree, lexical resolution và recursive untyped IR đã có.
Lambda body đã có AST/scope/IR và capture-free codegen; closure capture,
module graph/import exports, strict unresolved-name policy, type policy và
Typed IR vẫn cần hoàn thiện.

### 14.4 Quyết định bắt buộc trước Typed IR

V++ hiện hành xử gần dynamic hơn static: giá trị được mang ở runtime và chưa
có type checker. Trước khi làm Typed IR hoặc static checker, dự án phải chốt
một ADR cho một trong ba hướng:

| Hướng | Lợi ích | Hệ quả |
|---|---|---|
| Dynamic như Python | Linh hoạt, dễ phát triển frontend | Cần runtime checks và tối ưu kiểu suy đoán |
| Static như Java/C# | Chẩn đoán sớm, tối ưu dễ hơn | Cần annotation/inference, compatibility policy |
| Gradual typing | Lộ trình chuyển đổi mềm | Thiết kế phức tạp nhất, cần boundary rõ ràng |

Expression AST mang span, scope tree, ExprId-based resolution và recursive untyped IR đã
có. Cho tới khi ADR này được chốt, không được ngầm áp đặt static typing
hợp đồng runtime động. Việc hoàn thiện closure capture/import và Typed IR phải giữ
hợp đồng runtime động.

### 14.5 Sáu milestone compiler theo thứ tự

1. **Expression AST** — node biểu thức có span và precedence ổn định.
2. **Scope tree** — global/class/function/block/lambda/catch với parent/child rõ ràng.
3. **Real name resolution** — bind node → symbol, phân biệt direct/indirect/dynamic call.
4. **Recursive IR lowering** — hạ body, block, expression và control flow, không chỉ top-level.
5. **Direct IR → bytecode** — emitter đọc IR và phát instruction/fixup, không parse token lại.
6. **Bỏ token bridge** — đếm fallback, giảm về 0 trên corpus `.vi`, rồi mới xóa legacy path.

Typed IR là roadmap riêng sau quyết định type policy; nó không chặn sáu milestone
untyped này. VM/object model/platform work tiếp tục sau khi compiler boundary đủ
ổn định để không phải sửa cùng lúc cả frontend lẫn runtime.

Nếu mục tiêu cuối là backend, ưu tiên chiều sâu ở các milestone này hơn việc
thêm nhiều keyword giống Java. TLS, socket, thread/async, connection pooling,
driver DB, crypto, logging, config, debugger, profiler, security và monitoring
đều là phần của platform — không chỉ là cú pháp.

### 14.6 Backlog kiến trúc có điều kiện chấp nhận

- [ ] Chốt ADR type policy (dynamic, static hoặc gradual) trước Typed IR.
- [x] Parser/Expression AST: arena mang span cho literal/name/operator/assignment/call/lambda/map và parser unit tests.
- [x] Scope tree: global/class/function/block/lambda/catch, parameter/local/import alias và parent/child link.
- [x] Name resolution MVP: bind expression/call/lambda body theo ExprId, shadowing/capture/recursion/class visibility; còn module graph.
- [x] Recursive IR control flow: expression/body/block/lambda/if/loop/switch/try/class đã hạ; import là cohort còn lại.
- [ ] IR/codegen boundary: migrate emitter theo opcode/feature, đo legacy fallback và chỉ xóa token bridge khi đạt zero.
- [ ] VM: tách handler và bổ sung unit test từng opcode trước tối ưu mới.
- [ ] Object heap/GC v2: instance/field và tracing/lifetime test trước inheritance.
- [ ] JSON/network/concurrency: mỗi API có contract, error path và integration test.

---

## 📊 Tổng Kết

| Thước đo | Ý nghĩa |
|---------|---------|
| `~148/161 (~92%)` | Snapshot của roadmap legacy, không còn là bộ đếm live sau khi tách MVP/đang làm/chưa có runtime API. |
| Capability hiện tại | Xem phần 14: V++ là runtime/compiler thử nghiệm, khoảng 15–25% Java platform theo ước lượng kiến trúc. |
| Backlog thực thi | Các checkbox ở §13 và §14.6 là nguồn trạng thái hiện hành. |

> Không diễn giải phần trăm roadmap nội bộ như mức tương đương với Java, C# hoặc
> Python. Mỗi hạng mục `🚧`/`⬜` phía trên có phạm vi và điều kiện chấp nhận riêng.
