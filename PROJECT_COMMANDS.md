# V++ — Các lệnh phát triển bản tự chủ

```sh
make build
make test
./VPP phiên bản
./VPP build hello.vi -o hello
./VPP run hello.vi -- arg1 arg2
./VPP jit hello.vi
./VPP jit hello.vi --function cộng -- 19 23
make benchmark
cmake --install build-pure-vpp --prefix "$HOME/.local"
```

`vpp jit` sử dụng bộ JIT đã build một lần; mỗi nguồn được biên dịch thành mã máy
ngay trong bộ nhớ. Mặc định dùng tập i32; `./VPP jit --dynamic "$PWD/hello.vi"`
hỗ trợ runtime kiểu động và import module, không cần assembler/linker khi thực thi.
Đặt `VPP_JIT_PROFILE=1` để xem thống kê biên dịch lại hàm nóng trong chế độ dynamic.
Đặt `VPP_JIT_TIER=0` để chạy baseline native, không thu thập hồ sơ hoặc biên dịch lại.
Có thể gọi trực tiếp `build-pure-vpp/bin/vpp-jit hello.vi` hoặc xem `./VPP jit --help`.

Chi tiết: [Cài đặt](docs/installation.md). Build đầy đủ hiện hỗ trợ macOS ARM64.

<details>
<summary>Lệnh của kiến trúc host trước đây (tham khảo lịch sử)</summary>

# V++ — Các lệnh phát triển thường dùng

File này tập hợp các lệnh build, test, chạy V++, đóng gói/cài extension và các
lệnh terminal thường dùng khi phát triển dự án.

> Chạy các lệnh bên dưới từ thư mục gốc của repository.

```bash
cd "/Users/winbiru/V++"
```

## 1. Chuẩn bị môi trường

### macOS

```bash
xcode-select --install
brew install cmake

cmake --version
c++ --version
```

### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake curl

cmake --version
c++ --version
```

### Windows

Trong PowerShell/Terminal có Visual Studio C++ Build Tools:

```powershell
choco install cmake --yes
cmake --version
```

## 2. Build bằng CMake

### Debug / build mặc định

```bash
cmake -S . -B build
cmake --build build --parallel
```

Binary chính:

```text
build/bin/vpp-cli
```

Kiểm tra:

```bash
./build/bin/vpp-cli phiên bản
./build/bin/vpp-cli giúp đỡ
```

### Release

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel --target vpp-cli
```

Chạy bản Release:

```bash
./build-release/bin/vpp-cli phiên bản
```

### Build lại từ sạch

```bash
rm -rf build
cmake -S . -B build
cmake --build build --parallel
```

## 3. Build nhanh không qua CMake

Project có script build CLI trực tiếp bằng C++ compiler:

```bash
./scripts/macos/build-vpp-cli.sh
```

Binary được tạo tại:

```text
bin/vpp-cli
```

Chạy:

```bash
./bin/vpp-cli phiên bản
```

Windows:

```cmd
scripts\build-vpp-cli.bat
```

## 4. Build bằng Makefile

Build:

```bash
make build
```

Build + chạy test regression:

```bash
make
```

hoặc:

```bash
make check
```

Xem cấu hình Makefile hiện tại:

```bash
make show
```

Xóa output test tạm:

```bash
make clean
```

Xóa cả build directory:

```bash
make distclean
```

> `make bless` ghi lại expected output từ kết quả hiện tại. Chỉ dùng khi đã kiểm
> tra chắc chắn output mới là đúng.

```bash
make bless
```

## 5. Chạy test

Sau khi build bằng CMake:

```bash
ctest --test-dir build --output-on-failure
```

Xem chi tiết toàn bộ test:

```bash
ctest --test-dir build --output-on-failure --verbose --no-tests=error
```

Chạy regression V++ trực tiếp:

```bash
VPP_EXEC=./build/bin/vpp-cli ./run_tests.sh
```

Nếu build bằng `scripts/macos/build-vpp-cli.sh`:

```bash
VPP_EXEC=./bin/vpp-cli ./run_tests.sh
```

CI macOS, Linux và Windows dùng compiler và JIT trong gói V++ đã build.
Runner mặc định chạy các fixture đã được đánh dấu `jit: true` bằng
`vpp-jit --dynamic`; các fixture còn lại được build AOT một lần rồi thực thi.
Không fallback AOT khi một fixture JIT thất bại. Cách này giữ hợp đồng của
fixture chưa tương thích với giới hạn image JIT hoặc cần runtime bản địa.
Các kiểm tra compiler/linker/VM/i32, tooling và các API JIT độc lập vẫn chạy theo
hợp đồng riêng. Test từ chối biên dịch dùng `--check-dynamic`; test argv truyền
đối số sau đường dẫn nguồn trong `--dynamic`.

```bash
python3 scripts/quality/run_native_regression.py --compiler "$PACKAGE/bin/vpp" \
  --jit "$PACKAGE/bin/vpp-jit" --jobs 2
```

Để kiểm tra riêng đường biên dịch AOT, dùng `--backend aot`. Để thử nghiệm
JIT trên cả các nguồn chưa được xác nhận tương thích, dùng `--backend jit`.
Các tùy chọn
`--phase build/run`, `--artifacts`, `--batch-size`, `--module-suites` chỉ
phục vụ kiểm tra AOT và tương thích với artifact cũ.

## 6. Chạy chương trình V++

Chạy một file `.vi`:

```bash
./build/bin/vpp-cli scripts/test/program.vi
```

Hoặc dùng wrapper của project:

```bash
./VPP chạy scripts/test/program.vi
```

Các lệnh tooling thường dùng:

```bash
./build/bin/vpp-cli --giải-mã scripts/test/program.vi
./build/bin/vpp-cli --dump-ast scripts/test/program.vi
./build/bin/vpp-cli --dump-ir scripts/test/program.vi
./build/bin/vpp-cli --lint scripts/test/program.vi
./build/bin/vpp-cli --định-dạng scripts/test/program.vi
./build/bin/vpp-cli --định-dạng scripts/test/program.vi --in-place
./build/bin/vpp-cli --repl
./build/bin/vpp-cli --lsp
```

Scaffold project:

```bash
./build/bin/vpp-cli khởi tạo demo
./build/bin/vpp-cli khởi tạo backend my-api
```

Quản lý package:

```bash
./build/bin/vpp-cli cài đặt "./gói/chuẩn"
./build/bin/vpp-cli danh sách
./build/bin/vpp-cli thông tin "chuẩn"
./build/bin/vpp-cli kiểm tra "chuẩn"
./build/bin/vpp-cli thống kê
./build/bin/vpp-cli xóa mypkg
```

## 7. Extension V++ cho VS Code / Cursor / VSCodium

Extension nằm trực tiếp trong repository (`package.json`, `extension.js`,
`syntaxes/`, `themes/`, `language-configuration.json`). Project dùng
`scripts/vpp-lang` để cài và đóng gói extension.

### Xem trợ giúp

```bash
./scripts/vpp-lang help
```

### Cài command `vpp-lang` vào user PATH

```bash
./scripts/vpp-lang self-install
```

Sau đó có thể dùng:

```bash
vpp-lang help
vpp-lang list
```

Nếu `~/.local/bin` chưa nằm trong `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

### Cài extension vào VS Code

```bash
./scripts/vpp-lang install --editor vscode
```

Hoặc sau khi đã `self-install`:

```bash
vpp-lang install --editor vscode
```

### Cài extension vào Cursor

```bash
./scripts/vpp-lang install --editor cursor
```

### Cài extension vào VSCodium

```bash
./scripts/vpp-lang install --editor vscodium
```

### Cài vào mọi editor được hỗ trợ đã có trên máy

```bash
./scripts/vpp-lang install --editor all
```

### Xem extension đã cài ở đâu

```bash
./scripts/vpp-lang list
```

### Tạo package extension

```bash
./scripts/vpp-lang pack
```

File được tạo theo dạng:

```text
dist/vpp-language-<version>.vlang
```

Ví dụ với version hiện tại:

```text
dist/vpp-language-0.1.2.vlang
```

### Gỡ extension

VS Code:

```bash
./scripts/vpp-lang uninstall --editor vscode
```

Cursor:

```bash
./scripts/vpp-lang uninstall --editor cursor
```

Gỡ command `vpp-lang` khỏi user PATH:

```bash
./scripts/vpp-lang self-uninstall
```

### Chạy extension ở chế độ phát triển

1. Mở repository trong VS Code.
2. Nhấn `F5`.
3. Trong cửa sổ **Extension Development Host**, mở file `.vi` hoặc `.vvm`.

Sau khi sửa grammar/theme/extension và đã cài local, reload VS Code bằng Command
Palette:

```text
Developer: Reload Window
```

## 8. Coverage

Chạy coverage gate giống CI:

```bash
./scripts/quality/run-coverage.sh
```

Thử threshold khác:

```bash
VPP_COVERAGE_MINIMUM=60 ./scripts/quality/run-coverage.sh
```

## 9. clang-tidy

```bash
cmake -S . -B build-tidy \
  -DBUILD_TESTS=OFF \
  -DVPP_ENABLE_CLANG_TIDY=ON

cmake --build build-tidy --parallel
```

## 10. Benchmark

```bash
cmake -S . -B build-benchmark \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=ON

cmake --build build-benchmark \
  --target vpp-benchmark-baseline \
  --parallel

./build-benchmark/bin/vpp-benchmark-baseline
```

## 11. Sanitizer

```bash
cmake -S . -B build-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"

cmake --build build-sanitized --parallel
ctest --test-dir build-sanitized --output-on-failure --no-tests=error
```

## 12. Các lệnh Git thường dùng cho project

```bash
git status
git diff
git diff --check
git log --oneline -10
```

Xem file thay đổi:

```bash
git status --short
```

## 13. Kiểm tra nhanh trước khi commit

Luồng khuyến nghị:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure --no-tests=error
git diff --check
git status --short
```

Nếu thay đổi extension:

```bash
./scripts/vpp-lang pack
./scripts/vpp-lang install --editor vscode
./scripts/vpp-lang list
```

## 14. Windows CMake + test

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --verbose --no-tests=error
```

Binary thường nằm tại một trong các vị trí:

```text
build/bin/Release/vpp-cli.exe
build/Release/vpp-cli.exe
build/vpp-cli.exe
```

## 15. Lệnh nhanh nhất cho công việc hằng ngày

Build + test:

```bash
make
```

Chạy một chương trình:

```bash
./build/bin/vpp-cli file.vi
```

Build CLI nhanh khi không có CMake:

```bash
./scripts/macos/build-vpp-cli.sh
```

Đóng gói extension:

```bash
./scripts/vpp-lang pack
```

Cài lại extension VS Code:

```bash
./scripts/vpp-lang install --editor vscode
```


</details>
