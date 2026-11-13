# Cài đặt V++ (bản V++/ASM tự chủ)

Bản hiện tại hỗ trợ bootstrap compiler đầy đủ trên **macOS ARM64 (Apple Silicon)**. CMake 3.20+, công cụ Xcode Command Line Tools (`xcode-select --install`) và shell Bash là các điều kiện build. Clang dùng để dịch Assembly của nền tảng và ở bước bootstrap seed cũ; không biên dịch C/C++. Compiler sinh ra dùng đường ARM64/Mach-O trực tiếp.

```sh
cmake -S . -B build-pure-vpp
cmake --build build-pure-vpp --parallel
ctest --test-dir build-pure-vpp --output-on-failure
./VPP phiên bản
./VPP build hello.vi -o hello
./hello
```

Cài đặt tại một prefix tùy chọn:

```sh
cmake --install build-pure-vpp --prefix "$HOME/.local"
"$HOME/.local/bin/vpp" phiên bản
```

Hoặc dùng `bash scripts/unix/install-vpp.sh "$HOME/.local"`. Lệnh này không sửa shell profile. Thêm `$HOME/.local/bin` vào `PATH` nếu cần.

Hai seed được kiểm tra SHA-256, chữ ký và thư viện động trước khi build. Đọc [bootstrap/README.md](../bootstrap/README.md) để thay seed. Linux/Windows và các dịch vụ của host cũ (VM CLI, LSP, package tooling) cần được chuyển sang native trước khi được phát hành ở nhánh này. Các bản phát hành cũ và tài liệu lịch sử không mô tả cách build của nhánh hiện tại.
