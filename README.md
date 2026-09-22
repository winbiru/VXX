# V++

V++ là một bộ compiler + virtual machine thử nghiệm cho một ngôn ngữ lập trình kiểu Việt hoá. Repo này tập trung vào bytecode rõ ràng, luồng điều khiển minh bạch và khả năng kiểm thử tốt.

## Nhanh Chóng

### Linux/macOS

```bash
cmake -S . -B build
cmake --build build --parallel
```

Hoặc:

```bash
./scripts/build-vpp-cli.sh
```

### Windows (MSVC Developer Command Prompt)

```bat
scripts\build-vpp-cli.bat
```

Lưu ý: không build bằng `c++ src/cli/main.cpp -o main` vì thiếu toàn bộ source files và cờ chuẩn C++17.

Chạy một chương trình:

```bash
./build/bin/vpp-cli src/tests/program.vi
```

Chạy toàn bộ test:

```bash
ctest --test-dir build --output-on-failure --no-tests=error
```

Nếu dùng binary tạo bởi `./scripts/build-vpp-cli.sh`, có thể chạy regression trực
tiếp bằng `VPP_EXEC=./bin/vpp-cli ./run_tests.sh`.

## Cài Nhanh Không Cần Clone

Ban co the tai file da build san tu GitHub Release.

### Linux

```bash
curl -L https://github.com/winbiru/VXX/releases/latest/download/vpp-linux-x64.tar.gz -o vpp-linux-x64.tar.gz
tar -xzf vpp-linux-x64.tar.gz
./install-vpp.sh
vpp giúp đỡ
```

### macOS

```bash
curl -L https://github.com/winbiru/VXX/releases/latest/download/vpp-macos.tar.gz -o vpp-macos.tar.gz
tar -xzf vpp-macos.tar.gz
./install-vpp.sh
vpp giúp đỡ
```

### Windows (PowerShell)

```powershell
Invoke-WebRequest -Uri "https://github.com/winbiru/VXX/releases/latest/download/vpp-windows-x64.zip" -OutFile "vpp-windows-x64.zip"
Expand-Archive -Path "vpp-windows-x64.zip" -DestinationPath ".\vpp-bin" -Force
.\vpp-bin\install-vpp.ps1
vpp giúp đỡ
```

Hoặc chạy file batch:

```cmd
.\vpp-bin\install-vpp.cmd
```

Luu y: release asset se duoc tao boi workflow `.github/workflows/release-binaries.yml` khi ban publish Release.

## Cài Đặt Hỗ Trợ Ngôn Ngữ

Repo này có extension cục bộ cho file `.vi` và `.vvm`, gồm tô màu cú pháp và tích hợp
language server V++ cho diagnostic, completion, go-to-definition, hover, rename và format.

### Cách 1: Cài vào VS Code

```bash
./scripts/vpp-lang self-install
vpp-lang install --editor vscode
```

Sau đó reload VS Code và mở file `.vi`.

Extension sẽ tự khởi động `vpp --lsp` khi có tài liệu V++ đang mở. Mặc định extension ưu tiên
VM do chính extension quản lý, sau đó dùng lệnh `vpp` trong `PATH`. Có thể đặt executable riêng
qua `vpp.lsp.executable`, tắt LSP bằng `vpp.lsp.enabled`, hoặc chạy lệnh
`V++: Khởi động lại máy chủ ngôn ngữ` từ Command Palette.

### Cách 2: Dùng trực tiếp trong workspace

1. Mở repo này trong VS Code.
2. Nhấn `F5`.
3. Ở cửa sổ Extension Development Host, mở file `.vi`.

### Các lệnh hữu ích

```bash
./scripts/vpp-lang list
./scripts/vpp-lang pack
./scripts/vpp-lang uninstall --editor vscode
./scripts/vpp-lang self-uninstall
```

`pack` sẽ tạo file `.vlang` trong `dist/`.

## Khởi tạo project

Template ứng dụng chuẩn tạo project schema 1 có `src/`, `tests/` và thư mục dependency `gói/`:

```bash
vpp khởi tạo ứng dụng my-app
cd my-app
vpp dựng src/main.vi
vpp chạy src/main.vi
vpp kiểm thử tests
```

Template backend vẫn có thể tạo bằng `vpp khởi tạo backend <tên>`. Project mẫu thực tế
`examples/hoa-don-cua-hang` minh họa module, class/constructor và import tương đối.

Hướng dẫn cài/cập nhật release cho Linux, macOS, Windows và đánh giá Homebrew/winget nằm tại
`docs/installation.md`.

## Tooling CLI

CLI chính hiện có các lệnh hỗ trợ phát triển:

```bash
./VPP giúp đỡ
./VPP phiên bản
./VPP chẩn đoán
./VPP nơi
./VPP thống kê
./VPP chạy example.vi
./VPP dựng example.vi
./VPP kiểm thử tests
./VPP gói cài đặt "./gói/lõi"
./VPP gói xóa ten-goi
./VPP gói thông tin "lõi"
./VPP gói kiểm tra "lõi"

./bin/vpp-cli --giải-mã example.vi
./bin/vpp-cli --dump-ast example.vi
./bin/vpp-cli --dump-ir example.vi
./bin/vpp-cli --soát-lỗi example.vi
./bin/vpp-cli --soát-lỗi src/
./bin/vpp-cli --định-dạng example.vi
./bin/vpp-cli --định-dạng example.vi --ghi-tệp
./bin/vpp-cli --định-dạng src/ --kiểm-tra
./bin/vpp-cli --repl
./bin/vpp-cli --lsp
./bin/vpp-cli khởi tạo demo
./bin/vpp-cli khởi tạo backend my-api
./bin/vpp-cli chạy example.vi
./bin/vpp-cli dựng example.vi
./bin/vpp-cli kiểm thử tests
./bin/vpp-cli gói khởi tạo demo
./bin/vpp-cli gói thêm lib.vi mypkg
./bin/vpp-cli gói cài đặt "./gói/lõi"
./bin/vpp-cli gói xóa mypkg
./bin/vpp-cli gói thông tin mypkg
./bin/vpp-cli gói kiểm tra mypkg
./bin/vpp-cli gói danh sách
```

CLI 1.0 lấy tên lệnh tiếng Việt làm contract chính. Các alias `new`, `run`, `test`,
`build` và tên package command tiếng Anh vẫn được giữ để tương thích với script cũ.
LSP (`vpp-cli --lsp`) dùng cùng diagnostic với linter và hỗ trợ completion,
go-to-definition, hover, rename dựa trên semantic model; vị trí/range tuân theo UTF-16 của LSP.
Lệnh `dựng` hiện chạy toàn bộ compiler pipeline và xác nhận bytecode/metadata trong bộ nhớ;
nó chưa ghi file `.vbc` cho tới khi format bytecode 1.0 được đóng băng.

`--soát-lỗi` là tên canonical của linter (`--lint` vẫn là alias). Lệnh nhận một tệp hoặc
thư mục, quét `.vi` đệ quy theo thứ tự xác định và trả mã lỗi khác 0 nếu có diagnostic lỗi;
output có dạng `tệp:dòng:cột: lỗi: ...` để CI/editor có thể định vị nguồn. `--định-dạng`
giữ nguyên comment và literal, có tính idempotent, hỗ trợ `--ghi-tệp` để sửa tại chỗ và
`--kiểm-tra` để CI chỉ kiểm tra mà không thay đổi file. Các alias `--in-place`/`--check`
được giữ cho script cũ.

`--dump-ast` in AST cấu trúc có span do parser tạo; `--dump-ir` in IR **sau
optimizer** mà cầu nối bytecode nhận. Cả hai chỉ xuất thông tin phát triển ra
stdout, không chạy V++ VM, nên có thể redirect vào file khi cần kiểm tra.

Các lệnh quản lý gói canonical nằm dưới namespace `gói`. Tên tiếng Anh và các alias
không dấu cũ vẫn được giữ để script hiện có không bị hỏng:

```bash
./bin/vpp-cli gói cài đặt ./duong-dan/goi.vi ten-goi
# tương thích: ./bin/vpp-cli install ./duong-dan/goi.vi ten-goi
```

Windows có thể dùng trực tiếp:

```cmd
VPP.cmd gói cài đặt duong-dan\goi.vi ten-goi
```

Thư viện chuẩn là tập các package tiếng Việt nằm trực tiếp dưới `gói/`.
`gói/chuẩn/main.vi` chỉ là entrypoint tổng hợp để nhập toàn bộ gói chuẩn.
Program mới nên import package nhỏ nhất cần dùng. Mỗi câu `nhập` chỉ nhận một
file hoặc package/folder. Target luôn viết trực tiếp, không dùng dấu nháy; tên
package hoặc đường dẫn có khoảng trắng vẫn được parser giữ như một target duy nhất:

```vi
nhập lõi;
nhập nhập xuất;
nhập mạng;
nhập hệ thống;
nhập dữ liệu;
nhập ứng dụng;
nhập dựng;
nhập kiểm thử;
```

- `gói/lõi`: toán học, chuỗi UTF-8 cơ bản, collections, chuyển kiểu, random, crypto cơ bản,
  luận lý và validation.
- `gói/nhập xuất`: tệp, đường dẫn/thư mục, cấu hình, thời gian và logging.
- `gói/hệ thống`: biến môi trường, nhận diện nền tảng và sleep mức mili giây.
- `gói/mạng`: HTTP client/server, REST helpers và JSON parse/serialize map/list/scalar.
- `gói/dữ liệu`: phân trang và database adapter.
- `gói/ứng dụng`: chỉ lifecycle ứng dụng chung; không tự kéo web, HTTP hay data.
- `gói/dựng`: facade tiện dụng cho web, dữ liệu và ứng dụng full stack.
- `gói/kiểm thử`: assertion và lifecycle test trong mã V++ (`khẳng định đúng/sai/bằng/khác`,
  `khẳng định rỗng/không rỗng`, `khẳng định ném lỗi`, `chạy ca kiểm thử`). Xem
  `docs/testing.md` cho discovery, setup/teardown và expected-error contract.

Một số API chuẩn hiện được nối trực tiếp vào native runtime:

```vi
nhập lõi;
nhập nhập xuất;
nhập hệ thống;

in thành chuỗi(42);
in loại của(42);
in ngẫu nhiên nguyên(1, 10);
in băm sha256("Việt Nam");
in hmac sha256("khóa", "Việt Nam");
in ngẫu nhiên bảo mật(16); // 32 ký tự hex = 16 byte ngẫu nhiên
in đường dẫn nối("tmp", "data.txt");
in đường dẫn tồn tại("tmp/data.txt");
in đọc biến môi trường("HOME", "");
in tên nền tảng();
```

Nhóm `lõi` có chuyển kiểu/quan sát loại, random và crypto cơ bản. Crypto 1.0 dùng
SHA-256, HMAC-SHA256 và random bảo mật từ primitive hệ điều hành/OpenSSL; digest/random
được trả dưới dạng hex chữ thường. `nhập xuất` có path, kiểm tra
tệp/thư mục, tạo/liệt kê/xóa thư mục; `hệ thống` có biến môi trường, nhận diện nền
tảng và sleep mili giây. Các hàm `.vi` tương ứng là public surface của gói chuẩn,
còn implementation native nằm trong `src/runtime/native/`.

`gói/chuẩn/main.vi` là entrypoint đầy đủ. Mỗi package chuẩn có `main.vi` tại
`gói/<tên tiếng Việt>/main.vi`. Có thể import theo tên package như
trên, hoặc dùng đường dẫn tường minh khi cần module con, ví dụ
`nhập gói/mạng/kiểm thử/api;`. Các bản cài từ release đặt các gói chuẩn
chuẩn cạnh binary và installer tự cấu hình `VPP_HOME`, vì vậy các import này
vẫn hoạt động ngoài repository.

`kiểm thử` và `mạng/kiểm thử/api.vi` là module tùy chọn, không được import
tự động bởi `main.vi`; mã production không bị kéo theo API kiểm thử.

Các package trên được bundle cùng V++; package manager nhìn thấy trực tiếp
`lõi`, `mạng`, `dữ liệu`... và hiện chưa tự resolve dependency/version giữa chúng.

HTTP server native hỗ trợ Linux, macOS và Windows.

`độ dài("Việt Nam")` và `đảo ngược(...)` xử lý chuỗi theo biên code point UTF-8.
Các API đổi hoa/thường hỗ trợ bảng chữ cái tiếng Việt theo phạm vi 1.0 và các helper text
liên quan dùng NFC để giữ hành vi nhất quán giữa chuỗi dựng sẵn và chuỗi tách dấu. JSON parser ánh xạ
`true/false` sang `đúng/sai` (1/0), `null` sang `rỗng`, object sang map và array
sang list.

Tạo backend tối giản:

```bash
vpp khởi tạo backend my-api
cd my-api
vpp application.vi
curl http://127.0.0.1:8080/health
```

Endpoint mẫu trả JSON boolean `true`.

### Gói dựng

Các entrypoint dựng canonical có thể import bằng đường dẫn:

```vi
nhập gói/dựng/web;
nhập gói/dựng/dữ liệu;
nhập gói/dựng/ứng dụng;
```

- `web`: lõi + nhập xuất + mạng.
- `dữ liệu`: lõi + nhập xuất + dữ liệu.
- `ứng dụng`: lõi + nhập xuất + mạng + dữ liệu + lifecycle.

Vì vậy hãy dùng `gói/ứng dụng` khi chỉ cần lifecycle, và dùng
`gói/dựng/ứng dụng.vi` khi chủ ý cần full stack. Test request builders ở
`gói/mạng/kiểm thử/api.vi` không được import tự động bởi cả hai entrypoint.

Các đường dẫn `khởi động/...` cũ được resolver chuyển sang `dựng/...`, nên mã cũ
vẫn chạy mà không cần giữ file shim trong cây package canonical. Adapter API mẫu
nằm tại `gói/ứng dụng/cầu nối/api.vi`; đường dẫn `tương thích/api.vi` cũ cũng được
redirect tương tự.

## Cấu Trúc Chính

- `src/cli/main.cpp`: entrypoint của CLI
- `src/core/`: tiện ích dùng chung
- `src/frontend/`: lexer (token mang span), parser, AST và keyword map
- `src/compiler/`: semantic analysis, IR không kiểu, optimizer và direct bytecode emitter
- `src/runtime/`: VM + native adapters (HTTP, file, DB)
- `src/tooling/`: formatter, linter, disassembler và AST/IR dump renderer
- `examples/`: ứng dụng mẫu chạy độc lập
- `templates/`: template do CLI scaffold sử dụng
- `src/tests/fixtures/`: dữ liệu/fixture cho regression test
- `src/tests/`: chương trình regression V++ (`.vi`) và expected output/fixture
- `test/`: source C++ cho CTest unit và CLI tooling checks
- `docs/`: bytecode, grammar, kiến trúc

Runtime có fixture nội bộ `src/include/vpp/runtime/vm_fixture.h` dành cho unit test
từng opcode handler mà không cần chạy toàn bộ dispatch hoặc phụ thuộc stdout. Đây là
test API nội bộ, không phải embedding API công khai.

## Pipeline Biên Dịch

V++ tổ chức quá trình biên dịch theo các bước tăng dần:

```text
Source
  ↓
Lexer (token mang span)
  ↓
Parser
  ↓
AST
  ↓
Semantic Analysis
  ↓
IR không kiểu
  ↓
Optimizer
  ↓
Bytecode
  ↓
V++ VM
```

Runtime dùng dynamic typing theo ADR 1.0, nên IR hiện chủ ý **không mang kiểu**.
Typed IR/static hoặc gradual checking được hoãn sau 1.0. Production compiler đi trực tiếp
từ structured IR sang bytecode; token bridge cũ không còn nằm trên production compile path.

Các header pipeline được quy hoạch dưới
`src/include/vpp/frontend/{token,ast,parser}.h` và
`src/include/vpp/compiler/{semantic,ir,optimizer,pipeline}.h`. Xem
`docs/architecture.md` để biết ranh giới từng bước và trạng thái API.

## Tài Liệu

- `docs/language-reference.md`
- `docs/cli.md`
- `docs/debugging.md`
- `docs/migration-1.0.md`
- `docs/package-system.md`
- `docs/testing.md`
- `docs/installation.md`
- `docs/semantics.md`
- `docs/runtime-errors.md`
- `PROJECT_COMMANDS.md`
- `README-updates.md`
- `docs/architecture.md`
- `docs/bytecode.md`
- `docs/diagnostic-codes.md`
- `docs/quality.md`
- `docs/grammar.bnf`
- `docs/language-comparison.md`
- `docs/language-comparison-en.md`
- `CHANGELOG.md`

Trạng thái roadmap và baseline kiểm thử gần nhất nằm ở `plans/progress.md`.
