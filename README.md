# V++

V++ là một bộ compiler + virtual machine thử nghiệm cho một ngôn ngữ lập trình kiểu Việt hoá. Repo này tập trung vào bytecode rõ ràng, luồng điều khiển minh bạch và khả năng kiểm thử tốt.

## Nhanh Chóng

### Linux/macOS

```bash
mkdir -p cmake-build-debug
cd cmake-build-debug
cmake ..
make -j
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
./bin/vpp-cli src/tests/program.vi
```

Chạy toàn bộ test:

```bash
./run_tests.sh
```

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

Repo này có extension cục bộ để tô màu cú pháp cho file `.vi` và `.vvm`.

### Cách 1: Cài vào VS Code

```bash
./scripts/vpp-lang self-install
vpp-lang install --editor vscode
```

Sau đó reload VS Code và mở file `.vi`.

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

## Tooling CLI

CLI chính hiện có các lệnh hỗ trợ phát triển:

```bash
./VPP giúp đỡ
./VPP phiên bản
./VPP bác sĩ
./VPP nơi
./VPP thống kê
./VPP chạy example.vi
./VPP cài đặt "./gói/thư viện"
./VPP caidat ./duong-dan/goi.vi ten-goi
./VPP xóa ten-goi
./VPP thông tin "thư viện"
./VPP kiểm tra "thư viện"

./bin/vpp-cli --giải-mã example.vi
./bin/vpp-cli --dump-ast example.vi
./bin/vpp-cli --dump-ir example.vi
./bin/vpp-cli --lint example.vi
./bin/vpp-cli --định-dạng example.vi
./bin/vpp-cli --định-dạng example.vi --in-place
./bin/vpp-cli --repl
./bin/vpp-cli --lsp
./bin/vpp-cli khởi tạo demo
./bin/vpp-cli khởi tạo backend my-api
./bin/vpp-cli cài đặt "./gói/thư viện"
./bin/vpp-cli danh sách
./bin/vpp-cli xóa mypkg
./bin/vpp-cli thông tin "thư viện"
./bin/vpp-cli kiểm tra "thư viện"
./bin/vpp-cli thống kê
./bin/vpp-cli gói khởi tạo demo
./bin/vpp-cli gói thêm lib.vi mypkg
./bin/vpp-cli gói xóa mypkg
./bin/vpp-cli gói thông tin mypkg
./bin/vpp-cli gói kiểm tra mypkg
./bin/vpp-cli gói danh sách
```

`--dump-ast` in AST cấu trúc có span do parser tạo; `--dump-ir` in IR **sau
optimizer** mà cầu nối bytecode nhận. Cả hai chỉ xuất thông tin phát triển ra
stdout, không chạy V++ VM, nên có thể redirect vào file khi cần kiểm tra.

`cài đặt` là lệnh chính cho package manager, và `caidat` cũng được hỗ trợ:

```bash
./bin/vpp-cli caidat ./duong-dan/goi.vi ten-goi
```

Windows có thể dùng trực tiếp:

```cmd
VPP.cmd caidat duong-dan\goi.vi ten-goi
```

Thư viện chuẩn là một package duy nhất tại `gói/thư viện`. Program mới nên
import module tiếng Việt nhỏ nhất cần dùng. Khi import theo tên module, tên có
khoảng trắng có thể để trần hoặc đặt trong dấu nháy; đường dẫn file có khoảng
trắng luôn phải đặt trong dấu nháy:

```vi
nhập cốt lõi;
nhập "vào ra";
nhập mạng;
nhập "mạng web";
nhập dữ liệu;
nhập "ứng dụng";
nhập "kiểm thử";
```

- `gói/thư viện/cốt lõi`: toán học, chuỗi, luận lý và validation.
- `gói/thư viện/vào ra`: tệp, cấu hình, thời gian và logging.
- `gói/thư viện/mạng`: HTTP client GET/POST/PUT/DELETE cùng HTTP server mức thấp.
- `gói/thư viện/mạng web`: REST helpers và JSON scalar an toàn, phụ thuộc `gói/thư viện/mạng`.
- `gói/thư viện/dữ liệu`: phân trang và database adapter.
- `gói/thư viện/ứng dụng`: chỉ lifecycle ứng dụng chung; không tự kéo web, HTTP hay data.
- `gói/thư viện/khởi động`: facade tiện dụng cho web, dữ liệu và ứng dụng full stack.
- `gói/thư viện/kiểm thử`: khẳng định cơ bản trong mã V++ (`khẳng định đúng`, `khẳng định sai`, `khẳng định bằng`, `khẳng định khác`).

`gói/thư viện/main.vi` là entrypoint đầy đủ. Mỗi module có `main.vi` tại
`gói/thư viện/<tên tiếng Việt>/main.vi`. Có thể import theo tên module như
trên, hoặc dùng đường dẫn tường minh khi cần module con, ví dụ
`nhập "gói/thư viện/mạng web/kiểm thử/api.vi";`. Các bản cài từ release đặt thư viện
chuẩn cạnh binary và installer tự cấu hình `VPP_HOME`, vì vậy các import này
vẫn hoạt động ngoài repository.

`kiểm thử` và `mạng web/kiểm thử/api.vi` là module tùy chọn, không được import
tự động bởi `main.vi`; mã production không bị kéo theo API kiểm thử.

Các module trên được bundle cùng V++; package manager xem `thư viện` là một
package và chưa tự resolve dependency/version cho module con.

HTTP server native hỗ trợ Linux, macOS và Windows.

Tạo backend tối giản:

```bash
vpp khởi tạo backend my-api
cd my-api
vpp application.vi
curl http://127.0.0.1:8080/health
```

Endpoint mẫu trả JSON boolean `true`.

### Module khởi động

Các entrypoint khởi động canonical có thể import bằng đường dẫn:

```vi
nhập "gói/thư viện/khởi động/web.vi";
nhập "gói/thư viện/khởi động/dữ liệu.vi";
nhập "gói/thư viện/khởi động/ứng dụng.vi";
```

- `web`: cốt lõi + vào ra + mạng web.
- `dữ liệu`: cốt lõi + vào ra + dữ liệu.
- `ứng dụng`: cốt lõi + vào ra + mạng web + dữ liệu + lifecycle.

Vì vậy hãy dùng `gói/thư viện/ứng dụng` khi chỉ cần lifecycle, và dùng
`gói/thư viện/khởi động/ứng dụng.vi` khi chủ ý cần full stack. Test request builders ở
`gói/thư viện/mạng web/kiểm thử/api.vi` không được import tự động bởi cả hai entrypoint.

Ba tên starter cũ `khởi động dữ liệu.vi`, `khởi động web.vi` và `khởi động ứng dụng.vi`
được giữ làm shim nhỏ trong cùng cây module; chúng không tạo thêm package hay
copy implementation.

## Cấu Trúc Chính

- `src/cli/main.cpp`: entrypoint của CLI
- `src/core/`: tiện ích dùng chung
- `src/frontend/`: lexer (token mang span), parser, AST và keyword map
- `src/compiler/`: semantic analysis, IR không kiểu, optimizer, direct bytecode emitter và compatibility legacy bridge
- `src/runtime/`: VM + native adapters (HTTP, file, DB)
- `src/tooling/`: formatter, linter, disassembler và AST/IR dump renderer
- `examples/`: ứng dụng mẫu chạy độc lập
- `templates/`: template do CLI scaffold sử dụng
- `src/tests/fixtures/`: dữ liệu/fixture cho regression test
- `src/tests/`: chương trình regression V++ (`.vi`) và expected output/fixture
- `test/`: source C++ cho CTest unit và CLI tooling checks
- `docs/`: bytecode, grammar, kiến trúc

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

Runtime vẫn dùng giá trị động, nên IR hiện chủ ý **không mang kiểu**. Dự án
chưa có chính sách typed IR; việc chọn dynamic, static hoặc gradual typing sẽ
được quyết định riêng trước khi thêm một lớp IR có kiểu. Backend bytecode cũ
được giữ sau một cầu nối tương thích, để pipeline mới không làm thay đổi hành
vi bytecode/VM đang có.

Các header pipeline được quy hoạch dưới
`include/vpp/frontend/{token,ast,parser}.h` và
`include/vpp/compiler/{semantic,ir,optimizer,pipeline}.h`. Xem
`docs/architecture.md` để biết ranh giới từng bước và trạng thái API.

## Tài Liệu

- `docs/architecture.md`
- `docs/bytecode.md`
- `docs/grammar.bnf`
- `docs/language-comparison.md`
- `docs/language-comparison-en.md`
