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

Mở Windows PowerShell 5.1 hoặc PowerShell 7 và chạy từng lệnh dưới đây.
Sao chép URL nguyên dạng trong khối lệnh, không dùng cú pháp Markdown `[URL](URL)`.

```powershell
Invoke-WebRequest -Uri "https://github.com/winbiru/VXX/releases/latest/download/vpp-windows-x64.zip" -OutFile "vpp-windows-x64.zip"
Expand-Archive -Path "vpp-windows-x64.zip" -DestinationPath ".\vpp-bin" -Force
$previousPolicy = Get-ExecutionPolicy -Scope Process
try {
    Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
    & .\vpp-bin\install-vpp.ps1
} finally {
    Set-ExecutionPolicy -Scope Process -ExecutionPolicy $previousPolicy -Force
}
vpp phiên bản
vpp giúp đỡ
```

Bộ cài tự lưu `PATH`/`VPP_HOME` cho người dùng và cập nhật ngay cửa sổ PowerShell
đang gọi script: không cần gõ `$env:Path` thủ công. Đường dẫn mặc định là
`%LOCALAPPDATA%\Programs\VPP`. Cài lại không thêm trùng đường dẫn, và bản vừa cài
được ưu tiên trong PATH. Đoạn lệnh trên chỉ tạm đổi execution policy của tiến trình
hiện tại và khôi phục sau khi cài; Group Policy của tổ chức vẫn có hiệu lực.

Bộ cài cũng tự chuyển console sang UTF-8 (`chcp 65001`), cấu hình
`[Console]::OutputEncoding` và `$OutputEncoding` trước khi chạy V++ để hiển thị
đúng tiếng Việt. Khi gọi `.ps1` trực tiếp, cấu hình này tiếp tục có hiệu lực trong
PowerShell hiện tại. Đây là thiết lập của phiên, không phải thiết lập toàn máy;
CLI bản mới tự xử lý UTF-8 khi chạy trong các cửa sổ khác.

Nếu muốn chạy bộ cài bằng file batch:

```powershell
.\vpp-bin\install-vpp.cmd
```

File `.cmd` cũng tự lưu biến môi trường cho người dùng, nhưng tiến trình con không
thể sửa môi trường của PowerShell cha. Sau cách cài này, đóng và mở lại **toàn bộ
ứng dụng terminal hoặc VS Code** để nhận PATH mới (chỉ mở tab mới có thể vẫn kế
thừa PATH cũ). Sau đó dùng `vpp phiên bản` để kiểm tra.

Source hiện tại lưu bộ cài dưới dạng UTF-8 có BOM để Windows PowerShell 5.1 đọc
đúng thư mục `gói`. Build MSVC tĩnh mặc định tích hợp C++ runtime, và CLI tự dùng
UTF-8 khi hiển thị console rồi khôi phục code page khi thoát. Những sửa đổi này
chỉ có trong release được build từ source mới; file ZIP đã phát hành không tự cập nhật.

#### Gỡ cài đặt Windows

Với bản release mới có bộ gỡ, chạy trong PowerShell hoặc CMD:

```powershell
vpp gỡ cài đặt
```

Nếu terminal chưa nhận PATH, dùng đường dẫn đầy đủ:

```powershell
& "$env:LOCALAPPDATA\Programs\VPP\vpp.exe" gỡ cài đặt
```

Bộ gỡ xóa executable, thư viện `gói`, `templates`, `examples` và các file bộ gỡ;
CLI khởi động bộ gỡ riêng rồi thoát để Windows cho phép xóa `vpp.exe`.
đồng thời bỏ đường dẫn cài khỏi PATH của người dùng. `VPP_HOME` chỉ bị xóa nếu
đang trỏ tới bản cài này. Các file khác và dự án bên ngoài được giữ lại.
Đóng và mở lại terminal/VS Code sau khi gỡ. Extension VS Code cần gỡ riêng.
Bản cũ chưa có lệnh này cần cập nhật bộ cài trước.

#### Khắc phục với bộ cài Windows cũ

Nếu PowerShell báo `vpp is not recognized` hoặc `The term 'vpp' is not recognized`,
chạy nguyên khối sau để lưu lại `PATH`/`VPP_HOME` cho tài khoản Windows và cập nhật
ngay cửa sổ hiện tại. Nếu cài ở vị trí tùy chỉnh, sửa `$vppDir` cho đúng.

```powershell
$vppDir = Join-Path $env:LOCALAPPDATA "Programs\VPP"

if (Test-Path "$vppDir\vpp.exe") {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $otherPaths = @($userPath -split ";" | Where-Object {
        $_ -and $_.TrimEnd("\") -ine $vppDir
    })

    [Environment]::SetEnvironmentVariable(
        "Path", ((@($vppDir) + $otherPaths) -join ";"), "User"
    )
    [Environment]::SetEnvironmentVariable("VPP_HOME", $vppDir, "User")

    $env:Path = "$vppDir;$env:Path"
    $env:VPP_HOME = $vppDir

    vpp phiên bản
    $testFile = Join-Path $env:USERPROFILE "kiem-tra-vpp.vi"
    if (Test-Path -LiteralPath $testFile -PathType Leaf) {
        vpp chạy $testFile
    } else {
        Write-Host "Môi trường đã được cấu hình. Chưa có file thử: $testFile"
    }
} else {
    Write-Host "Chưa tìm thấy V++. Hãy chạy lại bộ cài install-vpp.cmd."
}
```

Các cửa sổ terminal/VS Code khác đang mở cần khởi động lại để nhận môi trường mới.
Khi chạy file, dùng `vpp chạy .\kiem-tra-vpp.vi`; không thêm `\` sau đuôi `.vi`.
Sửa source không tự cập nhật bộ cài đã tải: cần cài release mới để nhận sửa đổi.

Nếu script báo `running scripts is disabled`, dùng `install-vpp.cmd` như trên.
Nếu đường dẫn `gói` bị đọc thành `gÃ³i`, sửa mã hóa script rồi cài lại:

```powershell
$scriptPath = (Resolve-Path ".\vpp-bin\install-vpp.ps1").Path
$content = [System.IO.File]::ReadAllText($scriptPath, [System.Text.Encoding]::UTF8)
$utf8Bom = New-Object System.Text.UTF8Encoding($true)
[System.IO.File]::WriteAllText($scriptPath, $content, $utf8Bom)
.\vpp-bin\install-vpp.cmd
```

Nếu `vpp` không in gì và `$LASTEXITCODE` là `-1073741515` (`0xC0000135`), Windows
không tải được DLL cần thiết. Bản cũ có thể cần
[Visual C++ Runtime x64 của Microsoft](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist):

```powershell
Invoke-WebRequest -Uri "https://aka.ms/vc14/vc_redist.x64.exe" -OutFile "$env:TEMP\vc_redist.x64.exe"
Start-Process -FilePath "$env:TEMP\vc_redist.x64.exe" -ArgumentList "/install" -Wait
vpp phiên bản
```

Chọn Install hoặc Repair và làm theo hướng dẫn. Nếu vẫn lỗi, mở `vpp.exe` từ
Explorer để xem tên DLL bị thiếu. Nếu bản cũ chạy được nhưng tiếng Việt bị lỗi dấu:

```powershell
chcp 65001
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$OutputEncoding = [Console]::OutputEncoding
vpp giúp đỡ
```

Thiết lập UTF-8 thủ công này chỉ áp dụng cho cửa sổ hiện tại.

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
