# V++

V++ là một bộ compiler + virtual machine thử nghiệm cho một ngôn ngữ lập trình kiểu Việt hoá. Repo này tập trung vào bytecode rõ ràng, luồng điều khiển minh bạch và khả năng kiểm thử tốt.

## Nhanh Chóng

```bash
mkdir -p cmake-build-debug
cd cmake-build-debug
cmake ..
make -j
```

Chạy một chương trình:

```bash
./bin/vpp-cli ../src/tests/program.vi
```

Chạy toàn bộ test:

```bash
cd /Users/winbiru/V++
./run_tests.sh
```

## Cài Nhanh Không Cần Clone

Ban co the tai file da build san tu GitHub Release.

### Linux

```bash
curl -L https://github.com/winbiru/VXX/releases/latest/download/vpp-linux-x64.tar.gz -o vpp-linux-x64.tar.gz
tar -xzf vpp-linux-x64.tar.gz
chmod +x ./vpp
./vpp giúp đỡ
```

### macOS

```bash
curl -L https://github.com/winbiru/VXX/releases/latest/download/vpp-macos.tar.gz -o vpp-macos.tar.gz
tar -xzf vpp-macos.tar.gz
chmod +x ./vpp
./vpp giúp đỡ
```

### Windows (PowerShell)

```powershell
Invoke-WebRequest -Uri "https://github.com/winbiru/VXX/releases/latest/download/vpp-windows-x64.zip" -OutFile "vpp-windows-x64.zip"
Expand-Archive -Path "vpp-windows-x64.zip" -DestinationPath ".\vpp-bin" -Force
.\vpp-bin\vpp.exe giúp đỡ
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
./VPP cài đặt ./lib/stdlib.vi stdlib
./VPP caidat ./duong-dan/goi.vi ten-goi
./VPP xóa ten-goi
./VPP thông tin stdlib
./VPP kiểm tra stdlib

./bin/vpp-cli --giải-mã example.vi
./bin/vpp-cli --lint example.vi
./bin/vpp-cli --định-dạng example.vi
./bin/vpp-cli --định-dạng example.vi --in-place
./bin/vpp-cli --repl
./bin/vpp-cli --lsp
./bin/vpp-cli khởi tạo demo
./bin/vpp-cli cài đặt ./lib/stdlib.vi stdlib
./bin/vpp-cli danh sách
./bin/vpp-cli xóa stdlib
./bin/vpp-cli thông tin stdlib
./bin/vpp-cli kiểm tra stdlib
./bin/vpp-cli thống kê
./bin/vpp-cli pkg khởi tạo demo
./bin/vpp-cli pkg thêm lib.vi mypkg
./bin/vpp-cli pkg xóa mypkg
./bin/vpp-cli pkg thông tin mypkg
./bin/vpp-cli pkg kiểm tra mypkg
./bin/vpp-cli pkg danh sách
```

`cài đặt` là lệnh chính cho package manager, và `caidat` cũng được hỗ trợ:

```bash
./bin/vpp-cli caidat ./duong-dan/goi.vi ten-goi
```

Windows có thể dùng trực tiếp:

```cmd
VPP.cmd caidat duong-dan\goi.vi ten-goi
```

`nhập "stdlib";` vẫn hoạt động, và package local sẽ được tìm trong `packages/<name>/main.vi`.

## Cấu Trúc Chính

- `src/cli/main.cpp`: entrypoint của CLI
- `src/frontend/`: lexer + keyword map
- `src/compiler/`: compile tokens thành bytecode
- `src/vm/`: runtime VM
- `docs/`: bytecode, grammar, kiến trúc
- `src/tests/`: chương trình kiểm thử

## Tài Liệu

- `docs/architecture.md`
- `docs/bytecode.md`
- `docs/grammar.bnf`
- `docs/language-comparison.md`
- `docs/language-comparison-en.md`

## Ghi Chú

- Đây là project thử nghiệm, chưa cam kết tương thích ổn định lâu dài.
- Bytecode, parser và CLI vẫn đang tiếp tục hoàn thiện.